param(
    [string]$FreeDosZip = 'E:\MHS-Repository\HamsterOS\build\freedos\FDT2607-FloppyEdition.zip',
    [string]$Boulder = 'E:\MHS-Repository\HamsterOS\dos\Boulder.exe',
    [string]$Compiler = '',
    [string]$ToolchainRoot = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $projectRoot 'build\dos-work'
$source = Join-Path $work 'source'
$package = Join-Path $work 'package'
$previous = Join-Path $work 'previous-package'
$destination = Join-Path $projectRoot 'DosTest'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

function Assert-WorkspacePath([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside this workspace: $fullPath"
    }
    for ($part = $fullPath; $part -ne $projectRoot; $part = Split-Path -Parent $part) {
        if ((Test-Path -LiteralPath $part) -and
            ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing to replace a linked path: $part"
        }
    }
}

function Remove-PackageTree([string]$Path) {
    Assert-WorkspacePath $Path
    if (Test-Path -LiteralPath $Path) {
        $links = Get-ChildItem -LiteralPath $Path -Force -Recurse |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
        if ($links) { throw "Refusing to remove a package containing linked files: $Path" }
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Assert-Hash([string]$Path, [string]$Expected) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected) {
        throw "Checksum mismatch: $Path"
    }
}

foreach ($inputFile in @($FreeDosZip, $Boulder)) {
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) { throw "Missing input: $inputFile" }
}
$FreeDosZip = [IO.Path]::GetFullPath($FreeDosZip)
$Boulder = [IO.Path]::GetFullPath($Boulder)
if ($Compiler) { $Compiler = [IO.Path]::GetFullPath($Compiler) }
if ($ToolchainRoot) { $ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot) }
foreach ($path in @($work, $package, $previous, $destination)) { Assert-WorkspacePath $path }
New-Item -ItemType Directory -Path $work -Force | Out-Null

# Recover an interrupted directory swap before starting the next build.
if (Test-Path -LiteralPath $previous) {
    if (Test-Path -LiteralPath $destination) { Remove-PackageTree $previous }
    else { Move-Item -LiteralPath $previous -Destination $destination }
}

Push-Location $projectRoot
try {
    $versionJson = & node scripts/firmware-version.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Firmware version validation failed.' }
    $version = $versionJson | ConvertFrom-Json
    # A newer GUI backend patch cannot be applied over an older overlay.
    # Refresh only this disposable build clone; keep its Git object cache
    # and the current DosTest kit while regenerating the selected version.
    $overlayState = Join-Path $source '.mhs-custom-gui.json'
    if (Test-Path -LiteralPath $overlayState) {
        $oldOverlay = Get-Content -LiteralPath $overlayState -Raw | ConvertFrom-Json
        if ($oldOverlay.snapshotDigest -ne $version.gui.snapshotDigest) {
            Assert-WorkspacePath $source
            $resolvedSource = (Resolve-Path -LiteralPath $source).Path
            if ($resolvedSource -ne (Join-Path $projectRoot 'build\dos-work\source')) {
                throw 'Refusing to refresh a source path outside the dedicated DOS build clone.'
            }
            $links = Get-ChildItem -LiteralPath $source -Force -Recurse |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
            if ($links) { throw 'Refusing to refresh a build clone containing linked paths.' }
            $origin = & git -C $source remote get-url origin
            if ($LASTEXITCODE -ne 0 -or $origin -ne 'https://github.com/SensoriumEmbedded/TeensyROM.git') {
                throw 'The disposable DOS build clone has an unexpected origin.'
            }
            Write-Host 'Refreshing the generated DOS build source for the newly selected GUI.'
            Invoke-Native git @('-C', $source, 'reset', '--hard', 'HEAD')
            Invoke-Native git @('-C', $source, 'clean', '-fdx')
        }
    }
    $freecom = Join-Path $work 'freecom'
    $command = Join-Path $freecom 'COMMAND.COM'
    $kssf = Join-Path $freecom 'KSSF.COM'
    if (-not ((Test-Path -LiteralPath $command) -and (Test-Path -LiteralPath $kssf))) {
        Invoke-Native python @('dos/tools/fetch_freecom.py', '--output', $freecom)
    }
    # The image builder validates the pinned hashes even when FreeCOM is cached.
    $image = Join-Path $work 'DOSVM.IMG'
    $imageManifest = Join-Path $work 'DOSVM.JSON'
    $cartridge = Join-Path $work 'DOSVM.CRT'
    $cartridgeManifest = Join-Path $work 'DOSVM.CRT.JSON'
    $bootBank = Join-Path $work 'dosvm-bootbank.bin'
    $terminalManifest = Join-Path $work 'dosvm-terminal.json'
    $bios = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios'
    Invoke-Native python @('dos/tools/build_freedos_boulder_image.py',
        '--source-zip', $FreeDosZip, '--command', $command, '--kssf', $kssf,
        '--boulder', $Boulder, '--output', $image, '--manifest', $imageManifest)
    Invoke-Native node @('dos/tools/build_dosvm_terminal.mjs',
        '--output-prg', (Join-Path $work 'dosvm-terminal.prg'),
        '--output-boot-bank', $bootBank, '--manifest', $terminalManifest)
    Invoke-Native python @('dos/tools/build_dosvm_cartridge.py', '--boot-bank', $bootBank,
        '--bios', $bios, '--output', $cartridge, '--manifest', $cartridgeManifest)
    Invoke-Native node @('dos/tests/mpe5_c64_boot_test.mjs', '--crt', $cartridge,
        '--manifest', $terminalManifest, '--out', (Join-Path $work 'c64-boot'))

    $firmware = Join-Path (Join-Path $work 'firmware') $version.filename
    $firmwareManifest = Join-Path $work 'manifests/firmware-build.json'
    # Require newly produced output; an earlier successful build cannot pass this gate.
    foreach ($staleFile in @($firmware, $firmwareManifest)) {
        if (Test-Path -LiteralPath $staleFile) { Remove-Item -LiteralPath $staleFile -Force }
    }
    $buildArguments = @{ OutputRoot = $work }
    if (Test-Path -LiteralPath $source) { $buildArguments.SourcePath = $source }
    if ($ToolchainRoot) { $buildArguments.ToolchainRoot = $ToolchainRoot }
    & ./scripts/build-firmware.ps1 @buildArguments
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
    $built = Get-Content -LiteralPath $firmwareManifest -Raw | ConvertFrom-Json
    if ($built.firmwareFilename -ne $version.filename) { throw 'Firmware version changed during the build.' }
    Assert-Hash $firmware $built.sha256
    Assert-Hash 'firmware-version.json' $built.versionConfiguration.sha256
    $dosSourceManifest = Join-Path $work 'manifests/native-dos-sources.json'
    foreach ($entry in (Get-Content -LiteralPath $dosSourceManifest -Raw | ConvertFrom-Json)) {
        Assert-Hash (Join-Path 'engine/native-dos' $entry.file) $entry.sha256
    }
    & ./dos/tools/test_mpe5_direct_memory.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 direct RAM backend regression failed.' }
    & ./dos/tools/test_mpe5_ram2_layout.ps1 -Source $source `
        -Elf (Join-Path $source 'Source/Teensy/MinimalBoot/build/MinimalBoot.ino.elf')
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 RAM2 ownership gate failed.' }

    Remove-PackageTree $package
    $packageFirmware = Join-Path $package 'firmware'
    New-Item -ItemType Directory -Path $packageFirmware -Force | Out-Null
    Copy-Item -LiteralPath $firmware -Destination $packageFirmware
    Invoke-Native python @('dos/tools/assemble_sd_card.py', '--cartridge', $cartridge,
        '--cartridge-manifest', $cartridgeManifest, '--image', $image,
        '--image-manifest', $imageManifest, '--output', (Join-Path $package 'sd-card'))

    $packagedImage = Join-Path $package 'sd-card/DOSVM/DOSVM.IMG'
    Assert-Hash $packagedImage (Get-Content -LiteralPath $imageManifest -Raw | ConvertFrom-Json).image.sha256
    $crtRecord = Get-Content -LiteralPath $cartridgeManifest -Raw | ConvertFrom-Json
    Assert-Hash (Join-Path $package 'sd-card/DOSVM.CRT') $crtRecord.cartridgeSha256
    Assert-Hash $bios $crtRecord.biosSha256
    & ./dos/tools/test_mpe5_publication.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 publication regression failed.' }
    & ./dos/tools/test_mpe5_vm.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Packaged VM host acceptance failed.' }
    & ./dos/tools/test_mpe5_keyboard.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Held PC keyboard acceptance failed.' }
    & ./dos/tools/test_mpe5_boulder_controls.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Boulder movement and controls acceptance failed.' }
    & ./dos/tools/test_mpe5_video.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'CGA video and BIOS timer acceptance failed.' }
    & ./dos/tools/test_mpe5_speaker.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'PC speaker acceptance failed.' }
    & ./dos/tools/test_mpe5_firmware.ps1 -Source $source -Image $packagedImage `
        -Cartridge (Join-Path $package 'sd-card/DOSVM.CRT') -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Integrated MPE5 firmware acceptance failed.' }
    & ./dos/tools/test_mpe5_latency.ps1 -Source $source -Image $packagedImage `
        -Cartridge (Join-Path $package 'sd-card/DOSVM.CRT') -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'DOS foreground latency acceptance failed.' }
    Invoke-Native node @('dos/tests/mpe5_c64_wire_test.mjs')
    Invoke-Native python @('dos/tools/render_c64_text.py')
    Invoke-Native node @('dos/tests/mpe5_c64_wire_test.mjs', '--scenario', 'graphics',
        '--expected-planes', (Join-Path $work 'boulder-firmware-planes.bin'),
        '--frame', (Join-Path $work 'boulder-frame.json'))
    Invoke-Native python @('dos/tools/render_c64_text.py',
        '--planes', (Join-Path $work 'boulder-c64-planes.bin'),
        '--frame', (Join-Path $work 'boulder-frame.json'),
        '--output', (Join-Path $work 'boulder-screen.png'))
    # Keep verification reports and rendered host screens in the disposable
    # build workspace. The hardware kit contains only files needed to run it.
    foreach ($metadata in @(
        (Join-Path $package 'sd-card/DOSVM/DOSVM.CRT.JSON'),
        (Join-Path $package 'sd-card/DOSVM/DOSVM.JSON')
    )) {
        if (Test-Path -LiteralPath $metadata) { Remove-Item -LiteralPath $metadata -Force }
    }

    $title = (Get-Content -LiteralPath $terminalManifest -Raw | ConvertFrom-Json).diagnosticTitle
    $stackReserveText = '{0:N0}' -f [uint64]$built.minimalBootStackReserveBytes
    $ram2HeapText = '{0:N0}' -f [uint64]$built.minimalBootRam2HeapReserveBytes
    $readme = @"
# Latest DOSVM test build

Built $([DateTime]::UtcNow.ToString('u')) with MPE firmware $($version.version).

1. Flash firmware/$($version.filename).
2. Copy the contents of sd-card/ to the SD card root.
3. Launch DOSVM.CRT. Its diagnostic title is: $title
4. Look for the FreeDOS C:\> prompt, type DIR, and check for BOULDER.EXE.
5. Type PCTONE for a short speaker test, then BOULDER for CGA graphics.
   Space skips the intro, then press Shift to start the game. Cursor keys move,
   Shift grabs, and Space pauses during play. Port 2 joystick directions
   act as cursor keys; fire acts as Shift. Physical play needs testing.

R16 runs on the standard TeensyROM without optional PSRAM. Guest addresses
00000h-7FFFFh map directly onto all 512 KiB of Teensy RAM2; there is no page
cache and no DOSVM.SWP. The virtual C: disk at /DOSVM/DOSVM.IMG stays
read-only. At the first prompt FreeDOS has about 374 KiB free; after repeated
DIR commands the validated free block remains 357,824 bytes (about 349 KiB).

RAM2 is exclusive guest memory for the life of DOS. Leaving bank 58 or using
the cartridge button requests a complete Teensy reboot, which returns to the
GUI with normal firmware memory restored. Update the firmware and DOSVM.CRT
together. The linked firmware retains a $stackReserveText-byte stack reserve, and the
post-link gate proves every live DOS, disk and transport object is in RAM1.
Before takeover, the shared native arena leaves $ram2HeapText bytes available
to the normal RAM2 heap. DOS then seals the handoff and owns all 512 KiB.

The CPU is built at O3, keeps a 25,000-instruction ceiling, and yields early
for input, display acknowledgements and four-sector disk boundaries. R16
serves instruction fetches and operands directly from RAM2/F000 instead of
routing them through the generic span callbacks. Interleaved host A/B runs of
the identical guest work measured 1.86x faster boot and 1.96x faster DIR than
R15. The two small operand helpers occupy 848 bytes of ITCM including linker
alignment, without adding a FlexRAM bank or reducing the $stackReserveText-byte
stack reserve. These are controlled host/link results, not a claimed physical
clock rate.

R16 retains CGA modes 4/5 (160x200 C64 multicolour), mode 6 (320x200 hires),
and PC speaker tones through SID voice 1. DOS text stays 320x200 hires with
40 visible columns. Shift/cursor releases are held for at least 550,000 guest
instructions so a quick physical tap reaches games with sparse polling;
printable input retains its short 512-instruction cadence. Port 2 directions
act as cursors and fire acts as Shift. Tandy 16-colour video is planned but is
not in this test build.

The package passed the C64 CPU boot audit, direct-memory and linked-RAM2
ownership gates, signed/unsigned-char VM tests, integrated firmware execution,
publication checks and C64 wire replay. The integrated run covers two
reset-separated FreeDOS boots, DIR, repeated letters, Backspace, PCTONE,
Boulder title/gameplay rendering and movement, plus a cold Sierra launch.
Pending packets remain immutable while the guest runs. The latency gate proves
prompt ACK/input interruption at modeled slow instruction rates. Physical
speed, stability and gameplay still need this exact firmware/CRT pair tested
on the cartridge.
See dos/HARDWARE-TEST.md in the repository for the hardware acceptance steps.

DosTest is replaced by the next successful test build. SHA256SUMS.txt records
the four required files in this compact hardware kit.
"@
    [IO.File]::WriteAllText((Join-Path $package 'README.md'), $readme + [Environment]::NewLine, $utf8)
    $checksums = Get-ChildItem -LiteralPath $package -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($package.Length + 1).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }
    [IO.File]::WriteAllLines((Join-Path $package 'SHA256SUMS.txt'), [string[]]$checksums, $utf8)

    # Both directories are on the same volume; keep the current kit until every gate passes.
    foreach ($path in @($package, $previous, $destination)) { Assert-WorkspacePath $path }
    if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $previous }
    try { Move-Item -LiteralPath $package -Destination $destination }
    catch {
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $destination }
        throw
    }
    Remove-PackageTree $previous
    Write-Host "Latest DOSVM test build: $destination"
}
finally { Pop-Location }

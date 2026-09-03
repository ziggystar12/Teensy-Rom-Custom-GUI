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

    Remove-PackageTree $package
    $packageFirmware = Join-Path $package 'firmware'
    New-Item -ItemType Directory -Path $packageFirmware -Force | Out-Null
    Copy-Item -LiteralPath $firmware, $firmwareManifest, $dosSourceManifest -Destination $packageFirmware
    Copy-Item -LiteralPath (Join-Path $work 'firmware/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex') -Destination $packageFirmware
    Invoke-Native python @('dos/tools/assemble_sd_card.py', '--cartridge', $cartridge,
        '--cartridge-manifest', $cartridgeManifest, '--image', $image,
        '--image-manifest', $imageManifest, '--output', (Join-Path $package 'sd-card'))

    $packagedImage = Join-Path $package 'sd-card/DOSVM/DOSVM.IMG'
    Assert-Hash $packagedImage (Get-Content -LiteralPath $imageManifest -Raw | ConvertFrom-Json).image.sha256
    $crtRecord = Get-Content -LiteralPath $cartridgeManifest -Raw | ConvertFrom-Json
    Assert-Hash (Join-Path $package 'sd-card/DOSVM.CRT') $crtRecord.cartridgeSha256
    Assert-Hash $bios $crtRecord.biosSha256
    & ./dos/tools/test_mpe5_paged_memory.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 paged memory regression failed.' }
    & ./dos/tools/test_mpe5_publication.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 publication regression failed.' }
    & ./dos/tools/test_mpe5_vm.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Packaged VM host acceptance failed.' }
    & ./dos/tools/test_mpe5_video.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'CGA video and BIOS timer acceptance failed.' }
    & ./dos/tools/test_mpe5_speaker.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'PC speaker acceptance failed.' }
    & ./dos/tools/test_mpe5_firmware.ps1 -Source $source -Image $packagedImage `
        -Cartridge (Join-Path $package 'sd-card/DOSVM.CRT') -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Integrated MPE5 firmware acceptance failed.' }
    Invoke-Native node @('dos/tests/mpe5_c64_wire_test.mjs')
    Invoke-Native python @('dos/tools/render_c64_text.py')
    Invoke-Native node @('dos/tests/mpe5_c64_wire_test.mjs', '--scenario', 'graphics',
        '--expected-planes', (Join-Path $work 'boulder-firmware-planes.bin'),
        '--frame', (Join-Path $work 'boulder-frame.json'))
    Invoke-Native python @('dos/tools/render_c64_text.py',
        '--planes', (Join-Path $work 'boulder-c64-planes.bin'),
        '--frame', (Join-Path $work 'boulder-frame.json'),
        '--output', (Join-Path $work 'boulder-screen.png'))
    Copy-Item -LiteralPath (Join-Path $work 'dos-screen.png') -Destination (Join-Path $package 'host-screen.png')
    Copy-Item -LiteralPath (Join-Path $work 'boulder-screen.png') -Destination $package
    foreach ($proof in @('dos-firmware-result.json','dos-c64-wire-result.json','boulder-c64-wire-result.json')) {
        Copy-Item -LiteralPath (Join-Path $work $proof) -Destination $package
    }

    $title = (Get-Content -LiteralPath $terminalManifest -Raw | ConvertFrom-Json).diagnosticTitle
    $readme = @"
# Latest DOSVM test build

Built $([DateTime]::UtcNow.ToString('u')) with MPE firmware $($version.version).

1. Flash firmware/$($version.filename).
2. Copy the contents of sd-card/ to the SD card root.
3. Launch DOSVM.CRT. Its diagnostic title is: $title
4. Look for the FreeDOS C:\> prompt, type DIR, and check for BOULDER.EXE.
5. Type PCTONE for a short speaker test, then BOULDER for CGA graphics.
   Space advances Boulder's title in the host test; physical play needs testing.

This build runs on the standard TeensyROM configuration without optional
PSRAM. FreeDOS gets 640 KiB conventional RAM through a 148 KiB page cache
in unused cartridge RAM. /DOSVM/DOSVM.SWP is the separate 1,185,792-byte
scratch backing file; copy it with the other SD files and leave the card
writable. Old scratch contents are discarded logically on every launch.
The virtual C: disk, /DOSVM/DOSVM.IMG, remains read-only.

R12 adds CGA modes 4/5 (160x200 C64 multicolour) and mode 6 (320x200 hires),
plus PC speaker tones through SID voice 1. DOS text stays 320x200 hires.
SID pitch is tuned for NTSC; PAL machines will play slightly lower.
The loader now says MHS DOSVM; update both firmware and CRT together.
The BIOS clock now advances: its previous frozen value stalled Boulder
countdowns and audio. Host tests reach its title, then a cave after Space.
The video workspace reuses BIOS staging memory; drawing adds no SD reads.

The build retains the larger resident cache and bounded VM work while the C64
displays an already-published packet. Pending packets remain immutable;
runtime failures are reported after ACK. Failed scratch-page transfers
are retried once at the same offset. Detailed runtime error codes replace
the generic05 error and preserve the failing address and guest CS:IP.
R10 reached a prompt on hardware but later failed after VER/SETUP; the
exact later hardware failure has not been reproduced in the host tests.
The VM tests run with both char defaults and exercise VER/SETUP/VER.
SETUP is the bundled FreeDOS installer, which currently reports environment
errors and aborts in the host test. The Boulder test proves title/cave output,
Space input and speaker activity; full keyboard gameplay remains unverified.

The package passed the C64 CPU boot audit, paged native VM acceptance, publication
regressions, integrated firmware host acceptance, and C64 wire replay. Those
checks include no-PSRAM boots, stale RAM/scratch contents, Sierra relaunch,
the returned prompt, DIR, keyboard, disk, all 1,000 initial cells,
hires frame completion, and idle refresh. The replay runs the actual terminal
and verifies C64 keyboard-matrix DIR and Return messages.
This build has not been verified on hardware.
host-screen.png is the completed no-PSRAM host run replayed through the C64 terminal.
boulder-screen.png is the CGA capture replayed through that same terminal.
See dos/HARDWARE-TEST.md in the repository for the hardware acceptance steps.
The official stock restore image is also included in firmware/.

DosTest is replaced by the next successful test build. SHA256SUMS.txt records
this package; firmware/firmware-build.json records its build inputs.
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

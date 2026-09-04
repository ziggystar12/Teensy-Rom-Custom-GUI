param(
    [string]$FreeDosZip = 'E:\MHS-Repository\HamsterOS\build\freedos\FDT2607-FloppyEdition.zip',
    [string]$Boulder = 'E:\MHS-Repository\HamsterOS\dos\Boulder.exe',
    [string]$Compiler = '',
    [string]$ToolchainRoot = '',
    [string]$CustomGuiAcmePath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $projectRoot 'build\dos-work'
$source = Join-Path $work 'source'
$package = Join-Path $work 'package'
$previous = Join-Path $work 'previous-package'
$destination = Join-Path $projectRoot 'DOSVM'
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
if ($CustomGuiAcmePath) {
    $CustomGuiAcmePath = [IO.Path]::GetFullPath($CustomGuiAcmePath)
    if (-not (Test-Path -LiteralPath $CustomGuiAcmePath -PathType Leaf)) {
        throw "Missing ACME assembler: $CustomGuiAcmePath"
    }
}
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
    # and the current DOSVM kit while regenerating the selected version.
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
    $redirector = Join-Path $work 'DOSDIR.COM'
    $upgrade = Join-Path $work 'dosvm-upgrade'
    Invoke-Native node @('dos/tools/build_dosdir_com.mjs', '--output', $redirector)
    Invoke-Native python @('dos/tools/build_freedos_boulder_image.py',
        '--source-zip', $FreeDosZip, '--command', $command, '--kssf', $kssf,
        '--boulder', $Boulder, '--redirector', $redirector,
        '--output', $image, '--manifest', $imageManifest, '--upgrade-dir', $upgrade)
    Invoke-Native node @('dos/tools/build_dosvm_terminal.mjs',
        '--output-prg', (Join-Path $work 'dosvm-terminal.prg'),
        '--output-boot-bank', $bootBank, '--manifest', $terminalManifest)
    Invoke-Native node @('--test', 'dos/tests/mpe5_packet_recovery_test.mjs')
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
    if ($CustomGuiAcmePath) { $buildArguments.CustomGuiAcmePath = $CustomGuiAcmePath }
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
        '--image-manifest', $imageManifest, '--upgrade-dir', $upgrade,
        '--output', (Join-Path $package 'sd-card'))

    $packagedImage = Join-Path $package 'sd-card/DOSVM/DOSVM.IMG'
    Assert-Hash $packagedImage (Get-Content -LiteralPath $imageManifest -Raw | ConvertFrom-Json).image.sha256
    Invoke-Native python @('dos/tests/mpe5_image_layout_test.py',
        '--source-zip', $FreeDosZip, '--image', $packagedImage)
    $crtRecord = Get-Content -LiteralPath $cartridgeManifest -Raw | ConvertFrom-Json
    Assert-Hash (Join-Path $package 'sd-card/DOSVM.CRT') $crtRecord.cartridgeSha256
    Assert-Hash $bios $crtRecord.biosSha256
    & ./dos/tools/test_mpe5_redirector.ps1
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'DOS folder redirector regression failed.' }
    & ./dos/tools/test_mpe5_core_services.ps1 -Image $packagedImage -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'DOS writable drives and tools acceptance failed.' }
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
# DOSVM

Built $([DateTime]::UtcNow.ToString('u')) with firmware $($version.version).
TeensyROM includes the GUI, MHS Power Engine (MPE), and DOSVM.

1. Flash firmware/$($version.filename).
2. For a new installation, copy sd-card/ to the SD card root. For an upgrade,
   keep your DOSVM.IMG and existing D: files: copy DOSVM.CRT and the supplied
   DOSVM/D/DOSVMUPD folder only. In DOS run D:\DOSVMUPD\UPDDOS once; this
   backs up and updates startup files inside C: without replacing the disk.
3. Launch DOSVM.CRT. The diagnostic title is: $title
4. Startup holds a Mean Hamster BIOS page with a POST beep before the normal
   C:\> prompt. DOS commands use the black-and-white 80 x 25 console with a
   blinking cursor. Type BOULDER for the included game.
   Space skips the intro; Shift starts the game. Cursor keys move, Shift grabs,
   and Space pauses. Port 2 directions act as cursors; fire acts as Shift.

C: is the writable /DOSVM/DOSVM.IMG file, with about 19 MiB initially free.
D: is the real /DOSVM/D folder on the SD card. Copy games into this folder
from your PC, using DOS 8.3 names such as GAMES/BOULDER.EXE. Back in DOS, use
D: then DIR to find them. DOS creates, changes, renames, and deletes actual
files in that folder. Subfolders work. Long filenames are not listed.

COPY D:\GAME.EXE C:\ copies a file into C:; COPY C:\FILE.TXT D:\ copies it
out. MD D:\GAMES makes a folder, and RD removes an empty folder. MEM, XCOPY,
MORE and ATTRIB are on PATH under C:\FREEDOS\BIN. COPY, MD, RD, DEL, REN,
TYPE and DIR are FreeCOM commands. DOSDIR.COM mounts D: automatically.

Let a save or copy finish before resetting. Writes and closes are flushed to
SD, but a reset during a multi-step filesystem operation can still interrupt
it. When updating in future, preserve your own C: image and DOSVM/D files;
the bundled image is a fresh template. No image editor is needed for D:.

DOSVM keeps the 512 KiB direct RAM2 guest, CGA rendering, a monochrome
80-column DOS console, PC speaker via SID, keyboard controls, writable drives
and paced packet recovery.
CGA scrolling now repaints while the picture remains visible. Folder state uses the
unused cartridge buffer in RAM1; it does not reduce the guest's 512 KiB.
Press Ctrl+Commodore+F7 to toggle sharp CGA graphics at any time. Sharp mode
preserves all 320 horizontal pixels with two colors per 8x8 block; blocks
using more colors are approximated. Press the same keys again for the
original multicolor output. It works across games without modifying them.
Normal F7, Ctrl+F7 and Commodore+F7 remain available to DOS applications.
The linked firmware retains a $stackReserveText-byte stack reserve. Before
DOS takeover, the normal RAM2 heap has $ram2HeapText bytes available.

Leaving DOS or using the cartridge button reboots the Teensy into the GUI.
R21 fixes the cold-start packet-recovery timeout in R20. If About already says
V$($version.version), replace only DOSVM.CRT and keep your existing C: image and D: files.
Run D:\DOSVMUPD\UPDDOS only if the R20 startup update was not already applied.
Color is the default after each launch.
If an older GUI rejects the HEX
with 'selection changed', use the V text updater once; the current firmware
retains the corrected graphical updater.

The kit is produced only after host VM, file I/O, keyboard, graphics, speaker,
firmware, memory ownership, packet fault recovery and C64 replay checks pass.
Physical speed, SD persistence and sustained play need this exact pair tested
on the cartridge. See dos/HARDWARE-TEST.md and dos/STORAGE.md for details.

DOSVM is replaced by the next successful build. Store your working
files on the SD card, not in this generated kit. SHA256SUMS.txt records its
firmware, cartridge, fresh disk image, shared-folder README and instructions.
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
    Write-Host "DOSVM: $destination"
}
finally { Pop-Location }

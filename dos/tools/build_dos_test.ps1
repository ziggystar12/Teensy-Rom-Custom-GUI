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
    & ./dos/tools/test_mpe5_publication.ps1 -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'MPE5 publication regression failed.' }
    & ./dos/tools/test_mpe5_vm.ps1 -Image $packagedImage -Bios $bios -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Packaged VM host acceptance failed.' }
    & ./dos/tools/test_mpe5_firmware.ps1 -Source $source -Image $packagedImage `
        -Cartridge (Join-Path $package 'sd-card/DOSVM.CRT') -Compiler $Compiler
    if (-not $? -or $LASTEXITCODE -ne 0) { throw 'Integrated MPE5 firmware acceptance failed.' }
    Invoke-Native node @('dos/tests/mpe5_c64_wire_test.mjs')

    $title = (Get-Content -LiteralPath $terminalManifest -Raw | ConvertFrom-Json).diagnosticTitle
    $readme = @"
# Latest DOSVM test build

Built $([DateTime]::UtcNow.ToString('u')) with MPE firmware $($version.version).

1. Flash firmware/$($version.filename).
2. Copy the contents of sd-card/ to the SD card root.
3. Launch DOSVM.CRT. Its diagnostic title is: $title
4. Look for the FreeDOS C:\> prompt, type DIR, and check for BOULDER.EXE.

This DOS implementation requires the optional Teensy PSRAM expansion.
Firmware error 04 reports unavailable VM memory; native Sierra running does
not prove that PSRAM is fitted. The VM needs 1,185,632 bytes for its flat PC
address map, I/O and private console, independently of flash and SD capacity.

The package passed the C64 CPU boot audit, native VM acceptance, publication
regressions, integrated firmware host acceptance, and C64 wire replay. Those
checks cover the returned prompt, DIR, keyboard, disk, all 1,000 initial cells,
hires frame completion, and idle refresh. The replay runs the actual terminal
and verifies C64 keyboard-matrix DIR and Return messages.
This build has not been verified on hardware.
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

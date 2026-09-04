param(
    [string]$WadPath = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$work = Join-Path $projectRoot 'build\doom-work'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'DOOMVM'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $OutputRoot.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "DOOMVM output must remain inside the workspace: $OutputRoot"
}
if ([string]::IsNullOrWhiteSpace($WadPath)) {
    $WadPath = Join-Path $projectRoot 'build\doom\assets\v1.9\DOOM1.WAD'
}
$WadPath = [IO.Path]::GetFullPath($WadPath)
if (-not (Test-Path -LiteralPath $WadPath -PathType Leaf)) {
    throw "User-supplied WAD is missing: $WadPath"
}
if ((Get-Item -LiteralPath $WadPath).Length -lt 12) {
    throw "WAD is too small: $WadPath"
}
$wadStream = [IO.File]::OpenRead($WadPath)
try {
    $wadHeader = New-Object byte[] 4
    if ($wadStream.Read($wadHeader, 0, $wadHeader.Length) -ne $wadHeader.Length -or
        [Text.Encoding]::ASCII.GetString($wadHeader) -notin @('IWAD', 'PWAD')) {
        throw "WAD header is invalid: $WadPath"
    }
}
finally {
    $wadStream.Dispose()
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
$bootBank = Join-Path $work 'doomvm-bootbank.bin'
$terminalPrg = Join-Path $work 'doomvm-terminal.prg'
$terminalManifest = Join-Path $work 'doomvm-terminal.json'
$cartridge = Join-Path $work 'DOOMVM.CRT'
$cartridgeManifest = Join-Path $work 'DOOMVM.CRT.JSON'

Push-Location $projectRoot
try {
    & node 'doom/tools/build_doomvm_terminal.mjs' `
        '--output-prg' $terminalPrg '--output-boot-bank' $bootBank `
        '--manifest' $terminalManifest
    if ($LASTEXITCODE -ne 0) { throw 'DOOMVM terminal build failed.' }
    & python 'doom/tools/build_doomvm_cartridge.py' `
        '--boot-bank' $bootBank '--output' $cartridge `
        '--manifest' $cartridgeManifest
    if ($LASTEXITCODE -ne 0) { throw 'DOOMVM cartridge build failed.' }
    & python 'doom/tests/mpe7_cartridge_layout_test.py' `
        '--crt' $cartridge '--manifest' $cartridgeManifest
    if ($LASTEXITCODE -ne 0) { throw 'DOOMVM cartridge layout test failed.' }

    $sdRoot = Join-Path $OutputRoot 'sd-card'
    $doomRoot = Join-Path $sdRoot 'DOOMVM'
    $manifestRoot = Join-Path $OutputRoot 'manifests'
    New-Item -ItemType Directory -Force -Path $doomRoot | Out-Null
    New-Item -ItemType Directory -Force -Path $manifestRoot | Out-Null
    Copy-Item -LiteralPath $cartridge -Destination (Join-Path $sdRoot 'DOOMVM.CRT') -Force
    $packagedWad = [IO.Path]::GetFullPath((Join-Path $doomRoot 'DOOM1.WAD'))
    if (-not $WadPath.Equals($packagedWad, [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $WadPath -Destination $packagedWad -Force
    }
    Copy-Item -LiteralPath $cartridgeManifest -Destination (Join-Path $manifestRoot 'DOOMVM.CRT.json') -Force
    Copy-Item -LiteralPath $terminalManifest -Destination (Join-Path $manifestRoot 'DOOMVM.TERMINAL.json') -Force

    $crtHash = (Get-FileHash -LiteralPath (Join-Path $sdRoot 'DOOMVM.CRT') -Algorithm SHA256).Hash.ToLowerInvariant()
    $wadHash = (Get-FileHash -LiteralPath $packagedWad -Algorithm SHA256).Hash.ToLowerInvariant()
    $terminalData = Get-Content -Raw -LiteralPath $terminalManifest | ConvertFrom-Json
    $package = [ordered]@{
        status = 'PASS'
        acceptance = 'cartridge-and-sd-layout-only'
        cartridge = (Join-Path $sdRoot 'DOOMVM.CRT')
        cartridgeBytes = (Get-Item -LiteralPath (Join-Path $sdRoot 'DOOMVM.CRT')).Length
        cartridgeSha256 = $crtHash
        wad = $packagedWad
        wadBytes = (Get-Item -LiteralPath $packagedWad).Length
        wadSha256 = $wadHash
        firmwareIntegrated = $false
        physicalHardwareProven = $false
    }

    $utf8NoBom = [Text.UTF8Encoding]::new($false)
    $packageManifest = [ordered]@{
        format = 'MHS-DOOMVM-SD-v1'
        copyToSdCard = @('/DOOMVM.CRT', '/DOOMVM/DOOM1.WAD')
        runtimeAssets = @(
            [ordered]@{ path='/DOOMVM.CRT'; bytes=$package.cartridgeBytes; sha256=$crtHash },
            [ordered]@{ path='/DOOMVM/DOOM1.WAD'; bytes=$package.wadBytes; sha256=$wadHash }
        )
        launcher = [ordered]@{
            identity = 'MHS DOOMVM'
            descriptor = 'M7D1 version 1'
            wadPath = '/DOOMVM/DOOM1.WAD'
            terminalPrgBytes = $terminalData.terminalPrgBytes
            terminalPrgSha256 = $terminalData.terminalPrgSha256
            bootBankBytes = $terminalData.bootBankBytes
            bootBankSha256 = $terminalData.bootBankSha256
            inputProtocol = $terminalData.inputProtocol
            packetProtocol = $terminalData.packetProtocol
        }
        requirements = [ordered]@{
            firmware = 'MHS Power Engine firmware with MPE7 DOOMVM support'
            externalPsramBytes = 8MB
            exclusiveRam2Bytes = 512KB
        }
        firmwareIncluded = $false
        physicalHardwareProven = $false
    }
    [IO.File]::WriteAllText((Join-Path $manifestRoot 'DOOMVM-PACKAGE.json'),
        ($packageManifest | ConvertTo-Json -Depth 6) + [Environment]::NewLine, $utf8NoBom)

    $readme = @"
MHS DOOMVM SD-CARD PACKAGE

Copy the contents of the sd-card folder to the root of the TeensyROM SD card.
Keep these exact paths:

  /DOOMVM.CRT
  /DOOMVM/DOOM1.WAD

DOOMVM.CRT is the C64 launcher, display, and input terminal. DOOM1.WAD is the
user-supplied game data. The launcher requires MHS Power Engine firmware with
MPE7 DOOMVM support, a Teensy 4.1, and at least 8 MiB of external PSRAM.

This package has software/layout validation only. It has not been accepted on
physical C64/TeensyROM hardware yet.
"@.Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText((Join-Path $OutputRoot 'README.txt'), $readme, $utf8NoBom)
    $controls = @"
MHS DOOMVM CONTROLS

Joystick port 2
  Up / Down       Forward / Backward
  Left / Right    Turn left / right
  Fire            Fire weapon

Keyboard
  W / S           Forward / Backward
  Cursor keys     Forward, backward, and turn
  A / D           Strafe left / right
  , / .           Strafe left / right
  Control         Fire weapon
  Space           Use / open
  Shift           Run
  1 through 7     Select weapon
  RUN/STOP        Menu / escape

Keyboard and joystick may be used together.
"@.Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText((Join-Path $OutputRoot 'CONTROLS.txt'), $controls, $utf8NoBom)
    $checksumText = @(
        "$crtHash  sd-card/DOOMVM.CRT",
        "$wadHash  sd-card/DOOMVM/DOOM1.WAD"
    ) -join "`n"
    [IO.File]::WriteAllText((Join-Path $OutputRoot 'SHA256SUMS.txt'),
        $checksumText + "`n", $utf8NoBom)

    # Never report a copy-ready SD tree when an older build left extra runtime
    # files beside the exact launcher/WAD pair. Refuse stale output without
    # deleting anything the caller may want to inspect or recover.
    $expectedSdFiles = @('DOOMVM.CRT', 'DOOMVM/DOOM1.WAD')
    $actualSdFiles = @(Get-ChildItem -LiteralPath $sdRoot -Recurse -File |
        ForEach-Object {
            $_.FullName.Substring($sdRoot.Length + 1).Replace('\', '/')
        } | Sort-Object)
    if (($actualSdFiles -join "`n") -cne ($expectedSdFiles -join "`n")) {
        throw "DOOMVM SD output contains stale or unexpected files: $($actualSdFiles -join ', ')"
    }

    $package | ConvertTo-Json -Compress
}
finally {
    Pop-Location
}

param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Image,
    [Parameter(Mandatory=$true)][string]$Cartridge,
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Source = [IO.Path]::GetFullPath($Source)
$Image = [IO.Path]::GetFullPath($Image)
$Cartridge = [IO.Path]::GetFullPath($Cartridge)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
$output = Join-Path $projectRoot 'build/dos-work'
$exe = Join-Path $output 'mpe5-firmware-test.exe'
$wire = Join-Path $output 'dos-wire.bin'
$screen = Join-Path $output 'dos-screen.txt'
$report = Join-Path $output 'dos-firmware-result.json'
foreach ($stale in @($wire,$screen,$report)) {
    if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale -Force }
}
$handlers = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/IO_Handlers'
$native = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/NativeDOS'
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $projectRoot 'engine/native-dos') -File -Recurse)) {
    $relative = $file.FullName.Substring((Join-Path $projectRoot 'engine/native-dos').Length+1)
    if ($relative -match '\.(cpp|h|c)$') {
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $native $relative)).Hash) {
            throw "Stale expanded firmware source: $relative"
        }
    }
}
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    # Cortex-M7 GCC defaults to unsigned plain char. Exercise the real firmware
    # with that default too; a signed-char PC build hid the R9 CPU jump bug.
    & $Compiler -std=c++17 -O2 -funsigned-char -w -I $handlers (Join-Path $projectRoot 'dos/tests/mpe5_firmware_host_test.cpp') -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'Integrated firmware harness compilation failed.' }
    $result = & $exe $Cartridge $Image (Join-Path $projectRoot 'Demo/The-Black-Cauldron-MPE.crt') $wire $screen (Join-Path (Split-Path -Parent $Image) 'DOSVM.SWP')
    if ($LASTEXITCODE -ne 0) { throw 'Integrated firmware acceptance failed.' }
    $result | Write-Output
    [ordered]@{
        passed = $true
        plainChar = 'unsigned (matching Cortex-M7 GCC)'
        result = ($result -join "`n")
        cartridgeSha256 = (Get-FileHash -LiteralPath $Cartridge).Hash.ToLowerInvariant()
        imageSha256 = (Get-FileHash -LiteralPath $Image).Hash.ToLowerInvariant()
        moduleSha256 = (Get-FileHash -LiteralPath (Join-Path $handlers 'IOH_MPE3TitlePull.c')).Hash.ToLowerInvariant()
        wireSha256 = (Get-FileHash -LiteralPath $wire).Hash.ToLowerInvariant()
        scope = 'Actual firmware sequencer and native engines, simulated SD and bus pins; physical hardware remains untested.'
    } | ConvertTo-Json | Set-Content -LiteralPath $report -Encoding utf8
}
finally { $env:PATH = $originalPath }

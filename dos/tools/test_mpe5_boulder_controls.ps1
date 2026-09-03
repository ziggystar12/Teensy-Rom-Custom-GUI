param(
    [Parameter(Mandatory=$true)][string]$Image,
    [string]$Bios = '',
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
if (-not $Bios) { $Bios = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios' }
$work = Join-Path $projectRoot 'build/dos-work'
$output = Join-Path $work 'mpe5-boulder-controls.exe'
$originalPath = $env:PATH
try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    & $Compiler -std=c++17 -O2 -w -funsigned-char (Join-Path $projectRoot 'dos/tests/mpe5_boulder_controls_test.cpp') `
        (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Boulder controls test compilation failed.' }
    & $output $Bios $Image
    if ($LASTEXITCODE -ne 0) { throw 'Boulder controls acceptance failed.' }
}
finally { $env:PATH = $originalPath }

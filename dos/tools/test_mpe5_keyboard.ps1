param(
    [Parameter(Mandatory=$true)][string]$Image,
    [string]$Bios = '',
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
if (-not $Bios) { $Bios = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios' }
$output = Join-Path $projectRoot 'build/dos-work/mpe5-keyboard-test.exe'
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    & $Compiler -std=c++17 -O2 -w -funsigned-char (Join-Path $projectRoot 'dos/tests/mpe5_keyboard_test.cpp') `
        (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Keyboard test compilation failed.' }
    & $output $Bios $Image
    if ($LASTEXITCODE -ne 0) { throw 'Keyboard acceptance failed.' }
}
finally { $env:PATH = $originalPath }

param([string]$Compiler = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Supply -Compiler with a native Windows g++ path.' }
$output = Join-Path $projectRoot 'build/dos-work/mpe5-paged-memory-test.exe'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror `
        (Join-Path $projectRoot 'dos/tests/mpe5_paged_memory_test.cpp') `
        (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Paged memory test compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Paged memory test failed.' }
}
finally { $env:PATH = $originalPath }

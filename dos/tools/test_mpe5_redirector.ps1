param([string]$Compiler = 'C:\msys64\mingw64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output = Join-Path ([IO.Path]::GetTempPath()) "mpe5-redirector-test-$PID.exe"
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler)$([IO.Path]::PathSeparator)$originalPath"
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation `
        (Join-Path $projectRoot 'dos/tests/mpe5_redirector_test.cpp') `
        (Join-Path $projectRoot 'engine/native-dos/mpe5_redirector.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Redirector test compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Redirector test failed.' }
} finally {
    $env:PATH = $originalPath
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Force }
}

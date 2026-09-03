param([string]$Compiler = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testSource = Join-Path $projectRoot 'dos\tests\mpe5_direct_memory_test.cpp'
$implementation = Join-Path $projectRoot 'engine\native-dos\mpe5_direct_memory.cpp'

if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $candidates = @(
        (Get-Command 'g++.exe' -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        'C:\msys64\mingw64\bin\g++.exe',
        'C:\msys64\mingw32\bin\g++.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    $Compiler = $candidates | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($Compiler) -or
    -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw 'A host g++ compiler is required. Supply -Compiler with a native Windows g++.exe path.'
}
foreach ($inputFile in @($testSource, $implementation)) {
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) {
        throw "Missing direct-memory test input: $inputFile"
    }
}

$output = Join-Path ([IO.Path]::GetTempPath()) "mpe5-direct-memory-test-$PID.exe"
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler)$([IO.Path]::PathSeparator)$originalPath"
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror $testSource $implementation -o $output
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw 'Unable to compile the MPE5 direct-memory acceptance test.'
    }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'MPE5 direct-memory acceptance failed.' }
}
finally {
    $env:PATH = $originalPath
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        Remove-Item -LiteralPath $output -Force
    }
}

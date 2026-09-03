param([string]$Compiler = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testSource = Join-Path $projectRoot 'dos\tests\mpe5_text_publication_test.cpp'
$fontDirectory = Join-Path $projectRoot 'build\dos-work'
$fontOutput = Join-Path $fontDirectory 'dos-font.bin'
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $candidates = @(
        (Get-Command 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        'C:\msys64\mingw64\bin\g++.exe',
        'C:\msys64\mingw32\bin\g++.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
    $Compiler = $candidates | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($Compiler) -or
    -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw 'A native Windows g++ compiler is required. Supply -Compiler with its path.'
}
$output = Join-Path ([IO.Path]::GetTempPath()) "mpe5-publication-test-$PID.exe"
$originalPath = $env:PATH
try {
    New-Item -ItemType Directory -Path $fontDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $fontOutput -PathType Leaf) {
        Remove-Item -LiteralPath $fontOutput -Force
    }
    $compilerDirectory = Split-Path -Parent $Compiler
    $env:PATH = "$compilerDirectory$([IO.Path]::PathSeparator)$env:PATH"
    & $Compiler -std=c++17 -O2 -w $testSource -o $output
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
        throw 'Unable to compile the MPE5 publication regression.'
    }
    & $output $fontOutput
    if ($LASTEXITCODE -ne 0) { throw 'MPE5 publication regression failed.' }
    if ((Get-Item -LiteralPath $fontOutput).Length -ne 2048) {
        throw 'Verified MPE5 font atlas must contain 256 eight-byte glyphs.'
    }
}
finally {
    $env:PATH = $originalPath
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        Remove-Item -LiteralPath $output -Force
    }
}

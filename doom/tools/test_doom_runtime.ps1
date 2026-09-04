param(
    [string]$Compiler = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$includeRoot = Join-Path $repoRoot 'engine\native-doom'
$runtimeSource = Join-Path $includeRoot 'mpe_doom_runtime.cpp'
$testSource = Join-Path $repoRoot 'doom\tests\mpe_doom_runtime_test.cpp'

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot 'build\doom-runtime-test'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

if ([string]::IsNullOrWhiteSpace($Compiler) -and
    -not [string]::IsNullOrWhiteSpace($env:CXX)) {
    $Compiler = $env:CXX
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command 'g++' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Compiler = $command.Source
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $mingwCompiler = 'C:\msys64\mingw64\bin\g++.exe'
    if (Test-Path -LiteralPath $mingwCompiler) {
        $Compiler = $mingwCompiler
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    throw 'No C++ compiler found. Pass -Compiler or set CXX.'
}

if (Test-Path -LiteralPath $Compiler) {
    $Compiler = (Resolve-Path -LiteralPath $Compiler).Path
}
$compilerDirectory = Split-Path -Parent $Compiler
$originalPath = $env:Path
try {
    if (-not [string]::IsNullOrWhiteSpace($compilerDirectory)) {
        $env:Path = "$compilerDirectory;$env:Path"
    }

    $executable = Join-Path $OutputRoot 'mpe_doom_runtime_test.exe'
    $compileArgs = @(
        '-std=c++17',
        '-O2',
        '-Wall',
        '-Wextra',
        '-Werror',
        '-pedantic',
        '-I', $includeRoot,
        $runtimeSource,
        $testSource,
        '-o', $executable
    )

    & $Compiler @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Doom runtime host test compilation failed with exit code $LASTEXITCODE."
    }

    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Doom runtime host test failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:Path = $originalPath
}

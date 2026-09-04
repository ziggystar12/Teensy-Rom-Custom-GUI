param(
    [string]$Compiler = '',
    [string]$ArmCompiler = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$includeRoot = Join-Path $repoRoot 'engine\native-doom'
$sources = @(
    (Join-Path $includeRoot 'mpe_doom_runtime.cpp'),
    (Join-Path $includeRoot 'mpe_doom_video.cpp'),
    (Join-Path $includeRoot 'mpe_doom_session.cpp')
)
$testSource = Join-Path $repoRoot 'doom\tests\mpe_doom_session_test.cpp'

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot 'build\doom-session-test'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

if ([string]::IsNullOrWhiteSpace($Compiler) -and
    -not [string]::IsNullOrWhiteSpace($env:CXX)) {
    $Compiler = $env:CXX
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command 'g++' -ErrorAction SilentlyContinue
    if ($null -ne $command) { $Compiler = $command.Source }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $candidate = 'C:\msys64\mingw64\bin\g++.exe'
    if (Test-Path -LiteralPath $candidate) { $Compiler = $candidate }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    throw 'No host C++ compiler found. Pass -Compiler or set CXX.'
}
if (Test-Path -LiteralPath $Compiler) {
    $Compiler = (Resolve-Path -LiteralPath $Compiler).Path
}

$originalPath = $env:Path
try {
    $hostCompilerDirectory = Split-Path -Parent $Compiler
    if ($hostCompilerDirectory) {
        $env:Path = "$hostCompilerDirectory;$env:Path"
    }
    $executable = Join-Path $OutputRoot 'mpe_doom_session_test.exe'
    $hostArgs = @(
        '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-funsigned-char', '-I', $includeRoot
    ) + $sources + @($testSource, '-o', $executable)
    & $Compiler @hostArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Doom session host compilation failed with exit code $LASTEXITCODE."
    }
    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw "Doom session host test failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:Path = $originalPath
}

if ([string]::IsNullOrWhiteSpace($ArmCompiler)) {
    $armCandidates = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'build\toolchain') `
        -Recurse -File -Filter 'arm-none-eabi-g++.exe' -ErrorAction SilentlyContinue |
        Sort-Object FullName)
    if ($armCandidates.Count -ne 0) { $ArmCompiler = $armCandidates[0].FullName }
}
if ([string]::IsNullOrWhiteSpace($ArmCompiler)) {
    throw 'No Teensy ARM C++ compiler found. Build the firmware toolchain or pass -ArmCompiler.'
}
if (Test-Path -LiteralPath $ArmCompiler) {
    $ArmCompiler = (Resolve-Path -LiteralPath $ArmCompiler).Path
}

$armOutput = Join-Path $OutputRoot 'teensy41'
New-Item -ItemType Directory -Force -Path $armOutput | Out-Null
$originalPath = $env:Path
try {
    $armCompilerDirectory = Split-Path -Parent $ArmCompiler
    if ($armCompilerDirectory) {
        $env:Path = "$armCompilerDirectory;$env:Path"
    }
    foreach ($source in $sources) {
        $object = Join-Path $armOutput (([System.IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
        $armArgs = @(
            '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-funsigned-char', '-fno-exceptions', '-fno-rtti',
            '-mcpu=cortex-m7', '-mthumb', '-mfloat-abi=hard', '-mfpu=fpv5-d16',
            '-I', $includeRoot, '-c', $source, '-o', $object
        )
        & $ArmCompiler @armArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Teensy 4.1 cross-compilation failed for $source with exit code $LASTEXITCODE."
        }
    }
}
finally {
    $env:Path = $originalPath
}

Write-Host "Doom session Teensy 4.1 cross-compile: PASS ($armOutput)"

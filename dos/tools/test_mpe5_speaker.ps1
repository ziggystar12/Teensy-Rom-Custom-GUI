param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
$output = Join-Path $projectRoot 'build/dos-work/mpe5-speaker-test.exe'
$originalPath = $env:PATH
try {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    $arguments = @(
        '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-funsigned-char',
        (Join-Path $projectRoot 'dos/tests/mpe5_speaker_test.cpp'),
        (Join-Path $projectRoot 'engine/native-dos/mpe5_platform.cpp'),
        (Join-Path $projectRoot 'engine/native-dos/mpe5_speaker.cpp'),
        '-o', $output
    )
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Speaker regression compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Speaker regression failed.' }
    $coreOutput = Join-Path $projectRoot 'build/dos-work/mpe5-speaker-core-test.exe'
    $coreArguments = @(
        '-std=c++17', '-O2', '-w', '-funsigned-char',
        (Join-Path $projectRoot 'dos/tests/mpe5_speaker_core_test.cpp'),
        (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp'),
        '-o', $coreOutput
    )
    & $Compiler @coreArguments
    if ($LASTEXITCODE -ne 0) { throw 'Speaker core regression compilation failed.' }
    & $coreOutput (Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios')
    if ($LASTEXITCODE -ne 0) { throw 'Speaker core regression failed.' }
}
finally { $env:PATH = $originalPath }

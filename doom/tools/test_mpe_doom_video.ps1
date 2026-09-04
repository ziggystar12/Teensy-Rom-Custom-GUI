param(
    [string]$Compiler = '',
    [string]$ArmCompiler = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "Doom video host compiler not found: $Compiler"
}
if (-not $ArmCompiler) {
    $armCandidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'build\toolchain') `
        -Recurse -File -Filter 'arm-none-eabi-g++.exe' -ErrorAction SilentlyContinue |
        Sort-Object FullName)
    if ($armCandidates.Count -ne 0) { $ArmCompiler = $armCandidates[0].FullName }
}
if (-not $ArmCompiler) {
    throw 'No Teensy ARM C++ compiler found. Build the firmware toolchain or pass -ArmCompiler.'
}
if (-not (Test-Path -LiteralPath $ArmCompiler -PathType Leaf)) {
    throw "Doom video ARM compiler not found: $ArmCompiler"
}
$work = Join-Path $projectRoot 'build/doom'
$output = Join-Path $work 'mpe-doom-video-test.exe'
$proof = Join-Path $work 'doom-video-proof.ppm'
$originalPath = $env:PATH
try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    $arguments = @(
        '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic', '-funsigned-char',
        '-DMPE_DOOM_VIDEO_DIAGNOSTICS=1',
        '-I', (Join-Path $projectRoot 'engine/native-doom'),
        (Join-Path $projectRoot 'doom/tests/mpe_doom_video_test.cpp'),
        (Join-Path $projectRoot 'engine/native-doom/mpe_doom_video.cpp'),
        '-o', $output
    )
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Doom video test compilation failed.' }
    & $output $proof
    if ($LASTEXITCODE -ne 0) { throw 'Doom video test failed.' }
    $proofFile = Get-Item -LiteralPath $proof
    $proofSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $proof).Hash.ToLowerInvariant()
    Write-Host "PPM proof: $($proofFile.FullName) ($($proofFile.Length) bytes, SHA-256 $proofSha256)"

    $armWork = Join-Path $work 'video-cortex-m7'
    $armObject = Join-Path $armWork 'mpe_doom_video.o'
    $stackReport = Join-Path $armWork 'mpe_doom_video.su'
    New-Item -ItemType Directory -Force -Path $armWork | Out-Null
    foreach ($stalePath in @($armObject, $stackReport)) {
        if (Test-Path -LiteralPath $stalePath) {
            Remove-Item -LiteralPath $stalePath -Force
        }
    }
    $armArguments = @(
        '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror', '-funsigned-char',
        '-fstack-usage', '-ffunction-sections', '-fdata-sections',
        '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
        '-I', (Join-Path $projectRoot 'engine/native-doom'),
        '-c', (Join-Path $projectRoot 'engine/native-doom/mpe_doom_video.cpp'),
        '-o', $armObject
    )
    & $ArmCompiler @armArguments
    if ($LASTEXITCODE -ne 0) { throw 'Doom video Cortex-M7 compilation failed.' }
    $renderStackLines = @(Get-Content -LiteralPath $stackReport |
        Where-Object { $_ -match 'Video::renderCell' })
    if ($renderStackLines.Count -ne 1) {
        throw "Expected one Doom renderCell stack record, found $($renderStackLines.Count)."
    }
    $stackMatch = [System.Text.RegularExpressions.Regex]::Match(
        $renderStackLines[0], "`t([1-9][0-9]*)`t")
    if (-not $stackMatch.Success) {
        throw 'Doom renderCell stack record has no positive numeric byte count.'
    }
    $renderStackBytes = [int]$stackMatch.Groups[1].Value
    if ($renderStackBytes -gt 640) {
        throw "Doom renderCell Cortex-M7 stack usage regressed to $renderStackBytes bytes."
    }
    Write-Host "Cortex-M7 compile PASS: renderCell stack $renderStackBytes bytes (640-byte guard)."
}
finally { $env:PATH = $originalPath }

param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$FramePath,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$PalettePath,
    [string]$Compiler = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsBelow([string]$Path, [string]$Directory) {
    $prefix = $Directory.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$FramePath = [System.IO.Path]::GetFullPath($FramePath)
$PalettePath = [System.IO.Path]::GetFullPath($PalettePath)
if (-not (Test-Path -LiteralPath $FramePath -PathType Leaf)) {
    throw "Indexed Doom frame not found: $FramePath"
}
if (-not (Test-Path -LiteralPath $PalettePath -PathType Leaf)) {
    throw "Doom RGB24 palette not found: $PalettePath"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $buildRoot 'doom\c64-frame-proof'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
if (-not (Test-IsBelow $OutputRoot $buildRoot)) {
    throw "C64 frame-proof output must remain below the ignored build directory: $buildRoot"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

if ([string]::IsNullOrWhiteSpace($Compiler) -and
    -not [string]::IsNullOrWhiteSpace($env:CXX)) {
    $Compiler = $env:CXX
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $knownCompiler = 'C:\msys64\mingw64\bin\g++.exe'
    if (Test-Path -LiteralPath $knownCompiler) {
        $Compiler = $knownCompiler
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command 'g++' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Compiler = $command.Source
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    throw 'No C++17 compiler found. Pass -Compiler or set CXX.'
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

    $videoRoot = Join-Path $repoRoot 'engine\native-doom'
    $converterSource = Join-Path $repoRoot 'doom\host\mhs_doom_c64_frame.cpp'
    $executable = Join-Path $OutputRoot 'mhs-doom-c64-frame.exe'
    $preview = Join-Path $OutputRoot 'e1m1-c64-preview.ppm'
    foreach ($generatedPath in @($executable, $preview)) {
        if (Test-Path -LiteralPath $generatedPath) {
            Remove-Item -LiteralPath $generatedPath -Force
        }
    }

    & $Compiler @(
        '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-DMPE_DOOM_VIDEO_DIAGNOSTICS',
        '-I', $videoRoot,
        (Join-Path $videoRoot 'mpe_doom_video.cpp'),
        $converterSource,
        '-o', $executable
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Doom C64 frame converter compilation failed with exit code $LASTEXITCODE."
    }

    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $converterOutput = @(& $executable $FramePath $PalettePath $preview)
    $timer.Stop()
    if ($LASTEXITCODE -ne 0) {
        throw "Doom C64 frame conversion failed with exit code $LASTEXITCODE."
    }
    $evidence = ($converterOutput -join "`n") | ConvertFrom-Json
    if ($evidence.status -ne 'PASS' -or
        $evidence.logical -ne '160x200 VIC-II multicolor' -or
        $evidence.records -ne 1000 -or $evidence.packets -ne 53 -or
        $evidence.recordBytes -ne 12 -or
        $evidence.searchSampleEvaluations -le 0 -or
        $evidence.searchSampleEvaluations -gt 14560000) {
        throw 'Doom C64 frame conversion evidence is incomplete.'
    }
    $previewFile = Get-Item -LiteralPath $preview
    if ($previewFile.Length -ne 192015) {
        throw "Unexpected C64 preview size: $($previewFile.Length) bytes."
    }

    [ordered]@{
        status = 'PASS'
        acceptance = 'host-conversion-only'
        source = $evidence.source
        logical = $evidence.logical
        preview = [ordered]@{
            path = $preview
            format = 'P6 PPM, 320x200 pixel-aspect corrected'
            bytes = $previewFile.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $preview).Hash.ToLowerInvariant()
        }
        transport = [ordered]@{
            records = $evidence.records
            recordBytes = $evidence.recordBytes
            payloadBytes = $evidence.records * $evidence.recordBytes
            packetsAt19Records = $evidence.packets
        }
        paletteSearchSampleEvaluations = $evidence.searchSampleEvaluations
        hostElapsedMilliseconds = $timer.ElapsedMilliseconds
        input = [ordered]@{
            frameBytes = (Get-Item -LiteralPath $FramePath).Length
            frameSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $FramePath).Hash.ToLowerInvariant()
            paletteBytes = (Get-Item -LiteralPath $PalettePath).Length
            paletteSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PalettePath).Hash.ToLowerInvariant()
        }
    } | ConvertTo-Json -Depth 5 -Compress
}
finally {
    $env:Path = $originalPath
}

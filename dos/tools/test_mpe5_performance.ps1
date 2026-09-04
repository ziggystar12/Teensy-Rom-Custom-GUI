param(
    [string]$Source = '',
    [string]$Image = '',
    [string]$Cartridge = '',
    [string]$Compiler = 'C:\msys64\mingw64\bin\g++.exe'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $project 'build/dos-work'
if (-not $Compiler) { $Compiler = 'C:\msys64\mingw64\bin\g++.exe' }
if (-not $Source) { $Source = Join-Path $work 'source' }
if (-not $Image) { $Image = Join-Path $work 'DOSVM.IMG' }
if (-not $Cartridge) { $Cartridge = Join-Path $work 'DOSVM.CRT' }
$Source = [IO.Path]::GetFullPath($Source)
$native = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/NativeDOS'
$handlers = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/IO_Handlers'
$canonical = Join-Path $project 'engine/native-dos'

$sourceManifest = Join-Path $work 'manifests/native-dos-sources.json'
if (-not (Test-Path -LiteralPath $sourceManifest -PathType Leaf)) {
    throw "Missing native DOS source manifest: $sourceManifest"
}
foreach ($entry in (Get-Content -LiteralPath $sourceManifest -Raw | ConvertFrom-Json)) {
    $relative = [string]$entry.file
    $canonicalFile = Join-Path $canonical $relative
    $expandedFile = Join-Path $native $relative
    if (-not (Test-Path -LiteralPath $canonicalFile -PathType Leaf) -or
        -not (Test-Path -LiteralPath $expandedFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $canonicalFile).Hash -ne [string]$entry.sha256 -or
        (Get-FileHash -LiteralPath $expandedFile).Hash -ne [string]$entry.sha256) {
        throw "Stale expanded firmware source: $relative"
    }
}

$exe = Join-Path $work 'mpe5-performance-r17.exe'
$originalPath = $env:PATH
$result = @()
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    & $Compiler -std=c++17 -O2 -funsigned-char -w -I $handlers `
        (Join-Path $project 'dos/tests/mpe5_performance_test.cpp') -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'R17 performance harness compilation failed.' }
    foreach ($polls in @(1,3,9)) {
        $run = & $exe $Cartridge $Image R17 $polls
        if ($LASTEXITCODE -ne 0) { throw "R17 performance acceptance failed at $polls pending polls." }
        $result += $run
        $run | Write-Output
    }
    $result | Set-Content -LiteralPath (Join-Path $work 'dos-performance-result.txt') -Encoding utf8
}
finally { $env:PATH = $originalPath }

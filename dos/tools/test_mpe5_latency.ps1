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
$Image = [IO.Path]::GetFullPath($Image)
$Cartridge = [IO.Path]::GetFullPath($Cartridge)
$handlers = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/IO_Handlers'
$native = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/NativeDOS'
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

$exe = Join-Path $work 'mpe5-latency.exe'
$resultPath = Join-Path $work 'dos-latency-result.txt'
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    $doomRuntime = Join-Path $project 'engine/native-doom'
    & $Compiler -std=c++17 -O2 -funsigned-char -w -I $handlers `
        (Join-Path $project 'dos/tests/mpe5_latency_test.cpp') `
        (Join-Path $doomRuntime 'mpe_doom_runtime.cpp') `
        (Join-Path $doomRuntime 'mpe_doom_video.cpp') `
        (Join-Path $doomRuntime 'mpe_doom_session.cpp') `
        (Join-Path $project 'dos/tests/mpe7_host_link_stubs.cpp') `
        -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'Direct-RAM latency harness compilation failed.' }
    $result = & $exe $Cartridge $Image
    if ($LASTEXITCODE -ne 0) { throw 'Direct-RAM latency acceptance failed.' }
    $result | Write-Output
    $result | Set-Content -LiteralPath $resultPath -Encoding utf8
}
finally { $env:PATH = $originalPath }

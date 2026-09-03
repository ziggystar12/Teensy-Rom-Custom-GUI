param(
    [string]$Source = '',
    [string]$Image = '',
    [string]$Cartridge = '',
    [string]$Compiler = 'C:\msys64\mingw64\bin\g++.exe'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $project 'build/dos-work'
if (-not $Source) { $Source = Join-Path $work 'source' }
if (-not $Image) { $Image = Join-Path $work 'DOSVM.IMG' }
if (-not $Cartridge) { $Cartridge = Join-Path $work 'DOSVM.CRT' }
$native = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/NativeDOS'
$handlers = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/IO_Handlers'
$canonical = Join-Path $project 'engine/native-dos'
$header = Join-Path $native 'mpe5_firmware.h'
$test = Join-Path $project 'dos/tests/mpe5_performance_test.cpp'
$originalPath = $env:PATH
$current = [IO.File]::ReadAllText((Join-Path $canonical 'mpe5_firmware.h'))
# Pin R12 scheduling while sharing every current core, input and paging fix.
$baselineCommit = '129badcb4131192449b6605358e26d3e58d6855c'
$old = (& git -C $project show "${baselineCommit}:engine/native-dos/mpe5_firmware.h") -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Pinned R12 scheduling source is unavailable.' }
$baseline = $current
$slicePattern = 'static constexpr uint32_t MPE5InstructionSlice = \d+u;'
$oldSlice = [regex]::Match($old,$slicePattern)
if (-not $oldSlice.Success) { throw 'Pinned R12 instruction slice is unavailable.' }
$baseline = [regex]::Replace($baseline,$slicePattern,$oldSlice.Value)
foreach ($function in @('MPE5ShouldYield','MPE5PumpPending','MPE5NextPacket')) {
    $pattern = '(?ms)^static FLASHMEM (?:bool|void) ' + $function + '\([^\n]*\)\r?\n\{.*?^\}'
    $oldFunction = [regex]::Match($old,$pattern)
    if (-not $oldFunction.Success -or [regex]::Matches($baseline,$pattern).Count -ne 1) {
        throw "Cannot isolate scheduler function $function."
    }
    $replacement = $oldFunction.Value
    $baseline = [regex]::Replace($baseline,$pattern,[System.Text.RegularExpressions.MatchEvaluator]{param($match) $replacement})
}
$result = @()
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    foreach ($file in Get-ChildItem -LiteralPath $canonical -File -Recurse) {
        if ($file.Extension -in @('.cpp','.h','.c')) {
            $relative = $file.FullName.Substring($canonical.Length+1)
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $native $relative)
        }
    }
    foreach ($variant in @('R12','current')) {
        [IO.File]::WriteAllText($header, $(if ($variant -eq 'R12') {$baseline} else {$current}))
        $exe = Join-Path $work "mpe5-performance-$($variant.ToLowerInvariant()).exe"
        & $Compiler -std=c++17 -O2 -funsigned-char -w -I $handlers $test -o $exe
        if ($LASTEXITCODE -ne 0) { throw "$variant performance harness compilation failed." }
        foreach ($polls in @(1,3,9)) {
            $run = & $exe $Cartridge $Image (Join-Path (Split-Path -Parent $Image) 'DOSVM.SWP') $variant $polls
            if ($LASTEXITCODE -ne 0) { throw "$variant performance acceptance failed at $polls pending polls." }
            $result += $run; $run | Write-Output
        }
    }
    $result | Set-Content -LiteralPath (Join-Path $work 'dos-performance-result.txt') -Encoding utf8
}
finally {
    # A baseline comparison must never leave old scheduling in a later build.
    [IO.File]::WriteAllText($header,$current)
    $env:PATH = $originalPath
}

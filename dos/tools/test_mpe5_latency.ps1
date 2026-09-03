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
$handlers = Join-Path $Source 'Source/Teensy/MinimalBoot/Common/IO_Handlers'
$canonical = Join-Path $project 'engine/native-dos'
$header = [IO.File]::ReadAllText((Join-Path $canonical 'mpe5_firmware.h'))
$baseline = (& git -C $project show 'fdd2ef8c4cd47cdcbb810714426ae4dac4b15bb5:engine/native-dos/mpe5_firmware.h') -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Pinned R13 scheduling source is unavailable.' }
$yieldPattern = '(?ms)^static FLASHMEM bool MPE5ShouldYield\([^\n]*\)\r?\n\{.*?^\}'
$oldYield = [regex]::Match($baseline,$yieldPattern)
if (-not $oldYield.Success -or [regex]::Matches($header,$yieldPattern).Count -ne 1) { throw 'Cannot isolate yield policy.' }
$oldSlice = [regex]::Match($baseline,'static constexpr uint32_t MPE5InstructionSlice = \d+u;')
if (-not $oldSlice.Success) { throw 'Cannot isolate instruction budget.' }
$originalHandler = [IO.File]::ReadAllText((Join-Path $handlers 'IOH_MPE3TitlePull.c'))
$originalPath = $env:PATH
$result = @()
try {
    $env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"
    foreach ($variant in @('R13','R14')) {
        $variantHeader = $header
        if ($variant -eq 'R13') {
            $replacement = $oldYield.Value
            $variantHeader = [regex]::Replace($variantHeader,$yieldPattern,[System.Text.RegularExpressions.MatchEvaluator]{param($m) $replacement})
            $variantHeader = [regex]::Replace($variantHeader,'static constexpr uint32_t MPE5InstructionSlice = \d+u;',$oldSlice.Value)
        }
        # These private generated files never replace staged firmware inputs.
        # Both policies share the current core, keyboard and paging fixes.
        $variantHeader = [regex]::Replace($variantHeader,'(?m)^#include "([^"]+)"',[System.Text.RegularExpressions.MatchEvaluator]{param($m)
            '#include "' + (Join-Path $canonical $m.Groups[1].Value).Replace('\','/') + '"'
        })
        $headerPath = Join-Path $work "mpe5-latency-$($variant.ToLowerInvariant()).h"
        [IO.File]::WriteAllText($headerPath,$variantHeader)
        $handler = [regex]::Replace($originalHandler,'(?m)^#include "([^"]+)"',[System.Text.RegularExpressions.MatchEvaluator]{param($m)
            $include = if ($m.Groups[1].Value -match 'mpe5_firmware\.h$') { $headerPath } else { [IO.Path]::GetFullPath((Join-Path $handlers $m.Groups[1].Value)) }
            '#include "' + $include.Replace('\','/') + '"'
        })
        [IO.File]::WriteAllText((Join-Path $work 'IOH_MPE3TitlePull.c'),$handler)
        $exe = Join-Path $work "mpe5-latency-$($variant.ToLowerInvariant()).exe"
        & $Compiler -std=c++17 -O2 -funsigned-char -w -I $work (Join-Path $project 'dos/tests/mpe5_latency_test.cpp') -o $exe
        if ($LASTEXITCODE -ne 0) { throw "$variant latency harness compilation failed." }
        $run = & $exe $Cartridge $Image (Join-Path (Split-Path -Parent $Image) 'DOSVM.SWP') $variant
        if ($LASTEXITCODE -ne 0) { throw "$variant latency acceptance failed." }
        $result += $run; $run | Write-Output
    }
    $result | Set-Content -LiteralPath (Join-Path $work 'dos-latency-result.txt') -Encoding utf8
}
finally { $env:PATH = $originalPath }

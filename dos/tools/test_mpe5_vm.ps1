param(
    [Parameter(Mandatory = $true)]
    [string]$Image,
    [string]$Bios = '',
    [string]$Compiler = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$testSource = Join-Path $projectRoot 'dos\tests\mpe5_vm_host_test.cpp'
$defaultBios = Join-Path $projectRoot 'engine\native-dos\vendor\8086tiny\bios'
$Image = [IO.Path]::GetFullPath($Image)
if ([string]::IsNullOrWhiteSpace($Bios)) { $Bios = $defaultBios }
$Bios = [IO.Path]::GetFullPath($Bios)

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
    throw 'A host g++ compiler is required for the MPE5 VM acceptance test. Supply -Compiler with a native Windows g++.exe path.'
}
if (-not (Test-Path -LiteralPath $testSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $Image -PathType Leaf) -or
    -not (Test-Path -LiteralPath $Bios -PathType Leaf)) {
    throw 'MPE5 VM host-test source, BIOS, or DOS image is missing.'
}

$output = Join-Path ([IO.Path]::GetTempPath()) "mpe5-vm-host-test-$PID.exe"
try {
    $compilerDirectory = Split-Path -Parent $Compiler
    $env:PATH = "$compilerDirectory$([IO.Path]::PathSeparator)$env:PATH"
    foreach ($charMode in @('signed','unsigned')) {
        Write-Output "MPE5 VM acceptance with $charMode host char:"
        & $Compiler -std=c++17 -O2 -w "-f$charMode-char" $testSource (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp') -o $output
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Unable to compile the MPE5 VM acceptance test ($charMode char)."
        }
        & $output $Bios $Image
        if ($LASTEXITCODE -ne 0) { throw "MPE5 VM acceptance failed ($charMode char)." }
    }
}
finally {
    if (Test-Path -LiteralPath $output -PathType Leaf) {
        Remove-Item -LiteralPath $output -Force
    }
}

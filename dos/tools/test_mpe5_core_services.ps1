param(
    [Parameter(Mandatory = $true)][string]$Image,
    [string]$Bios = '',
    [string]$Compiler = 'C:\msys64\mingw64\bin\g++.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $Compiler = @(
        (Get-Command 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        'C:\msys64\mingw64\bin\g++.exe',
        'C:\msys64\mingw32\bin\g++.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (!$Compiler) { throw 'A native Windows g++ compiler is required. Supply -Compiler.' }
}
if (!$Bios) { $Bios = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios' }
$Image = [IO.Path]::GetFullPath($Image)
$Bios = [IO.Path]::GetFullPath($Bios)
foreach ($inputPath in @($Image, $Bios, $Compiler)) {
    if (!(Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing test input: $inputPath" }
}
$output = Join-Path ([IO.Path]::GetTempPath()) "mpe5-core-services-$PID.exe"
$originalPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $Compiler)$([IO.Path]::PathSeparator)$env:PATH"
    foreach ($charMode in @('signed', 'unsigned')) {
        Write-Output "MPE5 BIOS/redirector acceptance with $charMode host char:"
        & $Compiler -std=c++17 -O2 -w "-f$charMode-char" (Join-Path $projectRoot 'dos/tests/mpe5_core_services_test.cpp') -o $output
        if ($LASTEXITCODE -ne 0) { throw 'Unable to compile core-services regression.' }
        & $output $Bios
        if ($LASTEXITCODE -ne 0) { throw 'Core-services regression failed.' }
        & $Compiler -std=c++17 -O2 -w "-f$charMode-char" (Join-Path $projectRoot 'dos/tests/mpe5_redirector_boot_test.cpp') (Join-Path $projectRoot 'engine/native-dos/mpe5_paged_memory.cpp') -o $output
        if ($LASTEXITCODE -ne 0) { throw 'Unable to compile FreeDOS redirector acceptance.' }
        & $output $Bios $Image
        if ($LASTEXITCODE -ne 0) { throw 'FreeDOS redirector acceptance failed.' }
    }
}
finally {
    $env:PATH = $originalPath
    if (Test-Path -LiteralPath $output -PathType Leaf) { Remove-Item -LiteralPath $output -Force }
}

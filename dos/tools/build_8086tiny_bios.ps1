param(
    [string]$Assembler = 'C:\msys64\usr\bin\nasm.exe',
    [string]$Source = '',
    [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Source) { $Source = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios.asm' }
if (-not $Output) { $Output = Join-Path $projectRoot 'engine/native-dos/vendor/8086tiny/bios' }
if (-not (Test-Path -LiteralPath $Assembler)) {
    $candidate = Get-Command nasm -ErrorAction SilentlyContinue
    if (-not $candidate) { throw 'NASM is required to rebuild the tracked 8086tiny BIOS.' }
    $Assembler = $candidate.Source
}
& $Assembler -f bin $Source -o $Output
if ($LASTEXITCODE -ne 0) { throw '8086tiny BIOS assembly failed.' }
$built = Get-Item -LiteralPath $Output
if ($built.Length -eq 0 -or $built.Length -gt 0xff00) {
    throw "8086tiny BIOS must be 1..65280 bytes; got $($built.Length)."
}
$sha256 = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "8086tiny BIOS: $($built.Length) bytes SHA256 $sha256"

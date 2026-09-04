$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$crtBuild = Join-Path $projectRoot 'nes\build\crt'
New-Item -ItemType Directory -Path $crtBuild -Force | Out-Null

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}

Push-Location $projectRoot
try {
    Invoke-Checked node @('nes/tools/nes.mjs', 'audit')
    Invoke-Checked node @('nes/tools/nes.mjs', 'test')
    Invoke-Checked node @('nes/tools/nes.mjs', 'layout')
    Invoke-Checked node @('nes/tools/build_nesvm_terminal.mjs',
        '--output-prg', 'nes/build/crt/nesvm-terminal.prg',
        '--output-boot-bank', 'nes/build/crt/nesvm-bootbank.bin',
        '--manifest', 'nes/build/crt/terminal.json')
    Invoke-Checked node @('nes/tools/build_nesvm_cartridge.mjs',
        '--boot-bank', 'nes/build/crt/nesvm-bootbank.bin',
        '--output', 'nes/sd-card/NESVM.CRT',
        '--manifest', 'nes/build/crt/cartridge.json')
    Invoke-Checked node @('nes/tests/nes_crt_test.mjs', 'nes/sd-card/NESVM.CRT',
        'nes/build/crt/terminal.json', 'nes/build/crt/cartridge.json')
    Invoke-Checked node @('nes/tests/nes_c64_boot_test.mjs')
    Invoke-Checked node @('nes/tools/assemble_nesvm_sd.mjs')
    Write-Host "NESVM CRT and SD-card package passed. Nothing was published."
} finally {
    Pop-Location
}

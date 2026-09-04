# NES-only modular test entry point. No built-in engine patch chain.
param()
$ErrorActionPreference = 'Stop'
& node (Join-Path $PSScriptRoot 'build-vm-test.mjs') all
if ($LASTEXITCODE -ne 0) { throw 'Modular firmware build failed; do not use incomplete outputs.' }

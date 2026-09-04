param(
    [string]$Destination = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$fetchScript = Join-Path $PSScriptRoot 'fetch_mcume_teensydoom.ps1'
if (-not [string]::IsNullOrWhiteSpace($Destination)) {
    $json = @(& $fetchScript -Offline -Destination $Destination)
}
else {
    $json = @(& $fetchScript -Offline)
}
$result = ($json -join "`n") | ConvertFrom-Json

if ($result.verified -ne $true -or $result.offline -ne $true) {
    throw 'MCUME offline source-lock verification did not complete'
}
if ($result.commit -ne '27f6b906aca34e06d6647bdca8215e25f8d20aa5') {
    throw "MCUME offline verification returned the wrong commit: $($result.commit)"
}
if ($result.repository -ne 'https://github.com/Jean-MarcHarvengt/MCUME.git' -or
    $result.subtree -ne 'MCUME_teensy41/teensydoom' -or
    $result.fileCount -ne 184 -or $result.payloadBytes -ne 1605084 -or
    $result.treeSha256 -ne '91e9d8c5bac42aff37756b7566ddbf92b5e6cc5500761a9474c9da3a64922ffb' -or
    $result.wadFiles -ne 0 -or
    $result.licenseStatus -ne 'per-file notices; subtree-wide license unresolved' -or
    $result.engineLicenseNotice -ne 'GPL-2.0-or-later' -or
    $result.unresolvedLicenseFiles -ne 17 -or
    $result.noticePreserved -ne $true) {
    throw 'MCUME offline source-lock evidence is incomplete'
}

Write-Host "MCUME source lock: PASS ($($result.fileCount) files, $($result.treeSha256))"

param(
    [string]$Destination = '',
    [switch]$Offline
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Git([string[]]$GitArguments) {
    $output = @(& git @GitArguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git $($GitArguments -join ' ') failed with exit code $LASTEXITCODE"
    }
    return $output
}

function Get-NormalizedRepositoryUrl([string]$Url) {
    return ($Url.Trim().TrimEnd('/') -replace '\.git$', '').ToLowerInvariant()
}

function Test-IsBelow([string]$Path, [string]$Directory) {
    $prefix = $Directory.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Convert-RepoAliasPath(
    [string]$Path,
    [string]$AliasRoot,
    [string]$CanonicalRoot
) {
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full.Equals($AliasRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $CanonicalRoot
    }
    if (Test-IsBelow $full $AliasRoot) {
        return $CanonicalRoot + $full.Substring($AliasRoot.Length)
    }
    return $full
}

function Assert-NoReparsePath([string]$Path, [string]$Boundary) {
    $current = [System.IO.Path]::GetFullPath($Path)
    $boundaryFull = [System.IO.Path]::GetFullPath($Boundary)
    while ($true) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing MCUME source operations through reparse point: $current"
            }
        }
        if ($current.Equals($boundaryFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parent = [System.IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrWhiteSpace($parent) -or
            -not (Test-IsBelow $current $boundaryFull)) {
            throw "MCUME source path escaped its verified boundary: $Path"
        }
        $current = $parent
    }
}

function Get-Sha256HexFromStream([System.IO.Stream]$Stream) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($Stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

function Get-Sha256HexFromBytes([byte[]]$Bytes) {
    $stream = [System.IO.MemoryStream]::new($Bytes, $false)
    try {
        return Get-Sha256HexFromStream $stream
    }
    finally {
        $stream.Dispose()
    }
}

$projectAliasRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectRoot = $projectAliasRoot
$projectRootItem = Get-Item -LiteralPath $projectAliasRoot -Force
if (($projectRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    $targets = @($projectRootItem.Target)
    if ($targets.Count -ne 1 -or [string]::IsNullOrWhiteSpace($targets[0])) {
        throw "Repository junction must have exactly one target: $projectAliasRoot"
    }
    $target = $targets[0]
    if (-not [System.IO.Path]::IsPathRooted($target)) {
        $target = Join-Path (Split-Path -Parent $projectAliasRoot) $target
    }
    $projectRoot = [System.IO.Path]::GetFullPath($target)
}
$originPath = Join-Path $projectRoot 'doom\third_party\mcume-teensydoom.origin.json'
$origin = Get-Content -Raw -LiteralPath $originPath | ConvertFrom-Json

if ($origin.schemaVersion -ne 1 -or
    $origin.commit -notmatch '^[0-9a-f]{40}$' -or
    [string]::IsNullOrWhiteSpace($origin.repository) -or
    [string]::IsNullOrWhiteSpace($origin.subtree)) {
    throw 'The MCUME origin record is malformed'
}
if ($origin.wadPolicy.included -ne $false) {
    throw 'The MCUME origin record must exclude WAD data'
}

$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'build\doom\upstream'))
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $projectRoot $origin.checkout.defaultPath
}
$Destination = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($Destination)) `
    $projectAliasRoot $projectRoot
$allowedPrefix = $allowedRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $Destination.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "MCUME checkout must remain below $allowedRoot"
}
Assert-NoReparsePath $Destination $projectRoot

$destinationParent = Split-Path -Parent $Destination
New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
$gitDirectory = Join-Path $Destination '.git'
if (-not (Test-Path -LiteralPath $gitDirectory -PathType Container)) {
    if ($Offline) {
        throw "Offline verification requires an existing checkout at $Destination"
    }
    if (Test-Path -LiteralPath $Destination) {
        $existing = @(Get-ChildItem -LiteralPath $Destination -Force)
        if ($existing.Count) {
            throw "Destination exists and is not an MCUME Git checkout: $Destination"
        }
    }
    else {
        New-Item -ItemType Directory -Path $Destination | Out-Null
    }
    Invoke-Git @('-C', $Destination, 'init', '--quiet') | Out-Null
    Invoke-Git @('-C', $Destination, 'remote', 'add', 'origin', $origin.repository) | Out-Null
}
$gitDirectoryItem = Get-Item -LiteralPath $gitDirectory -Force
if (($gitDirectoryItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "MCUME checkout .git directory must not be a reparse point: $gitDirectory"
}

$actualRepository = [string](Invoke-Git @('-C', $Destination, 'remote', 'get-url', 'origin'))
if ((Get-NormalizedRepositoryUrl $actualRepository) -ne
    (Get-NormalizedRepositoryUrl $origin.repository)) {
    throw "Existing checkout has the wrong origin: $actualRepository"
}

$dirtyBefore = @((Invoke-Git @('-C', $Destination, 'status', '--porcelain=v1', '--untracked-files=all')) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($dirtyBefore.Count) {
    throw "MCUME checkout has local changes; refusing to replace them: $($dirtyBefore -join ', ')"
}

Invoke-Git @('-C', $Destination, 'config', 'core.autocrlf', 'false') | Out-Null
Invoke-Git @('-C', $Destination, 'config', 'remote.origin.promisor', 'true') | Out-Null
Invoke-Git @('-C', $Destination, 'config', 'remote.origin.partialclonefilter', 'blob:none') | Out-Null

if (-not $Offline) {
    Invoke-Git @('-C', $Destination, 'fetch', '--quiet', '--depth', '1',
        '--filter=blob:none', 'origin', $origin.commit) | Out-Null
}

$commitExpression = "$($origin.commit)^{commit}"
$resolvedCommit = [string](Invoke-Git @('-C', $Destination, 'rev-parse', '--verify', $commitExpression))
if ($resolvedCommit.Trim().ToLowerInvariant() -ne $origin.commit) {
    throw "Pinned MCUME commit did not resolve exactly: $resolvedCommit"
}

# Inspect tree names before checkout/archive causes any selected blobs to be
# materialized. The chosen source subtree must never contain commercial game
# data or any other WAD.
$treeLines = @(Invoke-Git @('-C', $Destination, 'ls-tree', '-r', $origin.commit, '--', $origin.subtree))
$treeEntries = @()
foreach ($line in $treeLines) {
    if ($line -notmatch '^([0-9]{6})\s+blob\s+([0-9a-f]+)\t(.+)$') {
        throw "Unexpected MCUME tree entry: $line"
    }
    $fullPath = $Matches[3]
    if ([System.IO.Path]::GetExtension($fullPath) -ieq '.wad') {
        throw "Pinned MCUME source subtree contains a forbidden WAD path: $fullPath"
    }
    $treeEntries += [pscustomobject]@{
        Mode = $Matches[1]
        Object = $Matches[2]
        FullPath = $fullPath
        RelativePath = $fullPath.Substring($origin.subtree.Length + 1)
    }
}
if (-not $treeEntries.Count) {
    throw 'Pinned MCUME source subtree is empty'
}

if (-not $Offline) {
    Invoke-Git @('-C', $Destination, 'sparse-checkout', 'init', '--no-cone') | Out-Null
    Invoke-Git @('-C', $Destination, 'sparse-checkout', 'set', '--no-cone',
        "/$($origin.subtree)/") | Out-Null
    Invoke-Git @('-C', $Destination, 'checkout', '--quiet', '--detach', $origin.commit) | Out-Null
}

$head = [string](Invoke-Git @('-C', $Destination, 'rev-parse', 'HEAD'))
if ($head.Trim().ToLowerInvariant() -ne $origin.commit) {
    throw "MCUME checkout HEAD is not the pinned commit: $head"
}
$dirtyAfter = @((Invoke-Git @('-C', $Destination, 'status', '--porcelain=v1', '--untracked-files=all')) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($dirtyAfter.Count) {
    throw "Pinned MCUME checkout is not clean: $($dirtyAfter -join ', ')"
}

$wadFiles = @(Get-ChildItem -LiteralPath $Destination -Recurse -Force -File |
    Where-Object { $_.Extension -ieq '.wad' })
if ($wadFiles.Count) {
    throw "Forbidden WAD materialized under the MCUME checkout: $($wadFiles.FullName -join ', ')"
}

# Hash bytes from a Git archive, not the platform working tree. This avoids
# autocrlf and filesystem metadata differences. Paths and Git modes are part of
# the canonical LF inventory, then the complete inventory receives one SHA-256.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = Join-Path $destinationParent ('.mcume-teensydoom-{0}.zip' -f [guid]::NewGuid().ToString('N'))
$oldNoLazyFetch = $env:GIT_NO_LAZY_FETCH
if ($Offline) { $env:GIT_NO_LAZY_FETCH = '1' }
try {
    Invoke-Git @('-C', $Destination, 'archive', '--format=zip', "--output=$archivePath",
        $origin.commit, '--', "$($origin.subtree)/") | Out-Null
    $modeByPath = @{}
    foreach ($entry in $treeEntries) { $modeByPath[$entry.FullPath] = $entry.Mode }
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $records = [System.Collections.Generic.List[object]]::new()
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            if (-not $modeByPath.ContainsKey($entry.FullName)) {
                throw "Archive contains an unexpected path: $($entry.FullName)"
            }
            $stream = $entry.Open()
            try { $fileSha256 = Get-Sha256HexFromStream $stream }
            finally { $stream.Dispose() }
            $records.Add([pscustomobject]@{
                Mode = $modeByPath[$entry.FullName]
                Sha256 = $fileSha256
                Bytes = [uint64]$entry.Length
                RelativePath = $entry.FullName.Substring($origin.subtree.Length + 1)
            })
        }
    }
    finally {
        $zip.Dispose()
    }
}
finally {
    if ($Offline) {
        if ($null -eq $oldNoLazyFetch) { Remove-Item Env:GIT_NO_LAZY_FETCH -ErrorAction SilentlyContinue }
        else { $env:GIT_NO_LAZY_FETCH = $oldNoLazyFetch }
    }
    if (Test-Path -LiteralPath $archivePath) {
        [System.IO.File]::Delete($archivePath)
    }
}

$records.Sort([System.Comparison[object]]{
    param($left, $right)
    return [System.StringComparer]::Ordinal.Compare($left.RelativePath, $right.RelativePath)
})
$inventoryLines = [System.Collections.Generic.List[string]]::new()
[uint64]$payloadBytes = 0
foreach ($record in $records) {
    $inventoryLines.Add("$($record.Mode) $($record.Sha256) $($record.Bytes) $($record.RelativePath)")
    $payloadBytes += $record.Bytes
}
$inventoryText = [string]::Join("`n", $inventoryLines) + "`n"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$inventoryBytes = $utf8NoBom.GetBytes($inventoryText)
$treeSha256 = Get-Sha256HexFromBytes $inventoryBytes

$inventoryPath = [System.IO.Path]::GetFullPath(
    (Join-Path $projectRoot $origin.checkout.inventoryPath))
if (-not (Test-IsBelow $inventoryPath $allowedRoot)) {
    throw "MCUME inventory path must remain below $allowedRoot"
}
Assert-NoReparsePath $inventoryPath $projectRoot
$noticePath = Join-Path (Join-Path $Destination $origin.subtree) $origin.license.noticeFile
if (-not (Test-Path -LiteralPath $noticePath -PathType Leaf)) {
    throw "Required GPL notice file is missing: $noticePath"
}
$notice = Get-Content -Raw -LiteralPath $noticePath
if ($notice.IndexOf($origin.license.requiredText, [System.StringComparison]::Ordinal) -lt 0 -or
    $notice.IndexOf('either version 2', [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw "Required GPL-2.0-or-later notice was not preserved in $noticePath"
}
$unresolvedLicenseFiles = @($origin.license.unresolvedFiles)
if ($origin.license.status -ne 'per-file notices; subtree-wide license unresolved' -or
    $origin.license.engineExpression -ne 'GPL-2.0-or-later' -or
    $unresolvedLicenseFiles.Count -ne 17) {
    throw 'The MCUME per-file license-provenance record is incomplete'
}
foreach ($relativePath in $unresolvedLicenseFiles) {
    $unresolvedPath = Join-Path (Join-Path $Destination $origin.subtree) $relativePath
    if (-not (Test-Path -LiteralPath $unresolvedPath -PathType Leaf)) {
        throw "Recorded unresolved-license file is missing: $relativePath"
    }
}

if ($records.Count -ne [int]$origin.fileCount) {
    throw "MCUME file count changed: expected $($origin.fileCount), observed $($records.Count)"
}
if ([uint64]$origin.payloadBytes -ne $payloadBytes) {
    throw "MCUME payload size changed: expected $($origin.payloadBytes), observed $payloadBytes"
}
if ($origin.treeSha256 -notmatch '^[0-9a-f]{64}$' -or
    $treeSha256 -ne $origin.treeSha256) {
    throw "MCUME tree digest changed: expected $($origin.treeSha256), observed $treeSha256"
}
[System.IO.File]::WriteAllText($inventoryPath, $inventoryText, $utf8NoBom)

[ordered]@{
    verified = $true
    offline = [bool]$Offline
    repository = $origin.repository
    commit = $head.Trim().ToLowerInvariant()
    subtree = $origin.subtree
    checkout = $Destination
    fileCount = $records.Count
    payloadBytes = $payloadBytes
    treeSha256 = $treeSha256
    inventory = $inventoryPath
    wadFiles = 0
    licenseStatus = $origin.license.status
    engineLicenseNotice = $origin.license.engineExpression
    noticePreserved = $true
    unresolvedLicenseFiles = $unresolvedLicenseFiles.Count
} | ConvertTo-Json -Depth 4

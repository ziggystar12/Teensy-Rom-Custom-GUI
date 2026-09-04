param(
    [string]$Checkout = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-IsBelow([string]$Path, [string]$Directory) {
    $prefix = $Directory.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparsePath([string]$Path, [string]$Boundary) {
    $current = [System.IO.Path]::GetFullPath($Path)
    $boundaryFull = [System.IO.Path]::GetFullPath($Boundary)
    while ($true) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing recursive cleanup through reparse point: $current"
            }
        }
        if ($current.Equals($boundaryFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parent = [System.IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrWhiteSpace($parent) -or
            -not (Test-IsBelow $current $boundaryFull)) {
            throw "Cleanup path escaped its verified boundary: $Path"
        }
        $current = $parent
    }
}

function Assert-NoDescendantReparse([string]$Path) {
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push([System.IO.Path]::GetFullPath($Path))
    while ($pending.Count -ne 0) {
        $directory = $pending.Pop()
        foreach ($child in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing recursive cleanup containing reparse point: $($child.FullName)"
            }
            if ($child.PSIsContainer) {
                $pending.Push($child.FullName)
            }
        }
    }
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

function Invoke-NativeCapture(
    [string]$Command,
    [string[]]$Arguments
) {
    $previousErrorActionPreference = $ErrorActionPreference
    $output = @()
    $exitCode = $null
    try {
        # Windows PowerShell 5.1 represents native stderr as error records.
        # Preserve it as diagnostics and use only the process exit code here.
        $ErrorActionPreference = 'Continue'
        $output = @(& $Command @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    return [pscustomobject]@{
        Output = $output
        ExitCode = $exitCode
    }
}

function Invoke-SourceLock([string]$Script, [string]$Destination) {
    $output = @(& $Script -Offline -Destination $Destination)
    if ($LASTEXITCODE -ne 0) {
        throw 'Pinned MCUME source-lock verification failed.'
    }
    $lock = ($output -join "`n") | ConvertFrom-Json
    if ($lock.verified -ne $true -or $lock.offline -ne $true -or
        $lock.wadFiles -ne 0) {
        throw 'Pinned MCUME source-lock evidence is incomplete.'
    }
    return $lock
}

$repoAliasRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$repoRoot = $repoAliasRoot
$repoRootItem = Get-Item -LiteralPath $repoAliasRoot -Force
if (($repoRootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    $targets = @($repoRootItem.Target)
    if ($targets.Count -ne 1 -or [string]::IsNullOrWhiteSpace($targets[0])) {
        throw "Repository junction must have exactly one target: $repoAliasRoot"
    }
    $target = $targets[0]
    if (-not [System.IO.Path]::IsPathRooted($target)) {
        $target = Join-Path (Split-Path -Parent $repoAliasRoot) $target
    }
    $repoRoot = [System.IO.Path]::GetFullPath($target)
}
$originPath = Join-Path $repoRoot 'doom\third_party\mcume-teensydoom.origin.json'
$origin = Get-Content -Raw -LiteralPath $originPath | ConvertFrom-Json
$sourceLockScript = Join-Path $repoRoot 'doom\tools\fetch_mcume_teensydoom.ps1'

if ([string]::IsNullOrWhiteSpace($Checkout)) {
    $Checkout = Join-Path $repoRoot $origin.checkout.defaultPath
}
$Checkout = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($Checkout)) `
    $repoAliasRoot $repoRoot
$sourceRoot = Join-Path $Checkout ($origin.subtree -replace '/', '\')
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "Pinned MCUME source is missing: $sourceRoot"
}

$lockBefore = Invoke-SourceLock $sourceLockScript $Checkout
if ($lockBefore.commit -ne $origin.commit -or
    $lockBefore.treeSha256 -ne $origin.treeSha256) {
    throw 'Pinned MCUME source does not match its origin record.'
}

$allowedOutputRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'build\doom\adapted'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $allowedOutputRoot 'mcume-teensydoom'
}
$OutputRoot = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($OutputRoot)) `
    $repoAliasRoot $repoRoot
if (-not (Test-IsBelow $OutputRoot $allowedOutputRoot)) {
    throw "Adapted output must remain below $allowedOutputRoot"
}

Assert-NoReparsePath $OutputRoot $repoRoot
if (Test-Path -LiteralPath $OutputRoot) {
    Assert-NoDescendantReparse $OutputRoot
    $resolvedOutput = [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $OutputRoot).Path)
    if (-not (Test-IsBelow $resolvedOutput $allowedOutputRoot)) {
        throw "Refusing to replace adapted output outside $allowedOutputRoot"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

Get-ChildItem -LiteralPath $sourceRoot -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $OutputRoot -Recurse -Force
}

$overlayRoot = Join-Path $repoRoot 'doom\third_party\mcume-native-adapter'
foreach ($name in @('mhs_native_adapter.h', 'mhs_native_adapter.c')) {
    Copy-Item -LiteralPath (Join-Path $overlayRoot $name) `
        -Destination (Join-Path $OutputRoot $name) -Force
}

$patchPath = Join-Path $repoRoot 'doom\patches\mcume-teensydoom-native-adapter.patch'
$initResult = Invoke-NativeCapture 'git' @('-C', $OutputRoot, 'init', '--quiet')
if ($initResult.ExitCode -ne 0) {
    throw 'Unable to initialize the generated adapted-tree workspace.'
}
$checkResult = Invoke-NativeCapture 'git' @(
    '-C', $OutputRoot, 'apply', '--check', '--whitespace=error-all', $patchPath)
if ($checkResult.ExitCode -ne 0) {
    throw "MCUME native adapter patch check failed.`n$($checkResult.Output -join [Environment]::NewLine)"
}
$applyResult = Invoke-NativeCapture 'git' @(
    '-C', $OutputRoot, 'apply', '--whitespace=error-all', $patchPath)
if ($applyResult.ExitCode -ne 0) {
    throw "MCUME native adapter patch failed.`n$($applyResult.Output -join [Environment]::NewLine)"
}

$requiredMarkers = [ordered]@{
    'd_loop.c' = 'boolean D_RunSingleTic(void)'
    'd_main.c' = '#ifndef MHS_NATIVE_DOOM_ADAPTER'
    'i_video.c' = 'const byte *MHS_I_CurrentPalette(size_t *bytes)'
    'mhs_native_adapter.c' = 'int MHS_DoomRunOneTic('
}
foreach ($entry in $requiredMarkers.GetEnumerator()) {
    $path = Join-Path $OutputRoot $entry.Key
    if (-not (Select-String -LiteralPath $path -SimpleMatch $entry.Value -Quiet)) {
        throw "Adapted source marker missing from $($entry.Key): $($entry.Value)"
    }
}

$wadFiles = @(Get-ChildItem -LiteralPath $OutputRoot -Recurse -Force -File |
    Where-Object { $_.Extension -ieq '.wad' })
if ($wadFiles.Count -ne 0) {
    throw 'The generated adapted source tree unexpectedly contains WAD data.'
}

$lockAfter = Invoke-SourceLock $sourceLockScript $Checkout
if ($lockAfter.commit -ne $lockBefore.commit -or
    $lockAfter.treeSha256 -ne $lockBefore.treeSha256) {
    throw 'Applying the overlay changed the pinned source checkout.'
}

[ordered]@{
    status = 'PASS'
    acceptance = 'source-overlay-only'
    sourceCommit = $lockAfter.commit
    sourceTreeSha256 = $lockAfter.treeSha256
    patchSha256 = (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash.ToLowerInvariant()
    adapterHeaderSha256 = (Get-FileHash -LiteralPath (Join-Path $overlayRoot 'mhs_native_adapter.h') -Algorithm SHA256).Hash.ToLowerInvariant()
    adapterSourceSha256 = (Get-FileHash -LiteralPath (Join-Path $overlayRoot 'mhs_native_adapter.c') -Algorithm SHA256).Hash.ToLowerInvariant()
    outputRoot = $OutputRoot
    sourceFiles = @(Get-ChildItem -LiteralPath $OutputRoot -File).Count
    wadFiles = 0
    pinnedCheckoutStillClean = $true
} | ConvertTo-Json -Compress

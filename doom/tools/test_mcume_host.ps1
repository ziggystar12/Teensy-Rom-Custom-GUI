param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WadPath,
    [string]$Compiler = '',
    [string]$Checkout = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RelativePath([string]$BasePath, [string]$Path) {
    $baseUri = [System.Uri]::new($BasePath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar)
    $pathUri = [System.Uri]::new($Path)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
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
                throw "Refusing Doom host output through reparse point: $current"
            }
        }
        if ($current.Equals($boundaryFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $parent = [System.IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrWhiteSpace($parent) -or
            -not (Test-IsBelow $current $boundaryFull)) {
            throw "Doom host output escaped its verified boundary: $Path"
        }
        $current = $parent
    }
}

function Get-BoundedFailure([object[]]$Lines) {
    $diagnostics = @($Lines | Where-Object {
        $_.ToString() -match '(?i)error:|undefined reference|multiple definition|ld(?:\.exe)?:'
    })
    $bounded = @($diagnostics | Select-Object -First 20)
    $bounded += @($Lines | Select-Object -Last 20)
    return (($bounded | Select-Object -Unique) -join [Environment]::NewLine)
}

function Invoke-NativeCapture([string]$FilePath, [object[]]$ArgumentList) {
    # Windows PowerShell 5.1 promotes redirected native stderr to an error
    # record. Capture it under Continue so warnings remain diagnostics, then
    # make the native exit code the sole success boundary.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $FilePath @ArgumentList 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    return [pscustomobject]@{
        Lines = $lines
        ExitCode = $exitCode
    }
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
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
$originPath = Join-Path $repoRoot 'doom\third_party\mcume-teensydoom.origin.json'
$origin = Get-Content -Raw -LiteralPath $originPath | ConvertFrom-Json

if ($origin.schemaVersion -ne 1 -or $origin.commit -notmatch '^[0-9a-f]{40}$') {
    throw 'The MCUME source-lock record is malformed.'
}

$WadPath = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($WadPath)) `
    $repoAliasRoot $repoRoot
if (-not (Test-Path -LiteralPath $WadPath -PathType Leaf)) {
    throw "WAD not found: $WadPath"
}
if ([System.IO.Path]::GetExtension($WadPath) -ine '.wad') {
    throw 'The explicitly supplied -WadPath must name a .wad file.'
}
if (Test-IsBelow $WadPath $repoRoot) {
    $wadRelative = Get-RelativePath $repoRoot $WadPath
    $trackedWad = @(& git -C $repoRoot ls-files -- $wadRelative)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to confirm that the supplied WAD is untracked.'
    }
    if ($trackedWad.Count -ne 0) {
        throw "Refusing to use a tracked WAD: $WadPath"
    }
}

$wadStream = [System.IO.File]::OpenRead($WadPath)
try {
    $wadHeaderBytes = [byte[]]::new(4)
    if ($wadStream.Read($wadHeaderBytes, 0, 4) -ne 4) {
        throw 'The supplied WAD is too short to contain a valid header.'
    }
}
finally {
    $wadStream.Dispose()
}
$wadHeader = [System.Text.Encoding]::ASCII.GetString($wadHeaderBytes)
if ($wadHeader -notin @('IWAD', 'PWAD')) {
    throw "The supplied file has an invalid WAD header: $wadHeader"
}

if ([string]::IsNullOrWhiteSpace($Checkout)) {
    $Checkout = Join-Path $repoRoot $origin.checkout.defaultPath
}
$Checkout = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($Checkout)) `
    $repoAliasRoot $repoRoot
$sourceRoot = Join-Path $Checkout ($origin.subtree -replace '/', '\')
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "Pinned MCUME source is missing. Run doom/tools/fetch_mcume_teensydoom.ps1 first: $Checkout"
}

$sourceLockScript = Join-Path $repoRoot 'doom\tools\fetch_mcume_teensydoom.ps1'
$sourceLockOutput = @(& $sourceLockScript -Offline -Destination $Checkout)
if ($LASTEXITCODE -ne 0) {
    throw 'Pinned MCUME source-lock verification failed.'
}
$sourceLock = ($sourceLockOutput -join "`n") | ConvertFrom-Json
if ($sourceLock.verified -ne $true -or $sourceLock.offline -ne $true -or
    $sourceLock.commit -ne $origin.commit -or $sourceLock.wadFiles -ne 0) {
    throw 'Pinned MCUME source-lock evidence is incomplete.'
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $buildRoot 'doom\mcume-host-proof'
}
$OutputRoot = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($OutputRoot)) `
    $repoAliasRoot $repoRoot
if (-not (Test-IsBelow $OutputRoot $buildRoot)) {
    throw "Host-proof output must remain below the ignored build directory: $buildRoot"
}
Assert-NoReparsePath $OutputRoot $repoRoot
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

if ([string]::IsNullOrWhiteSpace($Compiler) -and
    -not [string]::IsNullOrWhiteSpace($env:CC)) {
    $Compiler = $env:CC
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $knownCompiler = 'C:\msys64\mingw32\bin\gcc.exe'
    if (Test-Path -LiteralPath $knownCompiler) {
        $Compiler = $knownCompiler
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command 'i686-w64-mingw32-gcc' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Compiler = $command.Source
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command 'gcc' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Compiler = $command.Source
    }
}
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    throw 'No C compiler found. Install 32-bit MinGW or pass -Compiler.'
}
if (Test-Path -LiteralPath $Compiler) {
    $Compiler = (Resolve-Path -LiteralPath $Compiler).Path
}

$compilerDirectory = Split-Path -Parent $Compiler
$originalPath = $env:Path
try {
    if (-not [string]::IsNullOrWhiteSpace($compilerDirectory)) {
        $env:Path = "$compilerDirectory;$env:Path"
    }
    $compilerTarget = ((& $Compiler -dumpmachine) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $compilerTarget -notmatch '^(i[3-6]86|x86)-.*mingw32$') {
        throw "The Doom host proof requires 32-bit MinGW; compiler target is '$compilerTarget'."
    }

$hostRoot = Join-Path $repoRoot 'doom\host'
$executable = Join-Path $OutputRoot 'mhs-mcume-doom-host32.exe'
$framePath = Join-Path $OutputRoot 'e1m1-final.indexed'
$palettePath = Join-Path $OutputRoot 'playpal.rgb24'
foreach ($generatedPath in @($executable, $framePath, $palettePath)) {
    if (Test-Path -LiteralPath $generatedPath) {
        Remove-Item -LiteralPath $generatedPath -Force
    }
}

$upstreamSources = @(Get-ChildItem -LiteralPath $sourceRoot -File -Filter '*.c' |
    Where-Object { $_.Name -ne 'i_main.c' } |
    Sort-Object Name |
    ForEach-Object { $_.FullName })
if ($upstreamSources.Count -ne 77) {
    throw "Pinned MCUME source set changed: expected 77 compilable C files, found $($upstreamSources.Count)."
}

$compileArgs = @(
    '-std=gnu11',
    '-O2',
    '-funsigned-char',
    '-Wall',
    '-Wextra',
    '-Wno-unused-parameter',
    '-Wno-unused-variable',
    '-Wno-sign-compare',
    '-Wno-format',
    '-Wno-error=incompatible-pointer-types',
    '-I', $hostRoot,
    '-I', $sourceRoot,
    '-include', (Join-Path $hostRoot 'mhs_mcume_host_compat.h')
)
$compileArgs += $upstreamSources
$compileArgs += @(
    (Join-Path $hostRoot 'mhs_mcume_host_shim.c'),
    (Join-Path $hostRoot 'mhs_mcume_host_main.c'),
    '-o', $executable,
    '-lm'
)

$compileResult = Invoke-NativeCapture $Compiler $compileArgs
$compileLog = @($compileResult.Lines)
if ($compileResult.ExitCode -ne 0) {
    throw "MCUME Doom host compilation failed.`n$(Get-BoundedFailure $compileLog)"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw 'MCUME Doom host compiler did not produce an executable.'
}

$runEvidence = @()
$runArtifacts = @()
for ($run = 1; $run -le 2; ++$run) {
    Push-Location -LiteralPath $OutputRoot
    try {
        $runResult = Invoke-NativeCapture $executable @(
            $WadPath, $framePath, $palettePath)
        $runOutput = @($runResult.Lines)
        $runExitCode = $runResult.ExitCode
    }
    finally {
        Pop-Location
    }
    if ($runExitCode -ne 0) {
        throw "MCUME Doom host run $run failed with exit code $runExitCode.`n$(Get-BoundedFailure $runOutput)"
    }
    try {
        $evidence = ($runOutput -join "`n") | ConvertFrom-Json
    }
    catch {
        throw "MCUME Doom host run $run did not emit valid JSON.`n$(Get-BoundedFailure $runOutput)"
    }
    if ($evidence.status -ne 'PASS' -or $evidence.pointerBits -ne 32 -or
        $evidence.screenWidth -ne 320 -or $evidence.screenHeight -ne 200 -or
        $evidence.episode -ne 1 -or $evidence.map -ne 1 -or
        $evidence.levelFrames -ne $evidence.iterations -or
        $evidence.changedTransitions -lt 200 -or
        $evidence.uniqueFrameHashes -lt 128 -or
        $evidence.frameBytes -ne 64000 -or $evidence.paletteBytes -ne 768) {
        throw "MCUME Doom host run $run did not satisfy the E1M1 proof contract."
    }
    if ($evidence.playerMoved -ne $true -or $evidence.playerTurned -ne $true -or
        $evidence.ammoSpent -le 0) {
        throw "MCUME Doom host run $run did not prove movement, turning, and firing in game state."
    }
    $runEvidence += $evidence

    $runFrame = Get-Item -LiteralPath $framePath
    $runPalette = Get-Item -LiteralPath $palettePath
    if ($runFrame.Length -ne 64000 -or $runPalette.Length -ne 768) {
        throw "MCUME Doom host run $run wrote invalid artifact sizes."
    }
    $runArtifacts += [pscustomobject]@{
        FrameSha256 = (Get-FileHash -LiteralPath $framePath -Algorithm SHA256).Hash.ToLowerInvariant()
        PaletteSha256 = (Get-FileHash -LiteralPath $palettePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$deterministicFields = @(
    'iterations', 'levelFrames', 'changedTransitions', 'uniqueFrameHashes',
    'firstFrameFnv1a', 'lastFrameFnv1a', 'playerMoved', 'playerTurned',
    'ammoSpent'
)
foreach ($field in $deterministicFields) {
    if ($runEvidence[0].$field -ne $runEvidence[1].$field) {
        throw "MCUME Doom host runs were not deterministic: field '$field' changed."
    }
}
if ($runArtifacts[0].FrameSha256 -ne $runArtifacts[1].FrameSha256 -or
    $runArtifacts[0].PaletteSha256 -ne $runArtifacts[1].PaletteSha256) {
    throw 'MCUME Doom host runs produced different frame or palette artifacts.'
}

$frame = Get-Item -LiteralPath $framePath
$palette = Get-Item -LiteralPath $palettePath
if ($frame.Length -ne 64000 -or $palette.Length -ne 768) {
    throw "Generated Doom artifacts have invalid sizes: frame=$($frame.Length), palette=$($palette.Length)."
}

$wadHash = (Get-FileHash -LiteralPath $WadPath -Algorithm SHA256).Hash.ToLowerInvariant()
$frameHash = $runArtifacts[1].FrameSha256
$paletteHash = $runArtifacts[1].PaletteSha256

[ordered]@{
    status = 'PASS'
    acceptance = 'host-only'
    sourceCommit = $sourceLock.commit
    sourceTreeSha256 = $sourceLock.treeSha256
    compilerTarget = $compilerTarget
    deterministicRuns = 2
    episode = 1
    map = 1
    screen = '320x200 indexed'
    iterations = $runEvidence[0].iterations
    levelFrames = $runEvidence[0].levelFrames
    changedTransitions = $runEvidence[0].changedTransitions
    uniqueFrameHashes = $runEvidence[0].uniqueFrameHashes
    firstFrameFnv1a = $runEvidence[0].firstFrameFnv1a
    lastFrameFnv1a = $runEvidence[0].lastFrameFnv1a
    playerMoved = $runEvidence[0].playerMoved
    playerTurned = $runEvidence[0].playerTurned
    ammoSpent = $runEvidence[0].ammoSpent
    wad = [ordered]@{
        path = $WadPath
        bytes = (Get-Item -LiteralPath $WadPath).Length
        sha256 = $wadHash
    }
    finalFrame = [ordered]@{
        path = $framePath
        bytes = $frame.Length
        sha256 = $frameHash
    }
    playpal = [ordered]@{
        path = $palettePath
        bytes = $palette.Length
        sha256 = $paletteHash
    }
} | ConvertTo-Json -Depth 4 -Compress
}
finally {
    $env:Path = $originalPath
}

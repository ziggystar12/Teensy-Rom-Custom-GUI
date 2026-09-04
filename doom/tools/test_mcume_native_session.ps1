param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WadPath,
    [string]$CCompiler = '',
    [string]$CxxCompiler = '',
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

function Get-RelativePath([string]$BasePath, [string]$Path) {
    $baseUri = [System.Uri]::new($BasePath.TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar)
    $pathUri = [System.Uri]::new($Path)
    return [System.Uri]::UnescapeDataString(
        $baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
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

function Get-BoundedFailure([object[]]$Lines) {
    $diagnostics = @($Lines | Where-Object {
        $_.ToString() -match '(?i)error:|undefined reference|multiple definition|FAIL:'
    } | Select-Object -First 24)
    $diagnostics += @($Lines | Select-Object -Last 16)
    return (($diagnostics | Select-Object -Unique) -join [Environment]::NewLine)
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

function Invoke-Compile(
    [string]$Compiler,
    [string[]]$Arguments,
    [string]$Description
) {
    $previousPath = $env:Path
    try {
        $compilerBin = Split-Path -Parent $Compiler
        $env:Path = "$compilerBin;$previousPath"
        $result = Invoke-NativeCapture $Compiler $Arguments
    }
    finally {
        $env:Path = $previousPath
    }
    if ($result.ExitCode -ne 0) {
        throw "$Description failed.`n$(Get-BoundedFailure $result.Output)"
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
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build\doom'))
$origin = Get-Content -Raw -LiteralPath (
    Join-Path $repoRoot 'doom\third_party\mcume-teensydoom.origin.json') |
    ConvertFrom-Json

$WadPath = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($WadPath)) `
    $repoAliasRoot $repoRoot
if (-not (Test-Path -LiteralPath $WadPath -PathType Leaf) -or
    [System.IO.Path]::GetExtension($WadPath) -ine '.wad') {
    throw "An existing external .wad is required: $WadPath"
}
$wadStream = [System.IO.File]::OpenRead($WadPath)
try {
    $headerBytes = [byte[]]::new(4)
    if ($wadStream.Read($headerBytes, 0, 4) -ne 4) {
        throw 'The supplied WAD has no complete header.'
    }
}
finally {
    $wadStream.Dispose()
}
if ([System.Text.Encoding]::ASCII.GetString($headerBytes) -notin @('IWAD', 'PWAD')) {
    throw 'The supplied WAD does not have an IWAD/PWAD header.'
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

if ([string]::IsNullOrWhiteSpace($Checkout)) {
    $Checkout = Join-Path $repoRoot $origin.checkout.defaultPath
}
$Checkout = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($Checkout)) `
    $repoAliasRoot $repoRoot

$applyScript = Join-Path $repoRoot 'doom\tools\apply_mcume_native_adapter.ps1'
$applyOutput = @(& $applyScript -Checkout $Checkout)
if ($LASTEXITCODE -ne 0) {
    throw 'MCUME native adapter overlay generation failed.'
}
$overlay = ($applyOutput -join "`n") | ConvertFrom-Json
if ($overlay.status -ne 'PASS' -or $overlay.sourceCommit -ne $origin.commit -or
    $overlay.wadFiles -ne 0 -or $overlay.pinnedCheckoutStillClean -ne $true) {
    throw 'MCUME native adapter overlay evidence is incomplete.'
}
$adaptedRoot = [System.IO.Path]::GetFullPath($overlay.outputRoot)

if ([string]::IsNullOrWhiteSpace($CCompiler)) {
    $CCompiler = 'C:\msys64\mingw32\bin\gcc.exe'
}
if ([string]::IsNullOrWhiteSpace($CxxCompiler)) {
    $CxxCompiler = 'C:\msys64\mingw32\bin\g++.exe'
}
foreach ($compiler in @($CCompiler, $CxxCompiler)) {
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
        throw "Required 32-bit compiler not found: $compiler"
    }
}
$CCompiler = (Resolve-Path -LiteralPath $CCompiler).Path
$CxxCompiler = (Resolve-Path -LiteralPath $CxxCompiler).Path
$compilerDirectory = Split-Path -Parent $CCompiler
$cTarget = ((& $CCompiler -dumpmachine) -join '').Trim()
$cxxTarget = ((& $CxxCompiler -dumpmachine) -join '').Trim()
if ($cTarget -notmatch '^(i[3-6]86|x86)-.*mingw32$' -or
    $cxxTarget -ne $cTarget) {
    throw "The proof requires matching 32-bit MinGW C/C++ compilers; got '$cTarget' and '$cxxTarget'."
}

$allowedProofRoot = Join-Path $buildRoot 'mcume-native-session-proof'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $allowedProofRoot 'default'
}
$OutputRoot = Convert-RepoAliasPath ([System.IO.Path]::GetFullPath($OutputRoot)) `
    $repoAliasRoot $repoRoot
if (-not (Test-IsBelow $OutputRoot $allowedProofRoot)) {
    throw "Proof output must remain below $allowedProofRoot"
}
foreach ($protectedPath in @($Checkout, $adaptedRoot, $WadPath)) {
    if ($OutputRoot.Equals($protectedPath, [System.StringComparison]::OrdinalIgnoreCase) -or
        (Test-IsBelow $OutputRoot $protectedPath) -or
        (Test-IsBelow $protectedPath $OutputRoot)) {
        throw "Proof output overlaps protected source path: $protectedPath"
    }
}
Assert-NoReparsePath $OutputRoot $repoRoot
if (Test-Path -LiteralPath $OutputRoot) {
    Assert-NoDescendantReparse $OutputRoot
    $resolvedOutput = [System.IO.Path]::GetFullPath(
        (Resolve-Path -LiteralPath $OutputRoot).Path)
    if (-not (Test-IsBelow $resolvedOutput $buildRoot)) {
        throw "Refusing to replace proof output outside $buildRoot"
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
$objectRoot = Join-Path $OutputRoot 'obj'
New-Item -ItemType Directory -Force -Path $objectRoot | Out-Null

$hostRoot = Join-Path $repoRoot 'doom\host'
$commonC = @(
    '-std=gnu11', '-O2', '-funsigned-char', '-DMHS_NATIVE_DOOM_ADAPTER=1',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-variable',
    '-Wno-sign-compare', '-Wno-format', '-Wno-error=incompatible-pointer-types',
    '-I', $hostRoot, '-I', $adaptedRoot,
    '-include', (Join-Path $hostRoot 'mhs_mcume_host_compat.h')
)
$cSources = @(Get-ChildItem -LiteralPath $adaptedRoot -File -Filter '*.c' |
    Where-Object { $_.Name -ne 'i_main.c' } | Sort-Object Name)
if ($cSources.Count -ne 78) {
    throw "Adapted MCUME source set changed: expected 78 C files, found $($cSources.Count)."
}

$objects = [System.Collections.Generic.List[string]]::new()
foreach ($source in $cSources) {
    $object = Join-Path $objectRoot ($source.BaseName + '.o')
    Invoke-Compile $CCompiler ($commonC + @('-c', $source.FullName, '-o', $object)) `
        "C compile $($source.Name)"
    $objects.Add($object)
}
$shimObject = Join-Path $objectRoot 'mhs_mcume_host_shim.o'
Invoke-Compile $CCompiler ($commonC + @(
    '-c', (Join-Path $hostRoot 'mhs_mcume_host_shim.c'), '-o', $shimObject)) `
    'MCUME host shim compile'
$objects.Add($shimObject)

$nm = Join-Path $compilerDirectory 'nm.exe'
if (-not (Test-Path -LiteralPath $nm -PathType Leaf)) {
    throw "Symbol inspector not found: $nm"
}
$iVideoObject = Join-Path $objectRoot 'i_video.o'
$iVideoSymbols = @(& $nm -u $iVideoObject)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the adapted video backend.'
}
$rgb565BackendReferences = @($iVideoSymbols |
    Where-Object { $_ -match 'emu_DrawLine16|emu_LineBuffer' }).Count
if ($rgb565BackendReferences -ne 0) {
    throw 'Adapted i_video.o still references the per-line RGB565 backend.'
}

$commonCxx = @(
    '-std=c++17', '-O2', '-funsigned-char', '-Wall', '-Wextra',
    '-I', (Join-Path $repoRoot 'engine\native-doom'), '-I', $adaptedRoot
)
$cxxSources = @(
    (Join-Path $repoRoot 'engine\native-doom\mpe_doom_runtime.cpp'),
    (Join-Path $repoRoot 'engine\native-doom\mpe_doom_video.cpp'),
    (Join-Path $repoRoot 'engine\native-doom\mpe_doom_session.cpp'),
    (Join-Path $repoRoot 'doom\tests\mcume_native_session_test.cpp')
)
foreach ($source in $cxxSources) {
    $object = Join-Path $objectRoot (
        [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o')
    Invoke-Compile $CxxCompiler ($commonCxx + @('-c', $source, '-o', $object)) `
        "C++ compile $(Split-Path -Leaf $source)"
    $objects.Add($object)
}

$executable = Join-Path $OutputRoot 'mcume-native-session-test32.exe'
Invoke-Compile $CxxCompiler (@($objects) + @('-static', '-o', $executable, '-lm')) `
    'MCUME native Session link'

# Keep the script process environment unchanged. MinGW runtime DLLs found next
# to the compiler are copied only into ignored proof output beside the EXE.
$objdump = Join-Path $compilerDirectory 'objdump.exe'
if (-not (Test-Path -LiteralPath $objdump -PathType Leaf)) {
    throw "Dependency inspector not found: $objdump"
}
$dependencyLines = @(& $objdump -p $executable)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect MCUME proof runtime dependencies.'
}
$dependencyNames = @($dependencyLines | ForEach-Object {
    if ($_.ToString() -match '^\s*DLL Name:\s*(.+?)\s*$') { $Matches[1] }
} | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)
foreach ($dependencyName in $dependencyNames) {
    $runtimeDll = Join-Path $compilerDirectory $dependencyName
    if (Test-Path -LiteralPath $runtimeDll -PathType Leaf) {
        Copy-Item -LiteralPath $runtimeDll -Destination $OutputRoot -Force
    }
}

$runs = @()
for ($run = 1; $run -le 2; ++$run) {
    $runDirectory = Join-Path $OutputRoot "run-$run"
    New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
    Push-Location $runDirectory
    try {
        $runResult = Invoke-NativeCapture $executable @($WadPath)
    }
    finally {
        Pop-Location
    }
    if ($runResult.ExitCode -ne 0) {
        throw "MCUME native Session run $run failed.`n$(Get-BoundedFailure $runResult.Output)"
    }
    $jsonLine = @($runResult.Output | Where-Object {
        $_.ToString().TrimStart().StartsWith('{')
    } | Select-Object -Last 1)
    if ($jsonLine.Count -ne 1) {
        throw "MCUME native Session run $run emitted no JSON evidence."
    }
    $evidence = $jsonLine[0].ToString() | ConvertFrom-Json
    if ($evidence.status -ne 'PASS' -or $evidence.pointerBits -ne 32 -or
        $evidence.sessionCoreCalls -ne 493 -or $evidence.gameticDelta -ne 493 -or
        $evidence.catchUpExecuted -ne 4 -or $evidence.catchUpDropped -ne 31 -or
        $evidence.postedEvents -ne 48 -or
        $evidence.postedDownMask -ne 262143 -or
        $evidence.postedUpMask -ne 262143 -or
        $evidence.movementFrames -ne 420 -or
        $evidence.changedTransitions -lt 200 -or
        $evidence.uniqueFrameHashes -lt 128 -or $evidence.inE1M1 -ne $true -or
        $evidence.tapAmmoAfter -ge $evidence.tapAmmoBefore -or
        ($evidence.movementXAfter -eq $evidence.movementXBefore -and
         $evidence.movementYAfter -eq $evidence.movementYBefore) -or
        $evidence.movementAngleAfter -eq $evidence.movementAngleBefore -or
        $evidence.movementAmmoAfter -ge $evidence.movementAmmoBefore -or
        $evidence.schedulerResyncs -ne 0) {
        throw "MCUME native Session run $run did not satisfy the proof contract."
    }
    $runs += $evidence
}

foreach ($field in @(
    'sessionCoreCalls', 'gameticDelta', 'catchUpExecuted', 'catchUpDropped',
    'postedEvents', 'postedDownMask', 'postedUpMask', 'movementFrames',
    'changedTransitions', 'uniqueFrameHashes', 'tapAmmoBefore', 'tapAmmoAfter',
    'movementXBefore', 'movementXAfter', 'movementYBefore', 'movementYAfter',
    'movementAngleBefore', 'movementAngleAfter', 'movementAmmoBefore',
    'movementAmmoAfter', 'schedulerResyncs', 'finalFrameFnv1a', 'paletteFnv1a',
    'inE1M1'
)) {
    if ($runs[0].$field -ne $runs[1].$field) {
        throw "MCUME native Session runs were not deterministic: $field changed."
    }
}

$sourceLockOutput = @(& (Join-Path $repoRoot 'doom\tools\fetch_mcume_teensydoom.ps1') `
    -Offline -Destination $Checkout)
if ($LASTEXITCODE -ne 0) {
    throw 'Final pinned MCUME source-lock verification failed.'
}
$sourceLock = ($sourceLockOutput -join "`n") | ConvertFrom-Json
if ($sourceLock.verified -ne $true -or $sourceLock.commit -ne $origin.commit -or
    $sourceLock.treeSha256 -ne $origin.treeSha256 -or $sourceLock.wadFiles -ne 0) {
    throw 'Final pinned MCUME source-lock evidence is incomplete.'
}

[ordered]@{
    status = 'PASS'
    acceptance = 'host-only-one-shot'
    sourceCommit = $sourceLock.commit
    sourceTreeSha256 = $sourceLock.treeSha256
    compilerTarget = $cTarget
    deterministicRuns = 2
    sessionCoreCalls = $runs[0].sessionCoreCalls
    gameticDelta = $runs[0].gameticDelta
    catchUp = [ordered]@{
        executed = $runs[0].catchUpExecuted
        dropped = $runs[0].catchUpDropped
    }
    input = [ordered]@{
        orderedEdgesDelivered = $true
        sameTicGameplayTap = 'latched for one gametic then released'
        tapAmmoBefore = $runs[0].tapAmmoBefore
        tapAmmoAfter = $runs[0].tapAmmoAfter
        postedEvents = $runs[0].postedEvents
        allActionMask = $runs[0].postedDownMask
    }
    gameplay = [ordered]@{
        xBefore = $runs[0].movementXBefore
        xAfter = $runs[0].movementXAfter
        yBefore = $runs[0].movementYBefore
        yAfter = $runs[0].movementYAfter
        angleBefore = $runs[0].movementAngleBefore
        angleAfter = $runs[0].movementAngleAfter
        ammoBefore = $runs[0].movementAmmoBefore
        ammoAfter = $runs[0].movementAmmoAfter
        inE1M1 = $runs[0].inE1M1
    }
    video = [ordered]@{
        format = '320x200 indexed + 768-byte RGB palette'
        rgb565BackendReferences = $rgb565BackendReferences
        changedTransitions = $runs[0].changedTransitions
        uniqueFrameHashes = $runs[0].uniqueFrameHashes
        finalFrameFnv1a = $runs[0].finalFrameFnv1a
        paletteFnv1a = $runs[0].paletteFnv1a
    }
    lifecycle = 'restart intentionally rejected; production cleanup remains open'
    schedulerResyncs = $runs[0].schedulerResyncs
    wad = [ordered]@{
        path = $WadPath
        bytes = (Get-Item -LiteralPath $WadPath).Length
        sha256 = (Get-FileHash -LiteralPath $WadPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    pinnedCheckoutStillClean = $true
} | ConvertTo-Json -Depth 5 -Compress

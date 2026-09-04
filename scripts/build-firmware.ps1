param(
    [string]$SourcePath = '',
    [string]$ToolchainRoot = '',
    [string]$CustomGuiSourcePath = '',
    [string]$CustomGuiAcmePath = '',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'

function Get-Sha256Hex([string]$Path) {
    $stream = [System.IO.File]::OpenRead([System.IO.Path]::GetFullPath($Path))
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$versionConfigurationPath = Join-Path $projectRoot 'firmware-version.json'
$versionConfigurationHash = Get-Sha256Hex $versionConfigurationPath
$mpeVersionJson = & node (Join-Path $PSScriptRoot 'firmware-version.mjs')
if ($LASTEXITCODE -ne 0) { throw 'Firmware version or selected GUI About validation failed' }
$mpeVersion = $mpeVersionJson | ConvertFrom-Json
$upstreamUrl = 'https://github.com/SensoriumEmbedded/TeensyROM.git'
$upstreamCommit = '3436b8fbd7c642ef9eabc691d3d09da08a6a6690'
$arduinoCliVersion = '1.4.1'
$arduinoCliZipSha256 = '44f506a29d134cb294898d5f729aea85e5498f5d81ff5fc63c549087c45a20a3'
$teensyCoreVersion = '1.61.0'
$crc32LibraryVersion = '2.0.0'
$vrEmu6502Upstream = 'https://github.com/visrealm/vrEmu6502'
$vrEmu6502Commit = 'aae98cb14386d832cb7357c99626520b6590bc24'
$vrEmu6502VendorRoot = Join-Path $projectRoot 'engine\vendor\vrEmu6502'
$vrEmu6502VendorFiles = @(
    [ordered]@{ name = 'vrEmu6502.c'; sha256 = '148810a9477a003f1455e22dc50be7b2cc5312510816747eacae9cbdc67ae74f' },
    [ordered]@{ name = 'vrEmu6502.h'; sha256 = '38dcfc5d22cd5fb26726f337d60cbb5a71d93dfa026ecff746ff2e63294221c2' },
    [ordered]@{ name = 'LICENSE'; sha256 = 'd09de2afb377d29147398264e0cc29cf6f3793221fa90cb3d091be870af91208' }
)
$teensyIndexUrl = 'https://www.pjrc.com/teensy/package_teensy_index.json'
$patchPaths = @(
    (Join-Path $projectRoot 'engine\patches\0001-agi64-picture-dma.patch'),
    (Join-Path $projectRoot 'engine\patches\0002-agi64-protocol-v3.patch'),
    (Join-Path $projectRoot 'engine\patches\0003-agi64-actor-frame-scatter-settle.patch'),
    (Join-Path $projectRoot 'engine\patches\0004-mhs-powerengine.patch'),
    (Join-Path $projectRoot 'engine\patches\0005-mhs-agi-scan.patch'),
    (Join-Path $projectRoot 'engine\patches\0006-mhs-priority-line.patch'),
    (Join-Path $projectRoot 'engine\patches\0007-Add-transactional-MPE-Native-AGI-transport.patch'),
    (Join-Path $projectRoot 'engine\patches\0008-Add-staged-MPE-native-ego-frame-service.patch'),
    (Join-Path $projectRoot 'engine\patches\0009-Add-target-only-MPE2-RAM-mailbox.patch'),
    (Join-Path $projectRoot 'engine\patches\0010-Add-retained-MPE2-room-handoff-engine.patch'),
    (Join-Path $projectRoot 'engine\patches\0011-Allow-map-only-MPE2-room-handoff.patch'),
    (Join-Path $projectRoot 'engine\patches\0012-Defer-no-seed-MPE2-upgrade-activation.patch'),
    (Join-Path $projectRoot 'engine\patches\0013-Allow-room-seed-after-native-picture-fallback.patch'),
    (Join-Path $projectRoot 'engine\patches\0014-Add-virtualized-AGI-engine-foundation.patch'),
    (Join-Path $projectRoot 'engine\patches\0015-Report-correlated-VAG1-startup-failures.patch'),
    (Join-Path $projectRoot 'engine\patches\0016-Retain-MPE2-kick-until-DMA-ready.patch'),
    (Join-Path $projectRoot 'engine\patches\0017-Defer-fresh-MPE2-activation-to-poller.patch'),
    (Join-Path $projectRoot 'engine\patches\0018-Move-MPE2-working-set-to-RAM2.patch'),
    (Join-Path $projectRoot 'engine\patches\0019-Retain-MPE2-kick-until-request-acquired.patch'),
    (Join-Path $projectRoot 'engine\patches\0020-Accept-stable-modal-frame-boundaries.patch'),
    (Join-Path $projectRoot 'engine\patches\0021-Update-MPE2-doorbell-conformance.patch'),
    (Join-Path $projectRoot 'engine\patches\0022-Optimize-MPE2-virtual-scheduler-hot-path.patch'),
    (Join-Path $projectRoot 'engine\patches\0023-Cache-MPE2-cartridge-pages.patch'),
    (Join-Path $projectRoot 'engine\patches\0024-Carry-validated-MPE2-activation-across-reset.patch'),
    (Join-Path $projectRoot 'engine\patches\0025-Hold-MPE2-room-publication-until-display-ready.patch'),
    (Join-Path $projectRoot 'engine\patches\0026-Accept-synchronous-modal-frame-boundaries.patch'),
    (Join-Path $projectRoot 'engine\patches\0027-Stream-port2-joystick-and-port1-1351-input.patch'),
    (Join-Path $projectRoot 'engine\patches\0028-Reserve-MinimalBoot-stack-after-MPE2-growth.patch'),
    (Join-Path $projectRoot 'engine\patches\0029-Coalesce-MPE2-video-dirty-row-spans.patch'),
    (Join-Path $projectRoot 'engine\patches\0030-Bound-MPE2-changing-frame-publication.patch'),
    (Join-Path $projectRoot 'engine\patches\0031-Add-MPE3-title-pull-service.patch'),
    (Join-Path $projectRoot 'engine\patches\0032-Route-native-title-to-MinimalBoot.patch'),
    (Join-Path $projectRoot 'engine\patches\0033-Stream-native-intro-and-skip-to-login.patch'),
    (Join-Path $projectRoot 'engine\patches\0034-Publish-complete-frame-display-transitions.patch'),
    (Join-Path $projectRoot 'engine\patches\0035-Run-native-SQ1-game-after-intro.patch'),
    (Join-Path $projectRoot 'engine\patches\0036-Keep-cartridge-session-initialization-in-flash.patch'),
    (Join-Path $projectRoot 'engine\patches\0037-Stream-native-cartridges-up-to-four-MiB.patch'),
    (Join-Path $projectRoot 'engine\patches\0038-Stage-native-FreeDOS-platform.patch'),
    (Join-Path $projectRoot 'engine\patches\0039-Expose-native-PSRAM-arena.patch'),
    (Join-Path $projectRoot 'engine\patches\0040-Launch-native-FreeDOS-session.patch'),
    (Join-Path $projectRoot 'engine\patches\0041-Protect-native-DOS-input-mailbox.patch'),
    (Join-Path $projectRoot 'engine\patches\0042-Reset-native-DOS-cartridge-lifecycle.patch'),
    (Join-Path $projectRoot 'engine\patches\0043-Pump-native-DOS-while-packet-awaits-ACK.patch'),
    (Join-Path $projectRoot 'engine\patches\0044-Recognize-DOSVM-cartridge-identity.patch'),
    (Join-Path $projectRoot 'engine\patches\0045-Give-native-DOS-exclusive-RAM2.patch'),
    (Join-Path $projectRoot 'engine\patches\0046-Add-explicit-MPE-native-arena-ownership.patch'),
    (Join-Path $projectRoot 'engine\patches\0047-Quiet-native-DOS-on-packet-retry.patch'),
    (Join-Path $projectRoot 'engine\patches\0048-Launch-NESVM-folder-emulator.patch'),
    (Join-Path $projectRoot 'engine\patches\0049-Reserve-NESVM-RAM1-workspace-and-stack.patch'),
    (Join-Path $projectRoot 'engine\patches\0050-Launch-DOOMVM-reset-only-engine.patch')
)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Join-Path $projectRoot 'build') $mpeVersion.releaseId
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$artifactDir = Join-Path $OutputRoot 'firmware'
$manifestDir = Join-Path $OutputRoot 'manifests'
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
if ([string]::IsNullOrWhiteSpace($CustomGuiSourcePath)) {
    $CustomGuiSourcePath = Join-Path $projectRoot $mpeVersion.gui.path
}

foreach ($patchPath in $patchPaths) {
    if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
        throw "Firmware patch not found at $patchPath"
    }
}

if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    $ToolchainRoot = Join-Path $projectRoot 'build\toolchain'
}
$ToolchainRoot = [System.IO.Path]::GetFullPath($ToolchainRoot)

$createdClone = [string]::IsNullOrWhiteSpace($SourcePath)
if ($createdClone) {
    $SourcePath = Join-Path $OutputRoot 'source'
    if (Test-Path -LiteralPath $SourcePath) {
        throw "Build source already exists at $SourcePath. Pass -SourcePath explicitly to reuse it or choose a new -OutputRoot."
    }
    & git clone --quiet $upstreamUrl $SourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Unable to clone the pinned TeensyROM source' }
}
$SourcePath = [System.IO.Path]::GetFullPath($SourcePath)
if (-not (Test-Path -LiteralPath (Join-Path $SourcePath '.git'))) {
    throw "TeensyROM source is not a Git checkout: $SourcePath"
}

# Stage the checksum-verified, byte-exact upstream CPU before applying 0014.
# The firmware patch adds only the Teensy-specific single-model placement to
# this disposable source copy; the tracked vendor originals remain immutable.
$vrEmu6502Destination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\ThirdParty\vrEmu6502'
New-Item -ItemType Directory -Path $vrEmu6502Destination -Force | Out-Null
foreach ($vendorFile in $vrEmu6502VendorFiles) {
    $vendorPath = Join-Path $vrEmu6502VendorRoot $vendorFile.name
    if (-not (Test-Path -LiteralPath $vendorPath -PathType Leaf)) {
        throw "Pinned vrEmu6502 vendor file not found at $vendorPath"
    }
    $vendorHash = Get-Sha256Hex $vendorPath
    if ($vendorHash -ne $vendorFile.sha256) {
        throw "vrEmu6502 vendor checksum mismatch for $($vendorFile.name): expected $($vendorFile.sha256), found $vendorHash"
    }
    Copy-Item -LiteralPath $vendorPath `
        -Destination (Join-Path $vrEmu6502Destination $vendorFile.name) -Force
}

Push-Location $SourcePath
try {
    if ($createdClone) {
        & git checkout --quiet --detach $upstreamCommit
        if ($LASTEXITCODE -ne 0) { throw "Unable to check out pinned TeensyROM commit $upstreamCommit" }
    }
    $resolvedHead = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $resolvedHead -ne $upstreamCommit) {
        throw "TeensyROM source must be pinned at $upstreamCommit; found $resolvedHead"
    }

    # Windows PowerShell can promote native stderr from a deliberately failing
    # probe into a terminating error when ErrorActionPreference is Stop. A
    # reverse-check of the final patch recognizes a source tree that already
    # contains the complete v2+v3 series; otherwise apply or recognize each
    # patch in order.
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    # The pinned upstream checkout uses CRLF working files on Windows while
    # the checked-in patch series is text-normalized. Ignore line-ending-only
    # whitespace so a brand-new clone remains a reproducible input.
    & git apply --reverse --check --ignore-space-change $patchPaths[-1] 2>$null
    $fullPatchSeriesAlreadyApplied = $LASTEXITCODE -eq 0
    $ErrorActionPreference = $savedErrorActionPreference
    if (-not $fullPatchSeriesAlreadyApplied -and $patchPaths.Count -gt 1) {
        # A retained build clone can contain the complete prior series while a
        # newly appended patch has not yet been applied. Later patches may
        # overlap early hunks, so probing patch 0001 individually is not a
        # reliable way to recognize that state. Recognize the previous tail and
        # advance the clone by exactly the new final patch.
        $ErrorActionPreference = 'SilentlyContinue'
        & git apply --reverse --check --ignore-space-change $patchPaths[-2] 2>$null
        $priorPatchSeriesAlreadyApplied = $LASTEXITCODE -eq 0
        $ErrorActionPreference = $savedErrorActionPreference
        if ($priorPatchSeriesAlreadyApplied) {
            & git apply --ignore-space-change --whitespace=nowarn $patchPaths[-1]
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to advance TeensyROM+ firmware source with $([IO.Path]::GetFileName($patchPaths[-1]))"
            }
            $fullPatchSeriesAlreadyApplied = $true
            Write-Host "Applied appended firmware patch $([IO.Path]::GetFileName($patchPaths[-1]))."
        }
    }
    if ($fullPatchSeriesAlreadyApplied) {
        Write-Host 'MHS Power Engine firmware patch series is already applied to the supplied source tree.'
    }
    else {
        foreach ($patchPath in $patchPaths) {
            $ErrorActionPreference = 'SilentlyContinue'
            & git apply --check --ignore-space-change $patchPath 2>$null
            $patchCanApply = $LASTEXITCODE -eq 0
            $ErrorActionPreference = $savedErrorActionPreference
            if ($patchCanApply) {
                & git apply --ignore-space-change --whitespace=nowarn $patchPath
                if ($LASTEXITCODE -ne 0) {
                    throw "Unable to apply TeensyROM+ firmware patch $([IO.Path]::GetFileName($patchPath))"
                }
                continue
            }
            $ErrorActionPreference = 'SilentlyContinue'
            & git apply --reverse --check --ignore-space-change $patchPath 2>$null
            $patchAlreadyApplied = $LASTEXITCODE -eq 0
            $ErrorActionPreference = $savedErrorActionPreference
            if (-not $patchAlreadyApplied) {
                throw "TeensyROM source is dirty or does not match firmware patch $([IO.Path]::GetFileName($patchPath))"
            }
            Write-Host "Firmware patch $([IO.Path]::GetFileName($patchPath)) is already applied."
        }
    }
}
finally {
    Pop-Location
}

# Native05 was released with the immutable upstream dispatch table in DTCM.
# The former builder obtained that state only on repeated builds: it copied
# the vendor originals before skipping an already-applied patch chain. Make
# the effective released input explicit for both fresh and retained clones.
# Patch 0014 remains unchanged as historical integration provenance.
$compiledVendorSources = @()
foreach ($vendorFile in $vrEmu6502VendorFiles) {
    $vendorPath = Join-Path $vrEmu6502VendorRoot $vendorFile.name
    if ((Get-Sha256Hex $vendorPath) -ne $vendorFile.sha256) {
        throw "Pinned vendor source changed during patch preparation: $($vendorFile.name)"
    }
    $destination = Join-Path $vrEmu6502Destination $vendorFile.name
    Copy-Item -LiteralPath $vendorPath -Destination $destination -Force
    $compiledHash = Get-Sha256Hex $destination
    if ($compiledHash -ne $vendorFile.sha256) {
        throw "Compiled retained vendor checksum mismatch: $($vendorFile.name)"
    }
    $compiledVendorSources += [ordered]@{ file=$vendorFile.name; sha256=$compiledHash }
}

# The native engines share one explicitly owned RAM2 arena. Stage its canonical
# runtime header separately so the patch-generated integration and portable
# engines compile the same ownership contract, and record the exact input.
$nativeRuntimeDestination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\NativeRuntime'
New-Item -ItemType Directory -Path $nativeRuntimeDestination -Force | Out-Null
$nativeRuntimeFiles = @('mhs_native_arena.h')
$nativeRuntimeProvenance = @()
foreach ($nativeRuntimeFile in $nativeRuntimeFiles) {
    $nativeRuntimeSource = Join-Path (Join-Path $projectRoot 'engine\native-runtime') $nativeRuntimeFile
    Copy-Item -LiteralPath $nativeRuntimeSource -Destination (Join-Path $nativeRuntimeDestination $nativeRuntimeFile) -Force
    $nativeRuntimeProvenance += [ordered]@{ file=$nativeRuntimeFile; sha256=(Get-Sha256Hex $nativeRuntimeSource) }
}
New-Item -ItemType Directory -Path $manifestDir -Force | Out-Null
ConvertTo-Json -InputObject @($nativeRuntimeProvenance) -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestDir 'native-runtime-sources.json') -Encoding utf8

# Native game code is compiled only through the bank-58 module. Keep portable
# sources in one canonical location and record their exact build provenance.
$nativeGameDestination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\NativeGame'
New-Item -ItemType Directory -Path $nativeGameDestination -Force | Out-Null
$nativeGameFiles = @('mpe4_game.h','mpe4_game.cpp','mpe4_package.h','mpe4_package.cpp',
    'mpe4_render.h','mpe4_render.cpp','mpe4_session.h','mpe4_session.cpp','mpe4_firmware.h')
$nativeGameProvenance = @()
foreach ($nativeGameFile in $nativeGameFiles) {
    $nativeGameSource = Join-Path (Join-Path $projectRoot 'engine\native-game') $nativeGameFile
    Copy-Item -LiteralPath $nativeGameSource -Destination (Join-Path $nativeGameDestination $nativeGameFile) -Force
    $nativeGameProvenance += [ordered]@{ file=$nativeGameFile; sha256=(Get-Sha256Hex $nativeGameSource) }
}
New-Item -ItemType Directory -Path $manifestDir -Force | Out-Null
$nativeGameProvenance | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestDir 'native-game-sources.json') -Encoding utf8

# Stage the native DOS platform alongside the selected native-game sources. The
# bank-58 MPE5 launcher opens a DOSVM.CRT session through the existing GUI and
# uses the writable SD image as C: and the SD DOSVM/D folder as D:.
$nativeDosDestination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\NativeDos'
New-Item -ItemType Directory -Path (Join-Path $nativeDosDestination 'vendor\8086tiny') -Force | Out-Null
$nativeDosFiles = @('mpe5_platform.h','mpe5_platform.cpp','mpe5_8086tiny.h',
    'mpe5_8086tiny.cpp','mpe5_firmware.h','mpe5_font8x8.h','mpe5_font4x8.h','mpe5_direct_memory.h',
    'mpe5_direct_memory.cpp','mpe5_cartridge_memory.h','mpe5_video.h','mpe5_video.cpp',
    'mpe5_speaker.h','mpe5_speaker.cpp',
    'mpe5_redirector.h','mpe5_redirector.cpp','mpe5_folder_fs.h',
    'vendor\8086tiny\8086tiny.c','vendor\8086tiny\bios','vendor\8086tiny\bios.asm','vendor\8086tiny\LICENSE.txt')
$nativeDosProvenance = @()
foreach ($nativeDosFile in $nativeDosFiles) {
    $nativeDosSource = Join-Path (Join-Path $projectRoot 'engine\native-dos') $nativeDosFile
    $nativeDosDestinationFile = Join-Path $nativeDosDestination $nativeDosFile
    $nativeDosDestinationDirectory = Split-Path -Parent $nativeDosDestinationFile
    New-Item -ItemType Directory -Path $nativeDosDestinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $nativeDosSource -Destination $nativeDosDestinationFile -Force
    $nativeDosProvenance += [ordered]@{ file=$nativeDosFile; sha256=(Get-Sha256Hex $nativeDosSource) }
}
$nativeDosProvenance | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestDir 'native-dos-sources.json') -Encoding utf8

# NESVM is compiled through the same bank-58 service but keeps its complete
# working set in the unused RAM1 tail of the resident cartridge. RAM2 remains
# available and untouched. The exact third-party CPU header is recorded with
# the other portable native NES sources.
$nativeNesDestination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\NativeNES'
New-Item -ItemType Directory -Path (Join-Path $nativeNesDestination 'vendor\chips') -Force | Out-Null
$nativeNesFiles = @('nes_rom.h','nes_rom.cpp','nes_input.h','nes_machine.h','nes_machine.cpp',
    'nes_sid.h','nes_sid.cpp','nes_video.h','nes_video.cpp','mpe6_firmware.h',
    'vendor\chips\m6502.h','vendor\chips\UPSTREAM.md')
$nativeNesVendorHash = 'c8fb5979be406283db60ae5864da601cebb27dad2b114187a6dea2f90f8925dc'
$nativeNesProvenance = @()
foreach ($nativeNesFile in $nativeNesFiles) {
    $nativeNesSource = Join-Path (Join-Path $projectRoot 'engine\native-nes') $nativeNesFile
    if (-not (Test-Path -LiteralPath $nativeNesSource -PathType Leaf)) {
        throw "Native NES source not found at $nativeNesSource"
    }
    $nativeNesDestinationFile = Join-Path $nativeNesDestination $nativeNesFile
    New-Item -ItemType Directory -Path (Split-Path -Parent $nativeNesDestinationFile) -Force | Out-Null
    Copy-Item -LiteralPath $nativeNesSource -Destination $nativeNesDestinationFile -Force
    $nativeNesProvenance += [ordered]@{ file=$nativeNesFile; sha256=(Get-Sha256Hex $nativeNesSource) }
}
if ((Get-Sha256Hex (Join-Path $nativeNesDestination 'vendor\chips\m6502.h')) -ne $nativeNesVendorHash) {
    throw 'Pinned native NES CPU vendor checksum mismatch'
}
$nativeNesVendorPatch = Join-Path $projectRoot 'engine\native-nes\vendor\chips\m6502-teensy-flash.patch'
Push-Location $SourcePath
try {
    & git apply --check --unidiff-zero --ignore-space-change $nativeNesVendorPatch
    if ($LASTEXITCODE -ne 0) { throw 'Native NES CPU flash-placement patch does not apply' }
    & git apply --unidiff-zero --ignore-space-change --whitespace=nowarn $nativeNesVendorPatch
    if ($LASTEXITCODE -ne 0) { throw 'Unable to apply native NES CPU flash-placement patch' }
}
finally {
    Pop-Location
}
$nativeNesCompiledVendorHash = Get-Sha256Hex (Join-Path $nativeNesDestination 'vendor\chips\m6502.h')
$nativeNesProvenance | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $manifestDir 'native-nes-sources.json') -Encoding utf8

# Verify and snapshot the committed selected GUI for each build. The helper
# leaves that source untouched, reassembles both menu assets, rejects
# backend drift/stale headers, then applies only the reviewed GUI overlay.
# Keep this separate from the numbered MPE series: do not add the obsolete
# main-checkout 0007-geos-desktop.patch to this build.
$customGuiArgs = @(
    (Join-Path $projectRoot 'scripts\prepare-teensyrom-custom-gui.mjs'),
    '--gui-source', $CustomGuiSourcePath,
    '--source', $SourcePath,
    '--snapshot-root', (Join-Path $manifestDir 'custom-gui-snapshots')
)
if (-not [string]::IsNullOrWhiteSpace($CustomGuiAcmePath)) {
    $customGuiArgs += @('--acme', $CustomGuiAcmePath)
}
$customGuiJson = & node @customGuiArgs
if ($LASTEXITCODE -ne 0) { throw 'Custom GUI snapshot/asset verification failed; no firmware was compiled' }
$customGui = $customGuiJson | ConvertFrom-Json
if ($customGui.sourceHead -ne $mpeVersion.gui.commit -or
    $customGui.snapshotDigest -ne $mpeVersion.gui.snapshotDigest) {
    throw 'Built GUI differs from the exact GUI selected in firmware-version.json'
}
Write-Host "Custom GUI snapshot: $($customGui.snapshotDigest) ($($customGui.sourceHead))"

# Build the Doom core from its pinned, checksum-verified upstream tree only
# after GUI preparation has produced the marker consumed by the deterministic
# stager. The generated adapter tree stays outside the firmware source clone;
# only its reviewed translation units and native boundary are copied in.
$doomCheckout = Join-Path $projectRoot 'build\doom\upstream\MCUME'
$doomSourceLockScript = Join-Path $projectRoot 'doom\tools\fetch_mcume_teensydoom.ps1'
$doomSourceLockJson = @(& $doomSourceLockScript -Destination $doomCheckout)
if ($LASTEXITCODE -ne 0) { throw 'Pinned MCUME Doom source verification failed' }
$doomSourceLock = ($doomSourceLockJson -join "`n") | ConvertFrom-Json
if ($doomSourceLock.verified -ne $true -or $doomSourceLock.wadFiles -ne 0) {
    throw 'Pinned MCUME Doom source evidence is incomplete'
}

$doomAdaptedSource = Join-Path $projectRoot 'build\doom\adapted\mcume-teensydoom'
$doomAdapterScript = Join-Path $projectRoot 'doom\tools\apply_mcume_native_adapter.ps1'
$doomAdapterJson = @(& $doomAdapterScript -Checkout $doomCheckout -OutputRoot $doomAdaptedSource)
if ($LASTEXITCODE -ne 0) { throw 'MCUME Doom native adapter failed' }
$doomAdapter = ($doomAdapterJson -join "`n") | ConvertFrom-Json
if ($doomAdapter.status -ne 'PASS' -or $doomAdapter.wadFiles -ne 0 -or
    $doomAdapter.sourceCommit -ne $doomSourceLock.commit -or
    $doomAdapter.sourceTreeSha256 -ne $doomSourceLock.treeSha256) {
    throw 'MCUME Doom adapter provenance differs from the pinned source lock'
}

$doomStagerScript = Join-Path $projectRoot 'doom\tools\prepare_doom_firmware_source.ps1'
$doomStagerJson = @(& $doomStagerScript -SourcePath $SourcePath `
    -AdaptedSourcePath $doomAdaptedSource)
if ($LASTEXITCODE -ne 0) { throw 'DOOMVM firmware source staging failed' }
$doomStager = ($doomStagerJson -join "`n") | ConvertFrom-Json
if ($doomStager.status -ne 'PASS' -or $doomStager.coreTranslationUnits -ne 78 -or
    $doomStager.adaptedHeaders -ne 104 -or $doomStager.nativeDoomFiles -ne 10 -or
    $doomStager.guiSnapshotDigest -ne $customGui.snapshotDigest) {
    throw 'DOOMVM firmware staging evidence is incomplete'
}

# Re-verify the stager-owned native boundary against its repository inputs.
# The unity header lives beside the other bank-58 producers; the nine compiled
# files live at sketch root so Arduino gives each its own translation unit.
$nativeDoomDestination = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\NativeDoom'
$nativeDoomFirmwareDestination = Join-Path $nativeDoomDestination 'mpe7_firmware.h'
$nativeDoomSketchRoot = Join-Path $SourcePath 'Source\Teensy\MinimalBoot'
$nativeDoomFiles = @(
    'mpe_doom_runtime.cpp', 'mpe_doom_runtime.h',
    'mpe_doom_session.cpp', 'mpe_doom_session.h',
    'mpe_doom_video.cpp', 'mpe_doom_video.h',
    'mpe7_core_config.h', 'mpe7_target.cpp', 'mpe7_target.h',
    'mpe7_firmware.h'
)
$nativeDoomProvenance = @()
foreach ($nativeDoomFile in $nativeDoomFiles) {
    $nativeDoomSource = Join-Path (Join-Path $projectRoot 'engine\native-doom') $nativeDoomFile
    $nativeDoomStaged = if ($nativeDoomFile -eq 'mpe7_firmware.h') {
        $nativeDoomFirmwareDestination
    }
    else {
        Join-Path $nativeDoomSketchRoot $nativeDoomFile
    }
    if (-not (Test-Path -LiteralPath $nativeDoomStaged -PathType Leaf) -or
        (Get-Sha256Hex $nativeDoomStaged) -ne (Get-Sha256Hex $nativeDoomSource)) {
        throw "Staged native Doom source differs from its repository input: $nativeDoomFile"
    }
    $nativeDoomProvenance += [ordered]@{
        file = $nativeDoomFile
        sha256 = Get-Sha256Hex $nativeDoomSource
    }
}

$doomFirmwareSourceTest = Join-Path $projectRoot 'doom\tests\mpe7_firmware_source_test.mjs'
& node $doomFirmwareSourceTest $SourcePath
if ($LASTEXITCODE -ne 0) { throw 'DOOMVM firmware integration conformance test failed' }
$doomSourceManifest = [ordered]@{
    sourceLock = $doomSourceLock
    adapter = $doomAdapter
    staging = $doomStager
    nativeFiles = $nativeDoomProvenance
    firmwareSourceTestSha256 = Get-Sha256Hex $doomFirmwareSourceTest
}
$doomSourceManifest | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $manifestDir 'native-doom-sources.json') -Encoding utf8

$conformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\agi-picture-conformance.mjs'
& node $conformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'TeensyROM+ AGI picture protocol conformance test failed'
}
$nativeConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe-native-conformance.mjs'
& node $nativeConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE Native AGI transaction conformance test failed'
}
$thinConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe-thin-client-conformance.mjs'
& node $thinConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE2 thin-client RAM mailbox conformance test failed'
}
$thinRoomConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe-thin-room-engine-conformance.mjs'
& node $thinRoomConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE2 retained room engine conformance test failed'
}
$thinDeferredUpgradeConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe-thin-deferred-upgrade-conformance.mjs'
& node $thinDeferredUpgradeConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE2 deferred upgrade conformance test failed'
}
$virtualAGIConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe-virtual-agi-conformance.mjs'
& node $virtualAGIConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE2 virtualized AGI engine foundation conformance test failed'
}
$titlePullConformanceTest = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\tests\mpe3-title-pull-conformance.mjs'
& node $titlePullConformanceTest
if ($LASTEXITCODE -ne 0) {
    throw 'MPE3 native title pull conformance test failed'
}
$nesFirmwareConformanceTest = Join-Path $projectRoot 'nes\tests\nes_firmware_source_test.mjs'
& node $nesFirmwareConformanceTest $SourcePath
if ($LASTEXITCODE -ne 0) {
    throw 'NESVM firmware integration conformance test failed'
}

if (-not (Test-Path -LiteralPath $ToolchainRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $ToolchainRoot -Force | Out-Null
}
$arduinoCli = Join-Path $ToolchainRoot 'arduino-cli.exe'
if (-not (Test-Path -LiteralPath $arduinoCli -PathType Leaf)) {
    $zipPath = Join-Path $ToolchainRoot "arduino-cli-$arduinoCliVersion.zip"
    $downloadUrl = "https://github.com/arduino/arduino-cli/releases/download/v$arduinoCliVersion/arduino-cli_${arduinoCliVersion}_Windows_64bit.zip"
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath
    $actualHash = Get-Sha256Hex $zipPath
    if ($actualHash -ne $arduinoCliZipSha256) {
        throw "arduino-cli archive checksum mismatch: expected $arduinoCliZipSha256, found $actualHash"
    }
    Expand-Archive -LiteralPath $zipPath -DestinationPath $ToolchainRoot -Force
}

$env:LOCALAPPDATA = $ToolchainRoot
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $ToolchainRoot 'Arduino15'
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $ToolchainRoot 'staging'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $ToolchainRoot 'Arduino'
$env:PATH = "$ToolchainRoot$([System.IO.Path]::PathSeparator)$env:PATH"

$teensyCore = Join-Path $env:ARDUINO_DIRECTORIES_DATA "packages\teensy\hardware\avr\$teensyCoreVersion"
if (-not (Test-Path -LiteralPath $teensyCore -PathType Container)) {
    & $arduinoCli core update-index --additional-urls $teensyIndexUrl
    if ($LASTEXITCODE -ne 0) { throw 'Unable to update the Teensy board package index' }
    & $arduinoCli core install "teensy:avr@$teensyCoreVersion" --additional-urls $teensyIndexUrl
    if ($LASTEXITCODE -ne 0) { throw "Unable to install Teensyduino $teensyCoreVersion" }
}

# TeensyROM's Source/BuildInfo.md lists CRC32 2.0.0 as its one external
# Arduino library. A clean CLI data directory does not receive it with the
# Teensy core, so install the pinned version before invoking the upstream
# dual-boot build.
$crc32Header = Join-Path $env:ARDUINO_DIRECTORIES_USER 'libraries\CRC32\src\CRC32.h'
if (-not (Test-Path -LiteralPath $crc32Header -PathType Leaf)) {
    & $arduinoCli lib install "CRC32@$crc32LibraryVersion"
    if ($LASTEXITCODE -ne 0) { throw "Unable to install CRC32 $crc32LibraryVersion" }
}

$commonDefs = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\Common\Common_Defs.h'
$versionMatch = Select-String -LiteralPath $commonDefs -Pattern '^\s*#define\s+TRVersion\s+"([^"]+)"' | Select-Object -First 1
if ($null -eq $versionMatch) { throw 'Unable to read the TeensyROM firmware version' }
$firmwareVersion = $versionMatch.Matches[0].Groups[1].Value
$upstreamOutput = Join-Path $SourcePath "Source\Teensy\tools\build\TeensyROM+_${firmwareVersion}_full.hex"
if (Test-Path -LiteralPath $upstreamOutput -PathType Leaf) {
    $resolvedOutput = [System.IO.Path]::GetFullPath($upstreamOutput)
    if (-not $resolvedOutput.StartsWith($SourcePath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace build output outside the TeensyROM source tree: $resolvedOutput"
    }
    Remove-Item -LiteralPath $resolvedOutput -Force
}

$dualBuild = Join-Path $SourcePath 'Source\Teensy\tools\Build-DualBoot.ps1'
$previousSourceDateEpoch = [Environment]::GetEnvironmentVariable('SOURCE_DATE_EPOCH', 'Process')
$sourceDateEpoch = (& git -C $SourcePath show -s --format=%ct $upstreamCommit).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceDateEpoch -notmatch '^\d+$') {
    throw "Unable to resolve the pinned upstream commit timestamp for deterministic firmware compilation"
}
try {
    # GCC derives __DATE__ and __TIME__ from SOURCE_DATE_EPOCH. TeensyROM prints
    # both strings, so pinning them is required for a stable firmware checksum.
    $env:SOURCE_DATE_EPOCH = $sourceDateEpoch
    & $dualBuild -Fab04_Features
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $upstreamOutput -PathType Leaf)) {
        throw 'TeensyROM+ dual-boot firmware build failed'
    }
}
finally {
    [Environment]::SetEnvironmentVariable('SOURCE_DATE_EPOCH', $previousSourceDateEpoch, 'Process')
}

$minimalBootElf = Join-Path $SourcePath 'Source\Teensy\MinimalBoot\build\MinimalBoot.ino.elf'
if (-not (Test-Path -LiteralPath $minimalBootElf -PathType Leaf)) {
    throw "MinimalBoot ELF not found after firmware build: $minimalBootElf"
}
$nmTool = Get-ChildItem -LiteralPath (Join-Path $env:ARDUINO_DIRECTORIES_DATA 'packages\teensy\tools\teensy-compile') `
    -Recurse -File -Filter 'arm-none-eabi-nm.exe' | Select-Object -First 1
if ($null -eq $nmTool) { throw 'Unable to locate arm-none-eabi-nm for the MinimalBoot memory checks' }
$minimalSymbols = @{}
$minimalSymbolSizes = @{}
$minimalNmOutput = @(& $nmTool.FullName -C -S -n $minimalBootElf)
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect the MinimalBoot ELF memory layout'
}
foreach ($symbolLine in $minimalNmOutput) {
    if ($symbolLine -match '^([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+\S\s+(.+)$') {
        $symbolName = $Matches[3].Trim()
        $minimalSymbols[$symbolName] = [Convert]::ToUInt64($Matches[1], 16)
        $minimalSymbolSizes[$symbolName] = [Convert]::ToUInt64($Matches[2], 16)
    }
    elseif ($symbolLine -match '^([0-9A-Fa-f]+)\s+\S\s+(.+)$') {
        $symbolName = $Matches[2].Trim()
        $minimalSymbols[$symbolName] = [Convert]::ToUInt64($Matches[1], 16)
    }
}
if (-not $minimalSymbols.ContainsKey('_ebss') -or -not $minimalSymbols.ContainsKey('_estack')) {
    throw 'Unable to resolve _ebss and _estack in the MinimalBoot ELF'
}
$minimalBootStackReserveBytes = $minimalSymbols['_estack'] - $minimalSymbols['_ebss']
$minimumStackReserveBytes = 16KB
if ($minimalBootStackReserveBytes -lt $minimumStackReserveBytes) {
    throw "MinimalBoot leaves only $minimalBootStackReserveBytes bytes for stack; at least $minimumStackReserveBytes are required"
}
Write-Host "MinimalBoot stack reserve: $minimalBootStackReserveBytes bytes"

# The inline FsFile and every ownership/control record must remain in RAM1.
# RAM2 is cleared after the reset-only handoff and can no longer hold live
# metadata.
foreach ($requiredSymbol in @('MPE5DiskFile', '_sdata', '_ebss', 'MPE5Active',
    'MPE5InputPending', 'MPE5Ram2Owned', 'MPE6Active', 'MPE6InputPending',
    'MPE7Active', 'MPE7InputPending', 'MPE7Ram2Owned', 'MPE7Error',
    'MPE7Session', 'MPE7WadPath', 'MPE7ArenaView',
    'MHSNativeArenaControlState')) {
    if (-not $minimalSymbols.ContainsKey($requiredSymbol)) {
        throw "Missing native engine initialization symbol: $requiredSymbol"
    }
}
if (-not $minimalSymbolSizes.ContainsKey('MPE5DiskFile') -or
    $minimalSymbols['MPE5DiskFile'] -lt $minimalSymbols['_sdata'] -or
    ($minimalSymbols['MPE5DiskFile'] + $minimalSymbolSizes['MPE5DiskFile']) -gt $minimalSymbols['_ebss']) {
    throw 'The native DOS FsFile object must reside in RAM1, never NOLOAD DMAMEM'
}
foreach ($owner in @('MPE5Active', 'MPE5InputPending', 'MPE5Ram2Owned',
    'MPE6Active', 'MPE6InputPending', 'MPE7Active', 'MPE7InputPending',
    'MPE7Ram2Owned', 'MPE7Error', 'MPE7Session', 'MPE7WadPath',
    'MPE7ArenaView')) {
    if ($minimalSymbols[$owner] -lt $minimalSymbols['_sdata'] -or
        $minimalSymbols[$owner] -ge $minimalSymbols['_ebss']) {
        throw "Reset-only native state must reside in initialized RAM1/BSS: $owner"
    }
}
if (-not $minimalSymbolSizes.ContainsKey('MHSNativeArenaControlState') -or
    $minimalSymbolSizes['MHSNativeArenaControlState'] -ne 16 -or
    $minimalSymbols['MHSNativeArenaControlState'] -lt $minimalSymbols['_sdata'] -or
    ($minimalSymbols['MHSNativeArenaControlState'] + $minimalSymbolSizes['MHSNativeArenaControlState']) -gt $minimalSymbols['_ebss']) {
    throw 'The shared native arena control record must be 16 bytes and reside entirely in initialized RAM1/BSS'
}
Write-Host 'Native DOS/Doom state and shared arena ownership placement: PASS (linked ELF)'

# MPE7 overlays Doom's mutable core image onto all physical RAM2 only after
# USB1 and the normal heap are retired. Prove the linker-provided boundaries
# used by that handoff, including the exact eight-MiB external PSRAM zone.
$mpe7LinkerSymbols = @('__mpe7_data_load', '__mpe7_data_start',
    '__mpe7_data_end', '__mpe7_bss_start', '__mpe7_bss_end',
    '__mpe7_runtime_start', '__mpe7_runtime_end', '__mpe7_zone_start',
    '__mpe7_zone_end', 'MemPool', '_flashimagelen')
foreach ($requiredSymbol in $mpe7LinkerSymbols) {
    if (-not $minimalSymbols.ContainsKey($requiredSymbol)) {
        throw "Missing MPE7 linker symbol: $requiredSymbol"
    }
}
$mpe7Ram2Start = [uint64]0x20200000
$mpe7Ram2EndExclusive = [uint64]0x20280000
$mpe7ZoneStart = [uint64]0x70000000
$mpe7ZoneEndExclusive = [uint64]0x70800000
$mpe7LowerFlashStart = [uint64]0x60000000
$mpe7UpperFlashStart = [uint64]0x60180000
if ($minimalSymbols['__mpe7_data_start'] -ne $mpe7Ram2Start -or
    $minimalSymbols['__mpe7_data_start'] -gt $minimalSymbols['__mpe7_data_end'] -or
    $minimalSymbols['__mpe7_data_end'] -gt $minimalSymbols['__mpe7_bss_start'] -or
    $minimalSymbols['__mpe7_bss_start'] -gt $minimalSymbols['__mpe7_bss_end'] -or
    $minimalSymbols['__mpe7_bss_end'] -gt $minimalSymbols['__mpe7_runtime_start'] -or
    $minimalSymbols['__mpe7_runtime_start'] -ge $minimalSymbols['__mpe7_runtime_end'] -or
    $minimalSymbols['__mpe7_runtime_end'] -ne $mpe7Ram2EndExclusive) {
    throw 'MPE7 RAM2 overlay boundaries are invalid in the linked ELF'
}
$mpe7RuntimeReserveBytes = [uint64]$minimalSymbols['__mpe7_runtime_end'] -
    [uint64]$minimalSymbols['__mpe7_runtime_start']
if ($mpe7RuntimeReserveBytes -lt [uint64](128KB)) {
    throw "MPE7 leaves only $mpe7RuntimeReserveBytes bytes of runtime RAM2; at least 131072 are required"
}
if ($minimalSymbols['__mpe7_zone_start'] -ne $mpe7ZoneStart -or
    $minimalSymbols['MemPool'] -ne $mpe7ZoneStart -or
    $minimalSymbols['__mpe7_zone_end'] -ne $mpe7ZoneEndExclusive) {
    throw 'MPE7 must own exactly the first eight MiB of external PSRAM'
}
if ($minimalSymbols['__mpe7_data_load'] -lt $mpe7LowerFlashStart -or
    $minimalSymbols['__mpe7_data_load'] -ge $mpe7UpperFlashStart -or
    $minimalSymbols['_flashimagelen'] -eq 0 -or
    $minimalSymbols['_flashimagelen'] -gt ($mpe7UpperFlashStart - $mpe7LowerFlashStart)) {
    throw 'MPE7 initialized data or MinimalBoot image crosses the shifted upper-firmware boundary'
}
Write-Host "MPE7 linked layout: RAM2 runtime $mpe7RuntimeReserveBytes bytes; PSRAM zone 8388608 bytes"

# The title IO2 handler services the physical bus. FLASHMEM is appropriate
# for the native sequencer, but never for this timing-critical handler.
$titleBusSymbol = 'MPE3TitleIO2Hndlr(unsigned char, bool)'
if (-not $minimalSymbols.ContainsKey($titleBusSymbol) -or
    -not $minimalSymbolSizes.ContainsKey($titleBusSymbol) -or
    $minimalSymbolSizes[$titleBusSymbol] -eq 0 -or
    ($minimalSymbols[$titleBusSymbol] + $minimalSymbolSizes[$titleBusSymbol]) -gt 512KB) {
    throw 'MPE3 title IO2 bus handler must remain entirely in fast instruction RAM'
}

# MPE2 must run on stock TeensyROM+ boards without optional PSRAM. Prove from
# the final linked ELF—not source annotations—that the complete virtual C64
# arena and all four large presentation shadows live in built-in Teensy RAM2.
# Check each symbol's complete extent so a linker change cannot place only its
# first byte inside RAM2. The remaining RAM2 span is the runtime heap used by
# the SD stack and the small vrEmu6502 CPU object.
$minimalBootRam2Start = [uint64]0x20200000
$minimalBootRam2EndExclusive = [uint64]0x20280000
$minimumRam2HeapReserveBytes = [uint64](256KB)
$nativeReleaseNumber = [int]$mpeVersion.releaseId.Substring('native'.Length)
if ($nativeReleaseNumber -ge 19) {
    # native19 removes the second 64 KiB engine allocation. Keep that recovered
    # capacity available to the runtime in this and later release profiles.
    $minimumRam2HeapReserveBytes = [uint64](320KB)
}
$minimalBootVirtualRam2Symbols = @(
    'MHSNativeArenaStorage',
    'MPEVirtualPresentedBitmap',
    'MPEVirtualPresentedText',
    'MPEVirtualPresentedScreen',
    'MPEVirtualPresentedColour',
    'MPEVirtualParserUnderlyingColour',
    'MPEVirtualRuntimeHeader',
    'MPEVirtualColour',
    'MPEVirtualVIC',
    'MPEVirtualSID',
    'MPEVirtualTextDirty',
    'MPEVirtualBitmapDirty',
    'MPEVirtualScreenDirty',
    'MPEVirtualColourDirty',
    'MPEVirtualVICDirty',
    'MPEVirtualSIDDirty',
    'MPEVirtualPresentedVIC',
    'MPEVirtualPresentedSID',
    'MPEVirtualPresentedSpritePair',
    'MPEVirtualSpritePair',
    'MPEVirtualPresentedSpritePointers',
    'MPEVirtualCharacterROM',
    'MPEVirtualKeyQueue',
    'MPEVirtualPageCache',
    'MPEVirtualSlotPage',
    'MPEVirtualActiveCartridgePage'
)
if (-not $minimalSymbols.ContainsKey('_heap_start') -or
    -not $minimalSymbols.ContainsKey('_heap_end')) {
    throw 'Unable to resolve _heap_start and _heap_end in the MinimalBoot ELF'
}
$minimalBootHeapStart = [uint64]$minimalSymbols['_heap_start']
$minimalBootHeapEnd = [uint64]$minimalSymbols['_heap_end']
if ($minimalBootHeapStart -lt $minimalBootRam2Start -or
    $minimalBootHeapEnd -gt $minimalBootRam2EndExclusive -or
    $minimalBootHeapEnd -le $minimalBootHeapStart) {
    throw ('MinimalBoot RAM2 heap range is invalid: 0x{0:X8}-0x{1:X8}' -f
        $minimalBootHeapStart, ($minimalBootHeapEnd - 1))
}
foreach ($symbolName in $minimalBootVirtualRam2Symbols) {
    if (-not $minimalSymbols.ContainsKey($symbolName) -or
        -not $minimalSymbolSizes.ContainsKey($symbolName)) {
        throw "Unable to resolve $symbolName and its size in the MinimalBoot ELF"
    }
    $symbolAddress = [uint64]$minimalSymbols[$symbolName]
    $symbolBytes = [uint64]$minimalSymbolSizes[$symbolName]
    $symbolEndExclusive = $symbolAddress + $symbolBytes
    if ($symbolBytes -eq 0 -or
        $symbolAddress -lt $minimalBootRam2Start -or
        $symbolEndExclusive -gt $minimalBootRam2EndExclusive -or
        $symbolEndExclusive -le $symbolAddress) {
        throw ('MinimalBoot symbol {0} is outside RAM2: 0x{1:X8}+0x{2:X}' -f
            $symbolName, $symbolAddress, $symbolBytes)
    }
    if ($symbolEndExclusive -gt $minimalBootHeapStart) {
        throw "MinimalBoot symbol $symbolName overlaps the RAM2 runtime heap"
    }
}
$minimalBootRam2HeapReserveBytes = $minimalBootHeapEnd - $minimalBootHeapStart
if ($minimalBootRam2HeapReserveBytes -lt $minimumRam2HeapReserveBytes) {
    throw "MinimalBoot leaves only $minimalBootRam2HeapReserveBytes bytes of RAM2 heap; at least $minimumRam2HeapReserveBytes are required"
}
Write-Host "MinimalBoot RAM2 heap reserve: $minimalBootRam2HeapReserveBytes bytes"

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
New-Item -ItemType Directory -Path $manifestDir -Force | Out-Null
$artifactPath = Join-Path $artifactDir $mpeVersion.filename
Copy-Item -LiteralPath $upstreamOutput -Destination $artifactPath -Force
$officialRestoreSource = Join-Path $SourcePath 'bin\TeensyROM\TeensyROM+_0.8_full.hex'
if (-not (Test-Path -LiteralPath $officialRestoreSource -PathType Leaf)) {
    throw "Pinned upstream TeensyROM+ restore image not found at $officialRestoreSource"
}
$officialRestorePath = Join-Path $artifactDir 'TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex'
Copy-Item -LiteralPath $officialRestoreSource -Destination $officialRestorePath -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\FIRMWARE-GUIDE.md') `
    -Destination (Join-Path $artifactDir 'MHS-POWER-ENGINE.md') -Force
$artifactHash = Get-Sha256Hex $artifactPath
$officialRestoreHash = Get-Sha256Hex $officialRestorePath
$patchManifest = @($patchPaths | ForEach-Object {
    [ordered]@{
        path = "engine/patches/$([IO.Path]::GetFileName($_))"
        sha256 = Get-Sha256Hex $_
    }
})
$manifestPath = Join-Path $manifestDir 'firmware-build.json'
$manifest = [ordered]@{
    artifact = [System.IO.Path]::GetFileName($artifactPath)
    sha256 = $artifactHash
    bytes = (Get-Item -LiteralPath $artifactPath).Length
    upstream = $upstreamUrl
    upstreamCommit = $upstreamCommit
    firmwareVersion = $firmwareVersion
    mpeFirmwareVersion = $mpeVersion.version
    firmwareFilename = $mpeVersion.filename
    versionConfiguration = [ordered]@{ file = 'firmware-version.json'; sha256 = $versionConfigurationHash }
    hardware = 'TeensyROM+ Fab0.4'
    minimalBootStackReserveBytes = $minimalBootStackReserveBytes
    minimalBootRam2HeapReserveBytes = $minimalBootRam2HeapReserveBytes
    minimalBootRam2MinimumHeapReserveBytes = $minimumRam2HeapReserveBytes
    nativeDosFileInitializedData = $true
    product = 'MHS Power Engine for TeensyROM+'
    buildProfile = $mpeVersion.releaseId
    compiledVendorSources = $compiledVendorSources
    nativeNES = [ordered]@{
        cartridgeIdentity = 'MHS NESVM'
        descriptor = 'N6D1 version 1'
        romDirectory = '/NESVM/ROMS'
        saveDirectory = '/NESVM/SAVES (reserved for future use)'
        mapperSupport = @(0, 11)
        memory = 'Unused resident-cartridge RAM1 tail only; RAM2 untouched'
        presentation = 'Complete 256x240 frame squished to 320x200 sharp bitmap cells by default; no crop'
        input = 'Port 2 joystick A; Space B; Return Start; Shift Select; Start+Select returns to the same menu row'
        audio = 'Basic NES APU register approximation streamed as 26-byte SID packets'
        sourceManifest = 'native-nes-sources.json'
        cpuVendorSourceSha256 = $nativeNesVendorHash
        cpuCompiledFlashPlacementSha256 = $nativeNesCompiledVendorHash
        physicalProof = $false
    }
    nativeDoom = [ordered]@{
        cartridgeIdentity = 'MHS DOOMVM'
        descriptor = 'M7D1 version 1; 8 MiB PSRAM; 64 x 8 KiB RAM2 blocks'
        wadPath = '/DOOMVM/DOOM1.WAD (user supplied; never built into firmware)'
        sourceCommit = $doomSourceLock.commit
        sourceTreeSha256 = $doomSourceLock.treeSha256
        sourceManifest = 'native-doom-sources.json'
        coreTranslationUnits = $doomStager.coreTranslationUnits
        adaptedHeaders = $doomStager.adaptedHeaders
        lowerFlashBytes = 0x00180000
        upperFlashOrigin = '0x60180000'
        ram2OverlayBytes = 0x00080000
        ram2RuntimeReserveBytes = $mpe7RuntimeReserveBytes
        psramZoneBytes = 0x00800000
        transport = 'MPE3 immutable CELL/SID packets; foreground input acceptance and exact ACK before reuse'
        lifecycle = 'Exclusive reset-only RAM2/PSRAM ownership; physical or firmware reboot required to exit'
        licenseStatus = $doomSourceLock.licenseStatus
        unresolvedLicenseFiles = $doomSourceLock.unresolvedLicenseFiles
        publicationReady = $false
        physicalProof = $false
    }
    nativeGame = [ordered]@{
        package = 'M4G1 version 1 appended to unchanged M3T1 intro'
        interpreter = 'Native bounded AGI bytecode, parser, motion and renderer'
        runtime6510Emulation = $false
        busMasterDma = $false
        helperBank = 58
        cartridgeStorage = 'SD only; standard 8KiB CHIP framing, native-only banks 64-255, physical bank58 omitted'
        maximumPhysicalCartridgeBytes = 4194304
        maximumLogicalCartridgeBytes = 4177920
        nativeChipIndexBytes = 2052
        reusedIntroArenaBytes = 65536
        cellPublication = 'C64 pulls immutable CRC packets; frame-end ACK advances gameplay and sound'
        input = 'Sequenced command 3 with checksum, keyboard ASCII/IBM scan, port-2 joystick and port-1 1351 mouse'
        save = 'Per-game SD /SAVES/MPE4-XXXXXXXX.sav (package CRC32), directory created automatically, verified temporary replacement and backup recovery; prior root slots are read-only restore fallbacks; legacy /MPE4-SQ1.sav preserved separately'
        physicalProof = $false
        validation = 'See exact native module and real-input Session playthrough reports supplied with each candidate'
    }
    virtualCpu = [ordered]@{
        implementation = 'vrEmu6502'
        model = 'CPU_6510'
        upstream = $vrEmu6502Upstream
        upstreamCommit = $vrEmu6502Commit
        dispatchTablePlacement = 'Native05 released upstream table in DTCM; explicit post-patch vendor restoration'
        license = 'MIT'
        addressSpaceBytes = 65536
        foundationImplemented = $true
        wholeGameEngineImplemented = $true
        directTitleEntry = 'R65B agi_title_start'
        gameBootProven = $true
        gameBootProofScope = 'Deterministic offline execution of the exact packaged SQ1 R650 image reached the first and repeated agi_frame_loop boundaries from agi_title_start; physical cartridge acceptance remains pending'
        presentation = 'Final-state row-bounded dirty bitmap/text cells, independently gated VIC/SID state, and independently gated ego sprite pair/pointers; full publication only for initial state, room/full-screen or video-mode transitions, and forced resynchronization'
        physicalProof = $false
    }
    nativeTitlePull = [ordered]@{
        protocol = 'M3TP'
        version = 1
        assetFormat = 'M3T1'
        scope = 'Compiled SQ1 full opening and original Sound 60; silent stop at login, no gameplay'
        assetFlags = @(7, 15)
        skipCommand = 2
        skipControls = @('Space', 'Return', 'joystick port 2 fire')
        finalLogin = 'Standalone 1000-cell hires frame; complete-frame publication, gate-off, END hold'
        streamScratchBytes = 1024
        helperBank = 58
        launch = 'Exact SQ1 MPE3 TITLE PULL, MHS DOSVM, MHS NESVM, or MHS DOOMVM standard EasyFlash header routes from SD to MinimalBoot before chip allocation'
        transport = 'C64 reads immutable EasyFlash IO2 packets; CRC16 and commit-last; explicit ACK before reuse'
        runtime6510Emulation = $false
        gameplayDma = $false
        fullAgiEngine = $false
        music = 'Native three-voice SID score cursors and retrigger masks; original score repeats until login; C64 raster-clocked NTSC playback'
        physicalProof = $false
    }
    protocol = 'MPE'
    protocolVersion = 1
    supportedProtocolVersions = @(1, 2)
    activationContract = [ordered]@{
        bank = 59
        challengeAddress = '$DFFB'
        challenge = '$3C'
        signatureAddresses = '$DFF0-$DFF4'
        signature = 'M P E + $01'
        requiredResponse = '$C3 at $DFFB plus generic capability bits at $DFF5'
        responseLifetime = 'Dedicated one-shot latch; asynchronous command completion cannot overwrite it'
        resetRule = 'Any unrelated locked IO2 access, wrong byte, or helper-bank change resets the sequence'
    }
    mpe2ActivationContract = [ordered]@{
        protocol = 'MPE2'
        protocolVersion = 2
        authority = 'Hard cold-boot target-only mode; Teensy owns AGI engine and game state while C64 is an input/VIC/SID terminal'
        bank = 59
        challengeAddress = '$DFFB'
        challenge = '$3C'
        signatureAddresses = '$DFF0-$DFF4'
        signature = 'M P E + $02'
        io2Reads = 0
        doorbellAddress = '$DFF6'
        commands = [ordered]@{
            thinKick = 80
            thinVBlank = 81
            emergencyReset = 127
        }
        responseChannel = 'Strict stop-and-wait MPO2 envelope in ordinary C64 RAM at $0480; $04C0 is reserved and unwritten'
        fallback = 'None after authority begins; $0400-$05FF overlaps the old C64 AGI runtime'
    }
    capabilityMask = 255
    capabilityFormula = '$01 discovery | $02 async | $04 DMA | $08 cartridge streaming | $10 PowerVM | $20 fail-closed | $40 AGI Engine | $80 timing'
    serviceQuery = [ordered]@{
        command = 47
        completionStatus = 175
        pageAddress = '$DFFF'
        acknowledgeCommand = 0
        pageCount = 5
        pages = @(
            [ordered]@{ page = 0; tag = 'MHS'; serviceCount = 5 },
            [ordered]@{ page = 1; tag = 'AGI'; service = 1 },
            [ordered]@{ page = 2; tag = 'PVM'; service = 16 },
            [ordered]@{ page = 3; tag = 'SCN'; service = 17 },
            [ordered]@{ page = 4; tag = 'PQL'; service = 18; version = 1; capabilityMask = 7 },
            [ordered]@{ page = 5; tag = 'NAG'; service = 32; version = 1; capabilityMask = 63 }
        )
    }
    modes = @(
        [ordered]@{
            name = 'mpe2-target-only-thin-client'
            protocol = 'MPE2'
            protocolVersion = 2
            activation = 'Bank 59; write $3C to $DFFB, then M P E + $02 to $DFF0-$DFF4; perform no IO2 reads'
            commands = [ordered]@{
                thinKick = 80
                thinVBlank = 81
                emergencyReset = 127
            }
            inputSlots = @('$0400-$043F active', '$0440-$047F reserved and unread')
            outputSlots = @('$0480-$04BF active', '$04C0-$04FF reserved and unwritten')
            sidSlots = @('$0500-$053F', '$0540-$057F')
            c64Private = '$0580-$05FF; firmware never writes it'
            envelope = '64 bytes: magic/version/code/length/flags, uint32 sequence, uint32 C64 session nonce, 40-byte zero-padded payload, CRC-16/CCITT-FALSE over bytes 0-55, zero reserved bytes, repeated uint32 sequence commit at bytes 60-63'
            dmaReads = 'Each THIN_KICK reads exactly one 64-byte MPI2 record at $0400 and validates only that request'
            dmaWrites = 'Continue the held 64-byte request-read ownership directly into one contiguous 64-byte MPO2 write at $0480, then CloseDMA once; never release and reacquire /DMA between them'
            videoTiming = '$DFFE bit 0 normalized from C64 $02A6; AGIPictureApplyVideoTiming runs before THIN_KICK DMA (NTSC 430 ns, PAL 440 ns)'
            physicalProofDiagnosticEnabled = $false
            malformedInputBehavior = 'Fail closed: release /DMA without publishing a response because sequence and nonce are untrusted'
            implementedMilestone = 'Retained HOF1/INP1/FRM1 Room 2 slice owns ego walking, animation, bounded collision, and LOOK AROUND result by message ID; legacy room seed is optional'
            agiEngineImplemented = $false
            retainedRoomSliceImplemented = $true
            retainedRoomSliceScope = 'One SQ1 Room 2 proof slice; full Logic 0/Room 2 AGI semantics, doors, non-ego actors, SID, and typed parser are not implemented after authority moves'
            retainedCollision = 'Supplied passability grid up to 16x8 is always enforced with step size 1 and a conservative 12-column ego-foot sweep; a matching retained priority map additionally blocks priority 0/1, and a mismatched seed rejects handoff'
            retainedParserIntent = 'LOOK AROUND result by message ID only; not a general parser implementation'
            transportPhysicalGatePassed = $true
            transportPhysicalEvidence = [ordered]@{
                date = '2026-08-31'
                machine = 'Physical NTSC Commodore 64 with TeensyROM+ Fab0.4'
                result = 'Operator-reported visible RESULT PASS'
                firmwareSha256 = '4fe52e11b122dfa6d68c27cc4b10563fdd3057a16355399e36b1af6e7ebc1b22'
                cartridgeSha256 = 'a237adffde8a15b84caa6eddf5998817dcd5e55ff71636703a23ca7b57f4668a'
                manifestSha256 = '490b34d92631c6be6b5975b529aed3cd112f5a35b2c9f42faf3374397bc3f0aa'
                evidenceArchive = 'Historical AGI-64 transport checkpoint, not included in this source repository'
            }
            productionImageDirectPhysicalRun = $false
            room2EnginePhysicalGatePassed = $false
            performanceGatePassed = $false
            compatibility = 'MPE v1 and AGI+ compatibility services remain unchanged; MPE2 itself requires matching firmware and has no native C64 fallback'
        },
        [ordered]@{
            name = 'mpe2-full-game-virtual-agi'
            protocol = 'MPE2'
            protocolVersion = 2
            authority = 'Teensy starts the compiler-certified R65B image at agi_title_start and owns the title, login, parser, AGI VM/state, rendering shadows, and sound state; C64 owns only input and physical VIC/SID presentation'
            boot = 'VAG1 START descriptor plus the exact MGA1/R650 package; the terminal stages physical C64 character ROM $D000-$D1FF at $0C00-$0DFF before the first doorbell'
            input = 'INT2 carries active-high joystick and one key edge per request; a submitted parser line is staged at $0580 and correlated by length and CRC'
            frameStatus = 'FRA2 at $0480-$04BF is committed last after all direct-DMA video, sprite, VIC, and SID publication succeeds'
            routineVideo = 'Only final-changed 8x8 cells, coalesced into row-bounded runs; bitmap cells carry 8 bitmap bytes plus one screen and one colour byte'
            exceptionalVideo = 'Full planes only for initial publication, room/full-screen transition, video-mode transition, or explicit forced resynchronization'
            textScreen = 'Virtual $0400 text output is translated to physical $0800 so MPE2 mailboxes remain private'
            parserRow = 'The runtime raster-split command row is flattened into final bitmap row 24 by firmware; the C64 terminal installs no raster IRQ'
            legacyFallback = 'None after VAG1 authority begins'
            offlineSQ1BootPassed = $true
            firmwareBuildPassed = $true
            physicalGatePassed = $false
            performanceGatePassed = $false
        },
        [ordered]@{
            name = 'service-query'
            command = 47
            completionStatus = 175
            behavior = 'Discover the MHS platform plus AGI Engine, bounded PowerVM, compiler-certified AGI scan, and mailbox-only priority-line services'
        },
        [ordered]@{
            name = 'power-vm'
            command = 48
            completionStatus = 176
            behavior = 'Run a checksummed cartridge task with bounded bytecode, C64 input/output spans, and an instruction watchdog'
        },
        [ordered]@{
            name = 'agi-pure-prefix-scan'
            command = 49
            completionStatus = 177
            behavior = 'Evaluate only certified predecoded Logic-0 IF/GOTO control flow, buffer HADMATCH, and return before the first native command'
        },
        [ordered]@{
            name = 'priority-line-query'
            command = 50
            completionStatus = 178
            queryPage = 4
            service = 18
            serviceVersion = 1
            capabilityMask = 7
            capabilityFormula = '$01 mailbox-only/no DMA | $02 retained-room seed and token bound | $04 exact trigger and whole-footline-water result'
            request = '$DFF8 X | $DFF9 Y | $DFFA width | $DFFB policy mask $23 | $DFFC-$DFFD zero | $DFFE machine flags | $DFFF room-seed token'
            result = '$DFF8-$DFFA PQL | $DFFB zero | $DFFC pass/trigger/all-water bits | $DFFD zero | $DFFE $07 | $DFFF echoed token'
            invalidRequestError = 27
            invalidRequestStatus = '$FB'
            mailboxOnly = $true
            dmaReads = 0
            dmaWrites = 0
            behavior = 'Evaluate one candidate cel foot line against the already retained room priority map; valid blocking is a normal $B2 result and every rejected request falls back to the native C64 scan'
        },
        [ordered]@{
            name = 'native-agi-transaction'
            service = 32
            serviceVersion = 1
            queryPage = 5
            capabilityMask = 63
            commands = [ordered]@{
                open = 64
                tickPost = 65
                tickStatus = 66
                tickCommit = 67
                hostAck = 68
                egoFrame = 69
            }
            completionStatuses = [ordered]@{
                opened = 192
                ready = 193
                status = 194
                committed = 195
                acknowledged = 196
                egoFrame = 197
            }
            package = 'MGA1 v1 with CMP1 compatibility manifest, canonical section directory, and CRC-32 validation'
            input = 'Fixed 64-byte MTI1 sequence/epoch/state-hash doorbell read in one bounded DMA transaction'
            result = 'Fixed 64-byte MTR1 staged result written only to compiler-reserved $5A80-$5ABF in one bounded DMA transaction, then accepted or discarded by explicit host ACK'
            egoFrame = 'Resolve immutable CAGI/GBC1/CSPR3 or CSPM1 data from the pack, read only the exact 160-byte object table, and stage exactly 128 masked ego bytes at $5A70-$5AEF without writing live game or video state'
            milestone = 'Accepted no-op transport smoke test; event-bearing ticks fail closed until the differential-tested retained AGI engine is installed'
            behavior = 'Validate one immutable per-game package, stage one sequenced tick without touching live C64 state, publish only to the exact ordinary-RAM exchange window, and commit retained sequence only after host validation and ACK'
        },
        [ordered]@{
            name = 'picture-dma'
            command = 16
            requiredCapabilityMask = 139
            conditionalCapabilityBits = @('4 when compiler-proven cell patches are packed', '16 when Exomizer is used', '32 when compact priority is used', '64 for Magic Desk paging')
            completionStatus = 144
            behavior = 'Decode and DMA bitmap, screen, colour, and exact priority representation'
        },
        [ordered]@{
            name = 'picture-prefetch'
            command = 17
            requiredCapabilityMask = 2
            completionStatus = 145
            behavior = 'Resolve an indexed AGP3 picture and prepare all decoded planes without blocking the C64'
        },
        [ordered]@{
            name = 'commit-prefetch'
            command = 18
            requiredCapabilityMask = 3
            completionStatus = 146
            behavior = 'Atomically DMA the matching prepared picture, or report a prefetch miss for exact fallback'
        },
        [ordered]@{
            name = 'cell-patch-dma'
            command = 32
            requiredCapabilityMask = 132
            completionStatus = 160
            behavior = 'Validate a complete absolute GAC3 patch, then scatter-DMA bitmap, screen, and colour cells'
        },
        [ordered]@{
            name = 'scene-prefetch'
            command = 33
            requiredCapabilityMask = 6
            completionStatus = 161
            maximumSceneBytes = 65535
            behavior = 'Atomically prepare one exact GAC3 scene across up to eight swap buffers while the game continues'
        },
        [ordered]@{
            name = 'room-seed'
            command = 34
            requiredCapabilityMask = 132
            completionStatus = 162
            retainedCellLimit = 212
            acceptsNativePictureFallback = $true
            prerequisite = 'Completed C64 planes plus valid priority format/span and idle DMA; no prior AGI+ picture-decode success is required'
            behavior = 'Capture and validate the complete live bitmap, screen, colour, and priority room base after SHOW.PIC or permanent ADD.TO.PIC, including a completed exact native-C64 picture fallback'
        },
        [ordered]@{
            name = 'actor-frame'
            command = 35
            requiredCapabilityMask = 132
            completionStatus = 163
            retainedCellLimit = 212
            nativeFirstObject = 1
            behavior = 'Compose the current non-ego object cohort from GBC1 view data, repair old and overlapping cells, and publish one bounded DMA frame'
        }
    )
    roomArtContract = [ordered]@{
        immutableBase = 'Replaced only by room-seed after SHOW.PIC or permanent ADD.TO.PIC'
        transientPatchRule = 'GAC3 cell-patch DMA marks affected cells dirty but never bakes actor art into the immutable room base'
        objectCount = 20
        objectTableBytes = 160
        retainedCellLimit = 212
        viewCache = 'Materialize the GBC1 index and up to twenty distinct room views into PSRAM when available; bounded cartridge reads and a 128-pattern cache otherwise'
        egoOwner = 'C64 VIC sprite compositor by default; actor-frame begins with object 1'
        fallback = 'Validate and stage the complete patch plus clean-cell metadata before the first DMA write; any rejection returns to the unchanged native compositor'
    }
    backwardCompatibility = [ordered]@{
        signatures = @('AGI+2', 'AGI+3')
        helperBank = 62
        commands = @(1, 2, 3, 16, 17, 18, 32, 33, 34, 35)
        status = 'Existing AGI+2 and AGI+3 activation, commands, statuses, and fallback remain byte-for-byte compatible'
    }
    cartridgeFormats = @('EasyFlash 1 MiB', 'Magic Desk 16K 1 MiB', 'Magic Desk 16K 2 MiB')
    visualCodecs = @('C64-RLE all visible planes', 'Exomizer bitmap plus C64-RLE screen/colour', 'Exomizer all visible planes')
    priorityFormats = @('C64-RLE full 13,440-byte runtime form', 'Exomizer full 13,440-byte runtime form', 'compact format 3 materialized to its exact variable-length runtime form')
    videoStandardContract = [ordered]@{
        sourceAddress = '$02A6'
        mailboxAddress = '$DFFE'
        ntsc = '$02A6 == 0 writes bit 0 set'
        pal = '$02A6 != 0 writes zero'
    }
    dmaProbeContract = [ordered]@{
        address = '$0400-$040F'
        seed = 'offset XOR $A5'
        result = 'offset XOR $5A'
        restoresOriginalBytes = $true
    }
    dmaFailSafe = [ordered]@{
        stateTimeoutMilliseconds = 250
        phi2EdgeTimeoutMicroseconds = 100
        error = 10
        status = '$EA'
        cleanup = 'R/W, address, and data return to input before /DMA deasserts'
        c64Policy = 'After acceptance, wait for terminal completion or error before fallback'
    }
    officialRestoreArtifact = [System.IO.Path]::GetFileName($officialRestorePath)
    officialRestoreSha256 = $officialRestoreHash
    officialRestoreBytes = (Get-Item -LiteralPath $officialRestorePath).Length
    arduinoCliVersion = $arduinoCliVersion
    teensyCoreVersion = $teensyCoreVersion
    crc32LibraryVersion = $crc32LibraryVersion
    patches = $patchManifest
    customGui = $customGui
    nativeRuntimeSources = $nativeRuntimeProvenance
    nativeGameSources = $nativeGameProvenance
    nativeDosSources = $nativeDosProvenance
    nativeNesSources = $nativeNesProvenance
    nativeDoomSources = $nativeDoomProvenance
    sourcePath = $SourcePath
    clonedForBuild = $createdClone
}
$json = $manifest | ConvertTo-Json -Depth 8
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $utf8NoBom)

Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') `
    -Destination (Join-Path $artifactDir 'START-HERE.md') -Force

$checksumNames = @(
    [System.IO.Path]::GetFileName($artifactPath),
    [System.IO.Path]::GetFileName($officialRestorePath),
    'START-HERE.md',
    'MHS-POWER-ENGINE.md'
)
$checksumFiles = @($checksumNames | ForEach-Object {
    $candidate = Join-Path $artifactDir $_
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { Get-Item -LiteralPath $candidate }
})
$checksumLines = $checksumFiles | Sort-Object Name -Unique | ForEach-Object {
    '{0}  {1}' -f (Get-Sha256Hex $_.FullName), $_.Name
}
$checksumPath = Join-Path $artifactDir 'SHA256SUMS.txt'
[System.IO.File]::WriteAllText($checksumPath,
    ($checksumLines -join [Environment]::NewLine) + [Environment]::NewLine, $utf8NoBom)


Write-Host "Built MHS Power Engine for TeensyROM+ firmware (not flashed): $artifactPath"
Write-Host "SHA-256: $artifactHash"
Get-Item -LiteralPath $artifactPath, $officialRestorePath,
        (Join-Path $artifactDir 'START-HERE.md'), $checksumPath |
    Select-Object FullName, Length, LastWriteTime |
    Format-List

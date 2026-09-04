param(
    [Parameter(Mandatory = $true)]
    [string]$SourcePath,
    [string]$AdaptedSourcePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$SourcePath = [System.IO.Path]::GetFullPath($SourcePath)
if ([string]::IsNullOrWhiteSpace($AdaptedSourcePath)) {
    $AdaptedSourcePath = Join-Path $repoRoot 'build\doom\adapted\mcume-teensydoom'
}
$AdaptedSourcePath = [System.IO.Path]::GetFullPath($AdaptedSourcePath)

function Read-NormalizedText([string]$Path) {
    return [System.IO.File]::ReadAllText($Path).Replace("`r`n", "`n").Replace("`r", "`n")
}

function Write-NormalizedText([string]$Path, [string]$Text) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Text.Replace("`r`n", "`n").Replace("`r", "`n"), $utf8NoBom)
}

function Replace-SingleRegex(
    [string]$Text,
    [string]$Pattern,
    [string]$Replacement,
    [string]$Description
) {
    $options = [System.Text.RegularExpressions.RegexOptions]::Multiline
    $matches = [System.Text.RegularExpressions.Regex]::Matches($Text, $Pattern, $options)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one $Description, found $($matches.Count)."
    }
    return [System.Text.RegularExpressions.Regex]::Replace(
        $Text, $Pattern, $Replacement, $options)
}

function Replace-SingleLiteral(
    [string]$Text,
    [string]$Before,
    [string]$After,
    [string]$Description
) {
    $first = $Text.IndexOf($Before, [System.StringComparison]::Ordinal)
    if ($first -lt 0) {
        throw "Unable to find $Description."
    }
    if ($Text.IndexOf($Before, $first + $Before.Length,
            [System.StringComparison]::Ordinal) -ge 0) {
        throw "Found more than one $Description."
    }
    return $Text.Substring(0, $first) + $After +
        $Text.Substring($first + $Before.Length)
}

function Assert-SingleLiteral(
    [string]$Text,
    [string]$Needle,
    [string]$Description
) {
    $first = $Text.IndexOf($Needle, [System.StringComparison]::Ordinal)
    $last = $Text.LastIndexOf($Needle, [System.StringComparison]::Ordinal)
    if ($first -lt 0 -or $first -ne $last) {
        throw "Expected exactly one $Description."
    }
}

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-RelativeUnixPath([string]$Root, [string]$Path) {
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside its expected root: $pathFull"
    }
    return $pathFull.Substring($rootFull.Length).Replace('\', '/')
}

function Get-InventorySha256([object[]]$Entries) {
    $inventory = @($Entries | Sort-Object file | ForEach-Object {
        "$($_.sha256) $($_.bytes) $($_.file)"
    }) -join "`n"
    if ($inventory.Length -ne 0) {
        $inventory += "`n"
    }
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $hasher.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($inventory))
    }
    finally {
        $hasher.Dispose()
    }
    return (($digest | ForEach-Object { $_.ToString('x2') }) -join '')
}

$sketchRoot = Join-Path $SourcePath 'Source\Teensy\MinimalBoot'
$commonDefsPath = Join-Path $sketchRoot 'Common\Common_Defs.h'
$linkerRoot = Join-Path $SourcePath 'Source\Teensy\tools\BootLinkerFiles'
$lowerLinkerPath = Join-Path $linkerRoot 'imxrt1062_t41.ld.orig'
$upperLinkerPath = Join-Path $linkerRoot 'imxrt1062_t41.ld.upper'
$upperBootDataPath = Join-Path $linkerRoot 'bootdata.c.upper'
$guiMarkerPath = Join-Path $SourcePath '.mhs-custom-gui.json'

Assert-File (Join-Path $sketchRoot 'MinimalBoot.ino') 'MinimalBoot sketch'
Assert-File $commonDefsPath 'MinimalBoot common definitions'
Assert-File $lowerLinkerPath 'lower linker template'
Assert-File $upperLinkerPath 'upper linker template'
Assert-File $upperBootDataPath 'upper boot-data template'
Assert-File $guiMarkerPath 'post-GUI preparation marker'
try {
    $guiMarker = Read-NormalizedText $guiMarkerPath | ConvertFrom-Json
}
catch {
    throw "Post-GUI preparation marker is invalid JSON: $guiMarkerPath"
}
if ($null -eq $guiMarker.snapshotDigest -or
    [string]$guiMarker.snapshotDigest -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Post-GUI preparation marker has no valid snapshot digest.'
}
if (-not (Test-Path -LiteralPath $AdaptedSourcePath -PathType Container)) {
    throw "Adapted MCUME Doom source is missing: $AdaptedSourcePath"
}

$originPath = Join-Path $repoRoot 'doom\third_party\mcume-teensydoom.origin.json'
Assert-File $originPath 'MCUME source-origin lock'
try {
    $sourceOrigin = Read-NormalizedText $originPath | ConvertFrom-Json
}
catch {
    throw "MCUME source-origin lock is invalid JSON: $originPath"
}
if ([string]$sourceOrigin.commit -notmatch '^[0-9a-fA-F]{40}$' -or
    [string]$sourceOrigin.treeSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'MCUME source-origin lock has no valid commit and tree digest.'
}

$adaptedInventory = @(Get-ChildItem -LiteralPath $AdaptedSourcePath -Recurse -Force -File |
    ForEach-Object {
        $relative = Get-RelativeUnixPath $AdaptedSourcePath $_.FullName
        if (-not $relative.StartsWith('.git/', [System.StringComparison]::OrdinalIgnoreCase)) {
            [pscustomobject][ordered]@{
                file = $relative
                sha256 = Get-Sha256 $_.FullName
                bytes = $_.Length
            }
        }
    } | Sort-Object file)
$adaptedTreeSha256 = Get-InventorySha256 $adaptedInventory

$allAdaptedC = @(Get-ChildItem -LiteralPath $AdaptedSourcePath -File -Filter '*.c' |
    Sort-Object Name)
$adaptedHeaders = @(Get-ChildItem -LiteralPath $AdaptedSourcePath -File -Filter '*.h' |
    Sort-Object Name)
if ($allAdaptedC.Count -ne 79) {
    throw "Adapted Doom tree must contain 79 C files before excluding i_main.c; found $($allAdaptedC.Count)."
}
if ($adaptedHeaders.Count -ne 104) {
    throw "Adapted Doom tree must contain 104 headers; found $($adaptedHeaders.Count)."
}
if (@($allAdaptedC | Where-Object Name -CEQ 'i_main.c').Count -ne 1 -or
    @($allAdaptedC | Where-Object Name -CEQ 'mhs_native_adapter.c').Count -ne 1) {
    throw 'Adapted Doom tree must contain exactly one i_main.c and mhs_native_adapter.c.'
}
$coreSources = @($allAdaptedC | Where-Object Name -CNE 'i_main.c')
if ($coreSources.Count -ne 78) {
    throw "Exactly 78 Doom C translation units must be staged; found $($coreSources.Count)."
}
foreach ($marker in @(
    @{ File = 'd_loop.c'; Text = 'boolean D_RunSingleTic(void)' },
    @{ File = 'd_main.c'; Text = '#ifndef MHS_NATIVE_DOOM_ADAPTER' },
    @{ File = 'i_video.c'; Text = 'const byte *MHS_I_CurrentPalette(size_t *bytes)' },
    @{ File = 'mhs_native_adapter.c'; Text = 'int MHS_DoomRunOneTic(' }
)) {
    $markerPath = Join-Path $AdaptedSourcePath $marker.File
    Assert-File $markerPath "adapted source $($marker.File)"
    if (-not (Select-String -LiteralPath $markerPath -SimpleMatch $marker.Text -Quiet)) {
        throw "Adapted Doom marker is missing from $($marker.File): $($marker.Text)"
    }
}
$wadFiles = @(Get-ChildItem -LiteralPath $AdaptedSourcePath -Recurse -Force -File |
    Where-Object Extension -IEQ '.wad')
if ($wadFiles.Count -ne 0) {
    throw 'Adapted Doom source unexpectedly contains WAD data.'
}

$nativeDoomRoot = Join-Path $repoRoot 'engine\native-doom'
$nativeFiles = @(
    'mpe_doom_runtime.cpp',
    'mpe_doom_runtime.h',
    'mpe_doom_session.cpp',
    'mpe_doom_session.h',
    'mpe_doom_video.cpp',
    'mpe_doom_video.h',
    'mpe7_core_config.h',
    'mpe7_target.cpp',
    'mpe7_target.h'
)
$firmwareHeaderName = 'mpe7_firmware.h'
foreach ($name in $nativeFiles) {
    Assert-File (Join-Path $nativeDoomRoot $name) "native Doom file $name"
}
Assert-File (Join-Path $nativeDoomRoot $firmwareHeaderName) `
    "native Doom firmware header $firmwareHeaderName"

$commonDefs = Read-NormalizedText $commonDefsPath
$lowerLinker = Read-NormalizedText $lowerLinkerPath
$upperLinker = Read-NormalizedText $upperLinkerPath
$upperBootData = Read-NormalizedText $upperBootDataPath

$commonDefs = Replace-SingleRegex $commonDefs `
    '^(\s*#define\s+UpperAddr\s+)0x(?:060000|180000)(\s*//.*)?$' `
    '${1}0x180000${2}' 'UpperAddr definition'
$lowerLinker = Replace-SingleRegex $lowerLinker `
    '^(\s*FLASH\s+\(rwx\):\s*ORIGIN\s*=\s*)0x60000000(\s*,\s*LENGTH\s*=\s*)(?:7936K|1536K)\s*$' `
    '${1}0x60000000${2}1536K' 'lower FLASH memory definition'
$upperLinker = Replace-SingleRegex $upperLinker `
    '^(\s*FLASH\s+\(rwx\):\s*ORIGIN\s*=\s*)0x60(?:060000|180000)(\s*,\s*LENGTH\s*=\s*)(?:7552K|6400K)\s*$' `
    '${1}0x60180000${2}6400K' 'upper FLASH memory definition'
$upperBootData = Replace-SingleRegex $upperBootData `
    '^(\s*)0x60(?:060000|180000),(\s*)$' `
    '${1}0x60180000,${2}' 'upper BootData base address'

$layoutMarker = '/* MPE7_DOOM_LINKER_LAYOUT_V1 */'
if (-not $lowerLinker.Contains($layoutMarker)) {
    $lowerLinker = Replace-SingleLiteral $lowerLinker `
        "SECTIONS`n{" `
        "SECTIONS`n{`n`t$layoutMarker" 'lower SECTIONS opening'

    $flashmemLine = "`t`t*(.flashmem*)"
    $codeRouting = @'
		*(.flashmem*)
		/* Only the 78 adapted Doom C translation units use this prefix. */
		*mhsdoom_core_*.o(.text .text.* .gnu.linkonce.t.*)
		/* Native orchestration remains separately named, but also executes XIP. */
		*mpe_doom_*.o(.text .text.* .gnu.linkonce.t.*)
		*mpe7_target*.o(.text .text.* .gnu.linkonce.t.*)
'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker $flashmemLine `
        $codeRouting.TrimEnd("`r", "`n") 'lower flashmem input rule'

    $itcmOpening = "`t.text.itcm : {"
    $rodataRouting = @'
	.text.doom_rodata : {
		*mhsdoom_core_*.o(.rodata .rodata.* .srodata .srodata.* .gnu.linkonce.r.*)
		*mpe_doom_*.o(.rodata .rodata.* .srodata .srodata.* .gnu.linkonce.r.*)
		*mpe7_target*.o(.rodata .rodata.* .srodata .srodata.* .gnu.linkonce.r.*)
		. = ALIGN(4);
	} > FLASH

	.text.itcm : {
'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker $itcmOpening `
        $rodataRouting.TrimEnd("`r", "`n") 'lower ITCM output opening'

    $oldDma = @'
	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM

'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker `
        $oldDma.Replace("`r`n", "`n") '' 'original RAM2 DMA output section'

    $dataOpening = "`t.data : {"
    $ram2Overlay = @'
	/* Boot-time DMA state and reset-only Doom state intentionally share RAM2. */
	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM
	OVERLAY ORIGIN(RAM) : NOCROSSREFS
	{
		.mpe7.ram {
			. = ALIGN(32);
			__mpe7_data_start = .;
			*mhsdoom_core_*.o(.data .data.* .sdata .sdata.*)
			__mpe7_data_end = .;
			. = ALIGN(32);
			__mpe7_bss_start = .;
			*mhsdoom_core_*.o(.bss .bss.* .sbss .sbss.*)
			*mhsdoom_core_*.o(COMMON)
			__mpe7_bss_end = .;
			. = ALIGN(32);
			__mpe7_runtime_start = .;
		}
	} > RAM AT> FLASH
	__mpe7_data_load = LOADADDR(.mpe7.ram);
	__mpe7_runtime_end = ORIGIN(RAM) + LENGTH(RAM);

	.data : {
'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker $dataOpening `
        $ram2Overlay.TrimEnd("`r", "`n") 'lower DTCM data output opening'

    $extramEnd = "`t_extram_end = ADDR(.bss.extram) + SIZEOF(.bss.extram);"
    $zoneSymbols = @'
	_extram_end = ADDR(.bss.extram) + SIZEOF(.bss.extram);

	/* MPE7 takes exclusive reset-only ownership after invalidating AGI PSRAM. */
	PROVIDE(MemPool = ORIGIN(ERAM));
	__mpe7_zone_start = ORIGIN(ERAM);
	__mpe7_zone_end = __mpe7_zone_start + 0x00800000;
'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker $extramEnd `
        $zoneSymbols.TrimEnd("`r", "`n") 'lower EXTRAM end symbol'

    $flashImageLength = "`t_flashimagelen = __text_csf_end - ORIGIN(FLASH);"
    $layoutAssertions = @'
	_flashimagelen = __text_csf_end - ORIGIN(FLASH);

	ASSERT(__mpe7_bss_end <= ORIGIN(RAM) + LENGTH(RAM), "MPE7 RAM2 overlay overflow")
	ASSERT(__mpe7_runtime_end - __mpe7_runtime_start >= 0x00020000, "MPE7 leaves under 128 KiB runtime RAM2")
	ASSERT(MemPool == ORIGIN(ERAM), "MPE7 MemPool must begin at PSRAM base")
	ASSERT(__mpe7_zone_end <= ORIGIN(ERAM) + LENGTH(ERAM), "MPE7 PSRAM zone overflow")
	ASSERT(__text_csf_end <= ORIGIN(FLASH) + LENGTH(FLASH), "MinimalBoot overlaps upper firmware")
	ASSERT(_itcm_block_count <= 4, "MinimalBoot consumed a fifth ITCM bank")
	ASSERT(_estack >= _ebss + 0x00004000, "MinimalBoot stack is below 16 KiB")
'@
    $lowerLinker = Replace-SingleLiteral $lowerLinker $flashImageLength `
        $layoutAssertions.TrimEnd("`r", "`n") 'lower flash image length symbol'
}

# Migrate the earliest V1 helper output, which put the PROGBITS .dmabuffers
# inputs inside the loadable overlay and unnecessarily consumed lower flash.
$legacyDmaOverlay = @'
	OVERLAY ORIGIN(RAM) : NOCROSSREFS
	{
		.bss.dma {
			*(.hab_log)
			*(.dmabuffers)
			. = ALIGN(32);
		}
		.mpe7.ram {
'@
$noloadDmaOverlay = @'
	.bss.dma (NOLOAD) : {
		*(.hab_log)
		*(.dmabuffers)
		. = ALIGN(32);
	} > RAM
	OVERLAY ORIGIN(RAM) : NOCROSSREFS
	{
		.mpe7.ram {
'@
if ($lowerLinker.Contains($legacyDmaOverlay.Replace("`r`n", "`n"))) {
    $lowerLinker = Replace-SingleLiteral $lowerLinker `
        $legacyDmaOverlay.Replace("`r`n", "`n") `
        $noloadDmaOverlay.Replace("`r`n", "`n") 'legacy loadable DMA overlay member'
}

Assert-SingleLiteral $commonDefs '#define UpperAddr           0x180000' 'shifted UpperAddr definition'
Assert-SingleLiteral $lowerLinker $layoutMarker 'MPE7 linker layout marker'
Assert-SingleLiteral $lowerLinker 'FLASH (rwx): ORIGIN = 0x60000000, LENGTH = 1536K' `
    'bounded lower FLASH definition'
Assert-SingleLiteral $upperLinker 'FLASH (rwx): ORIGIN = 0x60180000, LENGTH = 6400K' `
    'shifted upper FLASH definition'
Assert-SingleLiteral $upperBootData "`t0x60180000," 'shifted upper BootData address'
Assert-SingleLiteral $lowerLinker 'OVERLAY ORIGIN(RAM) : NOCROSSREFS' 'RAM2 overlay'
Assert-SingleLiteral $lowerLinker '.bss.dma (NOLOAD) : {' 'non-loadable boot DMA section'
Assert-SingleLiteral $lowerLinker '__mpe7_data_load = LOADADDR(.mpe7.ram);' `
    'MPE7 initialized-data load symbol'
Assert-SingleLiteral $lowerLinker '__mpe7_runtime_start = .;' 'MPE7 runtime start symbol'
Assert-SingleLiteral $lowerLinker '__mpe7_runtime_end = ORIGIN(RAM) + LENGTH(RAM);' `
    'MPE7 runtime end symbol'
Assert-SingleLiteral $lowerLinker 'PROVIDE(MemPool = ORIGIN(ERAM));' 'MPE7 MemPool symbol'
Assert-SingleLiteral $lowerLinker '__mpe7_zone_end = __mpe7_zone_start + 0x00800000;' `
    'eight MiB MPE7 zone bound'
if ($lowerLinker.Contains("`t`t.bss.dma {") -or
    $lowerLinker.Contains('LENGTH = 7936K') -or
    $upperLinker.Contains('0x60060000') -or
    $upperLinker.Contains('LENGTH = 7552K') -or
    $upperBootData.Contains('0x60060000') -or
    $commonDefs -match '(?m)^\s*#define\s+UpperAddr\s+0x060000') {
    throw 'A legacy lower/upper layout value survived the deterministic transform.'
}
$codeIndex = $lowerLinker.IndexOf('*mhsdoom_core_*.o(.text ', [System.StringComparison]::Ordinal)
$rodataIndex = $lowerLinker.IndexOf('.text.doom_rodata', [System.StringComparison]::Ordinal)
$itcmIndex = $lowerLinker.IndexOf('.text.itcm', [System.StringComparison]::Ordinal)
$overlayIndex = $lowerLinker.IndexOf('OVERLAY ORIGIN(RAM)', [System.StringComparison]::Ordinal)
$dataIndex = $lowerLinker.IndexOf("`t.data : {", [System.StringComparison]::Ordinal)
$bssIndex = $lowerLinker.IndexOf("`t.bss ALIGN(4) : {", [System.StringComparison]::Ordinal)
if ($codeIndex -lt 0 -or $rodataIndex -lt 0 -or $itcmIndex -lt 0 -or
    $overlayIndex -lt 0 -or $dataIndex -lt 0 -or $bssIndex -lt 0 -or
    $codeIndex -ge $itcmIndex -or $rodataIndex -ge $itcmIndex -or
    $overlayIndex -ge $dataIndex -or $dataIndex -ge $bssIndex) {
    throw 'MPE7 linker sections are not ordered before the broad ITCM/DTCM collectors.'
}
$coreGlobCount = [System.Text.RegularExpressions.Regex]::Matches(
    $lowerLinker, [regex]::Escape('*mhsdoom_core_*.o(')).Count
if ($coreGlobCount -ne 5) {
    throw "Lower linker must contain five Doom core object collectors; found $coreGlobCount."
}
$nativeCodeGlobCount = [System.Text.RegularExpressions.Regex]::Matches(
    $lowerLinker, [regex]::Escape('*mpe_doom_*.o(')).Count
$targetCodeGlobCount = [System.Text.RegularExpressions.Regex]::Matches(
    $lowerLinker, [regex]::Escape('*mpe7_target*.o(')).Count
if ($nativeCodeGlobCount -ne 2 -or $targetCodeGlobCount -ne 2) {
    throw 'Native Doom C++ code and read-only data must have exactly one flash collector each.'
}

# All transforms are validated before the first destination write.
Write-NormalizedText $commonDefsPath $commonDefs
Write-NormalizedText $lowerLinkerPath $lowerLinker
Write-NormalizedText $upperLinkerPath $upperLinker
Write-NormalizedText $upperBootDataPath $upperBootData

$expectedCoreNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($source in $coreSources) {
    [void]$expectedCoreNames.Add("mhsdoom_core_$($source.Name)")
}
foreach ($existing in @(Get-ChildItem -LiteralPath $sketchRoot -File -Filter 'mhsdoom_core_*.c')) {
    if (-not $expectedCoreNames.Contains($existing.Name)) {
        Remove-Item -LiteralPath $existing.FullName -Force
    }
}
if (@(Get-ChildItem -LiteralPath $sketchRoot -File -Filter 'mhsdoom_core_*.cpp').Count -ne 0) {
    throw 'Only adapted C files may use the mhsdoom_core_ object prefix.'
}

$corePreamble = @'
#define MHS_NATIVE_DOOM_ADAPTER 1
#include "mpe7_core_config.h"
'@.Replace("`r`n", "`n").TrimEnd("`n") + "`n"
foreach ($source in $coreSources) {
    $destination = Join-Path $sketchRoot "mhsdoom_core_$($source.Name)"
    $sourceText = Read-NormalizedText $source.FullName
    Write-NormalizedText $destination ($corePreamble + $sourceText)
}
foreach ($header in $adaptedHeaders) {
    Copy-Item -LiteralPath $header.FullName -Destination (Join-Path $sketchRoot $header.Name) -Force
}
foreach ($name in $nativeFiles) {
    Copy-Item -LiteralPath (Join-Path $nativeDoomRoot $name) `
        -Destination (Join-Path $sketchRoot $name) -Force
}
$nativeDoomIncludeRoot = Join-Path $sketchRoot 'Common\NativeDoom'
if (-not (Test-Path -LiteralPath $nativeDoomIncludeRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $nativeDoomIncludeRoot -Force | Out-Null
}
Copy-Item -LiteralPath (Join-Path $nativeDoomRoot $firmwareHeaderName) `
    -Destination (Join-Path $nativeDoomIncludeRoot $firmwareHeaderName) -Force

$stagedCore = @(Get-ChildItem -LiteralPath $sketchRoot -File -Filter 'mhsdoom_core_*.c' |
    Sort-Object Name)
if ($stagedCore.Count -ne 78) {
    throw "Staged Doom core must contain exactly 78 prefixed C files; found $($stagedCore.Count)."
}
if (Test-Path -LiteralPath (Join-Path $sketchRoot 'mhsdoom_core_i_main.c')) {
    throw 'i_main.c must not be staged into the Arduino sketch.'
}
foreach ($staged in $stagedCore) {
    $text = Read-NormalizedText $staged.FullName
    if (-not $text.StartsWith($corePreamble, [System.StringComparison]::Ordinal)) {
        throw "Core compatibility preamble is missing from $($staged.Name)."
    }
}
foreach ($header in $adaptedHeaders) {
    $destination = Join-Path $sketchRoot $header.Name
    if ((Get-Sha256 $destination) -ne (Get-Sha256 $header.FullName)) {
        throw "Staged Doom header differs from adapted source: $($header.Name)"
    }
}
foreach ($name in $nativeFiles) {
    $destination = Join-Path $sketchRoot $name
    if ((Get-Sha256 $destination) -ne (Get-Sha256 (Join-Path $nativeDoomRoot $name))) {
        throw "Staged native Doom file differs from repository source: $name"
    }
}
$firmwareHeaderDestination = Join-Path $nativeDoomIncludeRoot $firmwareHeaderName
if ((Get-Sha256 $firmwareHeaderDestination) -ne
    (Get-Sha256 (Join-Path $nativeDoomRoot $firmwareHeaderName))) {
    throw "Staged native Doom firmware header differs from repository source: $firmwareHeaderName"
}

$sourceEntries = @()
foreach ($staged in $stagedCore) {
    $sourceEntries += [pscustomobject][ordered]@{
        file = $staged.Name
        sha256 = Get-Sha256 $staged.FullName
        kind = 'adapted-core-c'
        sourceFile = $staged.Name.Substring('mhsdoom_core_'.Length)
    }
}
foreach ($header in $adaptedHeaders) {
    $destination = Join-Path $sketchRoot $header.Name
    $sourceEntries += [pscustomobject][ordered]@{
        file = $header.Name
        sha256 = Get-Sha256 $destination
        kind = 'adapted-header'
        sourceFile = $header.Name
    }
}
foreach ($name in $nativeFiles) {
    $destination = Join-Path $sketchRoot $name
    $sourceEntries += [pscustomobject][ordered]@{
        file = $name
        sha256 = Get-Sha256 $destination
        kind = 'native-doom'
        sourceFile = "engine/native-doom/$name"
    }
}
$sourceEntries += [pscustomobject][ordered]@{
    file = "Common/NativeDoom/$firmwareHeaderName"
    sha256 = Get-Sha256 $firmwareHeaderDestination
    kind = 'native-doom-firmware'
    sourceFile = "engine/native-doom/$firmwareHeaderName"
}
$sourceEntries = @($sourceEntries | Sort-Object file)
$stagedTreeEntries = @($sourceEntries | ForEach-Object {
    [pscustomobject][ordered]@{
        file = $_.file
        sha256 = $_.sha256
        bytes = (Get-Item -LiteralPath (Join-Path $sketchRoot $_.file)).Length
    }
})
$stagedTreeSha256 = Get-InventorySha256 $stagedTreeEntries

$linkerEntries = @(
    [pscustomobject][ordered]@{
        file = 'Source/Teensy/MinimalBoot/Common/Common_Defs.h'
        sha256 = Get-Sha256 $commonDefsPath
    },
    [pscustomobject][ordered]@{
        file = 'Source/Teensy/tools/BootLinkerFiles/imxrt1062_t41.ld.orig'
        sha256 = Get-Sha256 $lowerLinkerPath
    },
    [pscustomobject][ordered]@{
        file = 'Source/Teensy/tools/BootLinkerFiles/imxrt1062_t41.ld.upper'
        sha256 = Get-Sha256 $upperLinkerPath
    },
    [pscustomobject][ordered]@{
        file = 'Source/Teensy/tools/BootLinkerFiles/bootdata.c.upper'
        sha256 = Get-Sha256 $upperBootDataPath
    }
)

[ordered]@{
    status = 'PASS'
    acceptance = 'firmware-source-staging-only'
    sourcePath = $SourcePath
    adaptedSourcePath = $AdaptedSourcePath
    guiSnapshotDigest = ([string]$guiMarker.snapshotDigest).ToLowerInvariant()
    sourceCommit = ([string]$sourceOrigin.commit).ToLowerInvariant()
    sourceTreeSha256 = ([string]$sourceOrigin.treeSha256).ToLowerInvariant()
    adaptedTreeSha256 = $adaptedTreeSha256
    adaptedTreeFileCount = $adaptedInventory.Count
    adaptedTree = [ordered]@{
        sourceCommit = ([string]$sourceOrigin.commit).ToLowerInvariant()
        sourceTreeSha256 = ([string]$sourceOrigin.treeSha256).ToLowerInvariant()
        sha256 = $adaptedTreeSha256
        fileCount = $adaptedInventory.Count
    }
    stagedTreeSha256 = $stagedTreeSha256
    stagedSourceCount = $stagedCore.Count
    stagedFileCount = $sourceEntries.Count
    coreTranslationUnits = $stagedCore.Count
    excludedCoreEntry = 'i_main.c'
    adaptedHeaders = $adaptedHeaders.Count
    nativeDoomFiles = $nativeFiles.Count + 1
    lowerFlashOrigin = '0x60000000'
    lowerFlashBytes = 0x00180000
    upperFlashOrigin = '0x60180000'
    upperFlashBytes = 0x00640000
    eepromReserveOrigin = '0x607C0000'
    ram2OverlayOrigin = '0x20200000'
    psramZoneOrigin = '0x70000000'
    psramZoneBytes = 0x00800000
    memPoolAddress = '0x70000000'
    sources = $sourceEntries
    linkerFiles = $linkerEntries
    firmwareBuilt = $false
    physicalHardwareProven = $false
} | ConvertTo-Json -Compress

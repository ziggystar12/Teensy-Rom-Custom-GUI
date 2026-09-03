param(
    [string]$Source = '',
    [string]$Elf = '',
    [string]$Nm = ''
)

# Post-link ownership gate for the reset-only DOS session. Source checks prove
# the handoff sequence; symbol checks prove that no live DOS/transport object
# was linked into the 512 KiB physical range subsequently cleared for guest RAM.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Source) { $Source = Join-Path $projectRoot 'build\dos-work\source' }
$Source = [IO.Path]::GetFullPath($Source)
if (-not $Elf) {
    $Elf = Join-Path $Source 'Source\Teensy\MinimalBoot\build\MinimalBoot.ino.elf'
}
$Elf = [IO.Path]::GetFullPath($Elf)

function Assert-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}
function Assert-NoMatch([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) { throw $Message }
}
function Read-Required([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing RAM2 ownership input: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw
}
function Parse-Hex([string]$Hex) {
    return [Convert]::ToUInt64($Hex, 16)
}

$minimal = Join-Path $Source 'Source\Teensy\MinimalBoot\MinimalBoot.ino'
$native = Join-Path $Source 'Source\Teensy\MinimalBoot\Common\NativeDos'
$firmwarePath = Join-Path $native 'mpe5_firmware.h'
$platformPath = Join-Path $native 'mpe5_platform.h'
$cartridgePath = Join-Path $native 'mpe5_cartridge_memory.h'
$directPath = Join-Path $native 'mpe5_direct_memory.h'
$directImplPath = Join-Path $native 'mpe5_direct_memory.cpp'
$adapterPath = Join-Path $native 'mpe5_8086tiny.cpp'
$adapterHeaderPath = Join-Path $native 'mpe5_8086tiny.h'
$corePath = Join-Path $native 'vendor\8086tiny\8086tiny.c'
$titlePath = Join-Path $Source 'Source\Teensy\MinimalBoot\Common\IO_Handlers\IOH_MPE3TitlePull.c'
$firmware = Read-Required $firmwarePath
$platform = Read-Required $platformPath
$cartridge = Read-Required $cartridgePath
$direct = Read-Required $directPath
$directImpl = Read-Required $directImplPath
$adapter = Read-Required $adapterPath
$adapterHeader = Read-Required $adapterHeaderPath
$core = Read-Required $corePath
$title = Read-Required $titlePath
$minimalText = Read-Required $minimal

Assert-Match $platform 'ConventionalRamBytes\s*=\s*512u\s*\*\s*1024u' `
    'Firmware does not declare exactly 512 KiB of conventional DOS RAM.'
Assert-Match $direct 'ConventionalBytes\s*=\s*512u\s*\*\s*1024u' `
    'DirectMemory does not own exactly 512 KiB.'
Assert-Match $firmware '#define\s+MPE5_RAM2_BASE\s+\(\s*\(\s*uint8_t\s*\*\s*\)\s*0x20200000u\s*\)' `
    'DOS conventional address zero is not mapped to physical RAM2 base 0x20200000.'
Assert-Match $firmware 'MPE5Memory\.start\s*\(\s*MPE5_RAM2_BASE\s*,\s*mpe5::ConventionalRamBytes' `
    'Firmware does not start the direct-memory backend.'
Assert-Match $firmware 'Host\.conventionalRam\s*=\s*MPE5_RAM2_BASE' `
    'The CPU fast path is not pinned to physical RAM2.'
Assert-Match $firmware 'Host\.conventionalRamBytes\s*=\s*mpe5::ConventionalRamBytes' `
    'The CPU fast path does not expose exactly the 512 KiB declaration.'
Assert-Match $adapter 'MPE5Host\.conventionalRam\s*\+\s*address' `
    'The CPU adapter has no direct conventional-RAM fast path.'
Assert-Match $firmware 'MPE5Ram2Owned\s*=\s*true' `
    'Firmware never commits exclusive RAM2 ownership.'
Assert-Match $minimalText '!\s*MPE5Ram2Owned[^\r\n]*Serial\.available\s*\(' `
    'MinimalBoot still polls USB Serial after DOS owns its RAM2 buffers.'
Assert-Match $minimalText 'if\s*\(\s*MPE5Ram2Owned\s*\)\s*\{\s*REBOOT\s*;\s*return\s*;' `
    'The physical button can return into destroyed shared state instead of rebooting.'
Assert-Match $title 'if\s*\(\s*MPE5Ram2Owned\s*\)\s*\{\s*REBOOT\s*;\s*return\s+true\s*;' `
    'Cartridge bank loss can return into destroyed shared state instead of rebooting.'
Assert-NoMatch $title 'static\s+DMAMEM\s+MPE3TitleState\s+MPE3Title\b' `
    'Live MPE3 title state still occupies DOS conventional RAM2.'
Assert-NoMatch $title 'static\s+DMAMEM\s+uint8_t\s+MPE3TitlePacket\b' `
    'Live MPE3 packet state still occupies DOS conventional RAM2.'

# Arduino File constructs a heap-backed SDFile. FsFile keeps its Fat/exFAT file
# state inline in RAM1, so disk reads can continue after RAM2 heap destruction.
Assert-Match $firmware '\bFsFile\s+MPE5DiskFile\b' `
    'DOS disk handle is not an inline FsFile.'
Assert-Match $firmware 'SD\.sdfs\.open\s*\(' `
    'DOS disk is not opened through the allocation-free SdFat API.'
Assert-NoMatch $firmware '\bFile\s+MPE5DiskFile\b' `
    'Heap-backed Arduino File remains in the DOS handoff.'
Assert-NoMatch $firmware 'MPE5SwapFile|MPE5(Read|Write)Page|PagedMemory' `
    'The reset-only firmware still contains the SD page/swap backend.'

# RAM1 workspace/high-memory lending must be explicit and valid only after the
# complete tiny CRT is resident. The runtime ownership probes keep active
# EasyFlash/AGI mappings out of the sixteen strided video chunks.
Assert-Match $cartridge 'MPE5BorrowCartridgeTail\s*\(&Arena->workspace\s*,\s*&Arena->workspaceBytes\s*\)' `
    'DOS does not obtain its workspace from the validated RAM_Image tail.'
Assert-Match $cartridge 'Arena->highChunks\s*=\s*SwapBuffers\[0\]\.Image' `
    'B0000h does not begin at the first swap-buffer image.'
Assert-Match $cartridge 'Arena->highStorageBytes\s*=\s*\(uint32_t\)\(SwapLimit\s*-\s*\(uintptr_t\)Arena->highChunks\)' `
    'High-memory storage does not use the exact bytes remaining after its image pointer.'
Assert-Match $cartridge 'Arena->highStride\s*=\s*sizeof\(SwapBuffers\[0\]\)' `
    'High-memory mapping does not skip each swap-buffer metadata word.'
Assert-Match $cartridge 'Pointer\s*>=\s*SwapBase\s*&&\s*Pointer\s*<\s*SwapLimit' `
    'Active cartridge bank mappings are not rejected before borrowing SwapBuffers.'
Assert-Match $cartridge 'AGIPictureSwapBufferIsOwned\s*\(Slot\)' `
    'Active AGI picture swap slots are not rejected before takeover.'

# The only linked RAM2 library buffers outside our engine belong to USB1.
# Stop its interrupt source, controller, and automatic yield polling before
# RAM2 is cleared. Close/free every loader allocation and stage the BIOS in
# the RAM1 cartridge tail first.
Assert-Match $firmware 'NVIC_DISABLE_IRQ\s*\(\s*IRQ_USB1\s*\)' `
    'USB1 interrupt is not disabled before RAM2 takeover.'
Assert-Match $firmware 'NVIC_CLEAR_PENDING\s*\(\s*IRQ_USB1\s*\)' `
    'Pending USB1 interrupts are not cleared before RAM2 takeover.'
Assert-Match $firmware 'USB1_USBCMD\s*&=\s*~USB_USBCMD_RS' `
    'USB1 DMA/controller run state is not stopped before RAM2 takeover.'
Assert-Match $firmware 'MPE5WaitUsb1Clear\s*\(\s*&USB1_ENDPTPRIME' `
    'USB1 endpoint priming is not bounded before RAM2 takeover.'
Assert-Match $firmware 'USB1_ENDPTFLUSH\s*=\s*0xffffffffu' `
    'USB1 endpoints are not flushed before RAM2 takeover.'
Assert-Match $firmware 'MPE5WaitUsb1Clear\s*\(\s*&USB1_ENDPTFLUSH' `
    'USB1 endpoint flush completion is not bounded before RAM2 takeover.'
Assert-Match $firmware 'USB1_ENDPTSTATUS\s*==\s*0' `
    'USB1 endpoint activity is not verified idle before RAM2 takeover.'
Assert-Match $firmware 'USB1_USBCMD\s*\|=\s*USB_USBCMD_RST' `
    'USB1 controller is not reset after stopping its device schedule.'
Assert-Match $firmware 'MPE5WaitUsb1Clear\s*\(\s*&USB1_USBCMD\s*,\s*USB_USBCMD_RST' `
    'USB1 controller reset completion is not bounded before RAM2 takeover.'
Assert-Match $firmware 'yield_active_check_flags\s*&=\s*~\s*\([^;]*YIELD_CHECK_USB_SERIAL' `
    'Teensy yield can still poll USB Serial after its RAM2 buffers are cleared.'
Assert-Match $firmware '\bmyFile\.close\s*\(' `
    'The cartridge loader File is not closed before its RAM2 heap is destroyed.'
Assert-Match $firmware 'uint8_t\s*\*\s*const\s+Bios\s*=\s*Fixed\s*\+\s*0x100u' `
    'The BIOS destination is not inside the RAM1 fixed-F000 workspace.'
Assert-Match $firmware 'MPE4Read\s*\([^;]*\bBios\b\s*,\s*\(\s*uint16_t\s*\)\s*BiosBytes\s*\)' `
    'The BIOS is not staged from the CRT into RAM1 before takeover.'

$quiesceFunction = [regex]::Match($firmware,
    '(?ms)static\s+FLASHMEM\s+bool\s+MPE5QuiesceRam2Services\s*\(\s*\)\s*\{(?<body>.*?)^\}')
if (-not $quiesceFunction.Success) { throw 'Unable to isolate the RAM2 quiesce function.' }
$quiesceBody = $quiesceFunction.Groups['body'].Value
$disableSource = $quiesceBody.IndexOf('USB1_USBINTR = 0')
$disableUsb = $quiesceBody.IndexOf('NVIC_DISABLE_IRQ')
$waitPrime = $quiesceBody.IndexOf('MPE5WaitUsb1Clear(&USB1_ENDPTPRIME')
$flushUsb = $quiesceBody.IndexOf('USB1_ENDPTFLUSH = 0xffffffffu')
$stopUsb = $quiesceBody.IndexOf('USB1_USBCMD &= ~USB_USBCMD_RS')
$resetUsb = $quiesceBody.IndexOf('USB1_USBCMD |= USB_USBCMD_RST')
$disableYield = $quiesceBody.IndexOf('yield_active_check_flags')
$commitOwnership = $quiesceBody.IndexOf('MPE5Ram2Owned = true')
$restoreInterrupts = $quiesceBody.IndexOf('__set_primask')
if ($disableSource -lt 0 -or $disableUsb -le $disableSource -or
    $waitPrime -le $disableUsb -or $flushUsb -le $waitPrime -or
    $stopUsb -le $flushUsb -or $resetUsb -le $stopUsb -or
    $disableYield -le $resetUsb -or $commitOwnership -le $disableYield -or
    $restoreInterrupts -le $commitOwnership) {
    throw 'USB endpoint/controller/IRQ/yield shutdown does not precede RAM2 ownership commit.'
}

$startAt = $firmware.IndexOf('static FLASHMEM bool MPE5Start(uint32_t Root)')
if ($startAt -lt 0) { throw 'Unable to find MPE5Start for handoff-order validation.' }
$startEnd = $firmware.IndexOf('// This runs in the Phi2 handler', $startAt)
if ($startEnd -le $startAt) {
    throw 'Unable to isolate MPE5Start for handoff-order validation.'
}
$startText = $firmware.Substring($startAt, $startEnd - $startAt)
$stageBios = $startText.IndexOf('MPE4Read(nullptr, Root + sizeof(Header), Bios')
$openDisk = $startText.IndexOf('SD.sdfs.open')
$closeLoader = $startText.IndexOf('myFile.close')
$freeDebug = $startText.IndexOf('free(BigBuf)')
$quiesce = $startText.IndexOf('if (!MPE5QuiesceRam2Services())')
$coreStart = $startText.IndexOf('coreStart(Host)')
if ($stageBios -lt 0 -or $openDisk -le $stageBios -or
    $closeLoader -le $openDisk -or $freeDebug -le $closeLoader -or
    $quiesce -le $freeDebug -or $coreStart -le $quiesce) {
    throw 'Required BIOS/disk/loader/quiesce/coreStart handoff order regressed.'
}
$ownedSuffix = $startText.Substring($quiesce)
Assert-NoMatch $ownedSuffix '\b(malloc|calloc|realloc|free|new|delete)\b' `
    'A heap operation remains reachable after DOS commits RAM2 ownership.'

# Avoid silently testing a stale expanded firmware tree.
foreach ($relative in @('mpe5_direct_memory.h', 'mpe5_direct_memory.cpp',
                         'mpe5_platform.h', 'mpe5_firmware.h',
                         'mpe5_cartridge_memory.h', 'mpe5_8086tiny.h',
                         'mpe5_8086tiny.cpp', 'vendor\8086tiny\8086tiny.c')) {
    $canonical = Join-Path (Join-Path $projectRoot 'engine\native-dos') $relative
    $expanded = Join-Path $native $relative
    if ((Get-FileHash -LiteralPath $canonical -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $expanded -Algorithm SHA256).Hash) {
        throw "Expanded firmware source is stale: $relative"
    }
}

if (-not (Test-Path -LiteralPath $Elf -PathType Leaf)) {
    throw "Missing linked MinimalBoot ELF: $Elf"
}
if (-not $Nm) {
    $Nm = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'build\toolchain') `
        -Filter 'arm-none-eabi-nm.exe' -Recurse -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName -First 1
}
if (-not $Nm -or -not (Test-Path -LiteralPath $Nm -PathType Leaf)) {
    throw 'arm-none-eabi-nm.exe is required for the RAM2 ownership gate.'
}

$symbolLines = & $Nm -C -S -n --radix=x $Elf
if ($LASTEXITCODE -ne 0) { throw 'Unable to read symbols from the MinimalBoot ELF.' }
$symbols = foreach ($line in $symbolLines) {
    if ($line -match '^(?<address>[0-9a-fA-F]+)\s+(?<size>[0-9a-fA-F]+)\s+(?<kind>[bBdD])\s+(?<name>.+)$') {
        [pscustomobject]@{
            Address = Parse-Hex $Matches.address
            Size = Parse-Hex $Matches.size
            Kind = $Matches.kind
            Name = $Matches.name.Trim()
        }
    }
}

function Find-Boundary([string]$Name) {
    foreach ($line in $symbolLines) {
        if ($line -match ('^(?<address>[0-9a-fA-F]+)\s+(?:[0-9a-fA-F]+\s+)?[bBdDrR]\s+' +
                          [regex]::Escape($Name) + '$')) {
            return Parse-Hex $Matches.address
        }
    }
    throw "ELF boundary symbol '$Name' is missing."
}

$ram1Start = [uint64]0x20000000
$ram1End = [uint64]0x20080000
$ram2Start = [uint64]0x20200000
$ram2End = [uint64]0x20280000
$heapStart = Find-Boundary '_heap_start'
$heapEnd = Find-Boundary '_heap_end'
if ($heapStart -lt $ram2Start -or $heapStart -ge $ram2End -or
    $heapEnd -ne $ram2End -or $heapStart -ge $heapEnd) {
    throw 'The abandoned allocator does not occupy the expected physical RAM2 tail.'
}
$ram2 = @($symbols | Where-Object {
    $_.Size -gt 0 -and $_.Address -lt $ram2End -and
    $_.Address + $_.Size -gt $ram2Start
})
if (-not $ram2.Count) { throw 'ELF has no RAM2 symbols; wrong image or linker layout.' }

function Require-Symbol([string]$Name, [uint64]$MinimumSize = 1) {
    $matches = @($symbols | Where-Object Name -eq $Name)
    if ($matches.Count -ne 1 -or $matches[0].Size -lt $MinimumSize) {
        throw "ELF symbol '$Name' is missing, duplicated, or undersized."
    }
    return $matches[0]
}

$ramImage = Require-Symbol 'RAM_Image' 0x30000
$swap = Require-Symbol 'SwapBuffers' 0x20040
foreach ($arena in @($ramImage, $swap)) {
    if ($arena.Address -lt $ram1Start -or $arena.Address + $arena.Size -gt $ram1End) {
        throw "$($arena.Name) is not wholly resident in RAM1."
    }
}
if ($ramImage.Address -lt $swap.Address + $swap.Size -and
    $swap.Address -lt $ramImage.Address + $ramImage.Size) {
    throw 'RAM_Image workspace and strided high-memory SwapBuffers overlap.'
}
foreach ($liveName in @('MPE5DiskFile', 'MPE5Memory', 'MPE5Ram2Owned',
                         'MPE3Title', 'MPE3TitlePacket')) {
    $live = Require-Symbol $liveName
    if ($live.Address -lt $ram1Start -or $live.Address + $live.Size -gt $ram1End) {
        throw "$liveName is not wholly resident in RAM1."
    }
}

# Names in this list are actively read or written after the handoff. Any one
# inside RAM2 would corrupt guest memory or be corrupted by it. Core globals
# without an MPE5 prefix are listed explicitly because 8086tiny names them at
# file scope.
$coreLive = @(
    'mem','io_ports','bios_table_lookup','opcode_stream','regs8','regs16',
    'i_rm','i_w','i_reg','i_mod','i_mod_size','i_d','i_reg4bit',
    'raw_opcode_id','xlat_opcode_id','extra','rep_mode','seg_override_en',
    'rep_override_en','trap_flag','int8_asap','scratch_uchar','io_hi_lo',
    'vid_mem_base','spkr_en','reg_ip','seg_override','file_index',
    'wave_counter','op_source','op_dest','rm_addr','op_to_addr',
    'op_from_addr','i_data0','i_data1','i_data2','scratch_uint',
    'scratch2_uint','inst_counter','set_flags_type','GRAPHICS_X','GRAPHICS_Y',
    'pixel_colors','vmem_ctr','op_result','disk','scratch_int'
)
$forbidden = @($ram2 | Where-Object {
    $_.Name -match '^MPE5' -or
    $_.Name -match '^MPE3Title(?!InternalAssets$)' -or
    $coreLive -contains $_.Name
})
if ($forbidden.Count) {
    throw ('DOS/transport live symbols overlap guest RAM2: ' +
        (($forbidden | ForEach-Object { "{0}@0x{1:x}" -f $_.Name,$_.Address }) -join ', '))
}

# Every remaining RAM2 symbol is deliberately dead once MPE5Ram2Owned is set:
# old AGI/MPE engines are inactive, internal assets have been staged, USB1 is
# stopped, and the allocator is abandoned. A new category fails this audit so
# it must be classified instead of silently sharing guest memory.
$deadAfterHandoff = '^(MPE4CrtDirectory|MPE3TitleInternalAssets|' +
    'MPEVirtual.*|MPEThin.*|MHSNative.*|AGIPic.*|MHSPEScan.*|' +
    'MHSPEPower.*|usb_descriptor_buffer|rx_buffer|txbuffer|_heap_start)$'
$unknown = @($ram2 | Where-Object { $_.Name -notmatch $deadAfterHandoff })
if ($unknown.Count) {
    throw ('Unclassified RAM2 symbols require a handoff-liveness audit: ' +
        (($unknown | ForEach-Object { "{0}@0x{1:x}" -f $_.Name,$_.Address }) -join ', '))
}

$usb = @($ram2 | Where-Object Name -in @('usb_descriptor_buffer','rx_buffer','txbuffer'))
if ($usb.Count -ne 3) {
    throw 'Expected USB descriptor/RX/TX buffers were not all linked in RAM2.'
}

Write-Output (("MPE5 RAM2 ownership gate passed: guest 00000h-7FFFFh -> " +
    "0x20200000-0x2027ffff; {0} linked RAM2 symbols are classified dead, " +
    "the abandoned heap starts at 0x{1:x}, and all DOS/transport state plus " +
    "FsFile are in RAM1.") -f $ram2.Count,$heapStart)

# AGI-64 EasyFlash picture accelerator (experimental)

This optional MinimalBoot extension is for a matching, specially packed AGI-64
1 MB EasyFlash CRT on DMA-capable TeensyROM+ v0.4 hardware. Protocol v2 provides
three separately gated operations: decode-only, a bounded 16-byte DMA roundtrip
probe, and full picture DMA. This lets hardware validation prove decompression
before any bus-master write, then exercise DMA in a preserved scratch area
before writing the expanded picture planes. It is not enabled in the upper/full
TeensyROM firmware or on older hardware. The dedicated lower/MinimalBoot image
disables TCP Listen so its cartridge residency does not depend on Ethernet's
runtime allocation.

## Cartridge contract

The cartridge supplies a 24-bit raw offset into the EasyFlash payload. Starting
at that byte, three independently encoded `c64-rle` streams must appear in this
order:

| Stream | Expanded bytes | DMA destination |
| --- | ---: | ---: |
| Bitmap | 8000 | `$6000` |
| Screen | 1000 | `$5C00` |
| Colour | 1000 | `$D800` |

The C64-RLE control format matches AGI-64's `host/c64-rle.mjs`: a control byte
with bit 7 set encodes `(control & $7f) + 3` copies of the following byte;
otherwise it encodes `control + 1` literal bytes.

The special packer must place the picture index plus every visible picture
stream inside the first 24 physical banks (384 KB); priority stays on the C64.
The handler enforces the ceiling on each compressed byte. Every source half-bank
must have a direct RAM pointer in `BankDecode`; any `SwapSeekAddrMask` tag returns
E5 even when an SD swap-cache slot contains that half-bank. It never pauses to
fetch picture data from SD.

Do not use MinimalBoot's often-quoted 53-bank/848 KB *total cartridge capacity*
as the direct-resident cutoff. That total includes its 16 SD swap-cache blocks,
while `BankDecode` deliberately retains tagged pointers for those blocks. This
dedicated build disables TCP Listen and expands its fixed RAM1 image buffer from
160 KB to 264 KB. The conservative 24-bank contract then needs only another
120 KB from RAM2, rather than relying on most of the heap. A real boot should
still audit every accelerated source half. The runtime E5 response remains the
final safety check and causes the C64 decoder to take over.

The C64 caller must keep the EasyFlash bank stable while the request is in
progress; the firmware also ignores `$DE00` bank writes while status is decoding
or DMA, closing the shared DMA-state race. Once a command is accepted, the
caller waits for a terminal completion or error instead of timing out into a
bank-switching fallback. Firmware-side state and PHI2-edge deadlines first
release every driven bus signal and `/DMA`, then publish error 10/status `$ea`.
The caller should mask interrupts around the command/poll sequence so no code
observes the short gaps between the three DMA transfers. After a terminal error,
the caller retains its normal C64 decoder as the fallback.

Protocol v2 supplies a start offset but no compressed end/length. The packer is
therefore responsible for validating the three complete consecutive streams.
Firmware bounds every expanded plane and the 24-bank source window, but cannot
distinguish a truncated stream that happens to decode through following valid
bytes before reaching either bound.

## IO2 mailbox

While locked, IO2 remains ordinary 256-byte EasyFlash RAM. With EasyFlash bank
62 selected, activate the mailbox by writing ASCII `A`, `G`, `I`, `+`, then byte
`2` to `$DFF0-$DFF4` in order. Any intervening IO2 access, wrong byte, or
EasyFlash bank write resets the sequence. After activation, `$DFF0-$DFFF` have
these meanings:

| Address | Read | Write |
| --- | --- | --- |
| `$DFF0` | `A` | ignored |
| `$DFF1` | `G` | ignored |
| `$DFF2` | `I` | ignored |
| `$DFF3` | `+` | ignored |
| `$DFF4` | protocol version (`2`) | ignored |
| `$DFF5` | capability bits | ignored |
| `$DFF6` | reserved/zero | command |
| `$DFF7` | status | ignored |
| `$DFF8-$DFFA` | source offset, little endian | source offset |
| `$DFFB` | error detail | ignored |
| `$DFFC-$DFFD` | elapsed operation milliseconds | ignored |
| `$DFFE` | selected video standard | `1` NTSC, `0` PAL |
| `$DFFF` | reserved | ignored |

Capability bits at `$DFF5` are:

- Bit 0 (`$01`): full bounded C64-RLE decode plus picture DMA.
- Bit 1 (`$02`): bounded C64-RLE decode-only.
- Bit 2 (`$04`): fixed 16-byte DMA roundtrip probe.
- Bit 3 (`$08`): caller-selected NTSC/PAL DMA timing through `$DFFE`.

Full picture DMA therefore requires mask `$09`, decode-only requires `$02`, and
the DMA probe requires `$0c`. The matching C64 runtime normalizes the KERNAL
`$02A6` byte: its zero/NTSC value becomes `1` at `$DFFE`, while its nonzero/PAL
value becomes `0`.

Commands written to `$DFF6`:

- `$00`: acknowledge a result and return to ready.
- `$01`: decode the three streams and DMA them to C64 memory.
- `$02`: decode all three streams into Teensy workspace without DMA.
- `$03`: perform the fixed 16-byte DMA roundtrip probe described below.
- `$7f`: reset and relock the mailbox, restoring stock IO2 behavior.

Status values read from `$DFF7`:

- `$00`: ready.
- `$01`: decoding.
- `$02`: DMA transfer.
- `$80`: full picture DMA complete.
- `$81`: decode-only complete; C64 memory was not modified by DMA.
- `$82`: 16-byte DMA roundtrip complete.
- `$e0-$ff`: error; read `$DFFB` and use the C64 fallback. The low five
  status bits also carry the error detail.

Error values are: `1` locked, `2` busy, `3` bad command, `4` invalid source,
`5` source is tagged for SD swapping, `6` malformed RLE, `7` no safe workspace,
`8` DMA was already in use, `9` the DMA probe read did not match its seed, and
`10` a DMA state or PHI2-edge deadline expired after emergency bus release.

## Staged hardware diagnostics

Command `$02` performs the identical bounded three-plane decode used by full
acceleration, returns `$81`, and never examines `DMA_State` or calls
`PerformDMA`. The matching stage-1 C64 cartridge deliberately uses its normal
C64 decoder afterward, so a correct visible picture remains the authority.

Command `$03` limits the first bus-master test to `$0400-$040f`. Before issuing
it, the C64 saves those 16 bytes and writes this seed with its own CPU:

```
A5 A4 A7 A6 A1 A0 A3 A2 AD AC AF AE A9 A8 AB AA
```

For offsets `x = 0..15`, the seed is `x EOR $a5`. The Teensy DMA-reads and
verifies it. A mismatch returns error 9 without a DMA write. On a match, the
Teensy XORs every byte with `$ff` and DMA-writes `x EOR $5a` back. The C64
verifies that complement and restores all original bytes on every exit. The
diagnostic then deliberately uses normal C64 picture decoding. Only after both
stages pass should command `$01` be tested.

Before commands `$01` and `$03` start DMA, firmware applies the selected video
standard to the upstream timing globals. NTSC selects
`Def_nS_DMASetupNTSC`/`Def_nS_MaxAdjNTSC` (430/993 ns in the pinned source); PAL
selects `Def_nS_DMASetupPAL`/`Def_nS_MaxAdjPAL` (440/1030 ns). This is required
because MinimalBoot otherwise begins with PAL defaults, and the upstream source
documents write errors when 440 ns is used on NTSC hardware.

The low-level transfer has two independent fail-safe deadlines. Main-context
waits for DMA assertion, completion, and release are capped at 250 ms. Every
PHI2 edge wait inside the ISR is capped at 100 us. Expiry returns address, data,
and R/W pins to input, deasserts `/DMA`, and publishes `$ea` before the C64 is
allowed to enter its canonical fallback.

## Memory behavior

No permanent 10 KB buffer is added. The handler borrows two swap-cache blocks
that are not the current ROML or ROMH image, invalidates their cache tags, and
uses them for the 8000-byte bitmap plus the 2000-byte screen/colour tail. Picture
sources are required to be direct, so these cache blocks cannot contain source
data. This keeps the resident CRT allocation unchanged. The tradeoff is that up
to two unrelated cached overflow half-banks may need to be reloaded from SD
later.

## Build

Use the repository's pinned Teensyduino 1.61.0 toolchain and the Fab04 feature
build:

```powershell
cd Source\Teensy\tools
.\Build-DualBoot.ps1 -Fab04_Features
```

The feature macro is `FeatAGIPictureDMA` in
`MinimalBoot/Min_TeensyROM.h`. Arduino cannot import a parent sketch's `.ino`
tab, so `Common/DMAControl_Minimal.h` contains the same low-level routine as
`DMAControl.ino`; keep the two synchronized when bus timing changes. The PHI2
ISR enables `DMATransferISR` only for this feature and DMA-capable v0.4 hardware.

Before distributing a firmware image, validate on real v0.4 hardware with the
three staged CRTs in order: decode-only with intentional C64 fallback, the
16-byte read/verify/write/restore probe, then full picture DMA. Also cover
handshake and reset compatibility, pictures that cross 8 KB and 16 KB
boundaries, malformed/truncated input fallback, rejection just beyond the
24-bank physical prefix, tagged-source rejection, all 73 SQ1 pictures, PAL and
NTSC timing selection, and repeated room changes that force ordinary swap-cache
refills.

The host conformance model covers the three commands, zero-DMA decode-only,
the exact 16-byte DMA roundtrip, the three full DMA destinations, NTSC/PAL
timing selection, raw-bank walking, both C64-RLE forms, exact plane bounds, the
24-bank ceiling, bank-boundary crossing, tagged-source rejection, and the main
fallback errors:

```powershell
node Source\Teensy\MinimalBoot\tests\agi-picture-conformance.mjs
```

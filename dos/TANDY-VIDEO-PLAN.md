# Tandy 16-color video: tier 1 implementation plan

## Goal and compatibility boundary

Add BIOS modes `08h` (160x200x16) and `09h` (320x200x16) to the native DOSVM and project them through the existing MPE5/VIC-II cell protocol. Tier 1 provides one physical 32 KiB video page at guest physical addresses `B8000h-BFFFFh`.

## Current DOSVM baseline

Writable-storage and game-compatibility work are complete rather than part of
this tier: `C:` is a writable 20 MiB FAT16 image, `D:` is the writable
`/DOSVM/D/` SD-card folder, and Boulder plus Might and Magic have been
confirmed on physical hardware. See [the drive guide](STORAGE.md) and
[hardware record](HARDWARE-TEST.md). Tandy graphics and its three-tone PSG
path are now implemented in source and covered by focused host regressions;
they still need the physical Teensy/C64 acceptance gate.

The 512 KiB RAM2 allocation remains entirely conventional guest RAM. Video memory stays in the existing RAM1 high-memory aperture and in the renderer's private mirror. Firmware is still loaded from flash at boot; this work adds no RAM2 firmware copy and no flash-as-RAM dependency.

This is a useful Tandy graphics subset, not full Tandy memory paging:

- CPU and CRT page fields are latched and can be read back, but every selection aliases the sole `B8000h-BFFFFh` page.
- The BDA active page remains zero. There is no second backing page, page flipping, `3DDh` extended page support, or alias into low conventional memory.
- Direct-register software can use the documented Tandy mode, palette, mask, and CRTC controls below. PCjr register and shared-memory compatibility are outside tier 1.
- A game must select Tandy graphics explicitly or use the supported BIOS calls. Automatic Tandy machine identification is deferred until a target requires a narrowly tested compatibility signature.
- Tandy's three tone generators at port `C0h` are mapped to SID voices 1-3.
  This is packet-rate control data, not sampled audio; the PSG noise channel
  and PCM-style effects remain deferred.

Complete the direct RAM2 conventional-memory change before starting this feature, then preserve that memory layout as the baseline.

## Guest-visible video model

### Frame-buffer layout

Both modes use packed 4-bit logical color indexes, with the left/even pixel in the high nibble.

| BIOS mode | Image | Visible bytes | Address calculation |
| --- | --- | ---: | --- |
| `08h` | 160x200x16 | 16,000 | `(y & 1) * 2000h + (y >> 1) * 80 + (x >> 1)` |
| `09h` | 320x200x16 | 32,000 | `(y & 3) * 2000h + (y >> 2) * 160 + (x >> 1)` |

Mode `08h` uses two 8 KiB banks. Its last visible byte is `3F3Fh`; `1F40h-1FFFh` and `3F40h-3FFFh` are padding. Mode `09h` uses four 8 KiB banks. Its last visible byte is `7F3Fh`, with 192 padding bytes at the end of each bank.

CRTC registers `0Ch/0Dh` hold a word address. Convert it to a byte start and wrap it independently inside each 8 KiB bank:

```text
local = (startAddress * 2 + rowByteOffset + byteWithinRow) & 1FFFh
physical = bank * 2000h + local
```

The mirror and write observer must cover all 32 KiB. Writes in padding update the mirror but do not dirty a visible cell. Byte, word, `REP`, and writes crossing the old 16 KiB boundary must produce the same result.

### Port and register behavior

Add explicit video-port hooks in the actual 8086 `IN` and `OUT` handlers. Do not derive Tandy state from generic writes to the emulated port-latch area. The inherited core changes the `3DAh` latch to synthesize CGA status during `IN`; only real guest `OUT` instructions may change the Tandy video-array index.

The output hook must handle:

- `3D4h/3D5h`: CRTC index/data, including display start registers `0Ch/0Dh`.
- `3D8h`: CGA-compatible mode control and display enable. BIOS mode `08h` normally writes `0Ah`; mode `09h` writes `0Bh`. The Tandy video-array mode register selects 16-color operation; `3D8h` bit 4 remains clear.
- `3D9h`: retain existing CGA color-select behavior outside Tandy modes.
- `3DAh`: Tandy video-array address on `OUT`; retain the existing synthesized status on `IN`.
- `3DEh`: Tandy video-array data, using the most recent `3DAh` index.
- `3DFh`: bits 0-2 CRT page, bits 3-5 CPU page, and bits 6-7 video-address mode. Address mode `01b` selects the two-bank layout and `11b` the four-bank layout.

An actual `IN 3DAh` returns the existing synthesized status without changing the Tandy video-array index. Other `IN` instructions must not change video-array state.

Latch these video-array registers:

| Index | Meaning in tier 1 |
| --- | --- |
| `01h` | Palette mask applied to each logical pixel index |
| `02h` | Border color, retained for readback but not independently projected |
| `03h` | Tandy graphics mode control, including bit 4 for 16-color operation |
| `10h-1Fh` | Sixteen 4-bit RGBI palette entries |

Resolve the overlap with CGA mode control without breaking existing modes 4-6. Tandy interpretation is active only when the BDA mode is `08h/09h`, or after a valid `3DFh` two-bank/four-bank address-mode write has armed the Tandy register set. Otherwise the current CGA decoder remains authoritative, including `3D8h=0Ah` for CGA modes 4/5 and `3D8h=1Ah` for CGA mode 6.

For the single-page implementation, record all `3DFh` fields and mirror the byte at BDA `40:8A`, but do not offset CPU reads/writes or renderer reads by the page fields. A page-field change starts a complete renderer traversal so state transitions are deterministic even though storage aliases.

### BIOS behavior

The file `engine/native-dos/vendor/8086tiny/bios` is the ROM BIOS used by the DOSVM; the FreeDOS kernel itself does not need changes. Track its assembly source beside the binary and add a deterministic build helper before changing the binary, so the 7,665-byte current artifact remains reproducible.

Extend INT `10h` as follows:

- `AH=00h`: accept modes `08h` and `09h`, preserve the bit-7 no-clear request, program `3D8h`, the video-array registers, and `3DFh`, then clear 16 KiB or 32 KiB when requested.
- Update BDA `40:49` (mode), `40:4A` (columns: 20 for mode 8, 40 for mode 9), `40:4C` (page size: `4000h`/`8000h`), `40:4E` (start zero), `40:62` (active page zero), `40:65` (mode-control mirror), and `40:8A` (page register).
- `AH=0Fh`: return the current mode, corresponding column count, and page zero.
- `AH=05h`, Tandy subfunctions `AL=80h-83h`: set/read the CPU and CRT page latches. Tier 1 retains the requested values while all pages alias the sole backing page.
- `AH=10h`: implement `AL=00h` individual palette entry, `AL=01h` border, and `AL=02h` table operations for sixteen palette entries plus the border color. The palette mask remains video-array register `01h`.

BIOS character drawing and pixel primitives in these graphics modes may be deferred unless the first target program calls them. Direct frame-buffer drawing, correct mode setup, BDA state, palette services, and readback are required. The FreeDOS image builder only needs an optional diagnostic `.COM`; it needs no kernel patch.

## VIC-II projection

Keep the current wire record unchanged: one cell number, eight bitmap bytes, one screen byte, and one color byte. A frame still consists of 1,000 cells. Palette, mask, start-address, mode, page-latch, or display-enable changes begin a unique full-screen replacement.

Map colors in this order:

1. Apply the palette mask to the 4-bit logical pixel index.
2. Read the corresponding programmable RGBI palette entry.
3. Convert RGBI to the existing nearest VIC-II color table.

Mode `08h` uses VIC-II multicolor bitmap mode. Its 160x200 spatial resolution is exact: each 4x8 source-pixel cell becomes one VIC-II bitmap cell. VIC-II supplies the global `D021` color plus three per-cell colors, so choose mapped palette entry zero as the global background and the three most frequent remaining colors in each cell. Assign the local slots in descending frequency, then ascending VIC-II color number. Map overflow colors to the nearest selected slot. A cell is color-exact when it uses no more than three colors besides the background.

Mode `09h` uses VIC-II hires bitmap mode to preserve all 320x200 pixel positions. Choose the two most frequent mapped colors in each 8x8 cell, ordered by descending frequency and then ascending color number. Put the first in the screen-byte low nibble/bitmap value zero and the second in the high nibble/bitmap value one. Map overflow colors to the nearer selected VIC-II color. A cell is color-exact only when it contains at most two colors. A later optional projection may downsample mode 9 to 160 pixels for four colors per cell; it is not part of tier 1.

For both projections, define “nearest” with a compile-time 16x16 ranking table derived from squared RGB distance between the fixed VIC-II palette values; lower VIC-II color number wins a distance tie. Commit the table with golden tests so compiler math and platform integer widths cannot change output.

The current receiver has one global bitmap-mode flag, so mixed hires/multicolor cells, raster tricks, and exact 16-color display are unavailable. Do not add FLI or raster IRQ color switching. Latch the Tandy border register for guest state, set `D021` from mapped palette entry zero, and leave `D020` stable; independent border transmission would require a protocol change.

Dirty-cell coalescing remains mandatory. Rendering a changed source byte should mark only the affected VIC-II cell, except for state changes that require the full replacement described above.

## Tandy sound projection

Writes to the Tandy SN76496-compatible PSG at `C0h` latch the three tone
periods and four-bit attenuations. When any Tandy tone is audible, DOSVM
uses the three SID voices for those tones; PC-speaker synthesis remains the
voice-one fallback while the PSG is silent. The tone formula is preserved
without rounding through whole hertz:

```text
SID register = round((3,579,545 * 2^24) / (32 * PSG period * SID clock))
```

Muting a PSG voice clears only its SID voice. A newly audible tone or a
period change retriggers that voice's envelope; held tones do not retrigger
on every display packet. The existing 26-byte SID payload and 27-byte
frame-end packet stay unchanged.

## Files to change during implementation

| File | Planned change |
| --- | --- |
| `engine/native-dos/mpe5_video.h/.cpp` | Expand the mirror to 32 KiB; extend video state; add mode 8/9 address decoding, palette mapping, deterministic cell quantization, and dirty-cell mapping. |
| `engine/native-dos/mpe5_8086tiny.cpp/.h` | Observe the full aperture, mirror BDA state, own the Tandy register state, and expose explicit input/output video-port hooks. |
| `engine/native-dos/vendor/8086tiny/8086tiny.c` | Call the hooks only from real `IN`/`OUT` opcode paths; keep synthesized `3DAh` status behavior isolated. |
| `engine/native-dos/vendor/8086tiny/bios` | Rebuild with the INT `10h` behavior above. |
| `engine/native-dos/vendor/8086tiny/bios.asm` | Add the tracked BIOS source matching the rebuilt binary. |
| `dos/tools/build_8086tiny_bios.ps1` | Add a deterministic BIOS build and size/hash report. |
| `engine/native-dos/mpe5_firmware.h` | Update workspace assertions and mode integration only; retain packet format and buffering. |
| `engine/native-dos/mpe5_platform.*`, `mpe5_speaker.*` | Latch port `C0h` PSG writes and project its three tones to the three SID voices. |
| `dos/tests/mpe5_video_test.cpp` | Add mode layout, projection, palette, CRTC, and dirty-cell golden tests. |
| `dos/tests/mpe5_vm_host_test.cpp` | Add real-opcode Tandy port-hook, `3DAh` index isolation, CGA disambiguation, 32 KiB write, and BIOS fixture checks. |
| `dos/tests/mpe5_firmware_host_test.cpp` | Add workspace, backpressure, replacement, and CPU-resume immutability checks. |
| `dos/tests/mpe5_c64_wire_test.mjs` | Add executable 6510 receiver scenarios for `tandy8` and `tandy9`. |
| `dos/tests/fixtures/tandy_video.asm` | Add the source for the deterministic guest diagnostic assembled into `TGA16.COM`. |
| `dos/tools/build_freedos_boulder_image.py` | Optionally include `TGA16.COM` in the generated test image without changing FreeDOS. |

`mpe5_direct_memory.*` should not change for tier 1: its existing `B8000h-BFFFFh` high aperture is the required physical page. The C64 terminal receiver should need no production change because both global display modes and the 12-byte cell record already exist.

## Memory, code, and transport budget

| Item | Current | Tier 1 | Delta |
| --- | ---: | ---: | ---: |
| Video mirror | 16,384 B | 32,768 B | +16,384 B RAM1 |
| `CgaVideo` workspace | 26,509 B | 42,893 B | +16,384 B RAM1 |
| Total DOS workspace | 168,736 B | 185,120 B | +16,384 B RAM1 |
| Runtime tail after resident CRT chips | 216 KiB | 216 KiB | 0 B |
| Headroom in that runtime tail | 52,448 B | 36,064 B | -16,384 B |
| RAM2 use by video | 0 B | 0 B | 0 B |

The current cartridge occupies three 8 KiB resident chips in the 240 KiB
`RAM_Image`, so the runtime tail is 216 KiB. The rebuilt BIOS is 8,101 bytes;
with its 16-byte native header it occupies 8,117 bytes and leaves 75 bytes in
the existing 8 KiB chip. The measured cartridge therefore retains the
three-chip layout and leaves 36,064 bytes after the planned workspace. Register
state needs only tens of bytes. A two-page 64 KiB mirror would leave only 3,296
bytes of runtime headroom; a 128 KiB mirror is further beyond the budget, which
is why page flipping is deferred.

The packet buffer and record size do not change. A complete 1,000-cell replacement takes 53 cell packets plus one 27-byte frame-end payload: 12,567 wire bytes including current headers and CRCs. Repeating that at 60 Hz would require about 754,020 B/s. The C64 receiver also spends at least about 115,000 cycles on the bitmap copies alone, before transport, CRC, screen, and color work. Tier 1 therefore targets correct initial/full state plus sparse dirty updates; it does not promise full-screen animation at 60 Hz.

Record DWT cycle counts for conversion and actual ACK-limited cell/frame rates on hardware. Teensy-side conversion should fit in low single-digit milliseconds on an 816 MHz Teensy 4.1, but transport and the roughly 1 MHz C64 receiver are the expected limits.

## Required validation

### Pure renderer tests

- Mode 8: first/last visible bytes, both banks, both padding ranges, per-nibble pixels, CRTC start, and independent 8 KiB wrap.
- Mode 9: all four banks, every bank's padding, `3FFFh/4000h` crossing, CRTC start, and independent wrap.
- Exact cells at the VIC-II color limits and deterministic frequency/tie/distance results above those limits.
- Palette entry, mask, display enable, and start-address changes each force exactly one 1,000-unique-cell replacement.
- After initial completion, unchanged output is suppressed and a source write dirties only the affected cell.

### CPU and register tests

- Execute real Tandy `OUT 3DAh/3DEh` sequences, including register `03h` bit 4.
- Verify `IN 3DAh` and unrelated `IN` status synthesis cannot mutate the Tandy graphics-array index or data.
- Prove `3D8h=0Ah/1Ah` leaves existing CGA modes 4-6 intact until Tandy state is armed.
- Verify `3DFh` and BDA page fields latch/read back while all selections address the same 32 KiB page.
- Cover byte, word, `REP`, and cross-span writes through `BFFFFh`; renderer/state queries must cause no guest-cache or SD reads.

### BIOS guest fixture

Add a tiny deterministic `.COM` that invokes INT `10h` for modes 8 and 9 and records/asserts BDA bytes, port values, clear versus no-clear behavior, `AH=0Fh`, page-latch calls, and palette calls. Optionally place it in the generated FreeDOS image; do not modify FreeDOS itself.

### Firmware and C64 wire tests

- Retain the existing 19-record packet maximum and prove packet data stays immutable while the CPU resumes.
- Verify complete replacements contain every cell exactly once and retain dirty cells across backpressure.
- Add `tandy8` and `tandy9` scenarios to the 6510 receiver test. Check `D016` hires/multicolor selection, `D011/D018`, bitmap bytes, screen/color bytes, frame ACK, and replacement visibility.
- Run all existing text, CGA 4/5/6, Boulder, keyboard, speaker, direct-memory, and RAM2 layout regressions, including real `OUT C0h` three-voice PSG coverage.

Host tests and emulators are not physical acceptance. The final gate is a Teensy 4.1 plus C64 run of the diagnostic and first target program, with captured DWT conversion timing, ACK-limited update rate, correct mode/palette changes, and no RAM2 or CGA regression.

## Ordered milestones

1. **Lock the memory baseline.** Finish and validate the direct 512 KiB RAM2 conventional-memory mapping. Record the workspace/map report and existing CGA test results.
2. **Freeze expected output.** Add failing mode-layout, quantization, palette, paging-alias, and transport fixtures with fixed golden records.
3. **Separate real port I/O.** Add the explicit `IN`/`OUT` hooks and Tandy state machine; prove synthetic status latches cannot enter it.
4. **Add renderer support.** Expand the mirror, implement bank addressing and VIC-II projections, then pass the pure renderer and dirty-cell tests.
5. **Make the BIOS reproducible.** Track the matching assembly/build helper, implement INT `10h`, rebuild the ROM, and pass the guest fixture.
6. **Integrate without a wire change.** Update workspace checks, run firmware/6510 wire scenarios, and prove the full existing host regression suite.
7. **Pass the hardware gate.** Flash the firmware, run the diagnostic and target software on the Teensy/C64 pair, measure conversion and transport, and record the compatibility limits seen in practice.

Tier 1 is complete when modes 8 and 9 work through BIOS and direct ports, the guest sees the documented 32 KiB layout and register/BDA state, VIC-II output is stable within the stated per-cell color limits, the package-derived runtime-tail guard passes with at least the budgeted 208 KiB, all prior DOSVM modes still pass, and the physical hardware gate succeeds.

## Primary references

- [Tandy 1000 Service Manual](https://ftp.oldskool.org/pub/drivers/Tandy/1000/Tandy_1000_Service_Manual.pdf)
- [IBM PS/2 and PC BIOS Interface Technical Reference, April 1987](https://www.ardent-tool.com/docs/pdf/PS2_and_PC_BIOS_Interface_Technical_Reference_Apr87.pdf)

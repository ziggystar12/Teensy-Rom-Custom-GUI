# V1.1.6: GBVM and enhanced-video cadence candidate

Source base: `6489aaa` on main (GBVM/F5 source `7a76c73`). Firmware was built
from an isolated archive of these commits, not the unrelated uncommitted
RAM2/Doom work in the shared checkout. Those edits are preserved separately.
That parallel work subsequently landed as `6009933`, before the download
commit. **This V1.1.6 HEX does not include its RAM2_RO96 loader and cannot run
the new DoomVM profile.** A combined firmware needs a new version/build; the
Doom task has been explicitly notified. Main source is newer than this HEX.

## Changes

- Enhanced video retains exact destination-bank images in a 36 KiB optional
  workspace. Bitmap/screen bytes that already match are skipped. Raster code
  is resent only when that bank's split plan changes. Atomic bank flips and
  border grants remain mandatory. Older 24 KiB modules retain full uploads.
- NESVM lends the larger workspace. Core timing and F1/F7 paths are unchanged.
- Generic firmware geometry flags support native-height centering and wide
  pixels. GB default is 320 physical pixels by 144 rows, centered at y=28.
  All four original-GB shades survive the multicolor conversion.
- A generic comma-separated extension field routes both `gb,gbc` to GBVM.
  Protected native extensions remain rejected; duplicate ownership is fatal.
- Standalone MCUME gnuboy module; ROM-only 00/MBC1-01/MBC5-19, up to 512 KiB.
  No battery RAM/RTC/save-state support yet. Unsupported profiles are rejected.
  GBC palette changes within a frame are preserved up to 256 source colors;
  excess unique colors use nearest source-palette entries before firmware
  reduction. SID is a three-voice approximation of the four GB channels.

## Evidence (not physical acceptance)

- Matched firmware/NES/DOS suite: pass. Includes image CRC/bounds, both PAL/NTSC
  raster plans, inactive-bank switching, NES picker and scheduling, DOS Tandy,
  file operations, GUI checks and actual C64 client reset/START in VICE.
- Delta test: 12,775 bytes initial enhanced upload, zero picture bytes for
  repeated frames, 24 bytes for the synthetic moving 8x8 patch. This is not a
  bandwidth estimate for scrolling scenes and not a promise of 60 fps.
- Four-shade native-height geometry verified through actual firmware DMA
  representation: 28 black rows above and below; every source pixel retained.
- GB module integration: picker Down/Up, Return/Fire, CGB/DMG launch, return and
  relaunch, frozen Busy data, maintained emulated-time rate, no runtime file I/O.
- User-provided MARIO1.GB and Pac-Man.gbc ran 12 emulated seconds each with
  input; native frames were visually inspected. Both are excluded from Git
  and packages. GB launcher booted in both PAL and NTSC VICE.
- GB ARM: 67,824 code, 2,048 data, 148,416 BSS bytes. No unresolved imports or
  dynamic constructors. 46,144 bytes remain inside the module's RAM1 region.
  Host stack remains separately reserved at 49,152 bytes; host RAM2 globals 0.
- GB and NES ZIPs reopened and every member hashed; each included SOURCE
  independently relinked to exactly the packaged module.
- Optional undefined-behavior sanitizer unavailable in the installed MinGW
  toolchain (`-lubsan` missing); no sanitizer pass is claimed.

## Downloads

| File | SHA-256 |
| --- | --- |
| MPE_Firmware-V1.1.6.hex | 06582ea5ca7e8ebaadcc3efa6c066655cb0e14f5742627ef5c4c0a02ebaba434 |
| NESVM.zip | 511998c42c3d5c8eb3140b2c395e3b7c1983e85645a0921476b62f39fc5e7a41 |
| GBVM.zip | 3ccafaf6d241d144d2b26162e4f3e33563469d9ec4d6c30142e3ef1887793dce |

The existing DOSVM and AGIVM downloads and V1.1.5 HEX were not replaced.
GBVM.zip includes the GPL license and complete corresponding sources, no games.

## Hardware handoff

Install V1.1.6 and replace NESVM; preserve existing ROMs. Compare Mario in F1,
F5, F7: music/game time should remain full speed, F5 should transfer less on
small changes, and steady-state black flashing must not return. Dense scrolling
can still reduce F5 picture cadence. F5/F7 retain NES horizontal centering.

Extract GBVM.zip to the SD root; put the user's ROMs in `/VMS/GBVM/ROMS` and
launch GBVM.crt. Check the picker with keyboard and port-2 joystick, Mario's
four shades/28-row margins, Pac-Man color and sound, then Start+Select return.
F1 is the intended GB default. F3/F5 retain the C64 enhanced-mode left-edge
artifact; F7 trades colors for hires and is not necessarily better for GB.
Physical speed, audible output and bus reliability remain the user's test.

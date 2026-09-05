# GBADoom E1M1 VM backend

This local candidate ports pinned GBADoom to the generic ABI-2 VM host with
1 MiB internal RAM and three synthesized SID effect voices. It supports only
E1M1 at medium difficulty on initial launch. See [measured status](../../../doom/GBADOOM-STATUS.md).

## Reproduce

From the repository root:

```powershell
node scripts/audit-gbadoomvm.mjs --wad <path-to-user-supplied-DOOM1.WAD> --require-fit
$env:MPE_VM_TEST_OUT='build/gba'
node scripts/build-vm-test.mjs all
node scripts/build-doomvm.mjs build/gba
node scripts/verify-doomvm.mjs build/gba
```

Use the short build/gba directory: the Windows ARM toolchain's internally
expanded include paths can exceed its path limit with longer build roots.
The audit requires the existing Teensy ARM toolchain, MinGW32 at
C:/msys64/mingw32/bin and a clean checkout at build/doom/upstream/GBADoom of
commit 89097b3ff31ac1e1b2cdce9854e49726cfa462bf, including GbaWadUtil, its Qt DLL
and supplemental WAD. It downloads nothing and leaves the supplied WAD intact.

The audit writes engine.mvm, adapted sources and report.json under
build/doom/gbadoom-audit. The local kit is build/doom/e1m1-test/SD, containing
matching test firmware, DOOMVM.crt and /VMS/DOOMVM. No script flashes hardware.
Launch the client or doom1.gbd through the GUI. An empty client content path
selects /VMS/DOOMVM/doom1.gbd. Reset returns to the GUI.

The content envelope is GBDWAD1 followed by a zero byte, little-endian payload
length and CRC32, then GbaWadUtil's converted IWAD. The backend checks the entire
file through a 512-byte buffer before startup. It rejects raw WAD files.
Unused later-level data may remain on SD; it is never loaded as a later level.
Both exits and new-game requests stay on E1M1. There is no intermission.

--arm-only stages and links without game data. --require-fit enforces measured
startup and strict-link gates. The oversized measurement-only ELF is diagnostic;
engine.mvm is packed only from the strict profile ELF. Local kit generation
checks source and artifact hashes against the completed audit and host build.

## Adapter

- core.c supplies startup and one-tic calls, held-key transitions and sound updates.
- platform.cpp supplies bounded allocation, files, recovery, framebuffer and palette.
- w_wad.c keeps directory/cache metadata in RAM1 and payloads in the RAM2 zone.
  Persistent pointers remain pinned; frame resources become purgeable after rendering.
  Texture definitions hold lump IDs instead of cartridge pointers.
- module.ld places executable code in ITCM, selected fixed tables in spare RAM1
  and remaining constants above the 416 KiB RAM2 guest arena.
- doomvm.cpp publishes 240 x 160 indexed pixels (120 x 160 logical horizontal
  detail), palette and 26-byte SID frame end. Output remains frozen until ACK.
- sound.c synthesizes three bounded voices directly from Doom sound events,
  using the existing C64 SID receiver without sample buffers or a Tandy emulator.

The source adapter also corrects upstream font/status pointer declarations,
previous-state signedness, active platform/ceiling backlinks and sound-channel
expiry. Screen wipes are disabled to keep frame ownership cooperative.

## Demo controls and limits

| Input | Action |
| --- | --- |
| W/S or cursor up/down | Move |
| Cursor left/right | Turn |
| A/D | Strafe |
| Control or joystick fire | Fire |
| Space/Return | GBA use/run and menu accept |
| Tab | Automap |
| Escape | Menu |
| Joystick directions | Move/turn |

The DOS-style snapshot carries one ordinary key plus held modifiers and joystick
bits. Movement/fire and release are tested; a full multi-key keyboard matrix,
independent run/use and dedicated weapon keys remain outside this candidate.
SID effects approximate shots, doors, pickups and monsters. Music, original
sample playback and persistent saves remain unimplemented. Physical keyboard
chords/ghosting, display aspect, SD latency, sound and speed need hardware tests.

The adaptations carry GPL-2.0-or-later notices. Upstream embedded and supplemental
asset provenance is separate from the user's WAD; this kit is for local testing.
See [licensing notes](../../../doom/LICENSE.md) before any distribution.

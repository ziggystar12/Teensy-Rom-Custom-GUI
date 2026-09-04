# DOSVM hardware test

The user has confirmed DOSVM working on physical TeensyROM hardware. This
checklist targets the current V1.0.19 firmware and internal R23 cartridge
revision from `DOSVM/`, including the optional sharp CGA renderer; physical
confirmation of the V1.0.19 firmware/GUI pair remains open. V1.0.15 booted
working FreeDOS and Might and Magic on the user's hardware; its automatic
firmware update also worked. A physical pass for the new sharp renderer has
not yet been recorded. Physical R20 and R21 cold-start failures are recorded
below. The exact V1.0.17/R22 pair subsequently booted and ran Boulder and
Might and Magic correctly. R23 changes only the displayed DOS backslash;
physical confirmation of its `C:\>` glyph remains. Optional PSRAM is not required.

Use the distribution's `DOSVM/SHA256SUMS.txt` or
[SHA256SUMS.txt](SHA256SUMS.txt) for published files. The package contains the
matching firmware and CRT, a fresh C: image, and D: startup-update files.
Keep existing C: and D: data when upgrading; follow
[the storage upgrade steps](STORAGE.md#upgrading-dosvm). Earlier release kits
remain unchanged.

## R20/R21 cold-start failures and R22 correction

On 2026-09-04, the physical R20 receiver stopped before accepting a packet:

- Stage `03`, terminal error `02`: **NO PACKET - SERVICE OR LINK STALLED**.
- Packet count `0000`.
- Control bytes `4D 33 54 50 04 02 00 01`: command `04`, running status `02`,
  ACK `00`, and commit `01`.

Commit `01` proves the firmware had published the first packet. R20 could take
an exceptional read-recovery branch for that packet, request command `04`, and
then wait for two increments of the terminal's raster-frame counter. That
counter is enabled only after the initial bitmap is complete, so the bootstrap
path could wait until its bounded timeout instead of rereading packet 1.

R21 avoided that inactive counter during bootstrap and used bounded immediate
rereads. Its physical test also stopped before accepting packet 1:

- Stage `03`, terminal error `0C`: **UNSTABLE PACKET COMMIT**.
- Packet count `0000` and ACK `00`.

Those immediate attempts completed while IO2 was still unsettled. R22 requests
the firmware quiet window for bootstrap failures too. After quiet status `12`,
it counts two frames from live VIC raster register `$D012`, which is available
before raster interrupts start, and then rereads the unchanged packet.

An executable 6510 regression holds the first packet corrupt until it observes
the quiet request. R21 fails with no quiet request and no accepted packet; R22
passes. The normal, later transient, persistent-corruption, dropped-request,
missing-response, and firmware-error cases also pass.

## R16 observation and R17 correction

On 2026-09-03 the user reported that R16 Boulder was working well, then stopped
about ten seconds into play with:

- Stage `05`, terminal error `0C`: **UNSTABLE PACKET COMMIT**.
- Packet count `042F` (1,071 accepted packets).
- Signature readback `45 3B 5C 58`, versus expected `4D 33 54 50`.

Each fixed signature byte differs by XOR `08`. That supports corrupted reads
on the cartridge path; the photo does not prove that the publisher mutated a
pending packet. It also does not indicate that DOS ran out of guest RAM.

R17 introduced a quiet retry with command `04` after a failed packet read;
R22 uses it during bootstrap and after the initial display is live. Firmware
finishes the active VM slice, pauses foreground VM execution and publishes
status `12` when the same pending packet is available for rereading. The R22
receiver then measures two C64 frames from the live VIC raster before rereading.
A matching packet ACK resumes normal execution. Packet CRC validation and
bounded retry limits still reject persistent errors. The normal successful
path retains R16's direct-memory optimizations and control handling.

R16's interleaved host A/B tests measured 1.86x faster boot and 1.96x faster
`DIR` than R15 for identical guest work. R22 retains those changes; no new
physical speed ratio or stability result is claimed. Quick Shift/cursor taps
remain visible to the guest for at least 550,000 instructions, while ordinary
printable transitions keep the 512-instruction cadence.

V1.0.13 removed separate SD `mediaPresent()`/CMD13 probes from GUI firmware
fingerprinting; V1.0.19 retains that fix. A transient status-command failure
could disturb the active SDIO stream even when file reads were otherwise
working. The new path retains file identity, size, clean EOF, cancellation
and CRC verification. Host tests
exercise complete firmware files and a simulated status-command failure. The
user subsequently confirmed automatic firmware updating worked with V1.0.15.

## Check the matching hardware kit

1. Confirm V1.0.19 in About. Flash
   `DOSVM/firmware/MPE_Firmware-V1.0.19.hex` only if an older version is shown.
   If the older GUI says "Firmware selection changed," use the working **V**
   classic text updater once.
2. For a fresh installation, copy `DOSVM/sd-card/` contents to the SD root.
   To update R20 or R21, preserve the existing image and D: folder and replace only
   `/DOSVM.CRT`. Older kits also need the startup files described in
   [STORAGE.md](STORAGE.md#upgrading-dosvm).
3. Launch `DOSVM.CRT`. The startup page holds `Mean Hamster BIOS (C) 2026`,
   `Memory Test: 512K OK` and `Booting drive C:` with a short POST beep before
   FreeDOS reaches its prompt.
4. At `C:\>`, try `DIR`, `VER`, `DIR D:\` and `MEM`. Exercise Return, Backspace,
   quick key taps and repeated letters. DOS text is black-and-white 80x25;
   verify a full `DIR` remains readable and the underline cursor blinks at an
   idle `C:\>` prompt.
5. In an unused test folder, create a D: save and copy it to C: using the
   commands below. Wait for `C:\>` after the copy, reset to the launcher, and
   relaunch DOS. `TYPE` both files: each must still show `R22 SAVE OK`.
6. Run `PCTONE`: expect a SID tone, silence, and the DOS prompt.
7. Run `BOULDER`. Press **Space to skip the intro, then Shift to start**.
   Move repeatedly in all directions for several minutes, beyond the reported
   ten-second failure point. Check that quick taps and releases register,
   scrolling follows the player without stale rows or a repeatedly redrawn
   field, and no transport diagnostic appears. Shift grabs; Space pauses.
   C64 Shift+cursor selects Up/Left.
8. If available, test port 2 joystick directions and fire (translated to cursor
   keys and Shift). Releasing a direction must stop the held key. Compare
   responsiveness with R16 rather than assuming the host speed ratio applies.
9. Leave DOS and confirm the Teensy resets to the launcher. Launch DOS again,
   reboot out, then cold-launch a previously working Sierra game with this
   exact firmware.
10. Automatic updating worked with V1.0.15. On future updates, use a known matching HEX file.
    Confirm that the unchanged file reaches the update flow. Cancelling the
    confirmation must still leave the firmware untouched.

Storage commands before resetting:

```dos
MD D:\R22TEST
ECHO R22 SAVE OK>D:\R22TEST\SAVE.TXT
COPY D:\R22TEST\SAVE.TXT C:\R22SAVE.TXT
```

After relaunching:

```dos
TYPE D:\R22TEST\SAVE.TXT
TYPE C:\R22SAVE.TXT
```

After a completed save and shutdown, also inspect `DOSVM/D/R22TEST/SAVE.TXT`
on your PC: it must be an ordinary file containing the same text. Copy a
small DOS program with an 8.3 filename into `DOSVM/D/` and confirm that DOS
can list and run it after the next launch.

C: is the writable 20 MiB FAT16 image; D: is the writable `/DOSVM/D/` folder.
`MEM`, `XCOPY`, `MORE` and `ATTRIB` are included on PATH. See
[STORAGE.md](STORAGE.md) for limits and more copy/rename/delete examples.
Successful writes and closes flush to SD, but resetting during a filesystem
operation can interrupt it. Keep backups and wait for operations to finish.

DOS uses all 512 KiB of RAM2 and cannot return to overwritten firmware state
without resetting. The BIOS reports 512 KiB; use `MEM` to inspect usable DOS
memory with the current startup configuration and resident folder driver.
Link gates require live DOS/MPE/SD state in RAM1 and at least 16 KiB of stack.
Host checks cover both-drive persistence across guest restart, D: executable
loading, directory operations, and save/seek/truncate/close behavior. These
checks do not replace physical storage and sustained-play acceptance.

## If a diagnostic appears

Record the revision/title, stage, error, packet count, all control bytes and
what was happening immediately before it stopped. `PACKETS 0000` means no
packet was accepted; a higher count proves progress but does not identify the
failure's cause.

Terminal error `0C` means packet validation did not recover within its bounded
retries. Error `03` reports an explicit firmware failure; error `02` is a wait
timeout. For an explicit firmware failure, include `CTRL FB` as well:

| CTRL FB | Meaning |
| --- | --- |
| `02` | Startup failure |
| `04` | Safe reset-only RAM1 workspace unavailable |
| `05` | BIOS/disk startup failure |
| `40` | CPU no longer ready without a captured reason |
| `41` | Guest reached `CS:IP 0000:0000` |
| `42` / `43` | Invalid guest read / write span |
| `44` / `45` | Memory read / write callback failed |

For runtime codes `40`–`45`, `F8/F9/FA` contain the guest address low/mid/high,
`FC/FD` guest CS, and `FE/FF` guest IP. These replace input fields after execution
fails. A runtime error is deferred until the pending packet is acknowledged.

## Hardware history

R15 was confirmed on 2026-09-03 to boot FreeDOS and start Boulder's first cave
after Space followed by Shift. It still felt slow. Its V1.0.12 pair remains in
the unchanged release history:

- Firmware SHA-256:
  `fd31dcc2d6dc84fddacaa6f18f2c12ef18a6113f58f672346c7d475e32ccf309`
- CRT SHA-256:
  `7438e8715f07c0dadf687f57989641cc98d23a96c29fb68579a07b95bacd10d1`
- Disk SHA-256:
  `9b92715061c496a05466ad29d9697a717287fb6b6eaec1c4b4a6f850426ce9d4`

R10 established a physical DOS prompt, but later failed after `VER`/`SETUP`.
R12 established CGA output but had control and performance failures. R13
worsened responsiveness; its instructions-per-packet figures were not elapsed
speed measurements. R14 fixed lost input and foreground scheduling while still
using SD paging. R15 removed paging. R16 improved gameplay but failed sustained
transport stability as documented above.

Builds, native VM tests, C64 CPU replay and VICE cover software behavior and
controlled fault injection. They do not establish physical bus timing or
replace a pass on the exact firmware/CRT pair.

The user subsequently confirmed DOSVM working. Boulder scrolling remained
an observed issue; R20 added that correction and the BIOS-style startup. The
R20 receiver then failed the cold-start case documented above; R21's immediate
bootstrap retries also failed on hardware. R22 corrected cold startup and was
physically accepted with Boulder and Might and Magic. R23 retains that path.

R22 retains the scrolling reproduction: move one cell down, then Right until
the cave scrolls.
The original path issued two hidden replacements during ten CRTC origin
changes. The corrected path passes 198 scrolling packets through the C64
replay without hiding the display. Confirm the same visible scrolling on
hardware using that route.

## V1.0.15 physical baseline and V1.0.16 sharp CGA checks

The user reports that V1.0.15 boots successfully and runs Might and Magic.
The Mean Hamster BIOS page appears, followed by a blank interval before DOS
finishes starting. This blank interval remains observed behavior; the report
is not a precise timing measurement. The user also confirmed that automatic
firmware updating worked with V1.0.15.

The Might and Magic photo shows thick, merged strokes in graphics-mode text.
The default renderer reduces CGA mode 4/5 from 320 pixels to 160 logical
multicolour pixels across. V1.0.16 offers a general sharp display option;
it changes no guest program or guest video mode.

1. Keep the current R19 CRT and drives; install only the V1.0.16 firmware.
2. In a CGA application, press **Ctrl+Commodore+F7** once. Fine pixel gaps and
   narrow strokes should be visible in sharp 320x200 mode. Hold the chord
   briefly and confirm it does not toggle repeatedly.
3. Release the keys, then press the chord again to return to the original
   multicolour display. Game state and guest video mode must be unchanged.
4. Check both the Might and Magic menu and Boulder play/scrolling. Hires
   preserves the pixels exactly in cells using at most two colours; cells
   with additional colours are approximated. Choose the display you prefer.
5. Return to DOS text and check that text/input still work. Confirm the BIOS
   and the existing default multicolour view still work after a fresh launch.

No physical V1.0.16 sharp-mode acceptance is claimed before this check.

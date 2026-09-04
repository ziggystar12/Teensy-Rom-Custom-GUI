# DOSVM hardware test

Use the matching **R18 CRT and V1.0.14 firmware** from `DosTest/`. The matching
published copies are `firmware/` and `dos/sd-card/`. Both support the standard
TeensyROM configuration without optional PSRAM. **R18 hardware acceptance,
including saves and restart persistence, is still outstanding.**

The single `DosTest/` kit contains exactly six files:

- `README.md`
- `SHA256SUMS.txt`
- `firmware/MPE_Firmware-V1.0.14.hex`
- `sd-card/DOSVM.CRT`
- `sd-card/DOSVM/DOSVM.IMG`
- `sd-card/DOSVM/D/README.TXT`

Use `DosTest/SHA256SUMS.txt` for the local kit and
[SHA256SUMS.txt](SHA256SUMS.txt) for published files. Do not combine firmware
and CRT revisions. The V1.0.12 release in `releases/native20/` remains unchanged.

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
R18 retains it. Firmware finishes the active VM slice, pauses foreground VM
execution and
publishes status `12` when the same pending packet is available for rereading.
A matching packet ACK resumes normal execution. Packet CRC validation and
bounded retry limits still reject persistent errors. The normal successful
path retains R16's direct-memory optimizations and control handling.

R16's interleaved host A/B tests measured 1.86x faster boot and 1.96x faster
`DIR` than R15 for identical guest work. R18 retains those changes; no new
physical speed ratio or stability result is claimed. Quick Shift/cursor taps
remain visible to the guest for at least 550,000 instructions, while ordinary
printable transitions keep the 512-instruction cadence.

V1.0.13 removed separate SD `mediaPresent()`/CMD13 probes from GUI firmware
fingerprinting; V1.0.14 retains that fix. A transient status-command failure
could disturb the active SDIO stream even when file reads were otherwise
working. The new path retains
file identity, size, clean EOF, cancellation and CRC verification. Host tests
exercise complete firmware files and a simulated status-command failure; this
is not yet a physical GUI-update acceptance result.

## Check the matching hardware kit

1. Flash `DosTest/firmware/MPE_Firmware-V1.0.14.hex`. If the currently installed
   GUI says "Firmware selection changed," use the working **V** classic text
   updater to install this fix.
2. Back up your old image and saves before installing the R18 disk template.
   Copy `DosTest/sd-card/` contents to the SD root. Confirm `/DOSVM.CRT`,
   `/DOSVM/DOSVM.IMG` and `/DOSVM/D/README.TXT`; no swap file is required.
   Future firmware/CRT updates should preserve the working image and D: folder.
3. Launch `DOSVM.CRT`. The loader says **MHS DOSVM**, its diagnostic title
   contains **R18**, and boot reports `D: SD folder ready.` before the prompt.
4. At `C:\>`, try `DIR`, `VER`, `DIR D:\` and `MEM`. Exercise Return, Backspace,
   quick key taps and repeated letters. Text is 320x200 hires with 40 visible
   columns; the right half of the 80-column BIOS console is clipped.
5. In an unused test folder, create a D: save and copy it to C: using the
   commands below. Wait for `C:\>` after the copy, reset to the launcher, and
   relaunch DOS. `TYPE` both files: each must still show `R18 SAVE OK`.
6. Run `PCTONE`: expect a SID tone, silence, and the DOS prompt.
7. Run `BOULDER`. Press **Space to skip the intro, then Shift to start**.
   Move repeatedly in all directions for several minutes, beyond the reported
   ten-second failure point. Check that quick taps and releases register,
   the field does not restart unexpectedly, and no transport diagnostic appears.
   Shift grabs; Space pauses. C64 Shift+cursor selects Up/Left.
8. If available, test port 2 joystick directions and fire (translated to cursor
   keys and Shift). Releasing a direction must stop the held key. Compare
   responsiveness with R16 rather than assuming the host speed ratio applies.
9. Leave DOS and confirm the Teensy resets to the launcher. Launch DOS again,
   reboot out, then cold-launch a previously working Sierra game with this
   exact firmware.
10. From V1.0.14, test the GUI firmware updater using a known matching HEX file.
    Confirm that the unchanged file reaches the update flow. Cancelling the
    confirmation must still leave the firmware untouched.

Storage commands before resetting:

```dos
MD D:\R18TEST
ECHO R18 SAVE OK>D:\R18TEST\SAVE.TXT
COPY D:\R18TEST\SAVE.TXT C:\R18SAVE.TXT
```

After relaunching:

```dos
TYPE D:\R18TEST\SAVE.TXT
TYPE C:\R18SAVE.TXT
```

After a completed save and shutdown, also inspect `DOSVM/D/R18TEST/SAVE.TXT`
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

## Earlier hardware baselines

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

# DOSVM hardware test

Use **`DosTest/` at the repository root** for the latest test kit. Its README
names the exact firmware and diagnostic title; `SHA256SUMS.txt` identifies
the package. Build intermediates stay in `build/dos-work/`.

The released 1.0.5 firmware has been confirmed to run Sierra games on the
user's hardware. **R6 failed its physical launch test: it rebooted to the
GUI.** Its Sierra compatibility has not been confirmed. Firmware integration
is under repair; consult the package README to identify the next candidate.
R6 supplied bank-58 ROM records that the native firmware parser rejects;
this cartridge-format fault has now been reproduced before DOS startup.

The user's R7 hardware photo reaches the firmware but reports `CTRL FB=04`:
sufficient PSRAM was not detected. The compiled startup includes PSRAM
detection; whether compatible chips are fitted is still unconfirmed. R8's
font correction does not alter that memory requirement.

This DOS implementation requires the optional Teensy PSRAM expansion. It
needs 1,185,632 bytes of working storage for the guest's flat address map, console, and
I/O area, even though the guest reports 640 KiB conventional RAM. SD capacity
and firmware flash do not replace that RAM. Native Sierra can run without
PSRAM, so its successful launch is not a PSRAM check.

1. Flash the test firmware named in `DosTest/README.md` from
   `DosTest/firmware/`.
2. Copy all contents of `DosTest/sd-card/` to the SD-card root. This supplies
   `/DOSVM.CRT` and `/DOSVM/DOSVM.IMG` with their manifests.
3. Check that a previously working Sierra game still launches using this
   exact test firmware. Record that separately from the released 1.0.5 result.
4. Launch `DOSVM.CRT` from the GUI and check the diagnostic title.
5. Look for the FreeDOS `C:\>` prompt. Type `DIR` and check for the Boulder
   and README entries. Test Backspace, Return, and a second `DIR`.

If startup stops at a diagnostic, record the title, stage, error, packet
count, and control bytes. `PACKETS 0000` means the C64 has not accepted its
first packet. A higher count demonstrates some packet progress; it does not
by itself validate all transport behavior or establish the cause of a later
failure. A firmware error byte of `04` reports unavailable VM memory. With
no PSRAM installed, the replacement must report this error before accessing
the guest-memory arena. The top-level terminal error and the firmware error
are separate fields; include both when reporting a diagnostic.

The exact R6 SD image passed the native host acceptance test: the pinned
BIOS reaches `C:\>`, accepts queued `DIR`, finds the Boulder entry, and yields
1,000 changed CGA text cells. That verifies the VM boot, disk, keyboard queue,
and text-buffer path; R6's physical failure showed that this coverage was
insufficient.

The replacement's build gate adds actual C64 CPU startup in VICE and an
integrated firmware host test using its source, CRT, and SD image together,
followed by replay of its completed wire trace through the actual C64
terminal. Small controls and the SD `File` object receive ordinary startup
initialization (`File` in `.data`); bulk text, keyboard, and speaker buffers
remain in `NOLOAD` DMAMEM and are explicitly reset to retain the 16 KiB stack
reserve. Coverage includes missing-PSRAM rejection, all 1,000 unique base
cells, hires frame completion, idle heartbeats, and C64 keyboard-matrix
`DIR` and Return messages. These are host and emulator checks. The physical
firmware, C64 input, and display path still need the steps above.

The image is read-only. Boulder graphics and PC-speaker output are later
milestones; the current pass condition is a usable DOS prompt without a
Sierra regression.

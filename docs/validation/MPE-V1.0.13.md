# MPE Firmware V1.0.13 / DOSVM R17 validation

V1.0.13 / native21 fixes the GUI firmware preflight rejection and adds DOSVM
packet recovery while retaining R16's direct-memory CPU optimizations.
All checks below passed on 2026-09-03. No physical cartridge was flashed or
controlled during this validation. Sustained R17 gameplay and the corrected
GUI updater still require acceptance on the user's hardware.

## GUI updater

Fingerprinting no longer calls `SD.mediaPresent()` during an active HEX read.
Those status probes could disrupt an SDIO multiblock stream and falsely report
that an unchanged selection had changed. Opening, identity, length, exact EOF,
cancel and CRC checks remain. Negative reads at EOF now fail validation.

The SD-stream reproduction fails with the old production backend and passes
with the fix. The host suite passed 32 target, 77 discovery and 14 stream
checks using the complete V1.0.12 and R17/V1.0.13 firmware files and a 6 MiB
synthetic stream. Both manual and startup paths include a previously retained
stream. The assembled C64 tests also execute the real WAIT/resident polling
flow. The combined focused GUI and packet-recovery run passed all 47 tests.

The GUI remains 22,513/22,528 bytes; resident apps remain 4,093/4,096 bytes.
If an older installed GUI rejects the new file, use **V** to install through
the original text menu once. The new backend takes effect after reboot.

## DOS packet recovery

R16's physical failure occurred after 1,071 packets. Its diagnostic showed
fixed identity bytes XORed with `08`, which is evidence of read corruption;
it does not establish a publisher race or the exact electrical cause.

R17 sends command `04` only after a failed commit, CRC or length read. The VM
returns from its current slice, then publishes quiet status `12`. The C64
retries the same immutable packet. Only the matching acknowledgement resumes
execution. Valid transfers never request this pause. CRC checks and retry
bounds remain enforced.

All 10 generated-C64 recovery tests pass: clean traffic, transient commit/CRC/
length corruption, persistent corruption, missing quiet response, a dropped
first request, and a firmware error during the wait. Corrupted packets are
never acknowledged or displayed.

The actual integrated firmware/FreeDOS/Boulder test passed:

- 5,040 DOS packets, including 4,096 publications held under wrong/stale ACKs.
- 64 input snapshots interleaved with pending publication and 16 sequence wraps.
- 64 quiet requests injected during guest execution; readiness appears only
  after the slice returns and exact ACK alone resumes execution.
- Two reset-separated boots with poisoned startup state, repeated `DIR`,
  keyboard controls, CGA and speaker output, and four reset-only exits.
- No-PSRAM operation and retained 512 KiB direct RAM2 ownership.

The exact packaged C64 cartridge passes reset-to-terminal boot checks and
wire replay: 151 text packets/18 hires frames, plus 594 graphics packets/233
multicolour frames and 262 exact SID-register frames. Separate game tests
verify Shift-to-start, movement, grab, release and port-2 joystick mapping.

## Speed and memory

R17 retains the 25,000-instruction ceiling, direct RAM2/F000 instruction and
operand access, ITCM operand helpers and prompt ACK/input yields. Host speed
checks at 1, 3 and 9 pending polls passed, with sampled boot times of 0.102,
0.140 and 0.367 seconds. These are desktop host measurements, not cartridge
boot times or a promised physical frame rate. Clean-path sampling showed no
slowdown from the preceding R16 host results.

MinimalBoot retains 21,376 bytes of stack and 337,376 bytes of pre-DOS RAM2
heap, the same as R16. Its ITCM extent is 97,408 bytes, within three banks.
The full image retains 20,992 bytes of stack and 499,968 bytes of RAM2 heap.
The final ownership gate classifies all 55 RAM2 symbols and confirms live
DOS, transport and SD state remains in RAM1 before direct DOS takeover.

## Sierra and final image audit

The fresh integrated Sierra run passed 7,244 intro packets, 862 gameplay
frames, 444 input events and progression to room 2. Sprite and legacy bitmap
paths both pass, with zero input interrupt masks. Command `04` is ignored by
Sierra. The existing cartridge and save formats remain unchanged.

The artifact audit matches the combined HEX to both linked firmware images,
verifies all bus handlers are entirely in ITCM, and checks 592 unchanged
inputs, including all 47 patches and the selected GUI snapshot.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `MPE_Firmware-V1.0.13.hex` | 6,322,355 | `b9b62fc662f0966e56a495ea45f6e6bc1adea4a04a255c15f1f5014e395e7abc` |
| `DOSVM.CRT` | 24,688 | `ab8dfcf0b139b50f38238d3a7bc2c0274903dd7a10dc4f304688aa8c368faf30` |
| `DOSVM.IMG` | 1,516,032 | `9b92715061c496a05466ad29d9697a717287fb6b6eaec1c4b4a6f850426ce9d4` |

Use the matching pair in the single `DosTest/` kit. Published copies are in
`firmware/` and `dos/sd-card/`; their hashes are in `dos/SHA256SUMS.txt`.
See [hardware checks](../../dos/HARDWARE-TEST.md) for the physical test.

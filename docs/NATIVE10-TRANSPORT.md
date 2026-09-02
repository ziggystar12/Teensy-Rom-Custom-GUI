# V1.0.2 native transport correction

The hardware report behind this release was a corrupt Graham sprite after repeated direction changes, followed by the C64's `ERROR 05 / INVALID PACKET HEADER` screen. V1.0.2 addresses an interrupt-masking defect in native input handling and makes the cartridge receiver recover bounded transient header reads before reporting a protocol error.

## Teensy input ownership

V1.0.1 called `noInterrupts()` while consuming every accepted keyboard, joystick, or mouse event in `MPE4NextPacket()`. The released MinimalBoot ELF places the masked instructions at `$6000ff3c` through `$6000ff9e`: `CPSID i` followed by `CPSIE i`, spanning 98 bytes and four flash instruction-cache lines. A cache delay in this section can defer the PHI2 bus interrupt. The function's existing `FLASHMEM` placement is retained to preserve instruction RAM and stack reserves.

The ISR already rejects a new event while `MPE4InputPending` is true. V1.0.2 uses that ownership rule: the consumer copies all four volatile fields to locals, issues a memory barrier, releases the pending flag, and interprets only the locals. It never masks interrupts. A later event can be captured while the previous snapshot is being interpreted.

The ordinary PHI2, BA, read/write, and data-buffer timing paths are unchanged. Removing the known interrupt mask is the scoped correction; the physical report does not by itself establish which individual bus edge or read was corrupted.

## C64 receiver recovery

The updated cartridge terminal checks the bounded copied packet's CRC before interpreting magic and protocol bytes. An oversized length permits only the eight header reads and uses the same bounded retry budget. Three rereads are allowed; a clean fourth copy is accepted once. Persistent corruption stops without acknowledging or publishing the bad packet. A CRC-valid malformed identity is still a protocol error.

The additional receiver regression coverage contains 33 cases:

- Nine transient header faults in each of intro and gameplay modes: 18 cases.
- The same nine faults remaining corrupt across all four attempts: nine cases.
- CRC-valid malformed magic or protocol bytes: three cases.
- Transient failures at three commit-register read positions: three cases.

The failure path disables all sprites before selecting the diagnostic VIC bank, so stale game sprite pointers cannot draw over the error text.

## Validation evidence

The new native harness rejects the V1.0.1 source on its first accepted input, recording one interrupt mask and returning exit code 93. The fixed source passes both sprite and legacy bitmap variants with 862 gameplay frames, 350 accepted inputs, 350 rejected competing producer events, 64 direction reversals, and zero interrupt masks. The sprite variant includes 44 complete pose commits and 754 frames reusing the previous pose.

The linked-artifact audit also rejects the released V1.0.1 ELF. It inspects all linked `MPE4` native glue functions, including the packet poller and any factored input helper, for `CPSID`, interrupt-mask register writes, and calls that disable interrupts. This checks the actual ARM instructions in addition to the host harness.

The final V1.0.2 build repeated that proof against its exact source clone and rebuilt SQ1 cartridge. Both sprite and legacy bitmap runs passed the same 862 frames, 350 inputs, 350 competing producer rejections, 64 reversals, and zero interrupt masks. The final combined HEX passed the linked instruction, GUI, source hash, and memory audits. MinimalBoot retains 16,416 bytes of stack reserve and 271,488 bytes of RAM2 heap reserve.

The generated SQ1 terminal replay accepted all 1,184 packets, including 88 sprite shape packets and 862 gameplay frames. Bitmap, color, sprite memory, and VIC register state matched the expected frame data. The terminal hash is `aac15736e46c4772a775dd46b326b2457c9055aa1aaf80802452634add754107`; its exact native wire hash is `024b5b555b0a398ed68ce077149e850a23e033f4fe4f4ce1923672ed205af1bb`.

The Black Cauldron Demo also passed against the same native10 source: 1,362 gameplay frames, 19 rejected input ownership collisions, zero interrupt masks, and 13 complete sprite commits. Its terminal replay accepted 1,984 packets, with 5,226 loader checks passing separately.

The selected GUI passed all 179 tests. Its source commit is `a8803c43b4369a760c5d45df28037a5273f39921`, with the 75-file snapshot digest `c728ea7ded52092bebd8d619f656d71dda599bbf4947ed10712c9071bd5ad988`. All 11 release/provenance tests passed without skips, including preservation of native09 and its original GUI snapshot.

The release is `releases/native10/MPE_Firmware-V1.0.2.hex`, 6,169,685 bytes, SHA-256 `49d41fcbae2b591b64a6d846d1f90c23e4fbf677405bcc87ff7a25f1b1ac5560`. Build evidence is in `build/native10-final-proof/firmware-native-result.json` and `build/native10-final-proof/artifact-audit.json`; SQ1 receiver evidence is in the AGI-64 build's `build/mpe-v102/sq1/receiver-replay.json`.

Physical direction-change and menu testing remains the final timing check; host packet replay does not emulate VIC sprite DMA or Teensy GPIO timing.

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

These pre-build results use an isolated copy of the maintained native source and the existing SQ1 resource package. The final V1.0.2 build must rerun the native harness with its exact source clone and newly built SQ1 cartridge, pass the linked-artifact audit, and record its hashes before release. Physical direction-change and menu testing remains the final timing check; host packet replay does not emulate VIC sprite DMA or Teensy GPIO timing.

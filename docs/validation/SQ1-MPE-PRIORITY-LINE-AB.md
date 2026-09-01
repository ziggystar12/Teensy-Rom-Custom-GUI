# SQ1 MHS Power Engine priority-line A/B

This kit compares native C64 priority foot-line scanning with the generic MHS
Power Engine `PQL` mailbox service on physical TeensyROM+ Fab0.4 hardware. The
two cartridges use the same SQ1 source and production policy. Only
`mhsPriorityLineAcceleration` changes.

Both cartridges keep the known-risk services out of the test:

- `$31` Logic-0 scan is disabled and no `MPS1` descriptor is packed;
- `$20` cell-patch publication is disabled;
- `$23` actor-frame publication is disabled;
- Debug Information and fast loading are disabled;
- `$10` picture DMA, `$21` scene prefetch, and `$22` room seed remain enabled.

`PQL` is command `$32`, completion status `$B2`, service `$12`. It reads only
the room-priority copy retained by `$22` and returns pass/trigger/water bits in
the mailbox. It performs no DMA and writes neither C64 RAM nor video memory. A
missing, stale, malformed, or error response falls through to the unchanged C64
priority scanner; only a validated, acknowledged result is accepted.

## Static C64 cycle model

This A/B is a correctness and measurement candidate, not yet evidence of a
speedup. SQ1 VIEW 1 was decoded from the game source: every Roger walking cel is
exactly seven pixels wide. A static 6510 count from `priority_block_gate_open`
through return gives these approximate costs for the current direct bank-59
bridge:

- native priority-v3: about 265 cycles when the first pixel blocks, 890 cycles
  for an all-water pass, 970 cycles for a typical single-run land pass, and
  1,300-1,400 cycles for a pathological seven-pixel span crossing nearly every
  run boundary;
- steady-state PQL: about 370 cycles for blocked, 385 for an ordinary pass, and
  425-430 for a trigger or all-water pass, plus about 35 cycles for each Pending
  firmware-status poll.

Those PQL figures exclude the one-time page-4 service discovery and assume the
command is complete at the first status read and the acknowledgement is Ready
at its first read. They count ordinary 6510 instruction timings without extra
hardware wait-state stretching. An immediate completion therefore means zero
extra polling iterations; one to three immediately observed Pending states add
about 35-105 cycles. The firmware computation is short, but only physical
instrumentation can establish that typical poll count. One complete 65,536
iteration timeout budget is about 2.29 million C64 cycles before the existing
reset/abort synchronization path takes over.

The direct path saves and masks IRQ state, selects bank 59, calls selector
`$3a` at `$bf00`, restores the exact resource bank left by the cel-width lookup,
then restores flags before consuming the tri-state result. A previously
validated page-4 capability state also skips the full activation signature, but
Ready/busy synchronization, the complete tag/error/result/reserved/capability/
token validation, and terminal acknowledgement remain on every request. It no
longer reinstalls UI's 42-byte action gate on the hot path.

At zero to three Pending reads this model predicts a win for normal passing
lines (roughly 1.8-2.5 times fewer C64 cycles than the representative native
all-water and land cases) and a loss for the native first-pixel-block best case.
About 17 Pending iterations would erase the modeled advantage over the typical
970-cycle native land scan. Until physical timings establish the normal poll
count and the A/B acceptance sequence passes, do not describe PQL as a measured
CPU-cycle or gameplay-speed win for SQ1.

## Files

- `01-SQ1-MPE-NATIVE-PRIORITY.crt` is the control. Every priority foot-line is
  scanned by the C64.
- `02-SQ1-MPE-PQL-ACCELERATED.crt` submits eligible foot-lines to the Teensy and
  retains the native C64 fallback.
- Matching `-config.json` and `-report.json` files record the exact policy and
  packed-runtime contract for each cartridge.
- `MHS-PowerEngine-TRPlus-v1_full.hex` is the one firmware image used for both
  runs.
- `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex` restores official firmware after
  testing if desired.

The build script only creates and verifies files. It never flashes firmware.
It stages both cartridges outside the final kit, verifies the A/B contract and
packaged copies, and checks both firmware inputs against the canonical firmware
checksum manifest. It then publishes all payloads before publishing
`SHA256SUMS.txt` last. A failed control or accelerated build therefore does not
publish a new checksum manifest or mix a partial run into a previously verified
kit.

## Physical test sequence

1. From the kit directory, verify every file listed in `SHA256SUMS.txt` with a
   trusted SHA-256 checker. For example, Git Bash can run
   `sha256sum --check SHA256SUMS.txt`. Do not test with a missing or mismatched
   entry.
2. Flash `MHS-PowerEngine-TRPlus-v1_full.hex` once for this pair.
3. Disconnect USB power, turn off the C64, and confirm every TeensyROM+ LED is
   dark. A C64 power switch is not a cold MCU reset while USB is still powering
   the Teensy.
4. Power on and run `01-SQ1-MPE-NATIVE-PRIORITY.crt` first.
5. Follow the same Room 1 Astral Body route used in earlier tests. Type
   `astral body`, dismiss the retrieval message the same way, and observe the
   complete return to graphics.
6. Confirm Room 1 redraws, the robot moves and retrieves the tape, and Roger can
   walk afterward. Walk across several control/priority boundaries rather than
   testing only one position.
7. Type several commands and confirm each new letter is drawn once, without the
   parser retyping the whole line. Open and close the normal AGI menu, then test
   the physical TeensyROM+ menu button.
8. Turn everything off, remove USB power, and wait for all TeensyROM+ LEDs to go
   dark again.
9. Repeat steps 4 through 7 with
   `02-SQ1-MPE-PQL-ACCELERATED.crt`, using the same route and actions.

Do not change firmware between the control and PQL runs. Cold power-cycle
between every repeat, including retries of the same cartridge.

## Acceptance and reporting

A cartridge passes only if all of these are true:

- Room 1 redraws after the Astral Body retrieval message;
- the robot completes the tape retrieval;
- Roger walks normally afterward and respects room boundaries;
- parser input draws correctly and the standard AGI menus work;
- the TeensyROM+ menu button remains responsive.

A blank dark-blue field with a light-blue border is the C64-color OS screen even
when no banner or `READY.` text is visible. Record that separately from a grey
background lock, a visible Room 1 lock, or a cosmetic object-outline problem.
For each run, report the cartridge number, final screen/colors, last completed
retrieval action, movement/parser/menu results, and TeensyROM+ menu response.

If the native control fails, do not attribute the run to `PQL`; re-establish a
repeatable control first. If the control passes and only cartridge 02 fails, the
priority-line request/validation path is isolated. If both pass, repeat the pair
at least once before treating the service as physically accepted.

To return to official firmware, flash
`TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex` and perform one final complete cold
power cycle.

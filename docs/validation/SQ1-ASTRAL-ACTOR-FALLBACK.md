# SQ1 Astral Body physical-isolation kit v2

This is a physical-test kit, not a replacement MHS Power Engine release. It
contains the frozen cartridge and firmware files associated with one reported
9:04 PM retrieval pass. That single observation is not a stable hardware pass:
the same cartridge has since redrawn Room 1 and then hard-locked.

- `TRPLUS3-REFERENCE-2DED.hex`
- SHA-256 `2ded186c2fa66d83d1602d5dc9ab970f282cb7f3d48b7fd926b2829fb2838369`

The hash authenticates the firmware file only. It does not prove which image is
currently running on the Teensy. Reflash it before this sequence. No diagnostic
firmware is required; every cartridge below runs with that one generic image.

## Current physical observations

On 2026-08-30, with device firmware reported or assumed to be `2DED` but not
independently reconfirmed, the following behavior was observed:

1. `01-SQ1-REFERENCE-55FC.crt`: each typed key repainted the parser row, objects
   retained grey outlines, Room 1 became visible after the Astral Body message,
   and the machine then hard-locked. A visual redraw is not restored input and
   this run is a failure.
2. `03-SQ1-BLUE-ACTOR23-BYPASS-CC5E.crt`: blue screen; Room 1 did not redraw.
3. `04-SQ1-REFERENCE-LOADER-ONLY-DD48.crt`: grey outlines remained, followed by
   a blue screen without a Room 1 redraw.
4. `05-SQ1-REFERENCE-PALETTE-ONLY-6EA7.crt`: object borders were correct, then a
   blue screen without a Room 1 redraw.

Cartridges 04 and 05 modify disjoint regions yet both move the visible failure
earlier. They do not establish either region as the unique cause. These results
must be repeated after a confirmed flash and complete MCU power loss before
being treated as deterministic.

## Cold-start requirement

1. Verify every file against `SHA256SUMS.txt`.
2. Flash `TRPLUS3-REFERENCE-2DED.hex`.
3. Disconnect USB power. Turn off the C64 and confirm every TeensyROM+ LED goes
   dark before each run. If a modified board keeps USB power connected, a C64
   power cycle alone is not a cold Teensy reset.
4. Use the same save/route and dismiss the same Astral Body retrieval message.

## Clean post-redraw-lock A/B

1. `01-SQ1-REFERENCE-55FC.crt` is the exact frozen 9:04 cartridge.
2. `06-SQ1-REFERENCE-ACTOR23-BYPASS-2442.crt` differs from cartridge 01 at
   exactly three bytes. At bank 59 `$BF52` (CRT offset `$F0712`) it replaces
   `20 47 BA` (`JSR $BA47`) with `18 EA EA` (`CLC; NOP; NOP`), selecting the
   native C64 compositor without submitting `$23 ACTOR_FRAME`.

Run 01 and 06 on adjacent cold starts. If 01 redraws and locks while 06 fully
passes, `$23` or state uniquely consumed by `$23` is necessary for that late
lock. If both lock, the late failure is outside `$23`. If results vary across
cold starts, treat the path as electrically or timing-marginal rather than as a
deterministic cartridge regression.

## Blue-screen A/B

1. `02-SQ1-BLUE-REFERENCE-944C.crt` is the later 175-byte source-change set.
2. `03-SQ1-BLUE-ACTOR23-BYPASS-CC5E.crt` differs from cartridge 02 only at the
   three-byte `$23` call above.

Run 02 and 03 on adjacent cold starts. If both blue-screen before Room 1, the
blue-screen boundary occurs before or outside `$23`. Cartridge 03 still retains
complete-picture DMA, priority DMA, `$20` cached patches, `$21` scene prefetch,
and `$22` room seeding.

Cartridges 04 and 05 remain source-state probes, not passing controls. Because
the common cartridge 01 also fails, a failure in either probe is not sufficient
to name its changed region as the root cause.

## Pass criteria

A run passes only if all of the following occur:

- Room 1 redraws;
- the robot moves and completes tape retrieval;
- Roger responds to movement afterward;
- keyboard/parser input works;
- the TeensyROM+ menu button responds.

Room 1 becoming visible without resumed interpreter and input activity is a
failure. Record parser repaint behavior and object borders separately; those
cosmetic symptoms do not substitute for the acceptance gate.

Do not restore the rejected per-segment `$23` firmware. To return to stock,
flash an official TeensyROM+ restore image and cold power-cycle. The legacy
hardware-pass inputs referenced during development are deliberately not
distributed in this public repository; they remain private, input-only
validation material and do not establish current acceptance.

# SQ1 MHS Power Engine phase-trace CRT

This kit traces the real SQ1 Astral Body return path on physical TeensyROM+
Fab0.4 hardware. It does not replace or modify firmware. Every cartridge uses
the same generic reference image:

- `TRPLUS3-REFERENCE-2DED.hex`
- SHA-256 `2ded186c2fa66d83d1602d5dc9ab970f282cb7f3d48b7fd926b2829fb2838369`

The trace writes the same color to the VIC border and background immediately
before an accelerated call and again when that call returns. It preserves the
original A, X, Y, processor flags, game data, and fallback behavior.

## Run this first

1. Verify all files against `SHA256SUMS.txt`.
2. Flash `TRPLUS3-REFERENCE-2DED.hex` even if that image is believed to be
   installed already.
3. Disconnect USB power. Turn off the C64 and confirm every TeensyROM+ LED goes
   dark, then power on.
4. Launch `01-SQ1-REFERENCE-55FC-MPE-PHASE-TRACE.crt`.
5. Repeat the same Room 1 Astral Body retrieval route.
6. At the lock or blue screen, report the final border/background color and
   whether the TeensyROM+ menu button responds.

Cold power-cycle between runs.

## Physical results and next discriminator

On the NTSC Fab0.4 under test, both original trace cartridges last showed the
`$20` grey family and never showed a `$23` marker:

- 01 did not publish Room 1; it stopped on a blank blue field with a light-blue
  border and no Commodore banner or `READY.` text.
- 02 published Room 1, then locked before robot motion; AGI input and menus did
  not resume.
- 03, the 55FC `$20` bypass, stopped on the same blank C64-color screen as 01.
- 04, the corrected 944C `$20` bypass, restored Room 1 and completed the
  retriever/tape animation. Its flashing yellow trace means command `$23`
  returned failure and the native C64 compositor fallback completed the work.

Run cartridge 05 next. It is a clean, non-tracing candidate from the corrected
944C lineage. Exactly the `$20` and `$23` calls select the native C64 fallback;
`$10`, `$21`, and `$22` remain accelerated with the same generic 2DED firmware.

Cartridge 05 is physically accepted only if Room 1 redraws, the retriever gets
the tape, Roger can move afterward, keyboard/parser input works, and both AGI
and physical TeensyROM+ menus remain responsive.

## Color legend

| Accelerated phase | Before call, no return yet | Returned success | Returned failure |
| --- | --- | --- | --- |
| `$10` complete picture | red | green | brown |
| `$20` cached cell patch | dark grey | grey | light grey |
| `$22` room seed | orange | light green | light red |
| `$23` actor frame, palette A | purple | cyan | yellow |
| `$23` actor frame, palette B | light blue | white | black |

If a **before-call** color remains frozen, the C64 never regained execution
after publishing that command. That points at the command's DMA/close/bus-release
path rather than later interpreter code. A success or failure color proves the
C64 resumed far enough for the wrapper to observe the original carry result.

The Astral text-to-graphics return normally reuses the Room 1 seed established
on original entry. It may issue `$20`, then uses `$23` as the retrieval robot
moves. Therefore the most important results are dark grey/grey/light grey,
purple/cyan/yellow, and light blue/white/black. The `$23` palettes alternate on
adjacent calls, so a second frame remains distinguishable even when Astral's
graphics restore does not issue a new `$20` or `$22`. Earlier `$10` and `$22`
colors belong to original room setup and provide context.

Colors are diagnostic milestones, not the gameplay acceptance gate. A real
pass still requires Room 1 redraw, completed robot/tape motion, restored Roger
movement, keyboard/parser input, and a responsive TeensyROM+ menu button.

## Exact cartridges

- `01-SQ1-REFERENCE-55FC-MPE-PHASE-TRACE.crt`
  SHA-256 `49e31833afc3e5e0a6a72449fb8d3d84bd785c16988dee857e67356ef9617a16`
- `02-SQ1-LATER-944C-MPE-PHASE-TRACE.crt`
  SHA-256 `80058389aafa8a8b0e0d0c32fcdd0cece75a6b011ef59495f3a10500259ffb62`
- `03-SQ1-REFERENCE-55FC-MPE-TRACE-CELL20-BYPASS.crt`
  SHA-256 `7e7898a916a42323f0e58029cbf10946a45da5a04ecac6ca1324af4ccbe09820`
- `04-SQ1-LATER-944C-MPE-TRACE-CELL20-BYPASS.crt`
  SHA-256 `123f4caab27c56023960ec797f93fb5fda91fc15232f8cbe53009622738ad899`
- `05-SQ1-LATER-944C-CLEAN-NATIVE-ACTION-FALLBACK.crt`
  SHA-256 `7634ab6cdeca42039b0f3444bc612c629670d34be1514ea0d8b1835149fd637a`

The trace transforms inject 192 bytes into the verified-blank bank-59 range
`$B700-$B7BF` and replace exactly six three-byte call/epilogue sites. Clean
cartridge 05 injects no trace payload and changes only the six bytes belonging
to the `$20` and `$23` calls. Every transform is pinned to the `55fc...` or
`944c...` lineage and never writes to the frozen reference source.

To return to stock, flash `TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex` and perform
a complete cold power cycle.

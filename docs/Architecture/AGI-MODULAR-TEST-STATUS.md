# AGIVM extraction test status

September 4, 2026. Independent ABI 2 module, using the already published
V1.1.1 firmware byte-for-byte. No firmware source, HEX, ABI service definition,
NES engine or DOS engine change is needed. No hardware was flashed.

## Implemented

- `/VMS/AGIVM/{manifest.vmi,engine.mvm,client.crt,GAMES/*.AGI}` and root launcher.
- Generic manifest association of `.agi`; selecting a game outside the picker
  folder launches that exact path. The firmware never identifies AGI itself.
- Standalone M4G2 content with original game startup and validated CRC/indices.
  This is not a renamed cartridge. Existing native VIEW resources, sprites,
  C64 menus, original protection choices and package identity remain supported.
- The AGIVM-owned 128-entry, 17-row picker supports page keys and incremental
  selection updates. A corrupt selection stays in the picker with an error.
- Existing interpreter, renderer, parser, SID/sprite presentation, keyboard,
  joystick and 1351 mouse policy reside entirely in the module/client bundle.
- Generic file services supply new-format identity/epoch/CRC-checked save and
  restore with write/flush verification. No old-save compatibility work.

The AGI-64 desktop compiler 1.0.33 MPE option now emits standalone `.AGI`
content through `host/build-agivm-content.mjs`. This repository's explicit
`agi/tools/build_agi_content.mjs` command delegates to that same builder.
The compiler's ordinary C64 output stays `.crt`; no VM engine or firmware is
embedded in either the converter output or the standalone compiler.

## Graphics regression correction

The initial native VIEW encoder advanced its pixel cursor only once per cel,
not once per row. Generated rows overwrote each other, and cel ranges overlapped.
This corrupted 1,607/1,652 SQ1 cels and 739/740 KQ1 cels while still passing
checksums and transport replay. The encoder now advances once per row, and an
independent decoder checks every generated pixel against the original VIEW.
Multi-row, multi-cel and 1/2/4-bit regressions also reject overlap/corruption.

Corrected SQ1 and KQ1 files pass 1,200 actual-module frames and exact 6510 replay.
SQ1 now emits 36 sprite packets in that run (the original 50-packet figure below
was from corrupted content). The 5,780,568-byte SQ3 package also passes 1,200
module frames: room 2, 200 scans, 64 sprite packets, 373 resource-cache reads.
Content uses the module's existing 32 MiB SD-file bound, not a cartridge limit.
Firmware, module and C64-client hashes are unchanged. The user's physical test
confirmed launch but exposed the bad artwork; corrected content still needs a
physical rerun. No corrected-hardware graphics or full-game claim is made.

## Picker input regression correction

The user reported no cursor-key or joystick response after the selector drew.
The picker previously emitted nothing when its bitmap was unchanged, while
the C64 client's main loop samples input between frame ends. Once the initial
frame finished, both sides waited: the client for a packet, the module for input.
Direct calls to the keyboard scanner and injected module events had missed
this interaction.

AGIVM now emits paced frame ends while idle, with zero bitmap cells. Selection
changes still redraw only changed rows, without blanking. The normal C64
frame pacing, input release-arming and per-press direction behavior are retained.
The fix is entirely in `engine.mvm`: firmware V1.1.1, CRT/client, converters,
compiler and existing `.AGI` content do not change.

The new native idle-frame regression failed before the fix. Afterward, native
tests cover keyboard and joystick up/down, paging, fire, held/released stick,
corrupt selection and zero-cell idle frames. `agi/tests/picker_idle.mjs` also
executes the exact packaged C64 **main loop** with actual native picker packets;
it changes the physical matrix/port inputs after the menu draws and checks the
resulting input messages, without calling the scan routine directly. Existing
keyboard/mouse/ghosting, display replay, module guards and PAL/NTSC boot checks
pass. This is software/emulator evidence; physical selector confirmation remains
pending. Replace `/VMS/AGIVM/engine.mvm` and retry with launch controls released.

## Dialog blink correction

The user reported a full-screen blink when the SQ1 alarm dialog opens and when
it is dismissed. The renderer unconditionally disabled its high-resolution
parser strip for any modal. The unchanged C64 client correctly treats that
split-policy change as a layout transition and hides the whole screen while
updating it. A centered dialog does not require that layout change.

The renderer now retains the split when the modal's saved text/attributes show
that it has not overwritten the source input row or the two reserved bottom
rows. It also keeps suppressing the relocated parser's original row, avoiding
a duplicate prompt under the dialog. Low/tall windows, full-screen inventory,
synchronous string/number entry and authored graphics/text-mode changes retain
their existing layout paths, so dialog contents are not clipped to avoid a blink.

An authored AGI `print`/`print.at` regression failed before this correction.
It now checks repeated centered open/hold/dismiss, timed dismissal, unchanged
surrounding scenery, delta-only transfer, bottom-row overlap and authored input
and screen-mode changes. Exact packaged 6510-client replay verifies 40 normal
dialog frames never clear display-enable or set the transition-hidden state.
The complete regression wire contains 46 frames, including the intentional
layout-change cases. SQ1/KQ1 1,200-frame smoke tests and PAL/NTSC boot checks pass.
This is deterministic software evidence, not a reproduction of the user's
exact closet-exit sequence on hardware. That physical rerun remains required.

Replace only `/VMS/AGIVM/engine.mvm`. The CRT/client and firmware V1.1.1 remain
byte-for-byte unchanged, and existing `.AGI` content/compiler output is valid.

## Memory

All three modules reuse the same ABI 2 memory windows; only one loads.
The AGI ARM image uses 70,016 bytes of the 96 KiB RAM1 code window and 2,144
bytes of static RAM1 data/BSS. The native host test measures 63,960 bytes of
support workspace (64-bit host pointers; not an exact ARM runtime measurement),
well within the 192 KiB module support window after statics.

The pointer-free 9,624-byte AGI state lives in RAM2's first 16 KiB page. The
remaining 31 pages provide a bounded 507,904-byte game-resource cache. Page tags
stay in RAM1. Packages larger than RAM2 work through SD reads; the SQ1 test
package is 864,032 bytes and exercises eviction. Picture planes, framebuffers,
interpreter controls, queues and checkpoint scratch stay in RAM1.

The existing host has a separately reserved 48 KiB stack. No live repartition,
PSRAM, new interrupt logic or module flash caching was added. Link bounds and
host guard tests pass; physical stack high-water remains an acceptance gate.

## Verification

Run `node scripts/build-agivm.mjs`, then `node scripts/verify-agivm.mjs` with
optional explicit `.AGI` paths for private game tests. The focused verification
locks source hashes and records artifacts in `build/agivm/verification.json`.

- Actual module and real generic file/registry services: picker, page movement,
  80-cell selection change without blanking, corrupt-file rejection, exact
  direct launch, parser editing, pointer and held joystick input.
- Immutable packets and frames across pending ACKs; RAM1/RAM2 boundary guards.
- Save/re-read, wrong identity rejection, failed writes and failed flushes.
  The 12-slot Save/Restore lists now show `Empty`, verified saved room/score,
  or `Unavailable`. Metadata is cached outside the 9,624-byte saved State and
  refreshed on each opening. Storage checks the same primary/backup pair as
  Restore, using the unpublished frame as scratch before the renderer clears
  and rebuilds it; live RAM2 game state is never used as inspection scratch.
  The real-module fixture covers zero/255 room and score values, slots 1/12,
  overwrites, failed write/flush, backup recovery, corrupt/epoch rejection and
  cold restart. All 29 generated frames match the actual C64 presenter replay.
- MVM integrity/bounds tests and 25 content/actual-6510 keyboard/mouse tests,
  including held/released inputs, repeats, simultaneous keys and ghosting cases.
- Actual generated CRT resets, copies the exact client to RAM, issues START
  and reaches the bounded no-host diagnostic in VICE PAL and NTSC.
- KQ1: 1,200 frames, room 1, 200 scans, 2 sprite packets; 31 resource-cache reads.
- SQ1: 1,200 frames, room 2, 181 scans, 50 sprite packets; 138 cache reads.
- Exact 6510 replay of captured module packets checks bitmap, screen/color RAM,
  display modes, SID-frame boundaries and sprite shapes/coordinates/palette.
  The KQ1 replay covers 201 frames including sprite output; the SQ1 replay
  covers its first 201 startup frames, not the entire run.

These are software/package/emulator checks, not physical performance proof.
No full-game completion or resolution of the earlier field crashes is claimed.
The public download contains only the authored AGITEST diagnostic; private
compiled Sierra packages stay under ignored `build/agivm/private/`.

## Next hardware test

Keep V1.1.1, extract AGIVM.zip to SD root, and open AGIVM.crt. Test AGITEST,
then copy KQ1.AGI or SQ1.AGI into `/VMS/AGIVM/GAMES/` and launch it. Also test
direct `.AGI` selection elsewhere on SD. Check parser typing/DEL/Return,
held/released joystick, Shift/cursor/function combinations, mouse movement and
buttons, sprites, music, speed menus and room transitions. Record PAL/NTSC.
In SQ1, leave the closet and check that the alarm dialog opens and dismisses
without blanking the scene; also check a timed dialog and a large/low window.
Save in two rooms with different scores, reopen Save and Restore, and confirm
both labels and empty slots. Power-cycle, restore each slot, then overwrite one
and confirm its label changes. Physical save-menu/SD timing remains untested.
Reset returns to the GUI. Photograph the full diagnostic page if startup fails.

Module input overflow is FB `36`; direct invalid content is FB `32`.
Interpreter/session errors use FB `40 + session error`, with logic, opcode and
low instruction offset in F8/F9/FA. Long-run transport and game compatibility
remain open, alongside the already deferred NES performance work.

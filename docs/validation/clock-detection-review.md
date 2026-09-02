# SID clock reporting review

Reviewed the published V1.0.3/native11 source at `7a25d13` after a report that
startup displayed PAL. The report did not identify whether this was the tune or
machine label. This review does not establish a physical video-detection fault.

The default tune is **Death Is No Evil**. Its PSID header flags are `$0014`, whose
clock bits declare PAL; its speed field is zero. The old `SID Clock: PAL` startup
message reported those file flags. The C64's measured video/TOD bits are separate
and are published before the startup SID is loaded. This interpretation follows
the [HVSC SID specification](https://hvsc.c64.org/download/C64Music/DOCUMENTS/SID_file_format.txt).

The reporting change shows both values together, for example:

```text
SID tune timing: PAL
C64 video: NTSC, TOD: 60Hz
```

The first row is padded to the loading panel's 34-glyph width. Its carriage return
also separates rows in the classic menu. Both descriptions fit together in the
bitmap panel; no C64 code or memory allocation changes are required. Detection,
the four calibrated CIA timer values, detailed SID-page information, and playback
behavior remain unchanged.

## Executable checks

Run `node --test tests/sid-clock-reporting.test.mjs` from the repository root.
`CXX` can select another C++ compiler; the Windows default is the existing MSYS2
MinGW compiler. The test compiles the complete current `ParseSIDHeader` and the
real `SendMsgPrintfln` formatter, stubbing only transport and Arduino services.
It exercises all 16 combinations of the four tune-clock flags and the four
machine-video/TOD bit patterns. It verifies both labels, modal row bounds, the
unchanged machine register, the detailed SID information, and the exact existing
CIA timer values `$4CC7`, `$4FB2`, `$4058`, `$42C6`.

## Existing detector limitations

A separate read-only instruction probe assembled the production clock-detection
block in `MainMenu.asm` and executed those 6502 instructions. The CIA timer-high
fixtures documented in that source produced the expected register values:

| Timer high byte | Video / TOD fixture | Published bits |
| --- | --- | --- |
| `$32` | PAL / 50 Hz | `$00` |
| `$20` | NTSC / 50 Hz | `$01` |
| `$7F` | PAL / 60 Hz | `$02` |
| `$70` | NTSC / 60 Hz | `$03` |

To reproduce the boundary cases, assemble the block beginning at `Get video
standard and TOD frequency` and ending before `store this code page range`, at
`$2000`, with `IO1Port=$DE00` and `wRegVid_TOD_Clks=36`, followed by `RTS`.
For working-clock fixtures, advance `$DD08` on each polling phase and supply the
listed byte when `LDA $DD05` executes. Inspect `$DE24` and `$DC0E` at return.
These fixtures verify the real instruction paths, not analog CIA timing.

Two older failure paths remain outside this reporting-only change:

- Hold `$DD08` at zero throughout the initial probe. After both loop counters
  wrap, execution takes `jmp set60NTSC`, publishes `$03`, and clears CIA1's
  50-Hz selection. On an actual PAL machine with a missing TOD signal, that would
  misclassify the machine as NTSC. This is the opposite of the reported symptom.
- Allow the first TOD probe to pass, then stop `$DD08` from advancing in either
  later synchronization loop. Those polling loops have no timeout and do not
  return. A future detector change should add bounded waits and obtain video
  identity independently when TOD fails, with PAL and NTSC hardware coverage.

Neither path was introduced by the desktop update. No calibrated bus timings or
clock detection instructions were changed without corresponding hardware
evidence. MPE gameplay uses its own raster-frame acknowledgement pacing, so the
SID metadata label does not select game speed.

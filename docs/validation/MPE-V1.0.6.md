# MPE Firmware V1.0.6 validation

V1.0.6 / native14 corrects desktop menu and dialog stalls. Menus restore the
retained background and draw only their transient overlay; dialog/control
drawing permits IRQ service; the mouse IRQ publishes the live pointer position.
The native AGI engine, cartridge formats and save identities are unchanged.

## Executed desktop checks

All **252 checks passed**, with no failures or skips: 250 desktop/backend
checks and two SID-reporting checks. Run with ACME 0.97 available:

```powershell
node --test Source/C64/MainMenuCRT/tests/*.test.js Source/C64/MainMenuCRT/tests/*.test.mjs Source/Teensy/tests/*.test.js tests/sid-clock-reporting.test.mjs
```

The menu regression executes 24 operations across Home, a populated SD browser
and an IEC browser. It compares every final pixel with an independently composed
full frame, rejects backend access and base-canvas writes, preserves file
selection identity, and delivers periodic IRQs through the production SID/mouse
wedge. Menu activation removes the overlay before dispatching its action.

The verified V1.0.5 baseline at `37fe327` performed 975,034 CPU instructions
to open a menu over 16 SD files, versus 335,067 on Home. Each SD menu operation
requested the path and all 16 cached names/types again (17 requests and 386
serial reads). This was cached metadata traffic, not a directory rescan.

The new path opens in 158,287 instructions, moves a row in 158,308, and closes
in 45,291, with no backend requests. Costs no longer depend on the underlying
browser. The menu tests enforce at most 180,000 instructions for open/move/switch
and 55,000 for close. CPU counts exclude VIC bus steals, backend waits and real
SID/KERNAL routine costs; they are not physical frame-duration measurements.

The original plain menu redraw had only a short 54-cycle interrupt mask.
Its full-screen work delayed main-loop pointer publication. Dialog drawing had
separate long interrupt masks, including 1,591,149 cycles for firmware
confirmation. The fix removes renderer-wide masks while retaining short atomic
input/TOD snapshots and the compact menu's required zero-page protection.

Dialog tests inject actual production IRQ and mouse routines during drawing,
with a SID callback and KERNAL scratch/register behavior. Interrupted and
uninterrupted bitmap/color results match, and the original dialog appearance is
preserved. Music-control selection and repaint are covered too. The loading
indicator falls from 433,877 to 64,612 CPU cycles by publishing only its seven-row
track. All 29 animation phases preserve pixels and colors outside that track.

Pointer tests cover 108 boundary combinations, accepted motion, inactive/hidden
states, other sprite X bits, bank/register restoration and unchanged renderer
scratch/canvas memory. The main loop still owns visibility and click dispatch;
its short atomic position update reads live coordinates rather than a stale
frame snapshot.

The existing backend fixtures retain 370 menu-map scenarios, 36 file-operation
cases and 30 firmware-target checks. Shared widgets, scrolling, labels and
fresh-input firmware confirmation remain covered by the full suite.

## Memory and artifact

The compact cartridge is 7,541/8,192 bytes; its contained 7,365-byte menu is
unchanged. Desktop code is 22,426/22,528 bytes and resident apps are
4,080/4,096 bytes. Embedded desktop/Help payloads are 26,625/2,634 bytes.
All existing assembly memory guards pass.

`MPE_Firmware-V1.0.6.hex` is 6,186,965 bytes. SHA-256:

`62df17725a131d50bdf326b51e877674c9f4bfbe31b0616203ad10f75da3b1db`

The final audit verified both linked ARM images, 462 unchanged audited inputs,
the 108-file GUI snapshot, embedded GUI headers, all nine native engine sources
and the executed save tests. MinimalBoot retains 16,416 bytes of stack reserve
and 271,488 bytes of RAM2 heap reserve. The GUI source commit is
`f657a325cba8840a7fff1189320482ec3c00a5b4`; the
[native14 manifest](../../releases/native14/manifest.json) records exact inputs.
All 19 release/provenance checks passed, including preservation of native05
through native13 payloads and the historical GUI snapshots.

Local proof files in `build/native14-final-proof`:

- `artifact-audit.json`: `b54cf1747b9cc5669e414287af627ecc6aa8ef359db4d8e9395113d9b14f705f`.
- `firmware-native-result.json`: `af9a9210bbf2efee6c7388cb8b4fa8cc4f6b4d260da66b25029f894216cee479`.

Menu measurements and dialog IRQ evidence are retained under
`build/native14-ui-proof` in the canonical checkout. Source regression tests
remain the maintained, reproducible checks.

## Native game regression and hardware boundary

The actual integrated firmware module passed sprite and legacy bitmap runs
using unchanged V1.0.2 SQ1 data. The sprite run completed 862 frames, 1,184
packets, 350 inputs, 64 direction reversals, 88 sprite packets and 44 commits,
with zero input interrupt masks. Three- and four-layer frames remain covered.
Both runs passed five `/SAVES` directory checks, eight restore/fallback checks
and six injected transaction failures, with no root mutations.

The Black Cauldron demo was not rebuilt. Its unchanged cartridge SHA-256 is
`a235a7925332df82dc30e9c966c7038d75b70294e8294330236346e316ff2d7c`.

This firmware was built and checked in software, not flashed here. Physical
C64/Teensy acceptance remains open, especially continuous SID music and pointer
motion while opening menus and dialogs over SD/USB/IEC browsers. Host execution
does not establish VIC timing, real tune behavior or physical bus performance.

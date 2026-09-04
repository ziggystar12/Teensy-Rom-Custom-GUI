# TeensyROM firmware V1.0.19 / native27 validation

V1.0.19 combines the native M4G2 MHS Power Engine from `e69b885` with the
revised bitmap desktop and its loadable apps. The internal release id is
`native27`.

## Release identity

| Item | Recorded value |
| --- | --- |
| Firmware | `releases/native27/MPE_Firmware-V1.0.19.hex` |
| Firmware size | 6,408,716 bytes |
| Firmware SHA-256 | `492906fa51b1477cb523800af010b26353e2a7380b40e8893633ac285ab38ac3` |
| GUI source commit | `4c874daa6e89fc3e03ae66bfa3bc28b265334d23` |
| GUI snapshot | `gui/selected-v1.0.19` |
| GUI snapshot digest | `4d609784e1344a70fe0f6495d15a13fb9f9db44dc24680a8d93b2af48cfa9718` |
| GUI snapshot inputs | 162 |
| Backend patch SHA-256 | `58aa5a1eeb6c93fbebf19460dfabf793a3df307a5c16fcecb3136b2b86e5d47d` |
| Integration patches | 47 |

The native27 manifest records the same firmware, selected GUI, backend patch,
toolchain, engine sources, native DOS sources, shared native arena, restore
image, and patch chain. The release kit checksum file independently records the
firmware hash above.

## Linked firmware memory

The completed native27 build reported these linked values for the full upper
firmware:

```text
FLASH: code:415492, data:1558672, headers:8776   free for files:6143524
 RAM1: variables:264164, code:227720, padding:1656   free for local variables:30748
 RAM2: variables:24320   free for malloc/new:499968
```

The MinimalBoot lower firmware reported:

```text
FLASH: code:257476, data:19348, headers:8868   free for files:7840772
 RAM1: variables:407648, code:98184, padding:120   free for local variables:18336
 RAM2: variables:186912   free for malloc/new:337376
EXTRAM: variables:1310720
```

Both images therefore retain the build's required stack and heap reserves.
The build completed the selected-GUI provenance checks, applied all 47 locked
integration patches, compiled both images, combined them, and produced the
versioned release artifact.

## M4G2 engine preservation

`e69b885d58b3d1f264a42ea34fe9440c286638b8` introduced the M4G2 firmware
boundary used by the current AGI cartridges. A release audit compared every
`engineSources` entry in the native27 manifest with both the current working
tree and that commit. All nine files have the same SHA-256 in all three places:

- `engine/native-game/mpe4_game.h`
- `engine/native-game/mpe4_game.cpp`
- `engine/native-game/mpe4_package.h`
- `engine/native-game/mpe4_package.cpp`
- `engine/native-game/mpe4_render.h`
- `engine/native-game/mpe4_render.cpp`
- `engine/native-game/mpe4_session.h`
- `engine/native-game/mpe4_session.cpp`
- `engine/native-game/mpe4_firmware.h`

An exact Git comparison also found no changes from `e69b885` in the associated
M4G2 coverage:

- `tests/mpe3-title-native-harness.cpp`
- `tests/mpe4-firmware-native-harness.cpp`
- `tests/mpe4-game-native-harness.cpp`
- `tests/mpe4-game-preview.cpp`
- `tests/mpe4-render-harness.cpp`
- `tests/mpe4-session-arcada-harness.cpp`
- `tests/mpe4-session-game-harness.cpp`
- `tests/mpe4-session-kq1-harness.cpp`
- `tests/run-mpe4-firmware-native-harness.mjs`
- `tests/run-mpe4-game-native-harness.mjs`

Those checks retain M4G2 package/version enforcement, 1 MiB and 4 MiB cartridge
mapping, compact predecoded VIEW sidecars with checked raw VIEW fallback,
distinct Fastest/Fast/Normal scheduling, unchanged-frame suppression, and the
twelve-slot save format bound to game identity and compatibility epoch. The
firmware build's native conformance steps passed with those exact sources.

## Desktop validation

The immutable selected GUI snapshot passed all 302 C64 desktop tests and all
11 Teensy host tests. This coverage exercises the five-by-four browser layout,
parent-directory handling, modal file information and loading display,
left-to-right fill progress, F1 help, prompt icon selection, disk-image launch,
SID controls, and mouse input without renderer-state borrowing.

The same suites cover Copy, Paste, and permanent Cancel-first Delete; selection
and refresh behavior after file operations; the visible drag ghost and snap
grid; light/dark appearance with dots, dithered, and blank backgrounds; the
exclusive one-mouse/two-joystick port assignments; SD, USB, and internal-space
reporting; and the icon-based Control Panel with mouse, keyboard, and close-box
operation.

Snake, Calculator, and Text Viewer assemble as separate PRG payloads. The tests
verify that the firmware bundles their exact generated headers, that each app
loads into the shared application window only when launched, and that the apps
return to the resident desktop. Shared widget, dialog, bitmap, text, and input
services are exercised by the app integration tests.

These are deterministic source, host, assembly, manifest, conformance, and
linked-build results. They do not establish physical operation on a C64 or
TeensyROM+.

## Open physical acceptance

V1.0.19 has not yet been flashed and exercised on the target TeensyROM+ and
C64/128. Physical acceptance remains open for firmware update and reboot,
About-version confirmation, SD and USB browsing, file operations, disk launch,
SID selection, desktop dragging, Control Panel settings, loadable apps, and a
matching M4G2 cartridge including gameplay and save/restore.

Earlier user-confirmed updater behavior with V1.0.15 and the recorded
V1.0.17/R22 DOSVM cold-start and gameplay checks remain valid evidence for
those earlier artifacts. They are not counted as a physical pass for the
V1.0.19/native27 image.

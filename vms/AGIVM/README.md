# AGIVM — independent AGI module

Keep **firmware V1.1.1**. AGIVM requires no firmware change or reflash.
Extract [AGIVM.zip](../AGIVM.zip) to the SD root and launch `AGIVM.crt`.
The included `AGITEST.AGI` is a small diagnostic, not a Sierra game.

Place compiled games in `/VMS/AGIVM/GAMES/`, or select an `.AGI` file anywhere
on SD directly from the GUI. The CRT without a selected game opens the picker.
Up/Down selects; Left/Right changes page; Return or joystick-port-2 Fire runs.
The picker supports 128 names, 17 per page, and redraws changed rows without
blanking on selection movement. An invalid package shows a picker error.

**Save-slot details:** install the updated `engine.mvm` in `/VMS/AGIVM/`.
Both Save Game and Restore Game show all 12 slots with `Empty` or the saved
room number and score, for example `01  Room 12  Score 35`. Details refresh
whenever the dialog opens and describe the verified save that Restore will
load, including a recovered backup. Unusable existing saves show `Unavailable`.
The engine reads these values from the saved game, so no metadata migration,
game recompile or firmware flash is required for the labels.

**Selector input and dialog blink fixes:** for an existing installation, replace only
[`engine.mvm`](engine.mvm) at `/VMS/AGIVM/engine.mvm`. Firmware, CRT/client and
compiled `.AGI` games are unchanged. The idle picker now keeps input scanning
alive with frame-end packets, without resending bitmap cells. Release the
launch key/fire first; tap directions to move and release before another tap.

Ordinary centered dialogs now retain the high-resolution command-line strip
and update only changed cells when opening or closing. This avoids the previous
whole-screen blank caused by switching that strip off and on. Low/tall dialogs
which overlap the strip, full-screen inventory and authored screen-mode changes
still use the complete layout they require. The alarm-dialog hardware rerun is
pending; no game recompile or new firmware/client is required.

## Controls

In games, use the normal keyboard parser, Return to submit, DEL to edit, and
Run/Stop for Escape/the authored menu. Shift supplies the left/up cursor keys
and even-numbered function keys. The compiled C64 menus retain authored actions;
function behavior depends on the selected game's bindings. Joystick port 2
provides held directions and Fire. A 1351 mouse uses port 1 for movement/menu
input. Keyboard repeat starts after 20 video ticks, then every four ticks;
held launch keys must be released before game input arms.

Only one VM runs. Reset the C64/Teensy to return to the GUI. There is no GUI
reconstruction or module unload. New saves use `/VMS/AGIVM/SAVES/`, with 12
identity-checked slots. This is a fresh test format, not a legacy-save migration.

## Compile a game

`.AGI` is the actual standalone, checksummed M4G2 resource package, with original
game startup enabled. **Do not rename an old CRT to `.AGI`.** No title-bridge
cartridge, embedded engine or game-specific firmware is needed.

In Sierra Game Compiler **1.0.33 or newer**, select **MHS Power Engine (native
AGI)** and click **Make AGI**. Copy the resulting `.AGI` into
`/VMS/AGIVM/GAMES/`. Native VIEW pixels are checked against original artwork;
rebuild games produced by the earlier converter to correct corrupted graphics.

For command-line builds, use the same compiler sources and a matching profile:

```powershell
node agi/tools/build_agi_content.mjs --compiler-root E:/MHS-Repository/AGI-64 --profile E:/MHS-Repository/AGI-64/config/kq1-64.json --output build/agivm/private/KQ1.AGI
```

To rebuild all 16 configured games (14 original AGI games plus the SQ3 and
Colonel conversions) into a private SD folder, run:

```powershell
node agi/tools/build_agi_catalog.mjs --compiler-root E:/MHS-Repository/AGI-64 --out build/agivm/private/AGI-MPE-Enhanced-16
```

This writes `VMS/AGIVM/GAMES/`, a game manifest and checksums. Install the
shared AGIVM engine/client separately. Private game media is not included in
the public AGIVM download.

An optional `--source DIR` selects the original game directory. Otherwise the
profile's source directory is used. This command reads AGI-64 but does not
modify it. It preserves source protection policy, native VIEW preprocessing,
ego sprites/palette and standardized C64 menus. It runs original startup,
including for SQ1, rather than depending on the old prerendered title bridge.
Ordinary C64 compiler builds still output CRTs. MPE builds and this command
share the standalone `.AGI` builder. Existing AGIVM and firmware V1.1.1 need
no replacement for the graphics correction: replace only the game `.AGI`.

## Memory and test status

RAM1 contains engine code and support. RAM2 contains the 9,624-byte game state
and a paged game-resource cache; resources larger than 512 KiB remain SD-backed.
Rendering buffers, packet queues and cache metadata do not consume guest RAM2.
No PSRAM or flash module cache is used. The common 48 KiB stack stays reserved.

The actual module, content validation, generic launch, parser/editing, pointer,
held input, idle picker navigation, storage failure/roundtrip, packet immutability, and C64-client tests
pass. KQ1 reached room 1 and SQ1 room 2 during 1,200-frame module runs. PAL/NTSC
VICE boot and exact generated-display replay pass. These do not establish
hardware speed, long-running gameplay or physical crash acceptance.
See [detailed status](../../docs/Architecture/AGI-MODULAR-TEST-STATUS.md).

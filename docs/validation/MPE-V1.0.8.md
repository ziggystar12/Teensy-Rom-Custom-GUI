# MPE Firmware V1.0.8 validation

V1.0.8 / native16 restores Help and BASIC shortcut routing, adds a persistent
GUI / Text Original preference, and labels F2 BASIC and V TEXT in the browser
footer. Native AGI engine, cartridge and save formats are unchanged.

## Reported text symptoms

Executing the complete desktop PRG extracted from the immutable V1.0.6 HEX,
through its real overlapping loader, reproduces `?? ?ard`, `rEADING HEX FILE`,
`vERIFY`, and `cOPYING bUFFER`. The complete V1.0.7 PRG passes the same browser,
menu restore, affirmative update and serial WAIT flow, including final glyph
pixel checks. This establishes which released code reproduces the report; it
does not identify the firmware that was running on the physical machine.

The update progress screen is drawn by the desktop already resident before
flashing. Successful programming already reboots the Teensy, asserts C64 RESET,
loads the new compact menu, and releases RESET. Failed validation/preparation
returns to the existing desktop. No missing reset path was reproduced. Check
TEENSY > About MPE Firmware after reboot; if it still identifies an older
version, restart before evaluating the new UI.

Music-title capture had a separate remaining decoder: actual backend PETSCII
for `Death_Is No Evil` produced `dEATH_iS nO eVIL`. It now calls the shared
converter. Executed tests check case, punctuation, the 38-character bound,
metadata draining, final caption glyphs and enabled IRQs. This removes 11 bytes
of duplicate conversion code.

## Help and original menu

The GUI omits synthetic parent entries from its visible directory map. Fixed
PROGMEM shortcuts previously used raw indices in that filtered map: F1 selected
Desktop Shell instead of Help. Direct launches now use the raw map through both
directory and program selection, without persisting that temporary mode.
Thirty executed C64 launch paths cover Help, BASIC, Settings and Desktop Shell
against production backend maps, table identities and directory selection.
The previous public source reproduces the incorrect Help target.

The existing EEPROM-backed startup flags now use bit0 of PwrUpDefaults3 for
Text Original; zero remains the GUI default. Settings > Startup > E and V
preserve every other bit. Checks exercise 256 Settings key paths, 512 production
EEPROM write/reboot sequences, all 256 compact and expanded startup flag values,
and ordinary/shifted V transitions. Actual Settings text stays on row 20 above
its navigation footer. Explicit Help/Settings/BASIC launches do not change the
saved preference. Settings source and its generated PRG are now required,
compiled and compared in the release snapshot and final embedded-image audit.

The seven browser hints fit within 320 pixels. Executed glyph-position and 160 horizontal mouse-coordinate checks cover all labels and gaps, including
V TEXT beyond pixel 255. The rendering preview uses assembled production code.

## Release scope and reproducibility

This firmware uses the proven 37-patch native AGI release build. Ongoing native
DOS prototype work on main is preserved and excluded from this public image.
The source lock names the exact release commit: the committed manifest must
match the local manifest byte for byte before its recorded build tools are
verified from Git blobs. Existing firmware, engine, patch, backend, selected
GUI and configuration hash checks remain in force.

No physical C64/Teensy has been flashed or controlled here. Final hardware
acceptance remains open for menu launch/return, preference persistence,
post-update version/text appearance, pointer motion and SID continuity.

## Source checks and payload sizes

All 276 desktop/backend/SID checks passed without skips. Asset regeneration
also matches the reviewed font/icon inputs. Desktop code uses 22,499 of 22,528
bytes; resident apps use 4,080 of 4,096. The compact cartridge is 7,559 bytes,
its contained menu 7,383 bytes, desktop PRG 26,698, Help 2,631, and Settings 6,270.
All four generated headers match their assembled programs. The selected GUI
source checkpoint is `0c4bf886ab72e1fed5e0582f9908f84d7999c5ad`; its 129-file
snapshot digest is `7453b4facf0588128de889f5c787d9107975b7d90f077a4f11042858845f6dc4`.

Backend apply/exact-byte/reverse verification passes for all 20 reviewed files.
The backend patch SHA-256 is
`65b9ecd7d89b7c2cb5f287442b1db968a711c0c8d08f9e89836e5c000949f236`.

## Final combined firmware

`MPE_Firmware-V1.0.8.hex`: 6,192,712 bytes. SHA-256:
`572f2bb7d01cbcf7ceb3962210baa5cb62ad8ce396f974da6c89357430491b68`.
The final-image audit checks both ARM images, 544 audited inputs, all 129 snapshot files, all four
embedded C64 programs, and all nine unchanged native AGI sources. MinimalBoot
retains 16,416 bytes of stack reserve and 271,488 bytes of RAM2 heap reserve.
The extracted V1.0.8 desktop PRG also passes the complete text-flow replay:
SD Card, 16 captured filenames, cached menu restore, and all seven actual updater
messages, with exact published glyph pixels.

The integrated native module passes both sprite and legacy bitmap runs using
unchanged SQ1 data. The sprite run completes 862 frames, 1,184 packets, 350 inputs, 64 direction reversals, 88 sprite packets and 44 sprite commits with zero input
interrupt masks. Both runs pass five SAVES directory checks, eight restore/
fallback checks and six injected transaction failures, with no root mutations.
The Black Cauldron demo remains unchanged:
`a235a7925332df82dc30e9c966c7038d75b70294e8294330236346e316ff2d7c`.

Local proof is retained under `build/native16-final-proof`:

- `artifact-audit.json`: `0f81c428636a197272e849a6a3de2226ffa1ec4f1dc6a8f8a651370b28bbc884`.
- `firmware-native-result.json`: `d31d3244b766ca3583ee8f3b356bf96899bce668bdeb1db6ad59af17a79f6792`.

Source tests and exact released-HEX text replay logs are retained under
`build/native16-ui-proof`; backend verification is under
`build/native16-backend-proof`. The immutable [native16 manifest](../../releases/native16/manifest.json)
records all release inputs and payload checksums.

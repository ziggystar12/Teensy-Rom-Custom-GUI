# Native MHS Power Engine firmware

Use [MHS-PowerEngine-TRPlus-v1_full.hex](MHS-PowerEngine-TRPlus-v1_full.hex)
for native MPE game cartridges and the current desktop. The **native08** image
combines the native07 AGI engine with Custom GUI revision
`ac4a5d6ce3d8037d4fdd7eee58899b9bc7463b3e`, including the desktop apps and
Copy, Paste, and permanent Delete for individual files on SD and USB.

It retains native07's corrected key waits, including KQ1's full-screen King Edward
speech. The Return used to submit a command no longer dismisses the speech
before it appears. A fresh key or click continues it.

Existing native06 and native07 cartridges and saved games remain compatible.
No cartridge rebuild is needed. Launch the
CRTs from the **SD card**. Read [the firmware guide](MHS-POWER-ENGINE.md) for
installation, controls, saves and the included
[official restore image](TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex).

Firmware SHA-256:

`716bbaa67074da087787f2e4cb912f3a0c35cc3f8e8ff457b7ee75a4dffcdf16`

[Checksums](SHA256SUMS.txt), [source lock](source.lock.json), and
[release manifest](native08-manifest.json) identify the exact delivered files.

Engine source: [6ea55cc](https://github.com/ziggystar12/teensyrom-plus/tree/6ea55ccab1bbda9d077dbe8162f43d0f7abf6283).

The fresh dual firmware build and final artifact audit passed: both linked
images match the combined HEX, and the embedded GUI assets match the selected
source. The native harness passed 132 intro visits, 732 gameplay frames and
285 inputs, including save and input checks. All seven release/provenance
checks pass. This image has not been flashed here; physical C64/128, SD/USB
and mouse acceptance remain separate from these host checks.

Read [File Operations](FILE-OPERATIONS.md) for the included desktop controls.
The [original Desktop Apps notes](DESKTOP-APPS.md) document the earlier GUI-only
release. The combined native08 image replaces the separately paired GUI builds.

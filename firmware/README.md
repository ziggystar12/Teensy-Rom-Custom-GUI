# Native MHS Power Engine firmware

Use [MHS-PowerEngine-TRPlus-v1_full.hex](MHS-PowerEngine-TRPlus-v1_full.hex)
for native MPE game cartridges. This **native07** release combines the native
AGI engine with selected Custom GUI revision `e305f6dc24c526b1e337e9718fbb71d599ed70d8`.

Native07 fixes authored key waits, including KQ1's full-screen King Edward
speech. The Return used to submit a command no longer dismisses the speech
before it appears. A fresh key or click continues it.

Existing native06 cartridges and saved games remain compatible. Launch the
CRTs from the **SD card**. Read [the firmware guide](MHS-POWER-ENGINE.md) for
installation, controls, saves and the included
[official restore image](TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex).

Firmware SHA-256:

`f43f5f26832575ba596fd7730d77a48b5d92b7775d61e126b1b285f4f0d2628f`

[Checksums](SHA256SUMS.txt), [source lock](source.lock.json), and
[release manifest](native07-manifest.json) identify the exact files and
[engine source at eab8d7b](https://github.com/ziggystar12/teensyrom-plus/tree/eab8d7b33e8c08f110feb99c3866a73de05e2262).
The build and native tests passed; these files have not been flashed here.

The existing
[Desktop Apps firmware](TeensyROM+_0.8.0.4_CustomGUI_DesktopApps_full.hex)
and [its original documentation](DESKTOP-APPS.md) remain available separately.

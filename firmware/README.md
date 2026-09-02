# Native MHS Power Engine firmware

Use [MHS-PowerEngine-TRPlus-v1_full.hex](MHS-PowerEngine-TRPlus-v1_full.hex)
for native MPE game cartridges. This **native06** release combines the native
AGI engine with selected Custom GUI revision `e305f6dc24c526b1e337e9718fbb71d599ed70d8`.

Launch matching native CRTs from the **SD card**. Larger games use the native
4 MiB cartridge extension. Read [the firmware guide](MHS-POWER-ENGINE.md) for
installation, controls, saves and the included
[official restore image](TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex).

Firmware SHA-256:

`2197a34023237202c4c68be8cc908f2c509d5b99c853b4a9e1e153d2ba97acdf`

[Checksums](SHA256SUMS.txt), [source lock](source.lock.json), and
[release manifest](native06-manifest.json) identify the exact files and
[engine source at 5be5620](https://github.com/ziggystar12/teensyrom-plus/tree/5be56200d709fadec17739be13b3686b76a873c7).
The build and native tests passed; these files have not been flashed here.

The existing
[Desktop Apps firmware](TeensyROM+_0.8.0.4_CustomGUI_DesktopApps_full.hex)
and [its original documentation](DESKTOP-APPS.md) remain available separately.
The native image uses the selected e305 GUI recorded above; this checkout's
later Desktop Apps source remains separate.

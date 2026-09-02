# Native engine tests

These tests compile and execute the real native C++ engine. They accept game packages and cartridge fixtures supplied by the caller; this repository does not include game data or test cartridges. Generated output belongs in an external directory or the ignored `build/` directory. A C++17 compiler and Node.js are required. On Windows the runners can use MinGW; pass `--compiler` or set `CXX`/`MPE4_CXX` when it is installed elsewhere.

Run these commands from the repository root, replacing the fixture and output paths.

```powershell
node tests/run-mpe4-game-native-harness.mjs --package D:\Fixtures\SQ1-game.bin --output D:\Proof\core
node --test tests/teensyrom-plus-firmware-mpe3-boot.test.mjs
```

The first command covers startup, parser, menus, controller delivery, movement, object reuse, save/restore, timing and message expansion. It records the package and tested source hashes. The second compiles the boot selection and guarded launch code extracted from the maintained patch; it requires no game data. Set `MPE3_BOOT_TEST_OUTPUT` to choose its output directory.

The real Session playthroughs reconstruct every published C64 cell and compare it with the native renderer. They use actual input events and the game's original scripts. The SQ1 full route continues through the winning scene and all closing credits; the KQ1 smoke covers its own title, Room 1, keyboard movement, LOOK ROOM and mouse movement. Save/restore uses an in-memory storage fixture; these runners do not claim physical SD or C64 timing acceptance.

```powershell
node tests/run-mpe4-session-arcada.mjs --raw D:\Fixtures\SQ1-cartridge.bin --out D:\Proof\arcada
node tests/run-mpe4-session-game.mjs --raw D:\Fixtures\SQ1-cartridge.bin --out D:\Proof\complete-game
node tests/run-mpe4-session-game.mjs --raw D:\Fixtures\SQ1-cartridge.bin --out D:\Proof\pickup --pickup-command "take cartridge"
node tests/run-mpe4-session-kq1.mjs --raw D:\Fixtures\KQ1-cartridge.bin --out D:\Proof\kq1
```

The integrated firmware harness takes a patched build source tree from `scripts/build-firmware.ps1`, checks it against `engine/native-game`, compiles the actual IO2 service and emits a packet trace. The artifact audit checks the complete HEX, linked images, memory guards, GUI provenance and the native source proof. All six fixture paths and the ARM tool directory are explicit.

```powershell
node tests/run-mpe4-firmware-native-harness.mjs --source D:\Build\source --intro D:\Fixtures\SQ1-intro.bin --raw D:\Fixtures\SQ1-cartridge.bin --out D:\Proof\firmware
node tests/run-mpe4-firmware-artifact-audit.mjs --source D:\Build\source --build D:\Build --native-result D:\Proof\firmware\firmware-native-result.json --intro D:\Fixtures\SQ1-intro.bin --raw D:\Fixtures\SQ1-cartridge.bin --arm-tools D:\Toolchain\arm\bin --out D:\Proof\firmware-audit.json
```

The earlier standalone MPE3 intro/skip service can be checked with `run-mpe3-title-native-harness.mjs --asset D:\Fixtures\SQ1-intro.bin --output D:\Proof\intro.json`. Its optional `--trace-prefix` writes length-prefixed packets for the C64 presenter test in AGI-64. The MPE3 runner uses the historical intro-only patch boundary; use the integrated MPE4 harness for a current full-game source tree.

Two visual reference checks intentionally span repositories. They read the explicitly named AGI-64 checkout for its host decoders; they do not copy or modify it. The renderer test also requires the original SQ1 AGI resource directory. Its reference font is supplied by the AGI-64 checkout's configured VICE character ROM.

```powershell
$env:AGI64_SOURCE_ROOT='D:\Source\AGI-64'
$env:SQ1_SOURCE_DIR='D:\Games\SQ1'
$env:MPE4_RENDER_TEST_OUTPUT='D:\Proof\renderer'
node --test tests/mpe4-native-render.test.mjs
node tests/run-mpe4-game-preview.mjs --package D:\Fixtures\SQ1-game.bin --agi-root D:\Source\AGI-64 --output D:\Proof\preview
```

The renderer proof compares all 73 SQ1 pictures and overlays, 1,652 VIEW cels, the 132 intro frames, and moving-actor/color cases. AGI-64 retains its cartridge/pack, intro host, terminal/keyboard and 6510 presenter tests. Those host tests do not import this repository's engine source. The packet replay bridge accepts this repository's generated wire trace explicitly.

The native06 cartridge test exercises the actual MinimalBoot parser, upper-bank
page resolver and logical reader. It requires no game fixture for its complete
4 MiB synthetic image, malformed-container checks, legacy limit checks and
reserved-bank crossing tests. Optional paired CRT/raw inputs additionally
verify every indexed page and every native resource CRC through that reader.

```powershell
node tests/run-mpe4-cartridge-harness.mjs --source D:\Build\source --out D:\Proof\cartridge
node tests/run-mpe4-cartridge-harness.mjs --source D:\Build\source --out D:\Proof\large-game --crt D:\Fixtures\game.crt --raw D:\Fixtures\game.bin
```

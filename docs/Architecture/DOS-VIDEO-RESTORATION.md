# DOS video restoration — September 5, 2026

Historical rollback evidence. The subsequent [F5 opt-in update](DOS-F5-OPT-IN.md)
retains this original default renderer/transport and adds explicit F5 selection;
current engine/client downloads are no longer byte-identical rollback binaries.

## Scope and reason

After V1.1.7, physical DOS testing reported a long black/static interval after
POST, eventual arrival at C:, and static when starting Monkey Island in Tandy
mode. All shared mode shortcuts failed to recover the game. Full/update ZIP
runtime files were verified identical, excluding that packaging distinction.

Git comparison showed that `b59c1dd` replaced both the established converter
and dirty-CELL/SID transfer path with the shared indexed/DMA pipeline. It also
changed the Tandy 09h default from 320-wide hires to multicolor. A diagnostic
boot run observed mode 6 during BIOS port initialization before CGA80 restores
mode 3. This does not prove the exact electrical/timing cause of the static.

Restore the complete previously working DOS path instead of claiming a color
selection change fixes a transport failure. `vm/dos/dosvm.cpp` and both DOS
client generators are restored from `ef9cc9114fadcf00a64e399b110744a5b84d5696`.
The DOS overlay explicitly disables the newer shared generator's timing
publication default, reproducing the previously shipped non-indexed client.
The executable engine, client, BIOS and manifest must reproduce that Git
baseline byte for byte. No BIOS, CPU timing, guest RAM, sound or save behavior
is changed. In particular the GRAPHSET compatibility-save fix is retained.

DOS alone temporarily leaves shared indexed video: Tandy 08h keeps double-wide
multicolor, Tandy 09h keeps automatic hires, and Ctrl+Commodore+F7 again toggles
Sharp for CGA 4/5. F1/F3/F5 shared selectors are withdrawn from DOS. The intended
shared service remains available to NES/GB; re-enabling DOS requires matching
these defaults and physical transport/frame-completion acceptance.

## Reproduction and release gates

Use `MPE_VM_TEST_OUT=build/dos-restore`, then:

1. `node scripts/build-vm-test.mjs dos-module`
2. `node scripts/verify-dosvm.mjs --restore-video [optional private GRAPHSET.EXE]`
3. `node scripts/publish-dos-restoration.mjs`

The verifier checks the exact pre-port binaries, image bounds, real FreeDOS
boot, 512 KiB guest RAM, workspace guards, immutable packets, C:/D: writes,
twenty repeated compatibility saves, both Tandy guest modes and return to text.
It captures the real module's packets and executes the generated 6510 receiver,
comparing all 1,000 Tandy cells, VIC modes/background/bank and visible text
return. It additionally exercises input, twelve packet-retry cases, CGA/Tandy
conversion, BIOS/PSG behavior, Boulder, and PAL/NTSC reset/START in VICE.

The C64 mailbox/raster replay is a model, not physical cartridge timing proof.
The matching old engine/client provides stronger restoration evidence than a
new visually similar converter, but the new SD update still needs a hardware
retest of prompt timing, Monkey Island/Might and Magic Tandy output and sound.

The publisher replaces only DOS downloads. Firmware V1.1.7 and all other VM
packages remain untouched. `DOSVM-update.zip` contains no C: image or D: files;
use it for existing installations. `DOSVM.zip` remains a fresh-install kit.
Do not reflash firmware for this restoration.

## Verified restoration results

All gates above passed in `build/dos-restore/dos-video-restoration.json`,
including supplied GRAPHSET C: Tandy and D: Tandy/CGA/Tandy persistence.
The receiver replay consumed 6,272 actual module packets, with all 1,000 cells
matching in both Tandy modes and zero quiet retries. The broader input check
passed 61 snapshots; all twelve retry-fault cases passed.

- Engine: 89,648 bytes; SHA-256 `e994c7d313799acd5b23c91faccfe528607bdc1e4b25dfe95e16944bc3174b55`.
- Client: SHA-256 `ed1cb157dcc39739243c8ca5bafcffb48532d069b012168b0eb8b78f5d9d2238`.
- Measured workspace: 175,464 / 187,680 bytes; guest RAM remains 524,288 bytes.
- Disk-free update: 93,474 bytes; SHA-256 `c0932d5883b53606763660f26addbe4e48b40936ec2bebd0d8e331ed5b6d1791`.

Both executable hashes equal the previously shipped Git baseline. Firmware
V1.1.7 and NESVM/GBVM/AGIVM ZIP hashes are unchanged. Physical retest remains open.

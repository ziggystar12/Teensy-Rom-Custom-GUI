# TeensyROM Custom GUI file operations

[TeensyROM+_0.8.0.4_CustomGUI_FileOps_full.hex](TeensyROM+_0.8.0.4_CustomGUI_FileOps_full.hex)
is the GUI File Operations build for TeensyROM+ Fab0.4. Install the complete
image: it pairs the updated C64 desktop with its Teensy file-operation backend.
The compact/classic recovery menu and existing confirmed firmware updater
remain available.

This build adds Copy, Paste, and permanent Delete for individual files on SD
and USB. It retains the desktop apps, Drive 8/9 browsing/launching, 1351 mouse
on port 1, joystick on port 2, and keyboard controls. The home desktop has eight
icons and no Trash.

## Copy and Paste

1. Open an SD or USB folder and select one file.
2. Choose Edit > Copy or press Shift+C. Dismiss the clipboard confirmation.
3. Open the destination SD or USB folder, then choose Edit > Paste or Shift+P.

Copy stores the source path and filename in Teensy RAM; Paste reads the file
contents. The clipboard is not persistent and is lost when the Teensy firmware
restarts. Pasting across SD and USB is supported. Existing destination names
are always rejected; there is no overwrite option or automatic rename.

Paste writes in bounded chunks to a private temporary file, then reads it back
to verify size and CRC before publishing the final filename. Its progress
dialog accepts STOP, Escape, or Cancel. A normal error or cancellation removes
the incomplete copy. A power interruption or disconnected device can leave a
`.tr-copy-*.tmp` file when cleanup cannot finish; it can be deleted after the
device is available again.

## Delete

Choose File > Delete or press Shift+D. The desktop shows the captured filename
and asks whether to delete it permanently. **Cancel is selected initially.**
Press Y, click Delete, or select Delete with the arrows and press Return/fire
to confirm. STOP, Escape, N, or Cancel cancels.

The backend uses the prepared full path and rechecks its size/modification
metadata before deletion. The dialog cannot switch to another browser item
while awaiting confirmation. A deleted clipboard source clears the clipboard.
Deletion is permanent: there is no Trash folder, recover command, or persistent
recovery data.

## Storage scope

- Regular files in ordinary SD/USB directories are supported, including whole
  D64/D71/D81 images and `.hex` files. Copying `.hex` data does not run the updater.
- Filenames must use printable ASCII (bytes 32 through 126) so the complete
  target can be displayed before deletion.
- Directories, disk-image contents, built-in Teensy files, and IEC Drive 8/9
  writes are unsupported. Rename, Cut/Move, and New Folder are not included.
- Read/write failures, missing devices, conflicting names, and verification
  failures appear in the dialog. Paste/delete success refreshes the folder.

## Firmware pairing and validation

The [native07 MHS Power Engine kit](README.md) remains a separate release with
GUI revision `e305f6d` and engine revision `eab8d7b`. Its HEX, source lock, and
manifest do not describe this GUI build. Integrating these changes into native
MPE requires a new combined engine build.

See [SHA256SUMS.txt](SHA256SUMS.txt) for the release checksum and
[CUSTOM-DESKTOP.md](../docs/CUSTOM-DESKTOP.md) for the desktop contract. Build
the matching menu with `scripts/build-c64-menu.ps1` before building full
firmware with `Source/Teensy/tools/Build-DualBoot.ps1 -Fab04_Features`.

Release validation passed 156 automated checks, including 36 storage fault
scenarios and assembled C64 input/dialog/glyph checks, plus AGI protocol
conformance and both firmware builds. The combined HEX matches both compiled
halves, and its embedded menu headers match the freshly assembled binaries.
The desktop payload uses 21,120 of 22,528 bytes. The linked main and MinimalBoot
stack reserves are 26,176 and 20,832 bytes respectively.

Host fault-injection tests, assembled C64 checks, and firmware builds do not
replace physical C64/128, SD/USB, or mouse testing. This version still needs
real-hardware acceptance. The
[official restore image](TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex) remains available.

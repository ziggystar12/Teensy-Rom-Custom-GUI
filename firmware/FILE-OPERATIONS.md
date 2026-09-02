# TeensyROM Custom GUI file operations

[MHS-PowerEngine-TRPlus-v1_full.hex](MHS-PowerEngine-TRPlus-v1_full.hex)
is the combined native08 image for TeensyROM+ Fab0.4. Install the complete
image: it pairs the updated C64 desktop and Teensy file-operation backend
with the native07 MHS AGI engine.
The compact/classic recovery menu and existing confirmed firmware updater
remain available.

The desktop provides Copy, Paste, and permanent Delete for individual files on SD
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

The [native08 MHS Power Engine kit](README.md) combines these file operations
from GUI revision `ac4a5d6ce3d8037d4fdd7eee58899b9bc7463b3e` with the native07
AGI engine and its corrected dialogue key waits. Existing native06 and native07
cartridges and saved games remain compatible. Native game cartridges launch
from SD only, even though desktop file operations support both SD and USB.

See [SHA256SUMS.txt](SHA256SUMS.txt) for the release checksum,
[native08-manifest.json](native08-manifest.json) for the combined image's source
and memory records, and [CUSTOM-DESKTOP.md](../docs/CUSTOM-DESKTOP.md) for the
desktop contract. The combined image is built by
[`scripts/build-firmware.ps1`](../scripts/build-firmware.ps1) in this repository,
using the pinned GUI snapshot.

The earlier GUI-only release's test counts and stack reserves describe that
historical build. Use the [firmware release notes](README.md) and native08
manifest for the combined image's verification status and exact build records.

Host fault-injection tests, assembled C64 checks, and firmware builds do not
replace physical C64/128, SD/USB, or mouse testing. This version still needs
real-hardware acceptance. The
[official restore image](TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex) remains available.

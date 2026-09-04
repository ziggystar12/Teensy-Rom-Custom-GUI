# TeensyROM Custom GUI file operations

[MPE_Firmware-V1.0.12.hex](../firmware/MPE_Firmware-V1.0.12.hex)
is the combined V1.0.12 / native20 image for TeensyROM+ Fab0.4. Install the complete
image: it pairs the updated C64 desktop and Teensy file-operation backend
with the native MHS Power Engine, ego sprites, and restart/menu input fixes.
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

The [current MHS Power Engine kit](../firmware/README.md) combines these file operations
with a four-by-four scrolling browser, shared bitmap dialogs, and native
AGI ego sprites. V1.0.12 retains the current game packages and save identities.
The earlier V1.0.1 menu adaptation changed save identities; keep pre-V1.0.1
saves with their matching cartridges. Native game cartridges launch
from SD only, even though desktop file operations support both SD and USB.

See [SHA256SUMS.txt](firmware/SHA256SUMS.txt) for the current download checksums,
[the native20 manifest](../releases/native20/manifest.json) for the combined image's source
and memory records, and [CUSTOM-DESKTOP.md](CUSTOM-DESKTOP.md) for the
desktop contract. The combined image is built by
[`scripts/build-firmware.ps1`](../scripts/build-firmware.ps1) in this repository,
using the pinned GUI snapshot.

The earlier GUI-only release's test counts and stack reserves describe that
historical build. Use the [firmware release notes](../firmware/README.md) and native20
manifest for the combined image's verification status and exact build records.

Host fault-injection tests, assembled C64 checks, and firmware builds do not
replace physical C64/128, SD/USB, or mouse testing. This version still needs
real-hardware acceptance. The
[official restore image](../releases/native20/TeensyROM+_0.8_OFFICIAL-RESTORE_full.hex) remains available.

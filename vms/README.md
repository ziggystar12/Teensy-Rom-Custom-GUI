# Downloadable VMs

Install the current [generic firmware](../firmware/) once, then add the VM
packages you want. Each ZIP extracts to the SD root with a launcher and its
`VMS/<VM-name>/` support folder. The repository folder `vm/` contains source;
this `vms/` folder contains ready-to-copy packages.

| Package | Status |
| --- | --- |
| [NESVM.zip](NESVM.zip) / [files](NESVM/) | Nofrendo; full speed reported in F1/F7. V1.1.7 retains the F5 changed-area cadence candidate. Includes relinkable source. |
| [DOSVM.zip](DOSVM.zip) / [files](DOSVM/) | Original Tandy/CGA default plus explicit F5 Enhanced-25; F1 returns to original. Unchanged V1.1.7 firmware. Use [DOSVM-update.zip](DOSVM-update.zip) to preserve existing drives. |
| [GBVM.zip](GBVM.zip) / [files](GBVM/) | GB/GBC up to 512 KiB; adds 8 KiB battery RAM for Mario 2 and original Zelda. Kirby/Mario 1/Pac-Man regression-tested. Centered four-shade GB output; no games included. |
| [AGIVM.zip](AGIVM.zip) / [files](AGIVM/) | ABI 2, unchanged V1.1.1 firmware. Standalone `.AGI`, picker, original interpreter, sprites, SID, keyboard/joystick/mouse. Hardware-test candidate. |

Do not overwrite private ROMs or existing writable disk images when updating.
No commercial NES ROMs are included. Reboot to return to the GUI.

Use **V1.1.7** for this combined release. Replace the DOS/NES clients and engines
together while preserving existing disks and ROMs. All engines occupy the
same memory windows at different times, never simultaneously.

Replace GBVM to obtain the battery-cartridge update; keep its ROMS/SAVES folders.
AGIVM does not need replacing. AGI retains its
existing video solution and function-key behavior. DoomVM requires V1.1.7's
optional RAM2 constant-table profile; its supplied-game-data kit remains local.

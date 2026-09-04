# Downloadable VMs

Install the current [generic firmware](../firmware/) once, then add the VM
packages you want. Each ZIP extracts to the SD root with a launcher and its
`VMS/<VM-name>/` support folder. The repository folder `vm/` contains source;
this `vms/` folder contains ready-to-copy packages.

| Package | Status |
| --- | --- |
| [NESVM.zip](NESVM.zip) / [files](NESVM/) | ABI 2 RAM1/RAM2 split. Earlier baseline launched SMB; severe slowdown and visual-quality work remain. |
| [DOSVM.zip](DOSVM.zip) / [files](DOSVM/) | ABI 2. Full 512 KiB guest RAM; CGA/Tandy/80-column, keyboard, speaker/PSG, writable C:/D:. Hardware-speed test candidate. |

Do not overwrite private ROMs or existing writable disk images when updating.
No commercial NES ROMs are included. Reboot to return to the GUI.

These packages require **V1.1.1**. When updating from V1.1.0, replace NESVM's
client and engine together; the RAM1/RAM2 ABI changed. Both engines occupy the
same memory windows at different times, never simultaneously.

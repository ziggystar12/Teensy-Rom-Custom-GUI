# Downloadable VMs

Install the current [generic firmware](../firmware/) once, then add the VM
packages you want. Each ZIP extracts to the SD root with a launcher and its
`VMS/<VM-name>/` support folder. The repository folder `vm/` contains source;
this `vms/` folder contains ready-to-copy packages.

| Package | Status |
| --- | --- |
| [NESVM.zip](NESVM.zip) / [files](NESVM/) | V1.1.4 emulation timing fix, centered F7, indexed video modes and corrected picker. Physical speed/quality retest pending. |
| [DOSVM.zip](DOSVM.zip) / [files](DOSVM/) | ABI 2. Full 512 KiB guest RAM; CGA/Tandy/80-column, keyboard, speaker/PSG, writable C:/D:. Hardware-speed test candidate. |
| [AGIVM.zip](AGIVM.zip) / [files](AGIVM/) | ABI 2, unchanged V1.1.1 firmware. Standalone `.AGI`, picker, original interpreter, sprites, SID, keyboard/joystick/mouse. Hardware-test candidate. |

Do not overwrite private ROMs or existing writable disk images when updating.
No commercial NES ROMs are included. Reboot to return to the GUI.

Use **V1.1.4** for NESVM's timing correction; existing DOS/AGI require V1.1.1 or newer. Replace
NESVM's client and engine together. All engines occupy the
same memory windows at different times, never simultaneously.

Update to [V1.1.4 firmware](../firmware/) AND the current NESVM.zip for the
timing correction and centered F7. The NES engine has changed since V1.1.3.
DOS/AGI packages do not need replacing; AGI retains
its existing video solution and function-key behavior.

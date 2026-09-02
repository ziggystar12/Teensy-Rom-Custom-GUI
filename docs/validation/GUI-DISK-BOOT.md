# Disk boot validation finding

File > Boot Disk preserves `LOAD "*",device,1` semantics. On a Commodore 1541,
bare `*` can select the last program accessed; it selects the first program
when none has been loaded. The header preflight does not reset the drive, so
hardware acceptance should cover both cold and previously used drives.
See the [Commodore 1541 user manual](https://www.commodore.ca/wp-content/uploads/2018/11/commodore_vic_1541_floppy_drive_users_manual.pdf).

Executed 6502 checks cover devices 8/9, wildcard preflight failures, selected
IEC folders/images, BASIC and machine-code handling, File/Shift+RUNSTOP routing,
unsupported-source notices, and modal dismissal without a hidden launch.
These checks stub KERNAL disk calls and do not emulate the drive's DOS state.

# DOSVM MPE3 terminal provenance

`build/dos-work/dosvm-terminal.prg` and
`build/dos-work/dosvm-bootbank.bin` are generated C64 launch artifacts.
They are not copied from an SQ1 cartridge. Build them with
[`../tools/build_dosvm_terminal.mjs`](../tools/build_dosvm_terminal.mjs), which
uses the shared AGI-64 sources:

- `E:\MHS-Repository\AGI-64\host\mpe3-title-terminal.mjs`
- `E:\MHS-Repository\AGI-64\host\install-boot-bank.mjs`

The generator selects the generic M3 transport, enables the keyboard route,
and disables the 1351 mouse route. `build/dos-work/dosvm-terminal.json`
records the exact diagnostic title, footer, source hashes, and output hashes.
The stable `dos/tools/build_dos_test.ps1` workflow generates these artifacts
and publishes the tested kit to `DosTest/`. The CRT's internal
`SQ1 MPE3 TITLE PULL` field remains only as the firmware's existing native
launcher identifier; it is not presented to the player.

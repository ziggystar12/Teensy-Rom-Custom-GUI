# NES video modes — V1.1.2

Built on main baseline `b5167faf29933b3eaf09d5706f9648f6fae3abcd`.
The matched `build/vt` V1.1.2 build and full verification completed successfully.
Published download SHA-256 identities:

- Firmware: `4832906650534345d697d5f49249ef01ba7f19fa3008a50ba7e5466672b03285`
- NESVM.zip: `c6d693b1ef3bddd4869cbdfb5dab1889f25e826aa1337d487e969f11f0fd87b1`

Includes the V1.1.1 fast-DMA/cooperative-scheduler baseline and the repaired
idle picker, cursor/Shift controls, Return/Fire launch and held/release logic.
This is a matched firmware + NES engine + C64 client update. Existing DOS and
AGI packages are not replaced. AGI does not enter the new video converter.

Hold **Commodore + Control** and use the unshifted physical function keys:

| Key | Direct selection |
| --- | --- |
| F1 | Default: ordinary wide-pixel multicolor for NES |
| F3 | Auto-8: automatic enhancement in at most eight useful 8-line bands |
| F5 | Enhanced-25: automatic enhancement in every useful band |
| F7 | Sharp: 320x200, two colors per 8x8 cell |

No game-by-game settings or ROM identities are used. Firmware accepts the
NES's native indexed pixels and palette, scales and converts them, selects
band splits, and owns presentation. The picker retains sharp text independently
of the selected gameplay mode. F1/F7 retain the fast steady-frame DMA path.

F3/F5 are experimental reduced two-screen FLI. They allow a different color
pair above/below one shared split in each selected 8-line band, not arbitrary
four-color placement per cell. They require pause/transfer/resume, can be slower
or flicker, and have the VIC-II's leftmost 24-pixel rescan artifact. F1/F7 are
the quick fallback. The physical Ctrl+Commodore+F3 matrix is ambiguous with
Ctrl+Commodore+Cursor Right; that pattern selects Auto-8 and consumes Right.

## Software gates

- Actual NES module: 40-ROM paging, idle progress, immutable pending frames,
  all four firmware-resolved modes, Return/Fire and 120 Crossbow frame ends.
- Actual indexed host: configuration/range rejection, frozen Busy lifecycle,
  pause/DMA/resume, frame-boundary modes, direct Color/Sharp and failure release.
- Actual C64 client: picker input replay; exact chords, held/release, either
  Shift exclusion, multi-F-key rejection and F3 ghost suppression.
- VICE PAL and NTSC: three full-plan and three mixed-plan frames per standard;
  2,412 checked raster writes plus decoded screenshot bitmap-row/color checks.
  Mixed plans exercise all seven split positions and unenhanced bands.
- Matched firmware/module link and ABI checks; no engine embedded in firmware,
  no host RAM2 globals, and a 49,152-byte host stack reservation.
- Existing DOS module/storage/Tandy/audio, both clients' reset/START/timeout,
  NES core and focused GUI regressions.

## Physical acceptance still required

Flash V1.1.2, reboot, confirm About, and install the matching NES client and
engine together. Preserve ROMs. Check joystick/cursors/Return in the picker;
then compare a moving game in F1, F3, F5 and F7. Test held directions and sound
while changing modes, modifier-first release, return to picker, and GUI reset.
Check PAL/NTSC stability and decide whether enhanced-mode artifacts/cadence
are worthwhile. Software tests do not establish physical speed or playability.

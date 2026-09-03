# DOS terminal Latin font

The terminal uses the public-domain 128-character `font8x8_basic` table by
Daniel Hepper, based on Marcel Sondaar/IBM public-domain VGA fonts. Its
notice is retained in `engine/native-dos/mpe5_font8x8.h`.

The source was already bundled in this workspace at
`E:/MHS-Repository/HamsterOS/apps/zipzork_font8x8.inc`. Each source row was
bit-reversed from bit 0 on the left to the VIC-II's bit 7 on the left.
All eight rows are preserved. No drawing, scaling, or case conversion occurs
in the firmware. The generated firmware source manifest records its hash.

The table covers basic Latin/ASCII, including lowercase and all printable
punctuation. Extended CP437 glyphs above byte 127 remain a later milestone.

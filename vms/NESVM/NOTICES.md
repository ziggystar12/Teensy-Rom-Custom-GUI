# NESVM source and third-party notices

Module source: https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/tree/main/vm/nes
Portable engine: https://github.com/ziggystar12/Teensy-Rom-Custom-GUI/tree/main/engine/native-nes

The active NESVM CPU/PPU are Nofrendo, copyright (c) 1998-2000 Matthew Conte,
ported from Jean-Marc Harvengt's MCUME Teensy41 version at commit
27f6b906aca34e06d6647bdca8215e25f8d20aa5. Nofrendo and the MPE core adapter
are distributed under version 2 of the GNU Library General Public License.
See LICENSE-Nofrendo.txt and the full corresponding module sources/rebuild
instructions in SOURCE/ inside NESVM.zip. This is a modified MPE port, not
an unmodified MCUME release; it retains MPE input, SID and indexed video.

The source/reference core retains Andre Weissflog's m6502 from chips, under
the zlib/libpng license; it is not the linked runtime CPU in this package.
Copyright (c) 2018 Andre Weissflog.

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is
   not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

The font8x8_basic font by Daniel Hepper, derived from Marcel Sondaar/IBM fonts,
is public domain. Crossbow is the authorized repository demo; it is not placed
under the engine's software license. SMB and other private ROMs are not bundled.

# Native06 cartridge storage

Native CRTs launch from the SD card through the selected custom GUI. The full
firmware recognizes the complete, zero-padded `SQ1 MPE3 TITLE PULL` type-32
header before allocating CHIP data and enters MinimalBoot. This internal
identity is retained for every native game. Internal flash and USB launch do
not provide the required SD session and remain unsupported.

Small cartridges retain their existing 1 MiB layout. Larger packages use
standard 8 KiB CHIP framing with an MPE-specific range of banks 0 through 255.
This is an extended native container, not an ordinary 4 MiB EasyFlash device.
The C64 still selects only banks 0 through 63. Bank 58 has no ROM CHIP: its IO2
window remains the packet mailbox.

The host packs resources into logical addresses below `0x3FC000`. Logical
addresses below `0xE8000` equal physical addresses. Addresses at or above that
boundary add `0x4000`, skipping physical bank 58. A native read that crosses
the boundary is split before translation. This includes the 512-byte package
cache request that can straddle the gap when a package starts at `0x9700`.
The maximum physical size is `0x400000` bytes.

The first two CHIP packets are bank 0 at `$8000` and `$A000`, preserving the
resident boot halves. Low banks enter the existing 128-entry cartridge table;
there are at most 126 native entries because bank 58 is absent. A separate
2,052-byte RAM2 directory records file offsets for all 512 possible pages.
Banks 64 through 255 consume no cartridge allocation and never enter the C64
bank decoder. Foreground reads use the existing bounded SD page cache.

Every native CHIP must have a complete 8 KiB payload, exact packet length,
ROM or FLASH type, valid bank and address, and a unique page mapping. Missing
boot halves, reserved-bank entries, truncation, and failed seeks stop loading
with a menu error. The extended host writer includes all pages through the
resource end, including erased pages, except bank 58. The resource package
then verifies its complete CRC and each accessed resource remains bounded.

`tests/run-mpe4-cartridge-harness.mjs` executes the actual loader, page resolver
and native reader with a simulated SD file. Its synthetic case covers the full
4 MiB range, bank 255, crossing reads, malformed input and the unchanged legacy
128-chip limit. Paired `--crt` and `--raw` inputs additionally compare every
indexed page and verify all resource CRCs through the native reader. These
checks do not establish physical C64 timing or game completion.

# Cycle-stepped CPU provenance

- Upstream: https://github.com/floooh/chips
- Revision: `ca7d7ddd3ba77b48685d24120cf413ea53786767`
- File: `chips/m6502.h`, copied unchanged on 2026-09-04.
- SHA-256: `c8fb5979be406283db60ae5864da601cebb27dad2b114187a6dea2f90f8925dc`
- License: zlib/libpng; the complete copyright and license notice is preserved in the header.

NESVM calls `m6502_tick` once per CPU cycle and sets `bcd_disabled=true` for
the RP2A03. This does not implement the PPU, APU, DMA, or mapper for us. Those
are separate NESVM components and need separate conformance evidence.
The host build refuses a changed vendor checksum. The existing MPE2/AGI
vrEmu6502 source is not changed or replaced by this NES-only dependency.

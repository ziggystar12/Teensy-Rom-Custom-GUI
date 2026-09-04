# TeensyROM firmware V1.0.16 / DOSVM R19 validation

V1.0.16 adds an optional sharp 320x200 CGA renderer, selected with
Ctrl+Commodore+F7. The default remains the established multicolour display.
This is a presentation change for CGA modes 4/5, without game-specific logic
or guest mode changes. Two-colour 8x8 cells retain their individual source
pixels; additional colours are approximated within C64 hires constraints.
The existing R19 CRT, drives and startup configuration remain compatible.

The GUI About/backend version and generated desktop header were rebuilt with
ACME 0.97. The immutable GUI snapshot is gui/selected-v1.0.16, sourced from
commit 0fad5d5b6cfb660c62b568222b73f69103965307, with content digest
543c192b3ed66a04cbc5582cc2be61fdaded8af37afac8eae2f8e53f696e5179.

The exact Might and Magic menu reproduction uses 320x200 CGA mode 4
(control 0A, colour 30). Its 1,000 cells comprise 724 single-colour cells and
276 two-colour cells. The production sharp renderer matches all 64,000
source pixels. Focused checks cover 96 pixel cases and 58 input snapshots.

The full package checks passed. After correcting modifier-first release of
the shortcut, the final firmware was rebuilt and the integrated firmware and
C64 graphics replay repeated. The shortcut is consumed through F7 release,
including full-queue retries, without leaking a trailing key into the game.

The final C64 replay passed 1,068 packets, including 198 visible scrolling
packets and 121 packets covering sharp-mode activation and return to color.
It verified 302 multicolor frames and 400 exact SID register frames. The
integrated firmware passed 6,949 packets, 990 input events, 4,096 immutable
pending packets, 64 quiet retries, five BIOS-screen boot acknowledgements,
and 16 real FreeDOS writable-drive checks. Default color output and mode 6
retain their previous rendering. Sharp mode adds no video workspace or
guest RAM allocation.

The final linked memory guards retain 21,344 stack bytes and 337,376 bytes
of pre-DOS RAM2 heap; the guest still owns 512 KiB. The combined firmware is
6,371,328 bytes, SHA-256
`326d35c34fd46d647ee7255395acc98902d80aef573edf05307c733a6ba62436`.
The R19 CRT and 20 MiB image are byte-for-byte unchanged from V1.0.15.

The user confirmed V1.0.15 BIOS/DOS startup and Might and Magic working on
physical hardware, plus successful automatic firmware updating. The screen
briefly went blank after the BIOS banner while DOS started. This is a known
presentation interval, not a measured performance result. New sharp-mode
hardware acceptance remains open; see dos/HARDWARE-TEST.md.

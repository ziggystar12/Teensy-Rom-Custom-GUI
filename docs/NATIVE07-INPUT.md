# Native07 authored key waits

KQ1's original `TALK KING` logic switches to a text screen, displays the King's
speech, clears AGI variable 19, and waits for a new key. Native06 also retained
the Return key in a separate input latch. Its `have.key` test read that latch,
so the Return used to submit the command could dismiss the new text immediately.

Native07 makes variable 19 authoritative for an already processed key. A false
`have.key` test can receive a fresh event while the same logic scan is suspended.
The next test consumes that event; an extended key with a zero ASCII byte still
satisfies the wait. Pending input survives instruction-budget yields and is
cleared at the appropriate scan, modal and reset boundaries. Completed scans
clear variable 19. The fixed instruction budget and gameplay scan rate remain
unchanged.

This follows the key-variable check and fresh-event poll in ScummVM's
[AGI test implementation](https://github.com/scummvm/scummvm/blob/master/engines/agi/op_test.cpp)
and its completed-cycle key reset in the
[AGI main loop](https://github.com/scummvm/scummvm/blob/master/engines/agi/cycle.cpp).

The regression scope covers the exact released KQ1 cartridge and focused
bytecode fixtures: the command's Return cannot dismiss the speech, no-input
budget yields keep it visible, and fresh ASCII and extended events can finish
the authored wait. The cartridge bytes, packet protocol, storage mapping,
selected e305 GUI, and saved-state layout are unchanged. The existing native06
cartridges therefore need only the native07 firmware update.

Build and release manifests pin the exact corrected sources and firmware.
Offline checks do not replace confirmation on the user's C64.

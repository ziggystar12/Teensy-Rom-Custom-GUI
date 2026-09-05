# V1.1.3 — firmware ACK ordering fixes NES picker starvation

Physical report: neither keyboard nor joystick could select ROMs even with
the V1.1.2 modes/picker package; directly launching a ROM still works. The prior separate module and C64 client tests
passed but did not reproduce the actual host's callback ordering.

## Reproduction and cause

`picker_scheduler_test.cpp` includes the real `VMHostPoll.h` foreground loop
and the real NES module. Files/clock/bus are mocked; scheduling is not rewritten
by the test. With the V1.1.2 ordering, after 126 frame acknowledgments:

```text
inputHead=0 inputTail=1 selection=0 packetPending=1
Assertion failed: MPE6MenuState->selected==1
```

The host called `pump`, consumed the previous ACK, and immediately called
`packet`. NES defers menu input while a packet/frame is frozen. Every pump
therefore saw a pending packet, and every ACK was followed immediately by
another idle packet. Input reached the module but never left its queue.

The firmware now consumes a completed ACK before pumping the module. An ACK
turn gives the module bookkeeping/input time but no fresh emulation budget,
preserving prompt packet publication and the fast single-pump path. Pending
unacknowledged packets remain immutable. Indexed video host packets retain
their separate pause/DMA/resume ownership.

After correction the same test reports:

```text
inputHead=1 inputTail=1 selection=1 packetPending=1
PASS: actual firmware scheduling, idle picker input, held/release, paging,
Return/Fire launch and game-to-picker recovery
```

The regression also delays ACK for 40 polls while Down is queued, checks frozen
frame/packet identity, then releases ACK and checks that the selection advances.
The existing actual-client keyboard/joystick replay and PAL/NTSC checks remain
separate complementary tests. They do not establish physical acceptance.

## Install

Firmware SHA-256:
`2c1b797103577da6aefd0ccfb21964fc3c46ece893012970da1d0006f2e65a1a`.

Update firmware to **V1.1.3**, reboot and confirm the About version. If the
V1.1.2 NES client/module package is already installed, keep it: this correction
is entirely in firmware, and the rebuilt NES module/client bytes are unchanged.
Video keys remain Commodore+Control+F1/F3/F5/F7. AGI video is not changed.

Physical gate: wait in the picker, try cursor Down/Shift-Up and port-2 joystick,
page left/right, launch with Return/Fire, return with Start+Select and repeat.

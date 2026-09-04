# DOSVM C64-to-PC keymap

DOSVM reads the Commodore 64 keyboard and presents it to FreeDOS programs as
an IBM PC/XT keyboard. The **Commodore `C=` key is PC Alt**.

## Main keys

| C64 key | PC/DOS key |
| --- | --- |
| Commodore `C=` | Alt |
| CTRL | Ctrl |
| Either Shift or Shift Lock | Right Shift |
| RETURN | Enter |
| RUN/STOP | Esc |
| INST/DEL | Backspace |
| HOME/CLR | Home |
| Space | Space |
| A-Z | a-z; hold Shift for A-Z |

Ctrl+A through Ctrl+Z produce the normal PC control combinations. Holding
`C=` with another mapped key produces the corresponding PC Alt combination.
Press **Ctrl + Commodore `C=` + INST/DEL** to send the standard PC
**Ctrl+Alt+Delete** chord and warm-reboot FreeDOS. Keep all three keys held
until the screen begins restarting.

## Cursor and function keys

| C64 input | PC/DOS key |
| --- | --- |
| Cursor Right | Right Arrow |
| Shift + Cursor Right | Left Arrow |
| Cursor Down | Down Arrow |
| Shift + Cursor Down | Up Arrow |
| F1, F3, F5, F7 | F1, F3, F5, F7 |
| Shift + F1, F3, F5, F7 | F2, F4, F6, F8 |

For the shifted cursor and function-key combinations, DOSVM consumes Shift
while selecting the alternate PC key. The program receives Left/Up or the
even-numbered function key by itself.

## Numbers and punctuation

| C64 key | Without Shift | With Shift |
| --- | --- | --- |
| 1 | `1` | `!` |
| 2 | `2` | `"` |
| 3 | `3` | `#` |
| 4 | `4` | `$` |
| 5 | `5` | `%` |
| 6 | `6` | `&` |
| 7 | `7` | `'` |
| 8 | `8` | `(` |
| 9 | `9` | `)` |
| 0 | `0` | `0` |
| `-` | `-` | `_` |
| `.` | `.` | `>` |
| `,` | `,` | `<` |
| `/` | `/` | `?` |
| `:` | `:` | `[` |
| `;` | `;` | `]` |
| Pound `£` | `\` | `\` |
| Up Arrow | `^` | `^` |
| Left Arrow | `_` | `_` |
| `+`, `*`, `=`, `@` | Printed symbol | Same symbol |

The C64 pound key is therefore the DOS path separator. For example, type
`DIR C:` followed by the **pound key** to enter `DIR C:\`.

## Joystick port 2

DOSVM treats a port 2 joystick as keyboard input:

| Joystick input | PC/DOS key |
| --- | --- |
| Up, Down, Left, Right | Corresponding Arrow key |
| Fire | Shift |

This is useful in games such as Boulder. It does not emulate an IBM joystick
or game port. Opposite directions cancel each other.

## DOSVM shortcut and unavailable keys

**Ctrl + Commodore + F7** toggles the sharp CGA display. DOSVM consumes that
one chord. Ordinary F7, Ctrl+F7, and Commodore/Alt+F7 still reach programs.

The C64 keyboard has no standalone mapping for Tab, Insert, PC Delete, End, Page Up,
Page Down, F9-F12, Caps Lock, Num Lock, Print Screen, or Pause. RESTORE is
also outside the DOS keyboard map. Programs that read raw PC scan codes work
best with letters, numbers, arrows, F1-F8, Enter, Esc, Backspace, and the
Shift/Ctrl/Alt modifiers listed above.

# FreeDOS Edit provenance

DOSVM includes `EDIT.EXE` and `EDIT.HLP` from the official FreeDOS Edit 0.9c
package published by the FreeDOS project:

- Package: `edit.zip`, release `20250530.1`
- Source: <https://ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/latest/base/edit/20250530.1/edit.zip>
- Archive SHA-256: `244edc7f1aa4cd3680d9341dc67cac268df7a3e4910ab53a71272fb1925cf31f`
- License: GNU General Public License, Version 2

[`../tools/fetch_freedos_edit.py`](../tools/fetch_freedos_edit.py) validates
the archive plus both installed payloads before a DOSVM image is built. The
official package also contains its complete corresponding source and license.

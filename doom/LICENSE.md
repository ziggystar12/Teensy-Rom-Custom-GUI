# Doom work licensing and provenance

This directory is intentionally multi-license.

- Files carrying `SPDX-License-Identifier: GPL-2.0-or-later` are available
  under GNU GPL version 2 or, at your option, any later version. They form or
  link to an adaptation of the GPL-covered Doom engine. A local copy of GNU
  GPL version 2 is included as [`COPYING.GPL-2.0`](COPYING.GPL-2.0).
- Files carrying `SPDX-License-Identifier: MIT` remain under the repository's
  [MIT license](../LICENSE.md).
- [`mcume-teensydoom-native-adapter.patch`](patches/mcume-teensydoom-native-adapter.patch)
  contains modifications to GPL-covered core files and is GPL-2.0-or-later.
- Files without an SPDX identifier remain governed by the license stated in
  their own header or by the repository license when they are original MHS
  work.

The MCUME source is not vendored here. The source lock fetches the exact
upstream commit only into ignored build storage. Most Chocolate Doom-derived
engine files in the selected subtree carry GPL-2.0-or-later notices. The MCUME
repository and selected subtree have no root license file, however, and 17
platform/glue files do not contain a license grant. Some of those files carry
attribution-only notices, including an embedded `All rights reserved` notice
in `tft_t_dma.cpp`.

The exact unresolved list is recorded in
[`mcume-teensydoom.origin.json`](third_party/mcume-teensydoom.origin.json).
Do not vendor those files or publish a derived MCUME binary until their
provenance is resolved or they are replaced with clearly licensed MHS
implementations. Local source-identity, compilation, and interoperability
proofs do not resolve that distribution gate.

No Doom WAD is included, fetched, or licensed by this directory.

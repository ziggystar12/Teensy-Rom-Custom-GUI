# Local workspace

The maintained checkout is `E:\MHS-Repository\Teensy-Rom-Custom-GUI`.
Use it for GUI and MHS Power Engine source, builds, commits, and syncs.
The sibling `AGI-64` repository contains the separate game compiler and
shared C64 terminal sources.

Keep the latest local DOS test kit in **`DosTest/`** and use
**`build/dos-work/`** for its intermediates. Both are ignored by Git. Run
`dos/tools/build_dos_test.ps1` to validate and replace that kit. The committed,
copy-ready CRT and disk live under **`dos/sd-card/`** with their instructions
and checksums; see [DOSVM](../dos/README.md).

Temporary worktrees can be removed after their changes are integrated or
otherwise preserved. Check their dirty files, ignored source helpers, and
branch history first. Keep a worktree while another task is using it.
Do not create numbered folders for each DOS test attempt.

Released firmware kits remain under `releases/`, unchanged. The root
`firmware/` directory contains only the current public firmware and its
README; experimental builds belong in `DosTest/` or `build/`.

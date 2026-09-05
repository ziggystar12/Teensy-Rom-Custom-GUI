# Experimental standalone DoomVM

The selected next candidate is the [GBADoom backend](gba/README.md).
The remainder of this page documents the earlier MCUME comparison build.

This extracts the pinned MCUME Doom core behind the existing ABI 2 `VmHost` /
`VmModule` callbacks. **It is not loadable on the current firmware: both RAM1
link windows overflow.** The 512 KiB zone works in a short E1M1 host run, but
that alone does not establish a complete 1 MiB port. See
[the measured status and next gates](../../doom/MODULAR-STATUS.md).

The boundary uses generic file reads, clock, indexed video, packet completion,
input and failure callbacks. There are no Arduino, SdFat, firmware-global or
PSRAM dependencies in this directory. Core allocation and ARM newlib allocation
are redirected into a bounded RAM1 heap with real free/coalescing/realloc.
RAM2 supplies only the Doom zone; the indexed framebuffer and host conversion
workspace remain in RAM1.

Run from the repository root with a separately supplied, untracked WAD:

```powershell
node scripts/audit-doomvm.mjs --wad <path-to-DOOM1.WAD>
```

Requirements: the repository's existing Teensy ARM toolchain, the verified
MCUME checkout (use `doom/tools/fetch_mcume_teensydoom.ps1` if absent), Node,
PowerShell, and matching 32-bit MinGW compilers under `C:/msys64/mingw32/bin`.
The script never downloads a WAD or copies one into an output package.

The audit:

1. Verifies and stages the pinned source/adapter into ignored build storage.
2. Builds all 78 core C files plus the standalone module/platform boundary.
3. Links a **measurement-only** ELF with deliberately oversized memory windows.
   That image must never be installed or renamed to `engine.mvm`.
4. Separately links against the unchanged real `vm/abi/module.ld`. Its failure
   is recorded, never bypassed to create an installable image.
5. Tests the reclaiming heap and read-only WAD adapter, runs a constrained
   entrypoint check, and uses explicitly oversized host controls to isolate
   the zone requirement and exercise the VM callbacks.
6. Checks framebuffer immutability across Busy, frame-end ACK ownership,
   deterministic core output, allocation failures, arena guards and closed
   file handles. Source and WAD hashes accompany the report.

Output: `build/doom/modular-audit/report.json`, linker maps, section sizes,
symbols, compile logs and host probes. `AUDIT_COMPLETE` means the investigation
ran successfully; check **`loadable`**, which is currently false.
`--require-fit` returns a failing exit status while the memory gates fail.
Even a future `loadable: true` would be only a link/startup gate, not physical
gameplay acceptance; this tool intentionally has no package writer.

Current functional scope: direct content path or `/VMS/DOOMVM/WADS/DOOM1.WAD`,
E1M1, reset-only process lifetime, silent SID frame commits, existing DOS-style
single non-modifier scan plus held modifiers/joystick. The full WAD picker,
sound, saves, complete multi-key matrix, final client packaging, physical
timing and whole-episode acceptance remain unfinished.

The existing [Doom provenance/distribution record](../../doom/LICENSE.md)
also applies here. The generated upstream files remain ignored. The new core
adaptation, platform, heap and tests carry GPL-2.0-or-later notices.

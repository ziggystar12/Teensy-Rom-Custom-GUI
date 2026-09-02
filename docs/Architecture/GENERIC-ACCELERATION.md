# Legacy MPE acceleration boundary

This page describes the retained AGI+3 acceleration services and their C64
fallback path. The current **native AGI** engine runs game logic and rendering
on the Teensy and uses a different cartridge/session path. Start with the
[native firmware guide](../FIRMWARE-GUIDE.md), [combined build instructions](../../README.md#build-the-combined-firmware-on-windows),
and `engine/native-game/` for that implementation. The legacy services below
remain available for compatible older cartridges.

The MHS Power Engine work began with AGI-64, but its firmware architecture is
intended to support additional C64 projects. The legacy implementation should
be understood as a reusable service layer plus an AGI-specific reference
profile, not as a promise that every existing command is already engine-neutral.

## Owned by this firmware repository

- versioned activation and capability discovery;
- command, status, error, timing, and abort behavior;
- safe cartridge-bank access and direct-residency checks;
- bounded decoding and fixed workspace ownership;
- DMA setup, PHI2-edge deadlines, emergency bus release, and terminal status;
- cache, paging, prefetch, and scatter-transfer primitives; and
- deterministic host-side protocol and safety tests.

These mechanisms must remain usable without Sierra data or the AGI-64 compiler.

## Owned by an engine adapter

- resource numbering and game-specific index formats;
- interpretation of picture, priority, view, actor, or script metadata;
- cartridge packing policy and exact-edition fingerprints;
- destination memory layout; and
- the native C64 fallback used when a capability is absent or rejects input.

AGI-64 supplies this legacy reference adapter, packer, and runtime client in
its own repository. This TeensyROM project supplies the matching firmware side
and protocol documentation.

## Generalization rule

New projects should negotiate capabilities and add a narrow adapter instead of
depending on AGI names, fixed resource types, or undocumented register effects.
Generic services should accept explicit source ranges, destination descriptors,
lengths, and operation identifiers. Engine-specific convenience commands may
remain, but they must be optional and isolated from the transport and DMA
safety core.

All commands fail closed. Unsupported, malformed, nonresident, timed-out, or
aborted work returns a terminal error and leaves the C64 client responsible for
its normal fallback.

## Legacy reference material

- `MHS-POWER-ENGINE-PROTOCOL-V3.md` documents the historical mailbox and
  AGI+3 service set.
- `AGI64-INTEGRATION.md` documents the legacy compiler, cartridge, and runtime
  coupling.
- `MHS-POWER-ENGINE-FIRMWARE.md` documents the built firmware and hardware
  validation boundary.
- `../AGI_Picture_Accelerator.md` preserves the earlier staged v2 bring-up.

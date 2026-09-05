# Doom on MHS Power Engine

Status: the [GBADoom E1M1 candidate](GBADOOM-STATUS.md) now links in the 1 MiB
RAM-only VM layout. It uses a 416 KiB game arena, streamed SD resources and
three synthesized SID effect voices. Host tests pass; physical gameplay is
pending. Scope is the demo's first level only: exits restart E1M1.
See [reproduction instructions](../vm/doom/gba/README.md).

The [earlier MCUME extraction](MODULAR-STATUS.md) and
[core comparison](LOW-RAM-CORE-COMPARISON.md) remain comparison evidence.

The remainder of this document and [phase 1 status](PHASE1-STATUS.md) record
the historical firmware-linked/PSRAM approach. The current baseline is the
generic standalone VM host with no required PSRAM; the modular status above
supersedes those earlier memory and integration assumptions.

## Objective and agreed direction

Run a native ARM Doom engine on TeensyROM+ Fab0.4 / Teensy 4.1, with the real C64 providing VIC-II video, SID output, keyboard, and port-2 joystick input. Start with a playable E1M1, then pursue the complete shareware episode and a compact cartridge package.

The longer-term platform goal is one installed MPE firmware that can load either an AGI engine module or a Doom engine module supplied with a cartridge. AGI-64 has not shipped, so its MPE build can migrate to the new module model alongside the compiler and firmware. Permanent compatibility with the tiny PowerVM v1 interface is not a requirement of this design. Preserve working releases as regression references and recovery copies during development.

“V2” here means the proposed next native engine platform. It does not mean the historical MPE2 6510-emulation service, nor does it assign new wire-protocol numbers. Choose unambiguous identifiers when specifying the module ABI.

## Starting point: AGI is already native

The existing AGI interpreter, parser, motion, collision, rendering, and game state already run as native C++ on the Teensy. The C64 presents frames and sound and supplies input. There is no new 6502-to-C++ AGI port to undertake.

- [Native engine](../engine/native-game/mpe4_game.h): existing host callbacks and bounded `Game::tick` execution.
- [Session and buffers](../engine/native-game/mpe4_session.h): game/renderer state, frame publication, and the existing 64 KiB session arena.
- [Firmware integration](../engine/native-game/mpe4_firmware.h): native engine sources currently compiled directly into firmware.
- [Firmware guide](../docs/FIRMWARE-GUIDE.md): supported hardware and division of responsibilities.
- [Native tests](../tests/README.md): real engine, session, renderer, firmware, and artifact checks.

At present, engine code lives in firmware and the CRT contains game resources. Cartridge-loaded native engine code is a new loader/ABI milestone. The old PowerVM limits of 255 code bytes and 256-byte input/output buffers do not constrain this work. Neither a 16 KiB program cap nor a WebAssembly dependency has been selected.

Repository ownership:

| Repository | Responsibility |
| --- | --- |
| `E:\MHS-Repository\Teensy-Rom-Custom-GUI` | Native engines, firmware, selected GUI, module runtime/loader, native tests, firmware releases |
| `E:\MHS-Repository\AGI-64` | AGI compiler, resource packaging, C64 presenter/input code, cartridge construction, compiler integration |
| This `doom/` directory | Doom plan and future Doom-specific source, packaging tools, and documentation |

The implementation baseline was refreshed on 2026-09-04 at repository HEAD
`adb5eb930f86e9bb9e75a3e634921ffa893dd5ff`. The normal native25 builder
reproduced V1.0.17 with firmware SHA-256
`20d0ac933ebb947cf0d5db13574e4fa329209cffcd283ac3cf1dc7d4444a1367`.
That build does not contain Doom; it proves the pre-Doom baseline remains
buildable. Preserve concurrent work and refresh this checkpoint before the
first firmware-linked Doom build.

## Architecture

```mermaid
flowchart LR
    A["AGI cartridge: engine module + resources"] --> L["Common native MPE loader and services"]
    D["Doom cartridge: engine module + resources"] --> L
    L --> T["Teensy: selected engine, game state, rendering"]
    T <-->|"Input, frames, sound"| C["C64: keyboard, joystick, VIC-II, SID"]
```

The first Doom proof may link the engine into a test firmware, as AGI does today, behind the proposed common interface. That is an intermediate integration step, not completion of the cartridge-module goal. It lets the memory and video questions be answered before building a general native loader.

The common interface should cover:

- Engine initialization, bounded stepping, rendering, shutdown, and error reporting.
- Declared code, read-only data, working memory, stack, and optional PSRAM requirements. Load one game engine at a time and release its allocations on exit.
- Bounded resource reads from cartridge/SD with an explicit cache budget.
- Timestamped held controls plus separate text/menu events.
- Frame submission, display format/palette metadata, and acknowledgement of completed presentation.
- Sound events or SID command submission, keeping timing-sensitive C64 service responsive.
- Engine- and package-specific save/load data with versioned state and atomic replacement.

Keep the generic interface independent of AGI room, VIEW, parser, and opcode concepts. Existing AGI callbacks are a useful implementation reference, but Doom must not have to impersonate an AGI game.

For cartridge-loaded ARM code, specify the module header, target CPU, ABI version, entry points, imports, section sizes/alignment, relocations, BSS initialization, cache maintenance, and unload behavior. Validate all offsets and declared memory requirements before activation. Define the accepted-code trust/isolation model: checksums and relocation bounds alone do not sandbox arbitrary native instructions. Module loading must not corrupt the firmware's bus handlers or an active presentation transaction.

## Doom source selection

The phase-1 comparison baseline is now pinned to MCUME commit `27f6b906aca34e06d6647bdca8215e25f8d20aa5`, limited to `MCUME_teensy41/teensydoom`. The [source-lock record](third_party/mcume-teensydoom.origin.json) and [fetch/verify script](tools/fetch_mcume_teensydoom.ps1) reproduce and validate that exact upstream tree without materializing a WAD. This selects the first source to measure; it is not yet evidence that Doom fits, performs, or runs on MHS Power Engine hardware. The [Doom licensing record](LICENSE.md) also documents a publication blocker: the engine files carry GPL notices, but 17 selected MCUME platform/glue files have no repository-level or per-file license grant and must be resolved or replaced before distribution.

| Candidate | Why inspect it | Main adaptation question |
| --- | --- | --- |
| [MCUME Teensy Doom](https://github.com/Jean-MarcHarvengt/MCUME) | Doom already runs on Teensy 4.1 | Replace its display/input/audio backend and measure memory alongside TeensyROM |
| [RP2040 Doom](https://github.com/kilograham/rp2040-doom) | Small-memory implementation and highly compressed shareware resources | Separate portable engine/resource work from RP2040-specific rendering, multicore, and peripheral code |
| [Doom8088](https://github.com/FrenkelS/Doom8088) | Useful reduced-detail techniques, including flat floors/ceilings | Reuse suitable techniques or portable code without carrying over DOS/16-bit hardware assumptions |
| [FastDoom](https://github.com/viti95/FastDoom) | Performance and reduced-colour rendering reference | Its DOS/x86 backend is not the native Teensy integration target |

Do not choose a source solely because its standalone executable is small. Compare linked code, peak working memory, cache requirements, and measured rendering time. Preserve upstream notices and record the selected source/license; keep user-supplied WADs and generated game packages outside tracked source.

## Memory and storage

### Working memory

The existing AGI session must fit its 65,536-byte arena. That is an AGI implementation constraint, not the intended limit for Doom or engine modules.

The refreshed native25 build reports 18,336 bytes of MinimalBoot stack reserve,
337,376 bytes of RAM2 heap reserve, and 1,310,720 bytes of linker-reserved
EXTRAM variables. These are build-time baseline figures, not measurements of
unused Doom gameplay memory. Do not add the existing session arena and heap
figures together as if both were unoccupied.

Measure engine code/data, framebuffer/conversion buffers, level state, resource cache, stack high-water, and temporary decompression allocations. Keep cartridge interrupt handlers in the required fast memory. Do not bypass memory guards merely to make a build pass.

The selected MCUME core uses an 8 MiB PSRAM zone. The host proof preserves that
exact zone size. The firmware port must check the board's actual fitted memory,
lease the existing PSRAM arena exclusively, and prove that the 64,000-byte
framebuffer plus the 22,304-byte converter workspace fit without weakening
RAM2 or stack guards. Do not assume PSRAM is installed merely because the
linker reserves an EXTRAM address range.

### Cartridge and SD data

The shareware `DOOM1.WAD` is 4,196,020 bytes according to [FastDoom's supported-WAD table](https://github.com/viti95/FastDoom/blob/master/README.txt). It does not fit unchanged in a 2 MiB cartridge; it is also slightly larger than 4 MiB before cartridge code and metadata.

[RP2040 Doom's compression write-up](https://kilograham.github.io/rp2040-doom/flash.html) reports approximately 1,758 KiB of converted shareware data, allowing that port and the episode to fit in 2 MiB flash. This is a useful precedent, not a measured size for our package. Native engine-module bytes, C64 client, indexes, alignment, and reserved banks all count toward our final cartridge capacity.

Start with E1M1 and its required shared resources. Permit a separate asset file on the Teensy SD card during the proof, with explicit read/seek/cache integration. Then measure a self-contained 2 MiB converted package. Larger cartridge support can be evaluated once its separate implementation and hardware path are qualified. Storage capacity does not replace working RAM.

## Video, controls, and sound

### Video and simulation timing

The current native AGI route uses C64-pulled, immutable packets with CRC/commit checks and explicit ACKs. It does not use gameplay bus-master DMA. Retained older DMA services do not prove a continuous Doom transfer path.

Current cell records contain 12 bytes, with at most 19 cells per packet. Updating all 1,000 screen cells therefore requires 12,000 payload bytes in 53 packets, before packet headers/checksums and sound. The C64 also copies, validates, applies, and acknowledges them. Doom camera motion changes much more of the screen than typical AGI actor motion.

Benchmark a representative continuously moving view first. Record render time, C64 colour conversion, cache/SD stalls, transfer time, input latency, and end-to-end frame-time distribution. Use both typical motion and worst-case whole-view changes. A native host test or static screenshot cannot establish physical throughput.

Start with a 160-pixel-wide logical multicolour view and a reduced-height viewport with a HUD. Evaluate a stable small palette, bitmap-only updates where valid, and larger sequential transfer blocks before adding complexity. Respect the VIC-II four-colours-per-cell restriction, including the shared background colour. Any alternative transfer path requires its own real-hardware proof. Set an FPS acceptance target from the initial benchmark and visual trial; none is promised yet.

Keep Doom's 35 Hz simulation clock independent of display acknowledgements. Bound catch-up work after stalls so slow presentation does not permanently slow gameplay or starve input/audio. The existing AGI frame-ACK scheduling must be adapted, not copied unchanged.

### Controls

Reuse the C64 scanner, mailbox, and joystick plumbing. The AGI keyboard path currently sends a selected key event; Doom needs simultaneous held actions and reliable release detection.

Initial mapping proposal:

- Keyboard: cursor keys for forward/back and turning, separate strafe keys, fire, use/open, run, weapon selection, and pause/menu.
- Port-2 joystick: forward/back, turn, and fire; keyboard supplies use, strafe/run modifiers, and weapon selection.
- Support keyboard and joystick together. Verify move + turn + fire and strafe + fire combinations against the C64 matrix's actual limitations.

Use held-action bits or press/release events for gameplay and keep text/menu events separate. Verify that releasing a control, changing menus, or exiting a game cannot leave an action stuck.

### Sound

Reuse SID delivery, but add Doom-specific sound mapping. Basic recognizable firing, impact, pickup, and door effects are the first playable target. Original sampled effects and music require a separately measured audio/conversion design; the AGI three-voice score player does not implement them automatically.

## Implementation phases and completion checks

### 1. Memory and moving-view proof

- Pin a candidate Doom source and record its toolchain and asset identity.
- Build the native core and measure linked sections and peak memory with E1M1.
- Generate representative Doom frames and convert them to the proposed C64 view.
- Present a continuously changing view through the actual native transport on hardware; exercise keyboard and joystick input during transfer.
- Produce a short report with hashes, video standard, memory requirements, timing distribution, and the selected viewport/transport approach.

Complete when a real C64 moving-view trial and a defensible memory budget support the next phase. If they do not, revise the port, viewport, transport, or PSRAM requirement before committing to a full game integration.

### 2. Playable E1M1

- Integrate resource loading, engine initialization, bounded simulation, rendering, and exit through the shared native interface.
- Implement held keyboard/joystick actions and basic SID effects.
- Preserve actual Doom map/gameplay behavior: player movement/collision, enemies, weapons, doors, pickups, damage/death, restart, and the level exit.
- Add health/ammo HUD and a minimal menu. Begin with reduced detail where needed; record intentional visual/audio compromises.
- Run deterministic engine/input tests and compare converted frames with the host reference, then play the level from start to exit on the real C64.

Complete when E1M1 is playable through its exit with both input methods, stable memory use, responsive controls, and recorded physical performance. A rendered title or prerecorded fly-through is not this milestone.

### 3. Shared native v2 engine modules

- Specify the engine ABI and container based on both the existing AGI core and the Doom proof.
- Implement the native module packer/loader, imports, relocation rules, memory accounting, failure recovery, and clean shutdown.
- Extract the already-native AGI engine into the same module model. Update the AGI compiler, presenter integration, and firmware kit together.
- Test loading AGI and Doom with one firmware installation, returning to the menu, releasing resources, and loading the other engine without stale state.
- Reject malformed/incompatible modules before execution and preserve the working release/restore artifacts.
- Re-run meaningful AGI native/session/presenter and hardware regressions, including parser, movement, room changes, sound, and saves. Do not imply old saves are compatible if their schema changes.

Complete when both engines work as cartridge-supplied modules under the common firmware. A test firmware with both engines compiled in is useful evidence but does not complete this phase.

### 4. Shareware episode and release preparation

- Extend resource packaging to every shareware level and its common assets; measure the complete CRT and any external SD dependency.
- Add intermissions, game-over/restart flow, settings, and save/load with engine/package-specific identity.
- Improve visuals and audio within the measured frame budget.
- Exercise all episode levels, stress large scenes and cache misses, and test supported PAL/NTSC hardware configurations.
- Publish matched firmware, module/cartridge, source pins, checksums, controls, hardware requirements, measured performance, and known limitations.

The episode's exact final cartridge size and frame rate remain open until measured. Multiplayer, additional Doom games, and expanded controller types are outside the first release target.

## Current checkpoint

Use [PHASE1-STATUS.md](PHASE1-STATUS.md) for measured results, reproducible
commands, hashes, and the remaining gates. Do not substitute calendar estimates
for the outstanding firmware, physical-throughput, memory-high-water, and
gameplay evidence. Getting Doom playable and completing the general module
platform remain separate deliverables.

## Work boundaries and next action

Implementation has started under phase 1. The next action is to adapt the
pinned MCUME core to the exactly-one-gametic session contract, bounded SdFat
resource reads, exclusive PSRAM ownership, and the current MPE packet service.
That firmware-linked prototype must then present a continuously moving view on
the real C64 before work advances to the playable-E1M1 milestone.

Keep Doom-generated output under an ignored `build/doom/` directory or an explicitly named external build directory. Preserve unrelated working changes, selected GUI source, accepted hardware pairs, and normal AGI release outputs. Record source/firmware/asset hashes with evidence. Build and host checks, emulator boot checks, and physical gameplay acceptance must be reported separately. Firmware flashing, publication, and release replacement are not performed by this documentation task.

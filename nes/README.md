# NES on MHS Power Engine: native Teensy implementation

## Current modular test — Nofrendo speed candidate 1 / V1.1.5 host

The NES engine now loads independently from `/VMS/NESVM/`, not from firmware.
Use the [current package](../vms/NESVM/README.md) and
[ABI 2 test report](../docs/Architecture/DOS-MODULAR-TEST-STATUS.md). RAM1 holds
code/hot state and RAM2 holds ROM backing. The active module now uses
[Nofrendo's instruction/scanline core](../engine/nofrendo/README.md), while the
older cycle/dot core remains a test reference. The prior hardware test measured
35% speed, RUN 91%, HOST 9%; F5's picture was accepted. This replacement needs
a new speed test, but no new firmware if V1.1.5 is installed. Preserve your ROMs.

## Historical built-in prototype and original plan

The material below predates modular extraction; its firmware-linked service,
SD paths and installation references are superseded by the current guide.

Status, 2026-09-04: R1 now includes the portable core, generic [NESVM.CRT](sd-card/NESVM.CRT), direct `/NESVM/ROMS` picker, Mapper 0/11 loader, whole-frame presenter, basic SID packet output, and a successfully linked local Teensy firmware candidate. See [IMPLEMENTATION.md](IMPLEMENTATION.md) for the exact artifacts, controls, hashes, checks, and remaining physical acceptance gate. The previously bounded `WAITING FOR TEENSY` screen is expected with older firmware; NESVM requires the matching firmware candidate so its `N6D1` service can start.

Joystick port 2 supplies directions and A/fire; Space is B, Return/Enter is Start, either Shift is Select; one player only. In the ROM picker, port-2 up/down moves the highlight and fire or Return launches it. During a game, Start+Select (Return+Shift) returns to the same highlighted row. DOSVM-style Sharp Text is **on by default**, showing the whole frame at 320x200 hires with two colors per 8x8 cell. Ctrl+Commodore+F7 toggles to/from whole-frame 160x200 multicolor. This is a display-only choice, not text recognition or a crop. The private ROM folder stays ignored; only the exact owner-authorized [Crossbow demo](DEMO/README.md) is eligible for distribution. Its header specifies Mapper 11, not 0.

Baseline finalized: maintained `Teensy-Rom-Custom-GUI` checkout at commit `2646372e407f6fd15b10b3f3f8fdb00c7e4b478a` (`main`, equal to the fetched `origin/main` on 2026-09-03). The initial architecture inspection at `8459afe83e951d9a9834fba1d59da8de753a8207` was rechecked after the intervening DOSVM input/performance commit. Existing unrelated worktree changes were not modified. The historical `teensyrom-plus` tree is not an implementation or release destination; it may be consulted only for provenance.

The user-facing emulator lists ordinary `.nes` files from `/NESVM/ROMS` and launches the selected ROM directly. No per-game CRT conversion, compiler, manifest editing, or firmware rebuild is required. The reusable CRT starts the native firmware service; the native service owns the picker, reopens and hashes the selected file, loads the original bytes unchanged, and returns to the retained row after Start+Select.

## Executive decision

Build the NES emulator as another firmware-linked native Teensy engine, provisionally called **NESVM**, while the C64 or C128 in C64 mode remains the display, input, and SID terminal.

The first playable profile should be deliberately narrow:

- NTSC timing only.
- One standard NES controller.
- Mapper 0, initially the NROM-128 shape needed by **Thwaite**: 16 KiB PRG ROM, 8 KiB CHR ROM, horizontal or vertical mirroring, no trainer, no battery RAM.
- Open a ROM folder on the Teensy's SD card, select a `.nes` file from the list, and run it directly. The engine stays in firmware and a generic C64 terminal is installed once; the original ROM remains an unchanged standalone file.
- The PPU remains a native 256 x 240, 341-dot x 262-scanline emulation internally. A separate presenter reduces completed scanlines to VIC-II multicolor bitmap cells.
- Tier 1 shows the whole NES 256 x 240 view every time. Sharp Text defaults to 320 x 200 hires; the toggle selects 160 x 200 multicolor. Both are full-frame fits: no cropped upper playfield, viewport panning, or look-up/look-down control.
- NES CPU, PPU, APU, DMA, and controller time never wait for the C64 to acknowledge a video packet. Old presentation frames may be dropped; emulated time may not be skipped to make the display catch up.
- NES pulse 1, pulse 2, and triangle are approximated on the three SID voices. Noise uses deterministic voice stealing. DMC must eventually have correct emulated timing, but audible DMC reproduction is outside the first playable profile.
- Physical Teensy + C64 and Teensy + C128-in-C64-mode runs are separate acceptance gates. Host tests, VICE, source checks, and a successful firmware build do not prove gameplay, transport timing, input latency, or SID behavior.

The planned first hardware-sized game is [Thwaite](https://www.nesdev.org/wiki/User:Tepples/Thwaite), a GPLv3 homebrew NROM-128 title using the standard controller. Its relatively stable screen is useful for the transport gate and its source/redistribution terms can be audited. A lawful user-supplied **Super Mario Bros.** image is the NROM completeness and scrolling gate; it may be read locally but must never be tracked or published by this repository. R1 also exercises the supplied Mapper-11 demo on the host; this does not remove its separate memory/input/hardware gates.

## Why this architecture fits the maintained repository

The current product already has the right high-level split:

- Teensy owns native engine logic and state.
- A C64 terminal applies acknowledged VIC-II cells, writes SID state, and collects CIA keyboard/joystick input.
- Native code is compiled into the selected firmware; the CRT is data and C64-side terminal code. There is no cartridge-loadable ARM module ABI today.
- Native cartridges launch from SD and can carry Teensy-only pages in the extended CRT format.
- Bank 58 is the transactional C64-pulled mailbox. The publisher commits an immutable packet, the C64 verifies and applies it, and the exact packet remains available until acknowledged.

See the [firmware guide](../docs/FIRMWARE-GUIDE.md), [native storage contract](../docs/NATIVE06-STORAGE.md), [transport correction](../docs/NATIVE10-TRANSPORT.md), [native arena](../engine/native-runtime/mhs_native_arena.h), and [V1.0.12 validation record](../docs/validation/MPE-V1.0.12.md).

NESVM should follow the DOS engine's decoupled execution/presentation model, not the current AGI rule that waits for a frame-end ACK before the next game tick. The DOS path is useful precedent for keeping an immutable wire packet pending while the guest continues and for coalescing newer sound state. See [the DOS engine overview](../dos/README.md) and [DOS firmware adapter](../engine/native-dos/mpe5_firmware.h).

The engine should initially be compiled under a new `engine/native-nes/` subtree. A general cartridge-loaded native module ABI remains worthwhile, but NESVM does not need one to read ordinary ROM files. The repository's [Doom native-engine plan](../doom/README.md) distinguishes firmware-linked engines from future ARM modules. For NESVM, extend the current launcher to pair one reusable terminal with the selected SD ROM; the existing CRT launch limitation must be solved inside the product, not exposed as a per-game packing step.

## System boundary

```text
SD ROM folder: game-a.nes, game-b.nes, ...
      |
      v
GUI ROM list -> select one file -> Run
      |
      v
validated SD path/session handoff
  + reusable generic C64 NES terminal
      |
      v
Teensy foreground poller
  direct iNES loader -> hot ROM backing -> CPU/PPU/APU/controller scheduler
                              |                 |
                              |                 +--> versioned battery save
                              v
                 scanline reducer + SID adapter
                              |
                bank-58 transactional mailbox
                              |
                              v
C64 or C128 in C64 mode
  VIC-II bitmap + SID writes + port-2/CIA matrix sampling
```

All ROM parsing, hashing, save I/O, emulation, frame conversion, SID synthesis, and packet construction belongs in the foreground poller. The PHI2/IO2 path may only validate and latch bounded mailbox state. SD, USB, flash writes, allocation, and emulator stepping must never be added to the per-cycle bus handler; the repository's [architecture constraints](../docs/Architecture/Constraints.md) give that handler roughly one microsecond and explicitly prohibit slow work there.

## Compatibility tiers and non-goals

### Tier 0: deterministic component proof

- Strict iNES/NES 2.0 inspection and fail-closed error reporting.
- RP2A03 CPU, bus, controller, basic PPU, and basic APU tested headlessly.
- Synthetic NROM packages and licensed test ROMs only.
- No statement about physical playability.

### Tier 1: first playable profile

- Folder browsing, a list of ordinary `.nes` files, direct launch, and return to the same list to choose another ROM; no user-created CRTs.
- NTSC NROM-128 with 16 KiB PRG ROM and 8 KiB CHR ROM.
- Thwaite through the complete C64/128 terminal path.
- Controller 1 only; controller 2 reads as disconnected.
- VIC-II multicolor presentation; approximate SID output.
- No trainer, battery RAM, save states, DMC audio, PAL/Dendy timing, Vs. System, PlayChoice-10, four-screen VRAM, zapper, expansion controller, or expansion audio.

### Tier 2: Mapper 0 compatibility

- NROM-128 and NROM-256, CHR ROM and 8 KiB CHR RAM where declared.
- Correct sprite-0 hit, scrolling/address behavior, palette mirroring, PPUDATA buffering, OAM DMA, NMI edge cases, and stable unofficial CPU opcodes needed by allow-listed software.
- Private, lawful user-supplied Super Mario Bros. as the scrolling/completeness gate.
- Battery plumbing enabled only for a supported board/header that actually declares NVRAM.

### Tier 3: Mapper 2 and expanded compatibility

- UxROM/UNROM/UOROM with a documented ROM-backing decision.
- Optional second controller mapping, PAL timing, richer audio, and better transport only as separately gated work.

The following are not implied by any early tier: pixel-perfect NES color, cycle-perfect analog video, sample-identical audio, universal ROM compatibility, C128 VDC/80-column output, C128 2 MHz native mode, netplay, rewind, save states, cheats, or distribution of commercial ROMs.

## Folder browsing and ROM handling

### Required user workflow

1. Put `NESVM.CRT` at the SD-card root. Beside it, create `/NESVM/ROMS` and `/NESVM/SAVES`; the checked-in `nes/sd-card` tree is the canonical layout.
2. Copy lawfully held `.nes` files into `/NESVM/ROMS`. The NES launcher shows a paged, sorted list, matching `.nes` case-insensitively and retaining the actual filename/path for selection.
3. Select one ROM and choose Run using the existing GUI's keyboard/joystick navigation. Parse and validate that file, then start it automatically without a PC tool or per-game preparation.
4. Exit the game and return to the same folder and selected row. Select another ROM and run it without reflashing or repacking.

The list is a selector, not a batch loader: only the selected ROM's hot data is loaded. Folder size must not multiply emulator RAM usage. Use bounded directory pages and bounded display labels; do not silently truncate the path used for launch. Empty/unreadable folders, missing SD cards, vanished files, malformed headers, and unsupported mapper/region/size must produce a clear message and leave the list usable. Show unsupported files with a reason or diagnose them on launch; never fall back to treating them as C64 programs.

Folder browsing does not imply universal NES compatibility. Tier 1 runs the documented NROM-128 profile; later compatibility tiers add more games without changing this workflow.

### Direct-file launch integration, required in Tier 1

The [current type table](../Source/Teensy/MinimalBoot/Common/DriveDirLoad.h) has no NES entry and unknown files can fall into the PRG path. Therefore implement a real `rtFileNES` route in the maintained selected-source build, with matching full-firmware/MinimalBoot and C64 definitions:

1. Capture the selected SD volume, bounded canonical path, file identity/size, and a fresh launch token before leaving the browser. Preserve folder/row return context separately from the emulator arena.
2. Validate the header and declared sizes using a bounded file reader; compute SHA-256 while loading the selected ROM. Metadata describes the original file and never rewrites it.
3. Boot the reusable generic C64 NES terminal, supplied once with the emulator. Preserve the validated launch descriptor across any full-firmware-to-MinimalBoot transition; reopen/revalidate the file after the transition rather than keeping a stale handle or pointer.
4. Hand the descriptor to the explicit NES session dispatch. Require a matching token/protocol/terminal identity, claim the NES arena, preload the accepted PRG/CHR hot set, initialize the machine, and run. No emulation-time SD reads are permitted for Tier 1.
5. On exit, checkpoint supported battery data, silence SID, tear down mailbox/session/file ownership, restore the folder list, and clear the launch token. On failure or bank loss, unwind safely without using a stale path/handle/mailbox.

The generic terminal may internally use the existing native CRT launch mechanism. If so, it contains only the reusable launcher/terminal, not the selected game, and must be recognized by both native identity checks. The ROM path is an explicit handoff; no ROM-specific CRT needs to be created, persisted, or selected by the user. The folder workflow must also work on read-only ROM media for profiles without battery writes.

### Optional developer fixtures

A local inspector and ROM-to-CRT test packer may remain useful for deterministic receiver replay or diagnosis, but they are optional development tools and cannot satisfy the user-workflow acceptance gate. Any such fixture must preserve the original ROM bytes, obey existing CHIP-page/bank-58/container bounds, and keep private ROM-containing output untracked. A diagnostic hash/metadata report is generated by tooling when needed; normal users do not create or edit manifests.

### Strict iNES/NES 2.0 parser

The loader must follow [iNES](https://www.nesdev.org/wiki/INES) and [NES 2.0](https://www.nesdev.org/wiki/NES_2.0), and must never infer a runnable board from an invalid header.

- Require the 16-byte `NES` + `$1A` signature.
- Detect NES 2.0 with `(byte7 & $0C) == $08`.
- Use overflow-checked offset and size calculations and prove every declared section fits the actual file.
- Parse trainer presence, PRG/CHR size, mapper, submapper, mirroring, battery/NVRAM declaration, console type, timing region, and volatile/nonvolatile RAM sizes.
- Parse NES 2.0 byte 14's miscellaneous-ROM count and byte 15's default expansion/input device. Tier 1 rejects any miscellaneous ROM and accepts only a standard-controller-compatible or explicitly unspecified input declaration.
- For iNES, sizes are 16 KiB PRG units and 8 KiB CHR units; zero CHR means CHR RAM.
- For NES 2.0, parse the 12-bit mapper/submapper and RAM shift fields. Either implement exponent/multiplier ROM sizing correctly or reject that encoding explicitly in the first loader.
- A trainer is 512 bytes before PRG and conventionally maps at `$7000-$71FF`; Tier 1 rejects it rather than silently skipping it.
- Tier 1 rejects non-NTSC timing, four-screen VRAM, Vs./PlayChoice/extended console types, unsupported mapper/submapper, malformed trailing layout, unsupported RAM, and payloads over the allowed profile, with a specific reason.
- Old iNES headers with polluted upper bytes need a documented compatibility policy. Do not silently discard mapper bits merely to make a ROM launch.
- Log only metadata and cryptographic identity; never dump ROM contents in normal diagnostics.

### Mapper contracts

**Mapper 0 / NROM**

- 16 KiB PRG mirrors at `$8000-$BFFF` and `$C000-$FFFF`; 32 KiB PRG maps once across `$8000-$FFFF`.
- Fixed 8 KiB CHR ROM or declared 8 KiB CHR RAM at PPU `$0000-$1FFF`.
- Fixed horizontal or vertical CIRAM mirroring.
- No mapper registers or IRQ.
- Do not assume save RAM merely because old iNES byte 8 defaults to a value. Enable persistence only for an explicitly supported board/profile.

Reference: [NESdev NROM](https://www.nesdev.org/wiki/NROM).

**Mapper 2 / UxROM, after Mapper 0**

- `$8000-$BFFF`: switchable 16 KiB PRG bank.
- `$C000-$FFFF`: fixed final 16 KiB bank.
- A write anywhere in `$8000-$FFFF` selects the lower window. Decode only the address/data lines the identified board implements; mask/alias high bank bits according to that board and ROM size rather than applying an arbitrary modulo operation.
- 8 KiB CHR RAM, fixed horizontal/vertical mirroring, no IRQ or expansion audio.
- Initial cap: ordinary 128/256 KiB PRG variants.
- NES 2.0 submapper 1 means no bus conflict; submapper 2 applies written value AND the visible ROM byte. Ambiguous legacy headers must warn and use an allow-list or explicit compatibility record.

Reference: [NESdev UxROM](https://www.nesdev.org/wiki/UxROM) and [NES 2.0 submappers](https://www.nesdev.org/wiki/NES_2.0_submappers).

Mapper 2 is a memory/latency decision gate, not a simple switch statement. A newly selected bank can be executed immediately, so an SD miss cannot be hidden behind ordinary prefetch. Do not claim real-time Mapper 2 until its entire hot PRG strategy is proven: measured resident RAM, verified optional PSRAM, or another deterministic backing mechanism. Synchronous SD loads may be useful for diagnostics but are not a full-speed design.

### Legal and licensing policy

- Do not bundle, fetch, publish, or link to unauthorized commercial ROMs, ROM fragments, box art, keys, or commercial-ROM-derived CRTs. Local enumeration of the user's chosen folder is required; it is not a public ROM index or download service.
- Accept only a file selected from the user's local ROM folder; the user remains responsible for possessing the necessary rights.
- Keep private compatibility hashes and results out of source control and public release manifests.
- The owner explicitly authorized one Exidy image on 2026-09-04. Only the exact `nes/DEMO/Crossbow.nes` hash recorded in [DEMO/README.md](DEMO/README.md) is exempt; do not infer permission for the rest of the collection. Run `node nes/tools/nes.mjs audit` before staging/releasing NES work. Use an explicit one-file/hash allow-list, never recursive inclusion of `ROMS` or diagnostic outputs.
- Only add a diagnostic ROM or homebrew binary after recording its license, redistribution permission, source/provenance, pinned revision, and required notices. Test ROMs have different licenses; an entry on an emulator-test list is not redistribution permission.
- If Thwaite is carried or built by the repository, preserve GPLv3 source/offer and notices and record the exact upstream revision. Otherwise require the user to build/select it locally.
- Treat this as project risk control, not legal advice.

## Emulated machine design

### RP2A03 CPU and bus

R1 selects the permissive cycle-stepped [chips/m6502 core](../engine/native-nes/vendor/chips/UPSTREAM.md), pinned and checksum-enforced with `bcd_disabled=true`. It leaves the existing MPE2 vrEmu6502 vendor/core unchanged. The candidate discussion below is retained as architecture rationale, not an unresolved R1 choice.

The production core must advance at individual CPU-cycle boundaries. One NTSC CPU cycle advances the PPU exactly three dots and advances the APU/DMA scheduler once. Instruction count is useful for debugging, but it is not the authoritative clock.

Required CPU behavior:

- NMOS 6502 instruction, interrupt, stack, zero-page wrap, branch, read-modify-write, page-cross, dummy-read/write, and indirect-JMP-wrap behavior.
- RP2A03 decimal flag storage but binary-only ADC/SBC arithmetic: decimal mode is electrically disconnected.
- Reset, NMI, IRQ, BRK, and simultaneous-edge priority/timing.
- Correct open-bus-facing access order where the PPU, APU, DMA, controller, or mapper can observe it.
- Stable unofficial opcodes before a broad compatibility claim; Tier 1 may allow-list a title that needs only official opcodes, but illegal instructions must fail deterministically rather than execute arbitrary behavior.

The pinned MIT [vrEmu6502 core](../engine/vendor/vrEmu6502/UPSTREAM.md) is useful existing provenance and already contains an NMOS indirect-JMP behavior and an undocumented-opcode table. It is not a drop-in NES core: it has no RP2A03 model, honors decimal arithmetic, allocates its CPU object, and executes an instruction before counting down its remaining cycles. The CPU milestone must therefore make an evidence-based choice:

1. Extend/fork it with explicit 2A03 semantics, in-arena state, and per-cycle bus micro-operations; or
2. Pin a different permissively licensed, truly cycle-steppable core with equivalent provenance and checksum enforcement.

The simpler instruction-first wrapper is allowed only as a disposable experiment. `nestest`, DMA, dummy-access, interrupt, controller, and PPU timing results decide whether it survives; passing arithmetic opcodes alone is insufficient.

CPU address map:

| Address | NES behavior |
| --- | --- |
| `$0000-$07FF` | 2 KiB internal RAM |
| `$0800-$1FFF` | mirrors of internal RAM |
| `$2000-$2007` | eight PPU registers |
| `$2008-$3FFF` | mirrors of those PPU registers |
| `$4000-$4013` | APU channel registers |
| `$4014` | OAM DMA |
| `$4015` | APU status/channel enables |
| `$4016` | controller strobe and controller 1 serial read |
| `$4017` | APU frame counter write/controller 2 serial read |
| `$4018-$401F` | normally disabled/test area; deterministic open-bus policy |
| `$4020-$5FFF` | cartridge expansion; unsupported in early tiers |
| `$6000-$7FFF` | board PRG RAM/NVRAM when explicitly present |
| `$8000-$FFFF` | PRG ROM and mapper registers |

Reference: [NESdev CPU memory map](https://www.nesdev.org/wiki/CPU_memory_map) and [2A03](https://www.nesdev.org/wiki/2A03).

### Scheduler, DMA, and real time

Use monotonically increasing integer counters; do not use floating-point time in the emulated machine.

- NTSC target: approximately 1.789773 MHz CPU, 3 PPU dots per CPU cycle, 341 dots per scanline, 262 scanlines, about 60.0988 frames per second.
- Pace against a Teensy monotonic timer using fixed-point/integer accumulation, not the C64 raster or mailbox ACK.
- Permit a bounded catch-up budget after short foreground delays. If debt exceeds the budget, record it and resynchronize presentation; never omit CPU/PPU/APU cycles inside an emulated frame.
- OAM DMA stalls the CPU for 513 or 514 cycles according to alignment while PPU/APU time continues.
- DMC fetch DMA eventually stalls for the documented 3/4-cycle pattern and must interact correctly with OAM DMA and sensitive reads. It may be absent from Tier 1 only if the accepted title does not rely on it and the omission is reported.
- Service input latches and the wire pump frequently, but neither may change emulated time retroactively.
- Export counters for simulated CPU cycles, PPU frames, timer debt, maximum catch-up burst, presentation frames dropped, cells/packets, ACK wait, input queue depth, audio coalesces, and save duration.

Reference: [NESdev clock rates](https://www.nesdev.org/wiki/Clock_rate), [frame timing](https://www.nesdev.org/wiki/PPU_frame_timing), and [DMA](https://www.nesdev.org/wiki/DMA).

### Power-on, reset, and relaunch contract

Do not treat all starts as `memset(0)` and do not conflate an NES reset with leaving NESVM.

| Event | State contract |
| --- | --- |
| Cold NESVM launch | Create a new machine, apply documented RP2A03/APU/PPU power-up register state, mapper power-up state, PPU startup write-suppression interval, initial scroll/write latch and PPUDATA read buffer, and a documented deterministic seed/fill for electrically unspecified RAM/OAM. Run randomized-fill host tests as a compatibility check. |
| NES reset action, if later exposed | Preserve memory that a console reset does not clear, apply CPU/PPU/APU/mapper reset-line semantics, preserve battery RAM, and do not replay the cold power-up delay unless the hardware does. |
| Clean exit/relaunch | Checkpoint eligible battery data, release the arena/terminal, and make the next launch a new cold session. |
| C64 or Teensy reset/power loss | Assume no clean callback, recover only the last verified save generation, invalidate any presentation baseline, and rebuild the session/handshake from a known state. |

Focused tests must cover the PPU's startup ignored writes, first vblank/NMI, initial `w` toggle/read buffer, APU channel/frame-IRQ state, CPU interrupt/reset vector sequence and stack effects, mapper reset bank, warm-reset RAM preservation, and cold launches under several RAM/OAM fill seeds. Reference the [PPU register startup notes](https://www.nesdev.org/wiki/PPU_registers) and [CPU/APU power-up state](https://www.nesdev.org/wiki/CPU_power_up_state); pin the exact behavior used by the implementation rather than relying on remembered constants.

### PPU state and behavior

The PPU model remains NES-native even though its visible output is reduced for the VIC-II.

- 256 x 240 visible pixels within 341 dots x 262 scanlines; post-render, vblank, pre-render, and odd-frame dot skip when rendering is enabled.
- `$0000-$1FFF`: cartridge CHR ROM/RAM.
- `$2000-$2FFF`: nametable/attribute data backed by 2 KiB CIRAM and mapper-selected horizontal/vertical mirroring.
- `$3000-$3EFF`: nametable mirrors.
- `$3F00-$3F1F`: palette RAM with NES-specific universal-background mirrors, repeated through `$3FFF`.
- 256-byte primary OAM, secondary OAM, eight-sprite-per-scanline selection, sprite size/flip/priority, sprite-0 hit, clipping, and OAM addressing.
- `$2000-$2007` side effects, including `v/t/x/w` scrolling state, shared `$2005/$2006` write toggle, `$2002` vblank/toggle behavior, `$2007` read buffer/increment/palette exception, NMI edge timing, and rendering-time address changes.
- `$2001` left-edge clipping, grayscale, and NTSC color-emphasis behavior. Apply visual emphasis in the NES palette stage before the fixed C64 lookup; it must not alter PPU status/timing decisions.
- The hardware sprite-overflow bug may follow the Tier-1 allow-list, but eight-sprite selection and sprite-0 hit may not be faked at presentation time.

References: [PPU registers](https://www.nesdev.org/wiki/PPU_registers), [PPU memory map](https://www.nesdev.org/wiki/PPU_memory_map), [scrolling](https://www.nesdev.org/wiki/PPU_scrolling), and [sprite evaluation](https://www.nesdev.org/wiki/PPU_sprite_evaluation).

### Memory-tight raster sink

Do not make a 61,440-byte full indexed framebuffer mandatory in the first firmware. The current shared native arena is only 65,536 bytes, so a full framebuffer would consume almost all of it before ROM or machine state.

Instead, make PPU rendering target an abstract raster sink:

- Host tests can attach a full 256 x 240 indexed framebuffer for reference images.
- R1's target-oriented renderer keeps an eight-row, 320-byte-wide reduced stripe and incrementally encodes one VIC candidate frame, in either hires or multicolor. No full 256 x 240 framebuffer belongs to the target core; the host's reference images are diagnostic storage only.
- PPU collision, sprite-0, priority, and timing decisions happen before the sink; reducing pixels must never feed back into emulation.
- A later full-frame/debug buffer is optional only after linked-map and runtime high-water evidence proves room.

This keeps an accurate emulated picture boundary while making the production storage choice explicit.

## VIC-II presentation

### Unavoidable limitations

The NES and current terminal are not display-equivalent:

- NES produces 256 x 240 pixels from palette indices with scanline-sensitive scrolling and sprites.
- The current multicolor bitmap path is 160 x 200 logical pixels. Each 4 x 8 cell can use one global background plus only three local colors.
- Hires bitmap is 320 x 200 but permits only two colors per 8 x 8 cell.
- The present protocol does not provide mixed hires/multicolor cells, FLI, arbitrary per-pixel 16-color output, or native NES sprites.

Therefore Tier 1 is an intentional approximation. It must be described as NES gameplay presented through VIC-II constraints, not pixel-accurate NES video.

### Default Sharp Text converter and color alternative

The 2026-09-04 user request adopts DOSVM's `CgaVideo::renderSharp` approach by default. Resample the whole NES frame from 256x240 to 320x200 using fixed pixel-center sampling. Horizontal enlargement retains every source column; vertical reduction still loses some one-pixel rows. This is not an OCR/glyph replacement feature and cannot make all NES lettering exact.

Count the mapped colors per 8x8 cell. Keep the two most frequent, with stable lower-index ties, and map any remaining colors to the nearer representative in fixed RGB space. Cells with at most two mapped colors are exact after resampling. This is the DOSVM rule generalized from four CGA source entries to 16 mapped C64 colors, including the same diagnostic C64 palette. Colors are combined before counting. No temporal dithering is used.

Ctrl+Commodore+F7 toggles the display preference once per press. Holding it or releasing modifiers first must not toggle repeatedly; Shift/F8 does not activate it. The new mode applies at an image boundary. A future terminal must repaint a complete frame and switch VIC mode atomically, never interpret half a multicolor frame as hires. Game input and CPU/PPU time are unchanged.

The alternative multicolor policy below remains the fuller design target. R1 implements fixed black global background, three most-frequent local colors, deterministic color reduction, and matching host/streaming results; background optimization, exhaustive representative selection, and temporal palette hysteresis are not yet implemented.

Use deterministic multicolor output:

1. Map each NES palette index through a versioned, fixed NES-to-C64 16-color lookup table.
2. Fit the **entire extent** of the 256 x 240 picture into 160 x 200 with one pinned full-frame sampling rule: 256 -> 160 horizontally and 240 -> 200 vertically. Downsampling necessarily discards detail, not a region of the playfield. There is no Tier-1 overscan crop, scrolling viewport, camera pan, or title-specific hidden playfield.
3. Choose the frame's VIC global background from the histogram of mapped NES universal-backdrop samples. Keep the previous background when its fixed integer error is within a pinned hysteresis threshold; otherwise choose the lowest-error color with C64 palette index as the final tie-break.
4. For each 4 x 8 VIC cell, build a histogram of all mapped colors and exhaustively choose up to three local representatives alongside the global background. Minimize a versioned integer color-distance error plus a small pinned previous-cell-palette penalty; break ties lexicographically. Remap every discarded color to the nearest selected representative with the same distance table. Tier 1 does not dither.
5. Emit bitmap, screen, and color data in the existing 10-byte cell image; compare against the last ACKed C64 image and publish only changed cells.
6. Carry the selected `$D021` background in the versioned NES frame-end payload and apply it only after all cells in that visual generation. If partial visible updates tear, measure them; do not call packet CRC protection a tear-free display. A C64-side back buffer/swap is a later option subject to memory and raster proof.

The lookup table, distance matrix, scale samples, hysteresis constants, representative ordering, and tie-breaks are format inputs: pin and golden-test them so the same NES frame always produces identical C64 bytes.

An experimental centered 256 x 200 hires profile can preserve horizontal detail but reduces every cell to two colors; it is not the default and needs its own visual acceptance.

### Transport budget and frame dropping

The current mailbox carries at most 19 12-byte cells in one 240-byte data area. A full 1,000-cell replacement needs 53 cell packets plus one 27-byte frame-end packet, 12,000 cell payload bytes, and about 12,567 wire bytes before any independent `AUDIO` packets. The repository's [Tandy video/transport analysis](../dos/TANDY-VIDEO-PLAN.md) estimates at least roughly 115,000 C64 cycles just to copy one full replacement, before protocol and normal terminal work. Sixty full replacements would be about 754 kB/s (736 KiB/s) and are outside this path.

The 64 KiB Tier-1 target uses only two 10,000-byte cell images:

- **Presented:** the last completely acknowledged C64 cell image.
- **Candidate/Publishing:** the next fully converted image; immutable from the first emitted cell packet through its exact frame-end ACK.

When no publication is active, the scanline sink fills `Candidate` completely and the publisher diffs it against `Presented`. While that frame is being drained, later PPU frames still run but their presentation pixels are discarded rather than overwriting the frozen candidate. After the frame-end ACK, swap the two image roles and arm conversion at the next PPU frame boundary. This drops display frames without pausing or corrupting NES time and is consistent with the two-image memory budget.

A higher-memory tier may add a third replaceable `Latest` image so conversion continues during publication and intermediate candidates coalesce. That optimization needs the same arena/link-map proof as full NROM-256. Never mutate an in-flight frame or let the NES scheduler wait merely because a visual frame was dropped.

### NES-specific wire profile

Retain the existing bank-58 transactional envelope, but assign NESVM its own engine identity and protocol version. Do not reuse MPE4/MPE5 meanings accidentally:

- `CELL` retains the 12-byte VIC record and belongs to one frozen visual generation.
- A new `AUDIO` packet carries the 26-byte retrigger + SID-register body, is applied and ACKed immediately, and **does not** close or promote a visual frame.
- `END` carries the latest 26-byte SID body plus byte 26 for `$D021`, matching the useful 27-byte DOS extension, and alone closes the visual generation after all its cells.
- The controller snapshot is a separately versioned C64-to-Teensy command/register shape with its own sequence, checksum, and ACK. It is not an outbound display packet.

The C64 terminal and Teensy firmware must conformance-test exact identity, protocol, command/type, length, sequence, CRC, ACK, and engine-active conditions. A mismatch fails closed before applying a cell, SID register, background, or input. Legacy MPE4/MPE5 terminals keep their existing semantics.

Foreground service priority is:

1. latch/ACK the independent inbound controller transaction, with the terminal submitting it before requesting the next pull packet;
2. publish a due `AUDIO` packet;
3. publish a bounded batch of `CELL` packets followed by its `END`;
4. publish diagnostics.

Report **complete-frame cadence** as acknowledged `END` generations per wall-clock interval, separately from emulated PPU frames, converted candidates, and individual cell packets.

Retain sequence, CRC, commit-last publication, exact ACK, retry, and fail-closed semantics. If a reset, timeout, bank deselection, or terminal error makes the C64's partially applied frame uncertain, invalidate the diff baseline and require a full refresh after a clean handshake. A new bulk, row/column, tile-aware, or bus-master transport can be investigated only after the existing path is physically benchmarked. Historical DMA attempts are not a shortcut: host-safe scatter behavior previously locked real NTSC hardware, so any gameplay DMA is a new protocol and hardware qualification project.

### Early physical transport spike

Run this before completing the emulator:

- Generate deterministic NES-derived static, sprite-motion, palette-animation, and continuous-scroll cell sequences.
- Exercise full-frame and dirty-cell paths, plus both multicolor and experimental hires conversion.
- Replay exact packets into the actual C64 receiver in host tests.
- Stream them through a real Teensy 4.1 to a C64 and to a C128 in C64 mode.
- Record cells/frame, packets/frame, ACK latency/distribution, complete-frame cadence, visible tearing, C64 CPU load, input-to-emulation latency, input-to-visible latency, SID-update jitter, retries/CRC faults, and PAL/NTSC host differences.

Do not promise a presentation FPS until that measurement exists. A static screenshot or one clean boot does not pass this spike. Continuous scrolling remains blocked if the measured cadence/latency is not playable; the remedy is a newly proven transport, not skipped NES cycles.

## APU and SID

### Authoritative NES APU

Keep the NES APU independent from the SID adapter:

- Two pulse channels with duty, envelope, length, and sweep units.
- Triangle timer, length counter, and linear counter.
- Noise timer/LFSR, envelope, and length counter.
- Clock pulse/noise timer units at their CPU/2 cadence and triangle at CPU cadence; keep frame-counter clocks and register side effects on their specified cycle edges.
- DMC registers, sample address/length, output level, status/IRQ, and DMA timing at the tier that claims DMC compatibility.
- Four-/five-step frame counter timing, `$4015/$4017` side effects, frame IRQ, channel enable/disable behavior, and length-counter effects.
- NES nonlinear-mixer behavior in a host reference backend even though SID cannot reproduce it exactly.

References: [APU registers](https://www.nesdev.org/wiki/APU_registers), [APU status](https://www.nesdev.org/wiki/APU_Status), and [NES mixer](https://www.nesdev.org/wiki/APU_Mixer).

### Tier-1 SID adapter

Reuse the existing 26-byte SID body: one retrigger byte plus `$D400-$D418` state. NESVM carries that body in its independent `AUDIO` packet and as the prefix of its 27-byte visual `END`; the C64 writes registers directly, and Teensy must calculate values using the actual target SID clock.

- NES pulse 1 -> SID voice 1 pulse.
- NES pulse 2 -> SID voice 2 pulse.
- NES triangle -> SID voice 3 triangle.
- Map frequency with the measured NTSC/PAL SID clock and deterministic integer rounding.
- Map four NES pulse duty choices to documented SID pulse widths; volume/envelope changes become bounded SID ADSR/gate/volume changes.
- When noise is audible, steal the quietest/lowest-priority SID voice according to one documented rule, restore its tonal owner cleanly, and count steals.
- Tier 1 keeps DMC audible output muted while reporting that limitation. Correct DMC status/DMA is still required before claiming software that depends on it.
- Coalesce redundant SID states, but do not let a video transfer stretch APU tempo. Keep a small ordered queue whose emulated-cycle stamps support diagnostics and safe coalescing, then interleave due `AUDIO` packets ahead of video. The C64 applies Tier-1 audio immediately on receipt: timestamps do not remove ACK/receiver jitter. If the measured bound is not acceptable, a later C64-side scheduled audio ring is required rather than a false timing claim.

The result is recognizable, intentionally SID-flavored sound, not faithful NES audio. High-rate `$D418` sample playback or external Teensy audio is a separate experiment with its own bandwidth/hardware requirements.

The current MinimalBoot DOS path has used an NTSC SID clock because its reduced launch context lacks a complete video-standard contract. NESVM must either receive the detected host standard explicitly or stay NTSC-only and reject/mask unsupported use. Tier 1 chooses NTSC-only.

## Input and controller semantics

### Exact user-facing mapping

The logical NES controller bitmap uses the NES serial order:

| Bit | NES button | Input |
| ---: | --- | --- |
| 0 | A | joystick port 2 fire |
| 1 | B | `SPACE` |
| 2 | SELECT | either `SHIFT` |
| 3 | START | `RETURN` / Enter |
| 4 | Up | joystick port 2 up |
| 5 | Down | joystick port 2 down |
| 6 | Left | joystick port 2 left |
| 7 | Right | joystick port 2 right |

This is the 2026-09-04 mapping. Physical letter A/B and cursor keys are not game controls in R1. In particular, shifted cursor Up/Left would conflict with Shift=Select, so no keyboard-only directional fallback is enabled. Opposite Up+Down or Left+Right resolves to neutral. Ctrl+Commodore+F7 is display-only Sharp Text control, not a NES button. No second player is exposed.

C128 MVP operation is C64 mode using VIC-II, CIA keyboard/joystick, and SID. Native C128 keyboard/VDC/2 MHz behavior is not inferred from that result.

### Capture protocol

The current AGI input contract is insufficient because fire becomes a rising edge and only one keyboard edge is consumed per tick. NESVM needs a dedicated full held-state path:

- Sample all assigned matrix positions individually plus joystick port 2's active-low direction/fire bits; do not choose a single winning key.
- Sample joystick with the keyboard matrix driven to a known inactive state, account for joystick lines that can masquerade as matrix closures, and restore the CIA data-direction/output state expected by the terminal. Simultaneous port-2 direction/fire plus `SPACE`, `RETURN`, or either `SHIFT` is a required case, not an unsupported collision. Also test the display shortcut while gameplay inputs are held.
- Publish a checked, sequenced 8-bit logical held snapshot whenever it changes, with a wrapping terminal scan counter for ordering/latency diagnostics. Input submission and ACK run before the terminal asks Teensy for another outbound pull packet, so a long visual transfer cannot starve a release.
- Preserve short taps with a small ordered transition FIFO captured from the raster/regular scan. Apply each transition at an emulated CPU-cycle boundary. If a press and its release arrive before any `$4016` 1-to-0 latch observed the press, postpone that release until the first such latch or two emulated PPU frames after the press, whichever comes first; at the cap, release it and count an `unobservedTap`. This documented minimum-observation policy intentionally stretches some very short taps but cannot leave a key stuck indefinitely.
- Also retain a replaceable `latest state` and periodic resynchronization snapshot. If the transition queue overflows, flag it, deliver the newest complete state, and guarantee that an all-released state converges; a missed release must never leave A, B, Start, Select, or a direction stuck.
- Keep terminal/menu text events separate from the controller bitmap.
- The PHI2 handler only latches the validated snapshot/sequence. The foreground poller updates the emulator's live physical-controller state.

### NES serial port behavior

- While bit 0 written to `$4016` is 1, controller 1 continually presents current A.
- The 1-to-0 strobe transition freezes one complete eight-button snapshot.
- Successive `$4016` reads return A, B, Select, Start, Up, Down, Left, Right. Reads after the eighth return 1 for the standard controller policy.
- The latched snapshot is immutable during those eight reads even if the C64 input changes.
- `$4017` controller reads return the disconnected-controller pattern in Tier 1; its writes still control the APU frame counter.

Reference: [NES controller](https://www.nesdev.org/wiki/NES_controller) and [controller reading](https://www.nesdev.org/wiki/Controller_reading).

### Mandatory input tests

- Hold and release every individual button across many emulated frames.
- Taps shorter than a slow video ACK and releases during a long full-screen transfer.
- Press+release delivered before the next controller latch, no-latch timeout, one-latch observation, and the `unobservedTap` counter.
- A+B+direction, direction+Start, direction+Select, Start+Select, and diagonal+A+B.
- Both Shift keys as Select; letter B/cursor keys must not substitute for the specified controls.
- Sharp Text chord, repeat/held protection, modifier-first release, Shift/F8 exclusion, complete-frame mode changes, and no changes to emulated game state.
- Opposite-direction policy and transition back to a single direction.
- Queue-full/retry/duplicate/out-of-order/checksum-failure behavior followed by all-zero release convergence.
- Physical C64 and C128-in-C64-mode matrix ghosting/phantom combinations. Record combinations the electrical matrix cannot distinguish; do not hide them by dropping all multi-key input.
- No joystick-port-1 dependency and no regression of the menu's existing input after NESVM exits.

## RAM, ROM, and stack budget

### Current linked implementation

The local NESVM firmware candidate currently has:

- a 200 KiB initialized RAM1 cartridge image, with NESVM objects and selected-ROM bytes borrowing only the unused tail behind the fully resident 24,688-byte CRT;
- 23,296 bytes left for local variables and stack in the successful MinimalBoot link;
- 186,912 bytes of RAM2 variables plus a 337,376-byte system heap reserve; NESVM does not allocate from RAM2.

RAM2 remains the system/DOSVM 512 KiB region and is not an NES budget. The NES machine, renderer, two VIC cell images, menu state, SID adapter, and the selected ROM occupy the reclaimed RAM1 cartridge tail. The current candidate fits the authorized 98,320-byte Mapper-11 Crossbow image while retaining the measured stack floor. Optional PSRAM is not an R1 assumption.

The exact `MHS NESVM` identity owns this RAM1 tail only while the reusable terminal is resident. Reset, bank deselection, and launch failure clear the session state. The folder list is bounded at 128 entries and the hot path performs no SD reads after a game starts.

### Working production budget

These are engineering bounds to prove, not measurements of a linked NES build:

| Item | Thwaite profile | Full NROM-256 consideration |
| --- | ---: | ---: |
| CPU internal RAM | 2,048 | 2,048 |
| CIRAM | 2,048 | 2,048 |
| primary/secondary OAM + palette | about 320 | about 320 |
| PPU scanline scratch | target <= 512 | target <= 512 |
| hot PRG ROM | 16,384 | 32,768 |
| hot CHR ROM | 8,192 | 8,192 |
| PRG RAM/NVRAM | 0 | 0 or 8,192 when supported |
| two 10,000-byte VIC cell images | 20,000 | 20,000 |
| immutable packet/input/audio queues | target <= 1,024 | target <= 1,024 |
| CPU/PPU/APU/mapper/session state | target <= 8,000 | target <= 8,000 |
| Approximate total before alignment | about 57.2 KiB | about 73.2 KiB, or about 81.2 KiB with PRG RAM |

The Thwaite profile is intentionally selected because a compact, scanline-sink implementation might fit the existing 64 KiB arena. That is not proven until the real types are linked. Full NROM-256 does not fit this working budget and is a separate memory decision.

### Required memory gate

Before integrating gameplay:

1. Produce `sizeof` reports for CPU, PPU, APU, mapper, loader/cache, session, cell images, and queues.
2. Produce a firmware link map showing RAM1, RAM2, arena, stack, heap floor, and all large objects.
3. Fill the NES arena with a canary, run worst-case host sequences, and report high-water/alignment/guard integrity.
4. Boot and exit every existing native engine after adding the NES owner; run all arena ownership tests.
5. On physical hardware, exercise SD browsing/load/unload before and after NESVM to detect heap or lifecycle damage.

If Tier 1 exceeds 64 KiB, choose openly among:

- reduce production buffers/state while preserving host reference buffers;
- introduce a deliberate reset-only/quiesced RAM2 ownership model and re-prove the builder's heap requirement and every existing engine;
- require and validate a specific PSRAM configuration for a higher tier.

Do not silently allocate the excess from the general heap. A larger static arena without a new whole-firmware memory proof is not acceptable.

### ROM access rule

CPU PRG and PPU CHR reads are latency-critical and cannot invoke SD. Tier 1 loads the selected `.nes` file's 24 KiB hot ROM set at launch into the budgeted arena and performs no SD read during emulation. Do not preload the folder's other ROMs. Include bounded directory-page/return-context storage and the generic terminal's live allocation in the whole-firmware budget; release or reuse browsing scratch before claiming the NES arena. Any optional internal CRT backing consumes runtime heap and must be measured after loading, not assumed free from the link map.

Code remains flash-resident. Avoid large automatic arrays and recursion; preserve at least the current stack floor unless a measured call-depth proof justifies a change.

## Save RAM

Battery persistence is plumbing in Tier 1 and a claim only in a tier/board that declares NVRAM.

- Use a new versioned NES save format, not the AGI save schema.
- Filename prefix should be distinct, for example `/SAVES/NES-<short-id>.sav`; the short token is only a locator.
- Header stores format version, complete ROM SHA-256, mapper/submapper, PRG/CHR NVRAM sizes, payload length, generation, and header/payload CRCs.
- Persist exactly the board's declared nonvolatile bytes. Do not serialize volatile CPU/PPU state as a battery save.
- Perform I/O outside the PHI2 handler and outside emulated bus callbacks.
- Follow the repository's recoverable transaction: write temporary file, flush, read back and validate, preserve the previous valid file as `.bak`, then rename into place. Restore validates identity and sizes and may fall back to the verified backup.
- Mark dirty on write; checkpoint after a bounded quiet/debounce interval and on clean unload. Rate-limit writes. On abrupt power loss, the previous verified generation must remain recoverable.
- Save states, rewind, and cross-core state compatibility are separate, versioned features.

Old iNES PRG-RAM declarations are ambiguous. Prefer NES 2.0 metadata or an explicit, reviewed compatibility record; never create 8 KiB of persistent RAM solely because an archaic header byte is zero.

## Implementation layout

Proposed source ownership, to be created by implementation milestones rather than by this plan:

```text
nes/
  README.md                  this plan
  COMPATIBILITY.md           hashes/metadata for redistributable tests only
  docs/                      wire, container, palette, and save format specs
  tools/                     inspector and optional developer fixture packer
  tests/                     host unit/integration tests and licensed fixtures
engine/native-nes/
  core/                      portable CPU/bus/PPU/APU/controller/mappers
  nes_session.*              fixed ownership, scheduler, raster sink
  nes_loader.*               direct SD iNES reader, launch token, hot backing
  nes_browser.*              bounded folder listing and return context
  nes_presenter.*            VIC cell conversion/diff/publication
  nes_sid.*                  APU-to-SID adapter
  nes_firmware.*             foreground poller and bounded mailbox latch
  vendor/                    pinned third-party code plus license/provenance
```

Keep portable core code free of Arduino, SD, VIC-II, SID, and mailbox dependencies. Inject bounded callbacks or interfaces for ROM backing, raster sink, audio observation, logging, and persistence. All production storage is fixed at launch; no per-frame allocation.

The generic C64 terminal is reproducibly built by `nes/tools/build_nesvm_terminal.mjs`. Its NES input overlay lives in this repository; the build manifest records hashes for that overlay and the shared AGI-64 terminal/boot sources it consumes. `nes/tools/build_nesvm.ps1` rebuilds and checks the terminal, CRT, VICE boot, core, layout, ROM policy, and exact one-demo SD package.

## Milestones and exit gates

### M0 - Baseline, target inventory, and decision records

Deliver:

- pin the repository baseline, candidate CPU provenance, target hardware, and the current Teensyduino 1.61.0 toolchain; treat 1.62.0 as unqualified because the repository records SD stalls with two PSRAM chips;
- specify direct-file launch descriptor, folder-return context, generic terminal identity, input snapshot, presentation, diagnostics, and save formats;
- audit licenses for the CPU core, each test ROM, Thwaite, and any palette data;
- record NTSC-only and C128-in-C64-mode scope.
- pin/build the exact Thwaite revision and produce a source audit plus trusted-reference trace covering every reachable game mode: executed official/unofficial opcodes, `$4010-$4017` APU/DMC/controller accesses, PPU registers/status flags, mirroring, sprite behavior, and RAM needs.

Exit: formats and license table reviewed; no private ROM data in Git. Every Tier-1 accuracy deferral is backed by the target inventory, or the missing behavior moves into Tier 1.

### M1 - Memory and transport feasibility spike

Deliver:

- skeleton fixed-size `NesSession` with exact type-size/link-map report;
- explicit arena-owner/lifecycle proposal;
- scanline-to-VIC converter and synthetic static/motion/scroll sequences;
- host receiver replay and real Teensy+C64/C128 throughput report;
- provisional latency/cadence product thresholds based on measured hardware, not estimates.

Exit: a reviewed route exists for fitting Tier 1 without violating the heap floor, and physical transport is good enough for a static-screen game. Otherwise stop or redesign transport before building the full emulator.

### M2 - Folder browser, direct ROM launch, and lifecycle

Deliver:

- a paged SD folder browser with case-insensitive `.nes` filtering, subfolder navigation, original filenames, and restored folder/row after exit;
- strict direct-file iNES/NES 2.0 loader, hash/byte-preservation checks, and malformed/oversize/unsupported rejection without falling back to PRG launch;
- an explicit `rtFileNES` route and validated path/identity/token handoff through full firmware, MinimalBoot, and the reusable C64 terminal; no required per-ROM cartridge, conversion, or PC tool;
- generic terminal source/build and firmware session dispatch; if its internal boot path uses a native CRT, add one exact 32-byte `MHS NESVM` identity to both full-firmware `CRTRequiresMPE3MinimalBoot()` and MinimalBoot `mpe4cart::matches()` checks;
- a separately numbered NES wire/input ABI implemented and conformance-tested on both the C64 and Teensy sides; shared definitions remain synchronized in full firmware and MinimalBoot;
- staging and hashing of every `engine/native-nes` source in `scripts/build-firmware.ps1`, an ordered selected-source integration patch, and extended source/artifact/memory/ownership audits under a new candidate/version; immutable native20/V1.0.12 artifacts are not rewritten;
- hot NROM-128 backing with no emulation-time SD reads;
- clean launch, error, reset, unload, bank-deselection/stale-mailbox teardown, and return-to-ROM-list paths across every init, failure, ACK, restart, and polling exit.

Exit: two distinct legal/synthetic `.nes` files can be selected from a folder and launched in sequence, with a clean return to the same list between them and no per-game conversion. Large directories, subfolders, long names, invalid/unsupported ROMs, stale paths, and SD read failures leave the browser recoverable. No commercial ROM or generated private fixture is tracked.

### M3 - Headless RP2A03 and bus

Deliver:

- per-cycle CPU/bus scheduler, 2A03 decimal behavior, interrupts, official opcodes, page/dummy/RMW ordering, OAM DMA;
- NROM-128 bus and controller shift register;
- deterministic traces and differential tests against a trusted reference.

Exit: `nestest` and focused licensed/local CPU/DMA/controller suites pass at pinned hashes. Any test ROM added to Git has verified redistribution rights.

### M4 - PPU and reference raster

Deliver:

- register/memory/scroll/NMI/palette/OAM/sprite behavior;
- host full-frame raster sink and golden CRC/image tests;
- production scanline VIC sink with deterministic palette/cell output;
- dirty-cell/presentation queues independent of emulated time.
- an on-Teensy benchmark that runs the per-cycle CPU, three-dot PPU, OAM DMA, rendering, scanline conversion, and wire-pump service against worst-case synthetic/NROM activity.

Exit: focused PPU suites pass; packet replay exactly reproduces expected C64 bitmap bytes; a host image is not treated as physical proof. The Teensy sustains the 1.789773 MHz NTSC schedule without chronic timer debt and reports measured worst-case headroom before APU/SID integration.

### M5 - APU, SID, and complete input

Deliver:

- pulse/triangle/noise/frame-counter APU state and host reference observations;
- three-voice SID adapter, noise arbitration, timing/jitter counters;
- full eight-bit held controller snapshots, transition FIFO, release convergence, keyboard fallback, and multi-key tests;
- DMC status/DMA plan and explicit audible limitation.

Exit: deterministic APU tests pass; all mapped combinations pass receiver replay; real hardware has no stuck controls and produces recognizable, stable SID-facing sound. Repeat the on-Teensy real-time benchmark with APU, SID translation, input capture, and representative terminal traffic; stop before game integration if it cannot hold the NTSC schedule.

### M6 - Thwaite playable profile

Deliver:

- exact licensed Thwaite source/revision/ROM identity in the local test record;
- host end-to-end run and VICE terminal smoke test;
- firmware/terminal/source-ROM hash tuple, direct folder-launch evidence, and physical test worksheet; an internal generic-terminal CRT hash is included only if that boot route is used.

Exit: the physical Tier-1 acceptance matrix below passes on both target host classes. Only then call the profile playable.

### M7 - Full NROM and scrolling gate

Deliver:

- reviewed memory architecture for NROM-256 and CHR RAM;
- remaining PPU/DMA/unofficial-opcode accuracy required by focused tests;
- private local Super Mario Bros. test from title through sustained World 1-1 scrolling, death/restart, and representative sprite-0/HUD transitions;
- measured scrolling cadence/latency report and, if necessary, a newly qualified transport.

Exit: host accuracy and real hardware scrolling/playability both pass. A private ROM hash may appear in a local worksheet, never in public artifacts unless the user deliberately keeps the report private.

### M8 - Mapper 2 and save plumbing

Deliver independently:

- Mapper 2 bank/bus-conflict tests plus a proven hot-ROM memory strategy;
- a battery-backed synthetic harness and power-loss recovery test for persistence plumbing only;
- regression of the required Tier-1 folder-launch workflow with larger supported ROMs and save-capable sessions;
- PAL or other hardware/audio profiles only after separate timing work.

Exit: each feature carries its own host, firmware, and physical evidence. None is bundled into the Mapper-0 completion claim retroactively. Mapper 0 and ordinary Mapper 2 still make no real-cartridge save-compatibility claim; the first such claim needs a separately planned, exact battery-capable board, with MMC1/SNROM plus 8 KiB battery PRG RAM as the default candidate rather than an invented NROM/UxROM profile.

## Verification and acceptance matrix

### Host/component evidence

Host tests must be deterministic and record tool/core/test hashes.

- Browser/launch: empty and large paged folders, subfolders, mixed-case extensions, long/display-truncated names with exact path identity, only one ROM loaded, read-only ROM folder, file removed/replaced between list and load, SD failures, descriptor preservation/revalidation across MinimalBoot, failed-launch cleanup, return-folder/row restoration, and game A -> exit -> game B without conversion.
- Loader: signature, truncation at every section, integer overflow, trailing bytes policy, NES 2.0 detection, RAM shifts, exponent sizing rejection/support, trainer rejection, region/console rejection, mapper/submapper/mirroring, exact round trip.
- CPU: official instructions, decimal-disabled arithmetic, interrupts/BRK/reset, page crossings, branch timing, stack wrap, indirect JMP, RMW/dummy accesses, stable unofficial set when enabled.
- Bus/DMA: RAM/PPU mirrors, open bus policy, OAM 513/514 cycles, DMC interactions when implemented, mapper writes.
- Power/reset: cold power-up registers and write suppression, first vblank/NMI, deterministic and randomized unspecified memory, warm-reset preservation, mapper reset, unclean relaunch recovery.
- PPU: vblank/NMI races, `$2002`, `$2005/$2006`, `$2007` buffer/palette/increment, mirroring, odd-frame skip, clipping, grayscale/emphasis, sprite priority/zero/eight-per-line.
- Controller: strobe-high behavior, 1-to-0 latch, serial order, post-eight reads, immutable report, delayed input arrival.
- APU: frame sequencer, status/IRQ, envelopes, sweeps, length/linear counters, periods, enable side effects, DMC timing at claimed tier.
- Presenter: palette LUT, scale/crop, per-cell color choice, deterministic ties/hysteresis, full/dirty equivalence, frame coalescing, CRC/retry/ACK, wraparound.
- Saves: identity/size mismatch, temp write, readback failure, rename interruption, backup restore, dirty debounce, unload, simulated power loss.

[NESdev's emulator-test index](https://www.nesdev.org/wiki/Emulator_tests) is a test discovery source, not a blanket license. `nestest` should be the first CPU trace only after its local provenance/use is recorded.

Host passes establish component behavior and exact expected bytes. They do not establish Teensy real-time scheduling or C64 electrical/bus behavior.

### C64 terminal/VICE evidence

- Deterministic terminal build and provenance hashes.
- Generic terminal boots, displays errors safely, applies recorded cell/SID/end packets, detects corrupt/duplicate/out-of-order packets, and returns through the folder-browser handoff. An internal CRT fixture can test the receiver but cannot prove direct `.nes` selection.
- CIA input scanner produces exact eight-bit snapshots under scripted matrices/joystick values.
- VICE smoke run shows receiver compatibility and obvious screen layout issues.

VICE does not emulate Teensy GPIO timing, real SD stalls, VIC-II DMA interaction with the custom bus, physical keyboard ghosting, SID model/audio quality, or end-to-end latency. It is not gameplay acceptance.

### Firmware integration evidence

- Selected GUI snapshot and patch count/digests.
- Build provenance, complete source manifest, firmware hash, link map, stack estimate, RAM1/RAM2/arena/heap figures, CPU vendor hash/license.
- Existing MPE2/title/AGI/DOS ownership and source checks remain green.
- No forbidden bus-handler growth or slow ISR work.

A successful build proves fit for that artifact. It still does not prove a physical launch, play speed, input, display, or sound.

### Physical Tier-1 acceptance

Test a real Teensy 4.1 on both:

- an NTSC C64; and
- an NTSC C128 explicitly placed in C64 mode.

Record exact firmware SHA-256, selected source ROM SHA-256 and local path, terminal build hash, optional internal generic-terminal CRT hash, Teensy board/options, host model/revision, VIC/SID model, SD card, and video standard. Keep private library paths/hashes in local test records only.

Required observations:

- repeated cold boot, opening an SD folder, selecting an ordinary `.nes`, running it, exiting to the same folder/selection, and choosing another ROM without any PC tool, ROM conversion, or firmware rebuild;
- empty/large directories, subfolders, long names, unsupported/malformed ROMs, file/SD read failure, clean error recovery, reset, unload, and relaunch; verify unchanged source ROM hashes and no required per-ROM CRT output;
- at least 30 minutes of sustained Thwaite gameplay on each host class, including start/restart/game-over paths and representative screen changes;
- emulated CPU/frame counters versus wall time, timer-debt/catch-up maxima, displayed-frame cadence, cells/packets, ACK latency, dropped presentation frames, retry/CRC/error counts, and visible tearing;
- press/release and all mandatory simultaneous-control cases, including keyboard-only play, with no stuck input after long transfers or exit;
- measured press-to-emulation and press-to-visible latency against the thresholds chosen from M1;
- audible pulse/triangle/noise behavior, stable music tempo while video is busy, recorded SID packet jitter, and clean silence/gates on reset/exit;
- no lockup, spontaneous reset, bus corruption, menu corruption, or SD failure.

For a later battery-capable supported board, add save, power-off, reload, fallback-from-interrupted-write, and identity-mismatch tests. For the scrolling gate, add sustained full-level play rather than accepting a title screen or static room.

Physical evidence is the only basis for the words **playable on C64/128 hardware**. A failure here reopens the responsible subsystem even when all host tests pass.

## Risk register

| Risk | Consequence | Mitigation / stop condition |
| --- | --- | --- |
| 64 KiB arena and protected heap floor | session does not fit or breaks SD/other engines | scanline sink; Thwaite-sized profile; linked map/high-water first; explicit new ownership model or stop |
| instruction-granular CPU core | incorrect PPU/APU/DMA/controller behavior despite opcode passes | per-cycle micro-operations or replace core; test ROMs decide |
| C64 pull bandwidth | stale/tearing display and high latency, especially scrolling | decouple time, coalesce latest, prioritize input/audio, run M1 physical spike; new transport if gate fails |
| VIC-II palette/resolution limits | unreadable text, lost detail, flicker | fixed LUT, deterministic full-frame fit, cell hysteresis, per-title visual review; document approximation |
| three SID voices vs five NES channels | missing/noisy music and DMC | authoritative APU separate from adapter; deterministic arbitration; explicit fidelity limits; measured audio queue |
| keyboard matrix and delayed ACK | lost taps or stuck controls | full bitmap, transition FIFO, latest-state resync, all-zero convergence, physical ghosting matrix tests |
| emulation-time SD access | severe stalls and audio/timing debt | preload all Tier-1 hot PRG/CHR; reject profiles without proven backing |
| Mapper 2 ROM capacity | immediate bank switches cannot tolerate cache miss | postpone until measured resident/PSRAM/other deterministic strategy exists |
| save interruption/metadata ambiguity | corruption or wrong-ROM save | explicit supported boards, SHA-256 identity, exact lengths, temp/verify/backup/rename |
| bus ISR regression | physical lockups not visible on host | no slow ISR work, cycle-count/source checks, exact hardware gates, fail closed |
| commercial/test ROM licensing | unlawful or unreproducible release | local selection, no bundled commercial data, per-fixture license/provenance audit |
| PAL/C128 scope creep | wrong speed/pitch or misleading compatibility | NTSC + C64 mode only in Tier 1; separate host/video-standard gates later |

## Completion definition

The NESVM Tier-1 profile is complete only when:

- the user can open an SD ROM folder, select an ordinary `.nes` from its list, run it, exit, and select another game without conversion or per-game cartridges;
- the direct-file loader accepts the documented Thwaite/NROM-128 profile and rejects unsupported/malformed files safely while preserving the folder list;
- CPU, PPU, controller, APU, presentation, transport, lifecycle, and memory gates have reproducible host evidence;
- the selected firmware contains pinned source/provenance and stays within validated RAM/stack/ISR bounds without regressing existing engines;
- real Teensy+C64 and Teensy+C128-in-C64-mode sessions pass the full physical worksheet, including sustained gameplay, held/released/multi-key input, SID-facing sound, measured timing/latency, repeated exit/reload, and no bus or SD corruption; and
- no private or commercially copyrighted ROM bytes are committed or released.

Anything less may be a useful emulator-core, presenter, firmware-build, or laboratory milestone, but it is not finished hardware gameplay.

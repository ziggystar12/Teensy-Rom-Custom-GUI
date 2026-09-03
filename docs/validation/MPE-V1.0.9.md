# MPE Firmware V1.0.9 validation

V1.0.9 / native17 addresses SD discovery at cold startup and places the shared
clickable shortcut strip on Home as well as file windows. The native AGI engine,
cartridge format and save identities remain unchanged from the stable V1.0.8
release. Existing game cartridges do not need rebuilding.

Source validation and the native game/save checks are complete. The final
artifact and source-lock checks are recorded below. No physical C64/Teensy has
been flashed or controlled as part of this work.

## SD discovery behavior

V1.0.7 and V1.0.8 could sample SD DAT3 immediately after changing its pin mode.
The corrected probe waits five microseconds before reading pin 46, matching
the SD detection sequence in the library bundled with pinned Teensy core 1.61.
This is a source correction; its effect on cold physical starts still needs
hardware testing.

A failed detection, initialization, root access or candidate read leaves
discovery retryable. Cancellation while reading the candidate also leaves the
check retryable. An explicit GUI action that opens SD retries that optional
check; there is no background retry loop. The completed-check flag is set only
after an enumeration finds no newer candidate or a readable candidate is fully
prepared for confirmation. Subsequent SD opens in the same TeensyROM session do
not rescan or repeat a dismissed offer. Restarting TeensyROM resets discovery
and permits another scan; switching GUI/Text with V does not reset the flag.

The existing selection rules remain: only regular SD-root files matching
`MPE_Firmware-Vx.y.z.hex` with a strictly newer three-part numeric version are
candidates. Version components are compared numerically, not alphabetically.
Installed/older versions, malformed names, directories and restore images are
ignored. The highest candidate is shown in the shared confirmation dialog;
Cancel is the default. Filename discovery is not permission to flash.

The captured source, path and file fingerprint survive browser changes
without retargeting the update. Confirmation and final launch retain the
existing changed-file checks and consume the affirmative once. Failed discovery
leaves ordinary browsing and launches available. Discovery never renames
or deletes the candidate.

Executed production backend tests pass 103 scenarios: 30 firmware-target checks
and 73 discovery checks. These cover probe ordering, initialization and read
failures, retry and completed-scan behavior, candidate selection, cancellation,
view preservation and changed-file rejection. Executed C64 checks also cover
the explicit SD-open retry, one final redraw after no candidate or Cancel, and
the absence of discovery on other sources, ordinary redraws and text-mode opens.

Because the fix cannot run inside an older installed image, install V1.0.9
manually once from V1.0.7 or V1.0.8. After reboot, confirm the version in
TEENSY > About MPE Firmware. Update progress before reboot belongs to the
previously running desktop.

## Home shortcut strip

Home and browser windows share the same seven labels and hit targets:
F1 Help, F2 BASIC, F3 SD, F5 USB, F7 MEM, F8 PANEL and V TEXT. The last label
extends beyond pixel 255 and must retain its high X byte. Keyboard actions,
raw PROGMEM launch indices, the persistent GUI/Text preference, and the shared
mouse route are retained.

Executed Home and browser composition checks pass for all seven rendered labels
and matching mouse targets, including gaps and the final V label. Home selection
updates leave the footer intact, and Arrange retains its instructions. The
source suite also passes the existing Help/Settings/BASIC launch and saved
preference checks. Generated `build/native17-ui-proof/native-home.png` and
`build/native17-ui-proof/native-browser.png` were inspected and show the seven labels;
these are software-rendered previews, not physical display captures.

## Source checkpoint and reproduction

The selected GUI source checkpoint is
`2c9dd70724fadf1dbd86fe29ad91d9abb4a7304c`. Its 129-file snapshot is recorded in
`gui/selected-v1.0.9/provenance.json` with digest
`f78f3765f9ba95303e3077e1c7f8df7ee7e07dd38cd718c6b6fb330e083dca39`.

The reviewed backend patch contains 20 files and 75,442 bytes, with SHA-256
`2411ac2d005d78b53b99ba96666b353e0e847c39b13e6f3b764a5559fcf93dfa`.
`build/native17-backend-proof/verification.json` records successful apply and
reverse checks and exact agreement of the applied bytes against upstream
`3436b8fbd7c642ef9eabc691d3d09da08a6a6690`.

This release retains the stable 37-patch native AGI build. Ongoing DOS
development on `main` is outside this public image. Reproduction starts at the
exact `engineCommit` in `docs/firmware/source.lock.json`, using the
[detached-worktree instructions](../FIRMWARE-GUIDE.md#reproduce-the-release-source).
The source-lock check must verify the committed manifest and the exact Git
bytes of its build tools, alongside the engine, patch, backend, selected GUI
and configuration hashes.

## Completed source checks and C64 payloads

`build/native17-ui-proof/native17-source-tests.log` records **281 passed, 0 failed, 0 skipped**.
The 50 focused C64 checks are included in that total. The 103 backend scenarios
above execute within the source suite and are not 103 additional test cases.
The suite also covers launch routing, persistent GUI/Text preference, menu
rendering and IRQ behavior, and the maintained browser and firmware guards.

The assembled payloads remain inside the existing limits:

| Payload | Bytes | Limit | Remaining |
| --- | ---: | ---: | ---: |
| Desktop code | 22,466 | 22,528 | 62 |
| Resident apps | 4,074 | 4,096 | 22 |
| Desktop PRG | 26,659 | — | — |
| Compact cartridge (`TeensyROMC64.bin`) | 7,559 | 8,192 | 633 |
| Help | 2,631 | — | — |
| Settings | 6,270 | — | — |

## Final combined firmware

`MPE_Firmware-V1.0.9.hex`: 6,192,725 bytes. SHA-256:
`aa1c55d675f9076d904d4151007ce3ca22842ef473170a3341503b125ba0685b`.
The final-image audit checks both ARM images, 544 audited inputs, all 129
snapshot files, all four embedded C64 programs, and all nine unchanged native
AGI sources. MinimalBoot retains 16,416 bytes of stack reserve and 271,488 bytes
of RAM2 heap reserve.

The complete desktop PRG extracted from this HEX also passes the three
text-flow checks: SD Card, 16 captured filenames, cached menu restoration, and
all seven actual updater messages with exact published glyph pixels. Its
SHA-256 is `faa7255049f90998b96c83119953ea6c8fbda031a99accdbf189fceae5bd54b6`.
The replay log is retained in `build/native17-ui-proof/native17-text-release.log`.

## Native game and save checks

The integrated native module passes both sprite and legacy bitmap runs using
unchanged SQ1 data. The sprite run completes 862 frames, 1,184 packets, 350
inputs, 64 direction reversals, 88 sprite packets and 44 sprite commits with
zero input interrupt masks. Both runs pass five SAVES directory checks, eight
restore/fallback checks and six injected transaction failures, with no root
mutations. These execute the actual integrated firmware module with simulated
SD and bus pins; they do not establish physical bus timing.

The Black Cauldron demo remains unchanged, with SHA-256
`a235a7925332df82dc30e9c966c7038d75b70294e8294330236346e316ff2d7c`.
Native proof is retained in `build/native17-final-proof/firmware-native-result.json`,
SHA-256 `4321276f0e1fb4ff23daa9b6a97605b22911f51e955f06fc53734509ff72a1ea`.

## Final source-lock and artifact audit

The final combined-image audit passes and is retained in
`build/native17-final-proof/artifact-audit.json`, SHA-256
`bd195c37436cbcf167b9d4d3588f58c7142d7284530e2756cedd1e12c057fa04`.
The immutable [native17 manifest](../../releases/native17/manifest.json)
records the release inputs and payload checksums. Build manifests and logs
are retained under `build/native17-build-record`; source tests and rendered
Home/browser previews are under `build/native17-ui-proof`.

All 26 release/provenance checks pass without skips, including historical
payload preservation, current source and artifact hashes, and exact committed
build-tool verification. The source lock points to release checkpoint
`d7e1c67377f5a50f53e4005e38558def3be62062`. The check log is retained in
`build/native17-ui-proof/native17-release-checks.log`.

The previous [V1.0.8 validation](MPE-V1.0.8.md) and
[native16 manifest](../../releases/native16/manifest.json) preserve the baseline
record. They are not substitutes for the V1.0.9 checks. Cold starts with an SD
card, retry after an unavailable card, a real confirmed update, Home clicks,
pointer motion and SID continuity remain physical acceptance checks.

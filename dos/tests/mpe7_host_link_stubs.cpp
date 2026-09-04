// SPDX-License-Identifier: GPL-2.0-or-later
// Fail-closed target seams for the DOSVM integrated host harness.
//
// The harness compiles the real combined MPE3/MPE4/MPE5/MPE6/MPE7 dispatcher,
// including the production Doom session/runtime/video code. It does not link
// the complete MCUME Doom core or a Teensy linker script, so these target-only
// symbols keep an accidental DOOMVM launch unavailable while allowing the
// DOSVM path to be exercised end to end.

#include "../../engine/native-doom/mpe7_target.h"
#include "../../doom/third_party/mcume-native-adapter/mhs_native_adapter.h"

extern "C" {
uint8_t external_psram_size = 0;
uint8_t __mpe7_zone_start[1] = {};
uint8_t __mpe7_zone_end[1] = {};
uint8_t __mpe7_data_load[1] = {};
uint8_t __mpe7_data_start[1] = {};
uint8_t __mpe7_data_end[1] = {};
uint8_t __mpe7_bss_start[1] = {};
uint8_t __mpe7_bss_end[1] = {};
uint8_t __mpe7_runtime_start[1] = {};
uint8_t __mpe7_runtime_end[1] = {};
}

bool MPE7TargetPrepare(const char *, void *, size_t) { return false; }
bool MPE7TargetBeginClaimed(void *, size_t) { return false; }
void MPE7TargetResetBeforeClaim() {}
bool MPE7TargetHealthy() { return false; }
const char *MPE7TargetLastError() { return "DOOMVM unavailable in DOS host harness"; }
size_t MPE7TargetPrivateHeapUsed() { return 0; }
size_t MPE7TargetPrivateHeapHighWater() { return 0; }
size_t MPE7TargetEmuArenaUsed() { return 0; }
size_t MPE7TargetEmuArenaHighWater() { return 0; }

extern "C" {
int MHS_DoomStart(const char *) { return 0; }
int MHS_DoomRunOneTic(uint32_t, const mhs_doom_action_transition_t *, size_t) { return 0; }
void MHS_DoomFatal(const char *) {}
void MHS_DoomStop() {}
const uint8_t *MHS_DoomFramebuffer(size_t *bytes) { if (bytes) *bytes = 0; return nullptr; }
const uint8_t *MHS_DoomPaletteRgb(size_t *bytes) { if (bytes) *bytes = 0; return nullptr; }
const char *MHS_DoomLastError() { return "DOOMVM unavailable in DOS host harness"; }
int MHS_DoomGametic() { return 0; }
int MHS_DoomInE1M1() { return 0; }
int32_t MHS_DoomPlayerX() { return 0; }
int32_t MHS_DoomPlayerY() { return 0; }
uint32_t MHS_DoomPlayerAngle() { return 0; }
int MHS_DoomPlayerBulletAmmo() { return 0; }
uint32_t MHS_DoomLatchedTapCount() { return 0; }
uint32_t MHS_DoomSchedulerResyncs() { return 0; }
uint32_t MHS_DoomPostedDownMask() { return 0; }
uint32_t MHS_DoomPostedUpMask() { return 0; }
uint32_t MHS_DoomPostedEventCount() { return 0; }
int MHS_DoomEventsDrained() { return 0; }
int MHS_DoomActionKey(uint32_t) { return -1; }
uint32_t MHS_DoomPostedActionAt(uint32_t) { return 0; }
int MHS_DoomPostedPressedAt(uint32_t) { return 0; }
}

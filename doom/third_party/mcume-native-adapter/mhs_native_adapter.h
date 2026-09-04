// SPDX-License-Identifier: GPL-2.0-or-later
//
// This adapter links with the GPL-2.0-or-later MCUME Teensy Doom core.

#ifndef MHS_NATIVE_ADAPTER_H
#define MHS_NATIVE_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// This boundary is intentionally a one-process host proof. The pinned MCUME
// port has no complete teardown/reinitialize path yet, so production firmware
// must not claim Session reset/reentry support from this adapter.
#define MHS_DOOM_ADAPTER_HOST_PROOF_ONLY 1

enum mhs_doom_action {
    MHS_DOOM_ACTION_FORWARD = 1u << 0,
    MHS_DOOM_ACTION_BACKWARD = 1u << 1,
    MHS_DOOM_ACTION_TURN_LEFT = 1u << 2,
    MHS_DOOM_ACTION_TURN_RIGHT = 1u << 3,
    MHS_DOOM_ACTION_STRAFE_LEFT = 1u << 4,
    MHS_DOOM_ACTION_STRAFE_RIGHT = 1u << 5,
    MHS_DOOM_ACTION_FIRE = 1u << 6,
    MHS_DOOM_ACTION_USE = 1u << 7,
    MHS_DOOM_ACTION_RUN = 1u << 8,
    MHS_DOOM_ACTION_MAP = 1u << 9,
    MHS_DOOM_ACTION_MENU = 1u << 10,
    MHS_DOOM_ACTION_WEAPON1 = 1u << 11,
    MHS_DOOM_ACTION_WEAPON2 = 1u << 12,
    MHS_DOOM_ACTION_WEAPON3 = 1u << 13,
    MHS_DOOM_ACTION_WEAPON4 = 1u << 14,
    MHS_DOOM_ACTION_WEAPON5 = 1u << 15,
    MHS_DOOM_ACTION_WEAPON6 = 1u << 16,
    MHS_DOOM_ACTION_WEAPON7 = 1u << 17,
    MHS_DOOM_ACTION_ALL = (1u << 18) - 1u
};

typedef struct mhs_doom_action_transition {
    uint32_t action;
    uint8_t pressed;
} mhs_doom_action_transition_t;

// Initializes the pinned core directly in shareware E1M1. The caller owns the
// WAD path and must keep it valid only for this call. The MCUME port is not
// restart-safe in one process; after stop, start intentionally fails closed.
int MHS_DoomStart(const char *wad_path);

// Posts ordered make/break transitions before final held reconciliation,
// advances exactly one gametic, and renders one indexed frame. A complete
// down+up tap that starts and ends released is latched through this gametic;
// its release and any later ordered edges are processed immediately afterward.
// Timing belongs to mpe_doom::Session.
int MHS_DoomRunOneTic(
    uint32_t held_actions,
    const mhs_doom_action_transition_t *ordered_transitions,
    size_t transition_count);
// Fatal errors unwind only to the currently armed C adapter entrypoint.  This
// never crosses a C++ frame and lets the firmware publish a fail-closed error
// packet instead of sleeping forever inside the original Arduino port.
void MHS_DoomFatal(const char *message);
void MHS_DoomStop(void);

const uint8_t *MHS_DoomFramebuffer(size_t *bytes);
const uint8_t *MHS_DoomPaletteRgb(size_t *bytes);
const char *MHS_DoomLastError(void);
int MHS_DoomGametic(void);
int MHS_DoomInE1M1(void);
int32_t MHS_DoomPlayerX(void);
int32_t MHS_DoomPlayerY(void);
uint32_t MHS_DoomPlayerAngle(void);
int MHS_DoomPlayerBulletAmmo(void);
uint32_t MHS_DoomLatchedTapCount(void);
uint32_t MHS_DoomSchedulerResyncs(void);

// Test evidence for the adapter boundary. These counters record events posted
// into Doom's real queue; MHS_DoomEventsDrained confirms its responders drained
// that queue during the most recent one-tic call.
uint32_t MHS_DoomPostedDownMask(void);
uint32_t MHS_DoomPostedUpMask(void);
uint32_t MHS_DoomPostedEventCount(void);
int MHS_DoomEventsDrained(void);
int MHS_DoomActionKey(uint32_t action);
uint32_t MHS_DoomPostedActionAt(uint32_t index);
int MHS_DoomPostedPressedAt(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif

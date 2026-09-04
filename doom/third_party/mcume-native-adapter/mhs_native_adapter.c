// SPDX-License-Identifier: GPL-2.0-or-later
//
// Native MHS scheduling/input/video boundary for the pinned MCUME Teensy Doom
// core. The upstream copyright and GPL notices remain intact in the generated
// adapted tree; this file is distributed under the same license.

#include "mhs_native_adapter.h"

#include <setjmp.h>
#include <string.h>

#include "d_event.h"
#include "d_loop.h"
#include "d_main.h"
#include "doom.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_video.h"
#include "m_controls.h"
#include "w_wad.h"
#include "z_zone.h"

#define MHS_DOOM_FRAME_BYTES (320u * 200u)
#define MHS_DOOM_PALETTE_BYTES (256u * 3u)
#define MHS_DOOM_EVENT_LOG_CAPACITY 64u

extern boolean advancedemo;
extern gamestate_t wipegamestate;
extern void D_Display(void);
extern const byte *MHS_I_CurrentPalette(size_t *bytes);

static uint32_t mhs_held_actions;
static uint32_t mhs_posted_down;
static uint32_t mhs_posted_up;
static uint32_t mhs_posted_events;
static uint32_t mhs_latched_taps;
static uint32_t mhs_posted_action_log[MHS_DOOM_EVENT_LOG_CAPACITY];
static uint8_t mhs_posted_pressed_log[MHS_DOOM_EVENT_LOG_CAPACITY];
static int mhs_started;
static int mhs_ever_started;
static int mhs_fatal_armed;
static jmp_buf mhs_fatal_jump;
static char mhs_error[96];

static void MHS_SetError(const char *text)
{
    if (text == NULL)
    {
        text = "unknown MCUME adapter error";
    }
    strncpy(mhs_error, text, sizeof(mhs_error) - 1u);
    mhs_error[sizeof(mhs_error) - 1u] = '\0';
}

void MHS_DoomFatal(const char *message)
{
    MHS_SetError(message);
    mhs_started = 0;
    if (mhs_fatal_armed)
    {
        longjmp(mhs_fatal_jump, 1);
    }
    // A fatal outside Start/RunOneTic is an adapter contract violation.  The
    // reset-only target cannot safely unwind or return to another firmware
    // mode; physical reset remains the recovery boundary.
    for (;;) {}
}

int MHS_DoomActionKey(uint32_t action)
{
    switch (action)
    {
        case MHS_DOOM_ACTION_FORWARD: return key_up;
        case MHS_DOOM_ACTION_BACKWARD: return key_down;
        case MHS_DOOM_ACTION_TURN_LEFT: return key_left;
        case MHS_DOOM_ACTION_TURN_RIGHT: return key_right;
        case MHS_DOOM_ACTION_STRAFE_LEFT: return key_strafeleft;
        case MHS_DOOM_ACTION_STRAFE_RIGHT: return key_straferight;
        case MHS_DOOM_ACTION_FIRE: return key_fire;
        case MHS_DOOM_ACTION_USE: return key_use;
        case MHS_DOOM_ACTION_RUN: return key_speed;
        case MHS_DOOM_ACTION_MAP: return key_map_toggle;
        case MHS_DOOM_ACTION_MENU: return key_menu_activate;
        case MHS_DOOM_ACTION_WEAPON1: return key_weapon1;
        case MHS_DOOM_ACTION_WEAPON2: return key_weapon2;
        case MHS_DOOM_ACTION_WEAPON3: return key_weapon3;
        case MHS_DOOM_ACTION_WEAPON4: return key_weapon4;
        case MHS_DOOM_ACTION_WEAPON5: return key_weapon5;
        case MHS_DOOM_ACTION_WEAPON6: return key_weapon6;
        case MHS_DOOM_ACTION_WEAPON7: return key_weapon7;
        default: return -1;
    }
}

static int MHS_PostAction(uint32_t action, int pressed)
{
    event_t event;
    int key = MHS_DoomActionKey(action);

    if (key < 0)
    {
        MHS_SetError("held action has no Doom key mapping");
        return 0;
    }

    memset(&event, 0, sizeof(event));
    event.type = pressed ? ev_keydown : ev_keyup;
    event.data1 = key;
    event.data2 = key;
    D_PostEvent(&event);
    if (mhs_posted_events < MHS_DOOM_EVENT_LOG_CAPACITY)
    {
        mhs_posted_action_log[mhs_posted_events] = action;
        mhs_posted_pressed_log[mhs_posted_events] = pressed ? 1u : 0u;
    }
    ++mhs_posted_events;
    if (pressed)
    {
        mhs_posted_down |= action;
        mhs_held_actions |= action;
    }
    else
    {
        mhs_posted_up |= action;
        mhs_held_actions &= ~action;
    }
    return 1;
}

static int MHS_TransitionValid(
    const mhs_doom_action_transition_t *transition)
{
    uint32_t action = transition->action;
    return action == 0u ||
        (((action & (action - 1u)) == 0u) &&
         ((action & MHS_DOOM_ACTION_ALL) == action));
}

static int MHS_PostRange(
    const mhs_doom_action_transition_t *ordered_transitions,
    size_t begin,
    size_t end)
{
    size_t index;
    uint32_t action;

    for (index = begin; index < end; ++index)
    {
        action = ordered_transitions[index].action;
        if (action == 0u)
        {
            continue;
        }
        if (!MHS_TransitionValid(&ordered_transitions[index]) ||
            !MHS_PostAction(action, ordered_transitions[index].pressed != 0u))
        {
            MHS_SetError("ordered transition has an invalid Doom action");
            return 0;
        }
    }
    return 1;
}

static int MHS_ReconcileHeld(uint32_t held_actions)
{
    uint32_t changed;
    uint32_t action;

    held_actions &= MHS_DOOM_ACTION_ALL;
    changed = held_actions ^ mhs_held_actions;
    for (action = 1u; action <= MHS_DOOM_ACTION_WEAPON7; action <<= 1u)
    {
        if ((changed & action) != 0u &&
            !MHS_PostAction(action, (held_actions & action) != 0u))
        {
            return 0;
        }
    }
    return 1;
}

static int MHS_PrepareInput(
    uint32_t held_actions,
    const mhs_doom_action_transition_t *ordered_transitions,
    size_t transition_count,
    size_t *deferred_begin)
{
    size_t index;
    uint32_t seen_press = 0u;
    const uint32_t initial_held = mhs_held_actions;
    const uint32_t final_held = held_actions & MHS_DOOM_ACTION_ALL;

    if (deferred_begin == NULL ||
        (transition_count != 0u && ordered_transitions == NULL))
    {
        MHS_SetError("ordered transition metadata is invalid");
        return 0;
    }
    *deferred_begin = transition_count;
    for (index = 0u; index < transition_count; ++index)
    {
        const mhs_doom_action_transition_t *transition =
            &ordered_transitions[index];
        const uint32_t action = transition->action;
        if (!MHS_TransitionValid(transition))
        {
            MHS_SetError("ordered transition has an invalid Doom action");
            return 0;
        }
        if (action == 0u)
        {
            continue;
        }
        if (transition->pressed)
        {
            if ((initial_held & action) == 0u)
            {
                seen_press |= action;
            }
        }
        else if ((seen_press & action) != 0u &&
                 (final_held & action) == 0u)
        {
            *deferred_begin = index;
            ++mhs_latched_taps;
            break;
        }
    }

    if (!MHS_PostRange(ordered_transitions, 0u, *deferred_begin))
    {
        return 0;
    }
    return *deferred_begin != transition_count || MHS_ReconcileHeld(final_held);
}

static int MHS_FinishInput(
    uint32_t held_actions,
    const mhs_doom_action_transition_t *ordered_transitions,
    size_t transition_count,
    size_t deferred_begin)
{
    if (deferred_begin == transition_count)
    {
        return 1;
    }
    if (!MHS_PostRange(ordered_transitions, deferred_begin, transition_count) ||
        !MHS_ReconcileHeld(held_actions))
    {
        return 0;
    }
    // These events are chronologically after the sampled gametic. Apply them
    // now so the engine and merged held mask agree before the next call.
    D_ProcessEvents();
    return 1;
}

int MHS_DoomStart(const char *wad_path)
{
    size_t palette_bytes = 0u;

    mhs_error[0] = '\0';
    if (wad_path == NULL || wad_path[0] == '\0')
    {
        MHS_SetError("a WAD path is required");
        return 0;
    }
    if (mhs_started || mhs_ever_started)
    {
        MHS_SetError("the MCUME core cannot restart safely in one process");
        return 0;
    }

    mhs_ever_started = 1;
    mhs_held_actions = 0u;
    mhs_posted_down = 0u;
    mhs_posted_up = 0u;
    mhs_posted_events = 0u;
    mhs_latched_taps = 0u;
    memset(mhs_posted_action_log, 0, sizeof(mhs_posted_action_log));
    memset(mhs_posted_pressed_log, 0, sizeof(mhs_posted_pressed_log));

    if (setjmp(mhs_fatal_jump) != 0)
    {
        mhs_fatal_armed = 0;
        return 0;
    }
    mhs_fatal_armed = 1;

    D_DoomMain((char *) wad_path);
    G_InitNew(sk_medium, 1, 1);
    advancedemo = false;
    demoplayback = false;
    singledemo = false;
    wipegamestate = gamestate;
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));

    if (!D_BeginExternalTicControl())
    {
        MHS_SetError("MCUME could not transfer scheduling to MHS");
        mhs_fatal_armed = 0;
        return 0;
    }
    if (I_VideoBuffer == NULL || gamestate != GS_LEVEL ||
        gameepisode != 1 || gamemap != 1 ||
        MHS_I_CurrentPalette(&palette_bytes) == NULL ||
        palette_bytes != MHS_DOOM_PALETTE_BYTES)
    {
        MHS_SetError("MCUME did not initialize indexed E1M1 video");
        mhs_fatal_armed = 0;
        return 0;
    }

    mhs_started = 1;
    mhs_fatal_armed = 0;
    return 1;
}

int MHS_DoomRunOneTic(
    uint32_t held_actions,
    const mhs_doom_action_transition_t *ordered_transitions,
    size_t transition_count)
{
    int before;
    size_t deferred_begin;

    if (!mhs_started)
    {
        MHS_SetError("one-tic call made while MCUME is stopped");
        return 0;
    }
    if (setjmp(mhs_fatal_jump) != 0)
    {
        mhs_fatal_armed = 0;
        return 0;
    }
    mhs_fatal_armed = 1;
    if (!MHS_PrepareInput(held_actions, ordered_transitions, transition_count,
                          &deferred_begin))
    {
        mhs_fatal_armed = 0;
        return 0;
    }

    before = gametic;
    if (!D_RunSingleTic() || gametic != before + 1)
    {
        (void) MHS_FinishInput(held_actions, ordered_transitions,
                               transition_count, deferred_begin);
        switch (D_ExternalTicFailure())
        {
            case 2: MHS_SetError("one-tic core mode changed"); break;
            case 3: MHS_SetError("one-tic queue was not empty on entry"); break;
            case 4: MHS_SetError("one-tic command build failed"); break;
            case 5: MHS_SetError("one-tic command build buffered wrong count"); break;
            case 6: MHS_SetError("one-tic core advanced wrong count"); break;
            default: MHS_SetError("MCUME failed the exactly-one-gametic contract"); break;
        }
        mhs_fatal_armed = 0;
        return 0;
    }
    if (!MHS_FinishInput(held_actions, ordered_transitions, transition_count,
                         deferred_begin))
    {
        mhs_fatal_armed = 0;
        return 0;
    }

    // Suppress the original wipe animation: it is presentation-time work, not
    // simulation, and the MHS session owns frame pacing.
    wipegamestate = gamestate;
    D_Display();
    mhs_fatal_armed = 0;
    return 1;
}

void MHS_DoomStop(void)
{
    if (mhs_started)
    {
        (void) MHS_ReconcileHeld(0u);
        D_ProcessEvents();
    }
    mhs_started = 0;
    mhs_held_actions = 0u;
}

const uint8_t *MHS_DoomFramebuffer(size_t *bytes)
{
    if (bytes != NULL)
    {
        *bytes = mhs_started ? MHS_DOOM_FRAME_BYTES : 0u;
    }
    return mhs_started ? I_VideoBuffer : NULL;
}

const uint8_t *MHS_DoomPaletteRgb(size_t *bytes)
{
    if (!mhs_started)
    {
        if (bytes != NULL)
        {
            *bytes = 0u;
        }
        return NULL;
    }
    return MHS_I_CurrentPalette(bytes);
}

const char *MHS_DoomLastError(void)
{
    return mhs_error;
}

int MHS_DoomGametic(void)
{
    return gametic;
}

int MHS_DoomInE1M1(void)
{
    return mhs_started && gamestate == GS_LEVEL && gameepisode == 1 &&
        gamemap == 1;
}

int32_t MHS_DoomPlayerX(void)
{
    return MHS_DoomInE1M1() && players[consoleplayer].mo != NULL
        ? players[consoleplayer].mo->x
        : 0;
}

int32_t MHS_DoomPlayerY(void)
{
    return MHS_DoomInE1M1() && players[consoleplayer].mo != NULL
        ? players[consoleplayer].mo->y
        : 0;
}

uint32_t MHS_DoomPlayerAngle(void)
{
    return MHS_DoomInE1M1() && players[consoleplayer].mo != NULL
        ? players[consoleplayer].mo->angle
        : 0u;
}

int MHS_DoomPlayerBulletAmmo(void)
{
    return MHS_DoomInE1M1() ? players[consoleplayer].ammo[am_clip] : -1;
}

uint32_t MHS_DoomLatchedTapCount(void)
{
    return mhs_latched_taps;
}

uint32_t MHS_DoomSchedulerResyncs(void)
{
    return D_ExternalTicResyncs();
}

uint32_t MHS_DoomPostedDownMask(void)
{
    return mhs_posted_down;
}

uint32_t MHS_DoomPostedUpMask(void)
{
    return mhs_posted_up;
}

uint32_t MHS_DoomPostedEventCount(void)
{
    return mhs_posted_events;
}

int MHS_DoomEventsDrained(void)
{
    return D_EventQueueEmpty();
}

uint32_t MHS_DoomPostedActionAt(uint32_t index)
{
    return index < mhs_posted_events && index < MHS_DOOM_EVENT_LOG_CAPACITY
        ? mhs_posted_action_log[index]
        : 0u;
}

int MHS_DoomPostedPressedAt(uint32_t index)
{
    return index < mhs_posted_events && index < MHS_DOOM_EVENT_LOG_CAPACITY
        ? mhs_posted_pressed_log[index] != 0u
        : -1;
}

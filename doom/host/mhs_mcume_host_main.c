// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "doom.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_video.h"
#include "p_mobj.h"
#include "w_wad.h"
#include "z_zone.h"

#define MHS_FRAME_ITERATIONS 420u
#define MHS_MIN_CHANGED_TRANSITIONS 200u
#define MHS_MIN_UNIQUE_HASHES 128u
#define MHS_PLAYPAL_BYTES 768u

_Static_assert(sizeof(void *) == 4u,
               "The MCUME host proof must use a 32-bit compiler");
_Static_assert(SCREENWIDTH == 320, "Unexpected MCUME framebuffer width");
_Static_assert(SCREENHEIGHT == 200, "Unexpected MCUME framebuffer height");

extern boolean advancedemo;
extern int joystick;
extern void host_advance_time(uint32_t milliseconds);

static uint32_t mhs_fnv1a(const unsigned char *bytes, size_t length)
{
    uint32_t hash = 2166136261u;
    size_t index;

    for (index = 0u; index < length; ++index)
    {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

static unsigned int mhs_unique_hashes(const uint32_t *hashes,
                                      unsigned int count)
{
    unsigned int unique = 0u;
    unsigned int index;

    for (index = 0u; index < count; ++index)
    {
        unsigned int earlier;
        for (earlier = 0u; earlier < index; ++earlier)
        {
            if (hashes[earlier] == hashes[index])
            {
                break;
            }
        }
        if (earlier == index)
        {
            ++unique;
        }
    }
    return unique;
}

static int mhs_write_exact(const char *path, const void *bytes, size_t length)
{
    FILE *stream = fopen(path, "wb");
    size_t written;
    int close_result;

    if (stream == NULL)
    {
        return 0;
    }
    written = fwrite(bytes, 1u, length, stream);
    close_result = fclose(stream);
    return written == length && close_result == 0;
}

static int mhs_fail(const char *check)
{
    fprintf(stdout, "{\"status\":\"FAIL\",\"check\":\"%s\"}\n", check);
    fflush(stdout);
    return 1;
}

int main(int argc, char **argv)
{
    uint32_t hashes[MHS_FRAME_ITERATIONS];
    unsigned int changed_transitions = 0u;
    unsigned int level_frames = 0u;
    unsigned int frame;
    unsigned int unique_hashes;
    int palette_lump;
    int palette_length;
    const unsigned char *palette;
    fixed_t initial_x;
    fixed_t initial_y;
    angle_t initial_angle;
    int initial_ammo;
    fixed_t final_x;
    fixed_t final_y;
    angle_t final_angle;
    int final_ammo;

    if (argc != 4)
    {
        fprintf(stderr,
                "usage: mhs-mcume-doom-host32 WAD FRAME-OUTPUT PALETTE-OUTPUT\n");
        return 64;
    }

    D_DoomMain(argv[1]);
    G_InitNew(sk_medium, 1, 1);
    advancedemo = false;
    demoplayback = false;
    singledemo = false;

    if (I_VideoBuffer == NULL)
    {
        return mhs_fail("framebuffer-allocation");
    }
    if (gamestate != GS_LEVEL || gameepisode != 1 || gamemap != 1)
    {
        return mhs_fail("e1m1-initialization");
    }
    if (players[consoleplayer].mo == NULL)
    {
        return mhs_fail("player-initialization");
    }

    initial_x = players[consoleplayer].mo->x;
    initial_y = players[consoleplayer].mo->y;
    initial_angle = players[consoleplayer].mo->angle;
    initial_ammo = players[consoleplayer].ammo[am_clip];

    for (frame = 0u; frame < MHS_FRAME_ITERATIONS; ++frame)
    {
        // Match the known MCUME joystick layout: forward, right, and fire.
        // The bounded movement makes the rendered view change while remaining
        // inside E1M1 for the duration of the proof.
        joystick = frame >= 30u && frame < 300u
            ? (0x01 | 0x08 | 0x10)
            : 0;
        host_advance_time(29u);
        D_DoomLoop();

        if (I_VideoBuffer == NULL)
        {
            return mhs_fail("framebuffer-lifetime");
        }
        if (gamestate == GS_LEVEL && gameepisode == 1 && gamemap == 1)
        {
            ++level_frames;
        }

        hashes[frame] = mhs_fnv1a(I_VideoBuffer,
                                  (size_t) SCREENWIDTH * SCREENHEIGHT);
        if (frame > 0u && hashes[frame] != hashes[frame - 1u])
        {
            ++changed_transitions;
        }
    }

    unique_hashes = mhs_unique_hashes(hashes, MHS_FRAME_ITERATIONS);
    if (level_frames != MHS_FRAME_ITERATIONS || gamestate != GS_LEVEL)
    {
        return mhs_fail("gs-level-hold");
    }
    if (changed_transitions < MHS_MIN_CHANGED_TRANSITIONS ||
        unique_hashes < MHS_MIN_UNIQUE_HASHES)
    {
        return mhs_fail("moving-frame-evidence");
    }

    if (players[consoleplayer].mo == NULL)
    {
        return mhs_fail("player-lifetime");
    }
    final_x = players[consoleplayer].mo->x;
    final_y = players[consoleplayer].mo->y;
    final_angle = players[consoleplayer].mo->angle;
    final_ammo = players[consoleplayer].ammo[am_clip];
    if ((final_x == initial_x && final_y == initial_y) ||
        final_angle == initial_angle || final_ammo >= initial_ammo)
    {
        return mhs_fail("input-state-change");
    }

    palette_lump = W_GetNumForName("PLAYPAL");
    palette_length = W_LumpLength(palette_lump);
    if (palette_length < (int) MHS_PLAYPAL_BYTES)
    {
        return mhs_fail("playpal-length");
    }
    palette = W_CacheLumpNum(palette_lump, PU_STATIC);
    if (palette == NULL)
    {
        return mhs_fail("playpal-load");
    }

    if (!mhs_write_exact(argv[2], I_VideoBuffer,
                         (size_t) SCREENWIDTH * SCREENHEIGHT))
    {
        return mhs_fail("frame-write");
    }
    if (!mhs_write_exact(argv[3], palette, MHS_PLAYPAL_BYTES))
    {
        return mhs_fail("playpal-write");
    }

    fprintf(stdout,
            "{\"status\":\"PASS\",\"pointerBits\":32,"
            "\"screenWidth\":%d,\"screenHeight\":%d,"
            "\"episode\":%d,\"map\":%d,\"iterations\":%u,"
            "\"levelFrames\":%u,\"changedTransitions\":%u,"
            "\"uniqueFrameHashes\":%u,\"firstFrameFnv1a\":\"%08x\","
            "\"lastFrameFnv1a\":\"%08x\",\"frameBytes\":%u,"
            "\"paletteBytes\":%u,\"playerMoved\":true,"
            "\"playerTurned\":true,\"ammoSpent\":%d}\n",
            SCREENWIDTH, SCREENHEIGHT, gameepisode, gamemap,
            MHS_FRAME_ITERATIONS, level_frames, changed_transitions,
            unique_hashes, hashes[0], hashes[MHS_FRAME_ITERATIONS - 1u],
            (unsigned int) (SCREENWIDTH * SCREENHEIGHT), MHS_PLAYPAL_BYTES,
            initial_ammo - final_ammo);
    fflush(stdout);
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MPE7_TARGET_H
#define MPE7_TARGET_H

#include <stddef.h>
#include <stdint.h>

#ifndef MPE7_TARGET_CODE
#ifdef FLASHMEM
#define MPE7_TARGET_CODE FLASHMEM
#else
// Keep the API header self-contained for host/static checks.  The Teensy
// implementation includes Arduino.h first and therefore still receives the
// flash section attribute; the firmware linker also routes the whole object.
#define MPE7_TARGET_CODE
#endif
#endif

// Prepare every SD and RAM1 resource while the ordinary Teensy heap is still
// valid. After beginClaimed(), the target is reset-only and must not call the
// firmware malloc/new implementation again.
MPE7_TARGET_CODE bool MPE7TargetPrepare(const char *wad_path,
                                        void *private_heap,
                                        size_t private_heap_bytes);
MPE7_TARGET_CODE bool MPE7TargetBeginClaimed(void *emu_arena,
                                             size_t emu_arena_bytes);
MPE7_TARGET_CODE void MPE7TargetResetBeforeClaim();

MPE7_TARGET_CODE bool MPE7TargetHealthy();
MPE7_TARGET_CODE const char *MPE7TargetLastError();
MPE7_TARGET_CODE size_t MPE7TargetPrivateHeapUsed();
MPE7_TARGET_CODE size_t MPE7TargetPrivateHeapHighWater();
MPE7_TARGET_CODE size_t MPE7TargetEmuArenaUsed();
MPE7_TARGET_CODE size_t MPE7TargetEmuArenaHighWater();

#endif

// SPDX-License-Identifier: GPL-2.0-or-later
// First-playable DOOMVM producer for the established MPE3 C64 terminal.
//
// This header is included once by IOH_MPE3TitlePull.c, after MPE5 and MPE6.
// All state declared here therefore remains in ordinary RAM1.  The adapted
// Doom C objects use a linker overlay in RAM2 and may be initialized only
// after the normal RAM2 allocator and USB1 DMA owner have been retired.
#pragma once

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../mhs_native_adapter.h"
#include "../../mpe7_target.h"
#include "../../mpe_doom_session.h"

extern "C" uint8_t external_psram_size;

// These are linker boundaries, not C arrays.  The target linker fragment
// places the Doom zone at the physical eight-MiB PSRAM base and places only
// the adapted Doom core's mutable image in the RAM2 overlay.
extern "C" {
extern uint8_t __mpe7_zone_start[];
extern uint8_t __mpe7_zone_end[];
extern uint8_t __mpe7_data_load[];
extern uint8_t __mpe7_data_start[];
extern uint8_t __mpe7_data_end[];
extern uint8_t __mpe7_bss_start[];
extern uint8_t __mpe7_bss_end[];
extern uint8_t __mpe7_runtime_start[];
extern uint8_t __mpe7_runtime_end[];
}

static constexpr uint8_t MPE7Protocol = 1u;
static constexpr uint8_t MPE7DescriptorBytes = 16u;
static constexpr uint8_t MPE7RequiredPsramMiB = 8u;
static constexpr uint8_t MPE7RequiredRam2Blocks = 64u;
static constexpr uint32_t MPE7Ram2Base = 0x20200000u;
static constexpr uint32_t MPE7Ram2Bytes = 512u * 1024u;
static constexpr uint32_t MPE7Ram2Limit = MPE7Ram2Base + MPE7Ram2Bytes;
static constexpr uint32_t MPE7ZoneBase = 0x70000000u;
static constexpr uint32_t MPE7ZoneBytes = 8u * 1024u * 1024u;
static constexpr uint32_t MPE7MinimumPrivateHeapBytes = 128u * 1024u;
static constexpr uint32_t MPE7MinimumEmuArenaBytes = 80u * 1024u;
static constexpr size_t MPE7WadPathBytes = 256u;

// Doom follows DOS's reset-only ownership model, but has its own explicit
// owner so a stale or partially completed handoff cannot masquerade as DOS.
static constexpr MHSNativeArenaOwner MPE7ArenaOwner =
   MHSNativeArenaOwner::Doom;

struct MPE7LatchedInput
{
   uint8_t key;
   uint8_t scan;
   uint8_t joy;
   uint8_t flags;
   uint8_t sequence;
};

struct MPE7InputEdge
{
   uint8_t scan;
   bool pressed;
};

// ISR-visible state and the ownership sentinel must never enter the RAM2
// overlay.  The Session and private core allocator live in the unused tail of
// the fully resident three-page DOOMVM cartridge.
static volatile bool MPE7Active;
static volatile bool MPE7InputPending;
static volatile bool MPE7Ram2Owned;
static volatile MPE7LatchedInput MPE7Input;
static mpe_doom::Session *MPE7Session;
static MHSNativeArenaView MPE7ArenaView;
static char MPE7WadPath[MPE7WadPathBytes];
static uint8_t *MPE7PrivateHeap;
static size_t MPE7PrivateHeapBytes;
static uint8_t *MPE7EmuArena;
static size_t MPE7EmuArenaBytes;
static uint8_t *MPE7VideoWorkspace;
static bool MPE7SessionConstructed;
static bool MPE7FirstFrame;
static bool MPE7FirstCellPacket;
static bool MPE7FrameEndSidPending;
static bool MPE7FaultPending;
static bool MPE7FaultPublished;
static uint8_t MPE7PreviousScan;
static uint8_t MPE7PreviousModifiers;
static uint8_t MPE7Error;

static_assert(mpe_doom::Video::RecordBytes == MPE3TitleCellBytes,
              "Doom/MPE3 cell record ABI changed");
static_assert(mpe_doom::Video::Cells == 1000u,
              "Doom producer must emit one complete VIC frame");
static_assert(MPE3TitleCellsPerPacket == 19u,
              "Doom producer assumes the established 228-byte payload");
static_assert((uint32_t)mpe_doom::kActionAll == MHS_DOOM_ACTION_ALL,
              "Doom Session and core action masks changed independently");

static FLASHMEM uint32_t MPE7Read32(const uint8_t *data)
{
   return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
      ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static FLASHMEM uintptr_t MPE7AlignUp(uintptr_t value, uintptr_t alignment)
{
   return (value + alignment - 1u) & ~(alignment - 1u);
}

static FLASHMEM bool MPE7WadPathValid(const char *path, size_t bytes)
{
   if (!path || bytes < 2u || bytes >= MPE7WadPathBytes || path[0] != '/')
      return false;

   size_t component = 1u;
   for (size_t index = 1u; index <= bytes; ++index)
   {
      const bool end = index == bytes;
      const uint8_t value = end ? (uint8_t)'/' : (uint8_t)path[index];
      if (!end && (value < 0x20u || value > 0x7eu || value == '\\' ||
                   value == ':' || value == 0u))
         return false;
      if (value != '/') continue;

      const size_t length = index - component;
      if (!length || (length == 1u && path[component] == '.') ||
          (length == 2u && path[component] == '.' &&
           path[component + 1u] == '.'))
         return false;
      component = index + 1u;
   }
   return true;
}

static FLASHMEM void MPE7RecordFault(uint8_t error)
{
   if (!MPE7FaultPending)
   {
      MPE7Error = error ? error : (uint8_t)MPE3TitleErrorMemory;
      MPE7FaultPending = true;
   }
}

static FLASHMEM void MPE7Reset()
{
   // Once sealed, even reading an object whose storage was in RAM2 can be
   // unsafe.  The hardware reset is the only destructor for a live MPE7.
   if (MPE7Ram2Owned || MHSNativeArenaRequiresReset()) return;

   MPE7TargetResetBeforeClaim();
   if (MPE7SessionConstructed && MPE7Session)
      MPE7Session->~Session();
   if (MHSNativeArenaOwns(MPE7ArenaOwner))
      (void)MHSNativeArenaRelease(MPE7ArenaOwner);

   MPE7Active = false;
   MPE7InputPending = false;
   MPE7Session = nullptr;
   MPE7ArenaView = {};
   MPE7WadPath[0] = '\0';
   MPE7PrivateHeap = nullptr;
   MPE7PrivateHeapBytes = 0u;
   MPE7EmuArena = nullptr;
   MPE7EmuArenaBytes = 0u;
   MPE7VideoWorkspace = nullptr;
   MPE7SessionConstructed = false;
   MPE7FirstFrame = true;
   MPE7FirstCellPacket = true;
   MPE7FrameEndSidPending = false;
   MPE7FaultPending = false;
   MPE7FaultPublished = false;
   MPE7PreviousScan = 0u;
   MPE7PreviousModifiers = 0u;
   MPE7Error = 0u;
   MPE7Input.key = MPE7Input.scan = MPE7Input.joy = 0u;
   MPE7Input.flags = MPE7Input.sequence = 0u;
}

static FLASHMEM bool MPE7RejectStart(uint8_t error)
{
   MPE7Reset();
   MPE7Error = error;
   return false;
}

static FLASHMEM bool MPE7ValidateLinkedLayout()
{
   const uintptr_t dataLoad = (uintptr_t)__mpe7_data_load;
   const uintptr_t dataStart = (uintptr_t)__mpe7_data_start;
   const uintptr_t dataEnd = (uintptr_t)__mpe7_data_end;
   const uintptr_t bssStart = (uintptr_t)__mpe7_bss_start;
   const uintptr_t bssEnd = (uintptr_t)__mpe7_bss_end;
   const uintptr_t runtimeStart = (uintptr_t)__mpe7_runtime_start;
   const uintptr_t runtimeEnd = (uintptr_t)__mpe7_runtime_end;
   const uintptr_t zoneStart = (uintptr_t)__mpe7_zone_start;
   const uintptr_t zoneEnd = (uintptr_t)__mpe7_zone_end;

   if (external_psram_size < MPE7RequiredPsramMiB ||
       zoneStart != MPE7ZoneBase || zoneEnd != MPE7ZoneBase + MPE7ZoneBytes ||
       dataStart != MPE7Ram2Base || dataStart > dataEnd ||
       dataEnd > bssStart || bssStart > bssEnd || bssEnd > runtimeStart ||
       runtimeStart > runtimeEnd || runtimeEnd != MPE7Ram2Limit ||
       (dataLoad >= MPE7Ram2Base && dataLoad < MPE7Ram2Limit))
      return false;

   // Keep the converter at the aligned top of RAM2.  emu_Malloc receives only
   // the prefix, so the core can never overwrite Session's video workspace.
   if (mpe_doom::Video::WorkspaceBytes > runtimeEnd - runtimeStart)
      return false;
   const uintptr_t workspace = (runtimeEnd -
      mpe_doom::Video::WorkspaceBytes) & ~(uintptr_t)31u;
   if (workspace < runtimeStart ||
       workspace - runtimeStart < MPE7MinimumEmuArenaBytes ||
       runtimeEnd - workspace < mpe_doom::Video::WorkspaceBytes)
      return false;

   MPE7EmuArena = (uint8_t *)runtimeStart;
   MPE7EmuArenaBytes = workspace - runtimeStart;
   MPE7VideoWorkspace = (uint8_t *)workspace;
   return true;
}

static FLASHMEM void MPE7InitializeCoreOverlay()
{
   const size_t dataBytes =
      (size_t)((uintptr_t)__mpe7_data_end - (uintptr_t)__mpe7_data_start);
   const size_t bssBytes =
      (size_t)((uintptr_t)__mpe7_bss_end - (uintptr_t)__mpe7_bss_start);
   if (dataBytes)
      memcpy(__mpe7_data_start, __mpe7_data_load, dataBytes);
   if (bssBytes)
      memset(__mpe7_bss_start, 0, bssBytes);
#if defined(__arm__) || defined(__thumb__)
   __asm__ volatile("dsb\n\tisb" ::: "memory");
#endif
}

static FLASHMEM bool MPE7CoreStart(void *context)
{
   const char *path = static_cast<const char *>(context);
   return MPE7TargetHealthy() && MHS_DoomStart(path) != 0 &&
      MPE7TargetHealthy();
}

static FLASHMEM bool MPE7CoreRunOneTic(
   void *, const mpe_doom::CoreTicInput *input)
{
   if (!input || !MPE7TargetHealthy()) return false;
   mhs_doom_action_transition_t transitions[
      mpe_doom::Controls::kScanEventCapacity];
   size_t count = 0u;
   for (uint8_t index = 0u; index < input->scan_event_count; ++index)
   {
      const mpe_doom::ActionMask action =
         mpe_doom::defaultActionsForScan(input->scan_events[index].scan_code);
      if (!action) continue;
      if ((action & (action - 1u)) != 0u) return false;
      transitions[count].action = (uint32_t)action;
      transitions[count].pressed = input->scan_events[index].pressed ? 1u : 0u;
      ++count;
   }
   return MHS_DoomRunOneTic((uint32_t)input->held_actions,
      count ? transitions : nullptr, count) != 0 && MPE7TargetHealthy();
}

static FLASHMEM const uint8_t *MPE7CoreFramebuffer(void *, size_t *bytes)
{
   return MHS_DoomFramebuffer(bytes);
}

static FLASHMEM const uint8_t *MPE7CorePalette(void *, size_t *bytes)
{
   return MHS_DoomPaletteRgb(bytes);
}

static FLASHMEM void MPE7CoreStop(void *)
{
   MHS_DoomStop();
}

static FLASHMEM const char *MPE7CoreError(void *)
{
   if (!MPE7TargetHealthy()) return MPE7TargetLastError();
   return MHS_DoomLastError();
}

static FLASHMEM bool MPE7Start(uint32_t root)
{
   uint8_t descriptor[MPE7DescriptorBytes];
   MPE7Reset();
   if (MPE7Ram2Owned || MHSNativeArenaRequiresReset())
   { MPE7Error = MPE3TitleErrorMemory; return false; }

   if (!MPE4Read(nullptr, root, descriptor, sizeof(descriptor)) ||
       memcmp(descriptor, "M7D1", 4u) ||
       descriptor[4] != MPE7Protocol ||
       descriptor[5] != MPE7DescriptorBytes ||
       descriptor[6] != MPE7RequiredPsramMiB ||
       descriptor[7] != MPE7RequiredRam2Blocks)
      return MPE7RejectStart(MPE3TitleErrorHeader);

   const uint32_t pathBytes = MPE7Read32(descriptor + 8u);
   if (!pathBytes || pathBytes >= MPE7WadPathBytes ||
       root > UINT32_MAX - MPE7DescriptorBytes ||
       !MPE4Read(nullptr, root + MPE7DescriptorBytes,
                 (uint8_t *)MPE7WadPath, (uint16_t)pathBytes))
      return MPE7RejectStart(MPE3TitleErrorBounds);
   if (MHSNativeCRC32((const uint8_t *)MPE7WadPath, pathBytes) !=
          MPE7Read32(descriptor + 12u) ||
       !MPE7WadPathValid(MPE7WadPath, pathBytes))
      return MPE7RejectStart(MPE3TitleErrorHeader);
   MPE7WadPath[pathBytes] = '\0';

   if (!MPE7ValidateLinkedLayout())
      return MPE7RejectStart(MPE3TitleErrorMemory);

   uint8_t *tail = nullptr;
   uint32_t tailBytes = 0u;
   if (!MPE5BorrowCartridgeTail(&tail, &tailBytes) || !tail)
      return MPE7RejectStart(MPE3TitleErrorMemory);
   const uintptr_t tailStart = (uintptr_t)tail;
   const uintptr_t tailLimit = tailStart + tailBytes;
   const uintptr_t sessionStart =
      MPE7AlignUp(tailStart, alignof(mpe_doom::Session));
   if (tailLimit < tailStart || sessionStart < tailStart ||
       sessionStart > tailLimit ||
       sizeof(mpe_doom::Session) > tailLimit - sessionStart)
      return MPE7RejectStart(MPE3TitleErrorMemory);
   const uintptr_t heapStart = MPE7AlignUp(
      sessionStart + sizeof(mpe_doom::Session), 32u);
   if (heapStart < sessionStart || heapStart > tailLimit ||
       tailLimit - heapStart < MPE7MinimumPrivateHeapBytes)
      return MPE7RejectStart(MPE3TitleErrorMemory);

   MPE7Session = new ((void *)sessionStart) mpe_doom::Session(4u);
   MPE7SessionConstructed = true;
   MPE7PrivateHeap = (uint8_t *)heapStart;
   MPE7PrivateHeapBytes = tailLimit - heapStart;
   if (!MPE7TargetPrepare(MPE7WadPath, MPE7PrivateHeap,
                          MPE7PrivateHeapBytes))
      return MPE7RejectStart(MPE3TitleErrorRead);

   // No AGI producer or cache metadata may survive once its physical PSRAM
   // backing becomes Doom's zone.  These calls are still before takeover and
   // may use the ordinary firmware allocator if their cleanup needs it.
   AGIPictureReleaseSource();
   AGIPictureReleasePicture();
   AGIPictureReleaseScene();
   AGIPictureInvalidateLivePicture();

   MHSNativeArenaView view{};
   if (MHSNativeArenaClaim(MPE7ArenaOwner, MHSNativeArenaCapacity,
          MHSNativeArenaAlignment, &view) != MHSNativeArenaStatus::Okay ||
       !MHSNativeArenaLeaseValid(&view))
      return MPE7RejectStart(MPE3TitleErrorMemory);
   MPE7ArenaView = view;

   // The CRT loader's File implementation and debug buffer can point into the
   // normal RAM2 heap.  Retire them before USB1 and that allocator disappear.
   if (myFile) myFile.close();
   if (BigBuf) { free(BigBuf); BigBuf = nullptr; BigBufCount = 0; }
   if (!MPE5QuiesceRam2Services())
      return MPE7RejectStart(MPE3TitleErrorMemory);
   if (MHSNativeArenaSealResetOnly(MPE7ArenaOwner) !=
       MHSNativeArenaStatus::Okay)
   {
      // USB1 and the base heap are already gone.  Never attempt cleanup from
      // this point, even if the ownership control record was inconsistent.
      MPE7Ram2Owned = true;
      MPE7Error = MPE3TitleErrorMemory;
      return false;
   }

   MPE7Ram2Owned = true;
   MPE7InitializeCoreOverlay();
   if (!MPE7TargetBeginClaimed(MPE7EmuArena, MPE7EmuArenaBytes))
   { MPE7Error = MPE3TitleErrorMemory; return false; }

   const mpe_doom::CoreCallbacks callbacks = {
      MPE7WadPath, MPE7CoreStart, MPE7CoreRunOneTic,
      MPE7CoreFramebuffer, MPE7CorePalette, MPE7CoreStop, MPE7CoreError};
   if (!MPE7Session->start(callbacks, MPE7VideoWorkspace,
                           mpe_doom::Video::WorkspaceBytes, millis()))
   { MPE7Error = MPE3TitleErrorRead; return false; }
   if (!MPE7TargetHealthy())
   { MPE7Error = MPE3TitleErrorMemory; return false; }

   MPE3Title.Loaded = true;
   MPE3Title.Phase = MPE3TitleFinished;
   MPE3TitleMailbox[0xfcu] = 0u;
   MPE3TitleMailbox[0xfdu] = 0u;
   MPE3TitleMailbox[0xfeu] = 0u;
   MPE3TitleMailbox[0xffu] = 0u;
   MPE7FirstFrame = MPE7FirstCellPacket = true;
   MPE7FrameEndSidPending = false;
   MPE7FaultPending = MPE7FaultPublished = false;
   MPE7PreviousScan = MPE7PreviousModifiers = 0u;
   MPE3TitleMemoryBarrier();
   MPE7Active = true;
   return true;
}

// ISR only.  F8..FF retain DOSVM's held-scan-v2 envelope.  Unlike the older
// NES producer, acceptance is acknowledged only after foreground has applied
// the complete held-state transition to Session.
static inline void MPE7LatchInput()
{
   if (!MPE7Active || MPE7InputPending) return;
   const uint8_t sequence = MPE3TitleMailbox[0xfeu];
   const uint8_t flags = MPE3TitleMailbox[0xfdu];
   if (!sequence || sequence == MPE3TitleMailbox[0xfcu] ||
       !(flags & 0x80u) || (flags & (uint8_t)~0x8fu))
      return;
   const uint8_t key = MPE3TitleMailbox[0xf8u];
   const uint8_t scan = MPE3TitleMailbox[0xf9u];
   const uint8_t joy = MPE3TitleMailbox[0xfau];
   const uint8_t checksum = MPE3TitleMailbox[0xffu];
   if ((joy & (uint8_t)~mpe_doom::kJoystickAll) ||
       (uint8_t)(0xa5u ^ key ^ scan ^ joy ^ flags ^ sequence) != checksum)
      return;

   MPE7Input.key = key;
   MPE7Input.scan = scan;
   MPE7Input.joy = joy;
   MPE7Input.flags = flags;
   MPE7Input.sequence = sequence;
   MPE3TitleMemoryBarrier();
   MPE7InputPending = true;
}

static FLASHMEM void MPE7AppendInputEdge(MPE7InputEdge *edges,
                                         uint8_t *count, uint8_t scan,
                                         bool pressed)
{
   if (!scan || *count >= 5u) return;
   edges[*count].scan = scan;
   edges[*count].pressed = pressed;
   ++*count;
}

static FLASHMEM bool MPE7ApplyInput()
{
   if (!MPE7InputPending) return true;
   MPE3TitleMemoryBarrier();
   const MPE7LatchedInput input = {
      MPE7Input.key, MPE7Input.scan, MPE7Input.joy,
      MPE7Input.flags, MPE7Input.sequence};
   (void)input.key; // Doom consumes the normalized PC scan, not PETSCII.

   const uint8_t modifiers = input.flags & 7u;
   MPE7InputEdge edges[5];
   uint8_t count = 0u;
   if (MPE7PreviousScan && MPE7PreviousScan != input.scan)
      MPE7AppendInputEdge(edges, &count, MPE7PreviousScan, false);
   static const uint8_t modifierScans[3] = {0x2au, 0x1du, 0x38u};
   for (uint8_t bit = 0u; bit < 3u; ++bit)
      if ((MPE7PreviousModifiers & (1u << bit)) &&
          !(modifiers & (1u << bit)))
         MPE7AppendInputEdge(edges, &count, modifierScans[bit], false);
   for (uint8_t bit = 0u; bit < 3u; ++bit)
      if (!(MPE7PreviousModifiers & (1u << bit)) &&
          (modifiers & (1u << bit)))
         MPE7AppendInputEdge(edges, &count, modifierScans[bit], true);
   if (input.scan && input.scan != MPE7PreviousScan)
      MPE7AppendInputEdge(edges, &count, input.scan, true);

   if (!MPE7Session ||
       MPE7Session->pendingScanEvents() + count >
          mpe_doom::Controls::kScanEventCapacity)
      return false; // Retain the packet; the C64 will retry until it is ACKed.

   if (!count)
   {
      const mpe_doom::InputUpdate update = {false, 0u, false, input.joy};
      if (!MPE7Session->updateInput(update))
      { MPE7RecordFault(MPE3TitleErrorMemory); return false; }
   }
   else
   {
      for (uint8_t index = 0u; index < count; ++index)
      {
         const mpe_doom::InputUpdate update = {
            true, edges[index].scan, edges[index].pressed, input.joy};
         if (!MPE7Session->updateInput(update))
         { MPE7RecordFault(MPE3TitleErrorMemory); return false; }
      }
   }

   MPE7PreviousScan = input.scan;
   MPE7PreviousModifiers = modifiers;
   MPE3TitleMemoryBarrier();
   MPE3TitleMailbox[0xfcu] = input.sequence;
   MPE7InputPending = false;
   return true;
}

static FLASHMEM void MPE7Pump()
{
   if (!MPE7Active || MPE7FaultPending) return;
   (void)MPE7ApplyInput();
   if (MPE7FaultPending) return;
   const mpe_doom::AdvanceResult result = MPE7Session->advance(millis());
   if (result.core_failed || !MPE7TargetHealthy())
      MPE7RecordFault(MPE3TitleErrorMemory);
}

static FLASHMEM void MPE7PublishSid()
{
   memset(MPE3TitlePacket + MPE3TitlePacketHeaderBytes, 0, 26u);
   // Bit 0 is the terminal's visible-frame boundary; bit 5 enables input.
   // Doom is always the 160x200 multicolor mode, so hires bit 2 stays clear.
   MPE3TitlePublish(MPE3TitleSID, 0x21u, 26u);
}

static FLASHMEM void MPE7NextPacket()
{
   MPE7Pump();
   if (!MPE7Active) return;
   if (MPE7FaultPending)
   {
      if (!MPE7FaultPublished)
      {
         MPE7FaultPublished = true;
         MPE3TitleFail(MPE7Error ? MPE7Error :
                       (uint8_t)MPE3TitleErrorMemory);
      }
      return;
   }
   if (MPE7FrameEndSidPending)
   { MPE7PublishSid(); return; }

   const uint16_t count = MPE7Session->changes(
      MPE3TitlePacket + MPE3TitlePacketHeaderBytes,
      MPE3TitleCellsPerPacket);
   if (count)
   {
      uint8_t flags = MPE3TitleCellModeValid;
      if (MPE7FirstFrame)
      {
         flags |= 1u;
         if (MPE7FirstCellPacket) flags |= MPE3TitleCellReplace;
         if (MPE7Session->frameEndAwaitingAck()) flags |= 2u;
      }
      MPE7FirstCellPacket = false;
      MPE3TitlePublish(MPE3TitleCELL, flags,
                       (uint8_t)(count * MPE3TitleCellBytes));
      return;
   }

   // A silent immutable heartbeat bounds terminal input latency, including a
   // valid newly staged frame which happened to contain no changed cells.
   MPE7PublishSid();
}

static FLASHMEM void MPE7ResumeAfterACK()
{
   if (!MPE7Session) return;
   if (MPE3Title.PendingType == MPE3TitleCELL)
   {
      if (MPE7Session->frameEndAwaitingAck())
         MPE7FrameEndSidPending = true;
   }
   else if (MPE3Title.PendingType == MPE3TitleSID &&
            MPE7FrameEndSidPending)
   {
      if (!MPE7Session->acknowledgeFrameEnd())
      { MPE7RecordFault(MPE3TitleErrorMemory); return; }
      MPE7FrameEndSidPending = false;
      MPE7FirstFrame = false;
   }
   else if (MPE3Title.PendingType == MPE3TitleERROR)
   {
      MPE7Active = false;
   }
}

static FLASHMEM void MPE7PumpPending()
{
   // Simulation may advance while the C64 owns an immutable packet.  Session
   // prevents a new video target from crossing an unacknowledged frame end.
   MPE7Pump();
}

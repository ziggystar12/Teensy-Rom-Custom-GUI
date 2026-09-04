#ifndef MHS_NATIVE_ARENA_H
#define MHS_NATIVE_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifndef DMAMEM
#error "MHS native arena requires the Teensy DMAMEM placement macro"
#endif

// One fixed native-session arena. This is an ownership gate, not an allocator:
// clients always receive the same base address and must initialize every byte
// they use. Ownership transitions are foreground-only and never clear storage.
static constexpr uint32_t MHSNativeArenaCapacity = 65536u;
static constexpr uint32_t MHSNativeArenaAlignment = 32u;

enum class MHSNativeArenaOwner : uint8_t
{
   None = 0,
   MPE2,
   Title,
   PowerEngine,
   DOS,
   Doom
};

enum class MHSNativeArenaPhase : uint8_t
{
   Free = 0,
   Reusable,
   ResetOnly
};

enum class MHSNativeArenaStatus : uint8_t
{
   Okay = 0,
   InvalidArgument,
   InvalidOwner,
   InvalidSize,
   InvalidAlignment,
   Busy,
   WrongOwner,
   StaleLease,
   ResetOnly,
   InvalidState,
   InvalidTransition
};

struct MHSNativeArenaView
{
   uint8_t *data;
   uint32_t bytes;
   uint32_t alignment;
   uint32_t generation;
   MHSNativeArenaOwner owner;
   MHSNativeArenaPhase phase;
};

struct MHSNativeArenaControl
{
   MHSNativeArenaOwner owner;
   MHSNativeArenaPhase phase;
   uint16_t reserved;
   uint32_t generation;
   uint32_t bytes;
   uint32_t alignment;
};

// The arena itself is in RAM2. Its control record deliberately has no DMAMEM
// qualifier, so it remains in ordinary DTCM/BSS even when a reset-only engine
// takes over RAM2.
alignas(32) static DMAMEM uint8_t MHSNativeArenaStorage[MHSNativeArenaCapacity];
static MHSNativeArenaControl MHSNativeArenaControlState;

static_assert(sizeof(MHSNativeArenaStorage) == MHSNativeArenaCapacity,
              "MHS native arena capacity changed");

static inline bool MHSNativeArenaRequestValid(uint32_t Bytes,
                                              uint32_t Alignment)
{
   return Bytes && Bytes <= MHSNativeArenaCapacity && Alignment &&
      Alignment <= MHSNativeArenaAlignment &&
      !(Alignment & (Alignment - 1u)) &&
      !((uintptr_t)MHSNativeArenaStorage & (Alignment - 1u));
}

static inline bool MHSNativeArenaControlValid()
{
   const bool Free = MHSNativeArenaControlState.phase == MHSNativeArenaPhase::Free;
   if (Free)
      return MHSNativeArenaControlState.owner == MHSNativeArenaOwner::None &&
         !MHSNativeArenaControlState.bytes &&
         !MHSNativeArenaControlState.alignment;
   return MHSNativeArenaControlState.owner != MHSNativeArenaOwner::None &&
      MHSNativeArenaControlState.generation &&
      MHSNativeArenaRequestValid(MHSNativeArenaControlState.bytes,
                                 MHSNativeArenaControlState.alignment);
}

static inline uint32_t MHSNativeArenaNextGeneration()
{
   uint32_t Generation = MHSNativeArenaControlState.generation + 1u;
   if (!Generation) Generation = 1u;
   return Generation;
}

static inline void MHSNativeArenaWriteView(MHSNativeArenaView *View)
{
   View->data = MHSNativeArenaStorage;
   View->bytes = MHSNativeArenaControlState.bytes;
   View->alignment = MHSNativeArenaControlState.alignment;
   View->generation = MHSNativeArenaControlState.generation;
   View->owner = MHSNativeArenaControlState.owner;
   View->phase = MHSNativeArenaControlState.phase;
}

static inline bool MHSNativeArenaLeaseValid(const MHSNativeArenaView *View)
{
   return View && MHSNativeArenaControlValid() &&
      MHSNativeArenaControlState.phase != MHSNativeArenaPhase::Free &&
      View->data == MHSNativeArenaStorage &&
      View->bytes == MHSNativeArenaControlState.bytes &&
      View->alignment == MHSNativeArenaControlState.alignment &&
      View->generation == MHSNativeArenaControlState.generation &&
      View->owner == MHSNativeArenaControlState.owner &&
      View->phase == MHSNativeArenaControlState.phase;
}

static inline bool MHSNativeArenaOwns(MHSNativeArenaOwner Owner)
{
   return Owner != MHSNativeArenaOwner::None && MHSNativeArenaControlValid() &&
      MHSNativeArenaControlState.phase != MHSNativeArenaPhase::Free &&
      MHSNativeArenaControlState.owner == Owner;
}

static inline bool MHSNativeArenaRequiresReset()
{
   return MHSNativeArenaControlValid() &&
      MHSNativeArenaControlState.phase == MHSNativeArenaPhase::ResetOnly;
}

static inline MHSNativeArenaStatus MHSNativeArenaClaim(
   MHSNativeArenaOwner Owner, uint32_t Bytes, uint32_t Alignment,
   MHSNativeArenaView *View)
{
   if (!View) return MHSNativeArenaStatus::InvalidArgument;
   if (Owner == MHSNativeArenaOwner::None)
      return MHSNativeArenaStatus::InvalidOwner;
   if (!Bytes || Bytes > MHSNativeArenaCapacity)
      return MHSNativeArenaStatus::InvalidSize;
   if (!Alignment || Alignment > MHSNativeArenaAlignment ||
       (Alignment & (Alignment - 1u)) ||
       ((uintptr_t)MHSNativeArenaStorage & (Alignment - 1u)))
      return MHSNativeArenaStatus::InvalidAlignment;
   if (!MHSNativeArenaControlValid())
      return MHSNativeArenaStatus::InvalidState;
   if (MHSNativeArenaControlState.phase == MHSNativeArenaPhase::ResetOnly)
      return MHSNativeArenaStatus::ResetOnly;
   if (MHSNativeArenaControlState.phase != MHSNativeArenaPhase::Free)
      return MHSNativeArenaStatus::Busy;

   MHSNativeArenaControlState.owner = Owner;
   MHSNativeArenaControlState.phase = MHSNativeArenaPhase::Reusable;
   MHSNativeArenaControlState.generation = MHSNativeArenaNextGeneration();
   MHSNativeArenaControlState.bytes = Bytes;
   MHSNativeArenaControlState.alignment = Alignment;
   MHSNativeArenaWriteView(View);
   return MHSNativeArenaStatus::Okay;
}

static inline MHSNativeArenaStatus MHSNativeArenaHandoff(
   MHSNativeArenaOwner Current, MHSNativeArenaOwner Next,
   uint32_t Bytes, uint32_t Alignment, MHSNativeArenaView *View)
{
   if (!View) return MHSNativeArenaStatus::InvalidArgument;
   if (Current == MHSNativeArenaOwner::None)
      return MHSNativeArenaStatus::InvalidOwner;
   if (Next == MHSNativeArenaOwner::None)
      return MHSNativeArenaStatus::InvalidOwner;
   if (!Bytes || Bytes > MHSNativeArenaCapacity)
      return MHSNativeArenaStatus::InvalidSize;
   if (!Alignment || Alignment > MHSNativeArenaAlignment ||
       (Alignment & (Alignment - 1u)) ||
       ((uintptr_t)MHSNativeArenaStorage & (Alignment - 1u)))
      return MHSNativeArenaStatus::InvalidAlignment;
   if (!MHSNativeArenaControlValid())
      return MHSNativeArenaStatus::InvalidState;
   if (MHSNativeArenaControlState.phase == MHSNativeArenaPhase::ResetOnly)
      return MHSNativeArenaStatus::ResetOnly;
   if (Current != MHSNativeArenaControlState.owner)
      return MHSNativeArenaStatus::WrongOwner;
   if (Next == Current)
      return MHSNativeArenaStatus::InvalidTransition;

   MHSNativeArenaControlState.owner = Next;
   MHSNativeArenaControlState.phase = MHSNativeArenaPhase::Reusable;
   MHSNativeArenaControlState.generation = MHSNativeArenaNextGeneration();
   MHSNativeArenaControlState.bytes = Bytes;
   MHSNativeArenaControlState.alignment = Alignment;
   MHSNativeArenaWriteView(View);
   return MHSNativeArenaStatus::Okay;
}

static inline MHSNativeArenaStatus MHSNativeArenaRelease(
   MHSNativeArenaOwner Current)
{
   if (Current == MHSNativeArenaOwner::None)
      return MHSNativeArenaStatus::InvalidOwner;
   if (!MHSNativeArenaControlValid())
      return MHSNativeArenaStatus::InvalidState;
   if (MHSNativeArenaControlState.phase == MHSNativeArenaPhase::ResetOnly)
      return MHSNativeArenaStatus::ResetOnly;
   if (Current != MHSNativeArenaControlState.owner)
      return MHSNativeArenaStatus::WrongOwner;

   MHSNativeArenaControlState.owner = MHSNativeArenaOwner::None;
   MHSNativeArenaControlState.phase = MHSNativeArenaPhase::Free;
   MHSNativeArenaControlState.generation = MHSNativeArenaNextGeneration();
   MHSNativeArenaControlState.bytes = 0;
   MHSNativeArenaControlState.alignment = 0;
   return MHSNativeArenaStatus::Okay;
}

static inline MHSNativeArenaStatus MHSNativeArenaSealResetOnly(
   MHSNativeArenaOwner Current)
{
   if (Current == MHSNativeArenaOwner::None)
      return MHSNativeArenaStatus::InvalidOwner;
   if (!MHSNativeArenaControlValid())
      return MHSNativeArenaStatus::InvalidState;
   if (MHSNativeArenaControlState.phase == MHSNativeArenaPhase::ResetOnly)
      return MHSNativeArenaStatus::ResetOnly;
   if (Current != MHSNativeArenaControlState.owner)
      return MHSNativeArenaStatus::WrongOwner;

   MHSNativeArenaControlState.phase = MHSNativeArenaPhase::ResetOnly;
   MHSNativeArenaControlState.generation = MHSNativeArenaNextGeneration();
   return MHSNativeArenaStatus::Okay;
}

#ifdef MHS_NATIVE_ARENA_TEST
// Models C startup for the host harness. Production has no force-release path;
// the NOLOAD arena bytes intentionally remain untouched here as well.
static inline void MHSNativeArenaTestReset()
{
   MHSNativeArenaControlState.owner = MHSNativeArenaOwner::None;
   MHSNativeArenaControlState.phase = MHSNativeArenaPhase::Free;
   MHSNativeArenaControlState.reserved = 0;
   MHSNativeArenaControlState.generation = 0;
   MHSNativeArenaControlState.bytes = 0;
   MHSNativeArenaControlState.alignment = 0;
}
#endif

#endif

#include <stdint.h>

#include <iostream>
#include <stdexcept>

#define DMAMEM
#define MHS_NATIVE_ARENA_TEST 1
#include "../engine/native-runtime/mhs_native_arena.h"

static unsigned Checks;

static void require(bool Condition, const char *Message)
{
   ++Checks;
   if (!Condition) throw std::runtime_error(Message);
}

static void status(MHSNativeArenaStatus Actual, MHSNativeArenaStatus Expected,
                   const char *Message)
{
   require(Actual == Expected, Message);
}

int main()
{
   try
   {
      MHSNativeArenaStorage[0] = 0x31;
      MHSNativeArenaStorage[32768] = 0x72;
      MHSNativeArenaStorage[MHSNativeArenaCapacity - 1u] = 0xA5;
      MHSNativeArenaTestReset();

      require(sizeof(MHSNativeArenaStorage) == 65536u, "capacity");
      require(((uintptr_t)MHSNativeArenaStorage & 31u) == 0, "alignment");
      require(MHSNativeArenaControlValid(), "initial control");
      require(!MHSNativeArenaRequiresReset(), "initial reset state");
      require(!MHSNativeArenaOwns(MHSNativeArenaOwner::None), "none is not an owner");
      require(MHSNativeArenaStorage[0] == 0x31 &&
              MHSNativeArenaStorage[32768] == 0x72 &&
              MHSNativeArenaStorage[MHSNativeArenaCapacity - 1u] == 0xA5,
              "test reset cleared storage");

      MHSNativeArenaView Lease{};
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::None, 1, 1, &Lease),
             MHSNativeArenaStatus::InvalidOwner, "none claim");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 1, 1, nullptr),
             MHSNativeArenaStatus::InvalidArgument, "null claim lease");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 0, 1, &Lease),
             MHSNativeArenaStatus::InvalidSize, "zero size");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title,
             MHSNativeArenaCapacity + 1u, 1, &Lease),
             MHSNativeArenaStatus::InvalidSize, "oversize");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 1, 0, &Lease),
             MHSNativeArenaStatus::InvalidAlignment, "zero alignment");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 1, 3, &Lease),
             MHSNativeArenaStatus::InvalidAlignment, "non-power alignment");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 1, 64, &Lease),
             MHSNativeArenaStatus::InvalidAlignment, "excess alignment");

      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 4096, 8, &Lease),
             MHSNativeArenaStatus::Okay, "title claim");
      require(MHSNativeArenaLeaseValid(&Lease), "title lease");
      require(Lease.data == MHSNativeArenaStorage && Lease.bytes == 4096 &&
              Lease.alignment == 8 &&
              Lease.phase == MHSNativeArenaPhase::Reusable,
              "title lease bounds");
      require(MHSNativeArenaOwns(MHSNativeArenaOwner::Title), "title ownership");
      require(!MHSNativeArenaOwns(MHSNativeArenaOwner::PowerEngine),
              "unexpected power-engine ownership");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::MPE2, 1, 1, &Lease),
             MHSNativeArenaStatus::Busy, "double claim");

      MHSNativeArenaView Stale = Lease;
      ++Stale.generation;
      require(!MHSNativeArenaLeaseValid(&Stale), "stale generation accepted");
      require(MHSNativeArenaLeaseValid(&Lease), "live generation rejected");
      status(MHSNativeArenaRelease(MHSNativeArenaOwner::MPE2),
             MHSNativeArenaStatus::WrongOwner,
             "wrong-owner release");

      MHSNativeArenaView Power{};
      status(MHSNativeArenaHandoff(MHSNativeArenaOwner::Title, MHSNativeArenaOwner::None,
             MHSNativeArenaCapacity, 32, &Power),
             MHSNativeArenaStatus::InvalidOwner, "handoff to none");
      status(MHSNativeArenaHandoff(MHSNativeArenaOwner::Title, MHSNativeArenaOwner::Title,
             MHSNativeArenaCapacity, 32, &Power),
             MHSNativeArenaStatus::InvalidTransition, "handoff to self");
      status(MHSNativeArenaHandoff(MHSNativeArenaOwner::Title, MHSNativeArenaOwner::PowerEngine,
             MHSNativeArenaCapacity, 32, &Power),
             MHSNativeArenaStatus::Okay, "title to power engine");
      require(!MHSNativeArenaLeaseValid(&Lease) &&
              MHSNativeArenaLeaseValid(&Power), "handoff generations");
      require(Power.data == MHSNativeArenaStorage &&
              Power.bytes == MHSNativeArenaCapacity && Power.alignment == 32,
              "handoff bounds");
      require(MHSNativeArenaStorage[0] == 0x31 &&
              MHSNativeArenaStorage[32768] == 0x72 &&
              MHSNativeArenaStorage[MHSNativeArenaCapacity - 1u] == 0xA5,
              "handoff cleared storage");
      status(MHSNativeArenaRelease(MHSNativeArenaOwner::Title), MHSNativeArenaStatus::WrongOwner,
             "old-owner release");
      Stale = Power;
      --Stale.generation;
      require(!MHSNativeArenaLeaseValid(&Stale), "old generation survived");
      status(MHSNativeArenaRelease(MHSNativeArenaOwner::PowerEngine), MHSNativeArenaStatus::Okay,
             "power release");
      require(!MHSNativeArenaLeaseValid(&Power) &&
              MHSNativeArenaControlValid(), "released lease");

      MHSNativeArenaView MPE2{};
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::MPE2, 1, 1, &MPE2),
             MHSNativeArenaStatus::Okay, "MPE2 claim");
      status(MHSNativeArenaRelease(MHSNativeArenaOwner::MPE2), MHSNativeArenaStatus::Okay,
             "MPE2 release");

      MHSNativeArenaView DOS{};
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::DOS,
             MHSNativeArenaCapacity, 32, &DOS),
             MHSNativeArenaStatus::Okay, "DOS claim");
      status(MHSNativeArenaSealResetOnly(MHSNativeArenaOwner::DOS),
             MHSNativeArenaStatus::Okay, "DOS seal");
      require(!MHSNativeArenaLeaseValid(&DOS), "sealed lease remained valid");
      require(MHSNativeArenaRequiresReset() &&
              MHSNativeArenaOwns(MHSNativeArenaOwner::DOS), "reset-only state");
      status(MHSNativeArenaRelease(MHSNativeArenaOwner::DOS), MHSNativeArenaStatus::ResetOnly,
             "reset-only release");
      status(MHSNativeArenaHandoff(MHSNativeArenaOwner::DOS, MHSNativeArenaOwner::PowerEngine,
             1, 1, &Lease), MHSNativeArenaStatus::ResetOnly,
             "reset-only handoff");
      status(MHSNativeArenaClaim(MHSNativeArenaOwner::Title, 1, 1, &Lease),
             MHSNativeArenaStatus::ResetOnly, "reset-only claim");
      status(MHSNativeArenaSealResetOnly(MHSNativeArenaOwner::DOS),
             MHSNativeArenaStatus::ResetOnly, "second seal");

      MHSNativeArenaTestReset();
      require(MHSNativeArenaControlValid() && !MHSNativeArenaRequiresReset() &&
              !MHSNativeArenaLeaseValid(&DOS), "simulated MCU reset");
      require(MHSNativeArenaStorage[0] == 0x31 &&
              MHSNativeArenaStorage[32768] == 0x72 &&
              MHSNativeArenaStorage[MHSNativeArenaCapacity - 1u] == 0xA5,
              "simulated MCU reset cleared storage");

      std::cout << "{\"passed\":true,\"checks\":" << Checks
                << ",\"capacity\":" << MHSNativeArenaCapacity
                << ",\"alignment\":" << MHSNativeArenaAlignment
                << ",\"resetOnlyAbsorbing\":true,\"storageClears\":0}"
                << std::endl;
      return 0;
   }
   catch (const std::exception &Error)
   {
      std::cerr << "MHS native arena test failed after " << Checks
                << " checks: " << Error.what() << std::endl;
      return 1;
   }
}

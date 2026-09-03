#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#define FLASHMEM
#define DMAMEM

// Model MinimalBoot's resident CRT storage independently from RawROM. The
// latter remains the existing Sierra cartridge-reader fixture below.
static constexpr uint32_t RAM_ImageSize = 240u * 1024u;
static constexpr uint16_t MAX_CRT_CHIPS = 128;
struct StructCrtChip {
   uint8_t *ChipROM;
   uint16_t LoadAddress, ROMSize, BankNum;
};
uint8_t RAM_Image[RAM_ImageSize];
uint8_t NumCrtChips;
StructCrtChip CrtChips[MAX_CRT_CHIPS];
static uint8_t *BankDecode[64][2];
static constexpr uint8_t NumDecodeBanks = 64;
static constexpr uint8_t Num8kSwapBuffers = 16;
static struct { uint8_t Image[8192]; uint32_t Offset; }
   SwapBuffers[Num8kSwapBuffers];
static struct { uint32_t pages[512]; bool native; } MPE4CrtDirectory;
static uint32_t LoadedCartridgeBytes;

// The target gives this physical range exclusively to DOS until an MCU
// reboot. Host tests use an equally sized guarded array and record reboot
// requests rather than touching the desktop process address 0x20200000.
alignas(32) static uint8_t MPE5HostRam2[512u * 1024u];
#define MPE5_RAM2_BASE MPE5HostRam2
static bool HostRebooted;
#define REBOOT do { HostRebooted = true; } while (0)

// Exercise the production handoff's success/failure ownership contract on the
// host.  Target register ordering is checked separately against the expanded
// firmware; this seam lets the integrated boot test inject a wedged USB block.
static bool MPE5HostUsbQuiesceResult = true;
static unsigned MPE5HostUsbQuiesceCalls;
static bool MPE5TestUsb1Quiesce()
{
   MPE5HostUsbQuiesceCalls++;
   return MPE5HostUsbQuiesceResult;
}
#define MPE5_USB_QUIESCE_TEST

static uint8_t EZFlashRAM[256], CurrentEasyFlashBank = 58;
static uint32_t millis();
static constexpr uint16_t AGIPicBitmapLength = 8000;
static constexpr uint16_t AGIPicScreenLength = 1000;
static constexpr uint16_t AGIPicColourLength = 1000;
static constexpr uint32_t AGIPicGBC1ViewCacheCapacity = 20u * 65535u;
static uint8_t AGIPicRoomBitmap[8000], AGIPicRoomScreen[1000], AGIPicRoomColour[1000];
static uint8_t AGIPicGBC1ViewCacheMemory[AGIPicGBC1ViewCacheCapacity];
static constexpr uint8_t AGIPicLayout_EasyFlash = 0, DMA_S_DisableReady = 0;
static uint8_t AGIPicLayout, AGIPicPendingCommand, DMA_State;
static bool AGIPicActive, AGIPicResetPending, AGIPicAbortRequested, MPEThinUpgradePending;
static bool PSRAMAvailable = false;
static uint8_t WriteByte, ReadByte;
static uint32_t ReadCalls = 0, ReleaseCalls = 0, MaxReadLength = 0;
static bool FailNextRead = false;
static constexpr uint32_t Root = 0x4000;
static std::vector<uint8_t> RawROM(1024u * 1024u, 0xff);

static void DataPortWriteWaitLog(uint8_t Data) { ReadByte = Data; }
static uint8_t DataPortWaitRead() { return WriteByte; }
static void TraceLogAddValidData(uint8_t) {}
static void AGIPictureReleaseSource() { ReleaseCalls++; }
static void AGIPictureReleasePicture() {}
static void AGIPictureReleaseScene() {}
static void AGIPictureInvalidateLivePicture() {}
static bool AGIPictureGBC1CacheAvailable() { return PSRAMAvailable; }
uint8_t *AGIPictureNativeSharedArena() { return AGIPicGBC1ViewCacheMemory; }
static bool AGIPictureRawSpanValid(uint32_t Raw, uint32_t Length)
{
   return Length && Raw < RawROM.size() && Length <= RawROM.size() - Raw;
}
static bool AGIPictureReadRawBytes(uint32_t Raw, uint8_t *Data, uint16_t Length, uint8_t *Error)
{
   ReadCalls++;
   MaxReadLength = std::max(MaxReadLength, uint32_t(Length));
   if (FailNextRead) { FailNextRead = false; *Error = 1; return false; }
   if (!AGIPictureRawSpanValid(Raw, Length)) { *Error = 1; return false; }
   std::memcpy(Data, RawROM.data() + Raw, Length);
   return true;
}
static uint16_t MHSNativeRead16(const uint8_t *Data)
{
   return uint16_t(Data[0]) | (uint16_t(Data[1]) << 8);
}
static uint32_t MHSNativeRead32(const uint8_t *Data)
{
   return uint32_t(Data[0]) | (uint32_t(Data[1]) << 8) |
      (uint32_t(Data[2]) << 16) | (uint32_t(Data[3]) << 24);
}
static uint32_t MHSNativeCRC32Byte(uint32_t CRC, uint8_t Byte)
{
   CRC ^= Byte;
   for (uint8_t Bit = 0; Bit < 8; Bit++) CRC = (CRC >> 1) ^ ((CRC & 1) ? 0xedb88320u : 0);
   return CRC;
}
static uint32_t MHSNativeCRC32(const uint8_t *Data, uint32_t Length)
{
   uint32_t CRC = 0xffffffffu;
   for (uint32_t Index = 0; Index < Length; Index++) CRC = MHSNativeCRC32Byte(CRC, Data[Index]);
   return ~CRC;
}

// Compile and execute the actual firmware sequencer, not a host reimplementation.
#include "IOH_MPE3TitlePull.c"
static uint32_t millis() {
#ifdef MPE5_NATIVE
  return uint32_t(inst_counter) / 1000u;
#else
  return 0;
#endif
}

static void writeControl(uint8_t Address, uint8_t Data)
{
   WriteByte = Data;
   MPE3TitleIO2Hndlr(Address, false);
}
static uint8_t readControl(uint8_t Address)
{
   MPE3TitleIO2Hndlr(Address, true);
   return ReadByte;
}
static void put16(std::vector<uint8_t> &Data, size_t At, uint16_t Value)
{
   Data[At] = uint8_t(Value); Data[At + 1] = uint8_t(Value >> 8);
}
static void put32(std::vector<uint8_t> &Data, size_t At, uint32_t Value)
{
   for (unsigned Byte = 0; Byte < 4; Byte++) Data[At + Byte] = uint8_t(Value >> (Byte * 8));
}
static void repairCRC(std::vector<uint8_t> &Data)
{
   put32(Data, 56, MHSNativeCRC32(Data.data() + 64, uint32_t(Data.size() - 64)));
   put32(Data, 60, MHSNativeCRC32(Data.data(), 60));
}
static void start(const std::vector<uint8_t> &Asset, uint32_t AssetRoot = Root, bool Poll = true)
{
   MPE3TitleInit();
   std::fill(RawROM.begin(), RawROM.end(), 0xff);
   if (Root + Asset.size() <= RawROM.size()) std::copy(Asset.begin(), Asset.end(), RawROM.begin() + Root);
   CurrentEasyFlashBank = 58;
   std::memset(EZFlashRAM, 0, sizeof(EZFlashRAM));
   writeControl(0xf8, uint8_t(AssetRoot));
   writeControl(0xf9, uint8_t(AssetRoot >> 8));
   writeControl(0xfa, uint8_t(AssetRoot >> 16));
   for (unsigned Index = 0; Index < 4; Index++) writeControl(uint8_t(0xf0 + Index), "M3TP"[Index]);
   writeControl(0xf4, 1);
   assert(MPE3TitleOwned && MPE3TitleStartPending);
   if (Poll) MPE3TitlePollingHndlr();
}

// Call after start(..., false), before the first DOS poll. Parse the complete
// CRT to reproduce the loader's real resident prefix, including launcher ROM.
// Deliberately leave RawROM and AGIPictureReadRawBytes unchanged for Sierra.
static void prepareDosCartridgeMemory(const std::vector<uint8_t> &Cartridge)
{
   assert(MPE3TitleOwned && MPE3TitleStartPending);
   assert(Cartridge.size() >= 64 &&
          !std::memcmp(Cartridge.data(), "C64 CARTRIDGE   ", 16));
   std::memset(RAM_Image, 0xa5, sizeof(RAM_Image));
   std::memset(CrtChips, 0, sizeof(CrtChips));
   std::memset(BankDecode, 0, sizeof(BankDecode));
   std::memset(&MPE4CrtDirectory, 0, sizeof(MPE4CrtDirectory));
   NumCrtChips = 0;
   LoadedCartridgeBytes = 0;
   MPE4CrtDirectory.native = true;
   const auto BE16 = [&](size_t At) {
      return uint16_t(uint16_t(Cartridge.at(At)) << 8 | Cartridge.at(At + 1));
   };
   size_t At = 64;
   while (At < Cartridge.size())
   {
      assert(Cartridge.size() - At >= 16 &&
             !std::memcmp(Cartridge.data() + At, "CHIP", 4));
      const uint32_t PacketBytes = uint32_t(BE16(At + 4)) << 16 | BE16(At + 6);
      const uint16_t Type = BE16(At + 8), Bank = BE16(At + 10);
      const uint16_t Address = BE16(At + 12), Length = BE16(At + 14);
      assert(PacketBytes == 0x2010 && Length == 0x2000 &&
             PacketBytes <= Cartridge.size() - At && (Type == 0 || Type == 2));
      assert(Bank < 256 && Bank != 58 && (Address == 0x8000 || Address == 0xa000));
      const unsigned Half = Address == 0xa000, Page = Bank * 2u + Half;
      assert(!MPE4CrtDirectory.pages[Page]);
      if (!MPE4CrtDirectory.pages[0]) assert(Page == 0);
      else if (!MPE4CrtDirectory.pages[1]) assert(Page == 1);
      MPE4CrtDirectory.pages[Page] = uint32_t(At + 16);
      if (Bank < 64)
      {
         assert(NumCrtChips < MAX_CRT_CHIPS &&
                Length <= RAM_ImageSize - LoadedCartridgeBytes);
         uint8_t *Destination = RAM_Image + LoadedCartridgeBytes;
         std::memcpy(Destination, Cartridge.data() + At + 16, Length);
         CrtChips[NumCrtChips++] = {Destination, Address, Length, Bank};
         BankDecode[Bank][Half] = Destination;
         LoadedCartridgeBytes += Length;
      }
      At += PacketBytes;
   }
   assert(At == Cartridge.size() && NumCrtChips >= 2 &&
          MPE4CrtDirectory.pages[0] && MPE4CrtDirectory.pages[1]);
}

struct Visit { uint8_t Hold; std::vector<uint8_t> Frame; };
struct Event { uint32_t Tick; uint16_t Frequency; uint8_t Amplitude; bool End; };

static void tracePacket(std::ofstream *Trace)
{
   if (!Trace) return;
   const uint16_t Length = uint16_t(EZFlashRAM[6]) + 10u;
   Trace->put(char(Length)); Trace->put(char(Length >> 8));
   Trace->write(reinterpret_cast<const char *>(EZFlashRAM), Length);
}

int main(int argc, char **argv)
{
   assert(argc == 2 || argc == 3);
   std::ifstream File(argv[1], std::ios::binary);
   assert(File.good());
   std::vector<uint8_t> Asset((std::istreambuf_iterator<char>(File)), {});
   assert(Asset.size() >= 64 && std::memcmp(Asset.data(), "M3T1", 4) == 0);
   assert(MHSNativeRead32(Asset.data() + 8) == Asset.size());
   assert(MHSNativeRead32(Asset.data() + 56) == MHSNativeCRC32(Asset.data() + 64, uint32_t(Asset.size() - 64)));
   assert(MHSNativeRead32(Asset.data() + 60) == MHSNativeCRC32(Asset.data(), 60));
   const unsigned VisitCount = MHSNativeRead16(Asset.data() + 12);
   const uint32_t MusicOffset = MHSNativeRead32(Asset.data() + 40);
   const uint32_t MusicLength = MHSNativeRead32(Asset.data() + 44);
   const uint32_t DeltaOffset = MHSNativeRead32(Asset.data() + 48);
   const uint32_t DeltaLength = MHSNativeRead32(Asset.data() + 52);
   const bool Intro = (Asset[15] & 8) != 0;
   assert(VisitCount > 0 && MusicLength > 0 && DeltaLength > 0);

   std::memset(EZFlashRAM, 0xa5, sizeof(EZFlashRAM));
   MPE3TitleInit();
   assert(std::all_of(std::begin(EZFlashRAM), std::end(EZFlashRAM), [](uint8_t Byte) { return Byte == 0xa5; }));
   for (unsigned Address = 0xf0; Address <= 0xff; Address++)
   {
      writeControl(uint8_t(Address), uint8_t(Address ^ 0x5a));
      assert(readControl(uint8_t(Address)) == uint8_t(Address ^ 0x5a));
   }
   assert(!MPE3TitleOwned);

   // Independently expand the host's absolute cell records to reference frames.
   std::vector<uint8_t> Base(Asset.begin() + 64, Asset.begin() + 10064), Reference = Base;
   std::vector<Visit> Visits;
   uint32_t Cursor = DeltaOffset, ExpectedDeltaRecords = 0, ExpectedBaseRecords = 0;
   for (unsigned Index = 0; Index < VisitCount; Index++)
   {
      const unsigned Count = MHSNativeRead16(Asset.data() + Cursor);
      const uint8_t Hold = Asset[Cursor + 2], Flags = Asset[Cursor + 3];
      if (Flags & 1) { Reference = Base; ExpectedBaseRecords += 1000; }
      Cursor += 4;
      for (unsigned Record = 0; Record < Count; Record++, Cursor += 12)
      {
         unsigned Cell = MHSNativeRead16(Asset.data() + Cursor);
         std::copy(Asset.begin() + Cursor + 2, Asset.begin() + Cursor + 10, Reference.begin() + Cell * 8);
         Reference[8000 + Cell] = Asset[Cursor + 10]; Reference[9000 + Cell] = Asset[Cursor + 11];
      }
      ExpectedDeltaRecords += Count;
      Visits.push_back({uint8_t(Intro && Index + 1 == VisitCount ? 1 : Hold), Reference});
   }
   assert(Cursor == Asset.size());

   // Independent event timelines validate every native score snapshot and
   // retrigger. A duration-D event begins at tick T and lasts through T+D-1.
   std::array<std::vector<Event>, 3> Events;
   uint32_t ScoreTicks = 0;
   for (unsigned Voice = 0; Voice < 3; Voice++)
   {
      uint32_t At = MusicOffset + MPE3TitleRead24(Asset.data() + MusicOffset + Voice * 3), Tick = 0;
      while (true)
      {
         uint16_t Duration = MHSNativeRead16(Asset.data() + At);
         Events[Voice].push_back({Tick, MHSNativeRead16(Asset.data() + At + 2), Asset[At + 4], Duration == 0xffff});
         if (Duration == 0xffff) break;
         Tick += Duration; At += 5;
      }
      ScoreTicks = std::max(ScoreTicks, Tick);
   }
   assert(ScoreTicks == 2880);
   std::array<unsigned, 3> NextEvent = {};
   std::array<uint8_t, 25> SIDReference = {}; SIDReference[24] = 15;
   std::vector<uint8_t> Display(10000, 0);
   std::vector<unsigned> VisitFrames(VisitCount, 0);
   unsigned PacketCount = 0, CellPackets = 0, SIDPackets = 0, BaseRecords = 0, DeltaRecords = 0;
   unsigned SequenceWraps = 0;
   uint8_t PreviousSequence = 0;
   std::ofstream NormalTrace;
   if (argc == 3) NormalTrace.open(std::string(argv[2]) + "-normal.bin", std::ios::binary);
   start(Asset);
   assert(MPE3Title.Loaded && MPE3TitleAssets == MPE3TitleInternalAssets && !PSRAMAvailable);
   assert(EZFlashRAM[0xf5] == 2 && EZFlashRAM[0xfc] == 1 && EZFlashRAM[0xf7] == 1);
   while (MPE3TitleOwned)
   {
      assert(++PacketCount < 20000);
      uint8_t Sequence = EZFlashRAM[0xf7];
      tracePacket(argc == 3 ? &NormalTrace : nullptr);
      assert(Sequence && Sequence != PreviousSequence && EZFlashRAM[4] == Sequence);
      if (Sequence < PreviousSequence) SequenceWraps++;
      PreviousSequence = Sequence;
      assert(EZFlashRAM[0] == 'M' && EZFlashRAM[1] == '3' && EZFlashRAM[2] == 1);
      const uint8_t Type = EZFlashRAM[3], Flags = EZFlashRAM[5], Length = EZFlashRAM[6];
      const unsigned VisitIndex = std::min(unsigned(MPE3Title.Visit), VisitCount - 1);
      assert(unsigned(Length) + 10u <= 240u && EZFlashRAM[7] == uint8_t(VisitIndex));
      assert(MPE3TitleCRC16(EZFlashRAM, uint16_t(Length + 8)) == MHSNativeRead16(EZFlashRAM + Length + 8));
      std::array<uint8_t, 240> Stable; std::copy(std::begin(EZFlashRAM), std::begin(EZFlashRAM) + 240, Stable.begin());
      MPE3TitleScore LiveScore = MPE3Title.Score;
      uint32_t LiveCursor = MPE3Title.DeltaCursor;
      uint16_t LiveBaseCell = MPE3Title.BaseCell, LiveVisit = MPE3Title.Visit;
      uint8_t CountControl = EZFlashRAM[0xfe];
      for (unsigned Wait = 0; Wait < 4; Wait++) MPE3TitlePollingHndlr();
      writeControl(0xf6, uint8_t(Sequence == 1 ? 255 : Sequence - 1));
      MPE3TitlePollingHndlr();
      writeControl(0xf4, 1); // repeated START cannot replace a pending packet
      writeControl(0xf5, 0x55); writeControl(0xf7, 0x55);
      MPE3TitlePollingHndlr();
      assert(EZFlashRAM[0xf7] == Sequence && EZFlashRAM[0xf5] == 2 && EZFlashRAM[0xfe] == CountControl);
      assert(std::equal(Stable.begin(), Stable.end(), EZFlashRAM));
      assert(std::memcmp(&LiveScore, &MPE3Title.Score, sizeof(LiveScore)) == 0);
      assert(LiveCursor == MPE3Title.DeltaCursor && LiveBaseCell == MPE3Title.BaseCell && LiveVisit == MPE3Title.Visit);

      if (Type == MPE3TitleCELL)
      {
         CellPackets++;
         assert(Length && Length % 12 == 0 && Length <= 228 && !(Flags & ~31));
         assert((Flags & 8) && bool(Flags & 4) == bool(MPE3Title.VisitFlags & 2));
         assert(bool(Flags & 16) == ((Flags & 1) ? MPE3Title.BaseCell == 0 : MPE3Title.CellsRemaining == 1000));
         for (unsigned At = 8; At < unsigned(Length) + 8u; At += 12)
         {
            unsigned Cell = MHSNativeRead16(EZFlashRAM + At);
            assert(Cell < 1000 && EZFlashRAM[At + 11] < 16);
            std::copy(EZFlashRAM + At + 2, EZFlashRAM + At + 10, Display.begin() + Cell * 8);
            Display[8000 + Cell] = EZFlashRAM[At + 10]; Display[9000 + Cell] = EZFlashRAM[At + 11];
            if (Flags & 1) BaseRecords++; else DeltaRecords++;
         }
         if ((Flags & 3) == 3) assert(Display == Base);
      }
      else if (Type == MPE3TitleSID)
      {
         assert(Length == 26 && Flags == (1 | ((MPE3Title.VisitFlags & 2) ? 4 : 0)));
         const uint32_t Tick = Intro ? SIDPackets % ScoreTicks : SIDPackets;
         uint8_t Retrigger = 0;
         if (Intro && Tick == 0 && SIDPackets)
         {
            NextEvent = {}; SIDReference = {}; SIDReference[24] = 15;
            Retrigger = 7;
         }
         if (MPE3Title.ScoreSilent) { SIDReference = {}; Retrigger = 0; }
         for (unsigned Voice = 0; Voice < 3; Voice++)
         {
            if (MPE3Title.ScoreSilent) continue;
            if (NextEvent[Voice] >= Events[Voice].size()) continue;
            const Event &E = Events[Voice][NextEvent[Voice]];
            if (E.Tick != Tick) continue;
            NextEvent[Voice]++;
            unsigned At = Voice * 7;
            if (E.End) { SIDReference[At + 4] = 0x40; continue; }
            if ((E.Amplitude & 0x10) || (SIDReference[At + 4] & 0x80)) Retrigger |= uint8_t(1u << Voice);
            SIDReference[At] = uint8_t(E.Frequency); SIDReference[At + 1] = uint8_t(E.Frequency >> 8);
            SIDReference[At + 2] = 0; SIDReference[At + 3] = 8; SIDReference[At + 5] = 0;
            SIDReference[At + 6] = uint8_t((E.Amplitude & 15) << 4);
            SIDReference[At + 4] = !(E.Amplitude & 15) ? 0 : ((E.Amplitude & 16) ? 0x81 : 0x41);
         }
         assert(EZFlashRAM[8] == Retrigger);
         assert(std::equal(SIDReference.begin(), SIDReference.end(), EZFlashRAM + 9));
         assert(Display == Visits[VisitIndex].Frame);
         if (MPE3Title.Phase == MPE3TitleHoldFrames) VisitFrames[VisitIndex]++;
         SIDPackets++;
      }
      else
      {
         assert(Type == MPE3TitleEND && Length == 0 && Flags == 0);
         uint32_t ExpectedTicks = 0;
         for (const Visit &V : Visits) ExpectedTicks += V.Hold;
         assert(SIDPackets == (Intro ? ExpectedTicks : ScoreTicks + 1u) && MPE3TitleScoreEnded(&MPE3Title.Score));
         if (Intro) assert(std::all_of(SIDReference.begin(), SIDReference.end(), [](uint8_t B) { return B == 0; }));
      }
      writeControl(0xf6, Sequence);
      MPE3TitlePollingHndlr();
   }
   assert(EZFlashRAM[0xf5] == 3 && EZFlashRAM[3] == MPE3TitleEND);
   assert(BaseRecords == ExpectedBaseRecords && DeltaRecords == ExpectedDeltaRecords);
   for (unsigned Index = 0; Index < VisitCount; Index++) assert(VisitFrames[Index] == Visits[Index].Hold);
   assert(SequenceWraps == (PacketCount - 1) / 255);
   std::array<uint8_t, 240> Finished; std::copy(EZFlashRAM, EZFlashRAM + 240, Finished.begin());
   for (unsigned Wait = 0; Wait < 10; Wait++) MPE3TitlePollingHndlr();
   assert(std::equal(Finished.begin(), Finished.end(), EZFlashRAM));
   NormalTrace.close();

   unsigned Rejections = 0;
   auto reject = [&](std::vector<uint8_t> Invalid, uint8_t ExpectedError, uint32_t At = Root) {
      start(Invalid, At);
      assert(EZFlashRAM[0xf5] == 0xe0 && EZFlashRAM[0xfb] == ExpectedError);
      assert(EZFlashRAM[3] == MPE3TitleERROR && EZFlashRAM[6] == 1 && EZFlashRAM[8] == ExpectedError);
      assert(!MPE3Title.Loaded);
      writeControl(0xf6, EZFlashRAM[0xf7]); MPE3TitlePollingHndlr();
      assert(!MPE3TitleOwned);
      Rejections++;
   };
   auto Invalid = Asset; Invalid[6] ^= 1; reject(Invalid, MPE3TitleErrorHeader);
   Invalid = Asset; Invalid.back() ^= 1; reject(Invalid, MPE3TitleErrorBodyCRC);
   Invalid = Asset; put16(Invalid, MusicOffset + 9, 0); repairCRC(Invalid); reject(Invalid, MPE3TitleErrorMusic);
   Cursor = DeltaOffset;
   while (!MHSNativeRead16(Asset.data() + Cursor)) Cursor += 4;
   Invalid = Asset; put16(Invalid, Cursor + 4, 1000); repairCRC(Invalid); reject(Invalid, MPE3TitleErrorDelta);
   reject(Asset, MPE3TitleErrorBounds, 0xfffff0);
   Invalid = Asset;
   Invalid[15] = 7; // legacy resident assets still fail closed without PSRAM
   for (unsigned Index = 0; Index < 5000; Index++) Invalid.insert(Invalid.end(), {0, 0, 3, 0});
   put16(Invalid, 12, uint16_t(VisitCount + 5000));
   put32(Invalid, 8, uint32_t(Invalid.size())); put32(Invalid, 52, DeltaLength + 20000); repairCRC(Invalid);
   reject(Invalid, MPE3TitleErrorMemory);
   AGIPicActive = true;
   unsigned PriorReleases = ReleaseCalls;
   reject(Asset, MPE3TitleErrorBusy);
   assert(ReleaseCalls == PriorReleases && AGIPicActive);
   AGIPicActive = false;
   start(Asset);
   CurrentEasyFlashBank = 12;
   std::array<uint8_t, 256> BeforeLeave; std::copy(EZFlashRAM, EZFlashRAM + 256, BeforeLeave.begin());
   assert(!MPE3TitlePollingHndlr() && !MPE3TitleOwned);
   assert(std::equal(BeforeLeave.begin(), BeforeLeave.end(), EZFlashRAM));

   // Exercise streamed intros even when this invocation uses the legacy title
   // fixture. The synthetic extension is deliberately far larger than 64 KiB.
   std::vector<uint8_t> FullIntro = Asset;
   if (!Intro)
   {
      for (unsigned Visit = 0; Visit < 21; Visit++)
      {
         FullIntro.insert(FullIntro.end(), {0xe8, 3, uint8_t(Visit == 20 ? 7 : 255), uint8_t(Visit == 20 || (Visit & 1) ? 2 : 0)});
         for (unsigned Cell = 0; Cell < 1000; Cell++)
         {
            FullIntro.push_back(uint8_t(Cell)); FullIntro.push_back(uint8_t(Cell >> 8));
            for (unsigned Byte = 0; Byte < 8; Byte++) FullIntro.push_back(uint8_t(Cell + Byte * 13 + Visit));
            FullIntro.push_back(uint8_t(Cell ^ Visit)); FullIntro.push_back(uint8_t((Cell + Visit) & 15));
         }
      }
      FullIntro[15] = 15;
      put16(FullIntro, 12, uint16_t(VisitCount + 21));
      put32(FullIntro, 8, uint32_t(FullIntro.size()));
      put32(FullIntro, 52, uint32_t(FullIntro.size()) - DeltaOffset);
      repairCRC(FullIntro);
   }
   assert(FullIntro.size() - 10064 > 65536);
   const unsigned FullVisits = MHSNativeRead16(FullIntro.data() + 12);
   uint32_t FinalAt = DeltaOffset;
   for (unsigned Visit = 1; Visit < FullVisits; Visit++) FinalAt += 4 + 12 * MHSNativeRead16(FullIntro.data() + FinalAt);
   assert(MHSNativeRead16(FullIntro.data() + FinalAt) == 1000 && FullIntro[FinalAt + 3] == 2);
   std::vector<uint8_t> Login(10000, 0);
   for (unsigned Cell = 0; Cell < 1000; Cell++)
   {
      const uint8_t *Record = FullIntro.data() + FinalAt + 4 + Cell * 12;
      assert(MHSNativeRead16(Record) == Cell);
      std::copy(Record + 2, Record + 10, Login.begin() + Cell * 8);
      Login[8000 + Cell] = Record[10]; Login[9000 + Cell] = Record[11];
   }
   unsigned SkipScenarios = 0;
   for (unsigned SkipWhen = 0; SkipWhen < 4; SkipWhen++)
   {
      std::ofstream SkipTrace;
      if (argc == 3)
      {
         static const char *Name[4] = {"skip-before-load", "skip-cell", "skip-sid", "skip-second-cell"};
         SkipTrace.open(std::string(argv[2]) + "-" + Name[SkipWhen] + ".bin", std::ios::binary);
      }
      start(FullIntro, Root, SkipWhen != 0);
      if (SkipWhen == 3)
      {
         assert(EZFlashRAM[3] == MPE3TitleCELL);
         tracePacket(argc == 3 ? &SkipTrace : nullptr);
         writeControl(0xf6, EZFlashRAM[0xf7]); MPE3TitlePollingHndlr();
         assert(EZFlashRAM[3] == MPE3TitleCELL);
      }
      if (SkipWhen == 2)
      {
         unsigned Limit = 0;
         while (EZFlashRAM[3] != MPE3TitleSID)
         {
            assert(++Limit < 1000);
            tracePacket(argc == 3 ? &SkipTrace : nullptr);
            writeControl(0xf6, EZFlashRAM[0xf7]); MPE3TitlePollingHndlr();
         }
      }
      if (SkipWhen) tracePacket(argc == 3 ? &SkipTrace : nullptr);
      const uint32_t ReadsBeforeSkip = ReadCalls;
      writeControl(0xf4, 2);
      assert(ReadCalls == ReadsBeforeSkip && MPE3TitleSkipPending);
      if (SkipWhen)
      {
         std::array<uint8_t, 240> Pending; std::copy(EZFlashRAM, EZFlashRAM + 240, Pending.begin());
         const uint32_t PendingCursor = MPE3Title.DeltaCursor;
         const unsigned PendingVisit = MPE3Title.Visit;
         const MPE3TitleScore PendingScore = MPE3Title.Score;
         for (unsigned Wait = 0; Wait < 5; Wait++) MPE3TitlePollingHndlr();
         assert(std::equal(Pending.begin(), Pending.end(), EZFlashRAM));
         assert(PendingCursor == MPE3Title.DeltaCursor && PendingVisit == MPE3Title.Visit);
         assert(std::memcmp(&PendingScore, &MPE3Title.Score, sizeof(PendingScore)) == 0);
         assert(ReadCalls == ReadsBeforeSkip); // command and unacked polls cannot stream
         writeControl(0xf6, EZFlashRAM[0xf7]);
      }
      MPE3TitlePollingHndlr();
      assert(MPE3Title.Intro && MPE3Title.SkipApplied && MPE3Title.Visit == FullVisits - 1);
      assert(MPE3TitleAssets == MPE3TitleInternalAssets && !PSRAMAvailable);
      std::vector<uint8_t> SkippedDisplay(10000, 0xa5);
      unsigned Cells = 0, Frames = 0, FinalSignals = 0, Limit = 0;
      while (MPE3TitleOwned)
      {
         assert(++Limit < 60);
         tracePacket(argc == 3 ? &SkipTrace : nullptr);
         const uint8_t Type = EZFlashRAM[3], Length = EZFlashRAM[6];
         if (Type == MPE3TitleCELL)
         {
            assert((EZFlashRAM[5] & 12) == 12);
            assert(bool(EZFlashRAM[5] & 16) == (Cells == 0));
            for (unsigned At = 8; At < unsigned(Length) + 8u; At += 12)
            {
               const unsigned Cell = MHSNativeRead16(EZFlashRAM + At);
               assert(Cell == Cells++);
               std::copy(EZFlashRAM + At + 2, EZFlashRAM + At + 10, SkippedDisplay.begin() + Cell * 8);
               SkippedDisplay[8000 + Cell] = EZFlashRAM[At + 10]; SkippedDisplay[9000 + Cell] = EZFlashRAM[At + 11];
            }
            if (EZFlashRAM[5] & 2) { FinalSignals++; assert(Cells == 1000); }
         }
         else if (Type == MPE3TitleSID)
         {
            assert(Cells == 1000 && SkippedDisplay == Login && EZFlashRAM[5] == 5);
            assert(std::all_of(EZFlashRAM + 8, EZFlashRAM + 34, [](uint8_t B) { return B == 0; }));
            Frames++;
         }
         else assert(Type == MPE3TitleEND && SkippedDisplay == Login);
         writeControl(0xf4, 2); // repeats during final transfer cannot rewind it
         writeControl(0xf6, EZFlashRAM[0xf7]); MPE3TitlePollingHndlr();
      }
      assert(Cells == 1000 && Frames == 1 && FinalSignals == 1 && EZFlashRAM[0xf5] == 3);
      writeControl(0xf4, 2); MPE3TitlePollingHndlr(); assert(!MPE3TitleOwned);
      SkipScenarios++;
      SkipTrace.close();
   }
   Invalid = FullIntro; put16(Invalid, FinalAt, 999); repairCRC(Invalid); reject(Invalid, MPE3TitleErrorDelta);
   Invalid = FullIntro; Invalid[FinalAt + 3] = 3; repairCRC(Invalid); reject(Invalid, MPE3TitleErrorDelta);
   Invalid = FullIntro; Invalid[15] = 0x1f; repairCRC(Invalid); reject(Invalid, MPE3TitleErrorHeader);
   start(FullIntro, Root, false); writeControl(0xf4, 2); MPE3TitlePollingHndlr();
   const uint8_t PendingSequence = EZFlashRAM[0xf7];
   FailNextRead = true;
   MPE3TitlePollingHndlr(); assert(FailNextRead && EZFlashRAM[0xf7] == PendingSequence);
   writeControl(0xf6, PendingSequence); MPE3TitlePollingHndlr();
   assert(!FailNextRead && EZFlashRAM[0xfb] == MPE3TitleErrorRead && EZFlashRAM[3] == MPE3TitleERROR);
   assert(MaxReadLength <= 1024);
   std::cout << "{\"actualNativeModule\":true,\"psram\":false,\"assetBytes\":" << Asset.size()
      << ",\"visits\":" << VisitCount << ",\"scoreTicks\":" << ScoreTicks
      << ",\"sidPacketsIncludingFinalGateOff\":" << SIDPackets << ",\"cellPackets\":" << CellPackets
      << ",\"baseRecords\":" << BaseRecords << ",\"deltaRecords\":" << DeltaRecords
      << ",\"totalPackets\":" << PacketCount << ",\"sequenceWraps\":" << SequenceWraps
      << ",\"malformedOrBusyRejections\":" << Rejections << ",\"readCalls\":" << ReadCalls
      << ",\"skipScenarios\":" << SkipScenarios << ",\"streamedAssetBytes\":" << FullIntro.size()
      << ",\"maximumReadBytes\":" << MaxReadLength
      << ",\"endAcknowledged\":true}" << std::endl;
   return 0;
}

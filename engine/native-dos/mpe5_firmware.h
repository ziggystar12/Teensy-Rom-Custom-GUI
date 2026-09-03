// Native FreeDOS session hosted by the bank-58 M3 packet service. The C64
// owns launch, packet acknowledgement and keyboard sampling; this code owns
// the PC/XT machine in foreground time.
#pragma once

#ifndef MPE5_CODE
#define MPE5_CODE FLASHMEM
#endif

#include "mpe5_platform.cpp"
#include "mpe5_8086tiny.cpp"
#include "mpe5_paged_memory.cpp"

static_assert(mpe5::ConventionalRamBytes == 640u * 1024u,
              "FreeDOS native VM exposes 640 KiB conventional RAM");
static_assert(mpe5::CgaTextCells == 1000u,
              "CGA 40x25 terminal must map to the C64 cell grid");
static_assert(mpe5::AddressMapBytes == 0x10fff0u,
              "8086tiny must retain its complete 20-bit address map");
static constexpr uint32_t MPE5FixedSegmentBytes = 65536u;
static constexpr uint32_t MPE5ConsoleBytes =
   mpe5::NativeBackingBytes - mpe5::NativeTextShadowAddress;
static constexpr uint32_t MPE5CacheStorageBytes =
   (mpe5::PagedMemory::WorkspaceBytes + 31u) & ~31u;
static constexpr uint32_t MPE5WorkspaceBytes =
   MPE5CacheStorageBytes + MPE5FixedSegmentBytes + MPE5ConsoleBytes;
static constexpr uint32_t MPE5SwapBytes =
   mpe5::PagedMemory::PageCount * mpe5::SectorBytes;
static_assert(MPE5WorkspaceBytes <= 224u * 1024u,
              "DOS resident workspace must fit beyond the 24 KiB cartridge");

// Implemented beside the cartridge loader: only the validated unused tail
// may be lent to DOS. No optional PSRAM or runtime heap is needed.
#include "mpe5_cartridge_memory.h"

static constexpr uint8_t MPE5HeaderBytes = 16;
static constexpr uint8_t MPE5Protocol = 1;
static constexpr uint16_t MPE5BiosMaxBytes = 0xff00u;
static constexpr uint32_t MPE5InstructionSlice = 25000u;

// These small controls/objects need ordinary C++ startup initialization.
// In particular, File has a vtable and handle pointer: placing it in Teensy's
// NOLOAD DMAMEM can make even the first reset dereference an invalid object.
static volatile bool MPE5Active, MPE5InputPending;
static bool MPE5FirstFrame, MPE5TransportCanary;
static volatile uint8_t MPE5InputKey, MPE5InputScan;
static uint8_t MPE5Error;
static bool MPE5InputActivationPending;
static uint32_t MPE5Root;
static DMAMEM uint32_t MPE5SliceIo;
static File MPE5DiskFile;
static File MPE5SwapFile;
// These small, plain metadata objects are assigned from scratch on every
// reset before dereferencing any pointer, including after NOLOAD startup.
static DMAMEM uint8_t *MPE5PublishedViewport;
static DMAMEM mpe5::PagedMemory MPE5Memory;
// These pointer-free work buffers are explicitly initialized in MPE5Reset.
// Keeping them in RAM2 preserves the shared firmware's RAM1 stack reserve.
// File and ISR ownership flags above must retain ordinary initialization.
static DMAMEM mpe5::Keyboard MPE5Keyboard;
static DMAMEM mpe5::PcSpeaker MPE5Speaker;
static DMAMEM mpe5::CgaText MPE5Text;

static FLASHMEM uint32_t MPE5Read32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static FLASHMEM bool MPE5ReadSector(void *Context, uint32_t LBA,
                                    uint8_t Out[mpe5::SectorBytes])
{
   File *Input = static_cast<File *>(Context);
   ++MPE5SliceIo;
   uint32_t Offset = LBA * (uint32_t)mpe5::SectorBytes;
   if (!Input || !*Input || Offset > Input->size() ||
       mpe5::SectorBytes > Input->size() - Offset || !Input->seek(Offset))
      return false;
   return Input->read(Out, mpe5::SectorBytes) == mpe5::SectorBytes;
}

static FLASHMEM bool MPE5ReadPage(void *, uint32_t Page, uint8_t Out[512])
{
   return MPE5ReadSector(&MPE5SwapFile, Page, Out);
}

static FLASHMEM bool MPE5WritePage(void *, uint32_t Page, const uint8_t In[512])
{
   ++MPE5SliceIo;
   if (Page >= mpe5::PagedMemory::PageCount || !MPE5SwapFile ||
       !MPE5SwapFile.seek(Page * 512u)) return false;
   return MPE5SwapFile.write(In, 512u) == 512u;
}

static FLASHMEM bool MPE5MemoryReset(void *) { return MPE5Memory.reset(); }
static FLASHMEM bool MPE5MemoryRead(void *, uint32_t Address, uint8_t *Out, uint32_t Length)
{ return MPE5Memory.read(Address, Out, Length); }
static FLASHMEM bool MPE5MemoryWrite(void *, uint32_t Address, const uint8_t *In, uint32_t Length)
{ return MPE5Memory.write(Address, In, Length); }
static FLASHMEM bool MPE5ShouldYield(void *)
{ return MPE5SliceIo >= 4u || !MPE3TitleOwned || !MPE3TitleSelected(); }

// Preserve the DOS character bytes, including punctuation and lowercase.
// The full 8x8 Latin font uses the hires cell's pixel width directly.
#include "mpe5_font8x8.h"

static FLASHMEM void MPE5Glyph(uint8_t Character, uint8_t Bitmap[8])
{
   const uint8_t Glyph = Character < 128u ? Character : '?';
   memcpy(Bitmap, MPE5Font8x8[Glyph], 8);
}

static FLASHMEM void MPE5Reset()
{
   MPE5Active = MPE5InputPending = MPE5FirstFrame =
      MPE5TransportCanary = false;
   MPE5InputKey = MPE5InputScan = 0;
   MPE5InputActivationPending = false;
   MPE5Root = 0;
   MPE5SliceIo = 0;
   MPE5Keyboard.clear();
   MPE5Speaker = {};
   MPE5Text.reset();
   mpe5::coreReset();
   if (MPE5DiskFile) MPE5DiskFile.close();
   if (MPE5SwapFile) MPE5SwapFile.close();
   MPE5Memory = {};
   MPE5PublishedViewport = nullptr;
}

// M5D1 carries only version, header size, BIOS byte count and BIOS CRC. The
// SD `/DOSVM/DOSVM.IMG` file is the bootable C: volume.
static FLASHMEM bool MPE5Start(uint32_t Root)
{
   uint8_t Header[MPE5HeaderBytes];
   uint32_t BiosBytes;
   MPE5Reset();
   MPE5Error = 0;
   if (!MPE4Read(nullptr, Root, Header, sizeof(Header)) ||
       memcmp(Header, "M5D1", 4) || Header[4] != MPE5Protocol ||
       Header[5] != sizeof(Header))
   { MPE5Error = MPE3TitleErrorHeader; return false; }
   BiosBytes = MPE5Read32(Header + 8);
   if (!BiosBytes || BiosBytes > MPE5BiosMaxBytes ||
       BiosBytes > MPE3TitleInternalAssetBytes ||
       !MPE4Read(nullptr, Root + sizeof(Header), MPE3TitleInternalAssets,
                 (uint16_t)BiosBytes) ||
       MHSNativeCRC32(MPE3TitleInternalAssets, BiosBytes) != MPE5Read32(Header + 12))
   { MPE5Error = MPE3TitleErrorRead; return false; }
   MPE5DiskFile = SD.open("/DOSVM/DOSVM.IMG", FILE_READ);
   if (!MPE5DiskFile || !MPE5DiskFile.size() ||
       (MPE5DiskFile.size() % mpe5::SectorBytes))
   { MPE5Error = MPE3TitleErrorRead; MPE5Reset(); return false; }
   uint8_t *Workspace = nullptr;
   uint32_t WorkspaceBytes = 0;
   if (!MPE5BorrowCartridgeTail(&Workspace, &WorkspaceBytes) ||
       WorkspaceBytes < MPE5WorkspaceBytes)
   { MPE5Error = MPE3TitleErrorMemory; MPE5Reset(); return false; }
   // FILE_WRITE_BEGIN is read/write without append or truncation. The kit
   // supplies the allocated scratch file so startup needs no large SD write.
   // Old page contents are invisible until written in this session.
   MPE5SwapFile = SD.open("/DOSVM/DOSVM.SWP", FILE_WRITE_BEGIN);
   if (!MPE5SwapFile || MPE5SwapFile.size() < MPE5SwapBytes ||
       !MPE5Memory.start(Workspace, mpe5::PagedMemory::WorkspaceBytes,
                       {nullptr, MPE5ReadPage, MPE5WritePage}))
   { MPE5Error = MPE3TitleErrorRead; MPE5Reset(); return false; }
   mpe5::CoreHost Host{};
   Host.memory = {nullptr, MPE5MemoryReset, MPE5MemoryRead, MPE5MemoryWrite, MPE5ShouldYield};
   Host.fixedF000 = Workspace + MPE5CacheStorageBytes;
   Host.fixedF000Bytes = MPE5FixedSegmentBytes;
   Host.consoleShadow = Host.fixedF000 + MPE5FixedSegmentBytes;
   Host.consoleViewport = Host.consoleShadow +
      mpe5::NativeTextViewportAddress - mpe5::NativeTextShadowAddress;
   Host.bios = MPE3TitleInternalAssets;
   Host.biosBytes = (uint16_t)BiosBytes;
   Host.drive = {&MPE5DiskFile, MPE5ReadSector,
                 (uint32_t)(MPE5DiskFile.size() / mpe5::SectorBytes)};
   Host.keyboard = &MPE5Keyboard;
   Host.speaker = &MPE5Speaker;
   if (!mpe5::coreStart(Host))
   { MPE5Error = MPE3TitleErrorMemory; MPE5Reset(); return false; }
   MPE5PublishedViewport = Host.consoleViewport;
   MPE5Root = Root;
   MPE5FirstFrame = true;
   // Publish one fixed, tiny packet before running the PC.  A physical
   // transport fault now remains at packet zero; an ACK of this packet proves
   // that the M3 mailbox can carry a stable header, payload and commit.
   MPE5TransportCanary = true;
   MPE3Title.Loaded = true;
   MPE3Title.Phase = MPE3TitleFinished;
   MPE3TitleMailbox[0xFC] = MPE3TitleMailbox[0xFD] = 0;
   MPE3TitleMailbox[0xFE] = MPE3TitleMailbox[0xFF] = 0;
   MPE3TitleMemoryBarrier();
   MPE5Active = true;
   return true;
}

// This runs in the Phi2 handler, so it merely validates and records one key.
static inline void MPE5LatchInput()
{
   uint8_t Sequence = MPE3TitleMailbox[0xFE], Flags = MPE3TitleMailbox[0xFD];
   if (!Sequence || Sequence == MPE3TitleMailbox[0xFC] || MPE5InputPending ||
       !(Flags & 1u) || (Flags & ~1u)) return;
   uint8_t Key = MPE3TitleMailbox[0xF8], Scan = MPE3TitleMailbox[0xF9];
   if ((uint8_t)(0xA5 ^ Key ^ Scan ^ Flags ^ Sequence) != MPE3TitleMailbox[0xFF]) return;
   MPE5InputKey = Key; MPE5InputScan = Scan;
   MPE3TitleMemoryBarrier();
   MPE5InputPending = true;
   MPE3TitleMailbox[0xFC] = Sequence;
}

static FLASHMEM void MPE5PublishFrameEnd()
{
   // A validated gameplay SID keeps the terminal in hires mode and gives
   // it a frame tick before sampling keyboard input for the next packet.
   memset(MPE3TitlePacket + MPE3TitlePacketHeaderBytes, 0, 26);
   MPE3TitlePublish(MPE3TitleSID, 0x21 | MPE3TitleCellHires, 26);
}

static FLASHMEM void MPE5NextPacket()
{
   if (MPE5TransportCanary)
   {
      uint8_t *Record = MPE3TitlePacket + MPE3TitlePacketHeaderBytes;
      memset(Record, 0, MPE3TitleCellBytes);
      MPE5Glyph(' ', Record + 2);
      Record[10] = 0x10;
      Record[11] = 1;
      MPE5TransportCanary = false;
      MPE3TitlePublish(MPE3TitleCELL, MPE3TitleCellModeValid |
         MPE3TitleCellHires | MPE3TitleCellReplace, MPE3TitleCellBytes);
      return;
   }
   if (MPE5InputPending)
   {
      mpe5::Key Key{MPE5InputKey, MPE5InputScan};
      if (MPE5Keyboard.push(Key)) MPE5InputPending = false;
   }
   if (MPE5InputActivationPending)
   {
      // The existing native terminal enables its C64 keyboard sampler from a
      // validated gameplay-style frame end. A silent SID register set arms
      // that path after the complete initial text frame is visible.
      MPE5InputActivationPending = false;
      MPE5PublishFrameEnd();
      return;
   }
   MPE5SliceIo = 0;
   if (!mpe5::coreRun(MPE5InstructionSlice))
   { MPE5Error = MPE3TitleErrorRead; MPE3TitleFail(MPE5Error); return; }
   uint8_t Dirty[MPE3TitleCellsPerPacket * sizeof(mpe5::TextCell)];
   bool InitialFrame = !MPE5Text.initialComplete();
   uint16_t Count = MPE5Text.changes(MPE5PublishedViewport,
                                     Dirty, MPE3TitleCellsPerPacket);
   if (!Count)
   {
      // An idle prompt still needs a packet: the C64 samples its keyboard
      // between packets and otherwise waits until its transport timeout.
      MPE5PublishFrameEnd();
      return;
   }
   for (uint16_t Index = 0; Index < Count; ++Index)
   {
      const uint8_t *Cell = Dirty + Index * sizeof(mpe5::TextCell);
      uint8_t *Record = MPE3TitlePacket + MPE3TitlePacketHeaderBytes +
         Index * MPE3TitleCellBytes;
      Record[0] = Cell[0]; Record[1] = Cell[1];
      MPE5Glyph(Cell[2], Record + 2);
      Record[10] = (Cell[3] & 15u) << 4; // foreground over black bitmap background
      Record[11] = Cell[3];
   }
   uint8_t Flags = MPE3TitleCellModeValid | MPE3TitleCellHires |
      (MPE5FirstFrame ? MPE3TitleCellReplace : 0);
   MPE5FirstFrame = false;
   if (InitialFrame && MPE5Text.initialComplete())
   {
      // Completion belongs to the unique-cell traversal, not a sum of dirty
      // records. The following SID is published only after this CELL is ACKed.
      Flags |= 2; // initial complete text frame: make it visible
      MPE5InputActivationPending = true;
   }
   MPE3TitlePublish(MPE3TitleCELL, Flags, Count * MPE3TitleCellBytes);
}

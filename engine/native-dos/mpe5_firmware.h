// Native FreeDOS session hosted by the bank-58 M3 packet service. The C64
// owns launch, packet acknowledgement and keyboard sampling; this code owns
// the PC/XT machine in foreground time.
#pragma once

#ifndef MPE5_CODE
#define MPE5_CODE FLASHMEM
#endif
#ifndef MPE5_HOT_CODE
#ifdef FASTRUN
#define MPE5_HOT_CODE FASTRUN
#else
#define MPE5_HOT_CODE MPE5_CODE
#endif
#endif

#include "mpe5_platform.cpp"
#include "mpe5_speaker.cpp"
#include "mpe5_video.cpp"
#include "mpe5_8086tiny.cpp"
#include "mpe5_direct_memory.cpp"
#include "mpe5_redirector.cpp"
#include "mpe5_folder_fs.h"
#include <new>

static_assert(mpe5::ConventionalRamBytes == 512u * 1024u,
              "FreeDOS native VM owns all 512 KiB of RAM2");
static_assert(mpe5::CgaTextCells == 1000u,
              "CGA 40x25 terminal must map to the C64 cell grid");
static_assert(mpe5::AddressMapBytes == 0x10fff0u,
              "8086tiny must retain its complete 20-bit address map");
static constexpr uint32_t MPE5FixedSegmentBytes = 65536u;
static constexpr uint32_t MPE5ConsoleBytes =
   mpe5::NativeBackingBytes - mpe5::NativeTextShadowAddress;
static constexpr uint32_t MPE5DecodeBytes = 20u * 256u;
static constexpr uint32_t MPE5Align32(uint32_t Bytes)
{ return (Bytes + 31u) & ~31u; }
static constexpr uint32_t MPE5FixedOffset = 0;
static constexpr uint32_t MPE5PortsOffset =
   MPE5Align32(MPE5FixedOffset + MPE5FixedSegmentBytes);
static constexpr uint32_t MPE5DecodeOffset =
   MPE5Align32(MPE5PortsOffset + mpe5::NativeIoPortBytes);
static constexpr uint32_t MPE5ConsoleOffset =
   MPE5Align32(MPE5DecodeOffset + MPE5DecodeBytes);
static constexpr uint32_t MPE5VideoOffset =
   MPE5Align32(MPE5ConsoleOffset + MPE5ConsoleBytes);
static constexpr uint32_t MPE5RedirectorOffset =
   MPE5Align32(MPE5VideoOffset + mpe5::CgaVideo::WorkspaceBytes);
static constexpr uint32_t MPE5FolderOffset =
   MPE5Align32(MPE5RedirectorOffset + sizeof(mpe5::Redirector));
static constexpr uint32_t MPE5WorkspaceBytes =
   MPE5Align32(MPE5FolderOffset + sizeof(mpe5::FolderFilesystem));
static_assert(MPE5WorkspaceBytes <= 224u * 1024u,
              "DOS resident workspace must fit beyond the 24 KiB cartridge");

// Implemented beside the cartridge loader: only the validated unused tail
// may be lent to DOS. No optional PSRAM or runtime heap is needed.
#include "mpe5_cartridge_memory.h"

static constexpr uint8_t MPE5HeaderBytes = 16;
static constexpr uint8_t MPE5Protocol = 1;
static constexpr uint16_t MPE5BiosMaxBytes = 0xff00u;
static constexpr uint32_t MPE5InstructionSlice = 25000u;
#ifndef MPE5_RAM2_BASE
#define MPE5_RAM2_BASE ((uint8_t *)0x20200000u)
#endif

// These small controls/objects need ordinary C++ startup initialization.
// In particular, File has a vtable and handle pointer: placing it in Teensy's
// NOLOAD DMAMEM can make even the first reset dereference an invalid object.
static volatile bool MPE5Active, MPE5InputPending, MPE5Ram2Owned;
static volatile bool MPE5QuietRead;
static constexpr uint8_t MPE5QuietReadStatus = 0x10;
static MHSNativeArenaView MPE5ArenaView;
static bool MPE5FirstFrame, MPE5TransportCanary;
static bool MPE5BootScreenPending;
static uint8_t MPE5BootScreenSequence;
// The POST page remains on screen for a short, ACK-proven interval.  Count
// display frames rather than wall time so a slow C64 never misses it.
static uint8_t MPE5BootHoldFrames, MPE5BootBeepFrames;
static bool MPE5Graphics, MPE5DisplayHires, MPE5DisplayComplete;
static bool MPE5SharpGraphics, MPE5SharpHotkeyHeld, MPE5WarmRebootHotkeyHeld;
static uint8_t MPE5DisplayBackground;
static uint32_t MPE5SpeakerRevision;
static volatile uint8_t MPE5InputKey, MPE5InputScan;
static volatile uint8_t MPE5InputFlags, MPE5InputJoy;
static volatile uint8_t MPE5Error;
static bool MPE5InputActivationPending;
static uint32_t MPE5Root;
static uint32_t MPE5SliceIo;
static bool MPE5SliceYieldForInput;
static uint32_t MPE5DiskSectors;
static FsFile MPE5DiskFile;
// Construct these inline in the borrowed cartridge tail, never in RAM2/heap.
static mpe5::Redirector *MPE5Redirector;
static mpe5::FolderFilesystem *MPE5Folder;
static uint8_t *MPE5Bios;
static uint16_t MPE5BiosBytes;
static uint8_t *MPE5PublishedShadow;
static uint8_t *MPE5PublishedViewport;
static mpe5::DirectMemory MPE5Memory;
static mpe5::Keyboard MPE5Keyboard;
static mpe5::PcSpeaker MPE5Speaker;
static mpe5::CgaText80 MPE5Text;
static mpe5::CgaVideo MPE5DisplayVideo;
static mpe5::SpeakerSid MPE5Sid;

static FLASHMEM uint32_t MPE5Read32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static FLASHMEM bool MPE5ReadSector(void *Context, uint32_t LBA,
                                    uint8_t Out[mpe5::SectorBytes])
{
   FsFile *Input = static_cast<FsFile *>(Context);
   ++MPE5SliceIo;
   uint64_t Offset = (uint64_t)LBA * mpe5::SectorBytes;
   if (!Input || !Input->isOpen() || Offset > Input->fileSize() ||
       mpe5::SectorBytes > Input->fileSize() - Offset || !Input->seekSet(Offset))
      return false;
   return Input->read(Out, mpe5::SectorBytes) == mpe5::SectorBytes;
}

static FLASHMEM bool MPE5WriteSector(void *Context, uint32_t LBA,
                                     const uint8_t In[mpe5::SectorBytes])
{
   FsFile *Output = static_cast<FsFile *>(Context);
   ++MPE5SliceIo;
   const uint64_t Offset = (uint64_t)LBA * mpe5::SectorBytes;
   if (!Output || !Output->isOpen() || Offset > Output->fileSize() ||
       mpe5::SectorBytes > Output->fileSize() - Offset || !Output->seekSet(Offset))
      return false;
   return Output->write(In, mpe5::SectorBytes) == mpe5::SectorBytes && Output->sync();
}

static FLASHMEM void MPE5FolderIo(void *) { ++MPE5SliceIo; }
static FLASHMEM bool MPE5RedirectorService(void *Context, uint8_t Operation,
                                           mpe5::RedirectorRegisters &Registers)
{ return static_cast<mpe5::Redirector *>(Context)->service(Operation, Registers); }
static FLASHMEM void MPE5RedirectorReset(void *Context)
{ static_cast<mpe5::Redirector *>(Context)->reset(); }

static FLASHMEM bool MPE5MemoryReset(void *) { return MPE5Memory.reset(); }
static FLASHMEM bool MPE5MemoryRead(void *, uint32_t Address, uint8_t *Out, uint32_t Length)
{ return MPE5Memory.read(Address, Out, Length); }
static FLASHMEM bool MPE5MemoryWrite(void *, uint32_t Address, const uint8_t *In, uint32_t Length)
{ return MPE5Memory.write(Address, In, Length); }
static FLASHMEM bool MPE5ShouldYield(void *)
{
   // Input and an ACK can arrive in the PHI2 ISR during a guest slice.
   // Return promptly so foreground accepts the input or publishes the next
   // packet; finishing thousands more guest instructions adds visible lag.
   if (MPE5QuietRead || MPE5SliceIo >= 4u || (MPE5SliceYieldForInput && MPE5InputPending) ||
       !MPE3TitleOwned || !MPE3TitleSelected() ||
       (MPE3Title.Pending &&
        MPE3TitleMailbox[MPE3TitleRegACK] == MPE3Title.Sequence)) return true;
   return false;
}

static FLASHMEM void MPE5VideoWrite(void *, uint16_t Offset,
                                  const uint8_t *Data, uint16_t Length)
{ MPE5DisplayVideo.write(Offset, Data, Length); }

// The 80-column console uses the supplied mist64/80columns 4x8 charset. It
// keeps DOS text readable at two characters per physical C64 cell and uses
// white on black, like an MDA-style terminal.
#include "mpe5_font8x8.h"
#include "mpe5_font4x8.h"

static FLASHMEM void MPE5Glyph(uint8_t Character, uint8_t Bitmap[8])
{
   const uint8_t Glyph = Character < 128u ? Character : '?';
   memcpy(Bitmap, MPE5Font8x8[Glyph], 8);
}

static FLASHMEM void MPE5Glyph4(uint8_t Character, uint8_t Bitmap[8])
{
   // CP437's common box strokes make command-line boxes readable even though
   // the compact source charset itself follows C64 screen-code ordering.
   if (Character == 0xb3u)
   {
      for (uint8_t Row = 0; Row != 8u; ++Row) Bitmap[Row] = 0x90u;
      return;
   }
   if (Character == 0xc4u)
   {
      memset(Bitmap, 0, 8); Bitmap[3] = 0xf0u; return;
   }
   uint8_t Glyph = Character;
   if (Character >= 'A' && Character <= 'Z') Glyph = uint8_t(Character - 'A' + 1u);
   else if (Character >= 'a' && Character <= 'z') Glyph = uint8_t(0x80u + Character - 'a' + 1u);
   else if (Character >= '[' && Character <= '_') Glyph = uint8_t(Character - '[' + 27u);
   else if (Character == '`') Glyph = 0u;
   else if (Character >= '{' && Character <= '~') Glyph = uint8_t(0x80u + Character - '{' + 27u);
   else if (Character > 0x7fu) Glyph = '?';
   for (uint8_t Row = 0; Row != 8u; ++Row)
      Bitmap[Row] = uint8_t(MPE5Font4x8[Glyph][Row] << 4u);
}

static FLASHMEM void MPE5GlyphPair(uint8_t LeftCharacter,
                                   uint8_t RightCharacter, uint8_t Cursor,
                                   uint8_t Bitmap[8])
{
   uint8_t Left[8], Right[8];
   MPE5Glyph4(LeftCharacter, Left); MPE5Glyph4(RightCharacter, Right);
   for (uint8_t Row = 0; Row != 8u; ++Row)
      Bitmap[Row] = uint8_t(Left[Row] | (Right[Row] >> 4u));
   // A thin underline is visible without obscuring the command character.
   if (Cursor & 1u) Bitmap[7] |= 0xf0u;
   if (Cursor & 2u) Bitmap[7] |= 0x0fu;
}

static FLASHMEM void MPE5Reset()
{
   // RAM2 contains the allocator and shared-engine state after a cold boot.
   // Once DOS owns it, only a hardware/MCU reset may restore that state.
   if (MPE5Ram2Owned || MHSNativeArenaRequiresReset()) return;
   MPE5Active = MPE5InputPending = MPE5FirstFrame =
      MPE5TransportCanary = false;
   MPE5QuietRead = false;
   MPE5BootScreenPending = false;
   MPE5BootScreenSequence = 0;
   MPE5BootHoldFrames = MPE5BootBeepFrames = 0;
   MPE5InputKey = MPE5InputScan = 0;
   MPE5InputFlags = MPE5InputJoy = 0;
   MPE5InputActivationPending = false;
   MPE5Graphics = MPE5DisplayComplete = false;
   MPE5SharpGraphics = MPE5SharpHotkeyHeld = MPE5WarmRebootHotkeyHeld = false;
   MPE5DisplayHires = true;
   MPE5DisplayBackground = 0;
   MPE5SpeakerRevision = 0;
   MPE5Root = 0;
   MPE5SliceIo = 0;
   MPE5SliceYieldForInput = true;
   MPE5DiskSectors = 0;
   MPE5Keyboard.clear();
   MPE5Speaker = {};
   MPE5Sid.reset();
   MPE5DisplayVideo = {}; // DMAMEM is NOLOAD: discard any stale workspace pointer.
   MPE5Text.reset();
   mpe5::coreReset();
   if (MPE5Redirector) {
      MPE5Redirector->~Redirector(); MPE5Redirector = nullptr;
   }
   if (MPE5Folder) {
      MPE5Folder->end(); MPE5Folder->~FolderFilesystem(); MPE5Folder = nullptr;
   }
   if (MPE5DiskFile.isOpen()) MPE5DiskFile.close();
   MPE5Memory = {};
   MPE5Bios = nullptr;
   MPE5BiosBytes = 0;
   MPE5PublishedShadow = nullptr;
   MPE5PublishedViewport = nullptr;
   if (MHSNativeArenaOwns(MHSNativeArenaOwner::DOS))
      MHSNativeArenaRelease(MHSNativeArenaOwner::DOS);
   MPE5ArenaView = {};
}

// The USB1 device controller owns queue heads and CDC buffers in RAM2.  Merely
// clearing Run/Stop is insufficient: a primed or active endpoint may still
// complete a DMA.  Teensy's own reset path waits for priming, flushes every
// endpoint, and the NXP EHCI device deinit then stops and resets the controller.
// Bound every hardware wait so a wedged USB block rejects the DOS handoff
// without ever clearing or lending RAM2.
#if defined(__IMXRT1062__)
static FLASHMEM bool MPE5WaitUsb1Clear(volatile uint32_t *Register,
                                      uint32_t Mask)
{
   const uint32_t Started = ARM_DWT_CYCCNT;
   const uint32_t TimeoutCycles = F_CPU_ACTUAL / 200u; // five milliseconds
   for (uint32_t Poll = 0; Poll < 3000000u; Poll++)
   {
      if (!(*Register & Mask)) return true;
      if ((uint32_t)(ARM_DWT_CYCCNT - Started) >= TimeoutCycles) return false;
   }
   return false;
}
#endif

static FLASHMEM bool MPE5QuiesceRam2Services()
{
#if defined(__IMXRT1062__)
   const uint32_t InterruptMask = __get_primask();
   __disable_irq();
   // No ISR may prime another transfer once endpoint shutdown begins.
   USB1_USBINTR = 0;
   USB1_GPTIMER0CTRL = 0;
   USB1_GPTIMER1CTRL = 0;
   NVIC_DISABLE_IRQ(IRQ_USB1);
   NVIC_CLEAR_PENDING(IRQ_USB1);

   USB1_ENDPTSETUPSTAT = USB1_ENDPTSETUPSTAT;
   USB1_ENDPTCOMPLETE = USB1_ENDPTCOMPLETE;
   bool Quiesced = MPE5WaitUsb1Clear(&USB1_ENDPTPRIME, 0xffffffffu);
   // ENDPTSTATUS can reassert while a flush is being accepted.  Match NXP's
   // endpoint-cancel rule by checking status after FLUSH self-clears, with a
   // small bounded number of retries.
   for (uint8_t Try = 0; Quiesced && Try < 4u; Try++)
   {
      USB1_ENDPTFLUSH = 0xffffffffu;
      Quiesced = MPE5WaitUsb1Clear(&USB1_ENDPTFLUSH, 0xffffffffu);
      if (Quiesced && USB1_ENDPTSTATUS == 0) break;
      if (Try == 3u) Quiesced = false;
   }

   // Device mode has no usable USBSTS.HCHalted indication.  Run/Stop readback
   // followed by a completed controller reset is the hardware stop boundary.
   USB1_USBCMD &= ~USB_USBCMD_RS;
   Quiesced = Quiesced &&
      MPE5WaitUsb1Clear(&USB1_USBCMD, USB_USBCMD_RS);
   if (Quiesced)
   {
      USB1_USBCMD |= USB_USBCMD_RST;
      Quiesced = MPE5WaitUsb1Clear(&USB1_USBCMD, USB_USBCMD_RST) &&
         !(USB1_USBCMD & USB_USBCMD_RS) &&
         !(USB1_USBMODE & USB_USBMODE_CM_MASK) &&
         USB1_ENDPTPRIME == 0 && USB1_ENDPTFLUSH == 0 &&
         USB1_ENDPTSTATUS == 0;
   }

   usb_configuration = 0;
   yield_active_check_flags &= ~(YIELD_CHECK_USB_SERIAL |
      YIELD_CHECK_USB_SERIALUSB1 | YIELD_CHECK_USB_SERIALUSB2);
   __asm__ volatile ("dsb\n\tisb" ::: "memory");
   __set_primask(InterruptMask);
   return Quiesced;
#elif defined(MPE5_USB_QUIESCE_TEST)
   return MPE5TestUsb1Quiesce();
#else
   // Ordinary host tests have no USB controller or RAM2 DMA master.
   return true;
#endif
}

// M5D1 carries only version, header size, BIOS byte count and BIOS CRC. The
// SD `/DOSVM/DOSVM.IMG` file is the bootable C: volume.
static FLASHMEM bool MPE5Start(uint32_t Root)
{
   uint8_t Header[MPE5HeaderBytes];
   MPE5Reset();
   MPE5Error = 0;
   if (MPE5Ram2Owned || MHSNativeArenaRequiresReset())
   { MPE5Error = MPE3TitleErrorMemory; return false; }
   if (!MPE4Read(nullptr, Root, Header, sizeof(Header)) ||
       memcmp(Header, "M5D1", 4) || Header[4] != MPE5Protocol ||
       Header[5] != sizeof(Header))
   { MPE5Error = MPE3TitleErrorHeader; return false; }
   const uint32_t BiosBytes = MPE5Read32(Header + 8);
   if (!BiosBytes || BiosBytes > MPE5BiosMaxBytes)
   { MPE5Error = MPE3TitleErrorRead; return false; }

   MPE5ResetOnlyArena Arena{};
   if (!MPE5BorrowResetOnlyArena(&Arena) ||
       Arena.workspaceBytes < MPE5WorkspaceBytes)
   { MPE5Error = MPE3TitleErrorMemory; return false; }
   uint8_t *const Fixed = Arena.workspace + MPE5FixedOffset;
   uint8_t *const Ports = Arena.workspace + MPE5PortsOffset;
   uint8_t *const Decode = Arena.workspace + MPE5DecodeOffset;
   uint8_t *const Console = Arena.workspace + MPE5ConsoleOffset;
   uint8_t *const Video = Arena.workspace + MPE5VideoOffset;
   uint8_t *const Bios = Fixed + 0x100u;
   if (!MPE4Read(nullptr, Root + sizeof(Header), Bios, (uint16_t)BiosBytes) ||
       MHSNativeCRC32(Bios, BiosBytes) != MPE5Read32(Header + 12))
   { MPE5Error = MPE3TitleErrorRead; return false; }
   MPE5Bios = Bios;
   MPE5BiosBytes = (uint16_t)BiosBytes;

   MPE5DiskFile = SD.sdfs.open("/DOSVM/DOSVM.IMG", O_RDWR);
   const uint64_t DiskBytes = MPE5DiskFile.fileSize();
   if (!MPE5DiskFile.isOpen() || !DiskBytes ||
       (DiskBytes % mpe5::SectorBytes) ||
       DiskBytes / mpe5::SectorBytes > UINT32_MAX)
   { MPE5Error = MPE3TitleErrorRead; MPE5Reset(); return false; }
   MPE5DiskSectors = (uint32_t)(DiskBytes / mpe5::SectorBytes);
   MPE5Folder = new (Arena.workspace + MPE5FolderOffset)
      mpe5::FolderFilesystem(MPE5FolderIo, nullptr);
   if (!MPE5Folder->begin())
   { MPE5Error = MPE3TitleErrorRead; MPE5Reset(); return false; }
   MPE5Redirector = new (Arena.workspace + MPE5RedirectorOffset) mpe5::Redirector;
   MPE5Redirector->configure(mpe5::coreRedirectorMemory(), MPE5Folder->host());
   if (!MPE5DisplayVideo.start(Video, mpe5::CgaVideo::WorkspaceBytes))
   { MPE5Error = MPE3TitleErrorMemory; MPE5Reset(); return false; }

   mpe5::CoreHost Host{};
   Host.conventionalRam = MPE5_RAM2_BASE;
   Host.conventionalRamBytes = mpe5::ConventionalRamBytes;
   Host.decodeTable = Decode;
   Host.decodeTableBytes = MPE5DecodeBytes;
   Host.memory = {nullptr, MPE5MemoryReset, MPE5MemoryRead,
                  MPE5MemoryWrite, MPE5ShouldYield};
   Host.fixedF000 = Fixed;
   Host.fixedF000Bytes = MPE5FixedSegmentBytes;
   Host.consoleShadow = Console;
   Host.consoleViewport = Console +
      mpe5::NativeTextViewportAddress - mpe5::NativeTextShadowAddress;
   Host.bios = Bios; // staged outside RAM2 before DirectMemory::reset()
   Host.biosBytes = (uint16_t)BiosBytes;
   Host.drive = {&MPE5DiskFile, MPE5ReadSector, MPE5DiskSectors, MPE5WriteSector};
   Host.redirectorContext = MPE5Redirector;
   Host.redirector = MPE5RedirectorService;
   Host.redirectorReset = MPE5RedirectorReset;
   Host.keyboard = &MPE5Keyboard;
   Host.speaker = &MPE5Speaker;
   Host.milliseconds = millis;

   MHSNativeArenaView ArenaView{};
   if (MHSNativeArenaClaim(MHSNativeArenaOwner::DOS,
          MHSNativeArenaCapacity, MHSNativeArenaAlignment,
          &ArenaView) != MHSNativeArenaStatus::Okay ||
       !MHSNativeArenaLeaseValid(&ArenaView))
   { MPE5Error = MPE3TitleErrorMemory; MPE5Reset(); return false; }
   MPE5ArenaView = ArenaView;

   // The CRT loader's File pimpl and debug buffer came from the RAM2 heap.
   // Release every reachable heap object while the allocator is still live.
   if (myFile) myFile.close();
   if (BigBuf) { free(BigBuf); BigBuf = nullptr; BigBufCount = 0; }
   if (!MPE5QuiesceRam2Services())
   { MPE5Error = MPE3TitleErrorMemory; MPE5Reset(); return false; }
   if (MHSNativeArenaSealResetOnly(MHSNativeArenaOwner::DOS) !=
       MHSNativeArenaStatus::Okay)
   {
      // USB and the RAM2 allocator are already gone. Even an ownership-state
      // invariant failure is therefore reboot-only and must never release RAM2.
      MPE5Ram2Owned = true; MPE5Error = MPE3TitleErrorMemory; return false;
   }
   MPE5Ram2Owned = true;
   if (!MPE5Memory.start(MPE5_RAM2_BASE, mpe5::ConventionalRamBytes,
                         Arena.highChunks, Arena.highStorageBytes,
                         Arena.highStride, Ports, mpe5::NativeIoPortBytes) ||
       !mpe5::coreStart(Host))
   { MPE5Error = MPE3TitleErrorMemory; return false; }
   mpe5::coreSetVideoObserver({nullptr, MPE5VideoWrite});
   MPE5PublishedShadow = Host.consoleShadow;
   MPE5PublishedViewport = Host.consoleViewport;
   MPE5Root = Root;
   MPE5FirstFrame = true;
   MPE5TransportCanary = true;
   MPE5BootScreenPending = true;
   MPE5BootScreenSequence = 0;
   MPE5BootHoldFrames = 48u;
   MPE5BootBeepFrames = 10u;
   // A conventional PC POST chirp.  It is rendered through the same SID
   // frame as the visible POST page and is silenced after its bounded hold.
   MPE5Speaker.write(0x43u, 0xb6u);
   MPE5Speaker.write(0x42u, 0xa9u);
   MPE5Speaker.write(0x42u, 0x04u);
   MPE5Speaker.write(0x61u, 0x03u);
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
   if (MPE5Error >= 0x40u) return;
   uint8_t Sequence = MPE3TitleMailbox[0xFE], Flags = MPE3TitleMailbox[0xFD];
   const bool Snapshot = (Flags & 0x80u) != 0;
   if (!Sequence || Sequence == MPE3TitleMailbox[0xFC] || MPE5InputPending ||
       (Snapshot ? (Flags & ~0x8fu) != 0 : Flags != 1u)) return;
   uint8_t Key = MPE3TitleMailbox[0xF8], Scan = MPE3TitleMailbox[0xF9];
   const uint8_t Joy = MPE3TitleMailbox[0xFA];
   if ((!Snapshot && Joy) || (Joy & ~31u) ||
       (uint8_t)(0xA5 ^ Key ^ Scan ^ Joy ^ Flags ^ Sequence) != MPE3TitleMailbox[0xFF]) return;
   MPE5InputKey = Key; MPE5InputScan = Scan;
   MPE5InputFlags = Flags; MPE5InputJoy = Joy;
   MPE3TitleMemoryBarrier();
   MPE5InputPending = true;
   MPE3TitleMailbox[0xFC] = Sequence;
}

static FLASHMEM void MPE5PublishFrameEnd()
{
   uint8_t *Payload = MPE3TitlePacket + MPE3TitlePacketHeaderBytes;
   // MinimalBoot has no full-menu IO1 video-standard register. Use NTSC
   // tuning for this test kit; the adapter also supports explicit PAL tuning.
   MPE5Sid.render(MPE5Speaker, Payload, mpe5::SpeakerSid::NtscClockHz);
   MPE5SpeakerRevision = MPE5Speaker.revision();
   Payload[26] = MPE5DisplayBackground;
   MPE3TitlePublish(MPE3TitleSID, 0x21 |
      (MPE5DisplayHires ? MPE3TitleCellHires : 0), 27);
   if (MPE5BootScreenPending) MPE5BootScreenSequence = MPE3Title.Sequence;
   MPE5DisplayComplete = true;
}

static FLASHMEM void MPE5FailRuntime()
{
   const mpe5::CoreDiagnostic Diagnostic = mpe5::coreDiagnostic();
   const uint32_t Address = Diagnostic.address;
   MPE5Error = 0x40u + (uint8_t)Diagnostic.reason;
   // Once stopped, repurpose input/asset controls for the failed address and
   // CS:IP. Publish them before the typed ERROR; do not silently restart DOS.
   MPE5InputPending = false;
   MPE3TitleMailbox[0xf8] = (uint8_t)Address;
   MPE3TitleMailbox[0xf9] = (uint8_t)(Address >> 8);
   MPE3TitleMailbox[0xfa] = (uint8_t)(Address >> 16);
   MPE3TitleMailbox[0xfc] = (uint8_t)Diagnostic.cs;
   MPE3TitleMailbox[0xfd] = (uint8_t)(Diagnostic.cs >> 8);
   MPE3TitleMailbox[0xfe] = (uint8_t)Diagnostic.ip;
   MPE3TitleMailbox[0xff] = (uint8_t)(Diagnostic.ip >> 8);
   MPE3TitleMemoryBarrier();
   MPE3TitleFail(MPE5Error);
}

static FLASHMEM bool MPE5AcceptInput()
{
   mpe5::Key Key{MPE5InputKey, MPE5InputScan};
   const bool Snapshot = (MPE5InputFlags & 0x80u) != 0;
   // The DOS-only C64 terminal translates Ctrl+Commodore+INST/DEL to the
   // native PC/XT Delete scan. Restart the complete guest here instead of
   // relying on the tiny BIOS's partial warm-boot jump, which retained stale
   // DOS device state. Consume the held chord through the Delete release.
   const bool RebootChord = Snapshot && Key.scan == 0x53u &&
      (MPE5InputFlags & 7u) == 6u;
   const bool ConsumeReboot = Snapshot && Key.scan == 0x53u &&
      (RebootChord || MPE5WarmRebootHotkeyHeld);
   if (ConsumeReboot)
   {
      if (RebootChord && !MPE5WarmRebootHotkeyHeld)
      {
         // The tiny BIOS executes from its writable F000 segment and may
         // patch itself while DOS runs. Reload the pristine cartridge copy
         // before rebuilding the guest, just as a physical reset restores
         // ROM contents.
         uint8_t Header[MPE5HeaderBytes];
         if (!MPE4Read(nullptr, MPE5Root, Header, sizeof(Header)) ||
             memcmp(Header, "M5D1", 4) || Header[4] != MPE5Protocol ||
             Header[5] != sizeof(Header))
         { MPE5Error = 0x40u + (uint8_t)mpe5::CoreStop::ReadFailure; return true; }
         const uint32_t BiosBytes = MPE5Read32(Header + 8);
         if (!MPE5Bios || !BiosBytes || BiosBytes > MPE5BiosMaxBytes ||
             BiosBytes != MPE5BiosBytes ||
             !MPE4Read(nullptr, MPE5Root + sizeof(Header), MPE5Bios,
                       (uint16_t)BiosBytes) ||
             MHSNativeCRC32(MPE5Bios, BiosBytes) != MPE5Read32(Header + 12))
         { MPE5Error = 0x40u + (uint8_t)mpe5::CoreStop::ReadFailure; return true; }
         MPE5Keyboard.clear();
         MPE5Speaker = {}; MPE5Sid.reset(); MPE5SpeakerRevision = 0;
         MPE5DisplayVideo.reset(); MPE5Text.reset();
         MPE5Graphics = MPE5DisplayComplete = MPE5InputActivationPending = false;
         MPE5SharpGraphics = MPE5SharpHotkeyHeld = false;
         MPE5DisplayHires = true; MPE5DisplayBackground = 0;
         if (!mpe5::coreRestart()) return false;
         mpe5::coreSetVideoObserver({nullptr, MPE5VideoWrite});
         MPE5FirstFrame = true;
         MPE5BootScreenPending = true; MPE5BootScreenSequence = 0;
         MPE5BootHoldFrames = 48u; MPE5BootBeepFrames = 10u;
         MPE5Speaker.write(0x43u, 0xb6u);
         MPE5Speaker.write(0x42u, 0xa9u);
         MPE5Speaker.write(0x42u, 0x04u);
         MPE5Speaker.write(0x61u, 0x03u);
      }
      MPE5WarmRebootHotkeyHeld = true;
      return true;
   }
   if (Snapshot) MPE5WarmRebootHotkeyHeld = false;
   // Ctrl+Commodore+F7 is a display-only shortcut. All ordinary F7,
   // Ctrl+F7 and Alt+F7 input continues to reach the guest unchanged.
   const bool SharpChord = Snapshot && Key.scan == 0x41u &&
      (MPE5InputFlags & 7u) == 6u;
   // Keep F7 consumed until its own release, even if either modifier is
   // released first. Otherwise the tail of a shortcut becomes a guest key.
   const bool ConsumeF7 = Snapshot && Key.scan == 0x41u &&
      (SharpChord || MPE5SharpHotkeyHeld);
   if (ConsumeF7) { Key.ascii = 0; Key.scan = 0; }
   const bool Accepted = Snapshot ?
      MPE5Keyboard.acceptSnapshot(Key.ascii, Key.scan, MPE5InputFlags & 7u,
         MPE5InputJoy, (MPE5InputFlags & 8u) != 0) : MPE5Keyboard.push(Key);
   if (Accepted)
   {
      if (SharpChord && !MPE5SharpHotkeyHeld) MPE5SharpGraphics = !MPE5SharpGraphics;
      MPE5SharpHotkeyHeld = ConsumeF7;
   }
   return Accepted;
}

static FLASHMEM bool MPE5RunSlice()
{
   if (MPE5Error >= 0x40u) return false;
   // The first screen is real preboot state. Its final matching ACK proves
   // the C64 has received it before any guest instruction or disk read runs.
   if (MPE5BootScreenPending) return true;
   if (MPE5InputPending)
   {
      if (MPE5AcceptInput())
      {
         MPE5InputPending = false;
         // A warm reboot recreates the preboot page and hold. Do not execute
         // its first guest slice until the C64 has acknowledged that page.
         if (MPE5BootScreenPending) return true;
      }
   }
   MPE5SliceIo = 0;
   // A previously full keyboard queue must be allowed to drain. Only a
   // newly latched snapshot interrupts this slice before its time budget.
   MPE5SliceYieldForInput = !MPE5InputPending;
   if (mpe5::coreRun(MPE5InstructionSlice)) return true;
   MPE5Error = 0x40u + (uint8_t)mpe5::coreDiagnostic().reason;
   return false;
}

// The pending wire packet is a copy; guest memory and console buffers are
// private. Keep the CPU moving while the C64 displays/ACKs that copy. A
// failure is held here until ACK, preserving the immutable packet contract.
static inline void MPE5RequestQuietRead()
{
   // The bus ISR requests a retry without changing the packet or claiming
   // that a foreground guest instruction has already completed.
   MPE5QuietRead = true;
}

static inline void MPE5ResumeAfterACK()
{
   if (MPE5BootScreenPending && MPE5BootScreenSequence &&
       MPE3Title.PendingType == MPE3TitleSID &&
       MPE3Title.Sequence == MPE5BootScreenSequence)
   {
      if (MPE5BootBeepFrames && !--MPE5BootBeepFrames)
         MPE5Speaker.write(0x61u, 0x00u);
      if (MPE5BootHoldFrames) --MPE5BootHoldFrames;
      if (!MPE5BootHoldFrames)
      {
         MPE5BootScreenPending = false;
         // The held POST was one complete text generation. Start a fresh
         // traversal for guest boot output so every cleared or scrolled cell
         // reaches the C64 after both cold and warm starts.
         MPE5Text.reset();
      }
   }
   MPE5QuietRead = false;
   // Do not erase a typed runtime error when its packet is acknowledged.
   if (MPE3TitleMailbox[MPE3TitleRegStatus] ==
       (MPE3TitleRunning | MPE5QuietReadStatus))
      MPE3TitleMailbox[MPE3TitleRegStatus] = MPE3TitleRunning;
}

static FLASHMEM void MPE5PumpPending()
{
   if (!MPE5QuietRead && MPE5Active && !MPE5FirstFrame && !MPE5Error)
      MPE5RunSlice();
   if (MPE5QuietRead &&
       MPE3TitleMailbox[MPE3TitleRegStatus] < MPE3TitleError)
   {
      // Ready is published in foreground only, after a slice interrupted by
      // command 4 has returned. The receiver may now retry the same CRC-
      // protected packet without concurrent guest-memory/flash traffic.
      MPE3TitleMemoryBarrier();
      MPE3TitleMailbox[MPE3TitleRegStatus] =
         MPE3TitleRunning | MPE5QuietReadStatus;
   }
}

static FLASHMEM void MPE5NextPacket()
{
   if (MPE5Error >= 0x40u) { MPE5FailRuntime(); return; }
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
   if (MPE5InputActivationPending)
   {
      // The existing native terminal enables its C64 keyboard sampler from a
      // validated gameplay-style frame end. A silent SID register set arms
      // that path after the complete initial text frame is visible.
      MPE5InputActivationPending = false;
      MPE5PublishFrameEnd();
      return;
   }
   // The C64 receives speaker snapshots at display packet boundaries. The
   // guest must continue through short PIT/gate changes while the previous
   // packet awaits ACK, or software-generated sounds throttle the whole PC
   // to the cartridge's packet rate. The pending wire copy stays immutable.
   if (!MPE5RunSlice())
   { MPE5FailRuntime(); return; }
   // Finish a replacement using one display policy even if the guest keeps
   // changing its palette/start registers. Adopt the newest policy after
   // its frame end, so rapid changes cannot restart/hide the sweep forever.
   bool Changed = false;
   if (!MPE5Graphics || MPE5DisplayComplete)
   {
      // Apply the requested output policy only between complete sweeps;
      // a key arriving during a pending packet cannot change its format.
      Changed = MPE5DisplayVideo.setSharp(MPE5SharpGraphics);
      Changed = MPE5DisplayVideo.setState(mpe5::coreVideoState()) || Changed;
   }
   const bool Graphics = MPE5DisplayVideo.graphics();
   if (Graphics != MPE5Graphics || (Graphics && Changed))
   {
      // Scrolling changes the CRTC origin and repaints every cell, but keeps
      // the same bitmap format. Hide only an actual display-format change;
      // hiding every scroll repaint leaves a moving game black for most of
      // its transport time.
      const bool Replace = Graphics != MPE5Graphics ||
         (Graphics && MPE5DisplayHires != MPE5DisplayVideo.hires());
      MPE5Graphics = Graphics;
      MPE5DisplayHires = !Graphics || MPE5DisplayVideo.hires();
      MPE5DisplayBackground = Graphics ? MPE5DisplayVideo.background() : 0;
      MPE5DisplayComplete = false;
      MPE5FirstFrame = MPE5FirstFrame || Replace;
      if (!Graphics) MPE5Text.reset();
   }
   const bool SoundPending = MPE5DisplayComplete &&
      MPE5Speaker.revision() != MPE5SpeakerRevision;
   if (Graphics)
   {
      const bool Initial = !MPE5DisplayVideo.initialComplete();
      const uint16_t Count = MPE5DisplayVideo.changes(
         MPE3TitlePacket + MPE3TitlePacketHeaderBytes, MPE3TitleCellsPerPacket);
      if (!Count) { MPE5PublishFrameEnd(); return; }
      uint8_t Flags = MPE3TitleCellModeValid |
         (MPE5DisplayHires ? MPE3TitleCellHires : 0) |
         (MPE5FirstFrame ? MPE3TitleCellReplace : 0);
      MPE5FirstFrame = false;
      if (Initial && MPE5DisplayVideo.initialComplete())
      { Flags |= 2; MPE5InputActivationPending = true; }
      // Deliver one dirty batch before the newest sound snapshot. Multiple
      // speaker changes during transport coalesce; neither graphics nor CPU
      // execution waits for every edge of a software-generated sound.
      if (SoundPending) MPE5InputActivationPending = true;
      MPE3TitlePublish(MPE3TitleCELL, Flags, Count * MPE3TitleCellBytes);
      return;
   }
   uint8_t Dirty[MPE3TitleCellsPerPacket * sizeof(mpe5::TextPair)];
   bool InitialFrame = !MPE5Text.initialComplete();
   const mpe5::ConsoleCursor Cursor = mpe5::coreConsoleCursor();
   const bool CursorOn = Cursor.visible && ((millis() / 500u) & 1u) == 0u;
   uint16_t Count = MPE5Text.changes(MPE5PublishedShadow, Dirty,
                                     MPE3TitleCellsPerPacket,
                                     Cursor.position, CursorOn);
   if (!Count)
   {
      // An idle prompt still needs a packet: the C64 samples its keyboard
      // between packets and otherwise waits until its transport timeout.
      MPE5PublishFrameEnd();
      return;
   }
   for (uint16_t Index = 0; Index < Count; ++Index)
   {
      const uint8_t *Cell = Dirty + Index * sizeof(mpe5::TextPair);
      uint8_t *Record = MPE3TitlePacket + MPE3TitlePacketHeaderBytes +
         Index * MPE3TitleCellBytes;
      Record[0] = Cell[0]; Record[1] = Cell[1];
      MPE5GlyphPair(Cell[2], Cell[3], Cell[4], Record + 2);
      Record[10] = 0x10u; // white foreground over a black bitmap background
       Record[11] = 1u;
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
   if (SoundPending) MPE5InputActivationPending = true;
   MPE3TitlePublish(MPE3TitleCELL, Flags, Count * MPE3TitleCellBytes);
}

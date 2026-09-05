// Standalone DOSVM: core, peripherals, presentation and input live in this module.
#include "../abi/vm_abi.h"
#include <cstring>
#include <cstdio>
#include <new>
#define FLASHMEM
#define PROGMEM
#define MPE5_CODE
#define MPE5_HOT_CODE
static const VmHost *ModuleHost;
#include "files.h"
#include "../../engine/native-dos/mpe5_platform.cpp"
#include "../../engine/native-dos/mpe5_speaker.cpp"
#include "../../engine/native-dos/mpe5_video.cpp"
#include "../../engine/native-dos/mpe5_8086tiny.cpp"
#include "../../engine/native-dos/mpe5_redirector.cpp"
#include "../../engine/native-dos/mpe5_folder_fs.h"
#include "../../engine/native-dos/mpe5_font8x8.h"
#include "../../engine/native-dos/mpe5_font4x8.h"
#include "memory.h"
#include "video.h"
static uint64_t ClockMicros;static uint32_t ClockPrevious;static bool ClockStarted;
static uint32_t millis(){
 const uint32_t now=ModuleHost->micros_now();ClockMicros+=ClockStarted?uint32_t(now-ClockPrevious):now;
 ClockStarted=true;ClockPrevious=now;return uint32_t(ClockMicros/1000);
}
enum {MPE3TitlePacketHeaderBytes=8,MPE3TitleCellsPerPacket=19,MPE3TitleCellBytes=12,
 MPE3TitleCELL=1,MPE3TitleSID=2,MPE3TitleCellHires=4,MPE3TitleCellModeValid=8,MPE3TitleCellReplace=16,
 MPE3TitleRegStatus=0xf5,MPE3TitleRunning=2,MPE3TitleError=0xe0};
static uint8_t MPE3TitlePacket[240],MPE3TitleMailbox[256];
static struct {uint8_t PendingType,Sequence;bool Pending;} MPE3Title;
static VmPacket ModulePacket;
static bool ModulePacketPending;
static void MPE3TitleMemoryBarrier(){}
static void MPE3TitlePublish(uint8_t type,uint8_t flags,uint8_t count){
 ModulePacket={};ModulePacket.type=type;ModulePacket.flags=flags;ModulePacket.length=count;
 memcpy(ModulePacket.payload,MPE3TitlePacket+8,count);MPE3Title.PendingType=type;
 MPE3Title.Sequence=MPE3Title.Sequence==255?1:MPE3Title.Sequence+1;
 MPE3Title.Pending=ModulePacketPending=true;
}
static void MPE3TitleFail(uint8_t error){ModuleHost->fail(error,mpe5::coreDiagnostic().address);}
static constexpr uint8_t MPE5HeaderBytes=16,MPE5Protocol=1,MPE5QuietReadStatus=0x10;
static constexpr uint32_t MPE5BiosMaxBytes=0xff00,MPE5InstructionSlice=25000;
static bool MPE5Active,MPE5InputPending,MPE5QuietRead,MPE5FirstFrame,MPE5TransportCanary;
static bool MPE5BootScreenPending,MPE5Graphics,MPE5DisplayHires,MPE5DisplayComplete;
static uint8_t MPE5BootScreenSequence,MPE5BootHoldFrames,MPE5BootBeepFrames;
static bool MPE5SharpGraphics,MPE5SharpHotkeyHeld,MPE5WarmRebootHotkeyHeld,MPE5InputActivationPending;
static uint8_t MPE5DisplayBackground,MPE5InputKey,MPE5InputScan,MPE5InputFlags,MPE5InputJoy,MPE5Error;
static uint32_t MPE5SpeakerRevision,MPE5TandyRevision,MPE5Root,MPE5SliceIo,MPE5DiskSectors;
static bool MPE5SliceYieldForInput;
static FsFile MPE5DiskFile;
static mpe5::Redirector *MPE5Redirector;
static mpe5::FolderFilesystem *MPE5Folder;
static DosMemory *Memory;
static uint8_t *MPE5Bios,*MPE5PublishedShadow,*MPE5PublishedViewport;
static uint16_t MPE5BiosBytes;
static mpe5::Keyboard MPE5Keyboard;
static mpe5::PcSpeaker MPE5Speaker;
static mpe5::TandyPsg MPE5Tandy;
static mpe5::CgaText80 MPE5Text;
static mpe5::CgaVideo MPE5DisplayVideo;
// Default/F7 retain the proven converter AND CELL transport. Only F5 lends
// this same workspace to the firmware; never pay for two video arenas.
static void *MPE5VideoWorkspace;
static bool MPE5EnhancedWanted,MPE5EnhancedActive,MPE5EnhancedPending;
static uint8_t MPE5VideoHotkeyHeld;
static uint32_t MPE5EnhancedNext;
static DosRaster MPE5Raster;
static VmIndexedRasterFrame MPE5EnhancedFrame;
static_assert(mpe5::CgaVideo::WorkspaceBytes>=mpe_video::DeltaWorkspaceBytes,"video overlay");
static mpe5::SpeakerSid MPE5Sid;
static uint32_t BiosCrc;
static uint32_t MHSNativeCRC32(const void *p,uint32_t n){return vm_crc32(p,n);}
static uint32_t MPE5Read32(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static bool MPE4Read(void *,uint32_t offset,uint8_t *out,uint16_t count){
 if(offset==0&&count==16){memset(out,0,16);memcpy(out,"M5D1",4);out[4]=1;out[5]=16;
  uint32_t n=MPE5BiosBytes;memcpy(out+8,&n,4);memcpy(out+12,&BiosCrc,4);return true;}
 if(offset<16)return false;auto f=SD.sdfs.open("/DOSVM/bios.bin",O_RDONLY);
 bool ok=f&&f.seekSet(offset-16)&&f.read(out,count)==count;f.close();return ok;
}
static bool MPE5ReadSector(void *p,uint32_t lba,uint8_t out[512]){
 ++MPE5SliceIo;auto f=static_cast<FsFile *>(p);uint64_t offset=uint64_t(lba)*512;
 return f&&offset<=f->fileSize()&&512<=f->fileSize()-offset&&f->seekSet(uint32_t(offset))&&f->read(out,512)==512;
}
static bool MPE5WriteSector(void *p,uint32_t lba,const uint8_t in[512]){
 ++MPE5SliceIo;auto f=static_cast<FsFile *>(p);uint64_t offset=uint64_t(lba)*512;
 return f&&offset<=f->fileSize()&&512<=f->fileSize()-offset&&f->seekSet(uint32_t(offset))&&f->write(in,512)==512&&f->sync();
}
static void MPE5FolderIo(void *){++MPE5SliceIo;}
static bool MPE5MemoryReset(void *){return Memory->reset();}
static bool MPE5MemoryRead(void *,uint32_t a,uint8_t *p,uint32_t n){return Memory->transfer(a,p,n,false);}
static bool MPE5MemoryWrite(void *,uint32_t a,const uint8_t *p,uint32_t n){return Memory->transfer(a,const_cast<uint8_t *>(p),n,true);}
static uint8_t YieldDivider;
static bool MPE5ShouldYield(void *){
 // The core asks after every instruction. Poll generic host events once per
 // 64 instructions instead of paying a clock/callback cost on every opcode.
 return MPE5SliceIo>=4||((++YieldDivider&63)==0&&ModuleHost->should_yield());
}
static void MPE5VideoWrite(void *,uint16_t offset,const uint8_t *p,uint16_t n){
 if(!MPE5EnhancedActive)MPE5DisplayVideo.write(offset,p,n);
}
static bool MPE5EnhancedSupported(){
 const uint32_t required=VM_SERVICE_INDEXED_VIDEO|VM_SERVICE_INDEXED_RASTER;
 return ModuleHost->bytes>=sizeof(VmHost)&&(ModuleHost->services&required)==required&&
   ModuleHost->video_configure&&ModuleHost->video_indexed;
}
static void MPE5EnhancedPoll(){
 if(!MPE5EnhancedPending)return;
 // The firmware owns a frozen converted frame after consumption. Only the
 // live native VRAM may continue changing while that generation awaits ACK.
 if(!MPE5EnhancedFrame.source_consumed)
  MPE5Raster.capture(mpe5::coreVideoState(),Memory->video,MPE5EnhancedFrame);
 const auto result=ModuleHost->video_indexed(&MPE5EnhancedFrame.frame);
 if(result==VmVideoResult::Failed){ModuleHost->fail(0x27,2);return;}
 if(result==VmVideoResult::Transferred){
  MPE5EnhancedPending=false;MPE5DisplayHires=true;
  MPE5DisplayBackground=MPE5EnhancedFrame.resolved_background;
  MPE5DisplayComplete=true;MPE5EnhancedNext=millis()+16;
 }
}
// Only called by module_packet, never from input or while a module packet is
// pending. The shared generation must finish before reclaiming its workspace.
static void MPE5PublishFrameEnd();
static bool MPE5EnhancedPacket(){
 const bool wanted=MPE5EnhancedWanted&&DosRaster::graphics(mpe5::coreVideoState().mode)&&!MPE5BootScreenPending;
 if(MPE5EnhancedActive&&!wanted){
  if(MPE5EnhancedPending)return true;
  MPE5EnhancedActive=false;MPE5DisplayVideo.reset();
  MPE5DisplayVideo.write(0,Memory->video,sizeof Memory->video);
  MPE5Text.reset();MPE5Graphics=false;MPE5DisplayComplete=false;
  MPE5FirstFrame=true;MPE5InputActivationPending=false;
 }
 if(wanted&&!MPE5EnhancedActive&&MPE5EnhancedSupported()){
  VmIndexedVideoSetup setup{sizeof(setup),MPE5VideoWorkspace,mpe_video::DeltaWorkspaceBytes,2,4,VM_INDEXED_SEPARATE_SELECTORS};
  if(!ModuleHost->video_configure(&setup))return false;
  MPE5EnhancedActive=true;MPE5EnhancedNext=0;MPE5FirstFrame=false;
  MPE5InputActivationPending=false;
 }
 if(!MPE5EnhancedActive)return false;
 if(!MPE5EnhancedPending&&int32_t(millis()-MPE5EnhancedNext)>=0){
  const auto generation=MPE5EnhancedFrame.frame.generation+1;
  MPE5EnhancedFrame={};MPE5EnhancedFrame.frame.bytes=sizeof(MPE5EnhancedFrame);
  MPE5EnhancedFrame.frame.generation=generation;
  MPE5EnhancedFrame.read_pixel=DosRaster::pixel;MPE5EnhancedFrame.context=&MPE5Raster;
  MPE5EnhancedPending=true;MPE5EnhancedPoll();
 }
 if(!MPE5EnhancedPending)MPE5PublishFrameEnd();
 return true;
}
static bool MPE5RedirectorService(void *p,uint8_t op,mpe5::RedirectorRegisters &r){return static_cast<mpe5::Redirector *>(p)->service(op,r);}
static void MPE5RedirectorReset(void *p){static_cast<mpe5::Redirector *>(p)->reset();}
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

static FLASHMEM void MPE5PublishFrameEnd()
{
   uint8_t *Payload = MPE3TitlePacket + MPE3TitlePacketHeaderBytes;
   // MinimalBoot has no full-menu IO1 video-standard register. Use NTSC
   // tuning for this test kit; the adapter also supports explicit PAL tuning.
   MPE5Sid.render(MPE5Speaker, &MPE5Tandy, Payload, mpe5::SpeakerSid::NtscClockHz);
   MPE5SpeakerRevision = MPE5Speaker.revision();
   MPE5TandyRevision = MPE5Tandy.revision();
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
         MPE5Speaker = {}; MPE5Tandy = {}; MPE5Sid.reset();
         MPE5SpeakerRevision = MPE5TandyRevision = 0;
         if(!MPE5EnhancedActive)MPE5DisplayVideo.reset();
         MPE5EnhancedWanted=false;MPE5VideoHotkeyHeld=0;MPE5Text.reset();
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
   // Keep DOS snapshots separate from firmware's NES selectors. F5 opts in;
   // F1 restores the original renderer; F7 keeps the original Sharp toggle
   // (or selects Sharp when leaving F5). Ordinary function keys remain DOS's.
   const uint8_t Scan=Key.scan;
   const bool VideoChord=Snapshot&&(Scan==0x3b||Scan==0x3f||Scan==0x41)&&
      (MPE5InputFlags&7u)==6u;
   const bool ConsumeVideo=Snapshot&&(VideoChord||(Scan&&Scan==MPE5VideoHotkeyHeld));
   if(ConsumeVideo){Key.ascii=0;Key.scan=0;}
   const bool Accepted = Snapshot ?
      MPE5Keyboard.acceptSnapshot(Key.ascii, Key.scan, MPE5InputFlags & 7u,
         MPE5InputJoy, (MPE5InputFlags & 8u) != 0) : MPE5Keyboard.push(Key);
   if (Accepted)
   {
      if(VideoChord&&Scan!=MPE5VideoHotkeyHeld){
         if(Scan==0x3f){if(MPE5EnhancedSupported())MPE5EnhancedWanted=true;}
         else{
            MPE5SharpGraphics=Scan==0x41&&(MPE5EnhancedWanted||!MPE5SharpGraphics);
            MPE5EnhancedWanted=false;
         }
      }
      if(Snapshot)MPE5VideoHotkeyHeld=ConsumeVideo?Scan:0;
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
   // Handle a requested exit before any legacy code touches the overlaid
   // workspace. Pending shared frames are polled by pump through resume ACK.
   if(MPE5EnhancedActive&&MPE5EnhancedPacket())return;
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
   if(MPE5EnhancedPacket())return;
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
      (MPE5Speaker.revision() != MPE5SpeakerRevision ||
       MPE5Tandy.revision() != MPE5TandyRevision);
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

static uint32_t WorkspaceUsed;
static bool start(){
 uintptr_t cursor=uintptr_t(ModuleHost->workspace),limit=cursor+ModuleHost->workspace_bytes;
 auto take=[&](size_t n)->void*{cursor=(cursor+31)&~uintptr_t(31);if(cursor>limit||n>limit-cursor)return nullptr;void *p=(void *)cursor;cursor+=n;return p;};
 auto fixed=(uint8_t *)take(65536),decode=(uint8_t *)take(5120),console=(uint8_t *)take(6000);
 auto video=take(mpe5::CgaVideo::WorkspaceBytes),mem=take(sizeof(DosMemory));
 auto folder=take(sizeof(mpe5::FolderFilesystem)),redirector=take(sizeof(mpe5::Redirector));
 if(!fixed||!decode||!console||!video||!mem||!folder||!redirector){ModuleHost->fail(0x21,ModuleHost->workspace_bytes);return false;}
 WorkspaceUsed=uint32_t(cursor-uintptr_t(ModuleHost->workspace));
 Memory=new(mem) DosMemory{};Memory->guest=ModuleHost->guest_ram;
 auto bios=SD.sdfs.open("/DOSVM/bios.bin",O_RDONLY);
 if(!bios||!bios.fileSize()||bios.fileSize()>MPE5BiosMaxBytes){bios.close();ModuleHost->fail(0x22,0);return false;}
 MPE5Bios=fixed+256;MPE5BiosBytes=bios.fileSize();
 bool read=bios.read(MPE5Bios,MPE5BiosBytes)==MPE5BiosBytes;bios.close();
 if(!read){ModuleHost->fail(0x22,1);return false;}
 BiosCrc=vm_crc32(MPE5Bios,MPE5BiosBytes);
 MPE5DiskFile=SD.sdfs.open(ModuleHost->content_path[0]?ModuleHost->content_path:"/DOSVM/DOSVM.IMG",O_RDWR);
 if(!MPE5DiskFile||!MPE5DiskFile.fileSize()||(MPE5DiskFile.fileSize()%512)){ModuleHost->fail(0x23,0);return false;}
 MPE5DiskSectors=MPE5DiskFile.fileSize()/512;
 MPE5Folder=new(folder) mpe5::FolderFilesystem(MPE5FolderIo,nullptr);
 if(!MPE5Folder->begin()){ModuleHost->fail(0x24,0);return false;}
 MPE5Redirector=new(redirector) mpe5::Redirector{};
 MPE5Redirector->configure(mpe5::coreRedirectorMemory(),MPE5Folder->host());
 MPE5VideoWorkspace=video;
 if(!MPE5DisplayVideo.start(video,mpe5::CgaVideo::WorkspaceBytes))return false;
 mpe5::CoreHost h{};
 h.conventionalRam=ModuleHost->guest_ram;h.conventionalRamBytes=ModuleHost->guest_ram_bytes;
 h.fixedF000=fixed;h.fixedF000Bytes=65536;h.decodeTable=decode;h.decodeTableBytes=5120;
 h.consoleShadow=console;h.consoleViewport=console+4000;h.bios=MPE5Bios;h.biosBytes=MPE5BiosBytes;
 h.memory={nullptr,MPE5MemoryReset,MPE5MemoryRead,MPE5MemoryWrite,MPE5ShouldYield};
 h.drive={&MPE5DiskFile,MPE5ReadSector,MPE5DiskSectors,MPE5WriteSector};
 h.keyboard=&MPE5Keyboard;h.speaker=&MPE5Speaker;h.tandy=&MPE5Tandy;h.milliseconds=millis;
 h.redirectorContext=MPE5Redirector;h.redirector=MPE5RedirectorService;h.redirectorReset=MPE5RedirectorReset;
 if(!mpe5::coreStart(h)){ModuleHost->fail(0x25,uint32_t(mpe5::coreDiagnostic().reason));return false;}
 mpe5::coreSetVideoObserver({nullptr,MPE5VideoWrite});
 MPE5PublishedShadow=h.consoleShadow;MPE5PublishedViewport=h.consoleViewport;
 MPE5FirstFrame=MPE5TransportCanary=MPE5BootScreenPending=true;MPE5DisplayHires=true;
 MPE5BootHoldFrames=48;MPE5BootBeepFrames=10;
 MPE5Speaker.write(0x43,0xb6);MPE5Speaker.write(0x42,0xa9);MPE5Speaker.write(0x42,4);MPE5Speaker.write(0x61,3);
 MPE5Active=true;return true;
}
static VmInput InputQueue[32];static uint8_t InputHead,InputTail;
static void module_input(const VmInput *in){
 if((in->protocol&0x80)?(in->protocol&~0x8f):(in->protocol!=1||in->overflow))return;
 if(in->overflow&~31)return;
 uint8_t next=(InputTail+1)&31;if(next==InputHead){ModuleHost->fail(0x26,0);return;}
 InputQueue[InputTail]=*in;InputTail=next;
}
static void module_pump(){
 if(!MPE5InputPending&&InputHead!=InputTail){auto in=InputQueue[InputHead];InputHead=(InputHead+1)&31;
  MPE5InputKey=in.buttons;MPE5InputScan=in.display;MPE5InputJoy=in.overflow;MPE5InputFlags=in.protocol;MPE5InputPending=true;}
 MPE5PumpPending();
 MPE5EnhancedPoll();
}
static bool module_packet(VmPacket *out){
 if(ModulePacketPending)return false;MPE5NextPacket();if(!ModulePacketPending)return false;*out=ModulePacket;return true;
}
static void module_ack(){MPE5ResumeAfterACK();ModulePacketPending=MPE3Title.Pending=false;}
static const VmModule Module{VM_ABI,sizeof(VmModule),module_input,module_pump,module_packet,module_ack};
extern "C" __attribute__((section(".entry"),used)) const VmModule *vm_entry(const VmHost *host){
 if(!host||host->abi!=VM_ABI||host->bytes<VM_HOST_BASE_BYTES||(host->services&VM_SERVICES)!=VM_SERVICES||
  !host->guest_ram||host->guest_ram_bytes!=VM_RAM_BYTES)return nullptr;
 ModuleHost=host;return start()?&Module:nullptr;
}
#if defined(__arm__)
extern "C" void *_sbrk(ptrdiff_t){return reinterpret_cast<void *>(-1);}
extern "C" int _write(int,const void *,int){return -1;}
extern "C" int _read(int,void *,int){return -1;}
extern "C" int _close(int){return -1;}
extern "C" int _fstat(int,void *){return -1;}
extern "C" int _isatty(int){return 0;}
extern "C" int _lseek(int,int,int){return -1;}
extern "C" int _getpid(){return 1;}
extern "C" int _kill(int,int){return -1;}
extern "C" void _exit(int){for(;;){}}
#endif

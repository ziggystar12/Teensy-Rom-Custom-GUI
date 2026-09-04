// Native gameplay shares the proven M3 packet publisher and its retired intro
// arena. Included only in the native bank-58 service; no emulated CPU or DMA.
#include <new>
#ifndef MPE4_CART_CODE
#define MPE4_CART_CODE FLASHMEM
#define MPE4_CART_RODATA PROGMEM
#endif
#include "../MPE4Cartridge.h"
#define MPE4_CODE FLASHMEM
#define MPE4_RODATA PROGMEM
#include "mpe4_package.cpp"
#include "mpe4_game.cpp"
#include "mpe4_render.cpp"
#include "mpe4_session.cpp"

static mpe4::Session *MPE4Game;
static uint32_t MPE4Root;
static volatile bool MPE4Active;
static uint8_t MPE4StartError;
static MHSNativeArenaView MPE4ArenaView;
static_assert(sizeof(mpe4::Session)<=MHSNativeArenaCapacity,
   "native gameplay must fit the shared native arena");
static_assert(alignof(mpe4::Session)<=MHSNativeArenaAlignment,
   "native gameplay alignment exceeds the shared native arena");

// The C64 owns one immutable mailbox event until the sequence is ACKed.  The
// Phi2 ISR therefore ACKs only after the event is retained here.  Keyboard
// edges need ordering, joystick directions need only the newest held state,
// and pointer motion can be coalesced while button transitions stay ordered.
// Monotonic producer/consumer cursors make each queue single-writer on both
// sides without ever masking the bus interrupt.
static constexpr uint8_t MPE4KeyboardSlots=16,MPE4PointerEdgeSlots=8;
static_assert(!(MPE4KeyboardSlots&(MPE4KeyboardSlots-1u))&&
   !(MPE4PointerEdgeSlots&(MPE4PointerEdgeSlots-1u)),"input queues must be powers of two");
static volatile uint8_t MPE4KeyboardWrite,MPE4KeyboardRead;
static volatile uint8_t MPE4KeyboardKey[MPE4KeyboardSlots],MPE4KeyboardScan[MPE4KeyboardSlots];
static volatile uint8_t MPE4JoyState,MPE4JoyRevision;
// Naturally aligned halfword loads/stores are atomic on the Teensy 4.x
// Cortex-M7. Sixteen bits also cannot alias within the terminal's bounded
// maximum of 56 input scans during one complete sprite frame transfer.
alignas(2) static volatile uint16_t MPE4JoyFireWrite;
static uint16_t MPE4JoyFireRead;
static volatile uint8_t MPE4PointerX,MPE4PointerY,MPE4PointerButtons;
alignas(2) static volatile uint16_t MPE4PointerRevision;
static uint16_t MPE4PointerReadRevision;
static volatile uint8_t MPE4PointerEdgeWrite,MPE4PointerEdgeRead;
static volatile uint8_t MPE4PointerEdgeX[MPE4PointerEdgeSlots],MPE4PointerEdgeY[MPE4PointerEdgeSlots];
static volatile uint8_t MPE4PointerEdgeButtons[MPE4PointerEdgeSlots];
alignas(2) static volatile uint16_t MPE4PointerEdgeRevision[MPE4PointerEdgeSlots];

static FLASHMEM void MPE4ResetInput()
{
   MPE4KeyboardWrite=MPE4KeyboardRead=0;
   MPE4JoyState=MPE4JoyRevision=MPE4JoyFireWrite=MPE4JoyFireRead=0;
   MPE4PointerX=MPE4PointerY=MPE4PointerButtons=MPE4PointerRevision=MPE4PointerReadRevision=0;
   MPE4PointerEdgeWrite=MPE4PointerEdgeRead=0;
}

static FLASHMEM bool MPE4Read(void *,uint32_t Raw,uint8_t *Data,uint16_t Count)
{
   if(!MPE3TitleSelected()||!Data)return false;
   uint32_t Physical;uint16_t Part;
   if(!mpe4cart::span(Raw,Count,Physical,Part))return false;
   while(Count)
   {
      uint8_t Error=0;
      if(!mpe4cart::span(Raw,Count,Physical,Part)||
         !AGIPictureReadRawBytes(Physical,Data,Part,&Error))return false;
      Raw+=Part;Data+=Part;Count-=Part;
   }
   return true;
}

// Validate into the unpublished frame, never over live game state. M4G2 binds
// save data to the stable package identity and compatibility epoch, not a
// cartridge build checksum. The compact 8.3 leaf preserves SD/FAT support.
static constexpr uint8_t MPE4SaveSlots=12;
static FLASHMEM bool MPE4ReadSave(const char *Path,const char *Identity,uint16_t Epoch,uint8_t Slot,size_t Bytes)
{
   if(!Identity||!Slot||Slot>MPE4SaveSlots||Bytes!=sizeof(mpe4::State)||Bytes>sizeof(MPE4Game->next))return false;
   File Input=SD.open(Path,FILE_READ);
   if(!Input)return false;
   uint8_t Header[32];
   bool Valid=Input.read(Header,sizeof(Header))==sizeof(Header);
   const uint32_t Stored=Valid?MHSNativeRead32(Header+20):0;
   Valid=Valid && Stored==Bytes && Input.size()==Stored+sizeof(Header);
   Valid=Valid && !memcmp(Header,"M4SV",4) && MHSNativeRead32(Header+4)==2 &&
      !memcmp(Header+8,Identity,6) && (Header[14]|uint16_t(Header[15])<<8)==Epoch && Header[16]==Slot &&
      !Header[17]&&!Header[18]&&!Header[19] &&
      MHSNativeRead32(Header+28)==MHSNativeCRC32(Header,28);
   if(Valid)Valid=Input.read(MPE4Game->next,Stored)==(int)Stored &&
      MHSNativeCRC32(MPE4Game->next,Stored)==MHSNativeRead32(Header+24);
   Input.close();return Valid;
}
static FLASHMEM void MPE4Write32(uint8_t *p,uint32_t v)
{ for(uint8_t n=0;n<4;n++)p[n]=(uint8_t)(v>>(n*8)); }
static const char MPE4SaveDirectory[] PROGMEM="/SAVES";
static FLASHMEM void MPE4SavePath(char *Path,const char *Identity,uint8_t Slot,char a,char b,char c)
{
   memcpy(Path,MPE4SaveDirectory,6);
   Path[6]='/';memcpy(Path+7,Identity,6);Path[13]='0'+Slot/10;Path[14]='0'+Slot%10;
   Path[15]='.';Path[16]=a;Path[17]=b;Path[18]=c;Path[19]=0;
}
static FLASHMEM bool MPE4Save(void *,const char *Identity,uint16_t Epoch,uint8_t Slot,const mpe4::State *State,size_t Bytes)
{
   if(!Identity||!Slot||Slot>MPE4SaveSlots||Bytes!=sizeof(mpe4::State))return false;
   // An existing file named SAVES is an error, never a reason to write root.
   if(!SD.exists(MPE4SaveDirectory)&&!SD.mkdir(MPE4SaveDirectory))return false;
   File Directory=SD.open(MPE4SaveDirectory,FILE_READ);
   bool IsDirectory=Directory&&Directory.isDirectory();Directory.close();
   if(!IsDirectory)return false;
   char Temp[20],Path[20],Backup[20];
   MPE4SavePath(Temp,Identity,Slot,'t','m','p');MPE4SavePath(Path,Identity,Slot,'s','a','v');
   MPE4SavePath(Backup,Identity,Slot,'b','a','k');
   uint8_t Header[32]={};memcpy(Header,"M4SV",4);MPE4Write32(Header+4,2);
   memcpy(Header+8,Identity,6);Header[14]=Epoch;Header[15]=Epoch>>8;Header[16]=Slot;
   MPE4Write32(Header+20,Bytes);MPE4Write32(Header+24,MHSNativeCRC32((const uint8_t *)State,Bytes));
   MPE4Write32(Header+28,MHSNativeCRC32(Header,28));
   if(SD.exists(Temp)&&!SD.remove(Temp))return false;
   File Output=SD.open(Temp,FILE_WRITE);
   if(!Output)return false;
   bool Valid=Output.write(Header,sizeof(Header))==sizeof(Header) &&
      Output.write((const uint8_t *)State,Bytes)==Bytes;
   Output.flush();Output.close();
   if(!Valid||!MPE4ReadSave(Temp,Identity,Epoch,Slot,Bytes))return false;
   // Retain the former complete save through replacement and recover it if
   // the final rename fails. A power loss can always leave a verified backup.
   if(SD.exists(Backup)&&!SD.remove(Backup))return false;
   bool Previous=SD.exists(Path);
   if(Previous&&!SD.rename(Path,Backup))return false;
   if(SD.rename(Temp,Path))return true;
   if(Previous)SD.rename(Backup,Path);
   return false;
}
static FLASHMEM bool MPE4Restore(void *,const char *Identity,uint16_t Epoch,uint8_t Slot,mpe4::State *State,size_t Bytes)
{
   if(!Identity||!Slot||Slot>MPE4SaveSlots)return false;
   char Path[20],Backup[20];MPE4SavePath(Path,Identity,Slot,'s','a','v');MPE4SavePath(Backup,Identity,Slot,'b','a','k');
   if(!MPE4ReadSave(Path,Identity,Epoch,Slot,Bytes) && !MPE4ReadSave(Backup,Identity,Epoch,Slot,Bytes))return false;
   memcpy(State,MPE4Game->next,Bytes);return true;
}
static FLASHMEM void MPE4Reset()
{
   MPE4Active=false;
   if(MPE4Game&&MHSNativeArenaLeaseValid(&MPE4ArenaView)&&
      MHSNativeArenaOwns(MHSNativeArenaOwner::PowerEngine))
      MPE4Game->~Session();
   MPE4Game=nullptr;
   if(MHSNativeArenaOwns(MHSNativeArenaOwner::PowerEngine))
      MHSNativeArenaRelease(MHSNativeArenaOwner::PowerEngine);
   MPE4ArenaView={};MPE4Root=0;MPE4StartError=0;MPE4ResetInput();
}
static FLASHMEM bool MPE4StartFailed(uint8_t Error)
{
   MPE4Reset();MPE4StartError=Error;return false;
}
static FLASHMEM void MPE4Probe(uint32_t Root)
{
   uint8_t Magic[4];MPE4Root=0;
   if(Root<mpe4cart::LogicalLimit && MPE4Read(nullptr,Root,Magic,4) && !memcmp(Magic,"M4G2",4))MPE4Root=Root;
}
static FLASHMEM bool MPE4Start()
{
   MPE4StartError=0;
   if(!MPE3TitleSelected()||!MHSNativeArenaLeaseValid(&MPE3TitleArenaView))
      return MPE4StartFailed(MPE3TitleErrorMemory);
   MHSNativeArenaView View{};
   if(MHSNativeArenaHandoff(MHSNativeArenaOwner::Title,
         MHSNativeArenaOwner::PowerEngine,sizeof(mpe4::Session),
         alignof(mpe4::Session),&View)!=MHSNativeArenaStatus::Okay)
      return MPE4StartFailed(MPE3TitleErrorMemory);
   MPE4ArenaView=View;
   if(!MPE3TitleSelected()||!MHSNativeArenaLeaseValid(&MPE4ArenaView)||
      !View.data||View.bytes<sizeof(mpe4::Session)||
      ((uintptr_t)View.data&(alignof(mpe4::Session)-1u)))
      return MPE4StartFailed(MPE3TitleErrorMemory);
   MPE4Game=new (View.data) mpe4::Session{};
   mpe4::Storage Storage{nullptr,MPE4Save,MPE4Restore};
   if(!MPE4Game->start(MPE4Read,nullptr,MPE4Root,mpe4cart::LogicalLimit,Storage))
      return MPE4StartFailed(MPE4Game->error);
   // The final intro visit is a validated independent 1000-cell hires frame.
   // Seed that exact visible image so entering the real get.string prompt
   // does not blank and repaint the same login a second time.
   uint8_t Records[19*12];
   for(uint16_t Cell=0;Cell<1000;)
   {
      uint8_t Count=1000-Cell>19?19:(uint8_t)(1000-Cell);
      if(!MPE4Read(nullptr,MPE3Title.DeltaRaw+MPE3Title.FinalVisitOffset+4u+Cell*12u,Records,Count*12u))
         return MPE4StartFailed(6);
      for(uint8_t Index=0;Index<Count;Index++,Cell++)
      {
         const uint8_t *Record=Records+Index*12u;
         if(MHSNativeRead16(Record)!=Cell)
            return MPE4StartFailed(6);
         memcpy(MPE4Game->current+Cell*8u,Record+2,8);
         MPE4Game->current[8000+Cell]=Record[10];MPE4Game->current[9000+Cell]=Record[11];
      }
   }
   MPE4Game->seedPresentedFrame(true);
   if(!MPE3TitleSelected())return MPE4StartFailed(MPE3TitleErrorRead);
   MPE4ResetInput();
   MPE3TitleMailbox[0xFC]=0;MPE3TitleMailbox[0xFD]=0;
   MPE3TitleMailbox[0xFE]=0;MPE3TitleMailbox[0xFF]=0;
   MPE3TitleMemoryBarrier();MPE4Active=true;return true;
}
// The Phi2 ISR only captures a checked, sequenced input event. Parsing,
// rendering, sound progression, and filesystem calls remain in the poller.
static inline void MPE4LatchInput()
{
   uint8_t Sequence=MPE3TitleMailbox[0xFE],Flags=MPE3TitleMailbox[0xFD];
   if(!Sequence||Sequence==MPE3TitleMailbox[0xFC]||!(Flags&7u)||(Flags&~31u))return;
   // Keyboard and pointer coordinates share F8/F9, never the packet payload.
   if((Flags&5u)==5u||(!(Flags&4u)&&(Flags&24u)))return;
   uint8_t Key=MPE3TitleMailbox[0xF8],Scan=MPE3TitleMailbox[0xF9],Joy=MPE3TitleMailbox[0xFA];
   if((Flags&4u)&&(Key>=160||Scan>=200))return;
   if((Joy&~31u)||(uint8_t)(0xA5^Key^Scan^Joy^Flags^Sequence)!=MPE3TitleMailbox[0xFF])return;
   // A compound key/joystick event is accepted as one transaction.  If the
   // ordered part is full, leave the ACK unchanged and the C64 retries the
   // exact same sequence rather than losing either half.
   uint8_t KeyWrite=MPE4KeyboardWrite;
   if((Flags&1u)&&(uint8_t)(KeyWrite-MPE4KeyboardRead)>=MPE4KeyboardSlots)return;
   const uint8_t Buttons=(Flags>>3)&3u;
   uint8_t PointerWrite=MPE4PointerEdgeWrite;
   const bool PointerEdge=(Flags&4u)&&Buttons!=MPE4PointerButtons;
   if(PointerEdge&&(uint8_t)(PointerWrite-MPE4PointerEdgeRead)>=MPE4PointerEdgeSlots)return;
   if(Flags&1u)
   {
      uint8_t Slot=KeyWrite&(MPE4KeyboardSlots-1u);
      MPE4KeyboardKey[Slot]=Key;MPE4KeyboardScan[Slot]=Scan;
      MPE3TitleMemoryBarrier();MPE4KeyboardWrite=KeyWrite+1u;
   }
   if(Flags&2u)
   {
      uint8_t Old=MPE4JoyState;
      MPE4JoyState=Joy;
      if((Joy&16u)&&!(Old&16u))MPE4JoyFireWrite++;
      MPE3TitleMemoryBarrier();MPE4JoyRevision++;
   }
   if(Flags&4u)
   {
      const uint16_t Revision=MPE4PointerRevision+1u;
      MPE4PointerX=Key;MPE4PointerY=Scan;MPE4PointerButtons=Buttons;
      MPE3TitleMemoryBarrier();MPE4PointerRevision=Revision;
      if(PointerEdge)
      {
         uint8_t Slot=PointerWrite&(MPE4PointerEdgeSlots-1u);
         MPE4PointerEdgeX[Slot]=Key;MPE4PointerEdgeY[Slot]=Scan;
         MPE4PointerEdgeButtons[Slot]=Buttons;MPE4PointerEdgeRevision[Slot]=Revision;
         MPE3TitleMemoryBarrier();MPE4PointerEdgeWrite=PointerWrite+1u;
      }
   }
   MPE3TitleMemoryBarrier();MPE3TitleMailbox[0xFC]=Sequence;
}

static FLASHMEM void MPE4ConsumeInput(mpe4::Input &Input)
{
   uint8_t Read=MPE4KeyboardRead;
   if(Read!=MPE4KeyboardWrite)
   {
      uint8_t Slot=Read&(MPE4KeyboardSlots-1u);
      Input.key=MPE4KeyboardKey[Slot];Input.scan=MPE4KeyboardScan[Slot];
      MPE3TitleMemoryBarrier();MPE4KeyboardRead=Read+1u;
   }
   // Revision sampling closes the only preemption window: the Phi2 ISR may
   // replace held state between any two foreground loads, but can never leave
   // a mixed snapshot that passes the revision check.
   uint8_t Joy,Before,After;uint16_t Fire;
   do
   {
      Before=MPE4JoyRevision;MPE3TitleMemoryBarrier();
      Joy=MPE4JoyState;Fire=MPE4JoyFireWrite;MPE3TitleMemoryBarrier();
      After=MPE4JoyRevision;
   }while(Before!=After);
   if(MPE4JoyFireRead!=Fire){Input.fire=true;MPE4JoyFireRead++;}
   Joy&=15u;if((Joy&3u)==3u)Joy&=~3u;if((Joy&12u)==12u)Joy&=~12u;
   static const uint8_t Directions[16] PROGMEM={0,1,5,0,7,8,6,0,3,2,4,0,0,0,0,0};
   Input.direction=Directions[Joy];

   Read=MPE4PointerEdgeRead;
   if(Read!=MPE4PointerEdgeWrite)
   {
      uint8_t Slot=Read&(MPE4PointerEdgeSlots-1u);
      Input.pointerEvent=true;Input.pointerX=MPE4PointerEdgeX[Slot];
      Input.pointerY=MPE4PointerEdgeY[Slot];Input.pointerButtons=MPE4PointerEdgeButtons[Slot];
      MPE4PointerReadRevision=MPE4PointerEdgeRevision[Slot];
      MPE3TitleMemoryBarrier();MPE4PointerEdgeRead=Read+1u;return;
   }
   uint8_t X,Y,Buttons;uint16_t Revision,PointerBefore,PointerAfter;
   do
   {
      PointerBefore=MPE4PointerRevision;MPE3TitleMemoryBarrier();
      X=MPE4PointerX;Y=MPE4PointerY;Buttons=MPE4PointerButtons;
      MPE3TitleMemoryBarrier();PointerAfter=MPE4PointerRevision;
   }while(PointerBefore!=PointerAfter);
   Revision=PointerAfter;
   if(Revision!=MPE4PointerReadRevision)
   {
      Input.pointerEvent=true;Input.pointerX=X;Input.pointerY=Y;Input.pointerButtons=Buttons;
      MPE4PointerReadRevision=Revision;
   }
}
static FLASHMEM void MPE4Fail()
{
   // If the cartridge bank disappeared, its mailbox no longer belongs to us.
   // Tear down the native session without writing through the stale mapping.
   if(!MPE3TitleSelected()){MPE4Reset();return;}
   // The existing visible diagnostics retain the exact native script site.
   const mpe4::State &State=MPE4Game->game.state;
   MPE3TitleMailbox[0xF8]=State.errorLogic;MPE3TitleMailbox[0xF9]=State.errorOpcode;
   MPE3TitleMailbox[0xFA]=(uint8_t)State.errorIp;MPE3TitleMailbox[0xFD]=(uint8_t)(State.errorIp>>8);
   MPE3TitleMailbox[0xFE]=(uint8_t)State.error;
   MPE3TitleFail(0x40+MPE4Game->error);
}
static FLASHMEM void MPE4NextPacket()
{
   if(!MPE3TitleSelected()||!MPE4Game||
      !MHSNativeArenaOwns(MHSNativeArenaOwner::PowerEngine)||
      !MHSNativeArenaLeaseValid(&MPE4ArenaView))
   {MPE4Reset();return;}
   if(!MPE4Game->framePending)
   {
      mpe4::Input Input{};Input.elapsed60Hz=1;
      MPE4ConsumeInput(Input);
      if(!MPE4Game->prepareFrame(Input)){MPE4Fail();return;}
   }
   uint8_t SpriteBytes=MPE4Game->spritePacket(MPE3TitlePacket+8);
   if(SpriteBytes)
   {
      MPE3TitlePublish(5,0x20,SpriteBytes);return;
   }
   bool First=false;
   uint8_t Count=MPE4Game->cells(MPE3TitlePacket+8,MPE3TitleCellsPerPacket,First);
   if(Count)
   {
      MPE3TitlePublish(MPE3TitleCELL,8|(MPE4Game->hires?4:0)|(MPE4Game->parserSplit?0x40:0)|(First?16:0),Count*12);return;
   }
   memcpy(MPE3TitlePacket+8,MPE4Game->sid,26);
   uint8_t SpriteDescriptor=MPE4Game->spriteDescriptor(MPE3TitlePacket+8+26);
   MPE3TitlePublish(MPE3TitleSID,0x21|(MPE4Game->hires?4:0)|(MPE4Game->parserSplit?0x40:0),26+SpriteDescriptor);
}

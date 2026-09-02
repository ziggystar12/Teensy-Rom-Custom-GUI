// Native gameplay shares the proven M3 packet publisher and its retired intro
// arena. Included only in the native bank-58 service; no emulated CPU or DMA.
#include <new>
#define MPE4_CODE FLASHMEM
#define MPE4_RODATA PROGMEM
#include "mpe4_package.cpp"
#include "mpe4_game.cpp"
#include "mpe4_render.cpp"
#include "mpe4_session.cpp"

static mpe4::Session *MPE4Game;
static uint32_t MPE4Root;
static volatile bool MPE4Active, MPE4InputPending;
static volatile uint8_t MPE4InputKey, MPE4InputScan, MPE4InputJoy, MPE4InputFlags;
static uint8_t MPE4Joy;

static FLASHMEM bool MPE4Read(void *,uint32_t Raw,uint8_t *Data,uint16_t Count)
{
   uint8_t Error=0;
   return MPE3TitleSelected() && AGIPictureReadRawBytes(Raw,Data,Count,&Error);
}

// Validate into the unpublished frame, never over live game state. Save files
// are bound to the complete package CRC and this exact pointer-free state ABI.
static FLASHMEM bool MPE4ReadSave(const char *Path,uint32_t Identity,size_t Bytes)
{
   if(Bytes!=sizeof(mpe4::State)||Bytes>sizeof(MPE4Game->next))return false;
   File Input=SD.open(Path,FILE_READ);
   if(!Input)return false;
   uint8_t Header[32];
   bool Valid=Input.size()==Bytes+sizeof(Header) && Input.read(Header,sizeof(Header))==sizeof(Header);
   Valid=Valid && !memcmp(Header,"M4SV",4) && MHSNativeRead32(Header+4)==1 &&
      MHSNativeRead32(Header+8)==Identity && MHSNativeRead32(Header+12)==Bytes &&
      !MHSNativeRead32(Header+20) && !MHSNativeRead32(Header+24) &&
      MHSNativeRead32(Header+28)==MHSNativeCRC32(Header,28);
   if(Valid)Valid=Input.read(MPE4Game->next,Bytes)==(int)Bytes &&
      MHSNativeCRC32(MPE4Game->next,Bytes)==MHSNativeRead32(Header+16);
   Input.close();return Valid;
}
static FLASHMEM void MPE4Write32(uint8_t *p,uint32_t v)
{ for(uint8_t n=0;n<4;n++)p[n]=(uint8_t)(v>>(n*8)); }
static FLASHMEM void MPE4SavePath(char *Path,uint32_t Identity,char a,char b,char c)
{
   Path[0]='/';Path[1]='M';Path[2]='P';Path[3]='E';Path[4]='4';Path[5]='-';
   for(uint8_t i=0;i<8;i++){uint8_t n=(Identity>>(28-i*4))&15;
      Path[6+i]=n<10?'0'+n:'A'+n-10;}
   Path[14]='.';Path[15]=a;Path[16]=b;Path[17]=c;Path[18]=0;
}
static FLASHMEM bool MPE4Save(void *,uint32_t Identity,const mpe4::State *State,size_t Bytes)
{
   char Temp[19],Path[19],Backup[19];
   MPE4SavePath(Temp,Identity,'t','m','p');MPE4SavePath(Path,Identity,'s','a','v');
   MPE4SavePath(Backup,Identity,'b','a','k');
   uint8_t Header[32]={};memcpy(Header,"M4SV",4);MPE4Write32(Header+4,1);
   MPE4Write32(Header+8,Identity);MPE4Write32(Header+12,Bytes);
   MPE4Write32(Header+16,MHSNativeCRC32((const uint8_t *)State,Bytes));
   MPE4Write32(Header+28,MHSNativeCRC32(Header,28));
   if(SD.exists(Temp)&&!SD.remove(Temp))return false;
   File Output=SD.open(Temp,FILE_WRITE);
   if(!Output)return false;
   bool Valid=Output.write(Header,sizeof(Header))==sizeof(Header) &&
      Output.write((const uint8_t *)State,Bytes)==Bytes;
   Output.flush();Output.close();
   if(!Valid||!MPE4ReadSave(Temp,Identity,Bytes))return false;
   // Retain the former complete save through replacement and recover it if
   // the final rename fails. A power loss can always leave a verified backup.
   if(SD.exists(Backup)&&!SD.remove(Backup))return false;
   bool Previous=SD.exists(Path);
   if(Previous&&!SD.rename(Path,Backup))return false;
   if(SD.rename(Temp,Path))return true;
   if(Previous)SD.rename(Backup,Path);
   return false;
}
static FLASHMEM bool MPE4Restore(void *,uint32_t Identity,mpe4::State *State,size_t Bytes)
{
   char Path[19],Backup[19];MPE4SavePath(Path,Identity,'s','a','v');MPE4SavePath(Backup,Identity,'b','a','k');
   if(!MPE4ReadSave(Path,Identity,Bytes) && !MPE4ReadSave(Backup,Identity,Bytes))return false;
   memcpy(State,MPE4Game->next,Bytes);return true;
}
static FLASHMEM void MPE4Reset()
{
   MPE4Active=MPE4InputPending=false;MPE4Game=nullptr;MPE4Root=0;MPE4Joy=0;
}
static FLASHMEM void MPE4Probe(uint32_t Root)
{
   uint8_t Magic[4];MPE4Root=0;
   if(Root<0xE8000u && MPE4Read(nullptr,Root,Magic,4) && !memcmp(Magic,"M4G1",4))MPE4Root=Root;
}
static FLASHMEM bool MPE4Start()
{
   MPE4Game=new (MPE3TitleInternalAssets) mpe4::Session{};
   mpe4::Storage Storage{nullptr,MPE4Save,MPE4Restore};
   if(!MPE4Game->start(MPE4Read,nullptr,MPE4Root,0xE8000u,Storage))return false;
   // The final intro visit is a validated independent 1000-cell hires frame.
   // Seed that exact visible image so entering the real get.string prompt
   // does not blank and repaint the same login a second time.
   uint8_t Records[19*12];
   for(uint16_t Cell=0;Cell<1000;)
   {
      uint8_t Count=1000-Cell>19?19:(uint8_t)(1000-Cell);
      if(!MPE4Read(nullptr,MPE3Title.DeltaRaw+MPE3Title.FinalVisitOffset+4u+Cell*12u,Records,Count*12u))
      {MPE4Game->error=6;return false;}
      for(uint8_t Index=0;Index<Count;Index++,Cell++)
      {
         const uint8_t *Record=Records+Index*12u;
         if(MHSNativeRead16(Record)!=Cell){MPE4Game->error=6;return false;}
         memcpy(MPE4Game->current+Cell*8u,Record+2,8);
         MPE4Game->current[8000+Cell]=Record[10];MPE4Game->current[9000+Cell]=Record[11];
      }
   }
   MPE4Game->seedPresentedFrame(true);
   MPE4Joy=0;MPE4InputPending=false;
   MPE3TitleMailbox[0xFC]=0;MPE3TitleMailbox[0xFD]=0;
   MPE3TitleMailbox[0xFE]=0;MPE3TitleMailbox[0xFF]=0;
   MPE3TitleMemoryBarrier();MPE4Active=true;return true;
}
// The Phi2 ISR only captures a checked, sequenced input event. Parsing,
// rendering, sound progression, and filesystem calls remain in the poller.
static inline void MPE4LatchInput()
{
   uint8_t Sequence=MPE3TitleMailbox[0xFE],Flags=MPE3TitleMailbox[0xFD];
   if(!Sequence||Sequence==MPE3TitleMailbox[0xFC]||MPE4InputPending||!(Flags&7u)||(Flags&~31u))return;
   // Keyboard and pointer coordinates share F8/F9, never the packet payload.
   if((Flags&5u)==5u||(!(Flags&4u)&&(Flags&24u)))return;
   uint8_t Key=MPE3TitleMailbox[0xF8],Scan=MPE3TitleMailbox[0xF9],Joy=MPE3TitleMailbox[0xFA];
   if((Flags&4u)&&(Key>=160||Scan>=200))return;
   if((Joy&~31u)||(uint8_t)(0xA5^Key^Scan^Joy^Flags^Sequence)!=MPE3TitleMailbox[0xFF])return;
   MPE4InputKey=Key;MPE4InputScan=Scan;MPE4InputJoy=Joy;MPE4InputFlags=Flags;
   MPE3TitleMemoryBarrier();MPE4InputPending=true;MPE3TitleMailbox[0xFC]=Sequence;
}
static FLASHMEM void MPE4Fail()
{
   // The existing visible diagnostics retain the exact native script site.
   const mpe4::State &State=MPE4Game->game.state;
   MPE3TitleMailbox[0xF8]=State.errorLogic;MPE3TitleMailbox[0xF9]=State.errorOpcode;
   MPE3TitleMailbox[0xFA]=(uint8_t)State.errorIp;MPE3TitleMailbox[0xFD]=(uint8_t)(State.errorIp>>8);
   MPE3TitleMailbox[0xFE]=(uint8_t)State.error;
   MPE3TitleFail(0x40+MPE4Game->error);
}
static FLASHMEM void MPE4NextPacket()
{
   if(!MPE4Game->framePending)
   {
      mpe4::Input Input{};Input.elapsed60Hz=1;
      if(MPE4InputPending)
      {
         noInterrupts();
         uint8_t Flags=MPE4InputFlags,Joy=MPE4InputJoy;
         if(Flags&1){Input.key=MPE4InputKey;Input.scan=MPE4InputScan;}
         if(Flags&2){Input.fire=(Joy&16)&&!(MPE4Joy&16);MPE4Joy=Joy;}
         if(Flags&4){Input.pointerEvent=true;Input.pointerX=MPE4InputKey;Input.pointerY=MPE4InputScan;
            Input.pointerButtons=(Flags>>3)&3u;}
         MPE4InputPending=false;interrupts();
      }
      // Opposing contacts cancel on each axis. Keyboard direction remains
      // latched by the core independently from this held joystick direction.
      uint8_t Joy=MPE4Joy&15;
      if((Joy&3)==3)Joy&=~3u;if((Joy&12)==12)Joy&=~12u;
      static const uint8_t Directions[16] PROGMEM={0,1,5,0,7,8,6,0,3,2,4,0,0,0,0,0};
      Input.direction=Directions[Joy];
      if(!MPE4Game->prepareFrame(Input)){MPE4Fail();return;}
   }
   bool First=false;
   uint8_t Count=MPE4Game->cells(MPE3TitlePacket+8,MPE3TitleCellsPerPacket,First);
   if(Count)
   {
      MPE3TitlePublish(MPE3TitleCELL,8|(MPE4Game->hires?4:0)|(First?16:0),Count*12);return;
   }
   memcpy(MPE3TitlePacket+8,MPE4Game->sid,26);
   MPE3TitlePublish(MPE3TitleSID,0x21|(MPE4Game->hires?4:0),26);
}

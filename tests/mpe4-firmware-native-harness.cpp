#include <map>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define PROGMEM
static constexpr int FILE_READ=0,FILE_WRITE=1;
static bool StorageFails=false;
struct File {
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t cursor=0;
  explicit operator bool()const{return bool(bytes);}
  size_t size()const{return bytes?bytes->size():0;}
  int read(uint8_t *out,size_t n){if(!bytes)return -1;n=std::min(n,bytes->size()-cursor);memcpy(out,bytes->data()+cursor,n);cursor+=n;return int(n);}
  size_t write(const uint8_t *in,size_t n){if(!bytes||StorageFails)return 0;bytes->insert(bytes->end(),in,in+n);return n;}
  void flush(){} void close(){bytes.reset();}
};
struct TestSD {
  std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
  File open(const char *p,int mode){if(StorageFails)return {};if(mode==FILE_WRITE&&!files.count(p))files[p]=std::make_shared<std::vector<uint8_t>>();return files.count(p)?File{files[p],0}:File{};}
  bool exists(const char *p){return files.count(p);}
  bool remove(const char *p){return files.erase(p)>0;}
  bool rename(const char *a,const char *b){if(!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
} SD;
static unsigned inputInterruptMasks=0;
static void noInterrupts(){inputInterruptMasks++;} static void interrupts(){}
#define main legacyIntroConformance
#include "mpe3-title-native-harness.cpp"
#undef main

static uint8_t inputSequence=0;
static unsigned nativeFrames=0,packets=0,inputEvents=0;
static unsigned pendingInputRejects=0,directionReversals=0;
static unsigned spritePackets=0,spriteCommits=0,coordinateFrames=0,visibleSpriteFrames=0,threeLayerFrames=0,fourLayerFrames=0;
static uint8_t screen[10000]{};
static uint8_t stagedShapes[256]{},visibleShapes[256]{},stagedParts=0;
static bool hasSpritePose=false;
static std::ofstream trace;
static void consumePacket()
{
  assert(MPE3TitleOwned&&MPE3Title.Pending);
  assert(EZFlashRAM[0]=='M'&&EZFlashRAM[1]=='3'&&EZFlashRAM[2]==1);
  unsigned length=EZFlashRAM[6]+8;
  assert(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length));
  assert(EZFlashRAM[3]!=14);
  bool native=MPE4Active;
  tracePacket(&trace);packets++;
  if(EZFlashRAM[3]==1)for(unsigned p=8;p<length;p+=12){unsigned c=MHSNativeRead16(EZFlashRAM+p);assert(c<1000);memcpy(screen+c*8,EZFlashRAM+p+2,8);screen[8000+c]=EZFlashRAM[p+10];screen[9000+c]=EZFlashRAM[p+11];}
  if(EZFlashRAM[3]==5) {
    assert(native&&MPE4Game->package.egoSprites&&EZFlashRAM[6]==130&&EZFlashRAM[8]==1);
    const uint8_t part=EZFlashRAM[9];assert(part<2&&stagedParts==(part?1:0));
    memcpy(stagedShapes+part*128,EZFlashRAM+10,128);stagedParts|=1u<<part;spritePackets++;
    assert(!memcmp(stagedShapes+part*128,MPE4Game->nextEgo.shapes+part*128,128));
    // A newly received half remains hidden until the SID frame boundary.
    if(MPE4Game->currentEgo.enable)assert(!memcmp(visibleShapes,MPE4Game->currentEgo.shapes,256));
  }
  if(native&&EZFlashRAM[3]==2) {
    assert(EZFlashRAM[5]&32);
    if(memcmp(screen,MPE4Game->next,10000)) {
      size_t offset=0;while(offset<10000&&screen[offset]==MPE4Game->next[offset])offset++;
      std::cerr<<"Presented frame differs at byte "<<offset<<" after "<<nativeFrames<<" frames / "<<inputEvents<<" inputs / "<<directionReversals<<" reversals\n";
      std::exit(94);
    }
    nativeFrames++;
    if(MPE4Game->package.egoSprites) {
      const auto &ego=MPE4Game->nextEgo;const uint8_t *descriptor=EZFlashRAM+34;
      assert(EZFlashRAM[6]==37&&descriptor[0]==1&&descriptor[1]==ego.enable);
      assert(MHSNativeRead16(descriptor+2)==ego.x&&descriptor[4]==ego.y&&!memcmp(descriptor+5,ego.colors,6));
      assert(stagedParts==0||stagedParts==3);
      if(stagedParts==3){memcpy(visibleShapes,stagedShapes,256);hasSpritePose=true;spriteCommits++;}
      else if(ego.enable)coordinateFrames++;
      stagedParts=0;
      if(ego.enable) {
        assert(hasSpritePose&&!memcmp(visibleShapes,ego.shapes,256));visibleSpriteFrames++;
        unsigned layers=0;for(unsigned bit=1;bit<=4;bit++)layers+=(ego.enable>>bit)&1;
        threeLayerFrames+=layers==3;fourLayerFrames+=layers==4;
      }
    } else assert(EZFlashRAM[6]==26&&!stagedParts&&!spritePackets);
  }
  std::array<uint8_t,240> before{};memcpy(before.data(),EZFlashRAM,240);
  uint32_t frames=native?MPE4Game->frames:0,reads=ReadCalls;
  for(unsigned n=0;n<3;n++)MPE3TitlePollingHndlr();
  assert(!memcmp(before.data(),EZFlashRAM,240));assert(ReadCalls==reads);
  if(native)assert(MPE4Game->frames==frames);
  uint8_t seq=EZFlashRAM[0xf7];writeControl(0xf6,seq);MPE3TitlePollingHndlr();
  assert(EZFlashRAM[0xfb]==0);
}
static void frame(){unsigned goal=nativeFrames+1;for(unsigned limit=0;nativeFrames<goal&&limit<10000;limit++)consumePacket();assert(nativeFrames==goal);}
static void send(uint8_t key,uint8_t scan=0,uint8_t joy=0,uint8_t flags=1)
{
  while(MPE4InputPending)frame();
  inputSequence=inputSequence==255?1:inputSequence+1;
  uint32_t reads=ReadCalls;
  writeControl(0xf8,key);writeControl(0xf9,scan);writeControl(0xfa,joy);writeControl(0xfd,flags);writeControl(0xfe,inputSequence);
  writeControl(0xff,uint8_t(0xa5^key^scan^joy^flags^inputSequence));writeControl(0xf4,3);
  assert(MPE4InputPending&&EZFlashRAM[0xfc]==inputSequence&&ReadCalls==reads);inputEvents++;
  // A complete, valid second producer event must not overwrite any field
  // while the consumer owns the first snapshot. Retry that sequence later.
  const uint8_t contender=inputSequence==255?1:inputSequence+1;
  writeControl(0xf8,23);writeControl(0xf9,199);writeControl(0xfa,1);
  writeControl(0xfd,4);writeControl(0xfe,contender);
  writeControl(0xff,uint8_t(0xa5^23^199^1^4^contender));writeControl(0xf4,3);
  assert(MPE4InputPending&&EZFlashRAM[0xfc]==inputSequence&&ReadCalls==reads);
  assert(MPE4InputKey==key&&MPE4InputScan==scan&&MPE4InputJoy==joy&&MPE4InputFlags==flags);
  pendingInputRejects++;
  frame();frame();
  if(inputInterruptMasks){std::cerr<<"Native input masked the PHI2 bus interrupt "<<inputInterruptMasks<<" time(s)\n";std::exit(93);}
}
int main(int argc,char **argv)
{
  assert(argc==4);
  // Run every accepted intro regression against this exact integrated module.
  char *legacyArgs[]={argv[0],argv[1]};std::ostringstream legacy;
  auto *output=std::cout.rdbuf(legacy.rdbuf());int legacyResult=legacyIntroConformance(2,legacyArgs);std::cout.rdbuf(output);assert(!legacyResult);
  std::ifstream rawFile(argv[2],std::ios::binary);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(rawFile)),{});assert(raw.size()==1048576);
  std::vector<uint8_t> combined(raw.begin()+Root,raw.end());trace.open(argv[3],std::ios::binary);assert(trace.good());
  start(combined);writeControl(0xf4,2);
  for(unsigned n=0;(!MPE4Active||MPE4Game->game.state.modal!=mpe4::StringInput)&&n<20000;n++)consumePacket();
  assert(MPE4Active&&MPE4Game->game.state.modal==mpe4::StringInput);
  assert(EZFlashRAM[0xfc]==0&&MPE4Game->game.state.vars[0]==69);
  uint32_t reads=ReadCalls;uint8_t ack=EZFlashRAM[0xfc];
  writeControl(0xfe,1);writeControl(0xfd,1);writeControl(0xff,0);writeControl(0xf4,3);
  assert(!MPE4InputPending&&EZFlashRAM[0xfc]==ack&&ReadCalls==reads);
  for(char c:std::string("Roger"))send(c);
  send(13,28);
  for(unsigned n=0;(MPE4Game->game.state.vars[0]!=2||!MPE4Game->game.state.playerControl)&&n<1000;n++)frame();
  assert(MPE4Game->game.state.vars[0]==2&&MPE4Game->game.state.playerControl);
  assert(std::string(MPE4Game->game.state.strings[1])=="Roger");
  const bool spritesEnabled=MPE4Game->package.egoSprites;
  auto &state=MPE4Game->game.state;
  // Corrupted events and duplicate/queued writes cannot advance game or steal
  // the first input ACK. The channel also survives more than one full wrap.
  for(unsigned n=0;n<260;n++)send(0,0,0,2);
  while(state.modal)send(13,28);
  // Real C64 ASCII+PC scan pairs must remain printable when the source binds
  // Alt-D/Alt-Z (ASCII zero). The old character-only fixture missed this.
  send('d',32);send('D',32);send('z',44);send('Z',44);
  assert(std::string(state.input)=="dDzZ");
  while(state.inputLength)send(8,14);
  // Malformed but correctly checksummed pointer records cannot steal ACKs.
  const uint8_t pointerSequence=inputSequence==255?1:inputSequence+1;
  for(const auto invalid:std::vector<std::array<uint8_t,3>>{{80,100,5},{160,100,4},{80,200,4},{80,100,8},{80,100,32}}){
    const auto previousAck=EZFlashRAM[0xfc];const auto previousReads=ReadCalls;
    writeControl(0xf8,invalid[0]);writeControl(0xf9,invalid[1]);writeControl(0xfa,0);
    writeControl(0xfd,invalid[2]);writeControl(0xfe,pointerSequence);
    writeControl(0xff,uint8_t(0xa5^invalid[0]^invalid[1]^invalid[2]^pointerSequence));writeControl(0xf4,3);
    assert(!MPE4InputPending&&EZFlashRAM[0xfc]==previousAck&&ReadCalls==previousReads);
  }
  send(80,100,0,4);
  assert(state.pointerX==80&&state.pointerY==100&&state.pointerButtons==0);
  send('l',38);send('o',24);send('o',24);send('k',37);send(13,28);
  assert(state.modal);
  send(80,100,0,12);assert(!state.modal&&state.pointerButtons==1);
  send(80,100,0,4);assert(state.pointerButtons==0);
  uint8_t x=state.objects[0].x,y=state.objects[0].y,room=state.vars[0];
  send(0,0,2,2);for(unsigned n=0;n<20;n++)frame();send(0,0,0,2);
  if(state.objects[0].x==x&&state.objects[0].y==y&&room==state.vars[0])
    std::cerr<<"movement before="<<unsigned(x)<<","<<unsigned(y)<<" after="<<unsigned(state.objects[0].x)<<","<<unsigned(state.objects[0].y)<<" modal="<<unsigned(state.modal)<<" direction="<<unsigned(state.objects[0].direction)<<"\n";
  assert((state.objects[0].x!=x||state.objects[0].y!=y||room!=state.vars[0])&&state.objects[0].direction==0);
  // Repeat both horizontal and vertical reversals through the live sequencer.
  // No input may pause the bus ISR, including sequence wrap and rejected peers.
  for(unsigned n=0;n<64;n++) {
    const uint8_t joy=std::array<uint8_t,4>{4,8,1,2}[n&3];
    send(0,0,joy,2);assert(MPE4Joy==joy&&!MPE4InputPending);directionReversals++;
  }
  send(0,0,0,2);assert(MPE4Joy==0&&pendingInputRejects==inputEvents&&!inputInterruptMasks);
  // Save/readback/backup recovery execute the actual firmware storage glue.
  const auto identity=MPE4Game->package.crc;
  char savePath[32],backupPath[32];
  std::snprintf(savePath,sizeof(savePath),"/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(backupPath,sizeof(backupPath),"/MPE4-%08X.bak",unsigned(identity));
  SD.files["/MPE4-SQ1.sav"]=std::make_shared<std::vector<uint8_t>>(4,0x5a);
  auto saved=state;assert(MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));
  assert(SD.exists(savePath));
  state.vars[3]^=7;assert(MPE4Restore(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(!memcmp(&saved,&state,sizeof(state)));
  assert(MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(SD.exists(backupPath));
  (*SD.files[savePath])[50]^=1;
  state.vars[3]^=3;assert(MPE4Restore(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(!memcmp(&saved,&state,sizeof(state)));
  auto before=state;assert(!MPE4Restore(nullptr,MPE4Game->package.crc^1,&state,sizeof(state)));assert(!memcmp(&before,&state,sizeof(state)));
  StorageFails=true;assert(!MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));StorageFails=false;
  const auto firstSave=*SD.files[savePath];
  assert(MPE4Save(nullptr,identity^0x80000000u,&state,sizeof(state)));
  assert(*SD.files[savePath]==firstSave);
  assert(*SD.files["/MPE4-SQ1.sav"]==std::vector<uint8_t>(4,0x5a));
  // Native05 files carry the unchanged 9528-byte State prefix. Exercise the
  // real firmware migration, including every validation before live replace.
  constexpr size_t oldBytes=9528;
  static_assert(offsetof(mpe4::State,overflowBindings)==oldBytes);
  assert(sizeof(state)==9624);
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  auto oldSave=*SD.files[savePath];oldSave.resize(32+oldBytes);
  MPE4Write32(oldSave.data()+12,oldBytes);
  MPE4Write32(oldSave.data()+16,MHSNativeCRC32(oldSave.data()+32,oldBytes));
  MPE4Write32(oldSave.data()+28,MHSNativeCRC32(oldSave.data(),28));
  SD.files.erase(backupPath);SD.files[savePath]=std::make_shared<std::vector<uint8_t>>(oldSave);
  std::memset(state.overflowBindings,0x7b,sizeof(state.overflowBindings));state.vars[3]^=5;
  assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
  assert(!std::memcmp(&state,oldSave.data()+32,oldBytes));
  const uint8_t *tail=reinterpret_cast<const uint8_t *>(state.overflowBindings);
  assert(std::all_of(tail,tail+sizeof(state.overflowBindings),[](uint8_t b){return b==0;}));
  const auto migrated=state;
  for(unsigned fault=0;fault<4;fault++) {
    auto invalid=oldSave;
    if(fault==0)invalid[50]^=1;
    if(fault==1)invalid.pop_back();
    if(fault==2)invalid[28]^=1;
    if(fault==3){MPE4Write32(invalid.data()+8,identity^1u);MPE4Write32(invalid.data()+28,MHSNativeCRC32(invalid.data(),28));}
    SD.files[savePath]=std::make_shared<std::vector<uint8_t>>(invalid);
    assert(!MPE4Restore(nullptr,identity,&state,sizeof(state)));
    assert(!std::memcmp(&migrated,&state,sizeof(state)));
  }
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.files[savePath]->size()==sizeof(state)+32);
  assert(MHSNativeRead32(SD.files[savePath]->data()+12)==sizeof(state));
  if(spritesEnabled)assert(spritePackets==spriteCommits*2&&spriteCommits&&coordinateFrames&&visibleSpriteFrames&&threeLayerFrames+fourLayerFrames);
  else assert(!spritePackets&&!spriteCommits&&!visibleSpriteFrames);
  CurrentEasyFlashBank=3;auto mailbox=std::array<uint8_t,256>{};memcpy(mailbox.data(),EZFlashRAM,256);
  assert(!MPE3TitlePollingHndlr()&&!MPE4Active&&!MPE3TitleOwned);assert(!memcmp(mailbox.data(),EZFlashRAM,256));
  trace.close();
  std::cout<<"{\"passed\":true,\"legacyIntro\":"<<legacy.str()<<",\"sessionBytes\":"<<sizeof(mpe4::Session)<<",\"packets\":"<<packets<<",\"nativeFrames\":"<<nativeFrames<<",\"inputEvents\":"<<inputEvents<<",\"keyboardScanChecks\":4,\"pointerChecks\":8,\"maximumRawRead\":"<<MaxReadLength<<",\"storageChecks\":9,\"legacyStorageChecks\":6,\"room\":2,\"runtimeCpuEmulation\":false"
    <<",\"spritesEnabled\":"<<(spritesEnabled?"true":"false")<<",\"spritePackets\":"<<spritePackets<<",\"spriteCommits\":"<<spriteCommits
    <<",\"coordinateFrames\":"<<coordinateFrames<<",\"visibleSpriteFrames\":"<<visibleSpriteFrames
    <<",\"inputInterruptMasks\":"<<inputInterruptMasks<<",\"pendingInputRejects\":"<<pendingInputRejects<<",\"directionReversals\":"<<directionReversals
    <<",\"threeLayerFrames\":"<<threeLayerFrames<<",\"fourLayerFrames\":"<<fourLayerFrames<<",\"spriteFrameAtomic\":true}\n";
}

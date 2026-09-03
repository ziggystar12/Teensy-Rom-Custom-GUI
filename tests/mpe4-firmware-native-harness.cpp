#include <map>
#include <set>
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
static constexpr int FILE_READ=0,FILE_WRITE=1,FILE_WRITE_BEGIN=2;
static bool StorageFails=false;
static size_t StorageWriteBudget=size_t(-1);
static unsigned rootWriteAttempts=0,rootMutationAttempts=0;
static bool saveFolderPath(const std::string &p){return p=="/SAVES"||p.rfind("/SAVES/",0)==0;}
struct File {
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t cursor=0;
  bool directory=false;
  unsigned shortReads=0,shortWrites=0,failedSeeks=0;
  explicit operator bool()const{return bool(bytes)||directory;}
  bool isDirectory()const{return directory;}
  size_t size()const{return bytes?bytes->size():0;}
  bool seek(size_t position){if(failedSeeks){--failedSeeks;return false;}if(!bytes||position>bytes->size())return false;cursor=position;return true;}
  int read(uint8_t *out,size_t n){if(!bytes)return -1;if(shortReads){--shortReads;n=std::min(n,size_t(17));}n=std::min(n,bytes->size()-cursor);memcpy(out,bytes->data()+cursor,n);cursor+=n;return int(n);}
  size_t write(const uint8_t *in,size_t n){if(!bytes||StorageFails)return 0;if(shortWrites){--shortWrites;n=std::min(n,size_t(17));}n=std::min(n,StorageWriteBudget);StorageWriteBudget-=n;if(cursor+n>bytes->size())bytes->resize(cursor+n);memcpy(bytes->data()+cursor,in,n);cursor+=n;return n;}
  void flush(){} void close(){bytes.reset();directory=false;}
};
struct TestSD {
  std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
  std::set<std::string> directories{"/"};
  std::vector<std::string> writeAttempts,mutations;
  std::string failWritePath,failReadPath;
  std::map<std::pair<std::string,std::string>,unsigned> renameFailures;
  bool mkdirFails=false;
  bool parentExists(const std::string &p)const{const auto slash=p.find_last_of('/');return slash!=std::string::npos&&directories.count(slash?p.substr(0,slash):"/");}
  File open(const char *p,int mode=FILE_READ){
    if(mode!=FILE_READ){writeAttempts.push_back(p);if(!saveFolderPath(p))rootWriteAttempts++;}
    if(StorageFails||(mode!=FILE_READ?failWritePath:failReadPath)==p)return {};
    if(directories.count(p))return mode==FILE_READ?File{nullptr,0,true}:File{};
    if(!parentExists(p))return {};
    if(mode!=FILE_READ&&!files.count(p)){files[p]=std::make_shared<std::vector<uint8_t>>();mutations.push_back(p);}
    return files.count(p)?File{files[p],mode==FILE_WRITE?files[p]->size():0,false}:File{};
  }
  bool exists(const char *p){return files.count(p)||directories.count(p);}
  bool mkdir(const char *p){if(!saveFolderPath(p))rootMutationAttempts++;if(StorageFails||mkdirFails||files.count(p)||!parentExists(p))return false;directories.insert(p);mutations.push_back(p);return true;}
  bool remove(const char *p){if(!saveFolderPath(p))rootMutationAttempts++;if(StorageFails||!files.count(p))return false;mutations.push_back(p);return files.erase(p)>0;}
  bool rename(const char *a,const char *b){
    if(!saveFolderPath(a)||!saveFolderPath(b))rootMutationAttempts++;
    auto failure=renameFailures.find({a,b});if(failure!=renameFailures.end()&&failure->second){failure->second--;return false;}
    if(StorageFails||!files.count(a)||exists(b)||!parentExists(b))return false;
    files[b]=files[a];files.erase(a);mutations.push_back(a);mutations.push_back(b);return true;
  }
} SD;
static unsigned inputInterruptMasks=0;
static void noInterrupts(){inputInterruptMasks++;} static void interrupts(){}
#define main legacyIntroConformance
#include "mpe3-title-native-harness.cpp"
#undef main

static uint8_t inputSequence=0;
static unsigned nativeFrames=0,packets=0,inputEvents=0;
static unsigned pendingInputRejects=0,directionReversals=0;
static unsigned saveDirectoryChecks=0,rootSaveFallbackChecks=0,saveFailureChecks=0;
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
static std::map<std::string,std::vector<uint8_t>> storageSnapshot()
{
  std::map<std::string,std::vector<uint8_t>> result;
  for(const auto &file:SD.files)result[file.first]=*file.second;
  return result;
}
static void checkSaveDirectory(uint32_t identity,mpe4::State &state,const std::vector<uint8_t> &legacySave)
{
  char path[32],backup[32],temp[32],rootPath[32],rootBackup[32];
  std::snprintf(path,sizeof(path),"/SAVES/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(backup,sizeof(backup),"/SAVES/MPE4-%08X.bak",unsigned(identity));
  std::snprintf(temp,sizeof(temp),"/SAVES/MPE4-%08X.tmp",unsigned(identity));
  std::snprintf(rootPath,sizeof(rootPath),"/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(rootBackup,sizeof(rootBackup),"/MPE4-%08X.bak",unsigned(identity));
  const auto clear=[](){SD=TestSD{};StorageFails=false;StorageWriteBudget=size_t(-1);};
  const auto put=[](const char *name,const std::vector<uint8_t> &bytes){SD.files[name]=std::make_shared<std::vector<uint8_t>>(bytes);};
  // Produce two distinct, valid records through the actual writer, creating
  // the directory on the first save and rotating the second into its backup.
  clear();const auto stateA=state;
  assert(!SD.exists("/SAVES"));assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.directories.count("/SAVES")&&SD.exists(path)&&!SD.exists(rootPath));
  const auto recordA=*SD.files[path];saveDirectoryChecks++;
  state.vars[3]^=0x35;const auto stateB=state;
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  const auto recordB=*SD.files[path];assert(*SD.files[backup]==recordA);saveDirectoryChecks++;
  const auto roots=[&](){clear();put(rootPath,recordA);put(rootBackup,recordB);};
  const auto unchangedRoots=[&](){assert(*SD.files[rootPath]==recordA&&*SD.files[rootBackup]==recordB);};
  const auto restore=[&](const mpe4::State &expected){
    state.vars[3]^=0x55;assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
    assert(!std::memcmp(&state,&expected,sizeof(state)));
  };
  // Restore is read-only. Prefer the folder's primary, then its backup,
  // before root saves. Corrupt records cannot alter the live game state.
  for(unsigned scenario=0;scenario<6;scenario++) {
    roots();auto bad=recordB;bad[50]^=1;
    const mpe4::State *expected=&stateA;
    if(scenario>=1&&scenario<=3) {
      SD.directories.insert("/SAVES");put(path,scenario==1?recordB:bad);
      put(backup,scenario==1?recordA:scenario==2?recordB:bad);
      if(scenario<=2)expected=&stateB;
    }
    if(scenario>=4){put(rootPath,bad);expected=&stateB;}
    if(scenario==5)put(rootBackup,bad);
    const auto before=storageSnapshot();const auto directories=SD.directories;
    if(scenario==5){const auto live=state;assert(!MPE4Restore(nullptr,identity,&state,sizeof(state)));assert(!std::memcmp(&live,&state,sizeof(state)));}
    else restore(*expected);
    assert(storageSnapshot()==before&&SD.directories==directories);
    assert(SD.writeAttempts.empty()&&SD.mutations.empty());rootSaveFallbackChecks++;
  }
  // A missing/unusable save directory must not turn a failed save into a
  // write at the SD root. The prior root save must remain recoverable.
  for(unsigned failure=0;failure<3;failure++) {
    roots();
    if(failure==0)SD.mkdirFails=true;
    if(failure==1)put("/SAVES",std::vector<uint8_t>{1,2,3});
    if(failure==2){SD.directories.insert("/SAVES");SD.failReadPath="/SAVES";}
    const auto before=storageSnapshot();state=stateB;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    assert(SD.writeAttempts.empty()&&storageSnapshot()==before);unchangedRoots();
    restore(stateA);saveDirectoryChecks++;
  }
  // Simulate failures opening, writing, verifying, and promoting a first
  // folder save. No failed operation consumes or renames legacy root files.
  for(unsigned failure=0;failure<4;failure++) {
    roots();SD.directories.insert("/SAVES");state=stateB;
    if(failure==0)SD.failWritePath=temp;
    if(failure==1)StorageWriteBudget=31;
    if(failure==2)SD.failReadPath=temp;
    if(failure==3)SD.renameFailures[{temp,path}]=1;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    StorageWriteBudget=size_t(-1);SD.failReadPath.clear();
    assert(!SD.exists(path)&&!SD.exists(backup));unchangedRoots();restore(stateA);saveFailureChecks++;
  }
  // A failed rotation keeps the primary. A failed final promotion rolls its
  // verified backup back into place; either outcome preserves a usable save.
  for(unsigned failure=0;failure<2;failure++) {
    roots();SD.directories.insert("/SAVES");put(path,recordA);put(backup,recordB);state=stateB;
    SD.renameFailures[failure?std::make_pair(std::string(temp),std::string(path)):std::make_pair(std::string(path),std::string(backup))]=1;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    assert(SD.exists(path)&&*SD.files[path]==recordA);unchangedRoots();restore(stateA);saveFailureChecks++;
  }
  // Successful new saves also leave legacy root records untouched. Future
  // restore must select the new folder record and its backup first.
  roots();state=stateB;assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(*SD.files[path]==recordB);unchangedRoots();restore(stateB);
  state=stateA;assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(*SD.files[path]==recordA&&*SD.files[backup]==recordB);unchangedRoots();
  (*SD.files[path])[50]^=1;restore(stateB);rootSaveFallbackChecks++;
  // The old 9528-byte prefix works from the root fallback too. Its next save
  // uses the current ABI in /SAVES without editing the legacy source record.
  clear();put(rootPath,legacySave);std::memset(state.overflowBindings,0x7b,sizeof(state.overflowBindings));
  assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
  assert(!std::memcmp(&state,legacySave.data()+32,mpe4::LegacyStateBytes));
  const auto *tail=reinterpret_cast<const uint8_t *>(state.overflowBindings);
  assert(std::all_of(tail,tail+sizeof(state.overflowBindings),[](uint8_t value){return !value;}));
  assert(!SD.exists("/SAVES")&&*SD.files[rootPath]==legacySave);
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.files[path]->size()==sizeof(state)+32&&*SD.files[rootPath]==legacySave);rootSaveFallbackChecks++;
  assert(!rootWriteAttempts&&!rootMutationAttempts&&!inputInterruptMasks);
}
#ifndef MPE4_HARNESS_MAIN
#define MPE4_HARNESS_MAIN main
#endif
int MPE4_HARNESS_MAIN(int argc,char **argv)
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
  std::snprintf(savePath,sizeof(savePath),"/SAVES/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(backupPath,sizeof(backupPath),"/SAVES/MPE4-%08X.bak",unsigned(identity));
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
  checkSaveDirectory(identity,state,oldSave);
  if(spritesEnabled)assert(spritePackets==spriteCommits*2&&spriteCommits&&coordinateFrames&&visibleSpriteFrames&&threeLayerFrames+fourLayerFrames);
  else assert(!spritePackets&&!spriteCommits&&!visibleSpriteFrames);
  CurrentEasyFlashBank=3;auto mailbox=std::array<uint8_t,256>{};memcpy(mailbox.data(),EZFlashRAM,256);
  assert(!MPE3TitlePollingHndlr()&&!MPE4Active&&!MPE3TitleOwned);assert(!memcmp(mailbox.data(),EZFlashRAM,256));
  trace.close();
  std::cout<<"{\"passed\":true,\"legacyIntro\":"<<legacy.str()<<",\"sessionBytes\":"<<sizeof(mpe4::Session)<<",\"packets\":"<<packets<<",\"nativeFrames\":"<<nativeFrames<<",\"inputEvents\":"<<inputEvents<<",\"keyboardScanChecks\":4,\"pointerChecks\":8,\"maximumRawRead\":"<<MaxReadLength<<",\"storageChecks\":9,\"legacyStorageChecks\":6,\"room\":2,\"runtimeCpuEmulation\":false"
    <<",\"spritesEnabled\":"<<(spritesEnabled?"true":"false")<<",\"spritePackets\":"<<spritePackets<<",\"spriteCommits\":"<<spriteCommits
    <<",\"coordinateFrames\":"<<coordinateFrames<<",\"visibleSpriteFrames\":"<<visibleSpriteFrames
    <<",\"inputInterruptMasks\":"<<inputInterruptMasks<<",\"pendingInputRejects\":"<<pendingInputRejects<<",\"directionReversals\":"<<directionReversals
    <<",\"saveDirectory\":{\"path\":\"/SAVES\",\"directoryChecks\":"<<saveDirectoryChecks<<",\"fallbackChecks\":"<<rootSaveFallbackChecks<<",\"transactionFailureChecks\":"<<saveFailureChecks
    <<",\"rootWriteAttempts\":"<<rootWriteAttempts<<",\"rootMutationAttempts\":"<<rootMutationAttempts<<"}"
    <<",\"threeLayerFrames\":"<<threeLayerFrames<<",\"fourLayerFrames\":"<<fourLayerFrames<<",\"spriteFrameAtomic\":true}\n";
}

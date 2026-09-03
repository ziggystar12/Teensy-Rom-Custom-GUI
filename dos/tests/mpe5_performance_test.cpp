// Fixed guest-work benchmark of the actual firmware publisher and pending
// packet pump. Packet/poll counts are transport work, not hardware speed.
#define MHS_NATIVE_ARENA_TEST
#define MPE4_HARNESS_MAIN unusedSierraConformance
#include "../../tests/mpe4-firmware-native-harness.cpp"
#include <chrono>
#include <csignal>

static std::vector<uint8_t> readDosFile(const char *path) {
  std::ifstream input(path,std::ios::binary); assert(input.good());
  return {std::istreambuf_iterator<char>(input),{}};
}
static std::vector<uint8_t> extractDos(const std::vector<uint8_t>& crt) {
  assert(crt.size()>=64&&!memcmp(crt.data(),"C64 CARTRIDGE   ",16));
  std::vector<uint8_t> raw(1048576,255);
  const auto be16=[&](size_t at){return unsigned(crt.at(at))*256+crt.at(at+1);};
  for(size_t at=64;at<crt.size();) {
    assert(!memcmp(crt.data()+at,"CHIP",4));
    const unsigned bank=be16(at+10),address=be16(at+12),size=be16(at+14);
    const size_t destination=bank*16384+(address==0xa000?8192:0);
    assert(size==8192&&destination+size<=raw.size()&&at+16+size<=crt.size());
    std::copy_n(crt.data()+at+16,size,raw.data()+destination);at+=16+size;
  }
  return {raw.begin()+Root,raw.end()};
}
struct Work {
  uint64_t packets=0,cells=0,sids=0,audible=0,polls=0,held=0,instructions=0,replacements=0,on=0,off=0;
} work;
static unsigned pollsPerPacket=3;
static uint8_t inputId=0,lastType=0;
static bool lastGate=false;
static std::array<uint8_t,8000> bitmap{};
static void receiveDos() {
  for(unsigned wait=0;!MPE3Title.Pending&&wait<20000;++wait)MPE3TitlePollingHndlr();
  assert(MPE3Title.Pending&&MPE5Active&&!MPE5Error);
  assert(EZFlashRAM[3]!=14);
  const unsigned length=EZFlashRAM[6]+8;
  assert(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length));
  ++work.packets;lastType=EZFlashRAM[3];
  if(lastType==1){
    work.cells+=EZFlashRAM[6]/12;work.replacements+=bool(EZFlashRAM[5]&16);
    for(unsigned at=8;at<length;at+=12){const unsigned cell=MHSNativeRead16(EZFlashRAM+at);assert(cell<1000);memcpy(bitmap.data()+cell*8,EZFlashRAM+at+2,8);}
  }
  if(lastType==2){
    ++work.sids;const bool gate=bool((EZFlashRAM[13]&1)&&(EZFlashRAM[33]&15));
    work.audible+=gate;work.on+=gate&&!lastGate;work.off+=!gate&&lastGate;lastGate=gate;
  }
  std::array<uint8_t,256> mailbox{};memcpy(mailbox.data(),EZFlashRAM,256);
  std::array<uint8_t,sizeof(MPE3TitlePacket)> packet{};memcpy(packet.data(),MPE3TitlePacket,packet.size());
  for(unsigned poll=0;poll<pollsPerPacket;++poll) {
    const uint32_t before=inst_counter;
    MPE3TitlePollingHndlr();++work.polls;work.held+=inst_counter==before;
    assert(!memcmp(mailbox.data(),EZFlashRAM,256)&&!memcmp(packet.data(),MPE3TitlePacket,packet.size()));
    assert(MPE3Title.Pending&&!MPE5Error);
  }
  writeControl(0xf6,EZFlashRAM[0xf7]);MPE3TitlePollingHndlr();
  assert(!MPE5Error);
}
static bool atPrompt() {
  const char prompt[]="C:\\>";
  if(MPE5InputPending||MPE5Keyboard.count()||MPE5TextCursor<4)return false;
  for(unsigned i=0;i<4;++i)
    if(MPE5Host.consoleShadow[2*(MPE5TextCursor-4+i)]!=uint8_t(prompt[i]))return false;
  return true;
}
static void untilPrompt() {
  for(unsigned n=0;n<20000;++n){receiveDos();if(atPrompt()&&lastType==2&&MPE5DisplayComplete&&!MPE5Graphics)return;}
  assert(!"missing DOS prompt");
}
static void sendDos(uint8_t ascii) {
  while(MPE5InputPending)receiveDos();
  if(!++inputId)++inputId;
  writeControl(0xf8,ascii);writeControl(0xf9,0);writeControl(0xfa,0);
  writeControl(0xfd,1);writeControl(0xfe,inputId);
  writeControl(0xff,uint8_t(0xa5^ascii^1^inputId));writeControl(0xf4,3);
  assert(MPE5InputPending);receiveDos();
}
static void snapshotDos(uint8_t ascii=0,uint8_t scan=0,uint8_t modifiers=0) {
  while(MPE5InputPending)receiveDos();if(!++inputId)++inputId;
  const uint8_t flags=uint8_t(0x80u|modifiers);
  writeControl(0xf8,ascii);writeControl(0xf9,scan);writeControl(0xfa,0);
  writeControl(0xfd,flags);writeControl(0xfe,inputId);
  writeControl(0xff,uint8_t(0xa5^ascii^scan^flags^inputId));writeControl(0xf4,3);
  assert(MPE5InputPending);receiveDos();
}
static void runDos(uint32_t instructions) {
  const uint32_t before=inst_counter;
  for(unsigned n=0;n<100000&&uint32_t(inst_counter-before)<instructions;++n)receiveDos();
  assert(uint32_t(inst_counter-before)>=instructions);
}
static unsigned pendingBitmapCells() {
  // Render the current mirror independently. The transported screen should
  // already contain the cave, allowing a small number of animation cells to
  // be newer than the immutable packet that the receiver just displayed.
  std::vector<uint8_t> storage(mpe5::CgaVideo::WorkspaceBytes);
  mpe5::CgaVideo expected;assert(expected.start(storage.data(),storage.size()));
  std::array<uint8_t,mpe5::CgaVideo::VramBytes> vram{};
  assert(MPE5Memory.read(mpe5::CgaTextAddress,vram.data(),vram.size()));
  expected.write(0,vram.data(),vram.size());expected.setState(mpe5::coreVideoState());
  uint8_t records[19*12];unsigned difference=0;
  while(!expected.initialComplete()) {
    const unsigned count=expected.changes(records,19);assert(count);
    for(unsigned index=0;index<count;++index) {
      const auto *record=records+index*12;const unsigned cell=record[0]+unsigned(record[1])*256;
      difference+=memcmp(bitmap.data()+cell*8,record+2,8)!=0;
    }
  }
  return difference;
}
static void measure(const char *name,uint32_t instructions) {
  work={};const uint32_t before=inst_counter;
  const auto started=std::chrono::steady_clock::now();
  for(unsigned n=0;n<100000&&(uint32_t(inst_counter-before)<instructions||lastType!=2);++n)receiveDos();
  const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  work.instructions=uint32_t(inst_counter-before);
  assert(work.instructions>=instructions&&MPE5Graphics&&!MPE5DisplayHires);
  std::cout<<name<<": instructions="<<work.instructions<<" packets="<<work.packets
    <<" cells="<<work.cells<<" SID="<<work.sids<<" audible="<<work.audible
    <<" pendingPolls="<<work.polls<<" heldPolls="<<work.held
    <<" replacementFrames="<<work.replacements
    <<" gateOn="<<work.on<<" gateOff="<<work.off
    <<" instructionsPerPacket="<<work.instructions/work.packets
    <<" hostSeconds="<<elapsed<<" hostMips="<<(work.instructions/elapsed/1000000.0)<<'\n';
}
static void powerCycle() {
  if(MPE5DiskFile.isOpen())MPE5DiskFile.close();
  MPE5Ram2Owned=false;
  MHSNativeArenaTestReset();
  MPE5Reset();
  MPE3TitleOwned=MPE3TitleStartPending=MPE3TitleSkipPending=false;
  MPE4Active=false;HostRebooted=false;
}
int main(int argc,char **argv) {
  std::signal(SIGABRT,[](int){std::_Exit(1);});
  assert(argc==4||argc==5);if(argc==5)pollsPerPacket=unsigned(std::stoul(argv[4]));
  const auto crt=readDosFile(argv[1]);auto dos=extractDos(crt);
  SD.directories.insert("/DOSVM");
  SD.files["/DOSVM/DOSVM.IMG"]=std::make_shared<std::vector<uint8_t>>(readDosFile(argv[2]));
  PSRAMAvailable=false;start(dos,Root,false);prepareDosCartridgeMemory(crt);MPE3TitlePollingHndlr();
  const auto bootStarted=std::chrono::steady_clock::now();untilPrompt();
  const auto bootSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-bootStarted).count();
  for(uint8_t c:std::string("BOULDER\r"))sendDos(c);
  std::cout<<argv[3]<<" instructionSlice="<<MPE5InstructionSlice
    <<" pendingPollsPerPacket="<<pollsPerPacket<<" hostBootSeconds="<<bootSeconds<<'\n';
  measure("title",12500000u);
  snapshotDos(' ',0x39);runDos(500000u);snapshotDos();runDos(500000u);
  snapshotDos(0,0,1);runDos(1000000u);snapshotDos();
  measure("playing",25000000u);
  assert(std::count_if(bitmap.begin(),bitmap.end(),[](uint8_t value){return value!=0;})>1000);
  const unsigned pendingCells=pendingBitmapCells();
  std::cout<<"visibleCave: currentBitmapCells="<<1000-pendingCells<<" of 1000\n";
  assert(pendingCells<=16);
  const uint32_t dataBase=mpe5_detail::readBits(mpe5_detail::readBits(0x26,2)*16u,2)*16u;
  assert(mpe5_detail::readBits(0x24,2)==0xa0&&dataBase>0x10000&&dataBase<0x80000);
  assert(mpe5_detail::readBits(dataBase+0x272e,1)==3&&mpe5_detail::readBits(dataBase+0x2544,1)==3&&mpe5_detail::readBits(dataBase+0x253a,1)==2);
  runDos(10000000u);
  assert(mpe5_detail::readBits(dataBase+0x272e,1)==3&&mpe5_detail::readBits(dataBase+0x2544,1)==3&&mpe5_detail::readBits(dataBase+0x253a,1)==2);
  std::cout<<"memory: directRam2Bytes="<<mpe5::ConventionalRamBytes<<'\n';
  // Coalescing short edges must still deliver both a sustained note and its
  // later silence. Run the shipped tone program from a separate fresh boot.
  powerCycle();start(dos,Root,false);prepareDosCartridgeMemory(crt);MPE3TitlePollingHndlr();
  untilPrompt();work={};lastGate=false;
  for(uint8_t c:std::string("PCTONE\r"))sendDos(c);untilPrompt();
  for(unsigned packet=0;packet<100&&(lastGate||MPE5Speaker.active());++packet)receiveDos();
  std::cout<<"PCTONE: audible="<<work.audible<<" gateOn="<<work.on<<" gateOff="<<work.off<<" returnedToPrompt=1\n";
  assert(work.audible&&work.on&&work.off&&!lastGate&&!MPE5Speaker.active());
}

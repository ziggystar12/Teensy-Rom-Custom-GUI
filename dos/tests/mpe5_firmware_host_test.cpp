// Execute the actual integrated M3/M4/M5 firmware with simulated SD and bus
// pins. This catches integration defects that the isolated x86 test cannot.
#define MPE4_HARNESS_MAIN unusedSierraConformance
#include "../../tests/mpe4-firmware-native-harness.cpp"
#include <csignal>

static std::vector<uint8_t> dosReadFile(const char *path) {
  std::ifstream in(path,std::ios::binary);assert(in.good());
  return {std::istreambuf_iterator<char>(in),{}};
}
static std::vector<uint8_t> dosCartridge(const char *path) {
  auto crt=dosReadFile(path);assert(crt.size()>=64&&!memcmp(crt.data(),"C64 CARTRIDGE   ",16));
  std::vector<uint8_t> raw(1048576,255);
  const auto be16=[&](size_t at){return unsigned(crt.at(at))*256+crt.at(at+1);};
  for(size_t at=64;at<crt.size();) {
    assert(!memcmp(crt.data()+at,"CHIP",4));
    unsigned bank=be16(at+10),address=be16(at+12),size=be16(at+14);
    size_t dest=bank*16384+(address==0xa000?8192:0);
    assert((address==0x8000||address==0xa000)&&size==8192&&dest+size<=raw.size());
    assert(at+16+size<=crt.size());std::copy_n(crt.data()+at+16,size,raw.data()+dest);at+=16+size;
  }
  return {raw.begin()+Root,raw.end()};
}
static std::array<bool,1000> dosSeen{};
static std::array<uint8_t,10000> dosScreen{};
static bool dosBaseComplete=false;
static unsigned dosPackets=0,dosFrames=0,dosInputs=0,sierraFrames=0;
static std::ofstream dosWire;
static void dosReceive(bool record) {
  for(unsigned n=0;!MPE3Title.Pending&&n<20000;n++)MPE3TitlePollingHndlr();
  assert(MPE3TitleOwned&&MPE3Title.Pending);
  assert(!memcmp(EZFlashRAM,"M3",2)&&EZFlashRAM[2]==1);
  const unsigned length=EZFlashRAM[6]+8;
  assert(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length));
  if(EZFlashRAM[3]==14){std::cerr<<"Firmware error "<<unsigned(EZFlashRAM[0xfb])<<"\n";std::abort();}
  if(record)tracePacket(&dosWire);
  if(EZFlashRAM[3]==1) {
    for(unsigned at=8;at<length;at+=12) {
      unsigned cell=MHSNativeRead16(EZFlashRAM+at);assert(cell<1000);dosSeen[cell]=true;
      memcpy(dosScreen.data()+cell*8,EZFlashRAM+at+2,8);
      dosScreen[8000+cell]=EZFlashRAM[at+10];dosScreen[9000+cell]=EZFlashRAM[at+11];
    }
    if(EZFlashRAM[5]&2) {
      assert(std::all_of(dosSeen.begin(),dosSeen.end(),[](bool v){return v;}));
      dosBaseComplete=true;
    }
  }
  if(EZFlashRAM[3]==2) {
    assert(dosBaseComplete);
    if(MPE5Active){assert((EZFlashRAM[5]&0x25)==0x25);dosFrames++;}
    if(MPE4Active)sierraFrames++;
  }
  // A pending publication remains byte-for-byte stable until its ACK.
  std::array<uint8_t,240> before{};memcpy(before.data(),EZFlashRAM,240);
  for(unsigned n=0;n<3;n++)MPE3TitlePollingHndlr();
  assert(!memcmp(before.data(),EZFlashRAM,240));
  if(MPE5Active)dosPackets++;
  writeControl(0xf6,EZFlashRAM[0xf7]);MPE3TitlePollingHndlr();
}
static std::string dosGuestText() {
  std::string result;
  for(unsigned cell=0;cell<1000;cell++) {
    uint8_t c=AGIPicGBC1ViewCacheMemory[mpe5::NativeTextViewportAddress+cell*2];
    result+=c>=32&&c<=126?char(c):' ';
    if(cell%40==39)result+='\n';
  }
  return result;
}
static void dosUntil(const char *text,bool record) {
  for(unsigned n=0;n<20000;n++) {
    bool currentPrompt=true;
    if(!strcmp(text,"C:\\>")) {
      currentPrompt=MPE5TextCursor>=4&&!MPE5InputPending&&!MPE5Keyboard.count();
      for(unsigned i=0;currentPrompt&&i<4;i++)
        currentPrompt=mem[mpe5::NativeTextShadowAddress+2*(MPE5TextCursor-4+i)]==uint8_t(text[i]);
    }
    // A SID is produced only after the current dirty scan is exhausted.
    if(MPE3Title.Pending&&EZFlashRAM[3]==2&&currentPrompt&&dosGuestText().find(text)!=std::string::npos) {
      uint8_t glyph[8];
      for(unsigned cell=0;cell<1000;cell++) {
        const uint8_t *guest=AGIPicGBC1ViewCacheMemory+mpe5::NativeTextViewportAddress+cell*2;
        MPE5Glyph(guest[0],glyph);
        assert(!memcmp(dosScreen.data()+cell*8,glyph,8));
        static constexpr uint8_t palette[16]={0,6,5,3,2,4,8,1,11,14,13,3,10,4,7,1};
        assert(dosScreen[8000+cell]==uint8_t(palette[guest[1]&15]<<4));
      }
      dosReceive(record);return;
    }
    dosReceive(record);
  }
  std::cerr<<dosGuestText()<<"\nMissing "<<text<<"\n";std::abort();
}
static void dosSend(uint8_t key,bool record) {
  while(MPE5InputPending)dosReceive(record);
  const uint8_t seq=uint8_t(++dosInputs);
  writeControl(0xf8,key);writeControl(0xf9,0);writeControl(0xfa,0);
  writeControl(0xfd,1);writeControl(0xfe,seq);
  writeControl(0xff,uint8_t(0xa5^key^1^seq));writeControl(0xf4,3);
  assert(MPE5InputPending&&readControl(0xfc)==seq);
  dosReceive(record);
}
static void dosResetDisplay() {dosSeen.fill(false);dosScreen.fill(0xa5);dosBaseComplete=false;}
static void dosSierra(const std::vector<uint8_t> &asset) {
  // Sierra's native engine must retain its no-PSRAM path after DOS stops.
  PSRAMAvailable=false;dosResetDisplay();
  MPE5Active=true;MPE5InputPending=true;MPE5Error=0xa5;
  start(asset);assert(!MPE5Active&&!MPE5InputPending&&MPE5Error==0);
  unsigned goal=sierraFrames+2;
  bool skipped=false;
  for(unsigned n=0;sierraFrames<goal&&n<20000;n++) {
    dosReceive(false);
    if(dosBaseComplete&&!skipped){writeControl(0xf4,2);skipped=true;}
  }
  assert(MPE4Active&&sierraFrames==goal&&!MPE5Active);
}
int main(int argc,char **argv) {
  // Keep an assertion failure in this unattended console test, without a
  // Windows crash-report dialog holding the build open.
  std::signal(SIGABRT,[](int){std::_Exit(1);});
  assert(argc==6);
  auto dos=dosCartridge(argv[1]),sierra=dosCartridge(argv[3]);
  assert(!memcmp(dos.data(),"M5D1",4)&&!memcmp(sierra.data(),"M3T1",4));
  SD.directories.insert("/DOSVM");
  SD.files["/DOSVM/DOSVM.IMG"]=std::make_shared<std::vector<uint8_t>>(dosReadFile(argv[2]));
  dosSierra(sierra);
  // Missing PSRAM must return a protocol error before touching its address.
  memset(AGIPicGBC1ViewCacheMemory,0xa5,sizeof(AGIPicGBC1ViewCacheMemory));
  start(dos,Root,false);AGIPicLayout=1;MPE3TitlePollingHndlr();
  assert(AGIPicLayout==AGIPicLayout_EasyFlash&&!MPE5Active);
  assert(MPE3Title.Pending&&EZFlashRAM[3]==14&&EZFlashRAM[0xfb]==MPE3TitleErrorMemory);
  assert(std::all_of(std::begin(AGIPicGBC1ViewCacheMemory),std::end(AGIPicGBC1ViewCacheMemory),[](uint8_t v){return v==0xa5;}));
  for(unsigned launch=0;launch<2;launch++) {
    PSRAMAvailable=true;dosResetDisplay();
    // Poison the CPU's NOLOAD execution flags on every launch.
    seg_override_en=rep_override_en=0xa5;trap_flag=int8_asap=1;inst_counter=0xa5a5a5a5;
    MPE5Active=MPE5InputPending=true;MPE5Error=0xa5;
    start(dos,Root,false);AGIPicLayout=1;MPE3TitlePollingHndlr();
    assert(AGIPicLayout==AGIPicLayout_EasyFlash&&MPE5Active&&!MPE4Active);
    assert(!MPE5InputPending&&MPE5Error==0);
    const bool record=launch==0;
    if(record){dosWire.open(argv[4],std::ios::binary);assert(dosWire.good());}
    dosUntil("C:\\>",record);
    const unsigned idleBefore=dosFrames;
    for(unsigned n=0;dosFrames<idleBefore+5&&n<20000;n++)dosReceive(record);
    assert(dosFrames>=idleBefore+5);
    for(uint8_t key:std::string(launch?"DIX\bR\r":"DIR\r"))dosSend(key,record);
    dosUntil("BOULDER  EXE",record);
    dosUntil("C:\\>",record);
    assert(dosGuestText().find("COMMAND")!=std::string::npos);
    if(record){std::ofstream screenFile(argv[5]);screenFile<<dosGuestText();dosWire.close();}
    // Switch out of the mailbox bank: stop the core, then launch Sierra again.
    CurrentEasyFlashBank=0;MPE3TitlePollingHndlr();assert(!MPE5Active&&!MPE3TitleOwned);
    dosSierra(sierra);
  }
  assert(!inputInterruptMasks);
  std::cout<<"PASS: actual integrated firmware; missing-PSRAM rejection; two dirty-state FreeDOS boots and DIR; "
           <<dosPackets<<" DOS packets, "<<dosFrames<<" hires frames, "<<dosInputs
           <<" keyboard events; Sierra cold/relaunch "<<sierraFrames<<" native frames.\n";
}

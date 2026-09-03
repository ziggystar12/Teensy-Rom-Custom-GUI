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
static unsigned swapReads=0,swapWrites=0,maxSliceIo=0;
static unsigned pendingProgress=0,pendingYields=0,canaryHolds=0,retryChecks=0;
static unsigned graphicsFrames=0,audibleFrames=0;
static uint8_t lastReceivedType=0;
static std::ofstream dosWire;
static void dosHoldPending(unsigned polls,bool healthy=true) {
  assert(MPE5Active&&MPE3Title.Pending);
  std::array<uint8_t,256> mailbox{};
  std::array<uint8_t,sizeof(MPE3TitlePacket)> packet{};
  memcpy(mailbox.data(),EZFlashRAM,mailbox.size());
  memcpy(packet.data(),MPE3TitlePacket,packet.size());
  const auto sequence=MPE3Title.Sequence;
  const auto fixedMemory=MPE5Host.fixedF000;
  const bool first=MPE5FirstFrame;
  for(unsigned n=0;n<polls;n++) {
    const unsigned instructions=inst_counter;
    const auto paging=MPE5Memory.stats();
    MPE3TitlePollingHndlr();
    assert(!memcmp(mailbox.data(),EZFlashRAM,mailbox.size()));
    assert(!memcmp(packet.data(),MPE3TitlePacket,packet.size()));
    assert(MPE3Title.Pending&&MPE3Title.Sequence==sequence);
    assert(MPE5Active&&MPE5Host.fixedF000==fixedMemory);
    if(first) {
      assert(inst_counter==instructions);
      assert(MPE5Memory.stats().pageReads==paging.pageReads);
      assert(MPE5Memory.stats().pageWrites==paging.pageWrites);
      canaryHolds++;
    } else {
      pendingProgress+=inst_counter!=instructions;
      maxSliceIo=std::max(maxSliceIo,unsigned(MPE5SliceIo));
      if(MPE5SliceIo>=4&&!MPE5Error) {
        // A storage-budget yield retains the running guest and its cache.
        assert(MPE5Ready&&MPE3Title.Loaded);
        assert(MPE5Memory.stats().hits>=paging.hits);
        pendingYields++;
      }
    }
    if(healthy)assert(!MPE5Error&&!MPE5Memory.failed()&&MPE5Ready);
  }
}

static void dosTransientPageIO() {
  // Exercise the real callbacks, including a short transfer that advances
  // the file cursor. A successful retry must re-seek and transfer all 512.
  const unsigned page=mpe5::PagedMemory::PageCount-1;
  const size_t offset=size_t(page)*512;
  auto &swap=*SD.files.at("/DOSVM/DOSVM.SWP");
  std::array<uint8_t,512> original{},source{},result{};
  memcpy(original.data(),swap.data()+offset,512);
  for(unsigned i=0;i<512;i++)source[i]=uint8_t(i*37u+19u);
  const auto size=swap.size();
  const unsigned oldRetries=MPE5PageRetries;
  for(unsigned fault=0;fault<4;fault++) {
    const bool write=fault<2;
    if(fault==0)MPE5SwapFile.shortWrites=1;
    else if(fault==2)MPE5SwapFile.shortReads=1;
    else MPE5SwapFile.failedSeeks=1;
    MPE5SliceIo=0;
    assert(write?MPE5WritePage(nullptr,page,source.data()):MPE5ReadPage(nullptr,page,result.data()));
    assert(MPE5SliceIo==2&&MPE5PageRetries==oldRetries+fault+1);
    assert(!MPE5PageError&&!MPE5Memory.failed()&&swap.size()==size);
    assert(!memcmp(source.data(),write?swap.data()+offset:result.data(),512));
    assert(!MPE5SwapFile.shortReads&&!MPE5SwapFile.shortWrites&&!MPE5SwapFile.failedSeeks);
    retryChecks++;
  }
  memcpy(swap.data()+offset,original.data(),512);
}

static void dosReceive(bool record) {
  for(unsigned n=0;!MPE3Title.Pending&&n<20000;n++)MPE3TitlePollingHndlr();
  assert(MPE3TitleOwned&&MPE3Title.Pending);
  assert(!memcmp(EZFlashRAM,"M3",2)&&EZFlashRAM[2]==1);
  const unsigned length=EZFlashRAM[6]+8;
  assert(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length));
  if(EZFlashRAM[3]==14){std::cerr<<"Firmware error "<<unsigned(EZFlashRAM[0xfb])<<"\n";std::abort();}
  if(record)tracePacket(&dosWire);
  lastReceivedType=EZFlashRAM[3];
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
    if(MPE5Active){
      assert((EZFlashRAM[5]&0x21)==0x21&&EZFlashRAM[6]==27);
      assert(bool(EZFlashRAM[5]&4)==MPE5DisplayHires);
      assert(EZFlashRAM[34]==MPE5DisplayBackground);
      dosFrames++;graphicsFrames+=MPE5Graphics;
      audibleFrames+=(EZFlashRAM[13]&1)&&(EZFlashRAM[33]&15);
    }
    if(MPE4Active)sierraFrames++;
  }
  // A pending publication remains byte-for-byte stable until its ACK.
  if(MPE5Active)dosHoldPending(3);
  else {
    std::array<uint8_t,240> before{};memcpy(before.data(),EZFlashRAM,240);
    const unsigned frames=MPE4Active?MPE4Game->frames:0;
    for(unsigned n=0;n<3;n++)MPE3TitlePollingHndlr();
    assert(!memcmp(before.data(),EZFlashRAM,240));
    if(MPE4Active)assert(MPE4Game->frames==frames);
  }
  if(MPE5Active){dosPackets++;maxSliceIo=std::max(maxSliceIo,unsigned(MPE5SliceIo));}
  writeControl(0xf6,EZFlashRAM[0xf7]);MPE3TitlePollingHndlr();
}
static std::string dosGuestText() {
  std::string result;
  for(unsigned cell=0;cell<1000;cell++) {
    uint8_t c=MPE5PublishedViewport[cell*2];
    result+=c>=32&&c<=126?char(c):' ';
    if(cell%40==39)result+='\n';
  }
  return result;
}
static void dosUntil(const char *text,bool record,unsigned limit=20000) {
  for(unsigned n=0;n<limit;n++) {
    bool currentPrompt=true;
    if(!strcmp(text,"C:\\>")) {
      currentPrompt=MPE5TextCursor>=4&&!MPE5InputPending&&!MPE5Keyboard.count();
      for(unsigned i=0;currentPrompt&&i<4;i++)
        currentPrompt=MPE5Host.consoleShadow[2*(MPE5TextCursor-4+i)]==uint8_t(text[i]);
    }
    // A pending SID can outlive its guest snapshot while DOS keeps running.
    // Require the wire display to converge to the current private viewport.
    if(MPE3Title.Pending&&EZFlashRAM[3]==2&&currentPrompt&&dosGuestText().find(text)!=std::string::npos) {
      uint8_t glyph[8];
      bool matches=true;
      for(unsigned cell=0;cell<1000;cell++) {
        const uint8_t *guest=MPE5PublishedViewport+cell*2;
        MPE5Glyph(guest[0],glyph);
        matches&=!memcmp(dosScreen.data()+cell*8,glyph,8);
        static constexpr uint8_t palette[16]={0,6,5,3,2,4,8,1,11,14,13,3,10,4,7,1};
        matches&=dosScreen[8000+cell]==uint8_t(palette[guest[1]&15]<<4);
      }
      if(matches){dosReceive(record);return;}
    }
    dosReceive(record);
  }
  std::cerr<<dosGuestText()<<"\nMissing "<<text<<" at "<<std::hex<<regs16[REG_CS]<<":"<<reg_ip
    <<" CX="<<regs16[REG_CX]<<" DX="<<regs16[REG_DX]<<std::dec
    <<" instructions="<<inst_counter<<" speaker="<<MPE5Speaker.revision()
    <<" sent="<<MPE5SpeakerRevision<<"\n";std::abort();
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
static void dosSnapshot(uint8_t ascii,uint8_t scan,uint8_t modifiers,uint8_t joystick,bool record) {
  while(MPE5InputPending)dosReceive(record);
  uint8_t seq=uint8_t(++dosInputs);if(!seq)seq=uint8_t(++dosInputs);
  const uint8_t flags=uint8_t(0x80u|modifiers);
  writeControl(0xf8,ascii);writeControl(0xf9,scan);writeControl(0xfa,joystick);
  writeControl(0xfd,flags);writeControl(0xfe,seq);
  writeControl(0xff,uint8_t(0xa5^ascii^scan^joystick^flags^seq));writeControl(0xf4,3);
  assert(MPE5InputPending&&readControl(0xfc)==seq);dosReceive(record);
}
static void dosInstructions(uint32_t count,bool record) {
  const uint32_t begin=inst_counter;
  for(unsigned packet=0;packet<30000&&
      (uint32_t(inst_counter-begin)<count||lastReceivedType!=2);++packet)dosReceive(record);
  assert(uint32_t(inst_counter-begin)>=count);
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
  assert(argc==7);
  auto dos=dosCartridge(argv[1]),sierra=dosCartridge(argv[3]);
  const auto completeDos=dosReadFile(argv[1]);
  assert(!memcmp(dos.data(),"M5D1",4)&&!memcmp(sierra.data(),"M3T1",4));
  SD.directories.insert("/DOSVM");
  SD.files["/DOSVM/DOSVM.IMG"]=std::make_shared<std::vector<uint8_t>>(dosReadFile(argv[2]));
  SD.files["/DOSVM/DOSVM.SWP"]=std::make_shared<std::vector<uint8_t>>(dosReadFile(argv[6]));
  assert(SD.files["/DOSVM/DOSVM.SWP"]->size()==MPE5SwapBytes);
  const auto originalDisk=*SD.files["/DOSVM/DOSVM.IMG"];
  dosSierra(sierra);
  // A failed read/write scratch-file open must report a recoverable error.
  SD.failWritePath="/DOSVM/DOSVM.SWP";
  start(dos,Root,false);prepareDosCartridgeMemory(completeDos);AGIPicLayout=1;MPE3TitlePollingHndlr();
  assert(!MPE5Active&&MPE3Title.Pending&&EZFlashRAM[3]==14&&EZFlashRAM[0xfb]==MPE3TitleErrorRead);
  SD.failWritePath.clear();
  // A corrupt resident-chip pointer must never lend cartridge data to DOS.
  start(dos,Root,false);prepareDosCartridgeMemory(completeDos);CrtChips[2].ChipROM++;MPE3TitlePollingHndlr();
  assert(!MPE5Active&&MPE3Title.Pending&&EZFlashRAM[3]==14&&EZFlashRAM[0xfb]==MPE3TitleErrorMemory);
  // The optional EXTMEM arena remains poisoned and inaccessible throughout
  // both boots. Old scratch-file bytes must not appear as new guest memory.
  memset(AGIPicGBC1ViewCacheMemory,0xa5,sizeof(AGIPicGBC1ViewCacheMemory));
  for(unsigned launch=0;launch<2;launch++) {
    PSRAMAvailable=false;dosResetDisplay();
    std::fill(SD.files["/DOSVM/DOSVM.SWP"]->begin(),SD.files["/DOSVM/DOSVM.SWP"]->end(),0xa5);
    // Poison the CPU's NOLOAD execution flags on every launch.
    seg_override_en=rep_override_en=0xa5;trap_flag=int8_asap=1;inst_counter=0xa5a5a5a5;
    MPE5Active=MPE5InputPending=true;MPE5Error=0xa5;
    start(dos,Root,false);prepareDosCartridgeMemory(completeDos);AGIPicLayout=1;
    const std::vector<uint8_t> prefix(RAM_Image,RAM_Image+3*8192);
    MPE3TitlePollingHndlr();
    assert(AGIPicLayout==AGIPicLayout_EasyFlash&&MPE5Active&&!MPE4Active);
    assert(!MPE5InputPending&&MPE5Error==0);
    dosHoldPending(8);
    if(!launch)dosTransientPageIO();
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
    assert(!memcmp(prefix.data(),RAM_Image,prefix.size()));
    assert(std::all_of(std::begin(AGIPicGBC1ViewCacheMemory),std::end(AGIPicGBC1ViewCacheMemory),[](uint8_t v){return v==0xa5;}));
    assert(*SD.files["/DOSVM/DOSVM.IMG"]==originalDisk);
    assert(!MPE5Memory.failed());
    const auto paging=MPE5Memory.stats();
    assert(paging.pageReads&&paging.pageWrites&&paging.evictions&&!paging.ioFailures);
    swapReads+=paging.pageReads;swapWrites+=paging.pageWrites;
    if(record){std::ofstream screenFile(argv[5]);screenFile<<dosGuestText();dosWire.close();}
    // Switch out of the mailbox bank: stop the core, then launch Sierra again.
    CurrentEasyFlashBank=0;MPE3TitlePollingHndlr();assert(!MPE5Active&&!MPE3TitleOwned);
    dosSierra(sierra);
  }
  assert(pendingProgress&&pendingYields&&canaryHolds&&retryChecks==4);
  // Run the shipped Boulder executable through the integrated CPU, pager,
  // video observer and packet publisher. Capture the actual wire for 6510
  // replay, rather than manufacturing packets from a separate renderer.
  dosResetDisplay();
  start(dos,Root,false);prepareDosCartridgeMemory(completeDos);MPE3TitlePollingHndlr();
  const std::string directory=std::string(argv[4]).substr(0,std::string(argv[4]).find_last_of("/\\")+1);
  dosWire.open(directory+"boulder-wire.bin",std::ios::binary);assert(dosWire.good());
  dosUntil("C:\\>",true);
  const unsigned soundBefore=audibleFrames;
  for(uint8_t key:std::string("PCTONE\r"))dosSend(key,true);
  dosUntil("C:\\>",true,1000);
  assert(audibleFrames>soundBefore&&!MPE5Speaker.active());
  for(uint8_t key:std::string("BOULDER\r"))dosSend(key,true);
  const auto beforeGraphics=graphicsFrames;
  const unsigned titleStart=inst_counter;
  for(unsigned packet=0;packet<30000&&
      (unsigned(inst_counter-titleStart)<12500000u||lastReceivedType!=2);packet++)dosReceive(true);
  assert(graphicsFrames>beforeGraphics&&MPE5Graphics&&!MPE5DisplayHires);
  assert(!MPE5Error&&!MPE5Memory.failed());
  // Space skips the introduction; Shift starts the selected cave. Exercise
  // held states and releases through the actual cartridge input mailbox.
  dosSnapshot(' ',0x39,0,0,true);dosInstructions(500000u,true);
  dosSnapshot(0,0,0,0,true);dosInstructions(500000u,true);
  dosSnapshot(0,0,1,0,true);dosInstructions(1000000u,true);
  dosSnapshot(0,0,0,0,true);
  const unsigned caveStart=inst_counter;
  for(unsigned packet=0;packet<30000&&
      (unsigned(inst_counter-caveStart)<25000000u||lastReceivedType!=2);packet++)dosReceive(true);
  assert(unsigned(inst_counter-caveStart)>=25000000u);
  // Work coalescing intentionally reduces SID/graphics packet counts. The
  // acceptance condition is a playable cave, not redundant frame traffic.
  assert(graphicsFrames>beforeGraphics);
  assert(std::count_if(dosScreen.begin(),dosScreen.begin()+8000,[](uint8_t v){return v!=0;})>1000);
  const uint32_t boulderCode=mpe5_detail::readBits(0x26,2)*16u;
  const uint32_t boulderData=mpe5_detail::readBits(boulderCode,2)*16u;
  const auto guestByte=[&](uint32_t offset){return mpe5_detail::readBits(boulderData+offset,1);};
  assert(mpe5_detail::readBits(0x24,2)==0xa0&&boulderData>0x10000&&boulderData<0x80000);
  assert(guestByte(0x272e)==3&&guestByte(0x2544)==3&&guestByte(0x253a)==2);
  const unsigned playerY=guestByte(0x253a);
  dosSnapshot(0,0x50,0,0,true);dosInstructions(1000000u,true);
  assert(guestByte(0x253a)>playerY);
  dosSnapshot(0,0,0,0,true);dosInstructions(1000000u,true);
  const unsigned stoppedY=guestByte(0x253a);dosInstructions(1000000u,true);
  assert(guestByte(0x253a)==stoppedY&&guestByte(0x272e)==3&&!guestByte(0x12c3));
  dosWire.close();
  {std::ofstream planes(directory+"boulder-firmware-planes.bin",std::ios::binary);
   planes.write(reinterpret_cast<const char*>(dosScreen.data()),dosScreen.size());}
  {std::ofstream metadata(directory+"boulder-frame.json");
   metadata<<"{\"hires\":"<<(MPE5DisplayHires?"true":"false")
     <<",\"background\":"<<unsigned(MPE5DisplayBackground)
     <<",\"graphicsFrames\":"<<graphicsFrames<<",\"audibleFrames\":"<<audibleFrames<<"}\n";}
  CurrentEasyFlashBank=0;MPE3TitlePollingHndlr();dosSierra(sierra);
  // A stopped CPU cannot overwrite an unacknowledged publication. Only its
  // ACK may publish the typed error and the CS:IP captured at the failure.
  dosResetDisplay();
  start(dos,Root,false);prepareDosCartridgeMemory(completeDos);MPE3TitlePollingHndlr();
  dosReceive(false);assert(!MPE5FirstFrame);
  regs16[REG_CS]=0;reg_ip=0;MPE5RepeatPending=MPE5DiskPending=false;
  dosHoldPending(1,false);
  assert(MPE5Error==0x41&&EZFlashRAM[3]!=14&&EZFlashRAM[0xfb]==0);
  const unsigned stoppedInstructions=inst_counter;
  dosHoldPending(3,false);assert(inst_counter==stoppedInstructions);
  writeControl(0xf6,EZFlashRAM[0xf7]);MPE3TitlePollingHndlr();
  assert(MPE3Title.Pending&&EZFlashRAM[3]==14&&EZFlashRAM[0xfb]==0x41);
  assert(!EZFlashRAM[0xfc]&&!EZFlashRAM[0xfd]&&!EZFlashRAM[0xfe]&&!EZFlashRAM[0xff]);
  CurrentEasyFlashBank=0;MPE3TitlePollingHndlr();dosSierra(sierra);

  // A full/read-only SD card must also defer its precise write diagnostic
  // until ACK, then release all borrowed state when the cartridge exits.
  dosResetDisplay();
  start(dos,Root,false);prepareDosCartridgeMemory(completeDos);MPE3TitlePollingHndlr();
  dosReceive(false);assert(!MPE5FirstFrame);
  StorageWriteBudget=0;
  for(unsigned poll=0;poll<3000&&!MPE5Error;poll++)dosHoldPending(1,false);
  assert(MPE5Error==0x4a&&MPE5PageError==0x4a&&EZFlashRAM[3]!=14&&EZFlashRAM[0xfb]==0);
  const auto failed=mpe5::coreDiagnostic();
  const uint32_t failedOffset=MPE5FailedPage*512u;
  writeControl(0xf6,EZFlashRAM[0xf7]);MPE3TitlePollingHndlr();
  assert(MPE3Title.Pending&&EZFlashRAM[3]==14&&EZFlashRAM[0xfb]==0x4a);
  assert(uint32_t(EZFlashRAM[0xf8])+(uint32_t(EZFlashRAM[0xf9])<<8)+(uint32_t(EZFlashRAM[0xfa])<<16)==failedOffset);
  assert(MHSNativeRead16(EZFlashRAM+0xfc)==failed.cs&&MHSNativeRead16(EZFlashRAM+0xfe)==failed.ip);
  StorageWriteBudget=size_t(-1);
  CurrentEasyFlashBank=0;MPE3TitlePollingHndlr();
  assert(!MPE5Active&&!MPE5DiskFile&&!MPE5SwapFile&&!MPE5PublishedViewport);
  assert(!inputInterruptMasks);
  std::cout<<"PASS: actual integrated firmware with no PSRAM; scratch-file failure and cartridge-bounds rejection; two dirty-state FreeDOS boots and DIR; "
           <<dosPackets<<" DOS packets, "<<dosFrames<<" display frames, "<<dosInputs
           <<" keyboard events; "<<swapReads<<" swap reads, "<<swapWrites<<" swap writes, max "
           <<maxSliceIo<<" SD operations/slice; "<<pendingProgress<<" pending CPU advances, "
           <<pendingYields<<" retained storage yields, "<<canaryHolds<<" canary holds; "
           <<retryChecks<<" transient page I/O recoveries; stopped/write errors deferred until ACK; "
           <<graphicsFrames<<" Boulder CGA frames, "<<audibleFrames<<" audible SID frames; "
           <<"Sierra cold/relaunch "<<sierraFrames<<" native frames.\n";
}

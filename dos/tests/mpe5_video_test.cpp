#define main existingDosAcceptanceMain
#include "mpe5_vm_host_test.cpp"
#undef main
#include <array>

namespace {

void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

struct VideoFixture {
  std::vector<uint8_t> storage = std::vector<uint8_t>(mpe5::CgaVideo::WorkspaceBytes + 64, 0xa5);
  mpe5::CgaVideo video;
  VideoFixture() { check(video.start(storage.data() + 32, mpe5::CgaVideo::WorkspaceBytes), "video workspace rejected"); }
  void guards() const {
    check(std::all_of(storage.begin(), storage.begin() + 32, [](uint8_t v) { return v == 0xa5; }) &&
          std::all_of(storage.end() - 32, storage.end(), [](uint8_t v) { return v == 0xa5; }),
          "video touched workspace guards");
  }
  static void observe(void *context, uint16_t offset, const uint8_t *bytes, uint16_t length) {
    static_cast<VideoFixture *>(context)->video.write(offset, bytes, length);
  }
  void attach() { mpe5::coreSetVideoObserver({this, observe}); }
};

void apply(const uint8_t *records, uint16_t count, std::vector<uint8_t> &planes) {
  check(count <= 19, "video exceeded19 records per packet");
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t *record = records + i * 12u;
    const uint16_t cell = uint16_t(record[0] | uint16_t(record[1]) << 8);
    check(cell < 1000, "video produced an invalid cell");
    std::copy_n(record + 2, 8, planes.begin() + cell * 8u);
    planes[8000 + cell] = record[10]; planes[9000 + cell] = record[11];
  }
}

std::vector<uint8_t> frame(mpe5::CgaVideo &video) {
  std::vector<uint8_t> planes(10000);
  std::array<bool,1000> seen{};
  uint8_t records[19 * 12];
  unsigned unique = 0;
  const bool initial = !video.initialComplete();
  for (unsigned batch = 0; batch < 1100; ++batch) {
    const bool wasInitial = !video.initialComplete();
    const uint16_t count = video.changes(records, 19);
    if (!count) {
      check(!initial || (video.initialComplete() && unique == 1000), "initial CGA frame omitted cells");
      return planes;
    }
    if (wasInitial) for (uint16_t i = 0; i < count; ++i) {
      const uint16_t cell = uint16_t(records[i*12] | uint16_t(records[i*12+1]) << 8);
      check(cell < 1000 && !seen[cell], "CGA initial frame repeated a cell"); seen[cell] = true; ++unique;
    }
    apply(records,count,planes);
  }
  throw std::runtime_error("video did not finish a bounded frame");
}

void verifyVideoCells() {
  VideoFixture f;
  mpe5::VideoState state{4,0x0a,0x30,0,true};
  check(f.video.setState(state) && f.video.graphics() && !f.video.hires(), "mode4 was not multicolor");
  const uint8_t pixels[] = {0x1b,0xe4};
  f.video.write(0,pixels,2); f.video.write(8192,pixels,2);
  auto planes = frame(f.video);
  check(planes[0] == 0x7d && planes[1] == 0x7d && planes[8000] == 0x34 && planes[9000] == 1,
        "CGA bank interleave, pair downsampling or default palette is wrong");
  check(planes[2] == 0 && f.video.background() == 0, "zero CGA scanline is not blank");
  uint8_t records[19*12];
  f.video.write(0,pixels,2);
  check(!f.video.changes(records,19), "unchanged VRAM produced a dirty packet");

  // Updates to an already-sent initial cell are retained for a later sweep,
  // without replacing or duplicating any of the1000 initial positions.
  f.video.reset(); f.video.setState(state);
  check(f.video.changes(records,19) == 19, "first bounded CGA batch missing");
  std::array<bool,1000> seen{}; for (unsigned i=0;i<19;++i) seen[i]=true;
  const uint8_t white = 0xff; f.video.write(0,&white,1);
  unsigned total=19;
  while (!f.video.initialComplete()) {
    const uint16_t count=f.video.changes(records,19); check(count>0, "CGA initial traversal stalled");
    for(unsigned i=0;i<count;++i) {
      const uint16_t cell=uint16_t(records[i*12]|uint16_t(records[i*12+1])<<8);
      check(cell<1000 && !seen[cell], "early VRAM edit duplicated initial cell"); seen[cell]=true; ++total;
    }
  }
  check(total==1000 && f.video.changes(records,19)==1 && records[0]==0 && records[1]==0 && records[2]==0xf0,
        "initial-frame VRAM edit was lost");
  f.video.write(16191,&white,1);
  check(f.video.changes(records,19)==1 && records[0]==0xe7 && records[1]==3 && records[9]==0x0f,
        "last odd-bank pixel did not dirty cell999");
  const uint8_t padding=0xa5; f.video.write(8191,&padding,1);
  check(!f.video.changes(records,19), "CGA bank padding dirtied visible cells");

  state.colorSelect=0x04; f.video.setState(state); planes=frame(f.video);
  check(f.video.background()==2 && planes[8000]==0x52 && planes[9000]==8,
        "CGA palette0/background selection failed");
  state.colorSelect=0x10; f.video.setState(state); planes=frame(f.video);
  check(planes[8000]==0xda && planes[9000]==7, "CGA intensity failed");
  state.mode=5; state.control=0x0e; state.colorSelect=0x30; f.video.setState(state); planes=frame(f.video);
  check(planes[8000]==0x3a && planes[9000]==1, "mode5 cyan/red/white palette failed");

  f.video.reset(); state={6,0x1a,15,0,true}; f.video.setState(state);
  const uint8_t mono[]={0x80,0x01,0x55,0xaa}; f.video.write(0,mono,4); planes=frame(f.video);
  check(f.video.hires() && planes[0]==0x81 && planes[8]==0xff && planes[8000]==0x10,
        "mode6 pair OR discarded a one-pixel stroke");
  state.startAddress=1; f.video.setState(state); planes=frame(f.video);
  check(planes[0]==0xff, "CRTC display-start word was not honored");
  state.enabled=false; f.video.setState(state); planes=frame(f.video);
  check(std::all_of(planes.begin(),planes.end(),[](uint8_t v){return v==0;}) && !f.video.background(),
        "disabled CGA display was not blanked");
  state.mode=3; f.video.setState(state);
  check(!f.video.graphics() && !f.video.changes(records,19), "text mode emitted graphics packets");
  f.guards();
  std::cout<<"CGA cells PASS: bounded unique coverage, retained edits, both banks, palettes/background, mode6 strokes, display start/blanking.\n";
}

using IndexedCell = std::array<uint8_t,64>;

void writeIndexedCell(VideoFixture &f, const mpe5::VideoState &state, uint16_t cell,
                      const IndexedCell &pixels) {
  // Independent CGA addressing: each group of four horizontal pixels is one
  // byte, with even and odd scanlines stored in separate 8 KiB banks.
  for (unsigned y=0; y<8; ++y) for (unsigned byte=0; byte<2; ++byte) {
    uint8_t packed=0;
    for (unsigned x=0; x<4; ++x) packed=uint8_t((packed<<2)|pixels[y*8+byte*4+x]);
    const unsigned screenY=(cell/40)*8+y, screenX=(cell%40)*8+byte*4;
    const uint16_t address=uint16_t((screenY%2)*8192+
      (unsigned(state.startAddress)*2+(screenY/2)*80+screenX/4)%8192);
    f.video.write(address,&packed,1);
  }
}

void expectHiresCell(const std::vector<uint8_t>& planes, uint16_t cell,
                     const IndexedCell &source, const std::array<uint8_t,4> &colors) {
  const uint8_t attributes=planes[8000+cell];
  for (unsigned y=0; y<8; ++y) for (unsigned x=0; x<8; ++x) {
    const uint8_t actual=planes[cell*8+y]&(0x80u>>x) ? attributes>>4 : attributes&15;
    if (actual!=colors[source[y*8+x]]) throw std::runtime_error(
      "sharp cell "+std::to_string(cell)+" pixel "+std::to_string(x)+","+std::to_string(y)+
      " expected color "+std::to_string(colors[source[y*8+x]])+" got "+std::to_string(actual));
  }
}

void verifySharpVideo() {
  VideoFixture f;
  check(!f.video.sharp() && !f.video.setSharp(false), "sharp policy was not default-off");
  struct PaletteCase { mpe5::VideoState state; std::array<uint8_t,4> colors; };
  const PaletteCase palettes[]={
    {{4,0x0a,0x30,0,true},{0,3,4,1}},
    {{4,0x0a,0x04,0,true},{2,5,2,8}},
    {{4,0x0a,0x10,0,true},{0,13,10,7}},
    {{4,0x0a,0x23,0,true},{3,3,4,15}},
    {{5,0x0e,0x30,0,true},{0,3,10,1}},
    {{4,0x0e,0x1f,0,true},{1,3,10,1}}
  };
  const uint8_t letter[8]={0x81,0xc3,0xa5,0x99,0x81,0x81,0x81,0x00};
  check(f.video.setSharp(true) && f.video.sharp(), "sharp policy did not enable");
  for (const auto &palette:palettes) for (unsigned background=0;background<4;++background)
    for (unsigned foreground=0;foreground<4;++foreground) {
      auto state=palette.state;
      // Exercise first/last cells and a display origin wrapping both banks.
      state.startAddress=background&1 ? 0xffff : 0;
      const uint16_t cell=foreground&1 ? 999 : 0;
      f.video.setState(state);
      // State may be unchanged; toggling presentation must request all cells.
      f.video.setSharp(false); f.video.setSharp(true);
      IndexedCell pixels{};
      for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x)
        pixels[y*8+x]=letter[y]&(0x80u>>x) ? foreground : background;
      writeIndexedCell(f,state,cell,pixels);
      const auto planes=frame(f.video);
      check(f.video.hires() && f.video.sharp(), "guest state update lost sharp presentation");
      expectHiresCell(planes,cell,pixels,palette.colors);
    }

  // Four distinct guest entries can still mean only two output colors.
  // Duplicate colors must be combined rather than displacing a used color.
  mpe5::VideoState state{4,0x0a,0x23,0,true}; f.video.setState(state);
  IndexedCell duplicates{};
  for (unsigned n=0;n<64;++n) duplicates[n]=n%3;
  writeIndexedCell(f,state,0,duplicates);
  expectHiresCell(frame(f.video),0,duplicates,{3,3,4,15});

  // Equal populations select the first two CGA indices. For palette1 bright,
  // both magenta and white are closer to cyan than black in fixed RGB space.
  state={4,0x0a,0x30,0,true}; f.video.setState(state);
  IndexedCell fourColors{};
  for (unsigned n=0;n<64;++n) fourColors[n]=n%4;
  writeIndexedCell(f,state,0,fourColors);
  auto planes=frame(f.video);
  check(planes[8000]==0x30, "sharp palette tie was not stable lowest-index priority");
  expectHiresCell(planes,0,fourColors,{0,3,3,3});

  // Dominant black/white preserves these colors; the remaining magenta is mapped
  // to black, cyan to white. This exercises both nearest-color outcomes.
  state={4,0x0a,0x30,1,true}; f.video.setState(state);
  IndexedCell reduced{};
  for (unsigned n=0;n<64;++n) reduced[n]=n<28 ? 0 : n<56 ? 3 : n<60 ? 1 : 2;
  writeIndexedCell(f,state,0,reduced);
  planes=frame(f.video);
  expectHiresCell(planes,0,reduced,{0,1,0,1});

  f.video.setSharp(false);
  const auto colorBefore=frame(f.video);
  check(!f.video.hires() && !f.video.setSharp(false) && f.video.initialComplete(),
        "unchanged sharp preference restarted a color frame");
  f.video.setSharp(true);
  check(!f.video.initialComplete(), "sharp toggle did not request a complete repaint");
  frame(f.video); f.video.setSharp(false);
  check(frame(f.video)==colorBefore, "sharp toggle changed the default color output");

  state={6,0x1a,15,0,true}; f.video.setState(state);
  const uint8_t mono[]={0x80,0x01,0x55,0xaa}; f.video.write(0,mono,4);
  const auto monoBefore=frame(f.video); f.video.setSharp(true);
  check(frame(f.video)==monoBefore && f.video.hires(), "sharp preference altered mode6 reduction");
  state.enabled=false; f.video.setState(state); frame(f.video);
  state.mode=4; f.video.setState(state); planes=frame(f.video);
  check(std::all_of(planes.begin(),planes.end(),[](uint8_t v){return v==0;}),
        "disabled sharp display retained color or bitmap data");
  state.mode=3; f.video.setState(state);
  check(!f.video.hires() && !f.video.graphics() && f.video.sharp(),
        "sharp preference changed guest text mode");
  f.video.reset();
  check(!f.video.sharp(), "reset retained sharp preference");
  f.guards();
  std::cout<<"Sharp CGA PASS:96 exact two-color letters, reversed/nonzero backgrounds, duplicate colors, both banks/start wrap, deterministic reduction, toggles, mode6 and reset.\n";
}

void verifyCoreVideo(const std::vector<uint8_t>& bios, Image& image) {
  PagedMachine machine; machine.start(bios,image); VideoFixture f; f.attach();
  machine.program({0xc7,0x06,0xff,0x01,0x34,0x12}); regs16[REG_DS]=0xb800;
  check(machine.run(1), "VRAM word store stopped");
  check(f.storage[32+511]==0x34 && f.storage[32+512]==0x12, "cross-page CPU word write did not reach video mirror");
  machine.program({0xf3,0xa4,0xeb,0xfe}); regs16[REG_ES]=0xb800;
  regs16[REG_SI]=0; regs16[REG_DI]=8191; regs16[REG_CX]=4;
  const uint8_t copy[]={0xaa,0x55,0x81,0x18};
  check(machine.pager.write(0x20000,copy,sizeof(copy)) && machine.run(1), "REP video fixture failed");
  do {check(machine.run(1), "bounded REP VRAM write failed");} while(MPE5RepeatPending);
  check(std::equal(copy,copy+4,f.storage.begin()+32+8191), "REP bank-crossing write did not reach video mirror");
  const uint8_t crossing[]={1,2,3,4}; mpe5_detail::writeBytes(0xb7fff,crossing,4);
  check(f.storage[32]==2 && f.storage[33]==3 && f.storage[34]==4, "VRAM observer did not clip an overlapping span");

  mpe5_detail::writeBits(0x449,1,4);
  auto state=mpe5::coreVideoState();
  check(state.mode==4 && state.colorSelect==0x30 && state.enabled, "BIOS CGA mode default was not observed");
  MPE5_PORT(0x3d8)=0x1a; MPE5_PORT(0x3d9)=2;
  mpe5_detail::writeBits(mpe5::AddressMapBytes+0x3d4,2,0x120c);
  MPE5_PORT(0x3d4)=13; MPE5_PORT(0x3d5)=0x34;
  state=mpe5::coreVideoState();
  check(state.mode==6 && state.colorSelect==2 && state.startAddress==0x1234,
        "CGA port state or CRTC word writes were not observed");
  const auto before=machine.pager.stats();
  for(unsigned i=0;i<100;++i) state=mpe5::coreVideoState();
  const auto after=machine.pager.stats();
  check(before.hits==after.hits && before.misses==after.misses, "video-state polling touched the guest cache");
  f.guards(); mpe5::coreReset();
  check(mpe5::coreVideoState().mode==0, "core reset retained a graphics mode");
  std::cout<<"CPU video hooks PASS: word/REP/span writes and CGA register state without cache reads.\n";
}

uint32_t clockValue = 0;
uint32_t testClock() { return clockValue; }

void verifyBiosTimer(const std::vector<uint8_t>& bios, Image& image) {
  PagedMachine machine; machine.start(bios,image); machine.until("C:\\>",true);
  // Install an actual guest INT1C countdown handler and allow the BIOS INT0A
  // clock conversion to invoke it. No host test decrements this guest value.
  const uint8_t handler[]={0x2e,0xff,0x0e,0x00,0x04,0xcf}; // DEC CS:[0400]; IRET
  check(machine.pager.write(0x10300,handler,sizeof(handler)),"timer handler write failed");
  mpe5_detail::writeBits(0x70,2,0x300); mpe5_detail::writeBits(0x72,2,0x1000);
  mpe5_detail::writeBits(0x10400,2,5);
  machine.program({0xfb,0xeb,0xfd});
  bool ticked=false;
  for(unsigned slice=0;slice<1000;++slice) {
    check(machine.run(5000),"BIOS countdown core stopped");
    if(mpe5_detail::readBits(0x10400,2)<5) {ticked=true;break;}
  }
  check(ticked,"BIOS clock never dispatched the guest INT1C countdown");

  machine.start(bios,image);
  inst_counter=0xfffffff5u;
  check(mpe5_detail::writeRtc(0x60000),"virtual-clock baseline failed");
  const uint32_t beforeWrap=mpe5_detail::readBits(0x60024,2);
  inst_counter=20;
  check(mpe5_detail::writeRtc(0x60000) && mpe5_detail::readBits(0x60024,2)==beforeWrap,
        "32-bit instruction wrap reset the deterministic guest clock");
  inst_counter=1020;
  check(mpe5_detail::writeRtc(0x60000) && mpe5_detail::readBits(0x60024,2)==(beforeWrap+1)%1000,
        "deterministic guest clock failed to advance after instruction wrap");

  auto host=MPE5Host; host.bios=bios.data(); host.milliseconds=testClock;
  clockValue=0xfffffff0u; check(mpe5::coreStart(host),"clock-wrap restart failed");
  machine.program({0x0f,0x01}); regs16[REG_ES]=0x6000; regs16[REG_BX]=0;
  clockValue=34; check(machine.run(1),"clock hypercall stopped");
  check(mpe5_detail::readBits(0x60024,2)==50 && mpe5_detail::readBits(0x6000c,4)==1 &&
        mpe5_detail::readBits(0x60014,4)==80 && mpe5_detail::readBits(0x60018,4)==2,
        "host millis wrap or DOS-compatible RTC field layout failed");
  mpe5::coreReset();
  std::cout<<"BIOS timer PASS:real guest INT1C countdown, monotonic-clock wrap and valid RTC layout.\n";
}

void verifyKeyboardTimerOrdering(const std::vector<uint8_t>& bios, Image& image) {
  PagedMachine machine; machine.start(bios,image); machine.until("C:\\>",true);
  // A game-style IRQ1 handler reads raw port60 rather than DOS's buffered
  // characters. The main loop must see make39 before the BIOS emits breakB9.
  const uint8_t handler[]={0x50,0xe4,0x60,0x2e,0xa2,0x00,0x04,0x58,0xcf};
  check(machine.pager.write(0x10300,handler,sizeof(handler)),"raw-key handler write failed");
  mpe5_detail::writeBits(0x24,2,0x300); mpe5_detail::writeBits(0x26,2,0x1000);
  mpe5_detail::writeBits(0x10400,2,0);
  machine.program({0x2e,0x80,0x3e,0x00,0x04,0x39,0x75,0xf8,
                   0x2e,0xc6,0x06,0x01,0x04,0x01,0xeb,0xfe});
  MPE5Host.milliseconds=testClock;
  clockValue=inst_counter/1000u+200u;
  regs8[FLAG_IF]=1; int8_asap=1;
  queue(machine.keyboard," ");
  bool observed=false;
  for(unsigned slice=0;slice<200;++slice) {
    check(machine.run(500),"raw-key main loop stopped");
    if(mpe5_detail::readBits(0x10401,1)==1){observed=true;break;}
  }
  check(observed && mpe5_detail::readBits(0x10400,1)==0x39,
        "simultaneous timer released Space before the guest could observe key-down");
  clockValue+=200; int8_asap=1;
  bool released=false;
  for(unsigned slice=0;slice<200;++slice) {
    check(machine.run(500),"raw-key release stopped");
    if(mpe5_detail::readBits(0x10400,1)==0xb9){released=true;break;}
  }
  check(released && mpe5_detail::readBits(0x10401,1)==1,
        "raw-key release or guest key-down latch was lost");
  mpe5::coreReset();
  std::cout<<"Keyboard/timer PASS:guest main loop observes make39 before later breakB9 when both interrupts are due.\n";
}

void saveFrame(const std::string& stem, const std::vector<uint8_t>& planes, const mpe5::CgaVideo& video) {
  std::ofstream binary(stem+".bin",std::ios::binary);
  binary.write(reinterpret_cast<const char*>(planes.data()),planes.size());
  static const uint8_t rgb[16][3]={{0,0,0},{255,255,255},{136,57,50},{103,182,189},
    {139,63,150},{85,160,73},{64,49,141},{191,206,114},{139,84,41},{87,66,0},
    {184,105,98},{80,80,80},{120,120,120},{148,224,137},{120,105,196},{159,159,159}};
  std::ofstream ppm(stem+".ppm",std::ios::binary); ppm<<"P6\n320 200\n255\n";
  for(unsigned y=0;y<200;++y) for(unsigned x=0;x<320;++x) {
    const unsigned cell=(y/8)*40+x/8;
    const uint8_t bitmap=planes[cell*8+y%8],screen=planes[8000+cell]; uint8_t color;
    if(video.hires()) color=(bitmap&(0x80>>(x%8))) ? screen>>4 : screen&15;
    else {
      const uint8_t slot=(bitmap>>(6-(x%8)/2*2))&3;
      color=slot==0 ? video.background() : slot==1 ? screen>>4 : slot==2 ? screen&15 : planes[9000+cell]&15;
    }
    ppm.write(reinterpret_cast<const char*>(rgb[color]),3);
  }
}

std::vector<uint8_t> snapshot(PagedMachine& machine, VideoFixture& f) {
  auto state=mpe5::coreVideoState(); f.video.setState(state);
  // Request a complete deterministic snapshot without touching guest state.
  auto alternate=state; alternate.enabled=!alternate.enabled; f.video.setState(alternate); f.video.setState(state);
  const auto before=machine.pager.stats(); auto planes=frame(f.video); const auto after=machine.pager.stats();
  check(before.hits==after.hits && before.misses==after.misses && before.pageReads==after.pageReads &&
        before.pageWrites==after.pageWrites, "CGA rendering generated guest-memory or SD traffic");
  std::vector<uint8_t> actual(16384); check(mpe5_detail::readBytes(0xb8000,actual.data(),actual.size()), "VRAM verification read failed");
  check(std::equal(actual.begin(),actual.end(),f.storage.begin()+32), "render mirror differs from actual guest VRAM");
  return planes;
}

void verifyBoulder(const std::vector<uint8_t>& bios, Image& image, const std::string& stem) {
  // Launch directly from a fresh prompt. Space skips the introduction;
  // Shift/fire starts the cave. Neither action depends on a phantom gameport.
  PagedMachine machine; machine.start(bios,image); VideoFixture f; f.attach(); machine.until("C:\\>",true);
  const uint8_t initialTextMode=mpe5::coreVideoState().mode;
  check(initialTextMode==3, ("initial 80-column DOS text mode was not preserved (got "+std::to_string(initialTextMode)+")").c_str());
  queue(machine.keyboard,"BOULDER\r");
  const unsigned titleStart=inst_counter;
  for(unsigned slice=0;slice<20000&&unsigned(inst_counter-titleStart)<12500000u;++slice)
    check(machine.run(25000),"BOULDER stopped before title");
  check(unsigned(inst_counter-titleStart)>=12500000u,"BOULDER title exceeded its execution bound");
  check(mpe5::coreVideoState().mode==4,"BOULDER did not select CGA mode4");
  auto title=snapshot(machine,f); saveFrame(stem+"-title",title,f.video);
  check(std::count_if(title.begin(),title.begin()+8000,[](uint8_t b){return b!=0;})>1000,"BOULDER graphics remained blank");
  std::cout<<"BOULDER title:mode="<<unsigned(mpe5::coreVideoState().mode)<<" background="<<unsigned(f.video.background())
    <<" speakerRevision="<<machine.speaker.revision()<<" reload="<<machine.speaker.effectiveReload()
    <<" active="<<machine.speaker.active()<<" CS:IP="<<std::hex<<regs16[REG_CS]<<':'<<reg_ip<<std::dec<<'\n';
  const auto runInput = [&](uint8_t ascii,uint8_t scan,uint8_t modifiers,uint32_t duration=500000u) {
    check(machine.keyboard.acceptSnapshot(ascii,scan,modifiers,0),"Boulder input snapshot rejected");
    const unsigned begin=inst_counter;
    while(unsigned(inst_counter-begin)<duration) check(machine.run(25000),"Boulder input stopped");
  };
  runInput(' ',0x39,0); runInput(0,0,0);
  runInput(0,0,1,1000000u); runInput(0,0,0);
  const unsigned caveStart=inst_counter;
  for(unsigned slice=0;slice<20000&&unsigned(inst_counter-caveStart)<25000000u;++slice)
    check(machine.run(25000),"BOULDER stopped after Space");
  check(unsigned(inst_counter-caveStart)>=25000000u,"BOULDER cave exceeded its execution bound");
  auto key=snapshot(machine,f); saveFrame(stem+"-key",key,f.video);
  unsigned changedBitmapBytes=0;
  for(unsigned offset=0;offset<8000;++offset) changedBitmapBytes+=title[offset]!=key[offset];
  std::cout<<"BOULDER after Space then Shift:queued="<<unsigned(machine.keyboard.count())<<" speakerRevision="<<machine.speaker.revision()
    <<" reload="<<machine.speaker.effectiveReload()<<" active="<<machine.speaker.active()<<" CS:IP="
    <<std::hex<<regs16[REG_CS]<<':'<<reg_ip<<std::dec<<" changedBitmapBytes="<<changedBitmapBytes<<'\n';
  check(changedBitmapBytes>4000 && machine.keyboard.count()==0,
        "Space then Shift did not replace the title with the full cave display");
  check(mpe5::coreDiagnostic().reason==mpe5::CoreStop::None,"BOULDER left a core failure");
  f.guards(); mpe5::coreReset();
  std::cout<<"BOULDER graphics PASS:fresh launch, actual guest VRAM, complete title, Space then Shift cave, zeroSD render reads.\n";
}

}  // namespace

int main(int argc,char**argv) {
  try {
    check(argc==4,"usage:mpe5_video_test BIOS IMAGE OUTPUTSTEM");
    const auto bios=readFile(argv[1]); Image image{readFile(argv[2])};
    verifyVideoCells(); verifySharpVideo(); verifyCoreVideo(bios,image); verifyBiosTimer(bios,image);
    verifyKeyboardTimerOrdering(bios,image); verifyBoulder(bios,image,argv[3]);
    return 0;
  } catch(const std::exception& error) {std::cerr<<"CGA acceptance FAILED:"<<error.what()<<'\n';mpe5::coreReset();return 1;}
}

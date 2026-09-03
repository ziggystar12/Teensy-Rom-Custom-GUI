// Exercise the real firmware publisher with elapsed-time and slow-CPU clocks.
// The slow model charges each guest yield boundary, including REP iterations;
// it does not claim a measured Teensy instruction rate or SD-card latency.
#include <chrono>
#include <cstdint>
#include <stdexcept>
static uint32_t latencyClock();
#define MPE5_SLICE_CLOCK() latencyClock()
#define MPE5_SLICE_TICKS_PER_US 1u
#define MPE4_HARNESS_MAIN unusedSierraConformance
#include "../../tests/mpe4-firmware-native-harness.cpp"
#include <csignal>

static bool realtimeClock=true;
static uint64_t modeledMicros=0;
static uint32_t instructionMicros=0,injectAt=0;
static unsigned injectKind=0;
static const auto clockOrigin=std::chrono::steady_clock::now();
static uint32_t latencyClock() {
  return realtimeClock ? uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now()-clockOrigin).count()) : uint32_t(modeledMicros);
}
static void requireLatency(bool okay,const char *message) {
  if(!okay)throw std::runtime_error(message);
}
static bool latencyYield(void *context) {
  if(!realtimeClock)modeledMicros+=instructionMicros;
  if(injectKind && uint32_t(modeledMicros)>=injectAt) {
    if(injectKind==1)writeControl(0xf6,MPE3Title.Sequence);
    else {
      writeControl(0xf8,'a');writeControl(0xf9,0x1e);writeControl(0xfa,0);
      writeControl(0xfd,0x80);writeControl(0xfe,1);
      writeControl(0xff,uint8_t(0xa5^'a'^0x1e^0x80^1));writeControl(0xf4,3);
    }
    injectKind=0;
  }
  return MPE5ShouldYield(context);
}
static std::vector<uint8_t> latencyRead(const char *path) {
  std::ifstream file(path,std::ios::binary);requireLatency(file.good(),"Cannot open fixture");
  return {std::istreambuf_iterator<char>(file),{}};
}
static std::vector<uint8_t> latencyAsset(const std::vector<uint8_t> &crt) {
  requireLatency(crt.size()>64&&!memcmp(crt.data(),"C64 CARTRIDGE   ",16),"Invalid CRT");
  std::vector<uint8_t> raw(1048576,255);
  const auto be16=[&](size_t at){return unsigned(crt.at(at))*256+crt.at(at+1);};
  for(size_t at=64;at<crt.size();) {
    requireLatency(!memcmp(crt.data()+at,"CHIP",4),"Invalid CRT chip");
    const unsigned bank=be16(at+10),address=be16(at+12),size=be16(at+14);
    const size_t destination=bank*16384+(address==0xa000?8192:0);
    requireLatency(size==8192&&destination+size<=raw.size()&&at+16+size<=crt.size(),"Invalid CRT size");
    std::copy_n(crt.data()+at+16,size,raw.data()+destination);at+=16+size;
  }
  return {raw.begin()+Root,raw.end()};
}
static bool latencyPrompt() {
  const char *prompt="C:\\>";
  if(MPE5TextCursor<4)return false;
  for(unsigned i=0;i<4;++i)
    if(MPE5Host.consoleShadow[2*(MPE5TextCursor-4+i)]!=uint8_t(prompt[i]))return false;
  return !MPE5InputPending&&!MPE5Keyboard.count();
}
static void quietGuest() {
  // Keep the publisher's screen unchanged while measuring only its mandatory
  // CPU service delay. The actual booted CPU still executes a guest JMP loop.
  const uint8_t loop[]={0xeb,0xfe};
  requireLatency(MPE5Memory.write(0x10000,loop,sizeof(loop)),"Cannot prepare guest loop");
  regs16[REG_CS]=0x1000;reg_ip=0;regs8[FLAG_IF]=regs8[FLAG_TF]=0;
  seg_override_en=rep_override_en=0;MPE5RepeatPending=false;
  MPE5Keyboard.clear();MPE5InputPending=false;MPE5InputActivationPending=false;
  MPE5TransportCanary=false;MPE5Error=0;
  MPE3Title.Pending=false;MPE3TitleMailbox[MPE3TitleRegACK]=0;
  MPE5Host.memory.shouldYield=latencyYield;
}
static void completeScreen(unsigned cost,bool baseline) {
  quietGuest();realtimeClock=false;modeledMicros=0;instructionMicros=cost;
  MPE5Text.reset();MPE5FirstFrame=true;MPE5DisplayComplete=false;
  std::array<bool,1000> seen{};unsigned count=0,packets=0;
  uint64_t longest=0;
  for(unsigned n=0;n<100;++n) {
    const uint64_t begin=modeledMicros;
    MPE3TitlePollingHndlr();longest=std::max(longest,modeledMicros-begin);
    requireLatency(MPE3Title.Pending&&MPE5Active&&!MPE5Error,"Screen publisher stopped");
    const unsigned length=EZFlashRAM[6]+8;
    requireLatency(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length),"Packet CRC");
    if(EZFlashRAM[3]==1)for(unsigned at=8;at<length;at+=12) {
      const unsigned cell=MHSNativeRead16(EZFlashRAM+at);
      requireLatency(cell<1000,"Bad cell");if(!seen[cell]){seen[cell]=true;++count;}
    }
    ++packets;
    if(EZFlashRAM[3]==2){requireLatency(count==1000,"Frame end before1000 cells");break;}
    writeControl(0xf6,EZFlashRAM[0xf7]);
    // A fixed, explicitly modeled0.5ms delivery/ACK cost. CPU deadlines are
    // the variable being tested; this is not a C64 transport speed claim.
    modeledMicros+=500;
  }
  requireLatency(count==1000&&EZFlashRAM[3]==2,"Incomplete display");
  const uint64_t limit=200000;
  requireLatency(baseline ? modeledMicros>limit : modeledMicros<=limit,
    baseline?"R13 no longer reproduces initial display stall":"Initial display exceeds200ms modeled budget");
  if(!baseline)requireLatency(longest<=2000+64*cost,"Foreground slice exceeds deadline and check interval");
  std::cout<<" initialDisplay costUs="<<cost<<" cells="<<count<<" packets="<<packets
    <<" elapsedUs="<<modeledMicros<<" maxForegroundUs="<<longest<<'\n';
}
static void interruptSlice(unsigned kind,bool baseline) {
  quietGuest();realtimeClock=false;modeledMicros=0;instructionMicros=3;
  MPE5FirstFrame=false;MPE3Title.Pending=true;
  MPE3TitleMailbox[MPE3TitleRegACK]=uint8_t(MPE3Title.Sequence-1);
  MPE3TitleMailbox[0xfc]=0;injectAt=501;injectKind=kind;
  std::array<uint8_t,sizeof(MPE3TitlePacket)> packet{};
  memcpy(packet.data(),MPE3TitlePacket,packet.size());
  MPE5PumpPending();
  requireLatency(!injectKind&&!memcmp(packet.data(),MPE3TitlePacket,packet.size()),"Pending packet mutated");
  requireLatency(baseline ? modeledMicros>100000 : modeledMicros<=600,
    "ACK/input did not interrupt foreground execution");
  if(kind==2)requireLatency(MPE5InputPending&&MPE3TitleMailbox[0xfc]==1,"Input latch lost");
  std::cout<<(kind==1?" ACK":" input")<<" arrivesUs="<<injectAt<<" returnUs="<<modeledMicros<<'\n';
}
int main(int argc,char **argv) {
  std::signal(SIGABRT,[](int){std::_Exit(1);});
  try {
    requireLatency(argc==5,"usage:latency-test CRT IMAGE SWAP R13|R14");
    const bool baseline=!strcmp(argv[4],"R13");
    const auto crt=latencyRead(argv[1]),asset=latencyAsset(crt);
    SD.directories.insert("/DOSVM");
    SD.files["/DOSVM/DOSVM.IMG"]=std::make_shared<std::vector<uint8_t>>(latencyRead(argv[2]));
    SD.files["/DOSVM/DOSVM.SWP"]=std::make_shared<std::vector<uint8_t>>(latencyRead(argv[3]));
    const auto begin=std::chrono::steady_clock::now();uint64_t longest=0;unsigned calls=0;
    PSRAMAvailable=false;start(asset,Root,false);prepareDosCartridgeMemory(crt);AGIPicLayout=1;
    for(;calls<30000;++calls) {
      const auto before=std::chrono::steady_clock::now();MPE3TitlePollingHndlr();
      longest=std::max(longest,uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-before).count()));
      requireLatency(!MPE5Error,"Boot stopped");
      if(MPE5Active)MPE5Host.memory.shouldYield=latencyYield;
      if(MPE3Title.Pending) {
        if(EZFlashRAM[3]==2&&latencyPrompt())break;
        writeControl(0xf6,EZFlashRAM[0xf7]);
      }
    }
    requireLatency(calls<30000&&latencyPrompt(),"FreeDOS did not boot");
    std::cout<<argv[4]<<" hostBootUs="<<std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now()-begin).count()<<" maxForegroundUs="<<longest
      <<" calls="<<calls<<" instructions="<<inst_counter<<'\n';
    for(unsigned cost:{1u,3u,10u})completeScreen(cost,baseline);
    interruptSlice(1,baseline);interruptSlice(2,baseline);
    if(!baseline) {
      quietGuest();modeledMicros=0xfffffc00ull;instructionMicros=3;
      const auto started=modeledMicros;requireLatency(MPE5RunSlice(),"Wraparound slice stopped");
      requireLatency(modeledMicros-started>=2000&&modeledMicros-started<=2192,"Clock wraparound broke deadline");
      // A full consumer queue leaves a snapshot pending before a slice. It
      // must not trigger the new-arrival yield forever: let IRQ1 drain room.
      quietGuest();modeledMicros=0;instructionMicros=3;regs8[FLAG_IF]=1;
      for(unsigned n=0;n<32;++n)
        requireLatency(MPE5Keyboard.push({0,0x9e,0x80}),"Cannot fill keyboard queue");
      MPE5InputKey='A';MPE5InputScan=0x1e;MPE5InputFlags=0x81;
      MPE5InputJoy=0;MPE5InputPending=true;
      const auto instructions=inst_counter;
      for(unsigned n=0;MPE5InputPending&&n<100;++n)
        requireLatency(MPE5RunSlice(),"Queue-pressure slice stopped");
      requireLatency(!MPE5InputPending&&inst_counter>instructions+1,"Full keyboard queue stalled foreground forever");
    }
    std::cout<<"Latency PASS: actual firmware1000cell publication; immutable packet; ACK/input service; "
      <<(baseline?"R13 stall reproduced":"2ms deadline and32bit clock wrap")<<". Host/model timing only; SD and physical hardware not timed.\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<"Latency FAILED: "<<error.what()<<'\n';return 1;}
}

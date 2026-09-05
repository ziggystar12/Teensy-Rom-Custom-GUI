// Real module + real host scheduler with a deterministic clock and bus model.
// This proves timing policy under load, not Teensy hardware throughput.
#define main nes_fixture_main
#include "module_test.cpp"
#undef main
static uint8_t EZFlashRAM[256];
namespace VmRuntime {
static const VmModule *module;
static VmPacket packet;
static bool active=true,started=true,startRequested,inputPending,pending,quietRequested;
static uint8_t failure,sequence;
static uint32_t sliceStarted;
static VmInput input;
static struct {bool configured,hostPacket;uint8_t phase,preferred,capabilities,requested;uint16_t geometry;} indexedVideo{};
static void fail(uint8_t code){failure=code;}
static bool transferIndexedVideo(){return true;}
static bool transferIndexedVideoSlice(){assert(false);return false;}
static void indexedVideoAck(){assert(false);}
static void indexedVideoLegacy(){}
static bool indexedVideoPacket(VmPacket &){return false;}
static uint16_t crc16(const uint8_t *,unsigned){return 0;}
static bool shouldYield(){return inputPending||quietRequested||(pending&&EZFlashRAM[0xf6]==sequence)||uint32_t(micros()-sliceStarted)>=1500;}
}
#include "../../Source/Teensy/MinimalBoot/VMHostPoll.h"
static uint64_t chargedCycles;
static uint32_t cyclesPerUs,transferUs;
static uint32_t timingNow(){
    if(cyclesPerUs&&MPE6Machine){
        if(MPE6Machine->cycles<chargedCycles)chargedCycles=MPE6Machine->cycles;
        const auto us=(MPE6Machine->cycles-chargedCycles)/cyclesPerUs;
        clockUs+=uint32_t(us);chargedCycles+=us*cyclesPerUs;
    }
    return clockUs;
}
static VmVideoResult timingVideo(VmIndexedFrame *frame){
    const auto result=indexed_test(frame);
    if(result==VmVideoResult::Transferred)clockUs+=transferUs;
    return result;
}
static void poll(){
    if(VmRuntime::pending)EZFlashRAM[0xf6]=VmRuntime::sequence;
    VMHostPoll();assert(!VmRuntime::failure);
    assert(MPE6ModeState==MPE6Mode::Game&&MPE6Machine->error==nes::MachineError::None);
}
int main(int argc,char **argv){
    assert(argc==2);std::ifstream f(argv[1],std::ios::binary);rom={std::istreambuf_iterator<char>(f),{}};
    VmHost h{VM_ABI,sizeof(VmHost),VM_HOST_SERVICES,arena,sizeof arena,"/VMS/NESVM","/VMS/NESVM/ROMS/GAME00.nes",timingNow,open_test,read_test,next_test,close_test};
    h.guest_ram=guest;h.guest_ram_bytes=sizeof guest;h.video_configure=configure_test;
    h.video_indexed=timingVideo;h.should_yield=VmRuntime::shouldYield;
    VmRuntime::module=vm_entry(&h);assert(VmRuntime::module);
    for(unsigned i=0;i<2000;i++){clockUs+=1000;poll();}
    printf("quick ACK: %llu cycles, %llu PPU frames in 2 modeled seconds\n",(unsigned long long)MPE6Machine->cycles,(unsigned long long)MPE6Machine->ppu.frames);fflush(stdout);
    assert(MPE6Machine->cycles==uint64_t(MPE6CpuHz)*2);
    assert(MPE6Machine->ppu.frames>=119);
    assert(MPE6StatsValid&&MPE6SpeedPercent==100);
    // Each emulated cycle costs 1/8 us; each display transfer costs 40 ms.
    // Display must give way to CPU/PPU/APU catch-up instead of causing slow motion.
    cyclesPerUs=8;chargedCycles=MPE6Machine->cycles;transferUs=40000;
    const auto begin=clockUs;const auto before=MPE6Machine->cycles;
    while(uint32_t(timingNow()-begin)<2000000){clockUs+=100;poll();}
    const auto elapsed=timingNow()-begin;
    const double rate=double(MPE6Machine->cycles-before)*1000000/(double(elapsed)*MPE6CpuHz);
    printf("expensive video: %.1f%% emulated-time rate (modeled load, not hardware)\n",rate*100);fflush(stdout);
    assert(rate>=0.90&&rate<=1.01);
    // Even a core too slow to catch up must not starve picture/input forever.
    cyclesPerUs=1;chargedCycles=MPE6Machine->cycles;
    const auto overloadBegin=clockUs;const auto pictures=videoPresented;
    while(uint32_t(timingNow()-overloadBegin)<500000){clockUs+=100;poll();}
    assert(videoPresented>pictures&&MPE6CycleDebt>0);
    // A pause longer than the former 50 ms cap must not delete emulated time.
    cyclesPerUs=0;transferUs=0;clockUs+=250000;poll();
    assert(!MPE6CycleDebt);
    assert(MPE6Machine->cycles==uint64_t(MPE6LastMicros)*MPE6CpuHz/1000000);
    // Speed readout distinguishes 1/3 emulated time from a slow display.
    // It also handles the 32-bit microsecond counter wrapping.
    MPE6StatsStart=0xffff0000u;MPE6StatsCycles=MPE6Machine->cycles-uint64_t(MPE6CpuHz)*2/3;MPE6StatsRunUs=0;
    MPE6SampleSpeed(MPE6StatsStart+2000000u,1800000u);
    assert(MPE6SpeedPercent==33&&MPE6RunPercent==90);
    // Polling an idle game packet path must not resend identical sound.
    while(VmRuntime::pending||MPE6FrameReady)poll();
    assert(MPE6LatestSid.bytes[0]==0);
    VmPacket packet{};assert(!VmRuntime::module->packet(&packet));
    puts("PASS: quick ACKs cannot starve emulation; expensive video yields to emulated time; bounded video under core overload; 250 ms pause retained; no unchanged SID flood");
}

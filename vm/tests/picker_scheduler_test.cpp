// Real firmware poll + real NES module. Only files, clock and hardware I/O
// are stubbed; unlike separate module/client tests, ACK has its real ordering.
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
static struct { bool configured=true,hostPacket=false;uint8_t capabilities=15,preferred=0,requested=0,phase=0; } indexedVideo;
static void fail(uint8_t e){failure=e;}
static bool transferIndexedVideo(){assert(false);return false;}
static bool indexedVideoPacket(VmPacket &){return false;}
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t crc=65535;while(n--){crc^=uint16_t(*p++)<<8;for(unsigned b=0;b<8;b++)crc=(crc<<1)^((crc&0x8000)?0x1021:0);}return crc;}
static bool shouldYield(){return inputPending||quietRequested||(pending&&EZFlashRAM[0xf6]==sequence)||uint32_t(micros()-sliceStarted)>=1500;}
}
#include "../../Source/Teensy/MinimalBoot/VMHostPoll.h"
static unsigned acknowledgedFrames;
static void poll(unsigned count=1){
    for(unsigned n=0;n<count;n++){
        // The client ACKs complete packets; the next host call performs
        // ACK consumption and publishes the successor in that SAME turn.
        if(VmRuntime::pending){
            if(VmRuntime::packet.type==2&&(VmRuntime::packet.flags&1))acknowledgedFrames++;
            EZFlashRAM[0xf6]=VmRuntime::sequence;
        }
        VMHostPoll();assert(!VmRuntime::failure);
    }
}
static void input(uint8_t buttons){VmRuntime::input={buttons,0,0,0x83};VmRuntime::inputPending=true;poll(80);}
int main(int argc,char **argv){
    assert(argc==2);std::ifstream f(argv[1],std::ios::binary);rom={std::istreambuf_iterator<char>(f),{}};
    VmHost h{VM_ABI,sizeof(VmHost),VM_HOST_SERVICES,arena,sizeof arena,"/VMS/NESVM","",now,open_test,read_test,next_test,close_test};
    h.guest_ram=guest;h.guest_ram_bytes=sizeof guest;h.video_configure=configure_test;h.video_indexed=indexed_test;h.should_yield=VmRuntime::shouldYield;
    VmRuntime::module=vm_entry(&h);assert(VmRuntime::module);poll(100);assert(acknowledgedFrames>4);
    const auto frozen=*MPE6Frozen;const auto packet=VmRuntime::packet;const auto sequence=VmRuntime::sequence;
    VmRuntime::input={nes::Down,0,0,0x83};VmRuntime::inputPending=true;
    EZFlashRAM[0xf6]=uint8_t(sequence-1); // Hold the ACK while input arrives.
    for(unsigned n=0;n<40;n++)VMHostPoll();
    assert(MPE6MenuState->selected==0&&VmRuntime::sequence==sequence);
    assert(!memcmp(&frozen,MPE6Frozen,sizeof frozen)&&!memcmp(&packet,&VmRuntime::packet,sizeof packet));
    poll(80);
    printf("scheduler: frameEnds=%u inputHead=%u inputTail=%u selection=%u packetPending=%d\n",acknowledgedFrames,InputHead,InputTail,MPE6MenuState->selected,int(ModulePacketPending));fflush(stdout);
    assert(MPE6MenuState->selected==1);
    input(nes::Down);assert(MPE6MenuState->selected==1);input(0);input(nes::Right);assert(MPE6MenuState->selected==18);
    input(0);input(nes::Left);assert(MPE6MenuState->selected==1);input(0);input(nes::Up);assert(MPE6MenuState->selected==0);
    input(0);input(nes::Start);assert(MPE6ModeState==MPE6Mode::Game);
    input(0);input(nes::Start|nes::Select);assert(MPE6ModeState==MPE6Mode::Menu);
    input(0);input(nes::Down);assert(MPE6MenuState->selected==1);input(0);input(nes::A);assert(MPE6ModeState==MPE6Mode::Game);
    puts("PASS: actual firmware scheduling, idle picker input, held/release, paging, Return/Fire launch and game-to-picker recovery");
}

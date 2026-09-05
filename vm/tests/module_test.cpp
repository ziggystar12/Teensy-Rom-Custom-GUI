#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
#include "../nes/nesvm.cpp"
static std::vector<uint8_t> rom;
static uint8_t arena[VM_DATA_BYTES],guest[VM_RAM_BYTES];
static uint32_t clockUs,entryIndex;
static uint8_t videoSnapshot[10000];
static unsigned videoCalls,videoPresented;
static std::ofstream pickerWire;
static unsigned pickerFrames;
static uint8_t pickerSequence;
static void recordPicker(const VmPacket &p){
    if(!pickerWire.is_open()||pickerFrames>=5)return;
    uint8_t bytes[240]={'M','3',1,p.type,++pickerSequence,p.flags,p.length,0};
    memcpy(bytes+8,p.payload,p.length);uint16_t crc=65535;
    for(unsigned n=0;n<unsigned(p.length)+8;n++){crc^=uint16_t(bytes[n])<<8;for(unsigned b=0;b<8;b++)crc=(crc<<1)^((crc&0x8000)?0x1021:0);}
    bytes[p.length+8]=crc;bytes[p.length+9]=crc>>8;const unsigned length=p.length+10;
    pickerWire.put(length);pickerWire.put(length>>8);pickerWire.write((char *)bytes,length);
    if(p.type==2&&(p.flags&1))pickerFrames++;
}
static uint32_t now(){return clockUs+=1000;}
static uint32_t open_test(const char *p,VmFileInfo *i){
    *i={};i->directory=!strcmp(p,"/VMS/NESVM/ROMS");i->bytes=rom.size();
    if(i->directory){entryIndex=0;return 1;}return strstr(p,".nes")?2:0;
}
static int32_t read_test(uint32_t h,uint32_t offset,void *p,uint32_t n){
    if(h!=2||offset>rom.size()||n>rom.size()-offset)return -1;memcpy(p,rom.data()+offset,n);return n;
}
static int32_t next_test(uint32_t h,VmFileInfo *i){
    if(h!=1)return -1;if(entryIndex==40)return 0;*i={};i->bytes=rom.size();snprintf(i->name,sizeof i->name,"GAME%02u.nes",entryIndex++);return 1;
}
static void close_test(uint32_t){}
static VmVideoResult video_test(const VmVideoFrame *frame){
    assert(frame&&frame->bytes==sizeof(VmVideoFrame));
    assert(frame->format==VM_VIDEO_FORMAT_VIC_CELL10);
    assert((frame->flags&~VM_VIDEO_FLAG_HIRES)==0);
    assert(frame->width==40&&frame->height==25&&frame->stride==10);
    assert(frame->background<16&&!frame->reserved&&frame->pixels);
    assert(frame->pixels>=arena&&frame->pixels+sizeof videoSnapshot<=arena+sizeof arena);
    ++videoCalls;
    if(videoCalls==1){memcpy(videoSnapshot,frame->pixels,sizeof videoSnapshot);return VmVideoResult::Busy;}
    if(videoCalls==2){assert(!memcmp(videoSnapshot,frame->pixels,sizeof videoSnapshot));return VmVideoResult::Unavailable;}
    ++videoPresented;return VmVideoResult::Transferred;
}
struct FrameStats { unsigned cells=0,replace=0; };
static FrameStats drain(const VmModule *m){
    FrameStats s;for(unsigned i=0;i<20000;i++){
        VmPacket p{};m->pump();if(!m->packet(&p))continue;
        assert(p.length<=228);if(p.type==1){s.cells+=p.length/12;s.replace+=(p.flags&16)!=0;}
        recordPicker(p);
        const bool done=p.type==2&&(p.flags&1);m->ack();if(done)return s;
    }assert(!"frame did not complete");return s;
}
static FrameStats press(const VmModule *m,uint8_t key){
    VmInput in{key,1,0,0x81};m->input(&in);auto s=drain(m);in.buttons=0;m->input(&in);m->pump();return s;
}
int main(int argc,char **argv){
    assert(argc==2||argc==3||(argc==4&&!strcmp(argv[2],"--picker-wire")));std::ifstream f(argv[1],std::ios::binary);rom={std::istreambuf_iterator<char>(f),{}};assert(rom.size()==98320);
    if(argc==4){pickerWire.open(argv[3],std::ios::binary);assert(pickerWire);}
    VmHost h{VM_ABI,sizeof(VmHost),VM_HOST_SERVICES,arena,sizeof arena,"/VMS/NESVM","",now,open_test,read_test,next_test,close_test};
    h.guest_ram=guest;h.guest_ram_bytes=sizeof guest;h.video_present=video_test;
    if(argc==3)h.content_path="/VMS/NESVM/ROMS/GAME99.nes";
    const VmModule *m=vm_entry(&h);assert(m);
    assert((uint8_t *)MPE6Machine>=arena&&(uint8_t *)MPE6Machine+sizeof(*MPE6Machine)<=arena+sizeof arena);
    assert(MPE6Machine->ram==guest&&MPE6RomBytes==guest+4384);
    if(argc==3){assert(MPE6ModeState==MPE6Mode::Game);assert(!strcmp(MPE6MenuState->roms[MPE6MenuState->selected].name,"GAME99.nes"));puts("PASS: direct-file launch runs exact requested file outside picker listing");return 0;}
    assert(MPE6MenuState->count==40);
    auto initial=drain(m);assert(initial.cells==1000&&initial.replace==1);
    // The client sends queued input between frame ends, not after audio-only
    // packets. A static menu must keep producing paced frame ends, no cells.
    for(unsigned n=0;n<4;n++){auto idle=drain(m);assert(!idle.cells&&!idle.replace&&MPE6MenuState->selected==0);}
    if(pickerWire.is_open()){assert(pickerFrames==5);pickerWire.close();}
    VmInput heldDown{nes::Down,1,0,0x81};m->input(&heldDown);drain(m);assert(MPE6MenuState->selected==1);
    m->input(&heldDown);assert(!drain(m).cells&&MPE6MenuState->selected==1);
    heldDown.buttons=0;m->input(&heldDown);assert(!drain(m).cells);
    press(m,nes::Up);assert(MPE6MenuState->selected==0);
    auto down=press(m,nes::Down);assert(MPE6MenuState->selected==1);assert(down.replace==0&&down.cells<100);
    auto right=press(m,nes::Right);assert(MPE6MenuState->selected==18&&right.replace==0);
    press(m,nes::Right);assert(MPE6MenuState->selected==35);
    press(m,nes::Right);assert(MPE6MenuState->selected==39);
    press(m,nes::Left);assert(MPE6MenuState->selected==22);
    press(m,nes::Left);assert(MPE6MenuState->selected==5);
    press(m,nes::Left);assert(MPE6MenuState->selected==0);
    // A pending packet/frame is immutable even if launch input arrives.
    VmInput downInput{nes::Down,1,0,0x81};m->input(&downInput);m->pump();VmPacket p{};assert(m->packet(&p));
    const auto frozen=*MPE6Frozen;VmInput launchInput{nes::A,1,0,0x81};m->input(&launchInput);
    for(unsigned i=0;i<100;i++)m->pump();assert(!memcmp(&frozen,MPE6Frozen,sizeof frozen));assert(MPE6ModeState==MPE6Mode::Menu);m->ack();drain(m);
    for(unsigned i=0;i<20000&&MPE6ModeState!=MPE6Mode::Game;i++){m->pump();if(m->packet(&p))m->ack();}
    assert(MPE6ModeState==MPE6Mode::Game);assert(MPE6Machine->error==nes::MachineError::None);
    // Real external-module callbacks, packet generation, ACKs and emulation.
    unsigned gameFrames=0;for(unsigned i=0;i<120000&&gameFrames<120;i++){
        m->pump();if(m->packet(&p)){if(p.type==2&&(p.flags&1))gameFrames++;m->ack();}
        assert(MPE6ModeState==MPE6Mode::Game);assert(MPE6Machine->error==nes::MachineError::None);
    }assert(gameFrames==120);assert(videoCalls>2&&videoPresented);
    VmInput colorMode{0,0,0,0x81};m->input(&colorMode);const auto modeChange=drain(m);
    assert(modeChange.cells==1000&&modeChange.replace==1);
    // Returning from a game must not reintroduce the idle-menu deadlock.
    VmInput back{uint8_t(nes::Start|nes::Select),1,0,0x81};m->input(&back);
    for(unsigned n=0;n<100&&MPE6ModeState!=MPE6Mode::Menu;n++)drain(m);
    assert(MPE6ModeState==MPE6Mode::Menu);drain(m);press(m,0);
    assert(!drain(m).cells);press(m,nes::Start);assert(MPE6ModeState==MPE6Mode::Game);
    puts("PASS: idle picker frame ends, held/released directions, Return/Start launch and game-to-picker idle recovery");
    printf("PASS: actual module, 40-ROM menu, 17-row paging, %u-cell row update without blanking, packet/video pending-frame immutability, Busy/Unavailable fallback, %u host-video frames, 1000-cell mode replacement, Crossbow 120 presented frames\n",down.cells,videoPresented);
}

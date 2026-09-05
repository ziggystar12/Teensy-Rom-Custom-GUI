// Core regressions and repeatable SAME-cycle host comparison; not hardware proof.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <chrono>
#include <type_traits>
#include "../../engine/native-nes/nes_rom.cpp"
#include "../../engine/native-nes/nes_machine.cpp"
#include "../../engine/nofrendo/machine.cpp"
namespace nf=mpe_nofrendo;
static uint8_t prg[32768],chr[8192];
static nes::Cartridge fixture(){
    memset(prg,0xea,sizeof prg);memset(chr,0,sizeof chr);
    prg[0]=0x4c;prg[1]=0;prg[2]=0x80;prg[0x100]=0x40;
    for(unsigned a=0x7ffa;a<0x8000;a+=2){prg[a]=0;prg[a+1]=a==0x7ffc?0x80:0x81;}
    nes::Cartridge c;c.prg=prg;c.chr=chr;c.info.prg_bytes=sizeof prg;c.info.chr_bytes=sizeof chr;return c;
}
static void address(nes::NofrendoMachine &m,unsigned a){m.read(0x2002);m.write(0x2006,a>>8);m.write(0x2006,a);}
static void checks(){
    nes::NofrendoMachine m;auto c=fixture();assert(m.init(c));
    m.ram[255]=0x34;m.ram[0]=0x12;m.ram[256]=0xab;
    assert(nf::zp_readword(255)==0x1234);
    m.ram[0]=0x77;assert(nf::bank_readbyte(0x800)==0x77&&nf::bank_readbyte(0x10000)==0x77);
    uint8_t a[4096]{},b[4096]{};a[4095]=0x23;b[0]=0x45;
    nf::cpu.mem_page[8]=a;nf::cpu.mem_page[9]=b;assert(nf::bank_readword(0x8fff)==0x4523);m.map_cartridge();
    for(unsigned i=0;i<256;++i)m.ram[0x200+i]=i^0x95;
    m.write(0x2003,17);m.write(0x4014,0x0a); // mirrored CPU RAM DMA
    for(unsigned i=0;i<256;++i)assert(nf::ppu.oam[uint8_t(17+i)]==(i^0x95));
    assert(m.dma_transfers==1&&m.dma_cycles==513);
    m.controller.set(nes::Right|nes::B);m.write(0x4016,1);m.write(0x4016,0);
    for(unsigned i=0;i<8;++i)assert((m.read(0x4016)&1)==((0x82>>i)&1));
    assert((m.read(0x4016)&1)==1&&(m.read(0x4017)&1)==0);
    address(m,0x3f10);m.write(0x2007,0x2b);address(m,0x3f00);assert((m.read(0x2007)&63)==0x2b);
    address(m,0x2000);m.write(0x2007,0x56);address(m,0x2400);m.read(0x2007);assert(m.read(0x2007)==0x56);
    address(m,0x0000);m.write(0x2007,0xa5);assert(chr[0]==0); // never write borrowed ROM
    assert(m.init(c));
    uint64_t requested=0;
    for(unsigned i=0;i<25000;++i){unsigned n=i%131+1;assert(m.run_cycles(n)==n);requested+=n;assert(m.cycles==requested);}
    assert(uint32_t(nf::cpu.total_cycles)==uint32_t(m.cycles+m.credit));
    nf::cpu.total_cycles=0xfffffff0;m.apu_cycles=0xfffffff0;assert(m.run_cycles(100)==100&&nf::cpu.total_cycles<128);
    // Scrolling must never write before the scanline buffer. Sentinels retain
    // x=-8 and x=264..319; visible and spill pixels are -7..263 inclusive.
    m.write(0x2001,0x1e);
    for(unsigned scroll=0;scroll<8;++scroll){
        memset(nf::line,0xcd,sizeof nf::line);m.read(0x2002);m.write(0x2005,scroll);m.write(0x2005,0);
        nf::ppu_scanline(nullptr,0,true);assert(nf::line[0]==0xcd);
        for(unsigned i=272;i<sizeof nf::line;++i)assert(nf::line[i]==0xcd);
    }
    // Rendering and discarded presentation must advance IDENTICAL core state.
    assert(m.init(c));m.write(0x4015,1);m.write(0x4003,8);m.run_cycles(60000);assert(m.apu.writes==2&&m.apu.phase>0);
    m.write(0x4015,16);assert(m.error==nes::MachineError::DmcNotImplemented&&m.run_cycles(10)==0);
    assert(m.init(c));prg[0]=2;m.run_cycles(50);assert(m.error==nes::MachineError::CpuJammed);
    puts("PASS: Nofrendo addressing/wrap, mirrored DMA, controller serialization, PPU palette/mirroring/ROM protection, exact cycle credit, long-run counter wrap, scroll guards, APU and explicit DMC/JAM errors");
}
struct Picture { uint8_t pixels[256*240]{}; uint64_t frames=0,pixel_calls=0; uint32_t state=2166136261u; };
static void pixel(void *p,uint16_t x,uint16_t y,uint8_t color){auto &s=*(Picture*)p;assert(x<256&&y<240&&color<64);s.pixels[y*256+x]=color;++s.pixel_calls;}
static void frame(void *p,uint64_t n){((Picture*)p)->frames=n;}
template<class Core> static double run(const nes::Cartridge &c,Picture &out,bool capture){
    Core m;assert(m.init(c,{&out,capture?pixel:nullptr,frame}));
    auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<600;++n){ // ten emulated seconds; same cycles and input script
        m.controller.set(n==120?nes::Start:n>125?(nes::Right|((n%60<20)?nes::A:0)):0);
        assert(m.run_cycles(29830)==29830&&m.error==nes::MachineError::None);
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(m.cycles==17898000&&out.frames>=599);
    if constexpr(std::is_same<Core,nes::NofrendoMachine>::value){
        auto hash=[&](const uint8_t* p,size_t n){while(n--)out.state=(out.state^*p++)*16777619u;};
        hash(m.ram,2048);hash(m.apu.regs,sizeof m.apu.regs);hash(m.apu.length,sizeof m.apu.length);
        hash(nf::ppu.nametab,sizeof nf::ppu.nametab);hash(nf::ppu.oam,sizeof nf::ppu.oam);
        hash(nf::ppu.palette,sizeof nf::ppu.palette);hash((uint8_t*)&nf::cpu.pc_reg,sizeof nf::cpu.pc_reg);
    }
    printf("  %s: %.1f ms, %llu frames, %u APU writes, %u controller reads\n",
           sizeof(Core)==sizeof(nes::Machine)?"cycle reference":"Nofrendo",ms,(unsigned long long)out.frames,m.apu.writes,m.controller_reads);
    return ms;
}
int main(int argc,char **argv){
    checks();
    for(int n=1;n<argc;++n){
        std::ifstream file(argv[n],std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
        nes::Cartridge c;assert(nes::inspect(bytes.data(),bytes.size(),bytes.size(),c.info)==nes::RomError::None);
        assert(nes::supported(c.info)==nes::RomError::None);c.prg=bytes.data()+c.info.prg_offset;c.chr=bytes.data()+c.info.chr_offset;
        uint8_t chrRam[8192]{};if(!c.info.chr_bytes){c.chr=nullptr;c.chr_ram=chrRam;}
        Picture old{},fast{},discarded{};printf("ROM %d, mapper %u (local media never copied)\n",n,c.info.mapper);
        const double baseline=run<nes::Machine>(c,old,true),candidate=run<nes::NofrendoMachine>(c,fast,true);
        printf("  SAME-cycle host speedup: %.2fx (NOT Teensy measurement)\n",baseline/candidate);
        run<nes::NofrendoMachine>(c,discarded,false);assert(fast.frames==discarded.frames&&fast.state==discarded.state);
        assert(fast.pixel_calls>256*240*599u);
    }
}

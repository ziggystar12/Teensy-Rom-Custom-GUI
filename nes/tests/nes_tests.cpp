#include "nes_machine.h"
#include "nes_sid.h"
#include "nes_video.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+" " #x); } while(0)
struct Fixture {
    std::array<uint8_t,32768> prg{};
    std::array<uint8_t,8192> chr{};
    nes::Cartridge c{};
    Fixture() {
        prg.fill(0xea);
        c.info.prg_bytes=32768; c.info.chr_bytes=8192;
        c.prg=prg.data(); c.chr=chr.data();
        prg[0x7ffa]=0x00; prg[0x7ffb]=0x81;
        prg[0x7ffc]=0x00; prg[0x7ffd]=0x80;
        prg[0x7ffe]=0x00; prg[0x7fff]=0x82;
    }
    void program(std::initializer_list<uint8_t> bytes,size_t offset=0) {
        std::copy(bytes.begin(),bytes.end(),prg.begin()+offset);
        const size_t end=offset+bytes.size();
        prg[end]=0x4c; prg[end+1]=uint8_t(end); prg[end+2]=uint8_t(0x80+(end>>8));
    }
};
static std::array<uint8_t,16> header() {
    std::array<uint8_t,16> h{}; h[0]='N'; h[1]='E'; h[2]='S'; h[3]=0x1a; h[4]=2; h[5]=1; return h;
}
static void rom_tests() {
    auto h=header(); nes::RomInfo o;
    CHECK(nes::inspect(h.data(),16,40976,o)==nes::RomError::None);
    CHECK(nes::supported(o)==nes::RomError::None);
    CHECK(o.prg_offset==16 && o.chr_offset==32784 && o.expected_bytes==40976);
    for(size_t n=0;n<16;++n) CHECK(nes::inspect(h.data(),n,n,o)==nes::RomError::ShortHeader);
    for(uint64_t size=16;size<40976;size+=127) CHECK(nes::inspect(h.data(),16,size,o)==nes::RomError::Truncated);
    CHECK(nes::inspect(h.data(),16,40977,o)==nes::RomError::TrailingData);
    h[0]=0; CHECK(nes::inspect(h.data(),16,40976,o)==nes::RomError::BadMagic); h=header();
    h[15]=1; CHECK(nes::inspect(h.data(),16,40976,o)==nes::RomError::ArchaicHeader); h=header();
    h[6]=4; CHECK(nes::inspect(h.data(),16,41488,o)==nes::RomError::None); CHECK(nes::supported(o)==nes::RomError::Trainer);
    h=header(); h[6]=8; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::FourScreen);
    h=header(); h[9]=1; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::UnsupportedRegion);
    h=header(); h[6]=2; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::Battery);
    h=header(); h[6]=0x40; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::Mapper);
    h=header(); h[5]=0; CHECK(nes::inspect(h.data(),16,32784,o)==nes::RomError::None); CHECK(o.chr_ram==8192); CHECK(nes::supported(o)==nes::RomError::None);
    h=header(); h[7]=8; h[10]=0x70; nes::inspect(h.data(),16,40976,o); CHECK(o.nes2 && o.prg_nvram==8192); CHECK(nes::supported(o)==nes::RomError::Battery);
    h=header(); h[7]=8; h[8]=1; nes::inspect(h.data(),16,40976,o); CHECK(o.mapper==256); CHECK(nes::supported(o)==nes::RomError::Mapper);
    h=header(); h[7]=8; h[9]=15; CHECK(nes::inspect(h.data(),16,40976,o)==nes::RomError::UnsupportedSizeEncoding);
    h=header(); h[7]=8; h[15]=2; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::UnsupportedExpansion);
    h=header(); h[7]=8; h[14]=1; nes::inspect(h.data(),16,40976,o); CHECK(nes::supported(o)==nes::RomError::UnsupportedExpansion);
    h=header(); h[6]=0xb0; h[5]=8; CHECK(nes::inspect(h.data(),16,98320,o)==nes::RomError::None); CHECK(o.mapper==11); CHECK(nes::supported(o)==nes::RomError::None);
}
static void input_tests() {
    for(unsigned state=0;state<256;++state) {
        nes::Controller p;
        p.set(uint8_t(state)); p.write(1); CHECK(p.read()==(state&1)); CHECK(p.read()==(state&1));
        p.write(0); p.set(uint8_t(~state));
        for(unsigned bit=0;bit<8;++bit) CHECK(p.read()==((state>>bit)&1));
        CHECK(p.read()==1); CHECK(p.read()==1);
        uint8_t rows[8]; std::memset(rows,0xff,8); uint8_t joy=0xff;
        if(state&nes::A) joy&=~16;
        if(state&nes::B) rows[7]&=~16;
        if(state&nes::Start) rows[0]&=~2;
        if(state&nes::Select) rows[1]&=~128;
        if(state&nes::Up) joy&=~1;
        if(state&nes::Down) joy&=~2;
        if(state&nes::Left) joy&=~4;
        if(state&nes::Right) joy&=~8;
        uint8_t expected=uint8_t(state);
        if((expected&48)==48) expected&=~48;
        if((expected&192)==192) expected&=~192;
        CHECK(nes::c64_buttons(joy,rows)==expected);
    }
    uint8_t rows[8]; std::memset(rows,0xff,8);
    rows[6]&=~16; CHECK(nes::c64_buttons(0xff,rows)==nes::Select);
    rows[7]&=~16; rows[0]&=~2; CHECK(nes::c64_buttons(0xef,rows)==(nes::A|nes::B|nes::Select|nes::Start));
    std::memset(rows,0xff,8); rows[3]&=~16; CHECK(nes::c64_buttons(0xff,rows)==0); // B key is NOT NES B now
    std::memset(rows,0xff,8); CHECK(nes::c64_buttons(0xff,rows)==0);
    nes::SharpControl sharp; CHECK(sharp.enabled);
    rows[0]&=~8; CHECK(!sharp.update(rows)); // F7 alone
    rows[7]&=~4; CHECK(!sharp.update(rows)); // Ctrl+F7
    rows[7]&=~32; CHECK(sharp.update(rows) && !sharp.enabled);
    CHECK(!sharp.update(rows) && !sharp.enabled);
    rows[7]=255; CHECK(!sharp.update(rows) && sharp.held);
    rows[7]&=~36; CHECK(!sharp.update(rows)); // modifiers re-held; F7 never released
    rows[0]=255; CHECK(!sharp.update(rows) && !sharp.held);
    rows[0]&=~8; CHECK(sharp.update(rows) && sharp.enabled);
    CHECK(nes::c64_buttons(0xff,rows)==0); // display-only chord, no NES buttons
    rows[0]=255; sharp.update(rows); rows[0]&=~8; rows[1]&=~128;
    CHECK(!sharp.update(rows) && sharp.enabled); // Shift/F8 is not F7
    CHECK(nes::c64_buttons(0xff,rows)==nes::Select);
}
static void mapper_tests() {
    Fixture f;
    f.c.info.prg_bytes=16384; f.prg[0]=0x11; f.prg[0x3fff]=0x22;
    CHECK(f.c.cpu_read(0x8000)==0x11); CHECK(f.c.cpu_read(0xc000)==0x11);
    CHECK(f.c.cpu_read(0xffff)==0x22);
    f.c.cpu_write(0x8000,0x99); CHECK(f.c.cpu_read(0x8000)==0x11);
    std::array<uint8_t,8192> chr_ram{}; f.c.info.chr_bytes=0; f.c.info.chr_ram=8192; f.c.chr_ram=chr_ram.data();
    f.c.ppu_write(0x1fff,0x93); CHECK(f.c.ppu_read(0x1fff)==0x93);
    std::vector<uint8_t> prg(131072),chr(131072);
    for(size_t i=0;i<prg.size();++i) prg[i]=uint8_t(i/32768);
    for(size_t i=0;i<chr.size();++i) chr[i]=uint8_t(i/8192);
    for(unsigned b=0;b<4;++b) prg[b*32768]=0xff;
    nes::Cartridge c; c.info.mapper=11; c.info.prg_bytes=uint32_t(prg.size()); c.info.chr_bytes=uint32_t(chr.size()); c.prg=prg.data(); c.chr=chr.data();
    CHECK(c.cpu_read(0x8001)==0 && c.ppu_read(0)==0);
    c.cpu_write(0x8000,0xf3); CHECK(c.cpu_read(0x8001)==3 && c.ppu_read(0)==15);
    c.cpu_write(0x8001,0xff); CHECK(c.bank==3 && c.bus_conflicts==1 && c.ppu_read(0)==0);
    c.info.chr_bytes=65536; c.cpu_write(0x8000,0xf0); CHECK(c.ppu_read(0)==7);
}
static void cpu_tests() {
    Fixture f;
    f.program({0xf8,0xa9,0x09,0x18,0x69,0x01,0x85,0x10,0xa9,0x10,0x38,0xe9,0x01,0x85,0x11});
    nes::Machine m; CHECK(m.init(f.c)); m.run_cycles(120);
    CHECK(m.error==nes::MachineError::None); CHECK(m.ram[0x10]==0x0a && m.ram[0x11]==0x0f); CHECK(!m.cpu.bcd_enabled);
    CHECK(m.ppu.ticks==m.cycles*3);
    m.write(0x0800,0xa5); CHECK(m.read(0)==0xa5 && m.read(0x1800)==0xa5);
    for(unsigned i=0;i<16;++i) CHECK((m.read(0x4017)&31)==0);
    f.program({0xa2,0x01,0xbd,0xff,0x00,0xe6,0x10});
    m.init(f.c); m.ram[0x100]=0x66; m.ram[0x10]=5;
    std::vector<nes::BusEvent> trace;
    m.trace_context=&trace; m.trace=[](void* ctx,const nes::BusEvent& e){static_cast<std::vector<nes::BusEvent>*>(ctx)->push_back(e);};
    m.run_cycles(80);
    CHECK(m.cpu.A==0x66 && m.ram[0x10]==6);
    std::vector<uint8_t> writes; bool dummy=false,actual=false;
    for(const auto& e:trace) {
        if(e.write && e.address==0x10) writes.push_back(e.data);
        if(!e.write && e.address==0) dummy=true;
        if(!e.write && e.address==0x100) actual=true;
    }
    CHECK(writes.size()==2 && writes[0]==5 && writes[1]==6); CHECK(dummy && actual);
    f.program({0x6c,0xff,0x02}); f.program({0xa9,0x77,0x85,0x22},0x100);
    m.init(f.c); m.ram[0x2ff]=0; m.ram[0x200]=0x81; m.ram[0x300]=0x82; m.run_cycles(80); CHECK(m.ram[0x22]==0x77);
    f.program({0x4c,0x00,0x80}); f.prg[0x100]=0xe6; f.prg[0x101]=0x23; f.prg[0x102]=0x40;
    m.init(f.c); m.run_cycles(30); m.ppu.ctrl=0x80; m.ppu.status=0x80; m.run_cycles(80);
    CHECK(m.ram[0x23]==1); m.run_cycles(80); CHECK(m.ram[0x23]==1);
    m.ppu.status=0; m.run_cycles(5); m.ppu.status=0x80; m.run_cycles(80); CHECK(m.ram[0x23]==2);
    f.program({0x02}); m.init(f.c); m.run_cycles(20); CHECK(m.error==nes::MachineError::CpuJammed);
    m.ram[19]=77; m.ppu.palette[1]=12; m.controller.set(255); m.apu.irq=true; m.cart.bank=3;
    CHECK(m.init(f.c)); CHECK(m.ram[19]==0 && m.ppu.palette[1]==0 && m.controller.live==0);
    CHECK(m.cycles==0 && !m.apu.irq && m.cart.bank==0 && !m.trace);
    CHECK(m.init(m.cart,m.raster));
}
static void dma_tests() {
    std::array<uint64_t,2> lengths{};
    for(unsigned parity=0;parity<2;++parity) {
        Fixture f;
        if(parity) f.program({0x24,0x00,0xa9,0x02,0x8d,0x14,0x40,0xe6,0x30});
        else f.program({0xa9,0x02,0x8d,0x14,0x40,0xe6,0x30});
        nes::Machine m; m.init(f.c); m.ppu.oam_addr=16;
        for(unsigned i=0;i<256;++i) m.ram[0x200+i]=uint8_t(i^0x5a);
        m.run_cycles(800);
        CHECK(m.dma_transfers==1 && !m.dma_active && m.ram[0x30]==1);
        CHECK(m.dma_cycles==513 || m.dma_cycles==514); lengths[parity]=m.dma_cycles;
        for(unsigned i=0;i<256;++i) CHECK(m.ppu.oam[(i+16)&255]==uint8_t(i^0x5a));
        CHECK(m.ppu.ticks==m.cycles*3);
    }
    CHECK(lengths[0]!=lengths[1]);
}
struct Pixels {
    std::array<uint8_t,61440> data{};
    static void put(void* ctx,uint16_t x,uint16_t y,uint8_t c) { static_cast<Pixels*>(ctx)->data[y*256+x]=c; }
};
static void ppu_tests() {
    Fixture f; nes::Ppu p; p.startup_dots=0;
    p.write(0x2000,0x11,f.c); CHECK(p.read(0x2400,f.c)==0x11); CHECK(p.read(0x3000,f.c)==0x11);
    f.c.info.vertical=true; p.write(0x2000,0x33,f.c); CHECK(p.read(0x2800,f.c)==0x33);
    p.write(0x3f10,0x2f,f.c); CHECK(p.read(0x3f00,f.c)==0x2f && p.read(0x3f30,f.c)==0x2f);
    p.v=0x2000; p.read_buffer=0x99; CHECK(p.cpu_read(7,f.c)==0x99); CHECK(p.cpu_read(7,f.c)==0x33);
    p.v=0x3f00; CHECK((p.cpu_read(7,f.c)&0x3f)==0x2f);
    p.status=0xe0; p.open_bus=0x1b; p.write_second=true; CHECK(p.cpu_read(2,f.c)==0xfb); CHECK(!(p.status&0x80) && !p.write_second);
    p.cpu_write(5,0x1d,f.c); CHECK(p.fine_x==5 && (p.t&31)==3 && p.write_second);
    p.cpu_write(5,0x2a,f.c); CHECK(((p.t>>5)&31)==5 && ((p.t>>12)&7)==2 && !p.write_second);
    p.v=31; p.increment_x(); CHECK(p.v==0x400);
    p.v=uint16_t(0x7000|(29<<5)); p.increment_y(); CHECK(p.v==0x800);
    p=nes::Ppu{}; p.cpu_write(0,0x80,f.c); CHECK(p.ctrl==0); p.startup_dots=1; p.tick(f.c,{}); p.cpu_write(0,0x80,f.c); CHECK(p.ctrl==0x80);
    p=nes::Ppu{}; p.startup_dots=0; p.mask=0x1e;
    std::memset(p.oam,0xff,sizeof(p.oam));
    for(unsigned r=0;r<8;++r) f.chr[r]=0xff; // tile 0: opaque background
    f.chr[16]=0x80; // tile 1: one sprite pixel
    p.palette[0]=0x0f; p.palette[1]=0x30; p.palette[17]=0x16;
    p.oam[0]=15; p.oam[1]=1; p.oam[2]=0; p.oam[3]=20;
    Pixels px; nes::RasterSink sink{&px,Pixels::put,nullptr};
    while(p.frames<2) p.tick(f.c,sink);
    CHECK(px.data[16*256+20]==0x16); CHECK(px.data[16*256+19]==0x30); CHECK(p.sprite0_hits==2);
    p.oam[2]=0x20; while(p.frames<3) p.tick(f.c,sink); CHECK(px.data[16*256+20]==0x30);
    p.oam[2]=0x40; while(p.frames<4) p.tick(f.c,sink); CHECK(px.data[16*256+27]==0x16);
    for(unsigned i=0;i<9;++i) { p.oam[i*4]=15; p.oam[i*4+1]=1; p.oam[i*4+2]=0; p.oam[i*4+3]=uint8_t(i*8); }
    p.select_sprites(16,f.c); CHECK(p.sprite_count==8 && (p.status&0x20));
    p=nes::Ppu{}; p.startup_dots=0;
    while(p.frames<1) p.tick(f.c,{}); auto begin=p.ticks;
    while(p.frames<2) p.tick(f.c,{}); CHECK(p.ticks-begin==89342);
    p.mask=0x18; begin=p.ticks;
    while(p.frames<3) p.tick(f.c,{}); const auto delta=p.ticks-begin;
    begin=p.ticks; while(p.frames<4) p.tick(f.c,{});
    CHECK((delta==89341 && p.ticks-begin==89342) || (delta==89342 && p.ticks-begin==89341));
}
static void ppu_headless_tests() {
    Fixture f;Pixels px;
    for(unsigned i=0;i<f.chr.size();i++)f.chr[i]=uint8_t(i*73+0x91);
    unsigned hits=0;
    for(unsigned x:{0u,7u,8u,247u,254u,255u})for(unsigned flip:{0u,0x40u,0x80u,0xc0u})
    for(unsigned mask:{0u,8u,16u,0x18u,0x1eu}){
        nes::Ppu drawn;drawn.startup_dots=0;drawn.ctrl=0x80;drawn.mask=mask;
        drawn.line=15;drawn.dot=320;drawn.fine_x=x&7;
        memset(drawn.oam,0xff,sizeof drawn.oam);
        for(unsigned i=0;i<8;i++){
            drawn.oam[i*4]=15;drawn.oam[i*4+1]=1+i;
            drawn.oam[i*4+2]=flip|(i&3);drawn.oam[i*4+3]=uint8_t(x+i);
        }
        drawn.select_sprites(16,f.c);
        auto headless=drawn;
        for(unsigned dot=0;dot<2200;dot++){
            drawn.tick(f.c,{&px,Pixels::put,nullptr});headless.tick(f.c,{});
            // All PPU-visible/internal state must agree at EVERY dot, including
            // sprite-0 timing, clipping, priority, scroll and fetch state.
            CHECK(!memcmp(&drawn,&headless,sizeof drawn));
        }
        hits+=drawn.sprite0_hits;
    }
    CHECK(hits>0);
}
static void apu_tests() {
    nes::Apu a;
    a.write(0x4015,1,0); a.write(0x4003,0,0); CHECK(a.status()&1);
    for(unsigned i=0;i<29830;++i) a.tick(); CHECK(a.irq); CHECK(a.status()&0x40); CHECK(!a.irq);
    a.write(0x4017,0x40,29830); for(unsigned i=0;i<60000;++i) a.tick(); CHECK(!a.irq);
    a.write(0x4015,0,0); CHECK(!(a.status()&15));
    Fixture f; nes::Machine m; m.init(f.c); m.write(0x4015,16); CHECK(m.error==nes::MachineError::DmcNotImplemented);
}
static void sid_tests() {
    nes::Apu a; nes::SidAdapter adapter; nes::SidPacket packet;
    a.write(0x4015,1,0);a.write(0x4000,0x9f,0);a.write(0x4002,0xff,0);a.write(0x4003,1,0);
    CHECK(sizeof(packet.bytes)==26);CHECK(adapter.render(a,packet));CHECK((packet.bytes[0]&1)!=0);
    CHECK(packet.bytes[3]==0 && packet.bytes[4]==8); // 50 percent SID pulse width
    CHECK(packet.bytes[5]==0x41 && packet.bytes[7]==0xf0 && packet.bytes[25]==0x0f);
    CHECK(packet.bytes[1]!=0 || packet.bytes[2]!=0);CHECK(!adapter.render(a,packet));CHECK(packet.bytes[0]==0);
    a.write(0x4003,1,1);CHECK(adapter.render(a,packet));CHECK(packet.bytes[0]&1);
    a.write(0x4015,0,2);CHECK(adapter.render(a,packet));CHECK(!(packet.bytes[5]&1));
    a.write(0x4015,4,3);a.write(0x4008,0x80,3);a.write(0x400a,0x80,3);a.write(0x400b,1,3);
    CHECK(adapter.render(a,packet));CHECK(packet.bytes[19]==0x11);CHECK(packet.bytes[0]&4);
    a.write(0x4015,12,4);a.write(0x400c,0x0f,4);a.write(0x400e,3,4);a.write(0x400f,0,4);
    CHECK(adapter.render(a,packet));CHECK(packet.bytes[19]==0x81);CHECK(packet.bytes[0]&4);CHECK(adapter.noise_steals==1);
    CHECK(adapter.packets>=5 && adapter.retriggers>=4);
    adapter.silence(packet);for(uint8_t value:packet.bytes)CHECK(value==0);
}
static void video_tests() {
    std::array<uint8_t,61440> pixels{}; pixels.fill(0x0f);
    nes::VicFrame f; nes::convert_frame(pixels.data(),f,false);
    for(uint16_t y=0;y<200;++y) for(uint16_t x=0;x<160;++x) CHECK(nes::vic_pixel(f,x,y)==0);
    pixels[0]=pixels[255]=pixels[239*256]=pixels[239*256+255]=0x30;
    nes::convert_frame(pixels.data(),f,false);
    CHECK(nes::vic_pixel(f,0,0)==1 && nes::vic_pixel(f,159,0)==1);
    CHECK(nes::vic_pixel(f,0,199)==1 && nes::vic_pixel(f,159,199)==1);
    CHECK(sizeof(f)==10002);
    nes::SquishRenderer stream(false);
    uint32_t random=0x12345678;
    for(unsigned frame=0;frame<3;++frame) {
        for(uint16_t y=0;y<240;++y) for(uint16_t x=0;x<256;++x) {
            random=random*1664525u+1013904223u;
            pixels[y*256+x]=uint8_t(random>>26);
            stream.pixel(x,y,pixels[y*256+x]);
        }
        CHECK(stream.output_x==0 && stream.output_y==200);
        nes::convert_frame(pixels.data(),f,false);
        CHECK(std::memcmp(f.cells,stream.frame.cells,sizeof(f.cells))==0);
        nes::SquishRenderer::finish(&stream,frame+1);
        CHECK(stream.frames==frame+1 && stream.output_x==0 && stream.output_y==0);
    }
    CHECK(sizeof(stream)<13000);
    pixels.fill(0x22); nes::convert_frame(pixels.data(),f,false);
    const uint8_t sky=nes::vic_pixel(f,80,100);
    CHECK(sky!=0 && sky!=1 && sky!=11 && sky!=12 && sky!=15);
    // Sharp defaults on, keeps every horizontal source column, and is exact
    // for two-color cells, including inverted text on a nonzero background.
    nes::SquishRenderer sharp;
    CHECK(sharp.frame.hires && sharp.requested_sharp);
    for(unsigned inversion=0;inversion<2;++inversion) {
        for(uint16_t y=0;y<240;++y) for(uint16_t x=0;x<256;++x) {
            pixels[y*256+x]=((x+y)%7==0)^bool(inversion)?0x30:0x0f;
            sharp.pixel(x,y,pixels[y*256+x]);
        }
        nes::convert_frame(pixels.data(),f); CHECK(f.hires);
        CHECK(std::memcmp(f.cells,sharp.frame.cells,sizeof(f.cells))==0);
        for(uint16_t y=0;y<200;++y) for(uint16_t x=0;x<320;++x) {
            const uint16_t sx=((2*x+1)*256)/640,sy=((2*y+1)*240)/400;
            CHECK(nes::vic_pixel(f,x,y)==(pixels[sy*256+sx]==0x30?1:0));
        }
        nes::SquishRenderer::finish(&sharp,inversion+1);
    }
    auto previous=sharp.frame;
    sharp.set_sharp(false); CHECK(sharp.frame.hires);
    CHECK(std::memcmp(previous.cells,sharp.frame.cells,sizeof(previous.cells))==0);
    for(uint16_t y=0;y<240;++y) for(uint16_t x=0;x<256;++x) {
        if(y==100 && x==0) sharp.set_sharp(true); // waits for next image
        sharp.pixel(x,y,pixels[y*256+x]);
    }
    nes::convert_frame(pixels.data(),f,false);
    CHECK(!sharp.frame.hires && std::memcmp(f.cells,sharp.frame.cells,sizeof(f.cells))==0);
    nes::SquishRenderer::finish(&sharp,3); sharp.pixel(0,0,pixels[0]); CHECK(sharp.frame.hires);
}
int main() {
    try {
        rom_tests(); input_tests(); mapper_tests(); cpu_tests(); dma_tests(); ppu_tests(); ppu_headless_tests(); apu_tests(); sid_tests(); video_tests();
        std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"machineBytes\":"<<sizeof(nes::Machine)
                 <<",\"streamingRendererBytes\":"<<sizeof(nes::SquishRenderer)
                 <<",\"sidAdapterBytes\":"<<sizeof(nes::SidAdapter)+sizeof(nes::SidPacket)
                 <<",\"scope\":\"original synthetic host tests including basic SID packet mapping; no game ROM bytes, audible output, or hardware proof\"}\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}

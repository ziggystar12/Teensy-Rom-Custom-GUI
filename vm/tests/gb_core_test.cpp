// Local media arguments are never copied into a package.
#include "../../engine/gnuboy/machine.h"
#include <cstdio>
#include <vector>
#include <cassert>
#include <chrono>
static unsigned frames,lines;static uint32_t hash=2166136261;static bool varied;
static uint16_t screenshot[144][160];
static void line(void *,unsigned y,const uint8_t *p,const uint16_t *pal){
    assert(y<144);lines++;
    for(unsigned i=0;i<160;i++){assert(p[i]<64);screenshot[y][i]=pal[p[i]];hash=(hash^pal[p[i]])*16777619;varied|=pal[p[i]]!=pal[p[0]];}
}
static void frame(void *){frames++;}
int main(int argc,char **argv){
    assert(argc>1);
    for(int arg=1;arg<argc;arg++){
        auto f=fopen(argv[arg],"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);
        std::vector<uint8_t> rom(n);assert(fread(rom.data(),1,n,f)==size_t(n));fclose(f);
        assert(!gb::inspect(rom.data(),rom.size()));assert(gb::start(rom.data(),rom.size(),{nullptr,line,frame}));
        auto start=std::chrono::steady_clock::now();frames=lines=0;varied=false;
        while(gb::ticks()<uint64_t(gb::ClockHz)*12){
            auto t=gb::ticks();gb::buttons(t>gb::ClockHz*4&&t<gb::ClockHz*4.2?8:t>gb::ClockHz*7?128:0);
            assert(gb::run(128)>=128);assert(!gb::error());uint8_t sound[26];gb::sid(sound);
        }
        assert(lines>144*100&&frames>100&&varied);
        char out[80];snprintf(out,sizeof out,"build/vt/gb-%s.ppm",gb::color()?"cgb":"dmg");
        auto image=fopen(out,"wb");assert(image);fprintf(image,"P6\n160 144\n255\n");
        for(auto &row:screenshot)for(auto c:row){uint8_t rgb[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};fwrite(rgb,1,3,image);}fclose(image);
        // No cartridge RAM: enabling it must remain safe and read as open bus.
        gb::poke(0,10);gb::poke(0xa000,123);assert(gb::peek(0xa000)==255);
        if(rom[0x147]==0x19){gb::poke(0x2000,0);assert(gb::peek(0x4100)==rom[0x100]);}
        const auto before=gb::ticks();const auto oldLines=lines;gb::capture(false);
        for(unsigned i=0;i<10000;i++)gb::run(128);
        assert(gb::ticks()>before&&lines==oldLines);
        printf("PASS %s: %s frames=%u lines=%u hash=%08x elapsed=%.2fs\n",argv[arg],gb::color()?"CGB":"DMG",frames,lines,hash,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    }
}

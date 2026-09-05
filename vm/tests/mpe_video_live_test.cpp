#include <cassert>
#include <cstdio>
#include <chrono>
#include <fstream>
#include "../video/mpe_video_live.cpp"
#include "../video/mpe_video_kernel.h"
using namespace mpe_video;
int main(int argc,char **argv){
    static const uint8_t palette[4][3]={{0,0,0},{255,255,255},{136,57,50},{103,182,189}};
    static uint8_t pixels[320*200];
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++)pixels[y*320+x]=((y&7)>=4?2:0)+(x&1);
    IndexedSource src{pixels,&palette[0][0],320,200,320,4};LiveConverter c;LiveFrame out{};
    assert(c.render(src,0,out)&&out.mode==0&&!out.mask);
    for(const auto &cell:out.cells)assert(cell[9]<16);
    assert(c.render(src,3,out)&&!out.mask);
    for(const auto &cell:out.cells)assert(cell[8]==cell[9]);
    assert(c.render(src,1,out)&&__builtin_popcount(out.mask)==8);
    for(unsigned i=0;i<25;i++)if(out.mask&(1u<<i))assert(out.split[i]==4);
    const auto prior=out;assert(c.render(src,1,out,&out));assert(!memcmp(&prior,&out,sizeof out));
    assert(c.render(src,2,out,&out)&&out.mask==0x1ffffff);
    for(auto split:out.split)assert(split==4);
    uint8_t kernel[KernelCapacity+16];memset(kernel,0xcc,sizeof kernel);
    for(bool ntsc:{false,true}){
        unsigned n=buildKernel(out,ntsc,kernel,KernelCapacity);assert(n&&n<=KernelCapacity);
        for(unsigned i=KernelCapacity;i<sizeof kernel;i++)assert(kernel[i]==0xcc);
        if(argc==2){std::ofstream f(std::string(argv[1])+(ntsc?"-ntsc.bin":"-pal.bin"),std::ios::binary);f.write((char *)kernel,n);}
    }
    if(argc==2)for(bool ntsc:{false,true}){
        for(unsigned band=0;band<25;band++)out.split[band]=1+band%7;
        out.mask=0x1555555;unsigned n=buildKernel(out,ntsc,kernel,KernelCapacity);assert(n);
        std::ofstream f(std::string(argv[1])+(ntsc?"-mixed-ntsc.bin":"-mixed-pal.bin"),std::ios::binary);f.write((char *)kernel,n);
    }
    assert(!buildKernel(out,false,kernel,2));out.split[0]=0;assert(!buildKernel(out,false,kernel,KernelCapacity));
    src.stride=319;assert(!c.render(src,0,out));src.stride=320;assert(!c.render(src,4,out));
    auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<100;i++)assert(c.render(src,1,out,&out));
    printf("PASS: live color/sharp, Auto-8 cap, all 25 useful bands, stable plans, bounded PAL/NTSC kernel; host conversion %.2f ms/frame (not Teensy timing)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100);
}

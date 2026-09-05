// DOS decodes its hardware layout; MPE owns scaling, C64 colors and transport.
// Synchronous reads use live VRAM, avoiding a second 32 KiB mirror.
#pragma once
#include "../video/mpe_video_live.h"
struct DosRaster {
    mpe5::VideoState state{};
    const uint8_t *vram=nullptr;
    uint8_t palette[48]{};
    static bool graphics(uint8_t mode){return (mode>=4&&mode<=6)||mode==8||mode==9;}
    static uint8_t pixel(void *context,uint16_t x,uint16_t y){
        const auto &r=*static_cast<DosRaster *>(context);const auto &s=r.state;
        if(!s.enabled)return 0;
        const bool tandy=s.mode==8||s.mode==9;
        const unsigned banks=s.mode==9?4:2,rowBytes=s.mode==9?160:80;
        const unsigned shift=tandy?1:s.mode==6?3:2;
        const unsigned local=(s.startAddress*2+(y/banks)*rowBytes+(x>>shift))&8191;
        const uint8_t b=r.vram[(y%banks)*8192+local];
        return tandy?(b>>((1-(x&1))*4))&15:s.mode==6?(b>>(7-(x&7)))&1:(b>>((3-(x&3))*2))&3;
    }
    void capture(const mpe5::VideoState &next,const uint8_t *memory,VmIndexedRasterFrame &out){
        state=next;vram=memory;auto &f=out.frame;
        const bool tandy=state.mode==8||state.mode==9;
        f.width=state.mode==8?160:state.mode==6?640:320;f.height=200;f.stride=0;
        f.colors=tandy?16:state.mode==6?2:4;f.palette=palette;f.palette_bytes=sizeof palette;
        out.geometry=VM_INDEXED_RGBI|(state.mode==8?uint16_t(VM_INDEXED_DOUBLE_WIDTH):uint16_t(VM_INDEXED_FOREGROUND));
        if(!tandy&&state.mode!=6)out.geometry|=VM_INDEXED_SOURCE_BACKGROUND;
        uint8_t colors[16]{};
        if(tandy){for(unsigned i=0;i<16;i++)colors[i]=state.tandyPalette[i&state.tandyMask&15]&15;}
        else if(state.mode==6)colors[1]=state.colorSelect&15;
        else{
            colors[0]=state.colorSelect&15;const unsigned bright=(state.colorSelect&16)?8:0;
            const bool alternate=state.colorSelect&32,mono=state.mode==5||(state.control&4);
            colors[1]=(mono||alternate?3:2)+bright;colors[2]=(mono?4:alternate?5:4)+bright;colors[3]=(mono||alternate?7:6)+bright;
        }
        for(unsigned i=0;i<f.colors;i++){
            const auto c=state.enabled?colors[i]:0;const unsigned bright=(c&8)?85:0;
            palette[i*3]=((c&4)?170:0)+bright;
            palette[i*3+1]=c==6?85:((c&2)?170:0)+bright;
            palette[i*3+2]=((c&1)?170:0)+bright;
        }
    }
};

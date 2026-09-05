// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <string.h>

namespace mpe_video {
// Bounded real-time companion to the exhaustive reference converter. All
// scaling/palette decisions live in firmware; producers submit native pixels.
struct LiveFrame {
    uint8_t cells[1000][10];
    uint8_t split[25];
    uint32_t mask;
    uint8_t mode;
};
struct IndexedSource {
    const uint8_t *pixels,*palette;
    uint16_t width,height,stride,colors;
};
class LiveConverter {
    uint8_t map_[256];
    uint32_t distance_[16][16];
    static const uint8_t *palette();
    struct Pair {uint8_t a,b;};
    static Pair pair(const uint8_t *hist) {
        uint8_t a=0,b=0;
        for(uint8_t i=1;i<16;i++)if(hist[i]>hist[a])a=i;
        b=a;
        for(uint8_t i=0;i<16;i++)if(i!=a&&hist[i]&&(b==a||hist[i]>hist[b]))b=i;
        return {a,b};
    }
    uint32_t error(const uint8_t *hist,Pair p) const {
        uint32_t e=0;for(unsigned i=0;i<16;i++)e+=hist[i]*(distance_[i][p.a]<distance_[i][p.b]?distance_[i][p.a]:distance_[i][p.b]);return e;
    }
    void encode(const uint8_t *pixels,uint8_t first,uint8_t end,Pair p,uint8_t *out) const {
        for(unsigned y=first;y<end;y++){out[y]=0;for(unsigned x=0;x<8;x++)
            if(distance_[pixels[y*8+x]][p.b]<distance_[pixels[y*8+x]][p.a])out[y]|=0x80>>x;}
    }
    void samples(const IndexedSource &s,unsigned cell,uint8_t *p,bool nativeWidth) const {
        const unsigned cx=(cell%40)*8,cy=(cell/40)*8;
        const unsigned left=nativeWidth?(320-s.width)/2:0;
        for(unsigned y=0;y<8;y++){const unsigned sy=((2*(cy+y)+1)*s.height)/400;
            for(unsigned x=0;x<8;x++){
                const unsigned dx=cx+x;
                // Padding is C64 black, independent of the source palette.
                if(nativeWidth&&(dx<left||dx>=left+s.width)){p[y*8+x]=0;continue;}
                const unsigned sx=nativeWidth?dx-left:((2*dx+1)*s.width)/640;
                p[y*8+x]=map_[s.pixels[sy*s.stride+sx]];}}
    }
public:
    // mode: 0 ordinary multicolor; 1 Auto8; 2 Enhanced25; 3 Sharp.
    // Enhanced25/Sharp center narrower sources; all modes fit height.
    bool render(const IndexedSource &s,uint8_t mode,LiveFrame &out,const LiveFrame *previous=nullptr);
};
}

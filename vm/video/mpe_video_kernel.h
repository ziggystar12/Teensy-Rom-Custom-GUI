// SPDX-License-Identifier: MIT
#pragma once
#include "mpe_video_live.h"
#ifndef MPE_VIDEO_CODE
#define MPE_VIDEO_CODE
#endif
namespace mpe_video {
constexpr unsigned KernelCapacity=0x2800;
// $3000-$57ff, entry at raster 51 cycle 2; the receiver installs $02e0.
static MPE_VIDEO_CODE unsigned buildKernel(const LiveFrame &frame,bool ntsc,uint8_t *buffer,unsigned capacity){
    if(!buffer||capacity<KernelCapacity)return 0;
    uint8_t *p=buffer;
    for(unsigned y=0;y<200;y++){
        const unsigned band=y/8,row=y&7;
        const bool enhanced=(frame.mask&(1u<<band))!=0;
        if(enhanced&&(frame.split[band]<1||frame.split[band]>7))return 0;
        const bool split=enhanced&&row==frame.split[band];
        *p++=0xa9;*p++=enhanced&&row>=frame.split[band]?0x68:0x78;
        *p++=0x8d;*p++=0x18;*p++=0xd0;
        *p++=0xa9;*p++=split?uint8_t(0x38|((51+y)&7)):0x3b;
        *p++=0x8d;*p++=0x11;*p++=0xd0;
        unsigned delay=(ntsc?65:63)-12-(row==0?43:split?40:0);
        // A split on row 7 must restore ordinary YSCROLL before the next
        // line's early badline test. Waiting for next line cycle 14 is late.
        if(split&&row==7){*p++=0xa9;*p++=0x3b;*p++=0x8d;*p++=0x11;*p++=0xd0;delay-=6;}
        if(delay&1){*p++=0x24;*p++=0x03;delay-=3;}
        while(delay){*p++=0xea;delay-=2;}
    }
    *p++=0x4c;*p++=0xe0;*p++=0x02;
    return unsigned(p-buffer);
}
}

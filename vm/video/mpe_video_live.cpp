// SPDX-License-Identifier: MIT
#include "mpe_video_live.h"
#ifndef MPE_VIDEO_CODE
#define MPE_VIDEO_CODE
#endif
namespace mpe_video {
MPE_VIDEO_CODE const uint8_t *LiveConverter::palette(){
    static const uint8_t rgb[16][3]={{0,0,0},{255,255,255},{136,57,50},{103,182,189},{139,63,150},{85,160,73},{64,49,141},{191,206,114},
        {139,84,41},{87,66,0},{184,105,98},{80,80,80},{120,120,120},{148,224,137},{120,105,196},{159,159,159}};
    return &rgb[0][0];
}
MPE_VIDEO_CODE bool LiveConverter::render(const IndexedSource &s,uint8_t mode,LiveFrame &out,const LiveFrame *previous){
    if(!s.pixels||!s.palette||!s.width||!s.height||s.width>1024||s.height>1024||s.stride<s.width||!s.colors||s.colors>256||mode>3)return false;
    if(s.geometry&~3)return false;
    const bool nativeWidth=(mode==2||mode==3)&&(unsigned(s.width)*((s.geometry&2)?2:1)<=320);
    const uint32_t previousMask=previous?previous->mask:0;uint8_t previousSplit[25]{};
    if(previous)memcpy(previousSplit,previous->split,25);
    const auto rgb=palette();
    for(unsigned i=0;i<16;i++)for(unsigned j=0;j<16;j++){uint32_t e=0;for(unsigned c=0;c<3;c++){int d=int(rgb[i*3+c])-rgb[j*3+c];e+=d*d;}distance_[i][j]=e;}
    for(unsigned i=0;i<256;i++){
        uint32_t best=~0u;map_[i]=0;if(i>=s.colors)continue;
        for(unsigned j=0;j<16;j++){uint32_t e=0;for(unsigned c=0;c<3;c++){int d=int(s.palette[i*3+c])-rgb[j*3+c];e+=d*d;}
            if(e<best){best=e;map_[i]=j;}}
    }
    uint32_t base[25]{},cost[25][7]{};uint8_t bestSplit[25]{};uint32_t gain[25]{};
    for(unsigned cell=0;cell<1000;cell++){
        uint8_t p[64],hist[16]{};samples(s,cell,p,nativeWidth);auto dst=out.cells[cell];
        if(mode==0){
            for(unsigned y=0;y<8;y++)for(unsigned x=0;x<4;x++)hist[p[y*8+x*2+1]]++;
            uint8_t col[4]={0,0,0,0};hist[0]=0;
            for(unsigned k=1;k<4;k++){col[k]=pair(hist).a;hist[col[k]]=0;}
            for(unsigned y=0;y<8;y++){dst[y]=0;for(unsigned x=0;x<4;x++){uint8_t v=p[y*8+x*2+1],b=0;
                for(unsigned k=1;k<4;k++)if(distance_[v][col[k]]<distance_[v][col[b]])b=k;dst[y]|=b<<(6-2*x);}}
            dst[8]=(col[1]<<4)|col[2];dst[9]=col[3];continue;
        }
        for(auto v:p)hist[v]++;const Pair full=pair(hist);encode(p,0,8,full,dst);dst[8]=(full.b<<4)|full.a;dst[9]=dst[8];
        if(mode==3)continue;
        base[cell/40]+=error(hist,full);
        uint8_t top[16]{},bottom[16];memcpy(bottom,hist,16);
        for(unsigned split=1;split<8;split++){
            for(unsigned x=0;x<8;x++){auto v=p[(split-1)*8+x];top[v]++;bottom[v]--;}
            cost[cell/40][split-1]+=error(top,pair(top))+error(bottom,pair(bottom));
        }
    }
    out.mode=mode;out.mask=0;memset(out.split,0,sizeof out.split);
    if(mode==0||mode==3)return true;
    for(unsigned band=0;band<25;band++){
        unsigned split=0;for(unsigned k=1;k<7;k++)if(cost[band][k]<cost[band][split])split=k;
        // Keep an established split unless the new one improves error by 10%.
        if((previousMask&(1u<<band))&&previousSplit[band]>=1&&previousSplit[band]<=7){
            unsigned old=previousSplit[band]-1;if(uint64_t(cost[band][old])*9<=uint64_t(cost[band][split])*10)split=old;
        }
        bestSplit[band]=split+1;
        // Ignore tiny improvements; this also avoids palette-plan churn.
        if(base[band]>cost[band][split]&&base[band]-cost[band][split]>base[band]/32+1024)gain[band]=base[band]-cost[band][split];
    }
    for(unsigned slot=0;slot<(mode==1?8u:25u);slot++){
        unsigned band=25;uint32_t score=0;
        for(unsigned b=0;b<25;b++)if(gain[b]&&!(out.mask&(1u<<b))){
            uint32_t g=gain[b];if(previousMask&(1u<<b))g+=g/8;
            if(g>score){score=g;band=b;}}
        if(band==25)break;out.mask|=1u<<band;out.split[band]=bestSplit[band];
    }
    for(unsigned cell=0;cell<1000;cell++)if(out.mask&(1u<<(cell/40))){
        uint8_t p[64],top[16]{},bottom[16]{};samples(s,cell,p,nativeWidth);const auto split=out.split[cell/40];
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)(y<split?top:bottom)[p[y*8+x]]++;
        const auto a=pair(top),b=pair(bottom);auto dst=out.cells[cell];encode(p,0,split,a,dst);encode(p,split,8,b,dst);
        dst[8]=(a.b<<4)|a.a;dst[9]=(b.b<<4)|b.a;
    }
    return true;
}
}

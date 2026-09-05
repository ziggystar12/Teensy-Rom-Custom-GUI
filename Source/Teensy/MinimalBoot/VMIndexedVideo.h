// SPDX-License-Identifier: MIT
#pragma once
#define MPE_VIDEO_CODE FLASHMEM
#include "vm/video/mpe_video_live.cpp"
#include "vm/video/mpe_video_kernel.h"
#undef MPE_VIDEO_CODE

namespace VmRuntime {
struct IndexedVideoState {
    mpe_video::LiveFrame *frame;
    mpe_video::LiveConverter *converter;
    uint8_t *kernel;
    uint16_t kernelBytes;
    uint8_t phase,requested,preferred,capabilities;
    uint32_t generation;
    bool configured,hostPacket,displayReady;
};
static IndexedVideoState indexedVideo{};
static bool transferIndexedVideo();
static_assert(sizeof(mpe_video::LiveFrame)+sizeof(mpe_video::LiveConverter)+mpe_video::KernelCapacity<=VM_INDEXED_VIDEO_WORKSPACE_BYTES,"Indexed workspace overflow");
static bool videoRange(const void *p,uint32_t bytes){
    auto address=(uintptr_t)p;return address>=VM_DATA_BASE&&address<=VM_DATA_LIMIT&&bytes<=VM_DATA_LIMIT-address;
}
static FLASHMEM bool configureIndexedVideo(const VmIndexedVideoSetup *setup){
    if(!setup||setup->bytes!=sizeof(*setup)||setup->workspace_bytes<VM_INDEXED_VIDEO_WORKSPACE_BYTES||
       ((uintptr_t)setup->workspace&3)||!videoRange(setup->workspace,VM_INDEXED_VIDEO_WORKSPACE_BYTES)||
       setup->default_mode>3||(setup->capabilities&~15)||!(setup->capabilities&(1u<<setup->default_mode))||setup->reserved)return false;
    indexedVideo={};auto p=(uint8_t *)setup->workspace;memset(p,0,VM_INDEXED_VIDEO_WORKSPACE_BYTES);
    indexedVideo.frame=(mpe_video::LiveFrame *)p;p+=sizeof(mpe_video::LiveFrame);
    indexedVideo.converter=(mpe_video::LiveConverter *)p;p+=sizeof(mpe_video::LiveConverter);
    indexedVideo.kernel=p;indexedVideo.requested=indexedVideo.preferred=setup->default_mode;
    indexedVideo.capabilities=setup->capabilities;indexedVideo.configured=true;return true;
}
static FLASHMEM VmVideoResult submitIndexedVideo(VmIndexedFrame *source){
    auto &v=indexedVideo;
    if(!v.configured||!source||source->bytes!=sizeof(*source))return VmVideoResult::Unavailable;
    if(v.phase==4){
        if(source->generation!=v.generation)return VmVideoResult::Failed;
        source->resolved_mode=v.frame->mode;v.phase=0;v.displayReady=true;return VmVideoResult::Transferred;
    }
    if(v.phase)return VmVideoResult::Busy;
    if(!source->width||!source->height||source->width>1024||source->height>1024||source->stride<source->width||
       !source->colors||source->colors>256||source->pixel_bytes<uint32_t(source->stride)*source->height||source->palette_bytes<uint32_t(source->colors)*3||
       !videoRange(source->pixels,source->pixel_bytes)||!videoRange(source->palette,source->palette_bytes))return VmVideoResult::Failed;
    if(DMA_State!=DMA_S_DisableReady)return VmVideoResult::Busy;
    const bool direct=v.displayReady&&!v.frame->mask&&v.frame->mode==v.requested&&(v.requested==0||v.requested==3);
    const mpe_video::IndexedSource s{source->pixels,source->palette,source->width,source->height,source->stride,source->colors};
    if(!v.converter->render(s,v.requested,*v.frame,v.frame))return VmVideoResult::Failed;
    // Plain Color/Sharp retain the speed candidate's single held-DMA path.
    // Only mode transitions and timed raster kernels require pause/resume.
    if(direct){
        if(!transferIndexedVideo())return VmVideoResult::Failed;
        source->resolved_mode=v.frame->mode;return VmVideoResult::Transferred;
    }
    v.kernelBytes=mpe_video::buildKernel(*v.frame,(videoTiming&1)!=0,v.kernel,mpe_video::KernelCapacity);
    if(!v.kernelBytes)return VmVideoResult::Failed;
    v.generation=source->generation;v.phase=1;return VmVideoResult::Busy;
}
static FLASHMEM bool indexedVideoPacket(VmPacket &packet){
    auto &v=indexedVideo;if(v.phase!=1&&v.phase!=3)return false;
    packet={};packet.type=5;packet.length=3;
    packet.payload[0]=v.phase==1?1:2;packet.payload[1]=v.frame->mode;packet.payload[2]=v.frame->mask!=0;
    return true;
}
static FLASHMEM bool transferIndexedVideo(){
#if defined(FeatVMVideoDMA) && defined(Fab04_FullDMACapable)
    auto &v=indexedVideo;if((videoTiming&0xfe)!=0x80)return false;
    nS_DMASetup=(videoTiming&1)?Def_nS_DMASetupNTSC:Def_nS_DMASetupPAL;
    nS_MaxAdj=(videoTiming&1)?Def_nS_MaxAdjNTSC:Def_nS_MaxAdjPAL;
    uint8_t row[400];bool started=false,okay=true;
    auto segment=[&](uint16_t address,uint8_t *data,uint16_t bytes){
        if(!started){if(!PerformDMA(false,address,data,bytes,false))return false;started=true;return true;}
        return AGIContinueDMA(false,address,data,bytes,false);
    };
    for(unsigned y=0;y<25&&okay;y++){
        for(unsigned x=0;x<40;x++){auto cell=v.frame->cells[y*40+x];memcpy(row+x*8,cell,8);row[320+x]=cell[8];row[360+x]=cell[9];}
        okay=segment(0x6000+y*320,row,320)&&segment(0x5c00+y*40,row+320,40)&&
            segment(v.frame->mode==0?0xd800+y*40:0x5800+y*40,row+360,40);
    }
    if(okay&&v.frame->mask)okay=segment(0x3000,v.kernel,v.kernelBytes);
    bool closed=started&&CloseDMA();if(!okay||!closed){if(DMA_State!=DMA_S_DisableReady)AGIDMAEmergencyRelease();return false;}
    return true;
#else
    return false;
#endif
}
}

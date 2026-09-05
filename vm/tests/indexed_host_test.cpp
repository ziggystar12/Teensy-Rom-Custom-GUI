#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../abi/vm_abi.h"
#define FLASHMEM
#define FeatVMVideoDMA
#define Fab04_FullDMACapable
enum {DMA_S_DisableReady,DMA_S_Active};
static unsigned DMA_State,nS_DMASetup,nS_MaxAdj;
static constexpr unsigned Def_nS_DMASetupNTSC=1,Def_nS_DMASetupPAL=2,Def_nS_MaxAdjNTSC=3,Def_nS_MaxAdjPAL=4;
static uint8_t c64[65536];static unsigned segments;static bool dmaFail;
static bool PerformDMA(bool,uint16_t address,uint8_t *data,uint16_t bytes,bool){
    if(dmaFail)return false;DMA_State=DMA_S_Active;memcpy(c64+address,data,bytes);segments++;return true;
}
static bool AGIContinueDMA(bool r,uint16_t a,uint8_t *d,uint16_t n,bool f){return PerformDMA(r,a,d,n,f);}
static bool CloseDMA(){DMA_State=DMA_S_DisableReady;return true;}
static void AGIDMAEmergencyRelease(){DMA_State=DMA_S_DisableReady;}
namespace VmRuntime {static uint8_t videoTiming=0x80;}
#include "../../Source/Teensy/MinimalBoot/VMIndexedVideo.h"
using namespace VmRuntime;
int main(){
    void *arena=VirtualAlloc((void *)0x20010000,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena==(void *)0x20010000);
    auto p=(uint8_t *)VM_DATA_BASE;VmIndexedVideoSetup setup{sizeof(setup),p,VM_INDEXED_VIDEO_WORKSPACE_BYTES,0,15,0};
    setup.workspace_bytes--;assert(!configureIndexedVideo(&setup));setup.workspace_bytes++;
    setup.workspace=p+1;assert(!configureIndexedVideo(&setup));setup.workspace=p;assert(configureIndexedVideo(&setup));
    auto pixels=p+VM_INDEXED_VIDEO_WORKSPACE_BYTES;auto palette=pixels+64000;
    const uint8_t rgb[]={0,0,0,255,255,255,136,57,50,103,182,189};memcpy(palette,rgb,sizeof rgb);
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++)pixels[y*320+x]=((y&7)>=4?2:0)+(x&1);
    VmIndexedFrame source{sizeof(source),1,pixels,palette,64000,12,320,200,320,4,0};
    auto bad=source;bad.pixel_bytes--;assert(submitIndexedVideo(&bad)==VmVideoResult::Failed);
    bad=source;bad.palette=(uint8_t *)VM_DATA_LIMIT;assert(submitIndexedVideo(&bad)==VmVideoResult::Failed);
    indexedVideo.requested=1;assert(submitIndexedVideo(&source)==VmVideoResult::Busy&&indexedVideo.phase==1);
    const auto frozen=*indexedVideo.frame;indexedVideo.requested=3;
    assert(submitIndexedVideo(&source)==VmVideoResult::Busy&&!memcmp(&frozen,indexedVideo.frame,sizeof frozen));
    VmPacket packet{};assert(indexedVideoPacket(packet)&&packet.type==5&&packet.payload[0]==1&&packet.payload[1]==1);
    indexedVideo.phase=2;assert(transferIndexedVideo());assert(segments==76); // 75 planes + timed kernel.
    indexedVideo.phase=3;assert(indexedVideoPacket(packet)&&packet.payload[0]==2);
    indexedVideo.phase=4;assert(submitIndexedVideo(&source)==VmVideoResult::Transferred&&source.resolved_mode==1);
    source.generation++;assert(submitIndexedVideo(&source)==VmVideoResult::Busy);assert(indexedVideo.frame->mode==3&&!indexedVideo.frame->mask);
    indexedVideo.phase=2;assert(transferIndexedVideo());indexedVideo.phase=4;assert(submitIndexedVideo(&source)==VmVideoResult::Transferred);
    const unsigned before=segments;source.generation++;assert(submitIndexedVideo(&source)==VmVideoResult::Transferred&&segments==before+75);
    DMA_State=DMA_S_Active;assert(submitIndexedVideo(&source)==VmVideoResult::Busy);DMA_State=DMA_S_DisableReady;
    // Native NES width stays centered through the actual firmware DMA path.
    source.width=256;source.height=240;source.stride=256;source.pixel_bytes=256*240;
    memset(pixels,1,source.pixel_bytes);source.generation++;
    assert(submitIndexedVideo(&source)==VmVideoResult::Transferred);
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++){
        const unsigned cell=(y/8)*40+x/8;
        const auto colors=c64[0x5c00+cell],bits=c64[0x6000+cell*8+y%8];
        const auto color=bits&(0x80>>(x%8))?colors>>4:colors&15;
        assert(color==unsigned(x>=32&&x<288));
    }
    indexedVideo.requested=0;source.generation++;assert(submitIndexedVideo(&source)==VmVideoResult::Busy);
    assert(transferIndexedVideo());indexedVideo.phase=4;assert(submitIndexedVideo(&source)==VmVideoResult::Transferred&&source.resolved_mode==0);
    for(unsigned i=0;i<1000;i++)assert(c64[0xd800+i]<16);
    // Switching to Default repaints the former margins with source content.
    assert(c64[0x6000]==0x55&&c64[0x5c00]==0x10);
    assert(c64[0x6000+39*8]==0x55&&c64[0x5c00+39]==0x10);
    dmaFail=true;source.generation++;assert(submitIndexedVideo(&source)==VmVideoResult::Failed&&DMA_State==DMA_S_DisableReady);
    VirtualFree(arena,0,MEM_RELEASE);
    puts("PASS: actual indexed host bounds/config, frozen Busy lifecycle, pause/DMA/resume, mode boundary, centered NES Sharp DMA, full-width Default restoration, fast steady Color/Sharp, DMA failure release");
}

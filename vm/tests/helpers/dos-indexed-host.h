// Actual V1.1.7 indexed service, with only physical DMA and clock stubbed.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define FeatVMVideoDMA
#define Fab04_FullDMACapable
enum {DMA_S_DisableReady,DMA_S_Active};
static unsigned DMA_State,nS_DMASetup,nS_MaxAdj,videoConfigurations,videoTransfers;
static uint32_t ARM_DWT_CYCCNT;
static constexpr uint32_t F_CPU_ACTUAL=600000000;
static constexpr unsigned Def_nS_DMASetupNTSC=1,Def_nS_DMASetupPAL=2,Def_nS_MaxAdjNTSC=3,Def_nS_MaxAdjPAL=4;
static uint8_t c64Video[65536];
static bool PerformDMA(bool,uint16_t address,uint8_t *data,uint16_t bytes,bool){
 DMA_State=DMA_S_Active;memcpy(c64Video+address,data,bytes);videoTransfers++;return true;
}
static bool AGIContinueDMA(bool r,uint16_t a,uint8_t *d,uint16_t n,bool f){return PerformDMA(r,a,d,n,f);}
static bool CloseDMA(){DMA_State=DMA_S_DisableReady;return true;}
static void AGIDMAEmergencyRelease(){DMA_State=DMA_S_DisableReady;}
namespace VmRuntime {static uint8_t videoTiming=0x83;}
#include "Source/Teensy/MinimalBoot/VMIndexedVideo.h"
static bool configureVideo(const VmIndexedVideoSetup *s){
 // No reconfiguration may erase a frozen generation or outstanding host ACK.
 assert(VmRuntime::indexedVideo.phase==0&&!VmRuntime::indexedVideo.hostPacket);
 assert(s->workspace==MPE5VideoWorkspace&&s->workspace_bytes==36864);
 assert(s->default_mode==2&&s->capabilities==4);
 videoConfigurations++;return VmRuntime::configureIndexedVideo(s);
}
static VmVideoResult submitDosVideo(VmIndexedFrame *f){
 // Native executable globals are not ARM RAM1. Mirror just the tiny raster
 // descriptor/palette there; the actual callback still reads the real VRAM.
 auto *r=reinterpret_cast<VmIndexedRasterFrame *>(f);
 auto *context=reinterpret_cast<DosRaster *>(VM_DATA_LIMIT-sizeof(DosRaster)-32);
 *context=*static_cast<DosRaster *>(r->context);
 auto arm=*r;arm.context=context;arm.frame.palette=context->palette;
 const auto result=VmRuntime::submitIndexedVideo(&arm.frame);
 r->source_consumed=arm.source_consumed;r->resolved_background=arm.resolved_background;
 r->frame.resolved_mode=arm.frame.resolved_mode;return result;
}

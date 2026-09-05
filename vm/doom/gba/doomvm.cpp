// SPDX-License-Identifier: GPL-2.0-or-later
#include "platform.h"
#include "core_api.h"
namespace gbadoomvm {
static const VmHost *host;
static bool running,pending,frameEnd,waiting;
static uint32_t keys,generation,lastTic;
static VmIndexedFrame frame;
static uint8_t sound[26];
static void fault(){running=false;close();host->fail(0x76,metrics().zoneRequest);}
static void input(const VmInput *in){
    if(!in||(in->protocol&0xf8)!=0x80)return;
    keys=0;
    switch(in->display){
        case 0x11:case 0x48:keys|=1;break; // W/up
        case 0x1f:case 0x50:keys|=2;break; // S/down
        case 0x4b:keys|=4;break;case 0x4d:keys|=8;break;
        case 0x1e:keys|=16;break;case 0x20:keys|=32;break; // A/D strafe
        case 0x39:case 0x1c:keys|=128;break; // GBA use/run/menu accept
        case 0x0f:keys|=256;break;case 0x01:keys|=512;break;
    }
    if(in->protocol&2)keys|=64; // Control/fire
    // Existing joystick snapshot is active high: directions then fire.
    if(in->overflow&1)keys|=1;if(in->overflow&2)keys|=2;
    if(in->overflow&4)keys|=4;if(in->overflow&8)keys|=8;
    if(in->overflow&16)keys|=64;
}
static void pump(){
    if(!running||frameEnd||waiting)return;
    if(pending){
        auto result=host->video_indexed(&frame);
        if(result==VmVideoResult::Busy)return;
        if(result!=VmVideoResult::Transferred){fault();return;}
        pending=false;frameEnd=true;return;
    }
    auto now=host->micros_now();if(uint32_t(now-lastTic)<28571)return;lastTic=now;
    if(!step(keys)){fault();return;}
    GbaSoundPayload(sound);
    frame={};frame.bytes=sizeof frame;frame.generation=++generation;
    frame.pixels=pixels();frame.pixel_bytes=38400;frame.palette=palette();frame.palette_bytes=768;
    frame.width=240;frame.height=160;frame.stride=240;frame.colors=256;pending=true;
}
static bool packet(VmPacket *p){
    if(!running||!frameEnd||waiting||!p)return false;
    // Receiver expects one SID gate mask followed by all 25 SID registers.
    *p={};p->type=2;p->flags=0x21|(frame.resolved_mode?4:0);p->length=26;
    for(unsigned i=0;i<26;i++)p->payload[i]=sound[i];
    frameEnd=false;waiting=true;return true;
}
static void ack(){waiting=false;}
__attribute__((section(".module_api"),used)) static const VmModule module={VM_ABI,sizeof(VmModule),input,pump,packet,ack};
}
extern "C" __attribute__((section(".entry"),used)) const VmModule *vm_entry(const VmHost *h){
    using namespace gbadoomvm;
    constexpr uint32_t required=VM_SERVICE_FILES|VM_SERVICE_CLOCK|VM_SERVICE_GUEST_RAM|VM_SERVICE_INDEXED_VIDEO|VM_SERVICE_RAM2_RO;
    if(!h||h->abi!=VM_ABI||h->bytes<sizeof(VmHost)||(h->services&required)!=required||
       !h->open||!h->read||!h->close||!h->micros_now||!h->fail||!h->video_configure||!h->video_indexed||
       !h->workspace||(uintptr_t(h->workspace)&7)||!h->guest_ram||(uintptr_t(h->guest_ram)&7)||
       h->guest_ram_bytes!=VM_RAM2_GUEST_BYTES)return nullptr;
    host=h;running=pending=frameEnd=waiting=false;keys=generation=0;
    if(!prepare(h))return nullptr;
    VmIndexedVideoSetup setup{sizeof setup,h->workspace,VM_INDEXED_VIDEO_WORKSPACE_BYTES,0,15,VM_INDEXED_SEPARATE_SELECTORS};
    if(!h->video_configure(&setup))return nullptr;
    if(!start()){fault();return nullptr;}
    running=true;lastTic=h->micros_now();return &module;
}

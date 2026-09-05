// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental ABI-2 extraction. The build gate must pass before packaging.
#include "platform.h"
#include "mhs_native_adapter.h"
#include "../../engine/native-doom/mpe_doom_runtime.h"
#include <new>
#include <string.h>
#include <stdio.h>

namespace doomvm {
static const VmHost *host;
static bool running,videoPending,frameEndReady,packetWaiting;
static uint32_t generation,lastTic;
static uint8_t previousScan,previousModifiers;
alignas(mpe_doom::Controls) static uint8_t controlStorage[sizeof(mpe_doom::Controls)];
static mpe_doom::Controls *controls;
static VmIndexedFrame frame;
static char path[384];
static void fault(uint8_t code){running=false;closeFiles();host->fail(code,metrics().zoneRequest);}
static void edge(uint8_t scan,bool pressed){if(!controls->pushScanEvent(scan,pressed))fault(0x73);}
static void input(const VmInput *in){
    if(!running||!in||(in->protocol&0xf8)!=0x80)return;
    // Current DOS-style client: one non-modifier scan plus held modifiers and
    // a complete joystick snapshot. Full matrix/ghosting acceptance is pending.
    if(previousScan&&previousScan!=in->display)edge(previousScan,false);
    const uint8_t scans[3]={0x2a,0x1d,0x38};
    for(unsigned i=0;i<3;i++)if((previousModifiers^in->protocol)&(1u<<i))edge(scans[i],in->protocol&(1u<<i));
    if(in->display&&in->display!=previousScan)edge(in->display,true);
    previousScan=in->display;previousModifiers=in->protocol&7;controls->setJoystickSnapshot(in->overflow);
}
static void pump(){
    if(!running)return;
    if(frameEndReady||packetWaiting)return;
    if(videoPending){
        auto result=host->video_indexed(&frame);
        if(result==VmVideoResult::Busy)return;
        if(result!=VmVideoResult::Transferred){fault(0x74);return;}
        videoPending=false;frameEndReady=true;return;
    }
    auto now=host->micros_now();if(uint32_t(now-lastTic)<28571)return;
    lastTic=now; // Slow presentation slows emulated time; no skipped gametics.
    mhs_doom_action_transition_t edges[mpe_doom::Controls::kScanEventCapacity];
    size_t n=0;mpe_doom::ScanEvent e;
    while(controls->popScanEvent(&e))edges[n++]={mpe_doom::defaultActionsForScan(e.scan_code),uint8_t(e.pressed)};
    if(!MHS_DoomRunOneTic(controls->heldActions(),edges,n)){fault(0x72);return;}
    size_t pixels=0,palette=0;
    frame={};frame.bytes=sizeof frame;frame.generation=++generation;
    frame.pixels=MHS_DoomFramebuffer(&pixels);frame.pixel_bytes=pixels;
    frame.palette=MHS_DoomPaletteRgb(&palette);frame.palette_bytes=palette;
    frame.width=320;frame.height=200;frame.stride=320;frame.colors=256;videoPending=true;
}
static bool packet(VmPacket *out){
    if(!running||!frameEndReady||packetWaiting||!out)return false;
    // Same host presentation commit as NES: silent SID state + frame end.
    *out={};out->type=2;out->flags=0x21|(frame.resolved_mode?4:0);out->length=25;
    frameEndReady=false;packetWaiting=true;return true;
}
static void ack(){packetWaiting=false;}
static const VmModule module={VM_ABI,sizeof(VmModule),input,pump,packet,ack};
}
extern "C" __attribute__((section(".entry"),used)) const VmModule *vm_entry(const VmHost *h){
    using namespace doomvm;
    constexpr uint32_t required=VM_SERVICE_FILES|VM_SERVICE_CLOCK|VM_SERVICE_GUEST_RAM|VM_SERVICE_INDEXED_VIDEO;
    if(!h||h->abi!=VM_ABI||h->bytes<sizeof(VmHost)||(h->services&required)!=required||
       !h->open||!h->read||!h->close||!h->micros_now||!h->fail||!h->video_configure||!h->video_indexed||
       !h->workspace||(uintptr_t(h->workspace)&7)||h->workspace_bytes<VM_INDEXED_VIDEO_WORKSPACE_BYTES+16||
       !h->guest_ram||(uintptr_t(h->guest_ram)&7)||h->guest_ram_bytes!=VM_RAM_BYTES||
       !h->package_root||!h->content_path)return nullptr;
    host=h;running=videoPending=frameEndReady=packetWaiting=false;generation=0;previousScan=previousModifiers=0;
    int length=h->content_path[0]?snprintf(path,sizeof path,"%s",h->content_path):
        snprintf(path,sizeof path,"%s/WADS/DOOM1.WAD",h->package_root);
    if(length<0||length>=int(sizeof path))return nullptr;
    auto memory=h->workspace+VM_INDEXED_VIDEO_WORKSPACE_BYTES;
    if(!prepare(h,memory,h->workspace_bytes-VM_INDEXED_VIDEO_WORKSPACE_BYTES,path))return nullptr;
    VmIndexedVideoSetup setup{sizeof setup,h->workspace,VM_INDEXED_VIDEO_WORKSPACE_BYTES,0,15,0};
    if(!h->video_configure(&setup))return nullptr;
    controls=new(controlStorage)mpe_doom::Controls();
    if(!MHS_DoomStart(path)){fault(0x71);return nullptr;}
    lastTic=h->micros_now();running=true;return &module;
}
// Any unredirected newlib heap or file use fails; the module must use VmHost.
#if defined(__arm__)
extern "C" void *_sbrk(ptrdiff_t){return reinterpret_cast<void *>(-1);}
extern "C" int _write(int,const void *,int){return -1;}
extern "C" int _read(int,void *,int){return -1;}
extern "C" int _close(int){return -1;}
extern "C" int _fstat(int,void *){return -1;}
extern "C" int _isatty(int){return 0;}
extern "C" int _lseek(int,int,int){return -1;}
extern "C" int _getpid(){return 1;}
extern "C" int _kill(int,int){return -1;}
extern "C" void _exit(int){for(;;){}}
#endif

// SPDX-License-Identifier: GPL-2.0-or-later
// 32-bit host execution only. Relaxed runs diagnose failures, never prove fit.
#include "../doom/platform.h"
#include "mhs_native_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
extern "C" const VmModule *vm_entry(const VmHost *);
static FILE *files[24];
static uint32_t clockUs,faultCode,frames,pendingGeneration,pendingHash;
static uint32_t frameHash(const uint8_t *pixels,size_t bytes){uint32_t h=2166136261u;for(size_t i=0;i<bytes;i++)h=(h^pixels[i])*16777619u;return h;}
static uint32_t now(){clockUs+=1000;return clockUs;}
static uint32_t openFile(const char *path,VmFileInfo *info){
    for(unsigned i=0;i<24;i++)if(!files[i]){auto f=fopen(path,"rb");if(!f)return 0;
        fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<0){fclose(f);return 0;}
        *info={};info->bytes=n;files[i]=f;return i+1;}return 0;
}
static int32_t readFile(uint32_t h,uint32_t offset,void *p,uint32_t n){
    if(!h||h>24||!files[h-1]||fseek(files[h-1],offset,SEEK_SET))return -1;
    auto got=fread(p,1,n,files[h-1]);return ferror(files[h-1])?-1:int32_t(got);
}
static void closeFile(uint32_t h){assert(h&&h<=24&&files[h-1]);fclose(files[h-1]);files[h-1]=nullptr;}
static void fail(uint8_t c,uint32_t){faultCode=c;}
static bool setup(const VmIndexedVideoSetup *){return true;}
static VmVideoResult video(VmIndexedFrame *frame){
    assert(frame->pixel_bytes==64000&&frame->palette_bytes==768);
    auto hash=frameHash(frame->pixels,frame->pixel_bytes);
    if(!pendingGeneration){pendingGeneration=frame->generation;pendingHash=hash;return VmVideoResult::Busy;}
    assert(frame->generation==pendingGeneration&&hash==pendingHash);
    pendingGeneration=0;frames++;return VmVideoResult::Transferred;
}
int main(int argc,char **argv){
    assert(sizeof(void *)==4);if(argc!=5)return 2;
    uint32_t workspace=strtoul(argv[2],nullptr,10),zone=strtoul(argv[3],nullptr,10);
    bool direct=!strcmp(argv[4],"diagnostic"),diagnostic=strcmp(argv[4],"module")!=0;
    // Guards are outside the emulated budgets and checked even on startup OOM.
    auto support=static_cast<uint8_t *>(calloc(1,workspace+64));auto guest=static_cast<uint8_t *>(calloc(1,zone+64));
    assert(support&&guest);memset(support+workspace,0xa5,64);memset(guest+zone,0x5a,64);
    VmHost host{};host.abi=VM_ABI;host.bytes=sizeof host;host.services=VM_HOST_SERVICES;
    host.workspace=support;host.workspace_bytes=workspace;host.guest_ram=guest;host.guest_ram_bytes=zone;
    host.package_root="/VMS/DOOMVM";host.content_path=argv[1];host.micros_now=now;host.open=openFile;
    host.read=readFile;host.close=closeFile;host.fail=fail;host.video_configure=setup;host.video_indexed=video;
    bool started=false;unsigned tics=0,changed=0;uint32_t lastHash=0;
    if(direct){
        assert(workspace>VM_INDEXED_VIDEO_WORKSPACE_BYTES);
        assert(doomvm::prepare(&host,support+VM_INDEXED_VIDEO_WORKSPACE_BYTES,workspace-VM_INDEXED_VIDEO_WORKSPACE_BYTES,argv[1]));
        started=MHS_DoomStart(argv[1]);
        if(started)for(;tics<140;tics++){
            if(!MHS_DoomRunOneTic((tics>20?1u:0u)|(tics>50?8u:0u)|(tics>80?64u:0u),nullptr,0))break;
            size_t bytes=0;auto frame=MHS_DoomFramebuffer(&bytes);uint32_t hash=frameHash(frame,bytes);
            if(tics&&hash!=lastHash)changed++;lastHash=hash;
        }
    }else{
        auto module=vm_entry(&host);started=module!=nullptr;
        if(module){
            for(unsigned i=0;i<16000&&frames<140&&!faultCode;i++){
                if(i==100){VmInput in{0,0x11,0,0x82};module->input(&in);} // W + Control held
                if(i==2200){VmInput in{0,0,0,0x80};module->input(&in);} // complete release
                module->pump();VmPacket packet{};
                if(module->packet(&packet)){
                    assert(packet.type==2&&packet.length==25&&(packet.flags&1));
                    auto before=MHS_DoomGametic();module->pump();assert(before==MHS_DoomGametic());
                    assert(!module->packet(&packet));module->ack();
                }
            }
            tics=MHS_DoomGametic();assert(frames==140&&!faultCode);
        }
    }
    doomvm::closeFiles();auto m=doomvm::metrics();
    for(unsigned i=0;i<64;i++){assert(support[workspace+i]==0xa5);assert(guest[zone+i]==0x5a);}
    for(auto f:files)assert(!f);
    // Escape error text for the JSON evidence without trusting upstream strings.
    char error[200];unsigned j=0;for(auto p=MHS_DoomLastError();*p&&j<190;p++){
        if(*p=='"'||*p=='\\')error[j++]='\\';if(uint8_t(*p)>=32)error[j++]=*p;
    }error[j]=0;
    printf("{\"diagnosticOnly\":%s,\"workspaceBytes\":%u,\"zoneBytes\":%u,\"started\":%s,\"tics\":%u,\"changedFrames\":%u,\"finalFrameHash\":%u,\"videoFrames\":%u,\"e1m1\":%s,\"faultCode\":%u,\"error\":\"%s\",\"heapHighWater\":%u,\"heapUsed\":%u,\"heapFailure\":%u,\"zoneRequest\":%u,\"zonePinned\":%u,\"zonePurgeable\":%u,\"zoneFree\":%u,\"reads\":%u,\"readBytes\":%u,\"zoneUsed\":%u,\"zoneHighWater\":%u,\"guardsIntact\":true,\"handlesClosed\":true}\n",
        diagnostic?"true":"false",workspace,zone,started?"true":"false",tics,changed,lastHash,frames,MHS_DoomInE1M1()?"true":"false",faultCode,error,
        m.heapHighWater,m.heapUsed,m.heapFailure,m.zoneRequest,m.zonePinned,m.zonePurgeable,m.zoneFree,m.reads,m.readBytes,m.zoneUsed,m.zoneHighWater);
    free(support);free(guest);return 0;
}

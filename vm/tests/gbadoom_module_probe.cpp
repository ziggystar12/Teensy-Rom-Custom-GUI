// SPDX-License-Identifier: GPL-2.0-or-later
// 32-bit host diagnostic. Never executes the Cortex-M7 image.
#include "../doom/gba/platform.h"
#include "../doom/gba/core_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "doomdef.h"
#include "g_game.h"
}
extern "C" const VmModule *vm_entry(const VmHost *);
static FILE *file;
static uint32_t clockUs,failCode,frames,changed,lastHash,pendingHash,pendingGeneration;
static uint32_t readCalls,failReadAt=UINT32_MAX;
static uint32_t audioFrames,audioHash=2166136261u;
static const char *contentFile;
static void audio(const uint8_t *p){
    if(p[25]){if(!audioFrames){printf("AUDIO_SAMPLE ");for(unsigned i=0;i<26;i++)printf("%02x",p[i]);puts("");}audioFrames++;}
    for(unsigned i=0;i<26;i++)audioHash=(audioHash^p[i])*16777619u;
}
static uint32_t hash(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static uint32_t now(){clockUs+=1000;return clockUs;}
static uint32_t openFile(const char *path,VmFileInfo *info){
    assert(!file);if(!strcmp(path,"/VMS/DOOMVM/doom1.gbd"))path=contentFile;file=fopen(path,"rb");if(!file)return 0;
    fseek(file,0,SEEK_END);long n=ftell(file);rewind(file);assert(n>=0);*info={};info->bytes=n;return 1;
}
static int32_t readFile(uint32_t h,uint32_t offset,void *p,uint32_t n){
    assert(h==1&&file);if(fseek(file,offset,SEEK_SET))return -1;
    if(++readCalls==failReadAt)return -1;
    // Deliberately short successful reads exercise the retry path.
    if(n>997)n=997;auto got=fread(p,1,n,file);return ferror(file)?-1:int32_t(got);
}
static void closeFile(uint32_t h){assert(h==1&&file);fclose(file);file=nullptr;}
static void fail(uint8_t code,uint32_t){failCode=code;}
static bool setup(const VmIndexedVideoSetup *s){return s&&s->workspace_bytes==VM_INDEXED_VIDEO_WORKSPACE_BYTES;}
static VmVideoResult video(VmIndexedFrame *f){
    assert(f->width==240&&f->height==160&&f->stride==240&&f->pixel_bytes==38400&&f->palette_bytes==768);
    uint32_t h=hash(f->pixels,f->pixel_bytes)^hash(f->palette,f->palette_bytes);
    if(!pendingGeneration){pendingGeneration=f->generation;pendingHash=h;return VmVideoResult::Busy;}
    assert(pendingGeneration==f->generation&&pendingHash==h);
    pendingGeneration=0;if(frames&&lastHash!=h)changed++;lastHash=h;frames++;return VmVideoResult::Transferred;
}
int main(int argc,char **argv){
    assert(sizeof(void *)==4);if(argc!=5)return 2;
    const uint32_t workspace=strtoul(argv[2],nullptr,10),zone=strtoul(argv[3],nullptr,10);
    bool direct=!strncmp(argv[4],"diagnostic",10);
    bool cycle=!strcmp(argv[4],"diagnostic-cycle");
    contentFile=argv[1];
    if(!strcmp(argv[4],"diagnostic-io"))failReadAt=17;
    auto support=static_cast<uint8_t *>(calloc(1,workspace+64));auto guest=static_cast<uint8_t *>(calloc(1,zone+64));assert(support&&guest);
    memset(support+workspace,0xa5,64);memset(guest+zone,0x5a,64);
    VmHost h{};h.abi=VM_ABI;h.bytes=sizeof h;h.services=VM_HOST_SERVICES;
    h.workspace=support;h.workspace_bytes=workspace;h.guest_ram=guest;h.guest_ram_bytes=zone;h.content_path=argv[1];h.package_root="/VMS/DOOMVM";
    if(!strcmp(argv[4],"module-default"))h.content_path="";
    h.open=openFile;h.read=readFile;h.close=closeFile;h.micros_now=now;h.fail=fail;h.video_configure=setup;h.video_indexed=video;
    bool started=false;unsigned tics=0;
    if(direct){
        assert(gbadoomvm::prepare(&h));started=gbadoomvm::start();
        if(started)for(;tics<(cycle?2100u:140u);tics++){
            if(cycle&&tics==700)G_ExitLevel();
            if(cycle&&tics==1400)G_SecretExitLevel();
            if(cycle&&tics==1800)G_InitNew(sk_medium,4,9);
            uint32_t keys=(tics>=10&&tics<110?1u:0u)|(tics>=40&&tics<110?8u:0u)|(tics>=65&&tics<110?64u:0u);
            if(!gbadoomvm::step(keys))break;
            uint8_t sound[26];GbaSoundPayload(sound);audio(sound);
            assert(GbaCoreInLevel());
            if(tics>=10)assert(GbaCoreInputMask()==keys);
            auto next=hash(gbadoomvm::pixels(),38400)^hash(gbadoomvm::palette(),768);
            if(tics&&next!=lastHash)changed++;lastHash=next;
        }
    }else{
        auto module=vm_entry(&h);started=module!=nullptr;
        if(module)for(unsigned i=0;i<18000&&frames<140&&!failCode;i++){
            VmInput in{};in.protocol=frames>=10&&frames<110?0x82:0x80;in.display=frames>=10&&frames<110?0x11:0;
            module->input(&in);module->pump();VmPacket p{};
            if(module->packet(&p)){
                if(frames>11&&frames<110)assert(GbaCoreInputMask()==65);
                if(frames>110)assert(GbaCoreInputMask()==0);
                assert(p.type==2&&p.length==26&&(p.flags&1));auto before=GbaCoreTic();
                uint8_t sound[26];GbaSoundPayload(sound);assert(!memcmp(p.payload,sound,26));audio(p.payload);
                const auto frozen=p;
                for(unsigned retry=0;retry<5;retry++)module->pump();
                assert(GbaCoreTic()==before&&!module->packet(&p));
                GbaSoundPayload(sound);assert(!memcmp(frozen.payload,sound,26));module->ack();
            }
        }
        tics=GbaCoreTic();
    }
    gbadoomvm::close();assert(!file);auto m=gbadoomvm::metrics();
    for(unsigned i=0;i<64;i++){assert(support[workspace+i]==0xa5);assert(guest[zone+i]==0x5a);}
    char error[400];unsigned pos=0;for(auto p=gbadoomvm::error();*p&&pos<390;p++){
        if(*p=='"'||*p=='\\')error[pos++]='\\';if(uint8_t(*p)>=32)error[pos++]=*p;
    }error[pos]=0;
    printf("{\"started\":%s,\"tics\":%u,\"frames\":%u,\"changedFrames\":%u,\"frameHash\":%u,\"e1m1\":%s,\"workspaceBytes\":%u,\"zoneBytes\":%u,\"zoneUsed\":%u,\"zoneHighWater\":%u,\"zoneRequest\":%u,\"supportUsed\":%u,\"reads\":%u,\"readBytes\":%u,\"resourceBytes\":%u,\"resourceCount\":%u,\"lastLump\":%u,\"faultCode\":%u,\"error\":\"%s\",\"guardsIntact\":true,\"handlesClosed\":true}\n",
        started?"true":"false",tics,frames,changed,lastHash,GbaCoreInLevel()?"true":"false",workspace,zone,m.zoneUsed,m.zoneHighWater,m.zoneRequest,m.supportUsed,m.reads,m.readBytes,m.resourceBytes,m.resourceCount,m.lastLump,failCode,error);
    if(started){assert(audioFrames>5);printf("AUDIO %u %u\n",audioFrames,audioHash);}
    free(support);free(guest);return 0;
}

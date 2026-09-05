// SPDX-License-Identifier: GPL-2.0-or-later
// Reset-only native backend. All resources are read through generic VmHost.
#include "platform.h"
#include "core_api.h"
#include "../heap.h"
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
namespace gbadoomvm {
static const VmHost *host;
static uint32_t handle,fileBytes,fileBase,supportOffset;
static doomvm::Heap supportHeap;
static Metrics stats;
static jmp_buf recovery;
static char lastError[192];
alignas(8) static uint16_t framebuffer[120*160];
static uint8_t colors[768];
bool prepare(const VmHost *h){
    host=h;stats={};handle=fileBytes=fileBase=0;supportOffset=VM_INDEXED_VIDEO_WORKSPACE_BYTES;lastError[0]=0;
    memset(framebuffer,0,sizeof framebuffer);memset(colors,0,sizeof colors);
    return h&&h->workspace&&h->workspace_bytes>=supportOffset+16&&h->guest_ram&&h->guest_ram_bytes>=4096&&
        supportHeap.init(h->workspace+supportOffset,h->workspace_bytes-supportOffset);
}
void close(){if(handle){host->close(handle);handle=0;}}
bool start(){
    if(setjmp(recovery)){close();return false;}
    const char *content=host->content_path;
    if(!content||!content[0])content="/VMS/DOOMVM/doom1.gbd";
    VmFileInfo info{};handle=host->open(content,&info);
    if(!handle||info.directory)GbaFatal("Cannot open converted GBADoom WAD");
    fileBytes=info.bytes;
    uint32_t header[4];GbaRead(0,header,sizeof header);
    if(memcmp(header,"GBDWAD1",8)||header[2]!=fileBytes-sizeof header)GbaFatal("Expected converted GBDWAD1 content");
    fileBase=sizeof header;fileBytes=header[2];
    // Validate the generated payload without retaining the whole WAD in RAM.
    uint8_t block[512];uint32_t crc=~0u;
    for(uint32_t offset=0;offset<fileBytes;){
        auto n=fileBytes-offset;if(n>sizeof block)n=sizeof block;GbaRead(offset,block,n);offset+=n;
        for(unsigned i=0;i<n;i++){crc^=block[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    }
    if((crc^~0u)!=header[3])GbaFatal("Converted WAD checksum mismatch");
    GbaCoreStart();return true;
}
bool step(uint32_t keys){if(setjmp(recovery)){close();return false;}GbaCoreStep(keys);return true;}
const char *error(){return lastError;}
Metrics metrics(){stats.supportUsed=supportOffset+supportHeap.highWater;return stats;}
const uint8_t *pixels(){return reinterpret_cast<const uint8_t *>(framebuffer);}
const uint8_t *palette(){return colors;}
}
using namespace gbadoomvm;
extern "C" {
void GbaFatal(const char *message){snprintf(lastError,sizeof lastError,"%s",message);longjmp(recovery,1);}
void I_Error(const char *format,...){va_list args;va_start(args,format);vsnprintf(lastError,sizeof lastError,format,args);va_end(args);longjmp(recovery,1);}
void *GbaZone(unsigned *bytes){*bytes=host->guest_ram_bytes;memset(host->guest_ram,0,*bytes);return host->guest_ram;}
void *GbaSupportAlloc(size_t n){
    auto p=supportHeap.allocate(n);if(n&&!p)GbaFatal("RAM1 support exhausted");
    if(p)memset(p,0,n);return p;
}
void GbaZoneAllocated(unsigned n){stats.zoneUsed+=n;if(stats.zoneUsed>stats.zoneHighWater)stats.zoneHighWater=stats.zoneUsed;}
void GbaZoneReleased(unsigned n){if(n>stats.zoneUsed)GbaFatal("Zone accounting underflow");stats.zoneUsed-=n;}
void GbaZoneFailure(unsigned n){stats.zoneRequest=n;}
uint32_t GbaFileSize(){return fileBytes;}
void GbaRead(uint32_t offset,void *buffer,uint32_t bytes){
    if(offset>fileBytes||bytes>fileBytes-offset)GbaFatal("WAD read outside file");
    auto p=static_cast<uint8_t *>(buffer);offset+=fileBase;
    while(bytes){auto n=host->read(handle,offset,p,bytes);stats.reads++;
        if(n<=0||uint32_t(n)>bytes)GbaFatal("WAD short read or I/O error");
        stats.readBytes+=n;offset+=n;p+=n;bytes-=n;}
}
void GbaResourceLoaded(unsigned lump,unsigned bytes){stats.resourceCount++;stats.resourceBytes+=bytes;stats.lastLump=lump;}
int GbaClockTics(){return int(host->micros_now()/28571u);}
char *strupr(char *s){for(auto p=s;*p;p++)if(*p>='a'&&*p<='z')*p-=32;return s;}
void I_InitScreen_e32(){}
void I_CreateBackBuffer_e32(){}
unsigned short *I_GetBackBuffer(){return framebuffer;}
unsigned short *I_GetFrontBuffer(){return framebuffer;}
void I_SetPallete_e32(const unsigned char *p){memcpy(colors,p,sizeof colors);}
void I_FinishUpdate_e32(const void *,const void *,unsigned,unsigned){}
void I_ProcessKeyEvents(){}
void I_Quit_e32(){GbaFatal("Game requested quit");}
int I_GetTime_e32(){return GbaClockTics();}
}
#if defined(__arm__)
extern "C" {
struct _reent;
void *__wrap_malloc(size_t n){return GbaSupportAlloc(n);}
void *__wrap_calloc(size_t n,size_t b){if(n&&b>SIZE_MAX/n)GbaFatal("calloc overflow");return GbaSupportAlloc(n*b);}
void *__wrap_realloc(void *p,size_t n){auto q=supportHeap.resize(p,n);if(n&&!q)GbaFatal("RAM1 realloc exhausted");return q;}
void __wrap_free(void *p){if(!supportHeap.release(p))GbaFatal("Invalid support free");}
void *__wrap__malloc_r(_reent *,size_t n){return __wrap_malloc(n);}
void *__wrap__calloc_r(_reent *,size_t n,size_t b){return __wrap_calloc(n,b);}
void *__wrap__realloc_r(_reent *,void *p,size_t n){return __wrap_realloc(p,n);}
void __wrap__free_r(_reent *,void *p){__wrap_free(p);}
}
extern "C" void *_sbrk(ptrdiff_t){return reinterpret_cast<void *>(-1);}
extern "C" int _write(int,const void *,int){return -1;}
extern "C" int _read(int,void *,int){return -1;}
extern "C" int _close(int){return -1;}
extern "C" int _fstat(int,void *){return -1;}
extern "C" int _isatty(int){return 0;}
extern "C" int _lseek(int,int,int){return -1;}
extern "C" int _getpid(){return 1;}
extern "C" int _kill(int,int){return -1;}
extern "C" void _exit(int){GbaFatal("Unexpected process exit");}
#endif

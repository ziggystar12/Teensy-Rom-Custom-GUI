// SPDX-License-Identifier: GPL-2.0-or-later
// No Arduino, SdFat, PSRAM, firmware globals or process heap dependencies.
#include "platform.h"
#include "heap.h"
#include "mhs_native_adapter.h"
#include "ff.h"
#include <limits.h>

namespace doomvm {
static const VmHost *host;
static Heap heap;
static Metrics stats;
static char wadPath[384];
struct File { uint32_t handle,cursor,bytes; };
static File files[8];
bool prepare(const VmHost *h,void *memory,uint32_t bytes,const char *wad) {
    if(!h||!wad||!wad[0]||strlen(wad)>=sizeof wadPath||!heap.init(memory,bytes))return false;
    host=h;stats={};memset(files,0,sizeof files);strcpy(wadPath,wad);return true;
}
void closeFiles(){for(auto &f:files){if(f.handle)host->close(f.handle);f={};}}
Metrics metrics(){stats.heapHighWater=heap.highWater;stats.heapUsed=heap.used;stats.heapFailure=heap.failedBytes;return stats;}
static File *file(FIL *f) {
    if(f)for(auto &slot:files)if(reinterpret_cast<FIL>(&slot)==*f&&slot.handle)return &slot;
    return nullptr;
}
}
using namespace doomvm;
extern "C" {
long systime;
int joystick;
void *DoomMalloc(size_t n){auto p=heap.allocate(n);if(n&&!p)MHS_DoomFatal("RAM1 private heap exhausted");return p;}
void *DoomCalloc(size_t n,size_t bytes){if(n&&bytes>SIZE_MAX/n)MHS_DoomFatal("calloc overflow");auto p=DoomMalloc(n*bytes);if(p)memset(p,0,n*bytes);return p;}
void DoomFree(void *p){if(!heap.release(p))MHS_DoomFatal("invalid private heap free");}
void *DoomRealloc(void *p,size_t n){if(p&&!heap.size(p))MHS_DoomFatal("invalid private heap realloc");auto q=heap.resize(p,n);if(n&&!q)MHS_DoomFatal("RAM1 private realloc exhausted");return q;}
char *DoomStrdup(const char *s){if(!s)return nullptr;auto p=static_cast<char *>(DoomMalloc(strlen(s)+1));strcpy(p,s);return p;}
void DoomExit(int){MHS_DoomFatal("Doom requested process exit");for(;;){}}
#if defined(__arm__)
// Newlib's format/config helpers also allocate through reentrant entrypoints.
// Linker wrapping closes that path into the firmware/process heap as well.
struct _reent;
void *__wrap_malloc(size_t n){return DoomMalloc(n);}
void *__wrap_calloc(size_t n,size_t b){return DoomCalloc(n,b);}
void *__wrap_realloc(void *p,size_t n){return DoomRealloc(p,n);}
void __wrap_free(void *p){DoomFree(p);}
void *__wrap__malloc_r(_reent *,size_t n){return DoomMalloc(n);}
void *__wrap__calloc_r(_reent *,size_t n,size_t b){return DoomCalloc(n,b);}
void *__wrap__realloc_r(_reent *,void *p,size_t n){return DoomRealloc(p,n);}
void __wrap__free_r(_reent *,void *p){DoomFree(p);}
#endif
unsigned char *DoomZone(int *bytes){*bytes=host->guest_ram_bytes;return host->guest_ram;}
void DoomZoneFailure(unsigned requested,unsigned pinned,unsigned purgeable,unsigned available){
    stats.zoneRequest=requested;stats.zonePinned=pinned;stats.zonePurgeable=purgeable;stats.zoneFree=available;
}
void DoomZoneAllocated(unsigned bytes){stats.zoneUsed+=bytes;if(stats.zoneUsed>stats.zoneHighWater)stats.zoneHighWater=stats.zoneUsed;}
void DoomZoneReleased(unsigned bytes){if(bytes>stats.zoneUsed)MHS_DoomFatal("zone accounting underflow");stats.zoneUsed-=bytes;}
void *emu_Malloc(int n){return n>0?DoomCalloc(1,n):nullptr;}
void *emu_MallocI(unsigned n){return DoomCalloc(1,n);}
void emu_Free(void *p){DoomFree(p);}
void emu_GetTimeOfDay(int *us,int *seconds){auto now=host->micros_now();*us=now%1000000;*seconds=now/1000000;systime=now/1000;}
void delay(unsigned long n){if(n>=10000)MHS_DoomFatal("Doom platform delay failure");}
void emu_DrawLine16(unsigned short *,int,int,int){}
void emu_printf(const char *){}
char *strupr(char *s){if(s)for(auto p=s;*p;p++)if(*p>='a'&&*p<='z')*p-=32;return s;}
FRESULT f_open(FIL *out,const char *path,unsigned char mode){
    if(!out)return FR_INVALID_PARAMETER;*out=nullptr;
    if(!path||strcmp(path,wadPath)||(mode&~FA_READ))return FR_NO_FILE;
    for(auto &f:files)if(!f.handle){VmFileInfo info{};auto h=host->open(path,&info);
        if(!h)return FR_NO_FILE;if(info.directory||info.bytes>INT_MAX){host->close(h);return FR_INVALID_OBJECT;}
        f={h,0,info.bytes};*out=reinterpret_cast<FIL>(&f);return FR_OK;}
    return FR_TOO_MANY_OPEN_FILES;
}
FRESULT f_close(FIL *p){auto f=file(p);if(!f)return FR_INVALID_OBJECT;host->close(f->handle);*f={};*p=nullptr;return FR_OK;}
FRESULT f_read(FIL *p,void *buffer,unsigned n,unsigned *got){
    if(got)*got=0;auto f=file(p);if(!f||(!buffer&&n))return FR_INVALID_OBJECT;
    if(n>f->bytes-f->cursor)n=f->bytes-f->cursor;
    auto count=host->read(f->handle,f->cursor,buffer,n);stats.reads++;
    if(count<0||uint32_t(count)>n)return FR_DISK_ERR;
    f->cursor+=count;stats.readBytes+=count;if(got)*got=count;return FR_OK;
}
FRESULT f_readn(FIL *p,void *buffer,unsigned n,unsigned *got){
    unsigned total=0;if(got)*got=0;
    if(!buffer&&n)return FR_INVALID_PARAMETER;
    while(total<n){unsigned count=0;auto r=f_read(p,static_cast<uint8_t *>(buffer)+total,n-total,&count);
        total+=count;if(got)*got=total;if(r!=FR_OK||!count)return FR_DISK_ERR;}
    return FR_OK;
}
FRESULT f_lseek(FIL *p,unsigned long n){auto f=file(p);if(!f||n>f->bytes)return FR_DISK_ERR;f->cursor=n;return FR_OK;}
unsigned long f_tell(FIL *p){auto f=file(p);return f?f->cursor:0;}
unsigned long f_size(FIL *p){auto f=file(p);return f?f->bytes:0;}
FRESULT f_write(FIL *,const void *,unsigned,unsigned *n){if(n)*n=0;return FR_WRITE_PROTECTED;}
FRESULT f_writen(FIL *p,const void *b,unsigned n,unsigned *w){return f_write(p,b,n,w);}
FRESULT f_unlink(const char *){return FR_WRITE_PROTECTED;}
FRESULT f_rename(const char *,const char *){return FR_WRITE_PROTECTED;}
FRESULT f_mkdir(const char *){return FR_WRITE_PROTECTED;}
FRESULT f_stat(const char *p,FILINFO *out){
    if(!p||strcmp(p,wadPath))return FR_NO_FILE;VmFileInfo info{};auto h=host->open(p,&info);
    if(!h)return FR_NO_FILE;host->close(h);if(info.directory||info.bytes>INT_MAX)return FR_INVALID_OBJECT;
    if(out){memset(out,0,sizeof *out);out->fsize=info.bytes;}return FR_OK;
}
}

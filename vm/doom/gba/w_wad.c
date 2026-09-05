// SPDX-License-Identifier: GPL-2.0-or-later
// Persistent engine pointers stay pinned. Render-only resources become
// purgeable only after the complete frame, when all consumers have finished.
#include "doomdef.h"
#include "w_wad.h"
#include "core_api.h"
static filelump_t *directory;
static void **cached;
static unsigned char *lifetime;
static unsigned count;
void ExtractFileBase(const char *path,char *dest){
    const char *base=path;for(const char *p=path;*p;p++)if(*p=='/'||*p=='\\'||*p==':')base=p+1;
    memset(dest,0,8);for(unsigned i=0;i<8&&base[i]&&base[i]!='.';i++)dest[i]=toupper((unsigned char)base[i]);
}
void W_Init(void){
    if(directory)return;
    wadinfo_t header;
    GbaRead(0,&header,sizeof header);
    unsigned bytes=GbaFileSize();
    if(memcmp(header.identification,"IWAD",4)||header.numlumps<=0||header.numlumps>4096||
       header.infotableofs<12||(unsigned)header.infotableofs>bytes||
       (unsigned)header.numlumps>(bytes-header.infotableofs)/sizeof(filelump_t))
        GbaFatal("Invalid converted IWAD directory");
    count=header.numlumps;
    directory=GbaSupportAlloc(count*sizeof *directory);
    cached=GbaSupportAlloc(count*sizeof *cached);
    lifetime=GbaSupportAlloc(count);
    GbaRead(header.infotableofs,directory,count*sizeof *directory);
    for(unsigned i=0;i<count;i++){
        const filelump_t *l=&directory[i];
        if(l->filepos<0||l->size<0||(unsigned)l->filepos>bytes||(unsigned)l->size>bytes-l->filepos)
            GbaFatal("Invalid converted IWAD lump extent");
    }
}
int W_CheckNumForName(const char *name){
    char key[8]={0};for(unsigned i=0;i<8&&name[i];i++)key[i]=toupper((unsigned char)name[i]);
    for(int i=(int)count-1;i>=0;i--)if(!memcmp(directory[i].name,key,8))return i;
    return -1;
}
int W_GetNumForName(const char *name){int i=W_CheckNumForName(name);if(i<0)I_Error("Missing lump %.8s",name);return i;}
static const filelump_t *entry(int n){if(n<0||(unsigned)n>=count)GbaFatal("Invalid WAD lump number");return &directory[n];}
const char *W_GetNameForNum(int n){return entry(n)->name;}
int W_LumpLength(int n){return entry(n)->size;}
static const void *load(int n,unsigned char life){
    const filelump_t *l=entry(n);
    if(!cached[n]){
        cached[n]=Z_Malloc(l->size?l->size:4,PU_STATIC,&cached[n]);
        GbaRead(l->filepos,cached[n],l->size);GbaResourceLoaded(n,l->size);
    }
    if(lifetime[n]!=1)lifetime[n]=life;
    GbaSetZoneTag(cached[n],PU_STATIC);
    return cached[n];
}
const void *W_CacheLumpNum(int n){return load(n,1);}
const void *GbaFrameLump(int n){return load(n,2);}
void GbaEndFrame(void){for(unsigned i=0;i<count;i++)if(cached[i]&&lifetime[i]==2)GbaSetZoneTag(cached[i],PU_CACHE);}
int GbaPatchWidth(int n){const filelump_t *l=entry(n);short width;if(l->size<8)GbaFatal("Short patch header");GbaRead(l->filepos,&width,sizeof width);return width;}

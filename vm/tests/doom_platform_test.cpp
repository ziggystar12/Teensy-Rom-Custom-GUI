// SPDX-License-Identifier: GPL-2.0-or-later
#include "../doom/platform.h"
#include "ff.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern "C" void MHS_DoomFatal(const char *){abort();}
static bool opened,readError;
static unsigned closeCount;
static const uint8_t contents[]={10,20,30,40,50};
static uint32_t openFile(const char *p,VmFileInfo *info){assert(!strcmp(p,"/WADS/DOOM1.WAD"));if(opened)return 0;*info={};info->bytes=5;opened=true;return 1;}
static int32_t readFile(uint32_t h,uint32_t offset,void *out,uint32_t bytes){
    assert(h==1&&opened&&offset<=5&&bytes<=5-offset);if(readError)return -1;
    // Force partial reads; the core's exact-read adapter must assemble them.
    if(bytes>2)bytes=2;memcpy(out,contents+offset,bytes);return bytes;
}
static void closeFile(uint32_t h){assert(h==1&&opened);opened=false;closeCount++;}
int main(){
    alignas(8) uint8_t memory[1024];VmHost host{};host.open=openFile;host.read=readFile;host.close=closeFile;
    assert(doomvm::prepare(&host,memory,sizeof memory,"/WADS/DOOM1.WAD"));
    FIL file=nullptr;assert(f_open(&file,"/WADS/DOOM1.WAD",FA_WRITE)==FR_NO_FILE);
    assert(f_open(&file,"/WADS/OTHER.WAD",FA_READ)==FR_NO_FILE);
    assert(f_open(&file,"/WADS/DOOM1.WAD",FA_READ)==FR_OK&&f_size(&file)==5);
    uint8_t result[8]{};unsigned n=99;
    assert(f_readn(&file,result,5,&n)==FR_OK&&n==5&&!memcmp(result,contents,5));
    assert(f_tell(&file)==5&&f_lseek(&file,6)==FR_DISK_ERR&&f_tell(&file)==5);
    assert(f_readn(&file,result,1,&n)==FR_DISK_ERR&&n==0);
    assert(f_lseek(&file,0)==FR_OK);readError=true;
    assert(f_readn(&file,result,5,&n)==FR_DISK_ERR&&n==0&&f_tell(&file)==0);readError=false;
    assert(f_readn(&file,nullptr,1,&n)==FR_INVALID_PARAMETER&&n==0);
    FIL foreign=reinterpret_cast<FIL>(uintptr_t(1234));
    assert(f_read(&foreign,result,1,&n)==FR_INVALID_OBJECT&&n==0);
    assert(f_write(&file,result,5,&n)==FR_WRITE_PROTECTED&&n==0);
    doomvm::closeFiles();assert(!opened&&closeCount==1);
    assert(f_close(&file)==FR_INVALID_OBJECT);
    puts("PASS: bounded read-only WAD handles, partial reads, EOF, I/O errors, seek bounds and close-on-failure");
}

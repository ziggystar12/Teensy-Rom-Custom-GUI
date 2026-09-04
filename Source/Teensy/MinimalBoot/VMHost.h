// SPDX-License-Identifier: MIT
#pragma once
#include "Common/VMFiles.h"
namespace VmRuntime {
using namespace VmFiles;
static VmRegistry::Launch launch;
static VmRegistry::Manifest manifest;
static VmHost host;
static const VmModule *module;
static VmPacket packet;
static uint8_t sequence;
static bool pending;
static volatile bool started;
static volatile bool active,startRequested,inputPending;
static volatile VmInput input;
static uint8_t failure;
static volatile bool quietRequested;
static uint32_t sliceStarted;
static void moduleFail(uint8_t error,uint32_t detail);
static void codeAccess(bool loading){
    // Core region 1 makes all ITCM read-only. A higher-priority region grants
    // only the module window RW+XN while loading, then restores RO+execute.
    uint32_t mask;__asm__ volatile("mrs %0, primask":"=r"(mask));__disable_irq();
    __asm__ volatile("dsb":::"memory");SCB_MPU_CTRL=0;
    // 96 KiB window: 32 KiB at 0x18000, then 64 KiB at 0x20000.
    for(unsigned i=0;i<2;i++){
        SCB_MPU_RBAR=(i?0x20000u:0x18000u)|SCB_MPU_RBAR_VALID|(11+i);
        SCB_MPU_RASR=SCB_MPU_RASR_TEX(1)|SCB_MPU_RASR_AP(loading?3:7)|
            (loading?SCB_MPU_RASR_XN:0)|SCB_MPU_RASR_SIZE(i?15:14)|SCB_MPU_RASR_ENABLE;
    }
    SCB_MPU_CTRL=SCB_MPU_CTRL_ENABLE;__asm__ volatile("dsb\nisb":::"memory");
    if(!mask)__enable_irq();
}
static uint32_t timeNow(){return micros();}
static bool shouldYield(){return inputPending||quietRequested||(pending&&EZFlashRAM[0xf6]==sequence)||uint32_t(micros()-sliceStarted)>=1500;}
static bool loadModule(){
    char path[128];snprintf(path,sizeof path,"%s/%s",launch.root,manifest.module);
    FsFile f=SD.sdfs.open(path,O_RDONLY);VmImageHeader h{};
    if(!f||f.isDirectory()||f.fileSize()>UINT32_MAX||f.read(&h,sizeof h)!=sizeof h||!vm_valid_header(h,f.fileSize())){f.close();failure=0x11;return false;}
    // Bounds are checked before writing either arena. RAM2 is never host heap.
    auto code=(uint8_t *)VM_CODE_BASE;auto data=(uint8_t *)VM_DATA_BASE;
    codeAccess(true);
    if(f.read(code,h.code_bytes)!=(int)h.code_bytes||f.read(data,h.data_bytes)!=(int)h.data_bytes){f.close();codeAccess(false);failure=0x12;return false;}f.close();codeAccess(false);
    uint32_t c=~0u;
    for(unsigned part=0;part<2;part++){auto p=part?data:code;uint32_t n=part?h.data_bytes:h.code_bytes;
        while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}}
    if(~c!=h.payload_crc){failure=0x13;return false;}
    memset(data+h.data_bytes,0,h.bss_bytes);
    __asm__ volatile("dsb\nisb":::"memory");
    const uint32_t used=(h.data_bytes+h.bss_bytes+31u)&~31u;
    host={VM_ABI,sizeof(VmHost),VM_SERVICES,data+used,VM_DATA_BYTES-used,launch.root,launch.content,timeNow,openFile,readFile,nextFile,closeFile,
        (uint8_t *)VM_RAM_BASE,VM_RAM_BYTES,openFlags,writeFile,fileOp,shouldYield,moduleFail};
    module=reinterpret_cast<VmEntry>(h.entry)(&host);
    // Native modules are trusted, but reject corrupt API pointers before calling.
    auto codePointer=[](uintptr_t p){return (p&1)&&(p&~1u)>=VM_CODE_BASE&&(p&~1u)<VM_CODE_LIMIT;};
    const uintptr_t p=(uintptr_t)module;
    if(p<VM_CODE_BASE||p>VM_CODE_LIMIT-sizeof(VmModule)||module->abi!=VM_ABI||module->bytes!=sizeof(VmModule)||
       !codePointer((uintptr_t)module->input)||!codePointer((uintptr_t)module->pump)||!codePointer((uintptr_t)module->packet)||!codePointer((uintptr_t)module->ack)){if(!failure)failure=0x14;module=nullptr;return false;}
    return true;
}
static uint16_t crc16(const uint8_t *p,unsigned n){uint16_t c=0xffff;while(n--){c^=(uint16_t)*p++<<8;for(unsigned b=0;b<8;b++)c=(c<<1)^((c&0x8000)?0x1021:0);}return c;}
static void fail(uint8_t error){failure=error;EZFlashRAM[0xfb]=error;__asm__ volatile("dmb":::"memory");EZFlashRAM[0xf5]=0xe0;}
static void moduleFail(uint8_t error,uint32_t detail){
    EZFlashRAM[0xf8]=detail;EZFlashRAM[0xf9]=detail>>8;EZFlashRAM[0xfa]=detail>>16;
    fail(error?error:0x16);
}
}
// Called only by the stock EasyFlash IO2 handler. No file or VM code in ISR.
bool VMHostIO2(uint8_t address,bool read){
    using namespace VmRuntime;if(!active||CurrentEasyFlashBank!=58)return false;
    if(read){DataPortWriteWaitLog(EZFlashRAM[address]);return true;}
    const uint8_t value=DataPortWaitRead();TraceLogAddValidData(value);
    if(address==0xf6||(address>=0xf8&&address<=0xfa)||address>=0xfd)EZFlashRAM[address]=value;
    if(address==0xf4){
        EZFlashRAM[address]=value;
        if(value==1&&!started)startRequested=true;
        if(value==4)quietRequested=true;
        if(value==3&&!inputPending&&EZFlashRAM[0xfe]&&EZFlashRAM[0xfe]!=EZFlashRAM[0xfc]){
            if((uint8_t)(0xa5^EZFlashRAM[0xf8]^EZFlashRAM[0xf9]^EZFlashRAM[0xfa]^EZFlashRAM[0xfd]^EZFlashRAM[0xfe])==EZFlashRAM[0xff]){
                input.buttons=EZFlashRAM[0xf8];input.display=EZFlashRAM[0xf9];input.overflow=EZFlashRAM[0xfa];input.protocol=EZFlashRAM[0xfd];
                __asm__ volatile("dmb":::"memory");inputPending=true;EZFlashRAM[0xfc]=EZFlashRAM[0xfe];
            }
        }
    }return true;
}
void VMHostPoll(){
    using namespace VmRuntime;if(!active)return;
    if(startRequested&&!started){started=true;startRequested=false;if(failure){fail(failure);return;}EZFlashRAM[0xf5]=2;}
    if(!started||failure||!module)return;
    if(inputPending){VmInput in{input.buttons,input.display,input.overflow,input.protocol};inputPending=false;module->input(&in);}
    sliceStarted=micros();
    if(quietRequested){EZFlashRAM[0xf5]=0x12;}else module->pump();
    if(failure)return;
    if(pending){if(EZFlashRAM[0xf6]!=sequence)return;module->ack();pending=false;quietRequested=false;EZFlashRAM[0xf5]=2;}
    if(quietRequested)return;
    if(!module->packet(&packet))return;
    if(failure)return;
    if(packet.length>228||packet.reserved||!packet.type){fail(0x15);return;}
    uint8_t bytes[240];sequence=sequence==255?1:sequence+1;
    bytes[0]='M';bytes[1]='3';bytes[2]=1;bytes[3]=packet.type;bytes[4]=sequence;bytes[5]=packet.flags;bytes[6]=packet.length;bytes[7]=0;
    memcpy(bytes+8,packet.payload,packet.length);const auto crc=crc16(bytes,8+packet.length);bytes[8+packet.length]=crc;bytes[9+packet.length]=crc>>8;
    for(unsigned i=0;i<10u+packet.length;i++)EZFlashRAM[i]=bytes[i];
    pending=true;__asm__ volatile("dmb":::"memory");EZFlashRAM[0xf7]=sequence;
}
bool VMHostBoot(){
    using namespace VmRuntime;
    if(!SD.sdfs.begin(SdioConfig(FIFO_SDIO)))return false;
    if(!VmRegistry::consume(launch)||!VmRegistry::readManifest(launch.root,manifest)||manifest.crc!=launch.manifest_crc)return false;
    char path[128];snprintf(path,sizeof path,"%s/%s",launch.root,manifest.client);
    FsFile f=SD.sdfs.open(path,O_RDONLY);uint8_t header[64],chip[16];
    if(!f||f.read(header,64)!=64||memcmp(header,"C64 CARTRIDGE   ",16)||header[23]!=32){f.close();return false;}
    for(unsigned i=0;i<2;i++){
        if(f.read(chip,16)!=16||memcmp(chip,"CHIP",4)||chip[10]||chip[11]||chip[12]!=(i?0xa0:0x80)||chip[13]||chip[14]!=0x20||chip[15]||f.read(RAM_Image+i*8192,8192)!=8192){f.close();return false;}
    }
    uint8_t descriptor[128];
    if(f.read(chip,16)!=16||memcmp(chip,"CHIP",4)||chip[10]||chip[11]!=1||chip[12]!=0x80||chip[13]||
       f.read(descriptor,128)!=128||memcmp(descriptor,"VMH1",4)||descriptor[4]!=VM_ABI||!memchr(descriptor+16,0,24)||
       strcmp((char *)descriptor+16,manifest.id)||vm_crc32(descriptor,124)!=*(uint32_t *)(descriptor+124)||
       vm_crc32(RAM_Image,16384)!=*(uint32_t *)(descriptor+8)){f.close();return false;}
    f.close();
    NumCrtChips=0;memset(EZFlashRAM,0,sizeof EZFlashRAM);CurrentEasyFlashBank=0;
    SetGameAssert;SetExROMDeassert;
    for(unsigned i=0;i<64;i++){BankDecode[i][0]=RAM_Image;BankDecode[i][1]=RAM_Image+8192;}
    LOROM_Image=RAM_Image;HIROM_Image=RAM_Image+8192;LOROM_Mask=HIROM_Mask=8191;
    CurrentIOHandler=IOH_EasyFlash;EmulateVicCycles=false;
    memcpy(EZFlashRAM+0xf0,"M3TP",4);EZFlashRAM[0xf5]=0;
    active=true;loadModule(); // Failures remain readable by the C64 client.
    doReset=true;return true;
}

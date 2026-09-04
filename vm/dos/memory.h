// Compact PC/XT hardware backing in RAM1. All conventional RAM stays in RAM2.
#pragma once
struct DosMemory {
    uint8_t *guest;
    uint8_t video[32768]{},consoleReadback[4096]{},consoleCompare[4096]{},ports[4096]{};
    bool reset(){memset(guest,0,VM_RAM_BYTES);memset(video,0,sizeof video);memset(consoleReadback,0,sizeof consoleReadback);memset(consoleCompare,0,sizeof consoleCompare);memset(ports,0,sizeof ports);return true;}
    // Absent expansion/video/ROM addresses are open bus (zero); unsupported
    // high I/O ports read FF and ignore writes. No modulo alias into guest RAM.
    bool transfer(uint32_t a,uint8_t *data,uint32_t n,bool writing){
        if(a>mpe5::NativeBackingBytes||n>mpe5::NativeBackingBytes-a)return false;
        while(n){uint8_t *p=nullptr;uint32_t end=0;uint8_t absent=0;
            if(a<VM_RAM_BYTES){p=guest+a;end=VM_RAM_BYTES;}
            else if(a<0xb8000)end=0xb8000;
            else if(a<0xc0000){p=video+a-0xb8000;end=0xc0000;}
            else if(a<0xc1000){p=consoleReadback+a-0xc0000;end=0xc1000;}
            else if(a<0xc8000)end=0xc8000;
            else if(a<0xc9000){p=consoleCompare+a-0xc8000;end=0xc9000;}
            else if(a<0xf0000)end=0xf0000;
            else if(a<0x100000)return false; // F000 is pinned by the core.
            else if(a<mpe5::AddressMapBytes){p=guest+a-0x100000;end=mpe5::AddressMapBytes;}
            else if(a<mpe5::AddressMapBytes+sizeof ports){p=ports+a-mpe5::AddressMapBytes;end=mpe5::AddressMapBytes+sizeof ports;}
            else if(a<mpe5::AddressMapBytes+mpe5::NativeIoPortBytes){end=mpe5::AddressMapBytes+mpe5::NativeIoPortBytes;absent=0xff;}
            else return false;
            uint32_t take=end-a;if(take>n)take=n;
            if(writing){if(p)memcpy(p,data,take);}else if(p)memcpy(data,p,take);else memset(data,absent,take);
            a+=take;data+=take;n-=take;
        }return true;
    }
};

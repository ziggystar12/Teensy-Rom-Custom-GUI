#include "../../engine/gnuboy/machine.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void checksum(std::vector<uint8_t> &rom){
    uint8_t c=0;for(unsigned i=0x134;i<=0x14c;i++)c=uint8_t(c-rom[i]-1);rom[0x14d]=c;
}
int main(){
    std::vector<uint8_t> rom(524288);
    for(unsigned bank=0;bank<32;bank++)rom[bank*16384]=bank;
    rom[0x148]=4;rom[0x149]=2;
    for(unsigned type:{2u,3u,0x1au,0x1bu}){
        rom[0x147]=type;checksum(rom);assert(!gb::inspect(rom.data(),rom.size()));
        assert(gb::start(rom.data(),rom.size(),{}));
        assert(gb::saveBytes()==(type==3||type==0x1b?8192u:0u));
        assert(gb::peek(0xa000)==255);gb::poke(0xa000,42);
        gb::poke(0,10);assert(gb::peek(0xa000)==255);
        gb::poke(0xa000,17);gb::poke(0xbfff,23);const auto rev=gb::saveRevision();
        gb::poke(0xbfff,23);assert(gb::saveRevision()==rev);
        for(unsigned bank=0;bank<64;bank++){
            gb::poke(0x2000,bank);gb::poke(0x4000,bank);gb::poke(0x6000,bank&1);
            assert(gb::peek(0x4000)==(type<4?((bank&31)?bank&31:1):bank&31));
            assert(gb::peek(0xa000)==17&&gb::peek(0xbfff)==23);
        }
        gb::poke(0,0);gb::poke(0xa000,66);assert(gb::peek(0xa000)==255);
        gb::poke(0,0xfa);assert(gb::peek(0xa000)==17);
        assert(gb::start(rom.data(),rom.size(),{}));gb::poke(0,10);assert(gb::peek(0xa000)==255);
    }
    rom[0x147]=3;rom[0x149]=3;checksum(rom);assert(gb::inspect(rom.data(),rom.size()));
    rom[0x149]=2;rom[0x148]=5;checksum(rom);assert(gb::inspect(rom.data(),rom.size()));
    rom[0x148]=4;rom[0x147]=0x10;checksum(rom);assert(gb::inspect(rom.data(),rom.size()));
    rom[0x147]=1;rom[0x149]=0;checksum(rom);assert(gb::start(rom.data(),rom.size(),{}));
    gb::poke(0,10);gb::poke(0xa000,17);assert(gb::peek(0xa000)==255&&!gb::saveBytes());
    rom[0x14d]^=1;assert(gb::inspect(rom.data(),rom.size()));
    puts("PASS GB cartridge profiles: MBC1/MBC5 8KiB RAM, battery flags, all bank/register aliases, disabled RAM, SRAM reset, unsupported sizes/RTC and checksum rejection");
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Independent MPE adaptation of MCUME's gnuboy. See README.md for provenance.
#include "machine.h"
#include <cstring>
#include <cstdlib>
#include <cstdarg>
namespace gbcore {
static const char *fault;
static const uint8_t *rom_data;
static size_t rom_size;
static uint64_t elapsed;
static gb::Video output;
static bool capture_pixels;
static uint32_t blank_units;
static uint8_t trigger[4],seen_trigger[4],previous_control[3];
static void core_fault(const char *,...) {fault="UNSUPPORTED GB CPU OPCODE";}
#include "core/defs.h"
#include "core/cpu.h"
#include "core/hw.h"
#include "core/lcd.h"
#include "core/mem.h"
#include "core/rtc.h"
#include "core/sound.h"
#include "core/regs.h"
static void sound_flush();
void hw_interrupt(byte,byte);void hw_dma(byte);void hw_hdma();void hw_hdma_cmd(byte);
void pad_refresh();void lcdc_trans();void lcdc_change(byte);void stat_write(byte);void stat_trigger();
void lcd_refreshline();void pal_write(int,byte);void pal_write_dmg(int,int,byte);void vram_write(int,byte);
void rtc_latch(byte);void rtc_write(byte);void sound_write(byte,byte);byte sound_read(byte);
static void emu_DrawLine16(uint16 *,int,int,int y){if(output.line&&capture_pixels&&y>=0&&y<144)output.line(output.context,y,scan.buf,scan.pal2);}
byte read_rom(unsigned address){return address<rom_size?rom_data[address]:0xff;}
#include "core/rtc.c"
#include "core/sound.c"
static void sound_flush(){
    if(snd.rate>0 && cpu.snd>=snd.rate){int n=cpu.snd/snd.rate;cpu.snd%=snd.rate;audio_play_sample(nullptr,nullptr,n*2);}
}
#include "core/lcd.c"
#undef C
#include "core/lcdc.c"
#undef C
#include "core/hw.c"
#include "core/mem.c"
#undef L
#include "core/cpu.c"
}
// Macro cleanup is generated alongside this unity import.
#include "core/undef.h"
namespace gb {
const char *inspect(const uint8_t *r,size_t n){
    if(!r||n<0x150)return "GB HEADER TOO SHORT";
    uint8_t check=0;for(unsigned i=0x134;i<=0x14c;i++)check=uint8_t(check-r[i]-1);
    if(check!=r[0x14d])return "GB HEADER CHECKSUM FAILED";
    if(r[0x148]>4||n!=(size_t(32768)<<r[0x148]))return "GB ROM SIZE UNSUPPORTED";
    // Initial, deliberately bounded no-save profile. Reject battery cartridges
    // rather than imply that progress will survive a reset.
    if(r[0x147]!=0 && r[0x147]!=1 && r[0x147]!=0x19)return "GB MAPPER/SAVES NOT SUPPORTED YET";
    if(r[0x149])return "GB CARTRIDGE RAM NOT SUPPORTED YET";
    if(r[0x147]==0 && n!=32768)return "INVALID UNBANKED GB ROM";
    return nullptr;
}
bool start(const uint8_t *r,size_t n,Video v){
    using namespace gbcore;fault=inspect(r,n);if(fault)return false;
    rom_data=r;rom_size=n;elapsed=0;blank_units=0;output=v;capture_pixels=true;
    memset(&cpu,0,sizeof cpu);memset(&hw,0,sizeof hw);memset(&ram,0,sizeof ram);
    memset(&scan,0,sizeof scan);memset(&rtc,0,sizeof rtc);memset(&mbc,0,sizeof mbc);
    memset(trigger,0,sizeof trigger);memset(seen_trigger,0,sizeof seen_trigger);memset(previous_control,0,sizeof previous_control);
    hw.cgb=(r[0x143]&0x80)!=0;mbc.type=r[0x147]==1?1:r[0x147]==0x19?5:0;
    mbc.romsize=n/16384;mbc.ramsize=0;
    hw_reset();cpu_reset();mbc_reset();lcd_reset();sound_reset(22050);return true;
}
unsigned run(unsigned units){
    using namespace gbcore;if(fault||!units)return 0;
    if(units>128)units=128;
    const unsigned before=ram.hi[0x44];const unsigned actual=cpu_emulate(units);elapsed+=actual;
    const unsigned after=ram.hi[0x44];
    if(!(ram.hi[0x40]&0x80)){
        blank_units+=actual;if(blank_units>=35112){blank_units-=35112;
            if(capture_pixels&&output.line){uint8_t row[160]{};uint16_t pal[64]{};pal[0]=0xffff;
                for(unsigned y=0;y<144;y++)output.line(output.context,y,row,pal);}
            if(output.frame)output.frame(output.context);}
    }else{
        blank_units=0;if(before<144&&after>=144&&output.frame)output.frame(output.context);
    }
    return actual;
}
void buttons(uint8_t b){using namespace gbcore;hw.pad=((b&1)<<4)|((b&2)<<4)|((b&4)<<4)|((b&8)<<4)|((b&16)>>2)|((b&32)>>2)|((b&64)>>5)|((b&128)>>7);pad_refresh();}
void capture(bool on){gbcore::capture_pixels=on;}
const char *error(){return gbcore::fault;}
bool color(){return gbcore::hw.cgb;}
uint64_t ticks(){return gbcore::elapsed;}
uint8_t peek(uint16_t a){return gbcore::mem_read(a);}
void poke(uint16_t a,uint8_t b){gbcore::mem_write(a,b);}
void sid(uint8_t packet[26],uint32_t clock){
    using namespace gbcore;memset(packet,0,26);auto p=packet+1;
    const auto regs=ram.hi;const unsigned routing=regs[0x25];
    for(unsigned voice=0;voice<3;voice++){
        unsigned ch=voice;
        if(voice==2 && snd.ch[3].on && snd.ch[3].envol && (routing&0x88))ch=3;
        const unsigned base=ch==0?0x11:ch==1?0x16:0x1b;
        const unsigned timer=(regs[base+2]|((regs[base+3]&7)<<8));
        unsigned volume=ch==2?((regs[0x1c]>>5)&3):snd.ch[ch].envol;
        if(ch==2)volume=volume?15>>(volume-1):0;
        bool on=snd.ch[ch].on&&volume&&(routing&(0x11<<ch))&&(regs[0x26]&0x80);
        uint64_t num=uint64_t(ch==2?65536:131072)<<24,den=uint64_t(2048-timer)*clock;
        if(ch==3){unsigned div=regs[0x22]&7;num=uint64_t(1048576)<<24;den=uint64_t(div?div*2:1)*(1u<<((regs[0x22]>>4)+1))*clock;}
        uint64_t f=(num+den/2)/den;if(f>65535)f=65535;
        auto out=p+voice*7;out[0]=f;out[1]=f>>8;
        static const uint16_t widths[]={512,1024,2048,3072};const unsigned width=ch<2?widths[regs[base]>>6]:0;
        out[2]=width;out[3]=width>>8;out[4]=(ch<2?0x40:ch==2?0x10:0x80)|(on?1:0);out[6]=volume<<4;
        if(on&&(trigger[ch]!=seen_trigger[ch]||previous_control[voice]!=out[4]))packet[0]|=1<<voice;
        seen_trigger[ch]=trigger[ch];previous_control[voice]=out[4];
    }
    p[24]=15;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Three bounded synthesized voices. Uses the existing C64 SID packet layout;
// no sample cache, PCM mixer, Tandy emulator or additional host service.
#include "core_api.h"
#include "sounds.h"
#include <string.h>
typedef struct {
    uint16_t frequency;
    int16_t slide;
    uint8_t left,duration,volume,wave,retrigger;
} SidVoice;
static SidVoice voices[3];
void GbaSoundReset(void){memset(voices,0,sizeof voices);}
void GbaSoundTick(void){
    for(unsigned i=0;i<3;i++){
        SidVoice *v=&voices[i];v->retrigger=0;
        if(v->left){v->left--;int f=(int)v->frequency+v->slide;v->frequency=f<1?1:f>65535?65535:f;}
    }
}
void GbaSoundStop(int channel){if(channel>=0&&channel<3)memset(&voices[channel],0,sizeof voices[channel]);}
int I_StartSound(int id,int channel,int vol,int sep){
    (void)sep;
    if(channel<0||channel>=3||id<=sfx_None||id>=NUMSFX)return -1;
    SidVoice *v=&voices[channel];memset(v,0,sizeof *v);
    if(vol<=0)return channel;
    v->volume=vol>=120?15:(vol+7)/8;v->retrigger=1;
    v->wave=0x20;v->frequency=2800;v->slide=-100;v->duration=9;
    switch(id){
        case sfx_pistol:case sfx_chgun:
            v->wave=0x80;v->frequency=13000;v->slide=-1900;v->duration=5;break;
        case sfx_shotgn:case sfx_dshtgn:case sfx_rxplod:case sfx_barexp:
            v->wave=0x80;v->frequency=17000;v->slide=-1200;v->duration=10;break;
        case sfx_sgcock:case sfx_swtchn:case sfx_swtchx:
            v->wave=0x80;v->frequency=9000;v->duration=3;break;
        case sfx_doropn:case sfx_dorcls:case sfx_bdopn:case sfx_bdcls:
        case sfx_pstart:case sfx_pstop:case sfx_stnmov:
            v->wave=0x40;v->frequency=1900;v->slide=35;v->duration=18;break;
        case sfx_itemup:case sfx_wpnup:case sfx_getpow:
            v->wave=0x10;v->frequency=8500;v->slide=600;v->duration=8;break;
        case sfx_plpain:case sfx_oof:case sfx_noway:
            v->frequency=2200;v->slide=-170;v->duration=7;break;
        case sfx_pldeth:case sfx_pdiehi:case sfx_podth1:case sfx_podth2:case sfx_podth3:
        case sfx_bgdth1:case sfx_bgdth2:case sfx_sgtdth:
            v->wave=0x80;v->frequency=6000;v->slide=-220;v->duration=18;break;
        default:break;
    }
    v->left=v->duration;return channel;
}
void GbaSoundPayload(uint8_t payload[26]){
    memset(payload,0,26);
    for(unsigned i=0;i<3;i++){
        const SidVoice *v=&voices[i];if(!v->left)continue;
        unsigned base=1+i*7;
        if(v->retrigger)payload[0]|=1u<<i;
        payload[base]=v->frequency;payload[base+1]=v->frequency>>8;
        payload[base+3]=8;payload[base+4]=v->wave|1;
        payload[base+5]=2;
        payload[base+6]=((v->volume*v->left/v->duration)<<4)|2;
        payload[25]=15;
    }
}
void I_InitSound(void){GbaSoundReset();}
void I_PlaySong(int handle,int looping){(void)handle;(void)looping;}
void I_PauseSong(int handle){(void)handle;}
void I_ResumeSong(int handle){(void)handle;}
void I_StopSong(int handle){(void)handle;}
void I_SetMusicVolume(int volume){(void)volume;}

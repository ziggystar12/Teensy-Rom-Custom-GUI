// SPDX-License-Identifier: MIT
// All AGI policy lives here. Firmware V1.1.1 supplies unchanged generic ABI 2.
#include "../abi/vm_abi.h"
#include <cstring>
#include <cstdio>
#include <new>
#define MHS_AGI_EXTERNAL_STATE 1
#include "../../engine/native-game/mpe4_package.cpp"
#include "../../engine/native-game/mpe4_game.cpp"
#include "../../engine/native-game/mpe4_render.cpp"
#include "../../engine/native-game/mpe4_session.cpp"
#define PROGMEM
#include "../nes/font8x8.h"

namespace agivm {
static const VmHost *host;
static constexpr unsigned PageBytes=16384,CachePages=31,Rows=17,MaxGames=128;
static constexpr uint32_t MaxContentBytes=32*1024*1024;
// Only the original ABI-2 prefix is required. Optional callbacks appended by
// newer firmware must not force a firmware upgrade for this packet-only VM.
static constexpr uint32_t RequiredHostBytes=offsetof(VmHost,fail)+sizeof(((VmHost *)nullptr)->fail);
struct Runtime {
    mpe4::Session session;
    char games[MaxGames][96]{},directory[256]{},selectedPath[384]{},message[80]{};
    uint16_t count=0,selected=0;
    uint32_t handle=0,fileBytes=0,tags[CachePages]{},lastTick=0,tickFraction=0;
    uint32_t cacheReads=0,cacheHits=0;
    VmInput events[128]{};
    uint8_t head=0,tail=0,joy=0,pointerX=0,pointerY=0,pointerButtons=0;
    bool pointerDirty=false,game=false,pending=false,prepared=false,presented=false;
    bool error=false,menuDirty=true,menuFull=true,menuReset=false;
    uint16_t cursor=0,baseCells=0;
    VmPacket packet{};
    explicit Runtime(mpe4::State &guest):session(guest) {}
};
static Runtime *runtime;
static_assert(sizeof(Runtime)<100*1024,"AGI support exceeds its RAM1 budget");
static_assert(sizeof(mpe4::State)<=PageBytes,"AGI guest state exceeds first RAM2 page");
static void fail(uint8_t code,uint32_t detail=0){runtime->error=true;host->fail(code,detail);}
static int compare(const char *a,const char *b){
    while(*a&&*b){char x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return uint8_t(x)-uint8_t(y);}
    return uint8_t(*a)-uint8_t(*b);
}
static bool extension(const char *p){auto n=strlen(p);return n>4&&!compare(p+n-4,".AGI");}
static void notice(const char *s){snprintf(runtime->message,sizeof runtime->message,"%s",s);runtime->menuDirty=true;}
static bool readRaw(void *,uint32_t offset,uint8_t *out,uint16_t count){
    auto &r=*runtime;
    if(!r.handle||offset>r.fileBytes||count>r.fileBytes-offset)return false;
    while(count){
        const uint32_t page=offset/PageBytes,slot=page%CachePages,start=page*PageBytes;
        const uint32_t bytes=r.fileBytes-start<PageBytes?r.fileBytes-start:PageBytes;
        auto cache=host->guest_ram+PageBytes+slot*PageBytes;
        if(r.tags[slot]!=page){
            r.tags[slot]=UINT32_MAX;
            if(host->read(r.handle,start,cache,bytes)!=(int32_t)bytes)return false;
            r.tags[slot]=page;r.cacheReads++;
        }else r.cacheHits++;
        uint32_t n=bytes-(offset-start);if(n>count)n=count;
        memcpy(out,cache+offset-start,n);out+=n;offset+=n;count-=n;
    }return true;
}
static bool operation(VmFsOp op,const char *path,const char *destination=nullptr,uint32_t h=0){
    VmFsRequest req{op,h,0,0,path,destination};return host->file_op(&req)==0;
}
static bool exists(const char *path,bool directory=false){VmFileInfo info{};auto h=host->open(path,&info);if(!h)return false;host->close(h);return bool(info.directory)==directory;}
static bool savePath(char *path,size_t bytes,const char *id,uint8_t slot,const char *ext){
    if(!id||strlen(id)!=6||slot<1||slot>12)return false;
    for(unsigned n=0;n<6;n++)if(!((id[n]>='A'&&id[n]<='Z')||(id[n]>='0'&&id[n]<='9')))return false;
    return snprintf(path,bytes,"%s/SAVES/%s%02u.%s",host->package_root,id,slot,ext)<(int)bytes;
}
static bool readSave(const char *path,const char *id,uint16_t epoch,uint8_t slot,mpe4::State *state){
    VmFileInfo info{};uint32_t h=host->open(path,&info);if(!h)return false;
    uint8_t header[32]{};bool ok=!info.directory&&info.bytes==sizeof(*state)+32&&host->read(h,0,header,32)==32;
    auto get=[&](unsigned n){return uint32_t(header[n])|(uint32_t(header[n+1])<<8)|(uint32_t(header[n+2])<<16)|(uint32_t(header[n+3])<<24);};
    ok=ok&&!memcmp(header,"AGSV",4)&&get(4)==1&&!memcmp(header+8,id,6)&&
        (header[14]|uint16_t(header[15])<<8)==epoch&&header[16]==slot&&!header[17]&&!header[18]&&!header[19]&&
        get(20)==sizeof(*state)&&get(28)==vm_crc32(header,28);
    if(ok)ok=host->read(h,32,state,sizeof(*state))==sizeof(*state)&&get(24)==vm_crc32(state,sizeof(*state));
    host->close(h);return ok;
}
static bool save(void *,const char *id,uint16_t epoch,uint8_t slot,const mpe4::State *state,size_t bytes){
    if(bytes!=sizeof(*state))return false;
    char dir[128],temp[160],path[160],backup[160];
    if(snprintf(dir,sizeof dir,"%s/SAVES",host->package_root)>=(int)sizeof dir||
       !savePath(temp,sizeof temp,id,slot,"TMP")||!savePath(path,sizeof path,id,slot,"SAV")||!savePath(backup,sizeof backup,id,slot,"BAK"))return false;
    if(!exists(dir,true)&&!operation(VmFsOp::Mkdir,dir))return false;
    uint8_t header[32]{};memcpy(header,"AGSV",4);memcpy(header+8,id,6);header[14]=epoch;header[15]=epoch>>8;header[16]=slot;
    auto put=[&](unsigned n,uint32_t v){for(unsigned i=0;i<4;i++)header[n+i]=v>>(i*8);};
    put(4,1);put(20,bytes);put(24,vm_crc32(state,bytes));put(28,vm_crc32(header,28));
    VmFileInfo info{};auto h=host->open_flags(temp,VM_OPEN_WRITE|VM_OPEN_CREATE|VM_OPEN_TRUNCATE,&info);if(!h)return false;
    bool ok=host->write(h,0,header,32)==32&&host->write(h,32,state,bytes)==(int32_t)bytes;
    ok=operation(VmFsOp::Flush,nullptr,nullptr,h)&&ok;host->close(h);
    // Restore/checkpoint scratch belongs to RAM1, never the live RAM2 state.
    auto scratch=reinterpret_cast<mpe4::State *>(runtime->session.next);
    if(!ok||!readSave(temp,id,epoch,slot,scratch))return false;
    if(exists(backup)&&!operation(VmFsOp::Remove,backup))return false;
    bool previous=exists(path);if(previous&&!operation(VmFsOp::Rename,path,backup))return false;
    if(operation(VmFsOp::Rename,temp,path))return true;
    if(previous)operation(VmFsOp::Rename,backup,path);return false;
}
static bool restore(void *,const char *id,uint16_t epoch,uint8_t slot,mpe4::State *state,size_t bytes){
    if(bytes!=sizeof(*state))return false;char path[160],backup[160];
    if(!savePath(path,sizeof path,id,slot,"SAV")||!savePath(backup,sizeof backup,id,slot,"BAK"))return false;
    auto scratch=reinterpret_cast<mpe4::State *>(runtime->session.next);
    if(!readSave(path,id,epoch,slot,scratch)&&!readSave(backup,id,epoch,slot,scratch))return false;
    memcpy(state,scratch,bytes);return true;
}
static mpe4::SaveInfo saveInfo(void *,const char *id,uint16_t epoch,uint8_t slot){
    char path[160],backup[160];
    if(!savePath(path,sizeof path,id,slot,"SAV")||!savePath(backup,sizeof backup,id,slot,"BAK"))return {mpe4::SaveUnavailable,0,0};
    // Verify the same primary/backup generation restore will load. The picker
    // opens before rendering, so the unpublished frame is available as scratch.
    auto scratch=reinterpret_cast<mpe4::State *>(runtime->session.next);
    if(readSave(path,id,epoch,slot,scratch)||readSave(backup,id,epoch,slot,scratch))
        return {mpe4::SaveReady,scratch->vars[0],scratch->vars[3]};
    return {exists(path)||exists(backup)?mpe4::SaveUnavailable:mpe4::SaveEmpty,0,0};
}
static bool launch(const char *path){
    auto &r=*runtime;VmFileInfo info{};
    if(!extension(path)||strlen(path)>=sizeof r.selectedPath){notice("INVALID .AGI PATH");return false;}
    r.handle=host->open(path,&info);r.fileBytes=info.bytes;
    if(!r.handle||info.directory||info.bytes<64||info.bytes>MaxContentBytes){if(r.handle)host->close(r.handle);r.handle=0;notice("CANNOT OPEN AGI CONTENT");return false;}
    for(auto &tag:r.tags)tag=UINT32_MAX;
    uint8_t header[64];
    // Standalone indexed M4G2, not a renamed CRT or a title-bridge-dependent pack.
    bool ok=readRaw(nullptr,0,header,64)&&!memcmp(header,"M4G2",4)&&
        (uint32_t(header[8])|(uint32_t(header[9])<<8)|(uint32_t(header[10])<<16)|(uint32_t(header[11])<<24))==info.bytes&&
        (header[32]&1)&&r.session.start(readRaw,nullptr,0,info.bytes,{nullptr,save,restore,saveInfo});
    if(!ok){host->close(r.handle);r.handle=0;r.menuReset=true;notice("INVALID AGI PACKAGE / CRC / STARTUP");return false;}
    strcpy(r.selectedPath,path);r.game=true;r.prepared=false;r.head=r.tail=0;r.pointerDirty=false;r.joy=0;
    r.lastTick=host->micros_now();r.tickFraction=0;return true;
}
static void listGames(){
    auto &r=*runtime;VmFileInfo info{};auto h=host->open(r.directory,&info);
    if(!h||!info.directory){if(h)host->close(h);notice("ADD .AGI FILES TO VMS/AGIVM/GAMES");return;}
    int32_t result=0;
    while((result=host->next(h,&info))==1){
        if(info.directory||!extension(info.name)||strchr(info.name,'/')||strchr(info.name,'\\'))continue;
        if(r.count==MaxGames){notice("FIRST 128 GAMES SHOWN");break;}
        strcpy(r.games[r.count++],info.name);
    }host->close(h);if(result<0)notice("DIRECTORY READ FAILED");
    char name[96];for(unsigned i=1;i<r.count;i++){strcpy(name,r.games[i]);unsigned j=i;while(j&&compare(name,r.games[j-1])<0){strcpy(r.games[j],r.games[j-1]);j--;}strcpy(r.games[j],name);}
    if(!r.count&&!r.message[0])notice("ADD .AGI FILES TO VMS/AGIVM/GAMES");
}
static void textRow(unsigned row,const char *text,uint8_t color=0xf0){
    auto &s=runtime->session;for(unsigned col=0;col<40;col++){
        uint8_t ch=*text?uint8_t(*text++):' ';if(ch>='a'&&ch<='z')ch-=32;if(ch>=128)ch='?';
        unsigned cell=row*40+col;memcpy(s.next+cell*8,MPE5Font8x8[ch],8);s.next[8000+cell]=color;s.next[9000+cell]=0;
    }
}
static void menuFrame(){
    auto &r=*runtime;memset(r.session.next,0,sizeof r.session.next);
    textRow(0,"MHS AGIVM - SELECT A GAME");textRow(1,"RAM1 ENGINE / RAM2 GAME MEMORY");
    unsigned page=r.selected/Rows,first=page*Rows;
    for(unsigned n=0;n<Rows;n++)if(first+n<r.count)textRow(3+n,r.games[first+n],first+n==r.selected?0x1e:0xf0);
    textRow(21,r.message);char line[41];snprintf(line,sizeof line,"PAGE %u/%u  %u GAMES",page+1,r.count?(r.count+Rows-1)/Rows:1,r.count);textRow(22,line);
    textRow(23,"UP/DOWN:SELECT LEFT/RIGHT:PAGE");textRow(24,"RETURN OR JOYSTICK 2 FIRE:RUN");
    r.cursor=0;r.prepared=true;r.menuDirty=false;r.menuFull=!r.presented||r.menuReset;r.menuReset=false;
}
static void enqueue(VmInput in){auto &r=*runtime;uint8_t next=(r.tail+1)&127;if(next==r.head){fail(0x36);return;}r.events[r.tail]=in;r.tail=next;}
static void input(const VmInput *in){
    auto &r=*runtime;if(r.error||!in)return;uint8_t flags=in->protocol;
    if(!(flags&7)||(flags&~31)||((flags&5)==5)||(!(flags&4)&&(flags&24))||(in->overflow&~31))return;
    if((flags&4)&&(in->buttons>=160||in->display>=200))return;
    if(flags&1)enqueue({in->buttons,in->display,0,1});
    if(flags&2){
        const uint8_t rising=in->overflow&~r.joy;
        if(!r.game){uint8_t key=rising&1?mpe4::Up:rising&2?mpe4::Down:rising&4?mpe4::Left:rising&8?mpe4::Right:rising&16?mpe4::Enter:0;if(key)enqueue({key,0,0,1});}
        else if(rising&16)enqueue({0,0,16,2});
        r.joy=in->overflow;
    }
    if(flags&4){
        uint8_t buttons=(flags>>3)&3;
        if(buttons!=r.pointerButtons)enqueue({in->buttons,in->display,0,flags});
        r.pointerX=in->buttons;r.pointerY=in->display;r.pointerButtons=buttons;r.pointerDirty=true;
    }
}
static void pump(){
    auto &r=*runtime;if(r.error||r.pending||r.prepared)return;
    if(!r.game){
        while(r.head!=r.tail){auto in=r.events[r.head];r.head=(r.head+1)&127;if(in.protocol!=1)continue;
            auto before=r.selected;switch(in.buttons){
                case mpe4::Up:if(r.selected)r.selected--;break;
                case mpe4::Down:if(r.selected+1<r.count)r.selected++;break;
                case mpe4::Left:case mpe4::PageUp:r.selected=r.selected>Rows?r.selected-Rows:0;break;
                case mpe4::Right:case mpe4::PageDown:if(r.count)r.selected=r.selected+Rows<r.count?r.selected+Rows:r.count-1;break;
                case mpe4::Enter:if(r.count){char path[384];snprintf(path,sizeof path,"%s/%s",r.directory,r.games[r.selected]);if(launch(path))return;}break;
            }if(before!=r.selected)r.menuDirty=true;
        }
        if(r.menuDirty)menuFrame();
        else {
            // The C64 client scans/arms input between frame ends, not inside
            // its packet wait loop. Keep that handshake alive when the picker
            // is idle; send only the paced frame end, never unchanged cells.
            r.cursor=1000;r.menuFull=false;r.prepared=true;
        }
        return;
    }
    mpe4::Input in{};
    uint8_t joy=r.joy&15;if((joy&3)==3)joy&=~3;if((joy&12)==12)joy&=~12;
    static const uint8_t directions[16]={0,1,5,0,7,8,6,0,3,2,4,0,0,0,0,0};in.direction=directions[joy];
    if(r.head!=r.tail){auto event=r.events[r.head];r.head=(r.head+1)&127;
        if(event.protocol&1){in.key=event.buttons;in.scan=event.display;}
        if(event.protocol&2)in.fire=true;
        if(event.protocol&4){in.pointerEvent=true;in.pointerX=event.buttons;in.pointerY=event.display;in.pointerButtons=(event.protocol>>3)&3;}
    }else if(r.pointerDirty){in.pointerEvent=true;in.pointerX=r.pointerX;in.pointerY=r.pointerY;in.pointerButtons=r.pointerButtons;r.pointerDirty=false;}
    // Wall clock handles PAL/NTSC equally; no elapsed time discarded on slow frames.
    const uint32_t now=host->micros_now(),delta=now-r.lastTick;r.lastTick=now;
    uint64_t ticks=uint64_t(delta)*60+r.tickFraction;r.tickFraction=ticks%1000000;
    in.elapsed60Hz=ticks/1000000>65535?65535:ticks/1000000;
    if(!r.session.prepareFrame(in)){const auto &s=r.session.game.state;fail(uint8_t(0x40+r.session.error),s.errorLogic|(uint32_t(s.errorOpcode)<<8)|(uint32_t(s.errorIp&255)<<16));return;}
    r.prepared=true;
}
static bool packet(VmPacket *out){
    auto &r=*runtime;if(r.pending||r.error)return false;pump();if(!r.prepared)return false;
    r.packet={};
    if(r.game){
        uint8_t n=r.session.spritePacket(r.packet.payload);
        if(n){r.packet.type=5;r.packet.flags=0x20;r.packet.length=n;}
        else{bool first=false;n=r.session.cells(r.packet.payload,19,first);
            if(n){r.packet.type=1;r.packet.flags=8|(r.session.hires?4:0)|(r.session.parserSplit?0x40:0)|(first?16:0);r.packet.length=n*12;}
            else{r.packet.type=2;r.packet.flags=0x21|(r.session.hires?4:0)|(r.session.parserSplit?0x40:0);memcpy(r.packet.payload,r.session.sid,26);r.packet.length=26+r.session.spriteDescriptor(r.packet.payload+26);}
        }
    }else{
        bool first=r.menuFull&&r.cursor==0;unsigned n=0;
        while(r.cursor<1000&&n<19){unsigned cell=r.cursor++;
            if(!r.menuFull&&!memcmp(r.session.current+cell*8,r.session.next+cell*8,8)&&r.session.current[8000+cell]==r.session.next[8000+cell])continue;
            auto p=r.packet.payload+12*n++;p[0]=cell;p[1]=cell>>8;memcpy(p+2,r.session.next+cell*8,8);p[10]=r.session.next[8000+cell];p[11]=0;
        }
        if(n){r.packet.type=1;r.packet.flags=12|(first?16:0);r.packet.length=n*12;}
        else{r.packet.type=2;r.packet.flags=0x25;r.packet.length=37;r.packet.payload[26]=1;}
    }
    if(r.packet.type==1&&!r.presented){
        r.packet.flags|=1;r.baseCells+=r.packet.length/12;
        if(r.baseCells==1000)r.packet.flags|=2;
    }
    r.pending=true;*out=r.packet;return true;
}
static void ack(){auto &r=*runtime;if(!r.pending)return;
    if(r.packet.type==2){if(r.game)r.session.acknowledgeFrame();else memcpy(r.session.current,r.session.next,10000);r.presented=true;r.prepared=false;}
    r.pending=false;
}
static const VmModule module{VM_ABI,sizeof(VmModule),input,pump,packet,ack};
}
extern "C" __attribute__((section(".entry"),used)) const VmModule *vm_entry(const VmHost *host){
    using namespace agivm;
    if(!host||host->abi!=VM_ABI||host->bytes<RequiredHostBytes||(host->services&VM_SERVICES)!=VM_SERVICES||!host->fail||
       !host->file_op||!host->write||!host->open_flags||!host->guest_ram||host->guest_ram_bytes!=VM_RAM_BYTES||
       host->workspace_bytes<sizeof(Runtime)||(uintptr_t(host->workspace)%alignof(Runtime))||(uintptr_t(host->guest_ram)%alignof(mpe4::State)))return nullptr;
    agivm::host=host;auto state=new(host->guest_ram)mpe4::State{};runtime=new(host->workspace)Runtime(*state);
    if(snprintf(runtime->directory,sizeof runtime->directory,"%s/GAMES",host->package_root)>=(int)sizeof runtime->directory)return nullptr;
    if(host->content_path[0]){if(!launch(host->content_path)){fail(0x32);return nullptr;}}
    else listGames();return &module;
}
#if defined(__arm__)
extern "C" void *_sbrk(ptrdiff_t){return reinterpret_cast<void *>(-1);}
extern "C" int _write(int,const void *,int){return -1;}
extern "C" int _read(int,void *,int){return -1;}
extern "C" int _close(int){return -1;}
extern "C" int _fstat(int,void *){return -1;}
extern "C" int _isatty(int){return 0;}
extern "C" int _lseek(int,int,int){return -1;}
extern "C" int _getpid(){return 1;}
extern "C" int _kill(int,int){return -1;}
extern "C" void _exit(int){for(;;){}}
#endif

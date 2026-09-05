// SPDX-License-Identifier: GPL-2.0-or-later
// Independent GB/GB Color module. Only generic MPE host services are imported.
#include "../abi/vm_abi.h"
#include "../video/mpe_video_live.h"
#include "../../engine/gnuboy/machine.h"
#define PROGMEM
#include "../nes/font8x8.h"
#undef PROGMEM
#include <cstring>
#include <cstdio>
namespace gbvm {
#include "saves.h"
static const VmHost *host;
static BatteryStore saves;
static uint32_t nextSave;
static bool saveBlocked;
struct Entry {char name[96];uint32_t bytes;};
static Entry entries[128];static unsigned count,selected;
static char directory[256],message[41];
alignas(4) static uint8_t videoWorkspace[mpe_video::DeltaWorkspaceBytes];
static uint8_t pixels[160*144],palette[256*3],remap[64];
static uint16_t colors565[256],lineColors[64];static uint16_t colorCount;
static bool lineColorsValid,game,ready,capturing,pending,frameEnd,menuDirty,replace,hires;
static bool videoSubmitted;
static uint32_t generation,lastMicros,remainder,readyMicros;
static int64_t debt;
static uint8_t latestSid[26],sentSid[26];
static uint8_t menu[1000][10],presented[1000][10];
static unsigned cursor,pendingCount,pendingCells[19];
static VmPacket inFlight;
static VmInput inputs[32];static unsigned head,tail;static uint8_t previous;
static int compare(const char *a,const char *b){
    for(;;a++,b++){unsigned x=*a,y=*b;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return x<y?-1:1;if(!x)return 0;}
}
static bool romName(const char *n){auto e=strrchr(n,'.');return e&&(!compare(e,".gb")||!compare(e,".gbc"));}
static void say(const char *s){snprintf(message,sizeof message,"%s",s);menuDirty=true;}
static void text(unsigned row,unsigned column,const char *s,bool inverse=false){
    for(unsigned x=column;*s&&x<40;x++,s++){auto cell=menu[row*40+x];unsigned ch=(uint8_t)*s;if(ch>127)ch='?';
        for(unsigned y=0;y<8;y++)cell[y]=MPE5Font8x8[ch][y]^(inverse?255:0);cell[8]=0x10;cell[9]=1;}
}
static void buildMenu(){
    for(unsigned i=0;i<1000;i++){memset(menu[i],0,8);menu[i][8]=0x10;menu[i][9]=1;}
    text(0,6,"MHS GBVM - GB / GAME BOY COLOR");
    char status[41];snprintf(status,sizeof status,"ROM %u OF %u",count?selected+1:0,count);text(2,1,status);
    unsigned page=(selected/17)*17;
    for(unsigned i=0;i<17&&page+i<count;i++){char name[39];snprintf(name,sizeof name,"%c %.36s",page+i==selected?'>':' ',entries[page+i].name);text(i+4,1,name,page+i==selected);}
    text(22,0,message);text(23,0,"GNUBOY GPL2+ - NO WARRANTY; SEE LICENSE");text(24,0,"CURSORS/JOY PICK  RETURN/FIRE RUN  S+S EXIT");
    cursor=0;ready=true;menuDirty=false;hires=true;
}
static void enumerate(){
    count=selected=0;VmFileInfo info{};auto dir=host->open(directory,&info);
    if(!dir||!info.directory){if(dir)host->close(dir);say("ROM DIRECTORY NOT FOUND");return;}
    int result;unsigned omitted=0;
    while((result=host->next(dir,&info))==1){
        if(info.directory||!memchr(info.name,0,sizeof info.name)||!romName(info.name))continue;
        if(count==128){omitted++;continue;}strcpy(entries[count].name,info.name);entries[count++].bytes=info.bytes;
    }
    host->close(dir);
    for(unsigned i=1;i<count;i++)for(unsigned j=i;j&&compare(entries[j].name,entries[j-1].name)<0;j--){auto t=entries[j];entries[j]=entries[j-1];entries[j-1]=t;}
    say(result<0?"ROM LIST READ FAILED":!count?"ADD .GB / .GBC FILES TO GBVM/ROMS":omitted?"LIST LIMITED TO FIRST 128 ROMS":"FIRE OR RETURN RUNS THE SELECTED ROM");
}
static uint8_t colorIndex(uint16_t c){
    for(unsigned i=0;i<colorCount;i++)if(colors565[i]==c)return i;
    const unsigned r=((c>>11)*255+15)/31,g=(((c>>5)&63)*255+31)/63,b=((c&31)*255+15)/31;
    if(colorCount<256){unsigned i=colorCount++;colors565[i]=c;palette[3*i]=r;palette[3*i+1]=g;palette[3*i+2]=b;return i;}
    // Preserve within-frame palette changes, up to the service's 256 colors.
    // Only overflow is approximated; C64 palette reduction is firmware-owned.
    unsigned best=0,error=~0u;for(unsigned i=0;i<256;i++){
        int dr=int(r)-palette[i*3],dg=int(g)-palette[i*3+1],db=int(b)-palette[i*3+2];unsigned e=dr*dr+dg*dg+db*db;if(e<error){error=e;best=i;}}
    return best;
}
static void line(void *,unsigned y,const uint8_t *src,const uint16_t *rgb){
    if(!capturing||y>=144)return;
    for(unsigned i=0;i<64;i++)if(!lineColorsValid||lineColors[i]!=rgb[i]){lineColors[i]=rgb[i];remap[i]=colorIndex(rgb[i]);}
    lineColorsValid=true;for(unsigned x=0;x<160;x++)pixels[y*160+x]=remap[src[x]&63];
}
static void frame(void *){
    if(capturing){ready=true;capturing=false;gb::capture(false);generation++;readyMicros=host->micros_now();}
    else if(!ready){colorCount=0;lineColorsValid=false;capturing=true;gb::capture(true);}
    uint8_t next[26];gb::sid(next);next[0]|=latestSid[0];memcpy(latestSid,next,26);
}
static bool loadPath(const char *path,uint32_t expected){
    if(!saves.flush()){say("SAVE FAILED - FIRE RETRIES; KEEP POWER ON");saveBlocked=true;return false;}
    if(saveBlocked){saveBlocked=false;say("SAVE RECOVERED - FIRE TO RUN ROM");return false;}
    VmFileInfo info{};auto file=host->open(path,&info);
    if(expected>host->guest_ram_bytes){if(file)host->close(file);say("ROM TOO LARGE - LIMIT IS 512 KIB");return false;}
    if(!file||info.directory||info.bytes!=expected){if(file)host->close(file);say("ROM MISSING OR CHANGED");return false;}
    uint32_t done=0;while(done<expected){auto n=expected-done;if(n>4096)n=4096;auto got=host->read(file,done,host->guest_ram+done,n);
        if(got!=int32_t(n)){host->close(file);say("ROM READ FAILED");return false;}done+=n;}
    host->close(file);const char *why=gb::inspect(host->guest_ram,expected);if(why){say(why);return false;}
    if(!gb::start(host->guest_ram,expected,{nullptr,line,frame})){say(gb::error());return false;}
    if(!saves.load(host,vm_crc32(host->guest_ram,expected))){say("SAVE READ FAILED - RESTORE SAVE BACKUP");return false;}
    game=capturing=true;ready=videoSubmitted=false;gb::capture(true);colorCount=0;lineColorsValid=false;generation=0;
    memset(pixels,0,sizeof pixels);memset(latestSid,0,sizeof latestSid);memset(sentSid,0,sizeof sentSid);
    debt=remainder=0;lastMicros=host->micros_now();nextSave=lastMicros+5000000;return true;
}
static void returnMenu(){
    game=capturing=false;gb::capture(false);gb::buttons(0);memset(latestSid,0,26);
    saveBlocked=!saves.flush();say(saveBlocked?"SAVE FAILED - FIRE RETRIES; KEEP POWER ON":"RETURNED - FIRE OR RETURN RUNS ROM");replace=true;
}
static void input(const VmInput *i){
    if(!i||i->protocol!=0x81||(i->display&~1))return;
    if(game&&(i->buttons&12)!=12)gb::buttons(i->buttons);
    unsigned next=(tail+1)&31;if(next==head){inputs[(tail-1)&31]=*i;return;}inputs[tail]=*i;tail=next;
}
static void pump(){
    if(!ready&&!pending&&head!=tail){auto in=inputs[head];head=(head+1)&31;uint8_t pressed=in.buttons&~previous;previous=in.buttons;
        if(game){if((in.buttons&12)==12&&(pressed&12))returnMenu();else gb::buttons(in.buttons);}
        else if(count){
            if(pressed&16){selected=selected?selected-1:count-1;menuDirty=true;}
            if(pressed&32){selected=(selected+1)%count;menuDirty=true;}
            if(pressed&64){selected=selected>=17?selected-17:0;menuDirty=true;}
            if(pressed&128){selected=selected+17<count?selected+17:count-1;menuDirty=true;}
            if(pressed&9){char path[384];snprintf(path,sizeof path,"%s/%s",directory,entries[selected].name);loadPath(path,entries[selected].bytes);}
        }
    }
    if(!game)return;
    const uint32_t now=host->micros_now(),dt=now-lastMicros;lastMicros=now;
    uint64_t credit=uint64_t(dt)*gb::ClockHz+remainder;debt+=credit/1000000;remainder=credit%1000000;
    // Actual instruction overshoot stays as signed debt; never underflow it or
    // make emulation speed depend on the presentation/ACK cadence.
    unsigned budget=4096;
    while(debt>0&&budget>=128&&!host->should_yield()){
        unsigned n=gb::run(unsigned(debt>128?128:debt));if(!n)break;debt-=n;budget=n>=budget?0:budget-n;
    }
    if(gb::error()&&!ready&&!pending){const char *why=gb::error();returnMenu();say(why);}
    if(game&&!ready&&!pending&&!videoSubmitted&&int32_t(now-nextSave)>=0){
        if(!saves.flush())returnMenu();
        nextSave=host->micros_now()+5000000;
    }
}
static bool publish(VmPacket *out,uint8_t type,uint8_t flags,unsigned length){inFlight.type=type;inFlight.flags=flags;inFlight.length=length;*out=inFlight;pending=true;return true;}
static bool audio(VmPacket *out,bool end){memcpy(inFlight.payload,latestSid,26);memcpy(sentSid,latestSid,26);frameEnd=end;return publish(out,2,0x20|(end?1:0)|(hires?4:0),26);}
static bool packet(VmPacket *out){
    if(pending)return false;
    if(!game&&menuDirty&&!ready)buildMenu();
    if(ready&&game){
        if(!videoSubmitted&&debt>gb::ClockHz/1000&&uint32_t(host->micros_now()-readyMicros)<100000){
            return memcmp(latestSid,sentSid,26)?audio(out,false):false;}
        VmIndexedFrame source{sizeof source,generation,pixels,palette,sizeof pixels,uint32_t(colorCount)*3,160,144,160,colorCount,0};
        videoSubmitted=true;auto result=host->video_indexed(&source);
        if(result==VmVideoResult::Busy)return false;
        if(result!=VmVideoResult::Transferred){host->fail(0x18,uint32_t(result));return false;}
        videoSubmitted=false;hires=source.resolved_mode!=0;return audio(out,true);
    }
    if(ready){
        unsigned n=0;while(cursor<1000&&n<19){unsigned c=cursor++;if(!replace&&!memcmp(menu[c],presented[c],10))continue;
            pendingCells[n]=c;auto p=inFlight.payload+n*12;p[0]=c;p[1]=c>>8;memcpy(p+2,menu[c],10);n++;}
        if(n){pendingCount=n;unsigned flags=12;if(replace){flags|=1;if(cursor==1000)flags|=2;if(cursor<=19)flags|=16;}return publish(out,1,flags,n*12);}
        return audio(out,true);
    }
    // Picker frame-end heartbeat is required for keyboard/joystick polling.
    if(!game)return audio(out,true);
    return memcmp(latestSid,sentSid,26)?audio(out,false):false;
}
static void ack(){
    if(inFlight.type==1){for(unsigned i=0;i<pendingCount;i++){unsigned c=pendingCells[i];memcpy(presented[c],menu[c],10);}pendingCount=0;}
    else if(inFlight.type==2){if(!memcmp(latestSid,sentSid,26)){latestSid[0]=0;sentSid[0]=0;}if(frameEnd){ready=false;replace=false;frameEnd=false;cursor=0;}}
    pending=false;
}
static const VmModule module={VM_ABI,sizeof(VmModule),input,pump,packet,ack};
}
extern "C" __attribute__((section(".entry"),used)) const VmModule *vm_entry(const VmHost *h){
    using namespace gbvm;constexpr unsigned required=127;
    if(!h||h->abi!=VM_ABI||h->bytes<sizeof(VmHost)||(h->services&required)!=required||!h->guest_ram||h->guest_ram_bytes<32768||!h->workspace||h->workspace_bytes<8192||!h->open_flags||!h->write||!h->file_op||!h->video_configure||!h->video_indexed||!h->should_yield)return nullptr;
    host=h;saves={};saveBlocked=false;head=tail=previous=0;game=ready=pending=false;replace=menuDirty=true;hires=true;
    VmIndexedVideoSetup setup{sizeof setup,videoWorkspace,sizeof videoWorkspace,0,15,VM_INDEXED_NATIVE_HEIGHT|VM_INDEXED_DOUBLE_WIDTH};
    if(!h->video_configure(&setup))return nullptr;
    if(h->content_path[0]){
        if(strlen(h->content_path)>=sizeof directory||!romName(h->content_path))return nullptr;
        strcpy(directory,h->content_path);auto slash=strrchr(directory,'/');if(!slash)return nullptr;if(slash==directory)slash[1]=0;else *slash=0;
    }else if(snprintf(directory,sizeof directory,"%s/ROMS",h->package_root)>=(int)sizeof directory)return nullptr;
    enumerate();
    if(h->content_path[0]){VmFileInfo info{};auto f=h->open(h->content_path,&info);if(f){h->close(f);if(!info.directory&&loadPath(h->content_path,info.bytes))return &module;}else say("DIRECT ROM NOT FOUND - PICK A ROM");}
    buildMenu();return &module;
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

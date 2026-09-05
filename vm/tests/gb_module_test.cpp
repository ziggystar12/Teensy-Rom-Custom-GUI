#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <cstdio>
#include "../gb/gbvm.cpp"
static std::vector<uint8_t> roms[2];static unsigned reads,opens,dirCursor,videoCalls,videoFrames,audioFrames,menuPackets;
static uint32_t now,videoGeneration,videoHash;static bool waiting;
static uint8_t ram2[512*1024];
static uint32_t clockNow(){return now;}
static void infoFor(unsigned index,VmFileInfo *i){*i={};strcpy(i->name,index?"Pac-Man.gbc":"MARIO1.GB");i->bytes=roms[index].size();}
static uint32_t openFile(const char *path,VmFileInfo *i){
    opens++;*i={};if(!strcmp(path,"/VMS/GBVM/ROMS")){i->directory=1;dirCursor=0;return 1;}
    if(strstr(path,"MARIO1.GB")){infoFor(0,i);return 2;}if(strstr(path,"Pac-Man.gbc")){infoFor(1,i);return 3;}return 0;
}
static int32_t readFile(uint32_t h,uint32_t off,void *p,uint32_t n){reads++;assert(h==2||h==3);assert(off+n<=roms[h-2].size());memcpy(p,roms[h-2].data()+off,n);return n;}
static int32_t nextFile(uint32_t h,VmFileInfo *i){assert(h==1);if(dirCursor==2)return 0;infoFor(dirCursor++,i);return 1;}
static void closeFile(uint32_t){}
static bool yield(){return false;}
static void fail(uint8_t,uint32_t){assert(false);}
static bool configure(const VmIndexedVideoSetup *s){assert(s->default_mode==0&&s->reserved==3&&s->workspace_bytes==36864);return true;}
static VmVideoResult video(VmIndexedFrame *f){
    assert(f->width==160&&f->height==144&&f->colors>0&&f->colors<=256);
    uint32_t h=vm_crc32(f->pixels,f->pixel_bytes)^vm_crc32(f->palette,f->palette_bytes);
    if(!waiting){waiting=true;videoCalls=0;videoGeneration=f->generation;videoHash=h;}
    assert(videoGeneration==f->generation&&videoHash==h); // frozen across CPU/audio/input work
    if(++videoCalls<20)return VmVideoResult::Busy;
    waiting=false;videoFrames++;f->resolved_mode=0;return VmVideoResult::Transferred;
}
static void turns(const VmModule *m,unsigned n){for(unsigned i=0;i<n;i++){
    now+=100;m->pump();VmPacket p{};if(m->packet(&p)){
        if(p.type==1)menuPackets++;if(p.type==2)audioFrames++;
        assert(p.length<=228);m->ack();}
}}
static void button(const VmModule *m,uint8_t b){VmInput in{b,0,0,0x81};m->input(&in);turns(m,250);}
int main(int argc,char **argv){
    assert(argc==3);for(unsigned i=0;i<2;i++){std::ifstream f(argv[i+1],std::ios::binary);assert(f);roms[i]={std::istreambuf_iterator<char>(f),{}};}
    VmHost host{};host.abi=VM_ABI;host.bytes=sizeof host;host.services=119;host.package_root="/VMS/GBVM";host.content_path="";
    host.micros_now=clockNow;host.open=openFile;host.read=readFile;host.next=nextFile;host.close=closeFile;
    host.guest_ram=ram2;host.guest_ram_bytes=sizeof ram2;host.should_yield=yield;host.fail=fail;host.video_configure=configure;host.video_indexed=video;
    auto m=vm_entry(&host);assert(m);turns(m,100);assert(menuPackets>0&&!gbvm::game);
    button(m,32);button(m,0);assert(gbvm::selected==1);button(m,8);button(m,0);assert(gbvm::game&&gb::color());
    const auto priorReads=reads,priorOpens=opens;const auto start=gb::ticks();const auto wall=now;
    turns(m,20000);assert(reads==priorReads&&opens==priorOpens&&videoFrames>50&&audioFrames>50);
    const int64_t expected=uint64_t(now-wall)*gb::ClockHz/1000000;
    assert(int64_t(gb::ticks()-start)>expected-5000&&int64_t(gb::ticks()-start)<expected+5000);
    button(m,12);button(m,0);turns(m,1000);assert(!gbvm::game);const auto priorPackets=menuPackets;turns(m,1000);assert(menuPackets==priorPackets);
    button(m,16);button(m,0);button(m,1);button(m,0);assert(gbvm::game&&!gb::color());turns(m,20000);assert(!gb::error());
    // All native DMG pixels remain four shades, even if palettes are remapped.
    unsigned used[256]{};for(auto c:gbvm::pixels)used[c]++;unsigned shades=0;for(auto n:used)shades+=n!=0;assert(shades<=4&&shades>1);
    printf("PASS GBVM: picker down/up, Return/fire, both ROM types, return/relaunch, frozen video, four shades, emulated clock, no runtime file I/O; %u pictures\n",videoFrames);
}

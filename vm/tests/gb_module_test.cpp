#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <cstdio>
#include <map>
#include "../gb/gbvm.cpp"
static std::vector<uint8_t> roms[5];static unsigned reads,opens,dirCursor,videoCalls,videoFrames,audioFrames,menuPackets;
static const char *names[]={"MARIO1.GB","Pac-Man.gbc","MARIO2.GB","Kirby.gb","ZELDA.GB"};
static std::map<std::string,std::vector<uint8_t>> savedFiles;
static std::string savedPath;static bool saveDir,failWrite,failFlush;
static unsigned saveWrites;
static uint32_t now,videoGeneration,videoHash;static bool waiting;
static uint8_t ram2[512*1024];
static uint8_t scratch[8192+32];
static uint32_t clockNow(){return now;}
static void infoFor(unsigned index,VmFileInfo *i){*i={};strcpy(i->name,names[index]);i->bytes=roms[index].size();}
static uint32_t openFile(const char *path,VmFileInfo *i){
    opens++;*i={};if(!strcmp(path,"/VMS/GBVM/ROMS")){i->directory=1;dirCursor=0;return 1;}
    for(unsigned n=0;n<5;n++)if(strstr(path,names[n])){infoFor(n,i);return n+2;}
    if(!strcmp(path,"/VMS/GBVM/SAVES")){i->directory=1;return saveDir?9:0;}
    if(savedFiles.count(path)){savedPath=path;i->bytes=savedFiles[path].size();return 10;}return 0;
}
static int32_t readFile(uint32_t h,uint32_t off,void *p,uint32_t n){
    reads++;if(h==10){const auto &s=savedFiles[savedPath];if(off>s.size()||n>s.size()-off)return -1;memcpy(p,s.data()+off,n);return n;}
    assert(h>=2&&h<=6);assert(off+n<=roms[h-2].size());memcpy(p,roms[h-2].data()+off,n);return n;
}
static uint32_t openFlags(const char *path,uint32_t flags,VmFileInfo *i){
    assert(strstr(path,"/SAVES/")&&flags==(VM_OPEN_WRITE|VM_OPEN_CREATE|VM_OPEN_TRUNCATE));
    savedPath=path;savedFiles[path].clear();*i={};return 10;
}
static int32_t writeFile(uint32_t h,uint32_t off,const void *p,uint32_t n){
    assert(h==10);saveWrites++;if(failWrite)return -1;
    auto &s=savedFiles[savedPath];if(s.size()<off+n)s.resize(off+n);memcpy(s.data()+off,p,n);return n;
}
static int32_t fileOp(VmFsRequest *r){
    if(r->operation==VmFsOp::Mkdir){assert(!strcmp(r->path,"/VMS/GBVM/SAVES"));saveDir=true;return 0;}
    if(r->operation==VmFsOp::Flush)return failFlush?-1:0;
    assert(r->operation==VmFsOp::Close);return 0;
}
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
    assert(argc==3||argc==6);for(int i=0;i<argc-1;i++){std::ifstream f(argv[i+1],std::ios::binary);assert(f);roms[i]={std::istreambuf_iterator<char>(f),{}};}
    memset(scratch,0xa5,sizeof scratch);
    VmHost host{};host.abi=VM_ABI;host.bytes=sizeof host;host.services=127;host.package_root="/VMS/GBVM";host.content_path="";
    host.workspace=scratch;host.workspace_bytes=8192;host.open_flags=openFlags;host.write=writeFile;host.file_op=fileOp;
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
    if(argc==6){
        assert(saveWrites==0);
        for(unsigned index:{2u,3u,4u}){
            button(m,12);button(m,0);turns(m,1000);assert(!gbvm::game);
            const auto otherSaves=savedFiles;
            assert(gbvm::loadPath((std::string("/VMS/GBVM/ROMS/")+names[index]).c_str(),roms[index].size()));
            turns(m,120000);assert(gbvm::game&&!gb::error());
            if(index==2){
                assert(gb::saveBytes()==8192);
                // A complete checked slot survives a failed replacement.
                gb::poke(0,10);gb::poke(0xa000,0x42);assert(gbvm::saves.flush());
                const auto good=savedFiles;
                gb::poke(0xa000,0x43);failWrite=true;assert(!gbvm::saves.flush());failWrite=false;
                bool intact=false;for(const auto &s:good)intact|=savedFiles[s.first]==s.second;assert(intact);
                failFlush=true;assert(!gbvm::saves.flush());failFlush=false;
                assert(gbvm::saves.flush()&&!gbvm::saves.dirty());
                gbvm::BatteryStore reload;
                gb::saveData()[0]=0;assert(reload.load(&host,vm_crc32(roms[2].data(),roms[2].size())));assert(gb::saveData()[0]==0x43);
                // Corrupt the newest slot, requiring the older checked copy.
                auto newest=savedFiles.begin();for(auto it=savedFiles.begin();it!=savedFiles.end();++it){uint32_t a,b;memcpy(&a,it->second.data()+12,4);memcpy(&b,newest->second.data()+12,4);if(a>b)newest=it;}
                newest->second.back()^=1;
                assert(reload.load(&host,vm_crc32(roms[2].data(),roms[2].size())));assert(gb::saveData()[0]==0x42);
                // Both slots damaged: report it, don't silently overwrite.
                const auto backup=savedFiles;for(auto &s:savedFiles)s.second[0]=0;
                assert(!reload.load(&host,vm_crc32(roms[2].data(),roms[2].size())));savedFiles=backup;
                // The module's exit failure blocks launching another ROM;
                // Fire retries saving before it permits any ROM replacement.
                gb::poke(0xa000,0x55);failWrite=true;button(m,12);button(m,0);turns(m,1000);
                assert(!gbvm::game&&gbvm::saveBlocked);
                assert(!gbvm::loadPath("/VMS/GBVM/ROMS/Kirby.gb",roms[3].size()));
                failWrite=false;assert(!gbvm::loadPath("/VMS/GBVM/ROMS/Kirby.gb",roms[3].size()));assert(!gbvm::saveBlocked);
                assert(gbvm::loadPath("/VMS/GBVM/ROMS/MARIO2.GB",roms[2].size()));
                assert(gb::saveData()[0]==0x55);turns(m,20000);
            }else if(index==4){
                assert(gb::saveBytes()==8192);gb::poke(0,10);gb::poke(0xa000,0x66);
                assert(gbvm::saves.flush());button(m,12);button(m,0);turns(m,1000);
                assert(gbvm::loadPath("/VMS/GBVM/ROMS/ZELDA.GB",roms[4].size()));
                assert(gb::saveData()[0]==0x66);
                for(const auto &saved:otherSaves)assert(savedFiles[saved.first]==saved.second);
            }else assert(!gb::saveBytes());
        }
        // Explicit, useful rejection of oversized ROMs with no out-of-bounds read.
        button(m,12);button(m,0);turns(m,1000);
        assert(!gbvm::loadPath("/VMS/GBVM/ROMS/Kirby.gb",1024*1024));assert(strstr(gbvm::message,"512 KIB"));
        for(unsigned i=8192;i<sizeof scratch;i++)assert(scratch[i]==0xa5);
        puts("PASS GBVM: Mario 2/Kirby/Zelda module launches, separate battery save/reload, failed-write/flush backup protection, corrupt-slot fallback, exit retry, 512KiB rejection and RAM1 guards");
    }
}

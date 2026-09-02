#include "mpe4_session.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <functional>

static void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
static unsigned u16(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
static uint32_t u32(const uint8_t *p){return u16(p)|uint32_t(u16(p+2))<<16;}
struct Fixture {
  std::vector<uint8_t> raw;unsigned reads=0,maxRead=0;
  explicit Fixture(const char *name){std::ifstream file(name,std::ios::binary);raw.assign(std::istreambuf_iterator<char>(file),{});
    require(raw.size()==0x100000,"expected 1MiB raw cartridge");}
  static bool read(void *context,uint32_t offset,uint8_t *data,uint16_t count){auto &f=*(Fixture*)context;f.reads++;
    if(count>f.maxRead)f.maxRead=count;if(offset>f.raw.size()||count>f.raw.size()-offset)return false;
    memcpy(data,f.raw.data()+offset,count);return true;}
};
struct Run {
  Fixture fixture;mpe4::Session session{};uint8_t displayed[10000]{};
  uint32_t packageRoot=0;unsigned frames=0,changedCells=0,fullFrames=0,audibleFrames=0;
  std::vector<std::string> gates;std::string reached="not-started";
  explicit Run(const char *name):fixture(name){
    constexpr unsigned intro=0x4000;const auto *header=fixture.raw.data()+intro;
    require(!memcmp(header,"M3T1",4)&&header[4]==1&&header[5]==64,"neutral M3 bridge header");
    const unsigned bytes=u32(header+8);require(bytes>=64&&bytes<0xe8000-intro,"bridge bounds");
    packageRoot=(intro+bytes+255)&~255u;
    require(!memcmp(fixture.raw.data()+packageRoot,"M4G1",4),"derived native package location");
    require(u32(fixture.raw.data()+packageRoot+32)==1,"generic original-startup flag");
    require(u16(header+12)==2&&header[15]==15,"generic two-visit bridge");
    const unsigned delta=u32(header+48),length=u32(header+52);require(delta<=bytes&&length<=bytes-delta,"bridge delta bounds");
    unsigned at=delta;
    for(unsigned visit=0;visit<2;visit++){
      require(at+4<=bytes,"bridge visit header");const unsigned count=u16(header+at),flags=header[at+3];at+=4;
      require(count<=1000&&count*12<=bytes-at,"bridge visit records");
      if(visit==1)require(count==1000&&(flags&2),"final neutral bridge is standalone hires");
      for(unsigned i=0;i<count;i++){const uint8_t *r=header+at+i*12;const unsigned cell=u16(r);
        require(cell<1000&&(visit!=1||cell==i),"ordered bridge records");
        if(visit==1){memcpy(displayed+cell*8,r+2,8);displayed[8000+cell]=r[10];displayed[9000+cell]=r[11];}}
      at+=count*12;
    }
    require(at==delta+length,"complete bridge delta consumed");
    for(uint8_t value:displayed)require(value==0,"generic bridge must contain no SQ1 art");
    require(session.start(Fixture::read,&fixture,packageRoot,0xe8000,{}),"native Session open");
    require(session.package.originalStartup,"native Package preserves original startup");
    require(s().vars[0]==0&&s().logic==0&&!session.game.flag(6),"authentic fresh LOGIC0 entry");
    memcpy(session.current,displayed,10000);session.seedPresentedFrame(true);mark("neutral-bridge-to-original-logic0");
  }
  mpe4::State &s(){return session.game.state;}
  std::string position(){char b[250];snprintf(b,sizeof(b),"room%u ego(%u,%u) control%u input%u modal%u scan%lu logic%u ip%u",
    s().vars[0],s().objects[0].x,s().objects[0].y,s().playerControl,s().inputEnabled,s().modal,
    (unsigned long)s().scans,s().logic,s().callDepth?s().calls[s().callDepth-1].ip:0);return b;}
  void mark(const char *name){reached=name;gates.emplace_back(name);fprintf(stderr,"PASS %s %s\n",name,position().c_str());}
  void snapshot(const char *name){std::ofstream file(std::string(name)+".frame.bin",std::ios::binary);file.write((char*)displayed,10000);
    std::ofstream text(std::string(name)+".text.txt");for(unsigned row=0;row<25;row++){
      for(unsigned col=0;col<40;col++){uint8_t ch=s().text[row*40+col];text<<char(ch>=32&&ch<127?ch:' ');}text<<'\n';}}
  void frame(mpe4::Input input={}){
    input.elapsed60Hz=1;if(!session.prepareFrame(input)){char b[360];snprintf(b,sizeof(b),"sessionError%u coreError%u logic%u opcode%u ip%u %s",
      session.error,s().error,s().errorLogic,s().errorOpcode,s().errorIp,position().c_str());throw std::runtime_error(b);}
    uint8_t records[228];bool first=false;unsigned total=0;int previous=-1;
    for(;;){unsigned count=session.cells(records,19,first);if(first)fullFrames++;if(!count)break;
      for(unsigned i=0;i<count;i++){const uint8_t *r=records+i*12;unsigned cell=u16(r);require(cell<1000&&int(cell)>previous,"ordered bounded cell stream");previous=cell;
        memcpy(displayed+cell*8,r+2,8);displayed[8000+cell]=r[10];displayed[9000+cell]=r[11];}total+=count;}
    require(!memcmp(displayed,session.next,10000),"cell replay reconstructs exact native renderer frame");
    changedCells+=total;if(session.sid[25])audibleFrames++;session.acknowledgeFrame();frames++;
  }
  void key(uint8_t key){mpe4::Input in{};in.key=key;frame(in);}
  void pointer(uint8_t x,uint8_t y,uint8_t buttons){mpe4::Input in{};in.pointerEvent=true;in.pointerX=x;in.pointerY=y;in.pointerButtons=buttons;frame(in);}
  void until(const std::function<bool()> &done,unsigned maximum=3000){for(unsigned i=0;i<maximum;i++){if(done())return;frame();}
    snapshot("timeout");throw std::runtime_error("timeout "+position());}
  void ready(){until([&]{return s().vars[0]==1&&s().playerControl&&s().inputEnabled&&!s().inScan&&s().modal==mpe4::NoModal;});}
};
int main(int argc,char **argv){Run *run=nullptr;std::string error;
  try{require(argc>=2,"usage mpe4-session-kq1 RAW.bin");run=new Run(argv[1]);auto &r=*run;auto &s=r.s();
    r.until([&]{return s.vars[0]==83&&s.pictureVisible&&!s.inScan;});
    require(s.vars[7]==158&&s.menuCount==6&&s.bindingCount>=20,"authored KQ1 global startup");
    require(!s.inputEnabled&&s.graphics,"authored animated title input policy");r.mark("original-title-room83");
    for(unsigned i=0;i<60;i++)r.frame();require(r.audibleFrames>0,"original title SOUND0 progresses");r.snapshot("kq1-title");
    r.key(' ');r.until([&]{return s.vars[0]==1;});r.ready();r.mark("space-continues-title-to-original-room1");
    require(s.pictureVisible&&(s.objects[0].flags&mpe4::Drawn)&&s.objects[0].view==0,"authored starting scene and Graham");r.snapshot("kq1-room1");
    const unsigned x=s.objects[0].x;r.key(mpe4::Right);r.until([&]{return s.objects[0].x>=x+4;},120);r.key(mpe4::Right);r.ready();
    require(s.objects[0].direction==0,"second cursor press stops Graham");r.mark("room1-keyboard-walk-and-stop");
    for(const char *word="look";*word;word++)r.key(*word);r.key(mpe4::Enter);
    r.until([&]{return s.modal==mpe4::Message;});
    std::string text((char*)s.text,1000);require(text.find("You need to be more specific.")!=std::string::npos,"bare LOOK uses authored clarification");
    r.mark("room1-authored-bare-look-response");r.key(mpe4::Enter);r.ready();
    for(const char *word="look room";*word;word++)r.key(*word);r.key(mpe4::Enter);
    r.until([&]{return s.modal==mpe4::Message;});text.assign((char*)s.text,1000);
    require(text.find("You are standing outside a")!=std::string::npos&&text.find("castle surrounded by an")!=std::string::npos&&
      text.find("alligator filled moat.")!=std::string::npos,"LOOK ROOM resolves original Room1 message16");
    r.mark("room1-authored-look-room-response");r.snapshot("kq1-look");r.key(mpe4::Enter);r.ready();
    const unsigned targetX=s.objects[0].x+4,targetY=s.objects[0].y;
    require(targetX<150&&targetY+s.graphicsTop+6<200,"bounded walk target");
    r.pointer(targetX,targetY+s.graphicsTop+6,1);r.pointer(targetX,targetY+s.graphicsTop+6,0);
    r.until([&]{return s.objects[0].x==targetX&&s.objects[0].y==targetY&&s.objects[0].motionMode==0;},240);
    r.mark("room1-mouse-click-walk-and-stop");r.snapshot("kq1-final");
  }catch(const std::exception &e){error=e.what();fprintf(stderr,"FAIL %s\n",error.c_str());if(run)run->snapshot("failure");}
  printf("{\"passed\":%s,\"scope\":\"Actual KQ1 package original startup, native Session and renderer, title input, Room1 keyboard/LOOK/mouse smoke only; no full-game claim\",\"reached\":\"%s\",\"packageRoot\":%u,\"frames\":%u,\"changedCells\":%u,\"fullFrames\":%u,\"audibleFrames\":%u,\"room\":%u,\"maxReadBytes\":%u,\"sessionBytes\":%zu,\"error\":\"%s\",\"gates\":[",
    error.empty()?"true":"false",run?run->reached.c_str():"not-started",run?run->packageRoot:0,run?run->frames:0,run?run->changedCells:0,
    run?run->fullFrames:0,run?run->audibleFrames:0,run?run->s().vars[0]:0,run?run->fixture.maxRead:0,sizeof(mpe4::Session),error.c_str());
  if(run)for(size_t i=0;i<run->gates.size();i++)printf("%s\"%s\"",i?",":"",run->gates[i].c_str());puts("]}");delete run;return error.empty()?0:1;
}

#include "mpe4_session.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
#include <functional>

static void require(bool yes,const std::string &message){if(!yes)throw std::runtime_error(message);}
struct Fixture {
  std::vector<uint8_t> raw;
  mpe4::State saved{};
  uint32_t identity=0;
  unsigned saves=0,restores=0,reads=0;
  bool savedValid=false;
  explicit Fixture(const char *file){std::ifstream f(file,std::ios::binary);raw.assign(std::istreambuf_iterator<char>(f),{});require(raw.size()==0x100000,"raw cartridge must be1MiB");}
  static bool read(void *p,uint32_t offset,uint8_t *data,uint16_t count){auto &f=*(Fixture*)p;f.reads++;
    if(offset>f.raw.size()||count>f.raw.size()-offset)return false;memcpy(data,f.raw.data()+offset,count);return true;}
  static bool save(void *p,uint32_t identity,const mpe4::State *s,size_t count){auto &f=*(Fixture*)p;
    if(count!=sizeof(*s))return false;f.saved=*s;f.identity=identity;f.savedValid=true;f.saves++;return true;}
  static bool restore(void *p,uint32_t identity,mpe4::State *s,size_t count){auto &f=*(Fixture*)p;
    if(!f.savedValid||identity!=f.identity||count!=sizeof(*s))return false;*s=f.saved;f.restores++;return true;}
};
struct Run {
  Fixture fixture;
  mpe4::Session session{};
  uint8_t displayed[10000]{};
  unsigned frames=0,changedCells=0,fullFrames=0;
  std::string reached="not-started";
  std::vector<std::string> gates;
  explicit Run(const char *raw):fixture(raw){mpe4::Storage storage{&fixture,Fixture::save,Fixture::restore};
    require(session.start(Fixture::read,&fixture,0x1ec00,0xe8000,storage),"Session start failed" );}
  mpe4::State &s(){return session.game.state;}
  std::string position(){char b[280];snprintf(b,sizeof(b),"room%u ego(%u,%u) control%u modal%u v3=%u scan%lu v30=%u f34=%u f35=%u f57=%u door4(cel%u cycle%u flags%u)",s().vars[0],s().objects[0].x,s().objects[0].y,s().playerControl,s().modal,s().vars[3],(unsigned long)s().scans,s().vars[30],session.game.flag(34),session.game.flag(35),session.game.flag(57),s().objects[4].cel,s().objects[4].cycleMode,s().objects[4].flags);return b;}
  void mark(const char *name){reached=name;gates.emplace_back(name);fprintf(stderr,"PASS %s %s\n",name,position().c_str());}
  void tick(uint8_t key=0,uint8_t direction=0,uint8_t scan=0){
    mpe4::Input input{key,scan,direction,false,false,1};
    if(!session.prepareFrame(input)){char b[220];snprintf(b,sizeof(b),"sessionError%u coreError%u logic%u opcode%u ip%u %s",session.error,s().error,s().errorLogic,s().errorOpcode,s().errorIp,position().c_str());throw std::runtime_error(b);}
    uint8_t records[19*12];bool first=false;unsigned total=0;int last=-1;
    for(;;){unsigned count=session.cells(records,19,first);if(first)fullFrames++;if(!count)break;
      for(unsigned n=0;n<count;n++){const uint8_t *r=records+n*12;unsigned cell=r[0]|unsigned(r[1])<<8;
        require(cell<1000&&int(cell)>last,"cell order/bounds");last=cell;
        memcpy(displayed+cell*8,r+2,8);displayed[8000+cell]=r[10];displayed[9000+cell]=r[11];}
      total+=count;}
    require(!memcmp(displayed,session.next,10000),"cell stream must reconstruct real renderer exactly");
    changedCells+=total;session.acknowledgeFrame();frames++;
  }
  void until(const std::function<bool()> &done,unsigned maximum=20000,bool dismiss=true,uint8_t direction=0){
    for(unsigned i=0;i<maximum;i++){if(done())return;tick(dismiss&&s().modal==mpe4::Message?mpe4::Enter:0,direction);}
    throw std::runtime_error("timed out after "+std::to_string(maximum)+" frames: "+position());
  }
  void ready(){until([&]{return s().playerControl&&s().inputEnabled&&s().modal==mpe4::NoModal&&!s().inScan;});}
  void type(const char *text){require(s().inputEnabled||s().modal==mpe4::StringInput||s().modal==mpe4::NumberInput,"typing while input locked");
    for(;*text;text++)tick(*text);tick(mpe4::Enter);}
  void command(const char *text){until([&]{return s().modal==mpe4::NoModal&&!s().inScan&&s().inputEnabled;});const uint32_t before=s().scans;type(text);until([&]{return s().scans>before&&!session.game.flag(2)&&s().modal==mpe4::NoModal&&!s().inScan&&s().inputEnabled;});}
  void room(uint8_t expected,uint8_t direction){const uint8_t from=s().vars[0];
    until([&]{return s().vars[0]!=from;},15000,true,direction);tick();
    require(s().vars[0]==expected,"unexpected room transition "+position());ready();}
  void coordinate(bool xAxis,int target,uint8_t direction){until([&]{int value=xAxis?s().objects[0].x:s().objects[0].y;return direction==1||direction==7?value<=target:value>=target;},6000,true,direction);tick();}
  void at(int x,int y,bool yFirst=false){auto horizontal=[&]{if(s().objects[0].x!=x)coordinate(true,x,s().objects[0].x>x?7:3);};
    auto vertical=[&]{if(s().objects[0].y!=y)coordinate(false,y,s().objects[0].y>y?1:5);};
    if(yFirst){vertical();horizontal();}else{horizontal();vertical();}}
  void diagonal(int x,int y,uint8_t direction){until([&]{const auto &o=s().objects[0];return (direction==6||direction==8?o.x<=x:o.x>=x)&&(direction==2||direction==8?o.y<=y:o.y>=y);},6000,true,direction);tick();}
};

int main(int argc,char **argv){Run *run=nullptr;std::string error;
  try{
    require(argc>=2,"usage mpe4-session-arcada RAW.bin");run=new Run(argv[1]);auto &r=*run;auto &s=r.s();
    r.until([&]{return s.vars[0]==69&&s.modal==mpe4::StringInput;},5000,false);
    require(s.vars[7]==202&&s.vars[10]==2&&s.modalMaximum==18,"source startup state");r.mark("source-login-rendered");
    for(const char *name="Rogerx";*name;name++)r.tick(*name);r.tick(mpe4::Backspace);r.tick(mpe4::Enter);
    r.until([&]{return s.vars[0]==2;});r.ready();r.mark("name-to-room2-rendered");
    require(!strcmp(s.strings[1],"Roger"),"typed and edited name retained");
    // Allow the scheduled full-screen Arcada alarm to arrive, then dismiss it.
    for(unsigned i=0;i<240;i++)r.tick(s.modal==mpe4::Message?mpe4::Enter:0);
    r.ready();r.mark("room2-alarm-complete");
    r.type("look");r.until([&]{return s.modal==mpe4::Message;},1000,false);r.mark("room2-look-message");r.tick(mpe4::Enter);r.ready();
    r.tick(mpe4::F1+4,0,63);r.until([&]{return r.fixture.saves>0;});r.ready();r.mark("authored-F5-save");
    r.until([&]{return s.vars[0]==77;},150000);r.mark("authored-timed-arcada-death");
    r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan;});r.tick(mpe4::F1+5,0,64);
    r.until([&]{return r.fixture.restores==1;});r.ready();require(s.vars[0]==2&&s.vars[3]==0,"restore after timed death");r.mark("C64-F6-restore-after-death");
    // Narrow west archive doorway uses the source y62..73 band.
    if(s.objects[0].y<70)r.coordinate(false,70,5);else if(s.objects[0].y>70)r.coordinate(false,70,1);
    r.room(1,7);r.mark("room2-to-archive-real-collision");
    r.room(4,7);r.mark("archive-to-room4");r.room(1,3);r.mark("room4-to-archive-scientist-trigger");
    r.until([&]{return s.vars[51]==2&&s.vars[34]==6;},20000);r.mark("scientist-slumped");
    if(s.objects[0].x<107)r.coordinate(true,107,3);else if(s.objects[0].x>107)r.coordinate(true,107,7);
    if(s.objects[0].y<110)r.coordinate(false,110,5);else if(s.objects[0].y>110)r.coordinate(false,110,1);
    r.type("look man");r.until([&]{return s.vars[3]>=2;},20000);r.ready();r.mark("scientist-clue-score2");
    r.type("search man");for(unsigned i=0;i<240;i++)r.tick(s.modal==mpe4::Message?mpe4::Enter:0);r.ready();
    if(s.objects[0].x>84)r.coordinate(true,84,7);else if(s.objects[0].x<84)r.coordinate(true,84,3);
    if(s.objects[0].y>106)r.coordinate(false,106,1);else if(s.objects[0].y<106)r.coordinate(false,106,5);
    r.type("look monitor");r.until([&]{return s.modal==mpe4::StringInput;},2000);r.type("ASTRAL BODY");
    r.until([&]{return s.vars[50]==2&&r.session.game.flag(35)&&s.objects[1].x==80&&s.objects[1].y==68&&s.objects[1].motionMode==0;},30000);r.ready();r.mark("astral-body-retrieval");
    r.type("get cartridge");r.until([&]{return s.inventory[1]==255;},2000);r.mark("cartridge-inventory");
    r.ready();r.tick(mpe4::F1+4,0,63);r.until([&]{return r.fixture.saves==2;});r.ready();r.mark("archive-save-with-inventory");
    r.coordinate(false,108,5);r.room(4,7);r.coordinate(true,55,7);r.coordinate(false,68,5);r.room(3,7);r.mark("room3-keycard-corpse");
    r.command("search man");r.command("get keycard");require(s.inventory[5]==255&&s.vars[3]==8,"keycard puzzle/score");r.mark("keycard-inventory-score8");
    // Physical C64 F6 maps to the original game restore controller.
    r.tick(mpe4::F1+5,0,64);r.until([&]{return r.fixture.restores==2;});r.ready();
    require(s.vars[0]==1&&s.inventory[1]==255&&s.inventory[5]!=255&&s.vars[3]==7,"restore archive checkpoint");r.mark("save-restore-room-inventory-rendered");
    r.coordinate(false,108,5);r.room(4,7);r.coordinate(true,55,7);r.coordinate(false,68,5);r.room(3,7);
    r.command("search man");r.command("get keycard");require(s.inventory[5]==255&&s.vars[3]==8,"reacquire after restore");
    r.room(4,3);r.at(35,63);r.until([&]{return s.vars[30]==1&&r.session.game.flag(30);});
    r.until([&]{return s.objects[0].y>=145&&s.playerControl&&s.inputEnabled;},20000,true,1);r.tick();r.mark("room4-elevator-lower-level");
    r.room(2,3);r.at(28,144);r.until([&]{return s.vars[30]==1;});r.room(5,1);r.mark("room2-elevator-room5");
    r.coordinate(false,147,5);r.room(6,3);r.at(100,132,true);r.command("push open bay door");
    r.until([&]{return s.vars[52]==1&&s.vars[3]==10&&s.playerControl&&s.inputEnabled;});
    require(s.vars[52]==1&&s.vars[3]==10,"bay door puzzle");r.mark("vehicle-bay-open-score10");
    r.room(7,3);r.at(85,128,true);r.command("use keycard");require(s.vars[3]==12&&r.session.game.flag(153),"keycard elevator unlock");
    r.at(106,135,true);r.until([&]{return s.vars[30]==1;});r.room(9,1);r.mark("restricted-elevator-flight-prep");
    r.at(67,104,true);r.command("push left button");r.until([&]{return s.vars[70]==3&&s.inputEnabled;});
    r.at(60,103);r.command("get gadget");require(s.vars[3]==14&&s.inventory[3]==255,"gadget acquisition");r.mark("gadget-inventory-score14");
    r.command("push right button");r.until([&]{return s.vars[69]==3&&s.inputEnabled;});r.at(76,103);r.command("get suit");
    require(s.vars[81]==1&&s.vars[3]==16,"spacesuit equipped");r.mark("flight-suit-score16");
    r.coordinate(true,130,3);r.at(75,153,true);r.command("push airlock button");
    r.until([&]{return r.session.game.flag(35);});r.coordinate(true,130,3);r.coordinate(false,115,1);r.room(8,7);r.mark("open-airlock-to-vehicle-bay");
    r.at(116,144);r.command("push platform button");r.until([&]{return r.session.game.flag(54)&&s.vars[3]==17;});
    r.at(51,99);r.type("enter pod");r.until([&]{return s.vars[0]==10&&s.inputEnabled&&!s.inScan&&s.modal==mpe4::NoModal;});r.mark("escape-pod-entered");
    r.command("close door");r.command("fasten seatbelt");require(r.session.game.flag(155)&&r.session.game.flag(44),"door/seatbelt secured");
    r.command("push power button");r.until([&]{return r.session.game.flag(188)&&s.inputEnabled;});
    r.command("push autonav button");r.until([&]{return r.session.game.flag(81)&&s.vars[3]==19&&s.inputEnabled;});
    r.type("pull lever");r.until([&]{return s.vars[0]==13&&s.vars[3]==34;},30000);r.mark("arcada-escape-score34");
    r.until([&]{return s.vars[0]==14&&s.inputEnabled;},30000);r.mark("kerona-crash-cinematic");
    r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan&&s.inputEnabled;});
    require(s.vars[0]==14&&s.vars[3]==34,"Arcada escape and Kerona crash final state");
    require(!strcmp(s.strings[1],"Roger")&&s.inventory[1]==255&&s.inventory[3]==255&&s.inventory[5]==255,"escaped inventory and player name");
    r.mark("arcada-complete-kerona-crash-score34");
  }catch(const std::exception &e){error=e.what();fprintf(stderr,"FAIL %s\n",error.c_str());
    if(run){auto &s=run->s();auto &o=s.objects[0];fprintf(stderr,"EGO view%u loop%u cel%u width%u height%u flags%u direction%u mode%u target(%u,%u) step%u motionFlag%u logic%u ip%u\n",o.view,o.loop,o.cel,o.width,o.height,o.flags,o.direction,o.motionMode,o.targetX,o.targetY,o.stepSize,o.motionFlag,s.logic,s.callDepth?s.calls[s.callDepth-1].ip:0);
      fprintf(stderr,"NEXT BASELINE:");for(int x=int(o.x)-1;x<int(o.x)+o.width;x++)if(x>=0&&x<160)fprintf(stderr," %d:%u",x,run->session.renderer.priorityAt(x,o.y));fputs("\n",stderr);
    }}
  printf("{\"passed\":%s,\"scope\":\"Arcada acceptance: actual portable Session, raw package and native renderer; genuine keyboard/joystick input; later gameplay is outside this test\",\"reached\":\"%s\",\"frames\":%u,\"changedCells\":%u,\"fullFrames\":%u,\"room\":%u,\"score\":%u,\"saves\":%u,\"restores\":%u,\"sessionBytes\":%zu,\"error\":\"%s\",\"gates\":[",
    error.empty()?"true":"false",run?run->reached.c_str():"not-started",run?run->frames:0,run?run->changedCells:0,run?run->fullFrames:0,
    run?run->s().vars[0]:0,run?run->s().vars[3]:0,run?run->fixture.saves:0,run?run->fixture.restores:0,sizeof(mpe4::Session),error.c_str());
  if(run)for(size_t i=0;i<run->gates.size();i++)printf("%s\"%s\"",i?",":"",run->gates[i].c_str());
  puts("]}");delete run;return error.empty()?0:1;
}

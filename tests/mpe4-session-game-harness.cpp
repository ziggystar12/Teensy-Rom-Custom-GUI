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
    // Match the ordinary IBM scan accompanying the C64 terminal's ASCII event.
    static const uint8_t letterScan[26]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    if(!scan){if(key>='a'&&key<='z')scan=letterScan[key-'a'];else if(key>='A'&&key<='Z')scan=letterScan[key-'A'];
      else if(key==' ')scan=57;else if(key==mpe4::Enter)scan=28;else if(key==mpe4::Backspace)scan=14;}
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
    fprintf(stderr,"ROOM %u -> %u direction%u at%u,%u\n",from,expected,direction,s().objects[0].x,s().objects[0].y);
    until([&]{return s().vars[0]!=from;},15000,true,direction);tick();
    require(s().vars[0]==expected,"unexpected room transition "+position());ready();}
  void coordinate(bool xAxis,int target,uint8_t direction){const uint8_t room=s().vars[0];fprintf(stderr,"MOVE %u %c to%d dir%u at%u,%u\n",room,xAxis?'x':'y',target,direction,s().objects[0].x,s().objects[0].y);until([&]{require(s().vars[0]==room,"room changed during coordinate movement");int value=xAxis?s().objects[0].x:s().objects[0].y;return direction==1||direction==7?value<=target:value>=target;},6000,true,direction);tick();}
  void at(int x,int y,bool yFirst=false){auto horizontal=[&]{if(s().objects[0].x!=x)coordinate(true,x,s().objects[0].x>x?7:3);};
    auto vertical=[&]{if(s().objects[0].y!=y)coordinate(false,y,s().objects[0].y>y?1:5);};
    if(yFirst){vertical();horizontal();}else{horizontal();vertical();}}
  void diagonal(int x,int y,uint8_t direction){const uint8_t room=s().vars[0];fprintf(stderr,"DIAG %u to%d,%d dir%u at%u,%u\n",room,x,y,direction,s().objects[0].x,s().objects[0].y);until([&]{require(s().vars[0]==room,"room changed during diagonal movement");const auto &o=s().objects[0];return (direction==6||direction==8?o.x<=x:o.x>=x)&&(direction==2||direction==8?o.y<=y:o.y>=y);},6000,true,direction);tick();}
};

int main(int argc,char **argv){Run *run=nullptr;std::string error;bool wholeGameComplete=false;unsigned completedCreditMask=0;
  try{
    require(argc>=2,"usage mpe4-session-game RAW.bin [pickup-command]");run=new Run(argv[1]);auto &r=*run;auto &s=r.s();
    r.until([&]{return s.vars[0]==69&&s.modal==mpe4::StringInput;},5000,false);
    require(s.vars[7]==202&&s.vars[10]==2&&s.modalMaximum==18,"source startup state");r.mark("source-login-rendered");
    for(const char *name="Rogerx";*name;name++)r.tick(*name);r.tick(mpe4::Backspace);r.tick(mpe4::Enter);
    r.until([&]{return s.vars[0]==2;});r.ready();r.mark("name-to-room2-rendered");
    require(!strcmp(s.strings[1],"Roger"),"typed and edited name retained");
    // Allow the scheduled full-screen Arcada alarm to arrive, then dismiss it.
    for(unsigned i=0;i<240;i++)r.tick(s.modal==mpe4::Message?mpe4::Enter:0);
    r.ready();r.mark("room2-alarm-complete");
    r.type("look");r.until([&]{return s.modal==mpe4::Message;},1000,false);r.mark("room2-look-message");r.tick(mpe4::Enter);r.ready();
    r.tick(0,0,63);r.until([&]{return r.fixture.saves>0;});r.ready();r.mark("authored-F5-save");
    r.until([&]{return s.vars[0]==77;},150000);r.mark("authored-timed-arcada-death");
    r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan;});r.tick(0,0,65);
    r.until([&]{return r.fixture.restores==1;});r.ready();require(s.vars[0]==2&&s.vars[3]==0,"restore after timed death");r.mark("F7-restore-after-death");
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
    r.type("look monitor");r.until([&]{return s.modal==mpe4::StringInput;},2000);r.type(argc>2&&argv[2][0]>='a'&&argv[2][0]<='z'?"astral body":"ASTRAL BODY");
    r.until([&]{return s.vars[50]==2&&r.session.game.flag(35)&&s.objects[1].x==80&&s.objects[1].y==68&&s.objects[1].motionMode==0;},30000);r.ready();r.mark("astral-body-retrieval");
    r.type(argc>2?argv[2]:"get cartridge");
    if(argc>2&&(strstr(argv[2],"tape")||strstr(argv[2],"TAPE"))){r.until([&]{return s.modal==mpe4::Message;},2000,false);
      require(s.inventory[1]!=255&&s.vars[9]==2,"original vocabulary rejects unknown TAPE noun");r.mark("source-tape-word-rejected");goto complete;}
    r.until([&]{return s.inventory[1]==255;},2000);require(s.vars[3]==7,"cartridge acquisition score");r.mark("cartridge-inventory");if(argc>2)goto complete;
    r.ready();r.tick(0,0,63);r.until([&]{return r.fixture.saves==2;});r.ready();r.mark("archive-save-with-inventory");
    r.coordinate(false,108,5);r.room(4,7);r.coordinate(true,55,7);r.coordinate(false,68,5);r.room(3,7);r.mark("room3-keycard-corpse");
    r.command("search man");r.command("get keycard");require(s.inventory[5]==255&&s.vars[3]==8,"keycard puzzle/score");r.mark("keycard-inventory-score8");
    // F7 is the original DOS restore binding (PC scan65), independent of C64 labels.
    r.tick(0,0,65);r.until([&]{return r.fixture.restores==2;});r.ready();
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
    r.command("unfasten seatbelt");r.command("get survival kit");require(s.inventory[11]==255&&s.vars[3]==36,"survival kit acquisition");
    r.command("open kit");require(s.inventory[11]!=255&&s.inventory[12]==255&&s.inventory[19]==255&&s.inventory[22]==255,"opened-kit inventory/showobj");r.mark("kerona-kit-opened-score36");
    r.type("exit pod");r.until([&]{return s.vars[0]==30;});r.ready();r.mark("kerona-outside-pod");
    require(s.objects[0].x==40&&s.objects[0].y==106,"pod exit baseline");
    r.coordinate(true,38,7);r.at(50,120,true);r.command("get glass");
    require(s.inventory[6]==255&&s.vars[3]==39,"reflective glass puzzle");r.mark("kerona-supplies-score39");
    r.room(21,3);r.coordinate(false,129,5);r.room(22,3);r.coordinate(false,138,5);r.room(23,3);r.mark("kerona-cliff-entry");
    r.coordinate(false,150,5);r.until([&]{return s.objects[0].x>=17;},6000,true,2);r.tick();
    r.at(20,132);r.coordinate(true,23,3);r.until([&]{return r.session.game.flag(92);});
    r.at(26,132);r.coordinate(false,130,1);r.until([&]{return s.objects[0].x>=32;},6000,true,2);r.tick();r.at(32,123);r.command("slow");r.at(37,123);
    r.command("normal");r.coordinate(false,89,1);r.until([&]{return s.objects[0].x>=38&&s.objects[0].y<=86;},6000,true,2);r.tick();
    r.room(20,1);r.mark("kerona-cliff-high-route");r.command("slow");r.coordinate(false,65,1);
    require(!r.session.game.flag(3)&&!r.session.game.flag(91),"safe west cliff vertical");
    r.until([&]{return s.objects[0].x<=3;},6000,true,8);r.tick();r.room(19,7);r.command("normal");r.mark("kerona-boulder-room");
    r.coordinate(false,59,1);r.coordinate(true,56,7);r.command("fastest");
    for(const char *text="push rock";*text;text++)r.tick(*text);
    r.until([&]{const auto &sp=s.objects[16];return sp.x>=36&&sp.x<=58&&sp.y>=137&&sp.y<=151&&(sp.flags&mpe4::Drawn)&&s.vars[108]==0&&r.session.game.flag(97)&&!r.session.game.flag(110)&&!r.session.game.flag(161);},100000);
    r.tick(mpe4::Enter);r.until([&]{return s.vars[108]==2&&r.session.game.flag(161)&&s.playerControl&&s.inputEnabled;},30000);r.ready();
    require(s.vars[3]==44,"spider boulder score");r.mark("kerona-spider-crushed-score44");r.command("normal");
    r.coordinate(true,20,7);r.coordinate(false,61,5);r.room(18,7);r.coordinate(false,64,1);r.diagonal(138,52,8);
    r.coordinate(false,44,1);r.diagonal(130,36,8);r.room(15,1);r.command("slow");r.coordinate(false,132,1);
    r.diagonal(138,128,8);r.coordinate(false,104,1);require(!r.session.game.flag(91),"safe cliff ascent");r.room(16,3);r.command("normal");
    r.coordinate(true,45,3);r.diagonal(53,69,2);r.coordinate(true,109,3);r.diagonal(117,61,2);r.room(17,3);
    r.until([&]{return s.vars[0]!=17;},20000,true,3);r.tick();r.until([&]{return s.vars[0]==25;},20000);r.ready();
    require(s.vars[3]==46&&r.session.game.flag(177),"alien lift score");r.mark("kerona-alien-lift-score46");
    r.coordinate(true,69,7);r.diagonal(61,153,8);r.command("get rock");require(s.inventory[2]==255,"cavern rock inventory");r.room(26,7);
    r.coordinate(true,71,7);r.diagonal(63,130,8);r.coordinate(true,43,7);r.diagonal(31,142,6);
    r.command("put rock in geyser");r.until([&]{return s.vars[3]==50&&r.session.game.flag(84)&&s.playerControl&&s.inputEnabled;});
    require(s.inventory[2]!=255,"geyser consumes rock");r.mark("kerona-geyser-plugged-score50");
    r.coordinate(true,15,7);r.coordinate(false,122,1);r.room(27,1);r.coordinate(true,102,3);r.coordinate(false,133,1);r.diagonal(98,129,8);
    r.coordinate(true,78,7);r.diagonal(54,105,8);r.room(28,7);r.coordinate(false,148,5);r.coordinate(true,68,7);
    r.command("put glass in beam");require(s.vars[3]==55&&r.session.game.flag(121)&&s.inventory[6]==255,"beam reflection puzzle");r.mark("kerona-beam-reflected-score55");
    r.diagonal(29,114,8);r.coordinate(false,90,1);require(!r.session.game.flag(3)&&!r.session.game.flag(91),"safe beam trigger crossing");
    r.coordinate(true,41,3);r.diagonal(64,64,2);r.coordinate(true,88,3);r.diagonal(92,60,2);r.coordinate(true,101,3);r.diagonal(104,57,2);r.room(27,3);
    r.coordinate(true,80,3);r.until([&]{return !(s.objects[2].flags&mpe4::Drawn)&&s.vars[31]>=27&&s.vars[31]<=45;});r.coordinate(true,92,3);
    r.until([&]{return !(s.objects[3].flags&mpe4::Drawn)&&s.vars[32]>=27&&s.vars[32]<=45;});r.coordinate(true,108,3);
    r.until([&]{return !(s.objects[4].flags&mpe4::Drawn)&&s.vars[33]>=27&&s.vars[33]<=45;});r.coordinate(true,120,3);r.room(26,3);
    require(s.vars[3]==58&&r.session.game.flag(178)&&!r.session.game.flag(91),"acid hazard route");r.mark("kerona-acid-crossing-score58");
    r.command("turn on translator");require(r.session.game.flag(154),"translator enabled");r.command("slow");r.coordinate(true,49,3);r.coordinate(false,69,5);
    require(s.objects[0].x==49&&s.objects[0].y==69&&!r.session.game.flag(3),"one-row alien passage");
    r.until([&]{return s.vars[0]!=26;},20000,true,3);r.tick();r.until([&]{return s.vars[0]==15&&s.playerControl&&s.inputEnabled;},30000);r.ready();r.command("normal");
    require(s.vars[3]==58&&s.vars[82]==1,"first alien audience progression");r.mark("first-alien-audience-return-score58");
    r.diagonal(128,131,4);r.coordinate(true,142,3);r.room(18,5);r.coordinate(true,130,3);r.diagonal(138,49,4);
    r.coordinate(false,59,5);r.diagonal(150,71,4);r.room(19,3);r.diagonal(15,59,2);r.room(20,3);r.diagonal(26,69,4);r.room(23,5);
    r.command("slow");r.coordinate(false,101,5);r.diagonal(34,106,6);r.coordinate(false,124,5);
    r.diagonal(26,132,6);r.coordinate(false,133,5);r.coordinate(true,24,7);r.until([&]{return !r.session.game.flag(92);});
    r.coordinate(false,140,5);r.coordinate(true,60,3);r.coordinate(false,110,1);r.diagonal(62,108,2);r.room(20,1);r.command("normal");
    r.coordinate(true,94,3);r.coordinate(false,143,1);r.coordinate(true,127,3);r.coordinate(false,129,1);r.coordinate(true,141,3);
    r.command("slow");r.at(147,129);for(const char *text="throw water";*text;text++)r.tick(*text);
    r.until([&]{return s.vars[0]==24;},1000,true,3);r.tick();
    require(!strcmp(s.input,"throw water")&&s.inputLength==11,"pretyped water retained across room transition");
    // v36 is computed only on the alternate live-spider branch. The successful
    // water branch independently computes its distance into v39 on submission.
    r.until([&]{return s.inputEnabled&&s.playerControl&&!s.inScan&&s.modal==mpe4::NoModal&&r.session.game.flag(38)&&!r.session.game.flag(97)&&!r.session.game.flag(43);},1000);
    r.tick(mpe4::Enter);r.until([&]{return s.vars[3]==63&&s.vars[82]==2&&r.session.game.flag(75)&&!r.session.game.flag(42)&&s.playerControl&&s.inputEnabled;},30000);r.ready();
    require(s.inventory[12]!=255&&s.inventory[4]==0,"Orat water consumed before chunk pickup");r.mark("orat-defeated-score63");
    r.at(120,128);r.command("get chunk");require(s.vars[3]==65&&s.inventory[4]==255,"Orat chunk inventory");r.mark("orat-chunk-score65");
    r.coordinate(false,143,5);r.coordinate(true,20,7);r.coordinate(false,138,1);r.coordinate(true,17,7);r.command("slow");r.room(20,1);r.command("normal");r.diagonal(119,143,6);r.coordinate(true,117,7);r.room(23,5);
    r.command("slow");r.diagonal(53,132,6);r.coordinate(false,140,5);r.coordinate(true,24,7);r.coordinate(false,132,1);
    r.until([&]{return r.session.game.flag(92);});r.at(26,132);r.coordinate(false,130,1);r.diagonal(32,124,2);r.at(37,123);r.coordinate(false,89,1);r.diagonal(40,86,2);r.room(20,1);
    r.command("slow");r.coordinate(false,65,1);r.diagonal(3,41,8);r.room(19,7);r.command("normal");r.coordinate(false,59,1);r.coordinate(true,20,7);r.coordinate(false,61,5);r.room(18,7);
    r.coordinate(false,64,1);r.diagonal(138,52,8);r.coordinate(false,44,1);r.diagonal(130,36,8);r.room(15,1);r.command("slow");r.coordinate(false,132,1);
    r.diagonal(138,128,8);r.coordinate(false,104,1);r.room(16,3);r.command("normal");r.coordinate(true,45,3);r.diagonal(53,69,2);r.coordinate(true,109,3);r.diagonal(117,61,2);r.room(17,3);
    r.until([&]{return s.vars[0]!=17;},20000,true,3);r.tick();r.until([&]{return s.vars[0]==25;});r.ready();r.mark("orat-proof-return-lift");
    r.coordinate(true,69,7);r.diagonal(61,153,8);r.room(26,7);r.coordinate(true,71,7);r.diagonal(63,130,8);r.coordinate(true,43,7);r.diagonal(31,142,6);
    r.coordinate(true,15,7);r.coordinate(false,122,1);r.room(27,1);r.coordinate(true,102,3);r.coordinate(false,133,1);r.diagonal(98,129,8);r.coordinate(true,78,7);r.diagonal(54,105,8);r.room(28,7);
    r.coordinate(false,146,5);r.coordinate(true,61,7);r.diagonal(29,114,8);r.coordinate(false,90,1);r.coordinate(true,41,3);r.diagonal(64,64,2);r.coordinate(true,88,3);r.diagonal(92,60,2);r.coordinate(true,101,3);r.diagonal(104,57,2);r.room(27,3);
    r.coordinate(true,80,3);r.until([&]{return !(s.objects[2].flags&mpe4::Drawn)&&s.vars[31]>=27&&s.vars[31]<=45;});r.coordinate(true,92,3);
    r.until([&]{return !(s.objects[3].flags&mpe4::Drawn)&&s.vars[32]>=27&&s.vars[32]<=45;});r.coordinate(true,108,3);
    r.until([&]{return !(s.objects[4].flags&mpe4::Drawn)&&s.vars[33]>=27&&s.vars[33]<=45;});r.coordinate(true,120,3);r.room(26,3);
    r.command("slow");r.coordinate(true,49,3);r.coordinate(false,69,5);r.until([&]{return s.vars[0]==29;},20000,true,3);r.tick();
    r.until([&]{return s.vars[33]==14&&r.session.game.flag(39)&&s.inputEnabled;});r.type("drop orat part");
    r.until([&]{return s.vars[82]==3&&!r.session.game.flag(36)&&s.playerControl;});
    require(s.vars[3]==75&&s.inventory[4]==0,"Orat proof delivered");r.mark("orat-proof-delivered-score75");
    r.room(31,1);r.until([&]{return s.vars[41]==1&&s.vars[39]==8&&s.playerControl&&s.inputEnabled;});r.ready();
    r.coordinate(false,120,1);r.coordinate(true,85,3);r.type("put cartridge in slot");
    for(unsigned page=2;page<=5;page++){r.until([&]{return s.vars[31]==page&&r.session.game.flag(33)&&s.modal==mpe4::NoModal;});
      require(s.vars[3]==80,"cartridge insertion score");r.mark((std::string("cartridge-record-page-")+std::to_string(page)).c_str());r.tick(' ');r.tick();}
    r.until([&]{return s.vars[31]==0&&s.inputEnabled;});r.command("get cartridge");require(s.vars[3]==85&&s.inventory[1]==255,"cartridge retrieval");r.mark("cartridge-recovered-score85");
    r.coordinate(false,135,5);r.coordinate(true,125,3);r.command("enter skimmer");r.until([&]{return s.vars[41]==2&&!(s.objects[0].flags&mpe4::Drawn);});
    r.type("turn key");r.until([&]{return s.vars[0]==78;});r.mark("kerona-skimmer-launch");r.until([&]{return s.vars[0]==33&&s.playerControl;},30000);r.tick();r.mark("skimmer-flight-entered");
    unsigned maximumCollisions=0;
    for(unsigned frame=0;frame<60000&&s.vars[0]==33;frame++){
      auto unsafe=[&](int x){for(unsigned id:{2u,3u}){const auto &o=s.objects[id];if((o.flags&mpe4::Drawn)&&o.y>=60&&o.y<=129&&x>=int(o.x)-21&&x<=int(o.x)+9)return true;}return false;};
      unsigned direction=0;int x=s.objects[0].x;if(unsafe(x)){int nearest=-1,distance=1000;for(int candidate=10;candidate<=122;candidate++)if(!unsafe(candidate)&&abs(candidate-x)<distance){nearest=candidate;distance=abs(candidate-x);}require(nearest>=0,"safe skimmer lane exists");direction=nearest<x?7:3;}
      if(s.vars[42]>maximumCollisions)maximumCollisions=s.vars[42];require(s.vars[42]<5,"nonfatal skimmer steering");r.tick(s.modal==mpe4::Message?mpe4::Enter:0,direction);}
    require(s.vars[0]==35,"skimmer flight reaches Ulence");r.tick();r.until([&]{return s.vars[3]==110&&r.session.game.flag(90)&&r.session.game.flag(32)&&r.session.game.flag(35)&&s.playerControl&&s.inputEnabled;});r.ready();r.mark("ulence-arrival-score110");
    fprintf(stderr,"SKIMMER maximum collisions %u\n",maximumCollisions);
    r.command("get skimmer key");require(s.inventory[7]==255,"skimmer key recovered");r.command("exit skimmer");
    r.until([&]{return s.vars[58]==4&&r.session.game.flag(185)&&!r.session.game.flag(68)&&r.session.game.flag(39)&&s.vars[30]>0&&s.inputEnabled;});
    r.type("no");r.until([&]{return s.vars[58]==10&&r.session.game.flag(185)&&!r.session.game.flag(68)&&!r.session.game.flag(39)&&s.playerControl&&s.inputEnabled;});r.ready();
    r.at(60,143);r.diagonal(85,118,2);r.coordinate(true,87,3);r.diagonal(90,115,2);r.room(70,3);r.coordinate(false,130,1);r.room(35,7);r.coordinate(true,50,7);r.coordinate(false,132,5);
    r.until([&]{return s.vars[58]==7&&r.session.game.flag(68)&&!r.session.game.flag(185)&&r.session.game.flag(39)&&s.vars[30]>0&&s.inputEnabled;});
    r.type("yes");r.until([&]{return s.vars[3]==115&&s.vars[58]==9&&s.vars[124]==30&&s.inventory[9]==255&&s.inventory[10]==255&&s.inventory[7]!=255&&s.playerControl&&s.inputEnabled;});r.ready();r.mark("ulence-skimmer-sold-score115");
    r.at(60,143,true);r.diagonal(85,118,2);r.coordinate(true,87,3);r.diagonal(90,115,2);r.room(70,3);r.coordinate(true,94,3);r.diagonal(120,160,4);
    for(unsigned drink=1;drink<=3;drink++){
      r.until([&]{return s.vars[35]==100&&s.vars[39]==2&&r.session.game.flag(41)&&s.inventory[8]!=255&&s.inputEnabled;});
      r.type("buy beer");r.until([&]{return s.vars[124]==30-drink*2&&s.inventory[8]==255&&s.inputEnabled;});r.ready();
      r.type("drink beer");r.until([&]{return s.vars[40]==drink&&s.vars[124]==30-drink*2&&s.inventory[8]!=255&&s.inputEnabled&&(drink!=3||(s.vars[3]==120&&r.session.game.flag(181)));});r.ready();}
    r.mark("ulence-three-beers-sector-HH-score120");
    r.until([&]{return !r.session.game.flag(40);});r.coordinate(false,144,1);r.diagonal(119,143,8);r.coordinate(false,142,1);r.room(75,3);r.until([&]{return !r.session.game.flag(198)&&s.inputEnabled&&s.playerControl;});r.ready();
    unsigned savedCash=s.vars[124];auto saveSlot=[&]{unsigned before=r.fixture.saves;r.tick(0,0,63);r.until([&]{return r.fixture.saves>before;});r.ready();};
    auto restoreSlot=[&]{unsigned before=r.fixture.restores;r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan;});r.tick(0,0,65);r.until([&]{return r.fixture.restores>before;});r.ready();require(s.vars[0]==75&&s.vars[124]==savedCash,"slot safety restore");};
    saveSlot();bool cashedOut=false;
    for(unsigned round=1;round<=1200&&!cashedOut;round++){
      r.ready();r.tick(0,0,66);r.until([&]{return s.vars[123]==3&&!s.inputEnabled;});
      r.until([&]{return s.vars[0]!=75||r.session.game.flag(123)||r.session.game.flag(233)||(s.inputEnabled&&s.vars[123]==0&&s.vars[129]==8&&!r.session.game.flag(124)&&!r.session.game.flag(126)&&!r.session.game.flag(134));});
      fprintf(stderr,"SLOTS round%u cash%u saved%u random%lu room%u skull%u\n",round,s.vars[124],savedCash,(unsigned long)s.random,s.vars[0],r.session.game.flag(123)||r.session.game.flag(233));
      if(r.session.game.flag(123)||r.session.game.flag(233)){restoreSlot();continue;}
      if(s.vars[0]!=75){r.ready();if(s.vars[0]==70&&s.vars[124]==250){cashedOut=true;break;}restoreSlot();continue;}
      if(s.vars[124]<savedCash){restoreSlot();continue;}
      if(s.vars[124]>savedCash){savedCash=s.vars[124];saveSlot();}
      if(savedCash==250){r.tick(0,0,68);r.until([&]{return s.vars[0]==70;});r.ready();cashedOut=true;}
    }
    require(cashedOut&&s.vars[0]==70&&s.vars[124]==250,"slot safety loop reaches250 buckazoids");r.mark("ulence-slots-cash250-score120");
    r.coordinate(false,130,1);r.room(35,7);r.coordinate(true,40,7);r.room(38,1);r.room(39,3);r.coordinate(true,60,3);r.room(71,1);
    r.coordinate(true,128,3);r.coordinate(false,73,1);r.coordinate(true,110,7);r.coordinate(false,68,1);r.coordinate(true,103,7);
    r.until([&]{return s.vars[30]==1&&s.vars[34]==6&&r.session.game.flag(38)&&r.session.game.flag(37)&&r.session.game.flag(39)&&s.inputEnabled;});r.ready();
    r.command("buy droid");r.until([&]{return s.vars[3]==124&&s.vars[124]==214&&r.session.game.flag(60)&&r.session.game.flag(41)&&r.session.game.flag(174);});r.ready();r.mark("ulence-flight-droid-purchased-score124");
    r.coordinate(true,110,3);r.coordinate(false,73,5);r.coordinate(true,128,3);r.coordinate(false,163,5);r.coordinate(true,72,7);r.room(39,5);r.coordinate(false,160,5);r.room(38,7);r.room(37,7);
    if(!r.session.game.flag(65)){r.room(34,5);r.until([&]{return (s.objects[15].flags&mpe4::Drawn)&&s.objects[15].motionMode==2;});r.room(37,1);r.until([&]{return r.session.game.flag(65);});}
    r.at(109,133);r.until([&]{return !r.session.game.flag(43)&&s.inputEnabled&&s.playerControl;});r.command("buy ship");
    r.until([&]{return s.vars[3]==128&&s.vars[124]==0&&r.session.game.flag(74)&&s.inventory[10]!=255;});r.ready();r.mark("ulence-correct-ship-purchased-score128");
    r.command("enter ship");r.until([&]{return r.session.game.flag(37)&&r.session.game.flag(45)&&r.session.game.flag(117)&&!r.session.game.flag(69)&&s.inputEnabled;});
    r.command("push load button");r.until([&]{return r.session.game.flag(69)&&r.session.game.flag(34)&&s.inputEnabled&&s.modal==mpe4::NoModal&&!s.inScan;});r.type("hh");
    r.until([&]{return s.vars[0]==43&&s.vars[36]==5&&s.inputEnabled;},60000);r.ready();require(s.vars[3]==153,"Deltaur flight score");r.mark("deltaur-arrival-score153");
    r.command("wear jetpack");r.command("exit ship");r.until([&]{return s.vars[0]==45;});r.ready();r.at(80,104);
    r.command("open door");r.coordinate(true,76,7);r.room(61,1);r.at(90,110);
    r.until([&]{return s.vars[31]==2&&s.objects[2].y>=125;});r.coordinate(true,78,7);r.room(57,1);
    require(s.vars[3]==154,"Deltaur inner airlock score");r.mark("deltaur-entered-undetected-score154");
    r.coordinate(false,145,1);r.diagonal(50,120,8);r.command("open trunk");r.type("enter trunk");r.until([&]{return s.vars[0]==53&&s.vars[3]==157;},40000);
    r.tick(' ');r.until([&]{return s.inputEnabled&&s.modal==mpe4::NoModal&&!s.inScan;});
    r.command("open trunk");r.ready();r.mark("deltaur-laundry-arrival-score157");r.at(67,109);r.command("open unit");r.until([&]{return s.vars[72]==3;});r.type("enter unit");
    r.until([&]{return s.vars[3]==162&&s.vars[72]==5&&s.vars[81]==3&&s.inputEnabled;},40000);r.command("exit unit");r.ready();r.mark("deltaur-sarien-disguise-score162");
    r.room(54,3);r.at(110,120,true);r.until([&]{return s.vars[119]<=30;});
    for(unsigned conversation=0;conversation<200&&!r.session.game.flag(218);conversation++)r.command("talk alien");
    require(r.session.game.flag(218)&&s.vars[3]==163,"guard asks King's Quest II question");r.command("yes");r.command("kiss alien");require(s.vars[3]==169,"guard conversation bonus");r.mark("deltaur-guard-dialogue-score169");
    r.at(66,101);r.room(49,1);r.coordinate(false,148,5);r.room(48,7);r.at(48,135);
    r.until([&]{return s.objects[0].y<80&&s.playerControl;},20000,true,1);r.tick();r.ready();r.coordinate(false,64,5);r.room(49,3);r.room(50,3);r.room(51,3);r.command("look suit");require(s.inventory[13]==255,"Sarien ID in uniform");
    r.at(101,124);r.command("show id");r.at(125,140,true);r.command("get grenade");r.at(101,124);r.until([&]{return s.inventory[14]==255&&s.inventory[15]==255&&s.vars[3]==173;});r.ready();r.mark("deltaur-armory-score173");
    r.room(50,7);r.at(77,41);r.command("drop grenade");r.until([&]{return s.vars[3]==178&&s.inventory[15]!=255;});r.ready();r.mark("deltaur-generator-guard-gassed-score178");
    r.room(51,3);r.at(101,124);r.command("show id");r.at(125,140,true);r.command("get grenade");require(s.vars[3]==179&&s.inventory[15]==255,"second grenade acquired");
    r.at(101,124);r.until([&]{return r.session.game.flag(38)&&!r.session.game.flag(37)&&s.modal==mpe4::NoModal&&!s.inScan;});r.room(50,7);r.room(49,7);r.room(48,7);
    r.until([&]{return s.vars[81]==4&&s.playerControl;},20000,true,7);r.tick();r.ready();r.mark("deltaur-helmet-lost-score179");
    r.at(48,53);r.until([&]{return s.objects[0].y>100&&s.playerControl;},20000,true,1);r.tick();r.ready();r.at(140,148,true);
    {unsigned saves=r.fixture.saves;r.tick(0,0,63);r.until([&]{return r.fixture.saves>saves;});r.ready();}
    for(unsigned encounter=0;encounter<100&&s.vars[3]<182;encounter++){
      r.room(49,3);if(s.vars[160]!=1){r.room(48,7);continue;}
      r.until([&]{return (s.objects[10].flags&mpe4::Drawn)!=0;});r.tick(0,3);
      for(unsigned frame=0;frame<2000&&s.vars[3]<182&&!s.vars[130];frame++)r.tick(s.modal==mpe4::Message?mpe4::Enter:0,0,64);
      if(s.vars[3]==182)break;
      unsigned restores=r.fixture.restores;r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan;});r.tick(0,0,65);r.until([&]{return r.fixture.restores>restores;});r.ready();
    }
    require(s.vars[3]==182&&r.session.game.flag(207),"one living Sarien defeated with F6");r.ready();r.mark("deltaur-live-sarien-combat-score182");r.room(50,3);
    r.coordinate(true,30,3);r.diagonal(50,158,4);r.coordinate(true,78,3);r.command("search guard");require(s.inventory[16]==255&&s.vars[3]==185,"guard remote control");r.command("push off button");require(s.vars[3]==188,"force-field disabled");r.mark("deltaur-force-field-off-score188");
    r.coordinate(true,60,7);r.coordinate(false,141,1);r.coordinate(true,78,3);r.coordinate(false,122,1);r.type("look panel");r.until([&]{return s.vars[0]==65&&!s.inScan;});
    const int keypad[5][3]={{93,124,1},{81,142,2},{81,124,3},{81,142,4},{81,85,4}};
    for(unsigned digit=0;digit<5;digit++){r.at(keypad[digit][0],keypad[digit][1]);r.tick(0,0,64);
      r.until([&]{return digit==4?s.vars[3]==198:s.vars[31]==keypad[digit][2];});}
    r.until([&]{return s.vars[0]==50&&s.vars[3]==198;},30000);r.ready();r.mark("deltaur-code6858-detonation-score198");
    r.coordinate(false,141,5);r.at(60,158);r.coordinate(true,50,7);r.diagonal(30,138,8);
    {unsigned saves=r.fixture.saves;r.tick(0,0,63);r.until([&]{return r.fixture.saves>saves;});r.ready();}
    bool escapeHallClear=false;
    for(unsigned encounter=0;encounter<100&&!escapeHallClear;encounter++){
      r.room(49,7);
      if(s.vars[160]==1){
        r.until([&]{return (s.objects[10].flags&mpe4::Drawn)!=0;});r.tick(0,s.objects[10].x<s.objects[0].x?7:3);
        for(unsigned frame=0;frame<2000&&s.vars[160]&&!s.vars[130];frame++)r.tick(s.modal==mpe4::Message?mpe4::Enter:0,0,64);
      }
      if(!s.vars[160]&&!s.vars[130]){r.ready();escapeHallClear=true;break;}
      unsigned restores=r.fixture.restores;r.until([&]{return s.modal==mpe4::NoModal&&!s.inScan;});r.tick(0,0,65);r.until([&]{return r.fixture.restores>restores;});r.ready();
    }
    require(escapeHallClear,"escape hallway guard defeated or absent");r.at(62,135);r.room(54,1);
    r.coordinate(false,105,5);r.at(91,100);r.room(62,1);require(s.vars[3]==199,"launch bay entry point");r.mark("deltaur-shuttle-bay-score199");
    r.coordinate(false,80,5);r.diagonal(36,81,4);r.coordinate(true,59,3);r.diagonal(64,86,4);
    r.coordinate(false,87,5);r.diagonal(69,92,4);r.coordinate(false,93,5);r.diagonal(71,95,4);
    r.coordinate(false,111,5);r.diagonal(70,112,6);r.coordinate(true,22,7);r.diagonal(12,122,6);
    r.command("enter ship");r.type("push launch");
    r.until([&]{return s.vars[0]==43&&s.vars[3]==202;},30000);r.mark("deltaur-shuttle-escape-score202");
    r.until([&]{return s.vars[0]==63;},40000);r.mark("deltaur-destroyed-score202");r.until([&]{return s.vars[0]==64;},40000);r.mark("xenon-hero-ceremony-score202");
    r.until([&]{return s.picture==42&&s.vars[34]==5;},40000);unsigned creditLines=0;
    for(unsigned frame=0;frame<20000&&creditLines!=0x7fff;frame++){
      if(s.vars[34]>=5&&s.vars[34]<=18)creditLines|=1u<<(s.vars[34]-5);else if(s.vars[34]==4)creditLines|=1u<<14;r.tick();}
    require(creditLines==0x7fff&&s.vars[0]==64&&s.vars[3]==202&&s.inventory[1]==255,"original winning scene and entire final credit cycle");
    r.until([&]{return s.vars[34]==5;});r.mark("original-winning-ending-and-full-credits-complete-score202");
    wholeGameComplete=true;completedCreditMask=creditLines;
    std::ofstream ending("winning-ending.frame",std::ios::binary);ending.write((char*)r.displayed,sizeof(r.displayed));
  }catch(const std::exception &e){error=e.what();fprintf(stderr,"FAIL %s\n",error.c_str());
    if(run){auto &s=run->s();fprintf(stderr,"ROOM DEBUG inputEnabled%u input=%s parsed=%s v36=%u v82=%u f38=%u f97=%u f43=%u\n",s.inputEnabled,s.input,s.parsedText,s.vars[36],s.vars[82],run->session.game.flag(38),run->session.game.flag(97),run->session.game.flag(43));for(unsigned i=0;i<32;i++)if(s.objects[i].flags&mpe4::Drawn){auto &o=s.objects[i];fprintf(stderr,"O%u %u,%u v%u l%u c%u p%u f%u dir%u mode%u target%u,%u\n",i,o.x,o.y,o.view,o.loop,o.cel,o.priority,o.flags,o.direction,o.motionMode,o.targetX,o.targetY);}}
    if(run){auto &s=run->s();auto &o=s.objects[0];fprintf(stderr,"EGO view%u loop%u cel%u width%u height%u flags%u direction%u mode%u target(%u,%u) step%u motionFlag%u logic%u ip%u\n",o.view,o.loop,o.cel,o.width,o.height,o.flags,o.direction,o.motionMode,o.targetX,o.targetY,o.stepSize,o.motionFlag,s.logic,s.callDepth?s.calls[s.callDepth-1].ip:0);
      fprintf(stderr,"NEXT BASELINE:");for(int x=int(o.x)-1;x<int(o.x)+o.width;x++)if(x>=0&&x<160)fprintf(stderr," %d:%u",x,run->session.renderer.priorityAt(x,o.y));fputs("\n",stderr);
      if(s.vars[0]==23)for(int y=115;y<141;y++){fprintf(stderr,"PRI y%u ",y);for(int x=15;x<48;x++)fprintf(stderr,"%x",run->session.renderer.priorityAt(x,y));fputs("\n",stderr);}}}
complete:
  printf("{\"passed\":%s,\"scope\":\"Actual portable Session, raw package and native renderer; genuine keyboard/joystick input only; %s\",\"wholeGameComplete\":%s,\"completedCreditMask\":%u,\"reached\":\"%s\",\"frames\":%u,\"changedCells\":%u,\"fullFrames\":%u,\"room\":%u,\"score\":%u,\"saves\":%u,\"restores\":%u,\"sessionBytes\":%zu,\"error\":\"%s\",\"gates\":[",
    error.empty()?"true":"false",argc>2?"focused cartridge pickup and original vocabulary acceptance":"complete authored SQ1 route through winning ending and full credits",wholeGameComplete?"true":"false",completedCreditMask,run?run->reached.c_str():"not-started",run?run->frames:0,run?run->changedCells:0,run?run->fullFrames:0,
    run?run->s().vars[0]:0,run?run->s().vars[3]:0,run?run->fixture.saves:0,run?run->fixture.restores:0,sizeof(mpe4::Session),error.c_str());
  if(run)for(size_t i=0;i<run->gates.size();i++)printf("%s\"%s\"",i?",":"",run->gates[i].c_str());
  puts("]}");delete run;return error.empty()?0:1;
}

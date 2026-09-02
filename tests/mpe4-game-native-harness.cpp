#include "mpe4_game.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>

static uint16_t u16(const uint8_t *p){return p[0]|uint16_t(p[1])<<8;}
static uint32_t u32(const uint8_t *p){return u16(p)|uint32_t(u16(p+2))<<16;}
static void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
struct Resource {uint32_t offset=0,length=0;};
struct Fixture {
  std::vector<uint8_t> bytes;
  Resource resources[7][256];
  mpe4::State saved{};
  bool savedValid=false;
  bool allowSave=true;
  unsigned reads=0,pictures=0,sounds=0,saves=0,restores=0,additions=0,maxRead=0;
  unsigned lastPicture=0;
  std::vector<uint8_t> overrideLogic;
  uint8_t priority=4;
  bool splitPriority=false;
  int blockedColumn=-1;
  Fixture(const char *file){std::ifstream f(file,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});
    require(bytes.size()>=64&&!memcmp(bytes.data(),"M4G1",4),"M4G1 file");
    unsigned index=u32(bytes.data()+12),count=u16(bytes.data()+16);
    for(unsigned i=0;i<count;i++){const uint8_t *p=bytes.data()+index+i*16;require(p[0]<7,"resource type");
      resources[p[0]][p[1]]={u32(p+4),u32(p+8)};require(u32(p+4)+u32(p+8)<=bytes.size(),"resource bounds");}}
  static uint32_t size(void *p,uint8_t t,uint8_t id){Fixture &f=*(Fixture*)p;
    if(t==0&&id==0&&!f.overrideLogic.empty())return f.overrideLogic.size();return t<7?f.resources[t][id].length:0;}
  static bool read(void *p,uint8_t t,uint8_t id,uint32_t off,uint8_t *data,uint16_t n){Fixture &f=*(Fixture*)p;
    f.reads++;if(n>f.maxRead)f.maxRead=n;uint32_t length=size(p,t,id);if(off>length||n>length-off)return false;
    const uint8_t *src=t==0&&id==0&&!f.overrideLogic.empty()?f.overrideLogic.data():f.bytes.data()+f.resources[t][id].offset;
    memcpy(data,src+off,n);return true;}
  static bool picture(void *p,uint8_t id,bool){Fixture &f=*(Fixture*)p;f.pictures++;f.lastPicture=id;return size(p,1,id)!=0;}
  static bool cel(void *p,uint8_t id,uint8_t loop,uint8_t cel,mpe4::CelInfo *out){
    uint8_t h[5],b[3];if(!read(p,2,id,0,h,5)||loop>=h[2])return false;
    if(!read(p,2,id,5+loop*2,b,2))return false;uint16_t l=u16(b);
    if(!read(p,2,id,l,b,1))return false;uint8_t count=b[0];if(cel>=count)return false;
    if(!read(p,2,id,l+1+cel*2,b,2))return false;uint16_t c=l+u16(b);
    if(!read(p,2,id,c,b,3))return false;*out={b[0],b[1],h[2],count};return true;}
  static bool add(void *p,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t,uint8_t){((Fixture*)p)->additions++;return true;}
  static uint8_t pri(void *p,uint8_t x,uint8_t){Fixture &f=*(Fixture*)p;return x==f.blockedColumn?0:f.splitPriority&&x>=80?2:f.priority;}
  static bool sound(void *p,uint8_t id){((Fixture*)p)->sounds++;return size(p,3,id)!=0;}
  static void stop(void*){}
  static bool save(void *p,const mpe4::State *s,size_t n){Fixture &f=*(Fixture*)p;if(!f.allowSave)return false;require(n==sizeof(*s),"save bytes");f.saved=*s;f.savedValid=true;f.saves++;return true;}
  static bool restore(void *p,mpe4::State *s,size_t n){Fixture &f=*(Fixture*)p;if(!f.savedValid)return false;require(n==sizeof(*s),"restore bytes");*s=f.saved;f.restores++;return true;}
  mpe4::Host host(){return {this,size,read,picture,cel,add,pri,sound,stop,save,restore};}
};
static mpe4::Step tick(mpe4::Game &g,uint8_t key=0,uint8_t direction=0,uint8_t scan=0,bool sound=false,unsigned elapsed=6,unsigned budget=8192){
  mpe4::Input input={key,scan,direction,false,sound,(uint16_t)elapsed};auto result=g.tick(input,budget);
  if(result==mpe4::Failed){char error[160];snprintf(error,sizeof(error),"core failure %u logic%u opcode%u ip%u room%u",g.state.error,g.state.errorLogic,g.state.errorOpcode,g.state.errorIp,g.state.vars[0]);throw std::runtime_error(error);}return result;
}
static void settle(mpe4::Game &g,unsigned n=500){for(unsigned i=0;i<n;i++){tick(g,0,0,0,false,6);
  if(g.state.modal!=mpe4::NoModal||!g.state.inScan)return;}throw std::runtime_error("settle budget exhausted");}
static void type(mpe4::Game &g,const char *s){for(;*s;s++)tick(g,*s,0,0,false,1);tick(g,mpe4::Enter,0,0,false,1);}
static std::vector<uint8_t> logicBytes(const std::vector<uint8_t> &code){std::vector<uint8_t>b={(uint8_t)code.size(),(uint8_t)(code.size()>>8)};b.insert(b.end(),code.begin(),code.end());b.insert(b.end(),{0,0,0});return b;}
static std::vector<uint8_t> logic(std::initializer_list<uint8_t> code){return logicBytes(code);}
int main(int argc,char **argv){try{
  require(argc>=2,"usage mpe4-game-native-harness GAME.bin");Fixture f(argv[1]);mpe4::Game g{};
  require(g.start(f.host(),true,1234),"start");settle(g);
  require(g.state.vars[0]==69&&g.state.modal==mpe4::StringInput,"authentic startup must reach login");
  require(g.state.vars[7]==202&&g.state.vars[10]==2&&g.state.bindingCount>=20,"Logic104 startup");
  require(g.state.modalMaximum==18&&g.state.modalRow==16,"authored login input");
  require(!g.state.graphics,"login text mode");type(g,"Roger");
  for(unsigned i=0;i<1000&&(g.state.vars[0]!=2||g.state.inScan||g.state.modal);i++){
    tick(g,g.state.modal==mpe4::Message?mpe4::Enter:0,0,0,true);}
  require(g.state.vars[0]==2&&strcmp(g.state.strings[1],"Roger")==0,"login must retain name and enter2");
  require(g.state.pictureVisible&&(g.state.objects[0].flags&mpe4::Drawn),"Room2 scene and ego");
  for(unsigned i=0;i<1000&&!g.state.playerControl;i++)tick(g,g.state.modal==mpe4::Message?mpe4::Enter:0,0,0,true);
  require(g.state.playerControl,"Room2 authored entrance completes");
  unsigned firstX=g.state.objects[0].x;
  tick(g,mpe4::Right);tick(g);require(g.state.objects[0].x>firstX,"keyboard direction survives zero joystick");
  tick(g,0,3);tick(g,0,0);require(g.state.objects[0].direction==0,"joystick release stops motion");
  type(g,"look");settle(g);require(g.state.modal==mpe4::Message,"authored LOOK message");tick(g,mpe4::Enter);
  unsigned c64MenuChecks=0;
  // Real terminal function events carry a physical C64 key as well as the
  // legacy IBM scan. Labels and shortcuts must identify the same controller.
  const uint8_t functions[][3]={{0,59,2},{1,60,16},{3,62,22},{4,63,3},{5,64,5},{6,65,7},{7,66,29}};
  for(const auto &key:functions){mpe4::Game keys=g;keys.state.inScan=false;keys.state.firstScan=false;keys.state.scanTicks=0;
    memset(keys.state.controllers,0,sizeof(keys.state.controllers));
    tick(keys,mpe4::F1+key[0],0,key[1],false,0);
    require((keys.state.controllers[key[2]>>3]&(1u<<(key[2]&7)))!=0,"C64 function routes to the displayed source controller");c64MenuChecks++;}
  const uint8_t commodore[][2]={{61,9},{62,4},{64,6},{66,8},{44,1}};
  for(const auto &key:commodore){mpe4::Game keys=g;keys.state.inScan=false;keys.state.firstScan=false;keys.state.scanTicks=0;
    memset(keys.state.controllers,0,sizeof(keys.state.controllers));tick(keys,0,0,key[0],false,0);
    require((keys.state.controllers[key[1]>>3]&(1u<<(key[1]&7)))!=0,"Commodore function/letter keeps the original game action");c64MenuChecks++;}
  {mpe4::Game keys=g;keys.state.inScan=false;keys.state.firstScan=false;keys.state.scanTicks=0;
    strcpy(keys.state.previousInput,"take cartridge");tick(keys,mpe4::F1+2,0,61,false,0);
    require(!strcmp(keys.state.input,"take cartridge")&&keys.state.inputLength==14,"physical F3 repeats the parser line");c64MenuChecks++;
    memset(keys.state.controllers,0,sizeof(keys.state.controllers));tick(keys,9,0,23,false,0);
    require((keys.state.controllers[1]&4)!=0,"CTRL-I reaches the original TAB inventory binding");c64MenuChecks++;}
  {mpe4::Game menu=g;tick(menu,mpe4::Escape);settle(menu);require(menu.state.modal==mpe4::Menu,"C64 menu opens");
    tick(menu,mpe4::Right);std::string text((const char*)menu.state.text,1000);
    require(text.find("Restore (F6)")!=std::string::npos&&text.find("Restart (F7)")!=std::string::npos&&
      text.find("Quit (C=Z)")!=std::string::npos&&text.find("F9")==std::string::npos&&text.find("Alt")==std::string::npos,
      "File menu shows available C64 keys");c64MenuChecks++;
    // Native05 saves contain the old PC menu strings. The unchanged State ABI
    // remains readable and the renderer repairs their hints on every draw.
    strcpy(menu.state.menuItems[3].text,"Restore  <F7>");tick(menu,mpe4::Down);
    text.assign((const char*)menu.state.text,1000);
    require(text.find("Restore (F6)")!=std::string::npos&&text.find("<F7>")==std::string::npos,"old save menu hints normalize without migration");c64MenuChecks++;
    mpe4::Input pointer={};pointer.pointerEvent=true;pointer.pointerX=41;pointer.pointerY=25;
    require(menu.tick(pointer)!=mpe4::Failed&&menu.state.menuSelection==3,"pointer hover uses the displayed C64 menu width");
    pointer.pointerY=49;require(menu.tick(pointer)!=mpe4::Failed&&menu.state.menuSelection==6,"idle menu keeps accepting pointer movement");c64MenuChecks++;
    const auto clickedSave=f.saves;pointer.pointerY=17;pointer.pointerButtons=1;
    require(menu.tick(pointer)!=mpe4::Failed,"mouse Save selection executes");
    for(unsigned i=0;i<1000&&f.saves==clickedSave;i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    require(f.saves==clickedSave+1,"mouse File/Save emits the original source action");c64MenuChecks++;
    for(unsigned i=0;i<1000&&(menu.state.modal||menu.state.inScan);i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    const auto saved=f.saves;
    tick(menu,mpe4::F1+4,0,63);for(unsigned i=0;i<1000&&f.saves==saved;i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    require(f.saves==saved+1,"physical C64 F5 performs source save");
    for(unsigned i=0;i<1000&&(menu.state.modal||menu.state.inScan);i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    const auto restored=f.restores;tick(menu,mpe4::F1+5,0,64);
    for(unsigned i=0;i<1000&&f.restores==restored;i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    require(f.restores==restored+1,"physical C64 F6 performs source restore");c64MenuChecks++;
    for(unsigned i=0;i<1000&&(menu.state.modal||menu.state.inScan);i++)tick(menu,menu.state.modal?mpe4::Enter:0);
    tick(menu,mpe4::F1+6,0,65);settle(menu);require(menu.state.modal==mpe4::Restart,"physical C64 F7 opens source restart confirmation");
    tick(menu,mpe4::Escape);require(menu.state.running&&menu.state.vars[0]==2,"C64 restart cancellation retains the running game");c64MenuChecks++;
  }
  for(unsigned phase=0;phase<12;phase++){
    mpe4::Game menu=g;for(const char *p="look at the ship";*p;p++)tick(menu,*p,0,0,false,1);
    for(unsigned i=0;i<phase;i++)tick(menu,0,0,0,false,1,1);
    tick(menu,mpe4::Escape,0,0,false,1,1);
    for(unsigned i=0;i<1000&&menu.state.modal!=mpe4::Menu;i++)tick(menu,menu.state.modal==mpe4::Message?mpe4::Enter:0,0,0,false,1);
    require(menu.state.modal==mpe4::Menu,"Escape after LOOK and partial input survives scan phases");
    require(!strcmp(menu.state.input,"look at the ship"),"opening menu preserves partial command");
  }
  // Parser uses source phrases/synonyms, unknown-word reporting and filler ids.
  require(g.parse("look at the ship"),"phrase parse");require(g.state.wordCount>=2,"significant parsed words");
  require(g.parse("zzyzzx"),"unknown parse");require(g.state.vars[9]==1,"first unknown word");
  require(g.parse("look zzyzzx ship")&&g.state.wordCount==2&&g.state.vars[9]==2,"parser stops at first unknown");
  char expanded[768];require(g.message(0,15,expanded,sizeof(expanded)),"nested message");
  require(strstr(expanded,"Roger")&&!strstr(expanded,"%"),"message references and string substitutions");
  // Input/controller menus are authored by Logic0/104, not hardcoded by room.
  tick(g,0,0,63);settle(g); // F5 save path, may show an authored confirmation.
  for(unsigned i=0;i<20&&g.state.modal;i++)tick(g,mpe4::Enter);
  tick(g,mpe4::Escape);settle(g);require(g.state.modal==mpe4::Menu,"authored Escape menu");
  const unsigned menuCount=g.state.menuCount,menuItemCount=g.state.menuItemCount;
  tick(g,mpe4::Right);require(g.state.menuColumn==1,"menu navigation");tick(g,mpe4::Escape);
  type(g,"restart game");settle(g);require(g.state.modal==mpe4::Restart,"restart confirmation");
  tick(g,mpe4::Escape);require(g.state.vars[0]==2&&!strcmp(g.state.strings[1],"Roger"),"cancel restart preserves game");
  type(g,"restart game");settle(g);tick(g,mpe4::Enter);
  for(unsigned i=0;i<1000&&(!g.state.playerControl||g.state.inScan||g.state.modal);i++)
    tick(g,g.state.modal==mpe4::Message?mpe4::Enter:0,0,0,true);
  require(g.state.vars[0]==2&&g.state.playerControl&&!strcmp(g.state.strings[1],"Roger"),"authored restart goes directly to Room2 with name retained");
  require(g.state.menuCount==menuCount&&g.state.menuItemCount==menuItemCount,"restart retains authored menus");
  tick(g,mpe4::Escape);settle(g);require(g.state.modal==mpe4::Menu,"menu available after restart");tick(g,mpe4::Escape);
  // Focused isolated bytecode exercises use the same interpreter and callbacks.
  Fixture unit(argv[1]);mpe4::Game u{};
  unsigned bindingChecks=0;
  {
    require(offsetof(mpe4::State,overflowBindings)==9528&&sizeof(mpe4::State)==9624,"legacy save prefix and bounded appended bindings");bindingChecks++;
    std::vector<uint8_t> code;
    for(unsigned i=0;i<mpe4::MaxBindings;i++)code.insert(code.end(),{121,uint8_t(32+i),0,uint8_t(64+i)});
    code.push_back(0);unit.overrideLogic=logicBytes(code);
    require(u.start(unit.host(),false),"64 authored binding init");settle(u);
    require(u.state.bindingCount==64&&u.state.bindings[31].controller==95&&
      u.state.overflowBindings[0].controller==96&&u.state.overflowBindings[31].controller==127,"all authored keys retained across legacy boundary");bindingChecks++;
    for(unsigned i=0;i<mpe4::MaxBindings;i++){
      u.state.inScan=false;u.state.firstScan=false;u.state.scanTicks=0;memset(u.state.controllers,0,sizeof(u.state.controllers));
      tick(u,uint8_t(32+i),0,30,false,0);
      require((u.state.controllers[(64+i)>>3]&(1u<<((64+i)&7)))!=0,"every stored key reaches its authored controller");bindingChecks++;
    }
    unit.overrideLogic=logic({121,95,0,250,0});tick(u);settle(u);
    require(u.state.bindingCount==64&&u.state.overflowBindings[31].controller==250,"replacing a key in full overflow table does not consume another slot");bindingChecks++;
    unit.overrideLogic=logic({121,96,0,251,0});mpe4::Input none={};none.elapsed60Hz=6;
    require(u.tick(none)==mpe4::Failed&&u.state.error==mpe4::ResourceBounds&&u.state.errorOpcode==121,"65th distinct key reports bounds instead of silently dropping an action");bindingChecks++;
    unit.overrideLogic=logic({121,1,1,20,121,1,2,21,121,1,3,22,121,1,4,23,121,1,0,24,121,0,32,25,121,'d',0,26,0});
    require(u.start(unit.host(),false),"16-bit key init");settle(u);
    require(u.state.bindingCount==7,"platform menu triggers remain distinct 16-bit bindings");bindingChecks++;
    const uint8_t keys[][3]={{1,30,24},{'d',32,26},{0,32,25}};
    for(const auto &key:keys){u.state.inScan=false;u.state.firstScan=false;u.state.scanTicks=0;
      memset(u.state.controllers,0,sizeof(u.state.controllers));tick(u,key[0],0,key[1],false,0);
      for(unsigned controller=0;controller<256;controller++)require(bool(u.state.controllers[controller>>3]&(1u<<(controller&7)))==(controller==key[2]),"ASCII, Alt scan and platform trigger codes do not alias");bindingChecks++;
    }
  }
  unit.overrideLogic=logic({33,5,41,5,141,37,5,49,119,35,5,70,5,3,201,3,86,5,201,0});
  require(u.start(unit.host(),false),"actor reuse init");settle(u);require(u.state.objects[5].direction==3,"first actor is moving");
  unit.overrideLogic=logic({34,33,5,67,5,37,5,49,119,35,5,70,5,0});tick(u);
  require(u.state.objects[5].x==49&&!u.state.objects[5].direction,"animate.obj resets stale direction on reused stationary trunk");
  unit.overrideLogic=logic({33,0,41,0,1,37,0,10,100,35,0,26,77,0,0});
  require(u.start(unit.host(),false),"stop-motion init");settle(u);
  require(!u.state.playerControl&&!u.state.objects[0].direction&&(u.state.objects[0].flags&mpe4::Motion),"AGI2 stop.motion retains normal motion capability");
  unit.overrideLogic=logic({132,0});tick(u);unsigned stoppedX=u.state.objects[0].x;tick(u,0,3);
  require(u.state.playerControl&&u.state.objects[0].x>stoppedX,"player.control resumes stopped ego without start.motion");
  unit.overrideLogic=logic({130,0,255,200,0});require(u.start(unit.host(),false,1234),"random restore init");settle(u);
  Fixture::save(&unit,&u.state,sizeof(u.state));const unsigned savedRoll=u.state.vars[200];tick(u);
  const uint32_t liveRandom=u.state.random;require(liveRandom!=unit.saved.random,"random source advanced");
  unit.overrideLogic=logic({126,0});tick(u);
  require(u.state.vars[200]==savedRoll&&u.state.random==liveRandom,"restore returns game data while retaining live random source");
  unit.overrideLogic=logic({130,0,255,200,0});tick(u);require(u.state.random!=liveRandom,"restored random hazard receives fresh roll");
  unit.overrideLogic=logic({1,200,0});require(u.start(unit.host(),false),"typing timing init");settle(u);
  const unsigned beforeTyping=u.state.scans;tick(u,'l');for(unsigned i=0;i<10;i++)tick(u);
  require(u.state.inputLength==1&&u.state.scans>beforeTyping&&u.state.vars[11]>=1,"ordinary command editing keeps room scans and game clock running");
  unit.overrideLogic.clear();
  require(u.start(unit.host(),false),"message census init");unsigned messageCount=0;
  for(unsigned id=0;id<256;id++)if(Fixture::size(&unit,0,id)){
    uint8_t h[3];require(Fixture::read(&unit,0,id,0,h,2),"logic size");unsigned base=2+u16(h);
    require(Fixture::read(&unit,0,id,base,h,3),"message table");
    for(unsigned message=1;message<=h[0];message++){char out[2048];
      require(u.message(id,message,out,sizeof(out)),"all original message expansions must be bounded");messageCount++;}}
  unit.overrideLogic=logic({3,200,254,1,200,1,200,2,201,3,202,7,9,202,200,10,203,202,11,202,11,0});
  require(u.start(unit.host(),false),"unit init");settle(u);
  require(u.state.vars[200]==255&&u.state.vars[201]==0&&u.state.vars[7]==11&&u.state.vars[203]==255,"arithmetic indirect saturation");
  unit.overrideLogic=logic({3,200,9,125,3,200,8,0});require(u.start(unit.host(),false),"save init");settle(u);
  require(unit.savedValid&&unit.saved.vars[200]==9&&u.state.vars[200]==8,"save immutable state callback");
  unit.overrideLogic=logic({3,200,9,125,3,200,8,0});unit.allowSave=false;
  require(u.start(unit.host(),false),"unavailable save init");settle(u);
  require(u.state.modal==mpe4::Message&&u.state.error==mpe4::Okay&&u.state.vars[200]==9,"save failure is resumable modal");
  tick(u,mpe4::Enter);require(u.state.vars[200]==8,"failed save resumes exact instruction");unit.allowSave=true;
  unit.overrideLogic=logic({3,200,9,126,3,200,8,0});unit.savedValid=false;
  require(u.start(unit.host(),false),"unavailable restore init");settle(u);
  require(u.state.modal==mpe4::Message&&u.state.error==mpe4::Okay,"restore failure is resumable modal");
  tick(u,mpe4::Enter);require(u.state.vars[200]==8,"failed restore resumes exact instruction");
  unit.overrideLogic=logic({3,124,37,129,224,0});require(u.start(unit.host(),false),"view description init");settle(u);
  require(u.state.showObject&&u.state.modal==mpe4::Message,"object inspection modal");
  std::string display((char*)u.state.text,1000);require(display.find("37")!=std::string::npos,"VIEW currency description formats variable");
  unit.overrideLogic=logic({3,200,1,0xff,1,200,1,0xff,3,0,3,201,42,0});require(u.start(unit.host(),false),"condition init");settle(u);
  require(u.state.vars[201]==42,"raw relative IF");
  unit.overrideLogic=logic({0xff,15,0,1,0xff,3,0,3,201,42,0});require(u.start(unit.host(),false),"ASCII string comparison init");
  strcpy(u.state.strings[0],"Rog-er 42!");strcpy(u.state.strings[1],"roger42");settle(u);
  require(u.state.vars[201]==42,"compare.strings ignores punctuation and ASCII case");
  unit.overrideLogic=logic({33,4,41,4,32,47,4,4,37,4,22,142,35,4,70,4,3,55,3,76,4,55,75,4,34,0});
  require(u.start(unit.host(),false),"reverse-loop init");settle(u);
  require(u.state.objects[4].cel==4&&u.state.objects[4].cycleTime==3&&!u.flag(34),"reverse.loop75 and cycle.time76 preserve separate meanings");
  unit.overrideLogic=logic({0});tick(u);tick(u);require(u.state.objects[4].cel==4,"cycle.time delays reverse animation");
  tick(u);require(u.state.objects[4].cel==3,"reverse animation decrements cel after configured interval");
  for(unsigned i=0;i<12&&!u.flag(34);i++)tick(u);
  require(u.flag(34)&&u.state.objects[4].cel==0&&!(u.state.objects[4].flags&mpe4::Cycling),"reverse.loop completes at first cel and sets authored flag");
  unit.overrideLogic=logic({0xff,1,0,0,0xff,6,0,12,0,12,3,18,2,
    0xff,7,3,0xff,3,0,3,200,1,0xff,7,0,0xff,3,0,3,201,1,0});
  require(u.start(unit.host(),false),"new-room controls init");
  strcpy(u.state.input,"throw water");u.state.inputLength=11;settle(u);
  require(u.state.vars[0]==2&&!u.state.vars[200]&&!u.state.vars[201],"new.room clears old trigger and water before incoming logic");
  require(u.state.inputLength==11&&!strcmp(u.state.input,"throw water"),"new.room retains editable pretyped command");
  unit.splitPriority=true;
  unit.overrideLogic=logic({33,0,41,0,1,37,0,100,100,35,0,26,
    0xff,7,3,0xff,3,0,3,200,1,37,0,10,100,
    0xff,7,3,0xff,3,0,3,201,1,0});
  require(u.start(unit.host(),false),"repositioned controls init");settle(u);
  require(u.state.vars[200]==1&&!u.state.vars[201]&&!u.flag(3),"draw and position refresh trigger before later instructions in same scan");
  unit.splitPriority=false;unit.priority=3;tick(u);
  require(u.flag(0)&&!u.flag(3),"stationary ego refreshes water and trigger");unit.priority=4;
  unit.blockedColumn=17;unit.overrideLogic=logic({33,0,41,0,1,37,0,10,100,54,0,15,35,0,26,3,201,3,86,0,201,0});
  require(u.start(unit.host(),false),"fixed priority15 init");settle(u);
  require(u.state.objects[0].x==11&&!u.flag(0)&&!u.flag(3),"fixed priority15 bypasses picture control without water or trigger flags");
  unit.overrideLogic=logic({56,0,0});tick(u);
  require(u.state.objects[0].x==10,"release.priority restores control collision and repairs illegal current baseline");
  unit.blockedColumn=18;
  unit.overrideLogic=logic({33,0,41,0,1,37,0,10,100,35,0,26,3,55,12,79,0,55,3,201,3,86,0,201,0});
  require(u.start(unit.host(),false),"step endpoint init");settle(u);
  require(u.state.objects[0].x==22,"large AGI step checks endpoint rather than intermediate control pixels");
  unit.overrideLogic=logic({33,0,41,0,1,37,0,10,100,35,0,26,3,55,3,79,0,55,3,201,3,86,0,201,0});
  require(u.start(unit.host(),false),"blocked step init");settle(u);
  require(u.state.objects[0].x==10,"blocked endpoint rejects the whole step");
  unit.overrideLogic=logic({33,0,41,0,1,37,0,10,100,35,0,26,3,201,3,86,0,201,147,0,13,100,0});
  require(u.start(unit.host(),false),"reposition repair init");settle(u);
  require(u.state.objects[0].x==11&&u.state.objects[0].y==99,"reposition repairs in AGI expanding-square order and skips one movement update");
  unit.blockedColumn=-1;
  unit.overrideLogic=logic({0xfe,0xfd,0xff});require(u.start(unit.host(),false),"loop init");
  require(tick(u,0,0,0,false,0,16)==mpe4::Yielded&&u.state.inScan,"bounded instruction slice yields");
  // Unsupported opcodes are explicit, and neither missing resources nor bounds
  // errors can be mistaken for a successfully completed game scan.
  unit.overrideLogic=logic({0xf0,0});require(u.start(unit.host(),false),"bad opcode init");
  mpe4::Input none{};require(u.tick(none)==mpe4::Failed&&u.state.error==mpe4::UnsupportedAction,"unsupported opcode");
  printf("{\"passed\":true,\"stateBytes\":%u,\"room\":%u,\"scans\":%lu,\"reads\":%u,\"maxReadBytes\":%u,\"pictures\":%u,\"soundStarts\":%u,\"saves\":%u,\"messagesChecked\":%u,\"c64MenuChecks\":%u,\"bindingChecks\":%u}\n",
    unsigned(sizeof(mpe4::State)),g.state.vars[0],(unsigned long)g.state.scans,f.reads,f.maxRead,f.pictures,f.sounds,f.saves,messageCount,c64MenuChecks,bindingChecks);
  return 0;
}catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}}

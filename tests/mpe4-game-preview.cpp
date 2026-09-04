#include "mpe4_game.h"
#include "mpe4_render.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static uint16_t u16(const uint8_t *p){return p[0]|uint16_t(p[1])<<8;}
static uint32_t u32(const uint8_t *p){return u16(p)|uint32_t(u16(p+2))<<16;}
struct Preview {
  struct Entry{uint32_t offset=0,length=0;};Entry entries[8][256];
  std::vector<uint8_t> package;
  mpe4::Game game{};mpe4::Renderer renderer;
  uint8_t visual[13440],priority[13440],frame[10000],font[1024];
  static uint32_t size(void *p,uint8_t type,uint8_t id){return type<8?static_cast<Preview*>(p)->entries[type][id].length:0;}
  static bool read(void *p,uint8_t type,uint8_t id,uint32_t off,uint8_t *out,uint16_t n){
    auto &a=*static_cast<Preview*>(p);if(type>=8)return false;const Entry &e=a.entries[type][id];
    if(off>e.length||n>e.length-off)return false;std::memcpy(out,a.package.data()+e.offset+off,n);return true;}
  static bool picture(void *p,uint8_t id,bool overlay){return static_cast<Preview*>(p)->renderer.drawPicture(id,overlay);}
  static bool cel(void *p,uint8_t view,uint8_t loop,uint8_t c,mpe4::CelInfo *info){return static_cast<Preview*>(p)->renderer.viewCelInfo(view,loop,c,info);}
  static bool add(void *p,uint8_t v,uint8_t l,uint8_t c,uint8_t x,uint8_t y,uint8_t pri,uint8_t margin){auto &a=*static_cast<Preview*>(p);a.renderer.priorityBase=a.game.state.priorityBase;return a.renderer.addToPicture(v,l,c,x,y,pri,margin);}
  static uint8_t pri(void *p,uint8_t x,uint8_t y){return static_cast<Preview*>(p)->renderer.priorityAt(x,y);}
  static bool sound(void *p,uint8_t id){return size(p,3,id)>0;}
  static void stop(void*){}
  static bool save(void*,uint8_t,const mpe4::State*,size_t){return false;}
  static bool restore(void*,uint8_t,mpe4::State*,size_t){return false;}
  void start(const std::string &file){std::ifstream f(file,std::ios::binary);package.assign(std::istreambuf_iterator<char>(f),{});
    if(package.size()<64||memcmp(package.data(),"M4G2",4))throw std::runtime_error("invalid package");
    unsigned index=u32(package.data()+12),count=u16(package.data()+16);
    for(unsigned i=0;i<count;i++){const uint8_t *e=package.data()+index+i*16;entries[e[0]][e[1]]={u32(e+4),u32(e+8)};}
    if(!read(this,6,0,0,font,1024))throw std::runtime_error("font missing");
    mpe4::Host h={this,size,read,picture,cel,add,pri,sound,stop,save,restore};
    if(!renderer.init(h,visual,priority,frame,font)||!game.start(h,true,1234))throw std::runtime_error("start failed");
  }
  void tick(uint8_t key=0,uint16_t elapsed=6,uint8_t scan=0){mpe4::Input input{key,scan,0,false,true,elapsed};
    if(game.tick(input)==mpe4::Failed)throw std::runtime_error("core failure "+std::to_string(game.state.error)+" logic "+std::to_string(game.state.errorLogic)+" opcode "+std::to_string(game.state.errorOpcode));}
  void settle(){for(unsigned i=0;i<1000;i++){tick();if(game.state.modal||!game.state.inScan)return;}throw std::runtime_error("settle timeout");}
  void type(const char *s,bool enter=true){for(;*s;s++)tick(*s,1);if(enter)tick(mpe4::Enter,1);}
  void snapshot(const std::string &name){if(!renderer.render(game.state,frame))throw std::runtime_error("render failed");
    std::ofstream f(name+".frame",std::ios::binary);f.write(reinterpret_cast<char*>(frame),10000);
    std::ofstream text(name+".txt");for(unsigned y=0;y<25;y++){for(unsigned x=0;x<40;x++){uint8_t c=game.state.text[y*40+x];text.put(c?char(c):' ');}text<<'\n';}
  }
};
int main(int argc,char **argv){try{
  if(argc!=3)throw std::runtime_error("package and output directory required");Preview p;p.start(argv[1]);p.settle();
  if(p.game.state.vars[0]!=69||p.game.state.modal!=mpe4::StringInput)throw std::runtime_error("login missing");
  p.snapshot(std::string(argv[2])+"/login");p.type("Roger");
  for(unsigned i=0;i<2000&&(p.game.state.vars[0]!=2||!p.game.state.playerControl||p.game.state.inScan||p.game.state.modal);i++)
    p.tick(p.game.state.modal==mpe4::Message?mpe4::Enter:0);
  if(p.game.state.vars[0]!=2||!p.game.state.playerControl)throw std::runtime_error("Room2 entrance missing");
  p.snapshot(std::string(argv[2])+"/room2");
  p.tick(mpe4::Escape);p.settle();
  if(p.game.state.modal!=mpe4::Menu)throw std::runtime_error("keyboard menu missing");
  p.snapshot(std::string(argv[2])+"/menu");p.tick(mpe4::Escape);p.settle();
  p.type("look");p.settle();
  if(p.game.state.modal!=mpe4::Message)throw std::runtime_error("LOOK message missing");
  p.snapshot(std::string(argv[2])+"/look");p.tick(mpe4::Enter);p.type("look at the ship",false);
  p.snapshot(std::string(argv[2])+"/parser");
  std::cout<<"{\"room\":"<<unsigned(p.game.state.vars[0])<<",\"egoX\":"<<unsigned(p.game.state.objects[0].x)
    <<",\"egoY\":"<<unsigned(p.game.state.objects[0].y)<<",\"snapshots\":5,\"actualCoreAndRenderer\":true}"<<std::endl;return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<std::endl;return 1;}}

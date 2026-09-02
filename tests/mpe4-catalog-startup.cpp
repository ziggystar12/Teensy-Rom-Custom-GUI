#include "mpe4_session.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>
static void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
struct Fixture {
  std::vector<uint8_t> package;
  static bool read(void *context,uint32_t at,uint8_t *to,uint16_t n){
    const auto &p=static_cast<Fixture*>(context)->package;
    if(at>p.size()||n>p.size()-at)return false;
    memcpy(to,p.data()+at,n);return true;
  }
};
int main(int argc,char **argv){try{
  check(argc==2,"package required");Fixture f;std::ifstream file(argv[1],std::ios::binary);
  f.package.assign(std::istreambuf_iterator<char>(file),{});
  mpe4::Session session{};
  check(session.start(Fixture::read,&f,0,f.package.size(),{}),"Session could not open package");
  unsigned changed=0,splitFrames=0,firstSplitCells=0;bool sawParser=false;
  uint8_t shown[10000]{};
  for(unsigned frame=0;frame<900;frame++){
    auto &s=session.game.state;mpe4::Input in{};in.elapsed60Hz=1;
    // Advance introductory messages, but do not guess copy-protection answers
    // or modify game variables to bypass the original scripts.
    if(frame%30==29&&(s.modal==mpe4::Message||s.modal==mpe4::Pause))in.key=mpe4::Enter;
    if(frame>120&&s.graphics&&s.inputEnabled&&!s.modal&&!s.inScan&&!s.inputLength&&!sawParser){in.key='l';sawParser=true;}
    if(!session.prepareFrame(in)) {
      char detail[180];snprintf(detail,sizeof(detail),"Session%u core%u logic%u opcode%u ip%u room%u",session.error,s.error,s.errorLogic,s.errorOpcode,s.errorIp,s.vars[0]);
      throw std::runtime_error(detail);
    }
    uint8_t records[228];bool first=false;unsigned count,total=0;int prior=-1;
    while((count=session.cells(records,19,first))!=0)for(unsigned i=0;i<count;i++) {
      const uint8_t *r=records+i*12;unsigned cell=r[0]|unsigned(r[1])<<8;
      check(cell<1000&&int(cell)>prior,"invalid CELL order");prior=cell;total++;
      memcpy(shown+cell*8,r+2,8);shown[8000+cell]=r[10];shown[9000+cell]=r[11];
    }
    check(!memcmp(shown,session.next,10000),"CELL stream differs from rendered frame");
    if(session.parserSplit){if(!splitFrames)firstSplitCells=total;splitFrames++;}
    changed+=total;session.acknowledgeFrame();
  }
  const auto &s=session.game.state;
  check(s.scans>0||(s.inScan&&s.callDepth&&s.modal),"no original logic or pending authored prompt");
  printf("{\"passed\":true,\"frames\":900,\"room\":%u,\"modal\":%u,\"scans\":%lu,\"changedCells\":%u,\"splitFrames\":%u,\"firstSplitCells\":%u,\"bindings\":%u}\n",
    s.vars[0],s.modal,(unsigned long)s.scans,changed,splitFrames,firstSplitCells,s.bindingCount);
  return 0;
}catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}}

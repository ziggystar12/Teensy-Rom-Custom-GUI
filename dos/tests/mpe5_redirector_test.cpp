#include "../../engine/native-dos/mpe5_redirector.h"
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
struct Node { std::string data; uint8_t attr=0; };
struct Fixture {
  static constexpr uint32_t Sda=0x2000,Table=0x4000,Cds=Table+3*0x58,Sft=0x7000,Dta=0x8000;
  std::array<uint8_t,mpe5::ConventionalRamBytes> memory{};
  std::map<std::string,Node> files{{"/",{"",0x10}},{"/GAME",{"",0x10}},
      {"/GAME/TEST.EXE",{"MZ executable",0x20}},{"/README.TXT",{"hello\r\n",0x20}}};
  std::array<std::string,16> handles{};
  mpe5::Redirector redirector;
  unsigned writes=0,closes=0,flushes=0;
  uint16_t writeError=0;
  static Fixture &self(void *p){return *static_cast<Fixture *>(p);}
  void w16(uint32_t p,uint16_t v){memory[p]=uint8_t(v);memory[p+1]=uint8_t(v>>8);}
  void w32(uint32_t p,uint32_t v){w16(p,uint16_t(v));w16(p+2,uint16_t(v>>16));}
  uint32_t r32(uint32_t p)const{return memory[p]|uint32_t(memory[p+1])<<8|uint32_t(memory[p+2])<<16|uint32_t(memory[p+3])<<24;}
  void pointer(uint32_t p,uint32_t value){w16(p,uint16_t(value&15));w16(p+2,uint16_t(value>>4));}
  void text(uint32_t p,const std::string &s){std::memcpy(memory.data()+p,s.c_str(),s.size()+1);}
  static uint16_t info(void *ctx,const char *path,mpe5::RedirectorFileInfo &out){
    auto &f=self(ctx);auto it=f.files.find(path);if(it==f.files.end())return 2;
    const std::string name=std::string(path).substr(std::string(path).find_last_of('/')+1);
    if(name.size()>12)return 13;
    out={};std::memcpy(out.name,name.c_str(),name.size()+1);out.size=uint32_t(it->second.data.size());
    out.attributes=it->second.attr;out.date=0x5d23;out.time=0x7820;return 0;
  }
  Fixture(){
    mpe5::RedirectorMemory access{this,
      [](void *p,uint32_t a,uint8_t *b,uint32_t n){auto &f=self(p);if(a>f.memory.size()||n>f.memory.size()-a)return false;std::memcpy(b,f.memory.data()+a,n);return true;},
      [](void *p,uint32_t a,const uint8_t *b,uint32_t n){auto &f=self(p);if(a>f.memory.size()||n>f.memory.size()-a)return false;std::memcpy(f.memory.data()+a,b,n);return true;}};
    mpe5::RedirectorHost h{};h.context=this;h.stat=info;
    h.enumerate=[](void *p,const char *directory,uint16_t index,mpe5::RedirectorFileInfo &out)->uint16_t{
      auto &f=self(p);const std::string prefix=std::string(directory)=="/"?"/":std::string(directory)+"/";
      for(auto &entry:f.files)if(entry.first.size()>prefix.size()&&entry.first.compare(0,prefix.size(),prefix)==0&&
          entry.first.find('/',prefix.size())==std::string::npos){if(!index--)return info(p,entry.first.c_str(),out);}
      return 18;};
    h.open=[](void *p,uint8_t slot,const char *path,uint16_t,uint16_t action,uint8_t attr,mpe5::RedirectorFileInfo &out,uint16_t &result)->uint16_t{
      auto &f=self(p);auto it=f.files.find(path);
      if(it==f.files.end()){if(!(action&0x10))return 2;f.files[path]={"",uint8_t(attr|0x20)};result=2;}
      else {if(!(action&3))return 80;if((action&3)==2){it->second.data.clear();result=3;}else result=1;}
      f.handles[slot]=path;return info(p,path,out);};
    h.close=[](void *p,uint8_t slot)->uint16_t{auto &f=self(p);++f.closes;f.handles[slot].clear();return 0;};
    h.read=[](void *p,uint8_t slot,uint32_t offset,uint8_t *out,uint16_t requested,uint16_t &actual)->uint16_t{
      auto &f=self(p);const std::string &data=f.files.at(f.handles[slot]).data;
      actual=uint16_t(offset>=data.size()?0:std::min<size_t>(requested,data.size()-offset));
      if(actual)std::memcpy(out,data.data()+offset,actual);return 0;};
    h.write=[](void *p,uint8_t slot,uint32_t offset,const uint8_t *in,uint16_t requested,uint16_t &actual)->uint16_t{
      auto &f=self(p);++f.writes;if(f.writeError){actual=0;return f.writeError;}
      auto &data=f.files.at(f.handles[slot]).data;data.resize(std::max<size_t>(data.size(),size_t(offset)+requested));
      std::memcpy(&data[offset],in,requested);actual=requested;return 0;};
    h.truncate=[](void *p,uint8_t slot,uint32_t n)->uint16_t{auto &f=self(p);f.files.at(f.handles[slot]).data.resize(n);return 0;};
    h.flush=[](void *p,uint8_t)->uint16_t{++self(p).flushes;return 0;};
    h.setTime=[](void *,uint8_t,uint16_t,uint16_t)->uint16_t{return 0;};
    h.setAttributes=[](void *p,const char *path,uint8_t attr)->uint16_t{auto &f=self(p);auto it=f.files.find(path);if(it==f.files.end())return 2;it->second.attr=attr;return 0;};
    h.mkdir=[](void *p,const char *path)->uint16_t{auto &f=self(p);if(f.files.count(path))return 5;f.files[path]={"",0x10};return 0;};
    h.rmdir=[](void *p,const char *path)->uint16_t{auto &f=self(p);auto it=f.files.find(path);if(it==f.files.end())return 3;
      for(auto &e:f.files)if(e.first.find(std::string(path)+"/")==0)return 5;f.files.erase(it);return 0;};
    h.remove=[](void *p,const char *path)->uint16_t{auto &f=self(p);auto it=f.files.find(path);if(it==f.files.end())return 2;
      if(it->second.attr&0x11)return 5;f.files.erase(it);return 0;};
    h.rename=[](void *p,const char *from,const char *to)->uint16_t{auto &f=self(p);auto it=f.files.find(from);if(it==f.files.end())return 2;
      if(f.files.count(to))return 5;f.files[to]=it->second;f.files.erase(it);return 0;};
    h.space=[](void *,uint32_t &total,uint32_t &free)->uint16_t{total=262144;free=131072;return 0;};
    redirector.configure(access,h);pointer(0x1016,Table);memory[0x1021]=5;
    pointer(Sda+0x282,Cds);pointer(Sda+0x0c,Dta);w16(Sda+0x10,0x100);
    auto r=registers(3);r.ds=Sda>>4;r.si=0;r.es=0x100;r.bx=0;
    require(redirector.service(0,r)&&!(r.flags&1)&&redirector.installed(),"install failed");
    require(memory[Cds]=='D'&&memory[Cds+0x43]==0x80&&memory[Cds+0x44]==0xc0,"CDS not mounted network/physical");
    require(redirector.service(0,r)&&(r.flags&1),"duplicate TSR installation allowed");
  }
  mpe5::RedirectorRegisters registers(uint16_t ax)const{
    mpe5::RedirectorRegisters r{};r.ax=ax;r.es=Sft>>4;r.di=0;r.ss=0x600;r.sp=0x100;r.flags=0x202;return r;
  }
  mpe5::RedirectorRegisters call(uint8_t fn,uint16_t count=0,uint32_t sft=Sft){auto r=registers(uint16_t(0x1100|fn));r.cx=count;
    r.es=uint16_t(sft>>4);r.di=uint16_t(sft&15);
    require(redirector.service(1,r),"owned call chained");return r;}
  void okay(const mpe5::RedirectorRegisters &r,const char *message){require(!(r.flags&1),message);}
};

void compatibilityReopen(){
  Fixture f;
  constexpr uint32_t second=Fixture::Sft+64;
  f.text(Fixture::Sda+0x9e,"D:\\GACARD.DTA");f.files["/GACARD.DTA"]={"old",0x20};
  f.w16(Fixture::Sft,1);f.w16(0x6106,0);
  f.okay(f.call(0x17),"GRAPHSET create");
  // GRAPHSET keeps the create handle open, then opens write-only compatibility.
  f.w16(second,1);f.memory[Fixture::Sda+0x24e]=1;
  f.okay(f.call(0x16,0,second),"same-process compatibility reopen rejected");
  f.memory[Fixture::Dta]=2;
  f.okay(f.call(9,1,second),"Tandy setting write");
  f.okay(f.call(6,0,second),"close reopened handle");
  f.okay(f.call(0x1d),"process cleanup of original create handle");
  require(f.files["/GACARD.DTA"].data==std::string(1,2),"Tandy setting lost on original close");
  require(f.closes==2,"compatibility reopen leaked a handle");
  for(const auto &h:f.handles)require(h.empty(),"host handle still open after process cleanup");
  f.w16(Fixture::Sft,1);f.memory[Fixture::Sda+0x24e]=0;
  f.okay(f.call(0x16),"reopen persisted setting");
  f.okay(f.call(8,1),"read persisted setting");
  require(f.memory[Fixture::Dta]==2,"persisted Tandy byte mismatch");
  f.okay(f.call(6),"close persisted setting");
}

void sharingModes(){
  // Exercise all access/share pairs and both PSP relationships. Explicit deny
  // rules remain enforced even for the same process; no-inherit is not sharing.
  unsigned cases=0;
  for(unsigned same=0;same<2;++same)for(unsigned a=0;a<15;++a)for(unsigned b=0;b<15;++b){
    Fixture f;constexpr uint32_t second=Fixture::Sft+64;
    const uint8_t firstMode=uint8_t((a/3)*16+a%3),nextMode=uint8_t(0x80+(b/3)*16+b%3);
    f.text(Fixture::Sda+0x9e,"D:\\README.TXT");f.w16(Fixture::Sft,1);
    f.memory[Fixture::Sda+0x24e]=firstMode;f.okay(f.call(0x16),"first sharing open");
    f.w16(Fixture::Sda+0x10,same?0x100:0x101);f.w16(second,1);
    f.memory[Fixture::Sda+0x24e]=nextMode;
    const auto deny=[](unsigned mode){const unsigned masks[]={0,3,2,1,0};return mode/3?masks[mode/3]:mode%3?3u:2u;};
    const unsigned access[]={1,2,3};
    const bool conflict=!(same&&a/3==0&&b/3==0)&&((deny(a)&access[b%3])||(deny(b)&access[a%3]));
    const auto result=f.call(0x16,0,second);
    require(bool(result.flags&1)==conflict&&(!conflict||result.ax==32),"sharing/PSP matrix mismatch");
    if(!conflict)f.okay(f.call(6,0,second),"close second sharing handle");
    f.okay(f.call(6),"close first sharing handle");++cases;
  }
  require(cases==450,"sharing matrix incomplete");
}
}

int main(){try{
  compatibilityReopen();sharingModes();
  Fixture f;
  auto unrelated=f.registers(0x1200);const auto saved=unrelated;
  require(!f.redirector.service(1,unrelated)&&!std::memcmp(&saved,&unrelated,sizeof saved),"unrelated multiplex registers changed");
  f.text(Fixture::Sda+0x9e,"D:\\README.TXT");f.memory[Fixture::Sda+0x24e]=0;
  f.w16(Fixture::Sft,1);f.okay(f.call(0x16),"open existing");
  auto read=f.call(8,100);f.okay(read,"read");require(read.cx==7&&!std::memcmp(f.memory.data()+Fixture::Dta,"hello\r\n",7),"read count/data");
  require(f.call(8,100).cx==0,"EOF read");f.okay(f.call(6),"close read");
  f.text(Fixture::Sda+0x9e,"D:\\SAVE.DAT");f.w16(Fixture::Sft,1);f.w16(0x6106,0);
  f.okay(f.call(0x17),"create save");
  std::string data(2700,'x');f.text(Fixture::Dta,data);auto written=f.call(9,uint16_t(data.size()));f.okay(written,"write save");
  require(written.cx==data.size()&&f.files["/SAVE.DAT"].data==data&&f.writes==3,"chunked write not persistent");
  f.w32(Fixture::Sft+0x15,17);f.okay(f.call(9,0),"truncate");require(f.files["/SAVE.DAT"].data.size()==17,"truncate size");
  f.okay(f.call(7),"commit");require(f.flushes>0,"commit did not flush");
  f.writeError=39;f.text(Fixture::Dta,"FAIL");auto diskfull=f.call(9,4);
  require((diskfull.flags&1)&&diskfull.ax==39&&diskfull.cx==0,"host diskfull swallowed");f.writeError=0;
  f.w16(Fixture::Sft,2);const auto closeCount=f.closes;f.okay(f.call(6),"duplicate close");require(f.closes==closeCount,"duplicate close released shared handle");
  f.okay(f.call(6),"final close");require(f.closes==closeCount+1,"final close leak");
  f.text(Fixture::Sda+0x9e,"D:\\SAVE.DAT");f.text(Fixture::Sda+0x11e,"D:\\RENAMED.DAT");f.okay(f.call(0x11),"rename");
  require(f.files.count("/RENAMED.DAT")&&!f.files.count("/SAVE.DAT"),"rename not reflected");
  f.text(Fixture::Sda+0x9e,"D:\\*.DAT");f.okay(f.call(0x13),"wildcard delete");require(!f.files.count("/RENAMED.DAT"),"delete not reflected");
  f.text(Fixture::Sda+0x9e,"D:\\NEW");f.okay(f.call(3),"mkdir");f.okay(f.call(5),"chdir");require(std::string(reinterpret_cast<char *>(f.memory.data()+Fixture::Cds))=="D:\\NEW","CDS chdir");
  f.text(Fixture::Sda+0x9e,"D:\\NEW");f.okay(f.call(1),"rmdir");
  f.text(Fixture::Sda+0x9e,"D:\\*.*");f.memory[Fixture::Sda+0x24d]=0x10;
  f.okay(f.call(0x1b),"find first");std::vector<std::string> names;
  for(unsigned i=0;i<20;++i){names.emplace_back(reinterpret_cast<char *>(f.memory.data()+Fixture::Sda+0x1b3),11);auto n=f.call(0x1c);if(n.flags&1){require(n.ax==18,"findnext error");break;}}
  require(names.size()==2&&names[0]=="GAME       "&&names[1]=="README  TXT","find data block wrong");
  f.text(Fixture::Sda+0x9e,"D:\\*.*");f.okay(f.call(0x1b),"outer DTA search");
  std::array<uint8_t,21> outerSearch{};
  std::memcpy(outerSearch.data(),f.memory.data()+Fixture::Sda+0x19e,outerSearch.size());
  f.pointer(Fixture::Sda+0x0c,Fixture::Dta+256);
  f.text(Fixture::Sda+0x9e,"D:\\GAME\\*.*");f.okay(f.call(0x1b),"nested search");
  require(f.memory[Fixture::Sda+0x1b3]=='.',"nested dot missing");f.okay(f.call(0x1c),"nested parent");
  require(f.memory[Fixture::Sda+0x1b4]=='.',"nested parent missing");f.okay(f.call(0x1c),"nested EXE");
  require(!std::memcmp(f.memory.data()+Fixture::Sda+0x1b3,"TEST    EXE",11),"nested file missing");
  f.pointer(Fixture::Sda+0x0c,Fixture::Dta);
  std::memcpy(f.memory.data()+Fixture::Sda+0x19e,outerSearch.data(),outerSearch.size());
  f.okay(f.call(0x1c),"outer DTA search was destroyed by nested search");
  require(!std::memcmp(f.memory.data()+Fixture::Sda+0x1b3,"README  TXT",11),"outer DTA cursor changed");
  auto space=f.call(0x0c);f.okay(space,"disk free");require(space.ax==64&&space.bx==4096&&space.cx==512&&space.dx==2048,"disk free geometry");
  f.text(Fixture::Sda+0x9e,"D:\\..\\ESCAPE");require(f.call(3).flags&1,"parent root escape accepted");
  f.text(Fixture::Sda+0x9e,"D:\\README.TXT");f.w16(Fixture::Sda+0x2dd,1);f.w16(Fixture::Sda+0x2df,0);f.w16(Fixture::Sda+0x2e1,0x40);f.w16(Fixture::Sft,1);
  auto extended=f.call(0x2e);f.okay(extended,"extended open");require(extended.cx==1,"extended open action result");
  auto seek=f.registers(0x1121);seek.cx=0xffff;seek.dx=0xfffe;require(f.redirector.service(1,seek)&&!(seek.flags&1)&&seek.ax==5,"seek from EOF");
  f.okay(f.call(6),"close extended");f.redirector.reset();require(!f.redirector.installed(),"reset retained mount");
  std::cout<<"Redirector unit regression passed; object "<<sizeof(mpe5::Redirector)<<" bytes, file saves/close/commit/rename/delete/directories/find/EOF/errors verified\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

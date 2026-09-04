// Real FreeDOS boot and command-level coverage of the resident INT2F hook.
#define main mpe5_original_vm_test_main
#include "mpe5_vm_host_test.cpp"
#undef main
#include <map>
#include <set>
#include "../../engine/native-dos/mpe5_redirector.cpp"
#include "mpe5_redirector_io_probe.h"

struct Folder {
  std::map<std::string,std::vector<uint8_t>> files;
  std::set<std::string> dirs{"/"};
  std::string handles[16];
  unsigned reads=0,writes=0,opens=0;
  static Folder &self(void *p) { return *static_cast<Folder*>(p); }
  static uint16_t stat(void *p,const char *path,mpe5::RedirectorFileInfo &out) {
    auto &f=self(p); auto found=f.files.find(path);
    if (found==f.files.end() && !f.dirs.count(path)) return 2;
    out={}; out.attributes=found==f.files.end()?0x10:0x20;
    out.size=found==f.files.end()?0:found->second.size();
    std::string name=path; name=name.substr(name.find_last_of('/')+1);
    std::copy_n(name.c_str(),std::min(size_t(12),name.size()),out.name); return 0;
  }
  static uint16_t enumerate(void *p,const char *path,uint16_t index,mpe5::RedirectorFileInfo &out) {
    auto &f=self(p); if (!f.dirs.count(path)) return 3;
    std::vector<std::string> names;
    const auto add=[&](const std::string &item) {
      const auto pos=item.find_last_of('/'); auto parent=pos?item.substr(0,pos):"/";
      if (item!="/" && parent==path) names.push_back(item);
    };
    for(auto &item:f.files) add(item.first);
    for(auto &item:f.dirs) add(item);
    std::sort(names.begin(),names.end());
    return index<names.size()?stat(p,names[index].c_str(),out):18;
  }
  static uint16_t open(void *p,uint8_t slot,const char *path,uint16_t,uint16_t action,uint8_t,
      mpe5::RedirectorFileInfo &out,uint16_t &result) {
    auto &f=self(p); auto found=f.files.find(path); const bool exists=found!=f.files.end();
    if (f.dirs.count(path)) return 5;
    if (exists && !(action&15)) return 80;
    if (!exists && !(action&0x10)) return 2;
    if (exists && (action&15)==2) f.files[path].clear();
    if (!exists) f.files[path]={};
    f.handles[slot]=path; ++f.opens; result=!exists?2:(action&15)==2?3:1;
    return stat(p,path,out);
  }
  static uint16_t close(void *p,uint8_t slot) { self(p).handles[slot].clear(); return 0; }
  static uint16_t read(void *p,uint8_t slot,uint32_t offset,uint8_t *out,uint16_t count,uint16_t &actual) {
    auto &f=self(p); ++f.reads; auto &file=f.files.at(f.handles[slot]);
    actual=offset>=file.size()?0:std::min(size_t(count),file.size()-offset);
    std::copy_n(file.data()+std::min(size_t(offset),file.size()),actual,out); return 0;
  }
  static uint16_t write(void *p,uint8_t slot,uint32_t offset,const uint8_t *in,uint16_t count,uint16_t &actual) {
    auto &f=self(p); ++f.writes; auto &file=f.files.at(f.handles[slot]);
    if(offset+count>file.size()) file.resize(offset+count);
    std::copy_n(in,count,file.data()+offset); actual=count; return 0;
  }
  static uint16_t truncate(void *p,uint8_t slot,uint32_t size) { self(p).files.at(self(p).handles[slot]).resize(size);return 0; }
  static uint16_t flush(void *,uint8_t) { return 0; }
  static uint16_t setTime(void *,uint8_t,uint16_t time,uint16_t date) {
    // Match the production SdFat adapter; invalid image timestamps must not
    // be hidden by a permissive host fixture during COPY acceptance.
    const auto month=(date>>5)&15u,day=date&31u;
    return !month||month>12||!day||(time>>11)>23||((time>>5)&63u)>59||(time&31u)>29?1:0;
  }
  static uint16_t attrs(void *,const char *,uint8_t) { return 0; }
  static uint16_t mkdir(void *p,const char *path) { return self(p).dirs.insert(path).second?0:5; }
  static uint16_t rmdir(void *p,const char *path) { return self(p).dirs.erase(path)?0:3; }
  static uint16_t remove(void *p,const char *path) { return self(p).files.erase(path)?0:2; }
  static uint16_t rename(void *p,const char *from,const char *to) {
    auto &f=self(p); auto item=f.files.extract(from); if(item.empty())return 2;
    item.key()=to; f.files.insert(std::move(item)); return 0;
  }
  static uint16_t space(void *,uint32_t &total,uint32_t &free) { total=40960;free=40000;return 0; }
  mpe5::RedirectorHost host() {
    return {this,stat,enumerate,open,close,read,write,truncate,flush,setTime,attrs,mkdir,rmdir,remove,rename,space};
  }
};

int main(int argc,char **argv) try {
  if(argc!=3) throw std::runtime_error("usage: mpe5_redirector_boot_test BIOS DOSVM.IMG");
  auto bios=readFile(argv[1]); Image image{readFile(argv[2])};
  std::vector<uint8_t> memory(mpe5::NativeBackingBytes),decode(5120);
  mpe5::Keyboard keyboard; mpe5::Redirector redirector; Folder folder;
  const std::string greeting="NATIVE FOLDER READ WORKS\r\n";
  folder.files["/HELLO.TXT"]={greeting.begin(),greeting.end()};
  folder.files["/IOCHECK.COM"]=mpe5RedirectorIoProbe();
  auto &large=folder.files["/LARGE.DAT"]; large.resize(20000+37);
  for(size_t i=0;i<large.size();++i)large[i]=uint8_t(i*73u+(i>>8));
  redirector.configure(mpe5::coreRedirectorMemory(),folder.host());
  mpe5::CoreHost host{}; host.addressMap=memory.data();host.addressMapBytes=memory.size();
  host.decodeTable=decode.data();host.decodeTableBytes=decode.size();
  host.bios=bios.data();host.biosBytes=bios.size();host.keyboard=&keyboard;
  host.drive={&image,readSector,uint32_t(image.bytes.size()/512),
    [](void *p,uint32_t lba,const uint8_t *in) {
      auto &image=*static_cast<Image*>(p); if(lba>=image.bytes.size()/512)return false;
      std::copy_n(in,512,image.bytes.data()+lba*512);return true;
    }};
  host.redirectorContext=&redirector;
  host.redirector=[](void *p,uint8_t op,mpe5::RedirectorRegisters &r) {
    auto &redirector=*static_cast<mpe5::Redirector*>(p);
    const auto before=r;
    bool result=redirector.service(op,r);
    if(std::getenv("MPE5_TRACE") && (!op || (result && (r.flags&1))))
      std::cerr<<"redirect op="<<unsigned(op)<<" ax="<<std::hex<<before.ax<<" -> "<<r.ax<<" CF="<<(r.flags&1)<<std::dec<<"\n";
    return result;
  };
  host.redirectorReset=[](void *p) { static_cast<mpe5::Redirector*>(p)->reset(); };
  if(!mpe5::coreStart(host))throw std::runtime_error("coreStart");
  runUntil(image,memory,"C:\\>",kBootSliceLimit,"redirector FreeDOS boot");
  if(!redirector.installed()) { printScreen(memory);throw std::runtime_error("DOSDIR did not install"); }
  const auto command=[&](const char *text) {
    queue(keyboard,text);
    for(unsigned i=0;i<30;++i) if(!mpe5::coreRun(kSliceInstructions))throw std::runtime_error("command halted");
    runUntil(image,memory,"C:\\>",kCommandSliceLimit,text);
    if(std::getenv("MPE5_TRACE"))printScreen(memory);
  };
  command("DIR D:\\\r");
  if(!hasText(memory,"HELLO"))throw std::runtime_error("DIR omitted shared folder file");
  command("TYPE D:\\HELLO.TXT\r");
  if(!hasText(memory,"NATIVE FOLDER READ WORKS"))throw std::runtime_error("TYPE failed to read shared file");
  command("D:\\IOCHECK\r");
  if(!hasText(memory,"IOCHECK PASS") || folder.files["/IOCHECK.DAT"]!=std::vector<uint8_t>({'0','1','2','3'}))
    throw std::runtime_error("DOS seek/read/truncate/commit/exit probe failed");
  for(const auto &handle:folder.handles) if(!handle.empty())throw std::runtime_error("DOS process exit leaked shared file handle");
  command("MEM\r");
  if(!hasText(memory,"Conventional"))throw std::runtime_error("MEM did not run");
  for(unsigned row=0;row<25;++row) {
    std::string line;
    for(unsigned col=0;col<80;++col)line+=char(memory[mpe5::NativeTextShadowAddress+2u*(row*80+col)]);
    if(line.find("Conventional")!=std::string::npos || line.find("Largest executable")!=std::string::npos)
      std::cout<<line<<'\n';
  }
  command("XCOPY D:\\HELLO.TXT C:\\ /Y\r");
  command("COPY C:\\HELLO.TXT D:\\XCOPY.TXT\r");
  if(folder.files["/XCOPY.TXT"]!=folder.files["/HELLO.TXT"])throw std::runtime_error("XCOPY did not execute/copy source");
  command("COPY D:\\HELLO.TXT C:\\COPY.TXT\r");
  command("COPY C:\\COPY.TXT D:\\ROUND.TXT\r");
  if(folder.files["/ROUND.TXT"]!=folder.files["/HELLO.TXT"])throw std::runtime_error("C/D copy round trip differs");
  command("COPY /B D:\\LARGE.DAT C:\\L.DAT\r");
  command("COPY /B C:\\L.DAT D:\\LARGE2.DAT\r");
  if(folder.files["/LARGE2.DAT"]!=large)throw std::runtime_error("cross-cluster C/D binary copy differs");
  command("COPY C:\\BOULDER.EXE D:\\B.EXE\r");
  if(folder.files["/B.EXE"].size()!=33280)throw std::runtime_error("shipped Boulder copy failed");
  const auto le16=[&](uint32_t at) { return uint32_t(image.bytes[at])|(uint32_t(image.bytes[at+1])<<8); };
  const uint32_t pbr=63*512, fatBytes=le16(pbr+22)*512;
  const uint32_t fatStart=pbr+le16(pbr+14)*512;
  if(!std::equal(image.bytes.begin()+fatStart,image.bytes.begin()+fatStart+fatBytes,image.bytes.begin()+fatStart+fatBytes))
    throw std::runtime_error("DOS cross-cluster writes left FAT copies inconsistent");
  command("MD D:\\SAVES\r");
  command("ECHO SAVED>D:\\SAVES\\SAVE.TXT\r");
  if(!folder.dirs.count("/SAVES") || folder.files["/SAVES/SAVE.TXT"].empty())throw std::runtime_error("host save file not written");
  command("DIR D:\\SAVES\r");
  if(!hasText(memory,"SAVE     TXT"))throw std::runtime_error("nested DIR omitted save file");
  command("REN D:\\ROUND.TXT RENAMED.TXT\r");
  if(!folder.files.count("/RENAMED.TXT") || folder.files.count("/ROUND.TXT"))throw std::runtime_error("rename failed");
  command("DEL D:\\RENAMED.TXT\r");
  if(folder.files.count("/RENAMED.TXT"))throw std::runtime_error("delete failed");
  mpe5::coreReset();
  if(redirector.installed())throw std::runtime_error("reset retained redirector state");
  keyboard.clear();
  if(!mpe5::coreStart(host))throw std::runtime_error("reboot failed");
  runUntil(image,memory,"C:\\>",kBootSliceLimit,"reboot with written C and saved D files");
  if(!redirector.installed())throw std::runtime_error("DOSDIR did not reinstall after reset");
  command("TYPE C:\\COPY.TXT\r");
  if(!hasText(memory,"NATIVE FOLDER READ WORKS"))throw std::runtime_error("C write did not survive guest reboot");
  command("TYPE D:\\SAVES\\SAVE.TXT\r");
  if(!hasText(memory,"SAVED"))throw std::runtime_error("D save did not survive guest reboot");
  mpe5::coreReset(); keyboard.clear();
  host.redirector=nullptr; host.redirectorReset=nullptr; host.redirectorContext=nullptr;
  if(!mpe5::coreStart(host))throw std::runtime_error("start without optional redirector failed");
  runUntil(image,memory,"C:\\>",kBootSliceLimit,"boot with no optional D redirector");
  command("TYPE C:\\COPY.TXT\r");
  if(!hasText(memory,"NATIVE FOLDER READ WORKS"))throw std::runtime_error("optional D absence broke C");
  mpe5::coreReset();
  std::cout<<"PASS FreeDOS DOSDIR boot, nested DIR/TYPE D:, MEM/XCOPY, C/D copy, save, rename, delete, reboot persistence, absent D; host reads="<<folder.reads<<" writes="<<folder.writes<<"\n";
  return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }

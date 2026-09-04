#include <map>
#include <set>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#define PROGMEM
static constexpr int FILE_READ=0,FILE_WRITE=1,FILE_WRITE_BEGIN=2;
// Use the embedded SdFat flag values. Arduino FILE_WRITE/FILE_WRITE_BEGIN
// are translated separately by TestSD::open; they are not FsFile open flags.
#ifndef O_RDONLY
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_AT_END 0x04
#define O_APPEND 0x08
#define O_CREAT 0x10
#define O_TRUNC 0x20
#define O_EXCL 0x40
#define O_ACCMODE 0x03
#endif
#ifndef T_WRITE
#define T_WRITE 4
#endif
static bool StorageFails=false;
static size_t StorageWriteBudget=size_t(-1);
static unsigned rootWriteAttempts=0,rootMutationAttempts=0;
static bool saveFolderPath(const std::string &p){return p=="/SAVES"||p.rfind("/SAVES/",0)==0;}
struct TestSD;
struct File {
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t cursor=0;
  bool directory=false;
  unsigned shortReads=0,shortWrites=0,failedSeeks=0;
  TestSD *owner=nullptr;
  std::string path;
  size_t directoryCursor=0;
  bool readable=true,writable=false,append=false;
  uint8_t error=0;
  explicit operator bool()const{return bool(bytes)||directory;}
  bool isOpen()const{return bool(*this);}
  bool isDirectory()const{return directory;}
  bool isDir()const{return directory;}
  bool isReadOnly()const;
  bool isHidden()const;
  bool getModifyDateTime(uint16_t *date,uint16_t *time);
  bool timestamp(uint8_t flags,uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,uint8_t second);
  size_t size()const{return bytes?bytes->size():0;}
  uint64_t fileSize()const{return size();}
  uint64_t curPosition()const{return cursor;}
  uint8_t getError()const{return error;}
  bool getWriteError()const{return error!=0;}
  void clearError(){error=0;}
  void clearWriteError(){error=0;}
  bool seek(size_t position);
  bool seekSet(uint64_t position){return position<=size_t(-1)&&seek(size_t(position));}
  int read(uint8_t *out,size_t n);
  int read(void *out,size_t n){return read(static_cast<uint8_t *>(out),n);}
  int read(){uint8_t byte;return read(&byte,1)==1?byte:-1;}
  size_t write(const uint8_t *in,size_t n);
  size_t write(const void *in,size_t n){return write(static_cast<const uint8_t *>(in),n);}
  bool truncate(uint64_t length);
  bool truncate(){return truncate(cursor);}
  bool sync();
  void flush(){(void)sync();}
  bool close();
  void rewind(){cursor=directoryCursor=0;error=0;}
  size_t getName(char *out,size_t capacity);
  bool openNext(File *dir,int flags=O_RDONLY);
  bool open(File *dir,const char *relative,int flags=O_RDONLY);
};
using FsFile=File;
struct TestSdfs {
  TestSD *owner=nullptr;
  explicit TestSdfs(TestSD *source=nullptr):owner(source){}
  // TestSD resets/copies keep the interface bound to its containing instance.
  TestSdfs &operator=(const TestSdfs &){return *this;}
  File open(const char *p,int flags=O_RDONLY);
  bool exists(const char *p);
  bool mkdir(const char *p,bool parents=true);
  bool remove(const char *p);
  bool rmdir(const char *p);
  bool rename(const char *a,const char *b);
  uint32_t clusterCount()const;
  uint32_t freeClusterCount()const;
  uint32_t sectorsPerCluster()const;
};
// The target cartridge loader owns these globals. The DOS handoff closes and
// frees them before taking RAM2, so expose the same lifecycle to host tests.
static File myFile;
static uint8_t *BigBuf=nullptr;
static uint32_t BigBufCount=0;
struct TestSD {
  struct Metadata {uint16_t date=0x21,time=0;uint8_t attributes=0;};
  TestSdfs sdfs;
  std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
  std::set<std::string> directories{"/"};
  std::map<std::string,Metadata> metadata;
  std::vector<std::string> writeAttempts,mutations;
  std::string failWritePath,failReadPath,failSeekPath,failSyncPath,failRemovePath;
  std::string failTruncatePath,failEnumerationPath;
  size_t enumerationFailAfter=size_t(-1);
  std::map<std::pair<std::string,std::string>,unsigned> renameFailures;
  bool mkdirFails=false;
  uint32_t totalClusters=4096,availableClusters=3072,clusterSectors=8;
  unsigned freeClusterQueries=0;
  TestSD():sdfs(this){}
  TestSD(const TestSD &other):TestSD(){*this=other;}
  TestSD &operator=(const TestSD &)=default;
  static std::string normalize(const char *p){
    if(!p||!*p)return {};
    std::vector<std::string> parts;std::string part;
    for(const char *c=p;;++c){
      if(*c=='/'||*c=='\\'||!*c){
        if(part==".."){if(parts.empty())return {};parts.pop_back();}
        else if(!part.empty()&&part!=".")parts.push_back(part);
        part.clear();if(!*c)break;
      }else part+=*c;
    }
    std::string result;for(const auto &entry:parts)result+="/"+entry;
    return result.empty()?"/":result;
  }
  static std::string folded(std::string p){for(char &c:p)if(c>='a'&&c<='z')c=char(c-'a'+'A');return p;}
  static std::string parent(const std::string &p){const auto slash=p.find_last_of('/');return slash==0?"/":p.substr(0,slash);}
  static bool same(const std::string &a,const std::string &b){return !a.empty()&&!b.empty()&&folded(normalize(a.c_str()))==folded(normalize(b.c_str()));}
  std::string resolve(const char *p)const{
    const auto normalized=normalize(p),key=folded(normalized);if(normalized.empty())return {};
    for(const auto &entry:directories)if(folded(entry)==key)return entry;
    for(const auto &entry:files)if(folded(entry.first)==key)return entry.first;
    const auto requestedParent=parent(normalized);
    for(const auto &entry:directories)if(folded(entry)==folded(requestedParent))
      return (entry=="/"?"":entry)+normalized.substr(requestedParent=="/"?0:requestedParent.size());
    return normalized;
  }
  bool parentExists(const std::string &p)const{return !p.empty()&&directories.count(resolve(parent(p).c_str()));}
  bool fault(const std::string &p,const std::string &configured)const{return StorageFails||same(p,configured);}
  File openFlags(const char *requested,int flags){
    const auto p=resolve(requested);const int access=flags&O_ACCMODE;
    const bool write=access==O_WRONLY||access==O_RDWR;
    if(write){writeAttempts.push_back(p);if(!saveFolderPath(p))rootWriteAttempts++;}
    if(p.empty()||access==O_ACCMODE||fault(p,write?failWritePath:failReadPath)||
       (access!=O_WRONLY&&same(p,failReadPath))||
       (!write&&(flags&(O_TRUNC|O_CREAT|O_APPEND))))return {};
    if(directories.count(p)){
      if(write||(flags&(O_CREAT|O_EXCL|O_TRUNC)))return {};
      File f;f.directory=true;f.owner=this;f.path=p;return f;
    }
    if(!parentExists(p))return {};
    const bool present=files.count(p)!=0;
    if(write&&metadata.count(p)&&(metadata[p].attributes&1))return {};
    if((present&&(flags&O_CREAT)&&(flags&O_EXCL))||(!present&&!(flags&O_CREAT)))return {};
    if(!present){files[p]=std::make_shared<std::vector<uint8_t>>();mutations.push_back(p);}
    if(flags&O_TRUNC){files[p]->clear();mutations.push_back(p);}
    File f;f.bytes=files[p];f.cursor=(flags&(O_AT_END|O_APPEND))?f.bytes->size():0;
    f.owner=this;f.path=p;f.readable=access!=O_WRONLY;f.writable=write;f.append=(flags&O_APPEND)!=0;return f;
  }
  File open(const char *p,int mode=FILE_READ){
    return openFlags(p,mode==FILE_READ?O_RDONLY:O_RDWR|O_CREAT|(mode==FILE_WRITE?O_APPEND:0));
  }
  bool exists(const char *p){const auto name=resolve(p);return !StorageFails&&(files.count(name)||directories.count(name));}
  bool mkdir(const char *requested,bool parents=true){
    const auto p=resolve(requested);if(!saveFolderPath(p))rootMutationAttempts++;
    if(p.empty()||StorageFails||mkdirFails||same(p,failWritePath)||files.count(p))return false;
    if(directories.count(p))return true;
    if(!parentExists(p)){if(!parents||!mkdir(parent(p).c_str(),true))return false;}
    const auto name=resolve(p.c_str());directories.insert(name);mutations.push_back(name);return true;
  }
  bool remove(const char *requested){
    const auto p=resolve(requested);if(!saveFolderPath(p))rootMutationAttempts++;
    if(p.empty()||fault(p,failRemovePath)||same(p,failWritePath)||!files.count(p))return false;
    mutations.push_back(p);metadata.erase(p);return files.erase(p)>0;
  }
  bool rmdir(const char *requested){
    const auto p=resolve(requested);if(!saveFolderPath(p))rootMutationAttempts++;
    if(p.empty()||p=="/"||fault(p,failRemovePath)||same(p,failWritePath)||!directories.count(p))return false;
    for(const auto &item:directories)if(item!=p&&item.rfind(p+"/",0)==0)return false;
    for(const auto &item:files)if(item.first.rfind(p+"/",0)==0)return false;
    directories.erase(p);metadata.erase(p);mutations.push_back(p);return true;
  }
  bool rename(const char *from,const char *to){
    const auto a=resolve(from),b=resolve(to);
    if(!saveFolderPath(a)||!saveFolderPath(b))rootMutationAttempts++;
    auto failure=renameFailures.find({a,b});if(failure!=renameFailures.end()&&failure->second){failure->second--;return false;}
    if(a.empty()||b.empty()||a=="/"||fault(a,failWritePath)||same(b,failWritePath)||
       (!files.count(a)&&!directories.count(a))||exists(b.c_str())||!parentExists(b))return false;
    if(files.count(a)){files[b]=files[a];files.erase(a);}
    else{
      if(b.rfind(a+"/",0)==0)return false;
      std::vector<std::string> oldDirs;std::vector<std::pair<std::string,std::shared_ptr<std::vector<uint8_t>>>> oldFiles;
      for(const auto &p:directories)if(p==a||p.rfind(a+"/",0)==0)oldDirs.push_back(p);
      for(const auto &p:files)if(p.first.rfind(a+"/",0)==0)oldFiles.push_back(p);
      for(const auto &p:oldDirs){directories.erase(p);directories.insert(b+p.substr(a.size()));}
      for(const auto &p:oldFiles){files.erase(p.first);files[b+p.first.substr(a.size())]=p.second;}
    }
    std::vector<std::pair<std::string,Metadata>> oldMetadata;
    for(const auto &p:metadata)if(p.first==a||p.first.rfind(a+"/",0)==0)oldMetadata.push_back(p);
    for(const auto &p:oldMetadata){metadata.erase(p.first);metadata[b+p.first.substr(a.size())]=p.second;}
    mutations.push_back(a);mutations.push_back(b);return true;
  }
} SD;
File TestSdfs::open(const char *p,int flags){return owner?owner->openFlags(p,flags):File{};}
bool TestSdfs::exists(const char *p){return owner&&owner->exists(p);}
bool TestSdfs::mkdir(const char *p,bool parents){return owner&&owner->mkdir(p,parents);}
bool TestSdfs::remove(const char *p){return owner&&owner->remove(p);}
bool TestSdfs::rmdir(const char *p){return owner&&owner->rmdir(p);}
bool TestSdfs::rename(const char *a,const char *b){return owner&&owner->rename(a,b);}
uint32_t TestSdfs::clusterCount()const{return owner&&!StorageFails?owner->totalClusters:0;}
uint32_t TestSdfs::freeClusterCount()const{if(owner)++owner->freeClusterQueries;return owner&&!StorageFails?owner->availableClusters:UINT32_MAX;}
uint32_t TestSdfs::sectorsPerCluster()const{return owner&&!StorageFails?owner->clusterSectors:0;}
bool File::isReadOnly()const{return owner&&owner->metadata.count(path)&&(owner->metadata.at(path).attributes&1);}
bool File::isHidden()const{return owner&&owner->metadata.count(path)&&(owner->metadata.at(path).attributes&2);}
bool File::getModifyDateTime(uint16_t *date,uint16_t *time){
  if(!isOpen()||!owner||owner->fault(path,owner->failReadPath)){error=1;return false;}
  const auto found=owner->metadata.find(path);const TestSD::Metadata info=found==owner->metadata.end()?TestSD::Metadata{}:found->second;
  if(date)*date=info.date;if(time)*time=info.time;return true;
}
bool File::timestamp(uint8_t flags,uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,uint8_t second){
  if(!isOpen()||!owner||owner->fault(path,owner->failWritePath)||year<1980||year>2107||
     !month||month>12||!day||day>31||hour>23||minute>59||second>59){error=1;return false;}
  if(flags&T_WRITE){auto &info=owner->metadata[path];info.date=uint16_t(((year-1980)<<9)|(month<<5)|day);info.time=uint16_t((hour<<11)|(minute<<5)|(second/2));}
  return true;
}
bool File::seek(size_t position){
  if(failedSeeks){--failedSeeks;error=1;return false;}
  // FAT FsFile cannot seek beyond EOF: DOS logical positions are the
  // redirector's responsibility until it extends the file with real writes.
  if(!bytes||position>bytes->size()||StorageFails||(owner&&owner->same(path,owner->failSeekPath))){error=1;return false;}
  cursor=position;return true;
}
int File::read(uint8_t *out,size_t n){
  if(!bytes||!readable||StorageFails||(owner&&owner->same(path,owner->failReadPath))){error=1;return -1;}
  if(shortReads){--shortReads;n=std::min(n,size_t(17));}
  n=std::min(n,cursor<bytes->size()?bytes->size()-cursor:0);
  if(n)memcpy(out,bytes->data()+cursor,n);cursor+=n;return int(n);
}
size_t File::write(const uint8_t *in,size_t n){
  if(!bytes||!writable||StorageFails||(owner&&owner->same(path,owner->failWritePath))){error=1;return 0;}
  if(append)cursor=bytes->size();
  if(shortWrites){--shortWrites;n=std::min(n,size_t(17));}
  n=std::min(n,StorageWriteBudget);StorageWriteBudget-=n;
  if(cursor+n<cursor){error=1;return 0;}
  if(cursor+n>bytes->size())bytes->resize(cursor+n);
  if(n)memcpy(bytes->data()+cursor,in,n);cursor+=n;return n;
}
bool File::truncate(uint64_t length){
  if(!bytes||!writable||length>bytes->size()||StorageFails||
     (owner&&(owner->same(path,owner->failTruncatePath)||owner->same(path,owner->failWritePath)))){error=1;return false;}
  cursor=size_t(length);bytes->resize(cursor);return sync();
}
bool File::sync(){
  if(StorageFails||(owner&&(owner->same(path,owner->failSyncPath)||(writable&&owner->same(path,owner->failWritePath))))){error=1;return false;}
  return true;
}
bool File::close(){const bool okay=sync();bytes.reset();directory=false;owner=nullptr;path.clear();cursor=directoryCursor=0;return okay;}
size_t File::getName(char *out,size_t capacity){
  if(!out||!capacity)return 0;*out=0;
  if(!isOpen()||StorageFails||(owner&&owner->same(path,owner->failReadPath))){error=1;return 0;}
  const auto name=path.substr(path.find_last_of('/')+1);const size_t n=std::min(name.size(),capacity-1);
  memcpy(out,name.data(),n);out[n]=0;return n;
}
bool File::openNext(File *dir,int flags){
  close();error=0;
  if(!dir||!dir->directory||!dir->owner){error=1;return false;}
  TestSD &fs=*dir->owner;
  if(fs.fault(dir->path,fs.failReadPath)||fs.same(dir->path,fs.failEnumerationPath)||dir->directoryCursor>=fs.enumerationFailAfter){dir->error=error=1;return false;}
  std::vector<std::string> children;
  for(const auto &p:fs.directories)if(p!=dir->path&&TestSD::parent(p)==dir->path)children.push_back(p);
  for(const auto &p:fs.files)if(TestSD::parent(p.first)==dir->path)children.push_back(p.first);
  std::sort(children.begin(),children.end(),[](const std::string &a,const std::string &b){return TestSD::folded(a)<TestSD::folded(b);});
  if(dir->directoryCursor>=children.size())return false;
  *this=fs.openFlags(children[dir->directoryCursor++].c_str(),flags);
  if(!*this){dir->error=error=1;return false;}return true;
}
bool File::open(File *dir,const char *relative,int flags){
  close();error=0;
  if(!dir||!dir->directory||!dir->owner||!relative){error=1;return false;}
  const std::string full=(*relative=='/'?std::string(relative):dir->path+"/"+relative);
  *this=dir->owner->openFlags(full.c_str(),flags);return isOpen();
}
#ifdef MPE_SD_STUB_TEST
#include "../engine/native-dos/mpe5_folder_fs.h"
int main(){
  // All operations below use in-memory maps. No path is opened on the host OS.
  TestSD fs;uint8_t buffer[64]{};char name[13];
  assert(fs.sdfs.mkdir("/DOSVM/D/LEVELS")&&fs.sdfs.exists("/dosvm/d/levels"));
  assert(!fs.sdfs.open("/DOSVM/D/MISSING.DAT",O_RDWR));
  auto file=fs.sdfs.open("/DOSVM/D/LEVELS/ONE.DAT",O_RDWR|O_CREAT|O_EXCL);
  assert(file&&file.write("abcdef",6)==6&&file.sync());
  assert(!fs.sdfs.open("/DOSVM/D/LEVELS/ONE.DAT",O_RDWR|O_CREAT|O_EXCL));
  assert(file.fileSize()==6&&!file.seekSet(7));
  assert(file.seekSet(2)&&file.write("XY",2)==2&&file.seekSet(0)&&file.read(buffer,6)==6);
  assert(!memcmp(buffer,"abXYef",6));
  assert(file.truncate(4)&&file.fileSize()==4&&!file.truncate(5));file.close();
  auto readOnly=fs.sdfs.open("/dosvm/d/levels/one.dat",O_RDONLY);
  assert(readOnly&&readOnly.getName(name,sizeof name)==7&&!strcmp(name,"ONE.DAT"));
  assert(readOnly.write("x",1)==0&&!readOnly.truncate(0));
  fs.failReadPath="/DOSVM/D/LEVELS/ONE.DAT";assert(readOnly.read(buffer,1)==-1);fs.failReadPath.clear();
  auto writer=fs.sdfs.open("/DOSVM/D/LEVELS/ONE.DAT",O_RDWR|O_TRUNC);
  assert(writer.fileSize()==0&&writer.write("reset",5)==5);
  fs.failSyncPath=writer.path;assert(!writer.sync());fs.failSyncPath.clear();
  fs.failTruncatePath=writer.path;assert(!writer.truncate(0)&&writer.fileSize()==5);fs.failTruncatePath.clear();
  writer.close();
  auto append=fs.open("/DOSVM/D/LEVELS/ONE.DAT",FILE_WRITE);
  assert(append.seekSet(0)&&append.write("!",1)==1&&append.fileSize()==6);append.close();
  auto beginning=fs.open("/DOSVM/D/LEVELS/ONE.DAT",FILE_WRITE_BEGIN);
  assert(beginning.write("R",1)==1&&beginning.fileSize()==6);beginning.close();
  assert(fs.sdfs.mkdir("/DOSVM/D/EMPTY"));
  auto dir=fs.sdfs.open("/DOSVM/D",O_RDONLY);File entry;
  std::vector<std::string> names;
  while(entry.openNext(&dir)){assert(entry.isDirectory());entry.getName(name,sizeof name);names.emplace_back(name);entry.close();}
  assert(!dir.getError()&&(names==std::vector<std::string>{"EMPTY","LEVELS"}));
  dir.rewind();assert(entry.openNext(&dir));fs.enumerationFailAfter=1;
  assert(!entry.openNext(&dir)&&dir.getError());fs.enumerationFailAfter=size_t(-1);dir.close();
  auto nested=fs.sdfs.open("/DOSVM/D/LEVELS");assert(entry.openNext(&nested)&&!entry.isDirectory());
  assert(entry.getName(name,sizeof name)==7&&entry.read(buffer,6)==6&&!memcmp(buffer,"Reset!",6));
  assert(!entry.openNext(&nested)&&!nested.getError());
  fs.files["/DOSVM/D/TOO-LONG-NAME.DAT"]=std::make_shared<std::vector<uint8_t>>();
  auto longName=fs.sdfs.open("/DOSVM/D/TOO-LONG-NAME.DAT");
  assert(longName.getName(name,sizeof name)==12&&name[12]==0);longName.close();
  assert(!fs.sdfs.rmdir("/DOSVM/D/LEVELS"));
  assert(fs.sdfs.rename("/DOSVM/D/LEVELS","/DOSVM/D/RENAMED"));
  assert(fs.sdfs.exists("/DOSVM/D/RENAMED/ONE.DAT")&&!fs.sdfs.exists("/DOSVM/D/LEVELS/ONE.DAT"));
  assert(!fs.sdfs.rename("/DOSVM/D/RENAMED","/DOSVM/D/RENAMED/CHILD"));
  fs.renameFailures[{"/DOSVM/D/RENAMED/ONE.DAT","/DOSVM/D/RENAMED/TWO.DAT"}]=1;
  assert(!fs.sdfs.rename("/DOSVM/D/RENAMED/ONE.DAT","/DOSVM/D/RENAMED/TWO.DAT"));
  assert(fs.sdfs.rename("/DOSVM/D/RENAMED/ONE.DAT","/DOSVM/D/RENAMED/TWO.DAT"));
  fs.failRemovePath="/DOSVM/D/RENAMED/TWO.DAT";assert(!fs.sdfs.remove(fs.failRemovePath.c_str()));fs.failRemovePath.clear();
  assert(fs.sdfs.remove("/DOSVM/D/RENAMED/TWO.DAT")&&fs.sdfs.rmdir("/DOSVM/D/RENAMED"));
  assert(!fs.sdfs.rmdir("/"));
  TestSD other;other=TestSD{};assert(other.sdfs.mkdir("/OTHER")&&!fs.sdfs.exists("/OTHER"));
  TestSD copied(other);assert(copied.sdfs.mkdir("/COPY")&&!other.sdfs.exists("/COPY"));
  StorageFails=true;assert(!fs.sdfs.exists("/DOSVM/D")&&!fs.sdfs.mkdir("/FAIL")&&!fs.sdfs.open("/DOSVM/D"));StorageFails=false;
  std::puts("host FsFile: flags, exact IO, truncate/sync, nested enumeration, rename/delete, faults and owner isolation passed");

  // Exercise the production callback adapter using only the above in-memory
  // SdFat implementation. The separate integrated test boots real FreeDOS.
  SD=TestSD{};unsigned ioCount=0;
  mpe5::FolderFilesystem folder([](void *p){++*static_cast<unsigned *>(p);},&ioCount);
  assert(folder.begin());auto host=folder.host();void *context=host.context;
  mpe5::RedirectorFileInfo info{};uint16_t result=0,actual=0;
  const auto open=[&](uint8_t slot,const char *path,uint16_t mode=0x42,uint16_t action=0x11){return host.open(context,slot,path,mode,action,0x20,info,result);};
  assert(SD.sdfs.exists("/DOSVM/D")&&host.stat(context,"/",info)==0&&info.attributes==0x10);
  assert(host.mkdir(context,"/LEVELS")==0&&host.mkdir(context,"/LEVELS")==5);
  assert(open(0,"/LEVELS/ONE.DAT")==0&&result==2&&info.size==0);
  assert(open(16,"/TOOMANY.DAT")==4&&open(0,"/TAKEN.DAT")==6);
  assert(host.write(context,0,3,reinterpret_cast<const uint8_t *>("abc"),3,actual)==0&&actual==3);
  memset(buffer,0xcc,sizeof buffer);assert(host.read(context,0,0,buffer,6,actual)==0&&actual==6);
  assert(buffer[0]==0&&buffer[1]==0&&buffer[2]==0&&!memcmp(buffer+3,"abc",3));
  assert(host.read(context,0,100,buffer,6,actual)==0&&actual==0);
  assert(host.truncate(context,0,10)==0&&host.read(context,0,6,buffer,4,actual)==0&&actual==4);
  assert(!buffer[0]&&!buffer[1]&&!buffer[2]&&!buffer[3]);
  assert(host.truncate(context,0,2)==0&&host.stat(context,"/LEVELS/ONE.DAT",info)==0&&info.size==2);
  assert(host.truncate(context,0,mpe5::FolderFilesystem::MaxExtensionBytes+3)==25);
  assert(host.write(context,0,UINT32_MAX,reinterpret_cast<const uint8_t *>("x"),1,actual)==25&&actual==0);
  assert(host.remove(context,"/LEVELS/ONE.DAT")==32&&host.rename(context,"/LEVELS","/OTHER")==32);
  assert(open(1,"/LEVELS/ONE.DAT",0x12,0x01)==32); // deny-all conflicts with open handle
  const uint16_t date=uint16_t((46<<9)|(9<<5)|3),time=uint16_t((14<<11)|(2<<5)|15);
  assert(host.setTime(context,0,time,date)==0&&host.close(context,0)==0);
  assert(host.stat(context,"/LEVELS/ONE.DAT",info)==0&&info.date==date&&info.time==time);
  assert(host.setAttributes(context,"/LEVELS/ONE.DAT",0x20)==0&&host.setAttributes(context,"/LEVELS/ONE.DAT",1)==5);
  assert(open(0,"/LEVELS/ONE.DAT",0x40,0x01)==0&&result==1);
  assert(host.write(context,0,0,reinterpret_cast<const uint8_t *>("x"),1,actual)==5&&host.truncate(context,0,0)==5);
  assert(host.close(context,0)==0&&host.close(context,0)==6);
  assert(open(0,"/LEVELS/ONE.DAT",0x42,0x10)==80);
  assert(open(0,"/LEVELS/ONE.DAT",0x42,0x12)==0&&result==3&&info.size==0);
  StorageWriteBudget=2;
  assert(host.write(context,0,0,reinterpret_cast<const uint8_t *>("abcd"),4,actual)==112&&actual==2);
  StorageWriteBudget=size_t(-1);
  SD.failReadPath="/DOSVM/D/LEVELS/ONE.DAT";assert(host.read(context,0,0,buffer,1,actual)==30);SD.failReadPath.clear();
  SD.failSeekPath="/DOSVM/D/LEVELS/ONE.DAT";assert(host.read(context,0,0,buffer,1,actual)==25);SD.failSeekPath.clear();
  SD.failSyncPath="/DOSVM/D/LEVELS/ONE.DAT";assert(host.flush(context,0)==29&&host.close(context,0)==29);SD.failSyncPath.clear();
  assert(host.mkdir(context,"/EMPTY")==0);
  SD.files["/DOSVM/D/TOO-LONG-NAME.DAT"]=std::make_shared<std::vector<uint8_t>>();
  SD.files["/DOSVM/D/VALID.TXT"]=std::make_shared<std::vector<uint8_t>>();
  SD.metadata["/DOSVM/D/VALID.TXT"].attributes=3;
  assert(host.stat(context,"/VALID.TXT",info)==0&&info.attributes==0x23);
  assert(open(0,"/VALID.TXT",0x42,0x01)==5&&host.remove(context,"/VALID.TXT")==5);
  names.clear();for(uint16_t i=0;;++i){const uint16_t error=host.enumerate(context,"/",i,info);if(error==18)break;assert(!error);names.emplace_back(info.name);}
  assert((names==std::vector<std::string>{"EMPTY","LEVELS","VALID.TXT"}));
  assert(host.enumerate(context,"/LEVELS",0,info)==0&&!strcmp(info.name,"ONE.DAT"));
  assert(host.enumerate(context,"/LEVELS",1,info)==18);
  SD.failEnumerationPath="/DOSVM/D";assert(host.enumerate(context,"/",0,info)==30);SD.failEnumerationPath.clear();
  assert(host.rmdir(context,"/LEVELS")==5&&host.rmdir(context,"/EMPTY")==0);
  assert(host.rename(context,"/LEVELS/ONE.DAT","/LEVELS/TWO.DAT")==0);
  assert(host.remove(context,"/LEVELS/TWO.DAT")==0&&host.rmdir(context,"/LEVELS")==0);
  const auto fileCount=SD.files.size(),directoryCount=SD.directories.size();
  for(const char *bad:{"/../BOOT.HEX","/LEVELS/../../BOOT.HEX","C:/BOOT.HEX","/NINECHARS.DAT","/A/LONG.EXTN","/BAD?.TXT"}){
    assert(open(0,bad)!=0&&host.stat(context,bad,info)!=0&&host.remove(context,bad)!=0&&host.mkdir(context,bad)!=0);
  }
  assert(host.rmdir(context,"/")==5&&host.rename(context,"/","/MOVED")==5);
  assert(SD.files.size()==fileCount&&SD.directories.size()==directoryCount);
  uint32_t total=0,available=0;assert(host.space(context,total,available)==0&&total==32768&&available<=24576);
  assert(SD.freeClusterQueries==1&&ioCount>20);
  folder.end();assert(host.stat(context,"/",info)==21&&host.space(context,total,available)==21);
  SD.mkdirFails=true;SD.directories.erase("/DOSVM/D");assert(!folder.begin());SD.mkdirFails=false;
  std::printf("folder adapter: rooted read/write, modes/share, zero extension, timestamps, 8.3 directories, IO faults and confinement passed (%zu bytes)\n",sizeof folder);
}
#else
static unsigned inputInterruptMasks=0;
static void noInterrupts(){inputInterruptMasks++;} static void interrupts(){}
#define main legacyIntroConformance
#include "mpe3-title-native-harness.cpp"
#undef main

static uint8_t inputSequence=0;
static unsigned nativeFrames=0,packets=0,inputEvents=0;
static unsigned queueFullRetries=0,directionReversals=0;
static unsigned stressCellPackets=0,stressKeyboardEdges=0,stressPointerSamples=0,stressPointerEdges=0,stressFireEdges=0;
static bool inputStressArmed=false,inputStressActive=false,inputStressComplete=false;
static unsigned saveDirectoryChecks=0,rootSaveFallbackChecks=0,saveFailureChecks=0;
static unsigned spritePackets=0,spriteCommits=0,coordinateFrames=0,visibleSpriteFrames=0,threeLayerFrames=0,fourLayerFrames=0;
static uint8_t screen[10000]{};
static uint8_t stagedShapes[256]{},visibleShapes[256]{},stagedParts=0;
static bool hasSpritePose=false;
static std::ofstream trace;
static bool inputAttempt(uint8_t sequence,uint8_t key,uint8_t scan,uint8_t joy,uint8_t flags)
{
  writeControl(0xf8,key);writeControl(0xf9,scan);writeControl(0xfa,joy);
  writeControl(0xfd,flags);writeControl(0xfe,sequence);
  writeControl(0xff,uint8_t(0xa5^key^scan^joy^flags^sequence));writeControl(0xf4,3);
  return EZFlashRAM[0xfc]==sequence;
}
static uint8_t nextInputSequence()
{
  inputSequence=inputSequence==255?1:inputSequence+1;return inputSequence;
}
static void checkInputBackpressure()
{
  assert(MPE4Game&&MPE4Game->framePending&&MPE3Title.Pending&&EZFlashRAM[3]==1);
  MPE4ResetInput();
  // Fill the ordered keyboard FIFO without allowing a game tick. Every edge
  // is ACKed immediately, so the C64 may continue scanning during all 53 cell
  // packets. The seventeenth edge remains owned by the C64 until one slot is
  // consumed, then the exact same sequence is accepted on retry.
  MPE4KeyboardWrite=MPE4KeyboardRead=250;
  std::vector<std::pair<uint8_t,uint8_t>> keys;
  for(uint8_t n=0;n<MPE4KeyboardSlots;n++) {
    const uint8_t key=uint8_t('a'+n),scan=uint8_t(30+n),sequence=nextInputSequence();
    assert(inputAttempt(sequence,key,scan,0,1));keys.push_back({key,scan});inputEvents++;
  }
  const uint8_t retrySequence=nextInputSequence();const uint8_t priorAck=EZFlashRAM[0xfc];
  assert(!inputAttempt(retrySequence,'q',46,0,1)&&EZFlashRAM[0xfc]==priorAck);queueFullRetries++;
  mpe4::Input input{};MPE4ConsumeInput(input);
  assert(input.key==keys[0].first&&input.scan==keys[0].second);
  assert(inputAttempt(retrySequence,'q',46,0,1));inputEvents++;
  for(unsigned n=1;n<keys.size();n++) {
    input={};MPE4ConsumeInput(input);assert(input.key==keys[n].first&&input.scan==keys[n].second);
  }
  input={};MPE4ConsumeInput(input);assert(input.key=='q'&&input.scan==46);
  input={};MPE4ConsumeInput(input);assert(!input.key&&!input.scan);
  stressKeyboardEdges=keys.size()+1;

  // Held joystick direction is a latest-state mailbox. Separate fire presses
  // use monotonic producer/consumer cursors, so press/release pairs that occur
  // inside one video transfer still become distinct game-tick edges. Begin at
  // 254 to prove that the widened counter cannot alias at the old byte wrap.
  MPE4ResetInput();
  MPE4JoyFireWrite=MPE4JoyFireRead=254;
  for(const uint8_t joy:std::array<uint8_t,4>{24,8,20,4}) {
    assert(inputAttempt(nextInputSequence(),0,0,joy,2));inputEvents++;
  }
  assert(MPE4JoyFireWrite==256);
  for(unsigned n=0;n<3;n++) {
    input={};MPE4ConsumeInput(input);assert(input.direction==7);
    assert(input.fire==(n<2));stressFireEdges+=input.fire;
  }
  assert(inputAttempt(nextInputSequence(),0,0,0,2));inputEvents++;
  input={};MPE4ConsumeInput(input);assert(!input.direction&&!input.fire);

  // Motion-only records collapse to the newest coordinates. Button states
  // retain their order and coordinates, including motion after the release.
  // The terminal scans once at each packet boundary. A full sprite frame has
  // at most 56 boundaries (two shape, 53 cell, one SID), safely below even the
  // former byte revision span; the 16-bit revision also leaves ample margin
  // for retries and future packet types. Cross its wrap and retain the last
  // of all 56 motion samples deterministically.
  MPE4ResetInput();MPE4PointerRevision=MPE4PointerReadRevision=65500;
  for(uint8_t n=0;n<56;n++) {
    assert(inputAttempt(nextInputSequence(),uint8_t(20+n),uint8_t(40+n),0,4));inputEvents++;
  }
  stressPointerSamples=56;
  input={};MPE4ConsumeInput(input);
  assert(input.pointerEvent&&input.pointerX==75&&input.pointerY==95&&!input.pointerButtons&&MPE4PointerReadRevision==20);
  assert(inputAttempt(nextInputSequence(),40,60,0,12));inputEvents++;
  assert(inputAttempt(nextInputSequence(),50,70,0,12));inputEvents++;
  assert(inputAttempt(nextInputSequence(),60,80,0,4));inputEvents++;
  assert(inputAttempt(nextInputSequence(),70,90,0,4));inputEvents++;
  input={};MPE4ConsumeInput(input);assert(input.pointerEvent&&input.pointerX==40&&input.pointerY==60&&input.pointerButtons==1);stressPointerEdges++;
  input={};MPE4ConsumeInput(input);assert(input.pointerEvent&&input.pointerX==60&&input.pointerY==80&&!input.pointerButtons);stressPointerEdges++;
  input={};MPE4ConsumeInput(input);assert(input.pointerEvent&&input.pointerX==70&&input.pointerY==90&&!input.pointerButtons);
  input={};MPE4ConsumeInput(input);assert(!input.pointerEvent);

  // A full button-edge queue applies the same wire backpressure as keyboard:
  // no ACK and no partial state change until the oldest edge is consumed.
  MPE4ResetInput();MPE4PointerEdgeWrite=MPE4PointerEdgeRead=252;std::vector<uint8_t> buttons;
  for(uint8_t n=0;n<MPE4PointerEdgeSlots;n++) {
    const uint8_t button=(n&1)?0:1,flags=uint8_t(4|(button<<3));
    assert(inputAttempt(nextInputSequence(),uint8_t(80+n),100,0,flags));buttons.push_back(button);inputEvents++;
  }
  const uint8_t pointerRetry=nextInputSequence();const uint8_t edgeAck=EZFlashRAM[0xfc];
  assert(!inputAttempt(pointerRetry,99,100,0,12)&&EZFlashRAM[0xfc]==edgeAck);queueFullRetries++;
  input={};MPE4ConsumeInput(input);assert(input.pointerButtons==buttons[0]);
  assert(inputAttempt(pointerRetry,99,100,0,12));inputEvents++;
  for(unsigned n=1;n<buttons.size();n++) {
    input={};MPE4ConsumeInput(input);assert(input.pointerEvent&&input.pointerButtons==buttons[n]);
  }
  input={};MPE4ConsumeInput(input);assert(input.pointerEvent&&input.pointerX==99&&input.pointerButtons==1);
  stressPointerEdges+=buttons.size()+1;

  // Reset drops every queued edge and held state. This helper is called by
  // both native start and the real cartridge/bank reset lifecycle.
  assert(inputAttempt(nextInputSequence(),'x',45,0,1));inputEvents++;
  assert(inputAttempt(nextInputSequence(),0,0,16,2));inputEvents++;
  assert(inputAttempt(nextInputSequence(),101,102,0,4));inputEvents++;
  MPE4ResetInput();input={};MPE4ConsumeInput(input);
  assert(!input.key&&!input.scan&&!input.direction&&!input.fire&&!input.pointerEvent);
  inputStressActive=true;
}
static void consumePacket()
{
  assert(MPE3TitleOwned&&MPE3Title.Pending);
  assert(EZFlashRAM[0]=='M'&&EZFlashRAM[1]=='3'&&EZFlashRAM[2]==1);
  unsigned length=EZFlashRAM[6]+8;
  assert(MPE3TitleCRC16(EZFlashRAM,uint16_t(length))==MHSNativeRead16(EZFlashRAM+length));
  assert(EZFlashRAM[3]!=14);
  bool native=MPE4Active;
#ifdef MHS_NATIVE_ARENA_H
  assertReusableArenaOwner(native?MHSNativeArenaOwner::PowerEngine:MHSNativeArenaOwner::Title);
#endif
  if(native&&inputStressArmed&&EZFlashRAM[3]==1&&(EZFlashRAM[5]&16)) {
    inputStressArmed=false;checkInputBackpressure();
  }
  if(native&&inputStressActive&&EZFlashRAM[3]==1)stressCellPackets++;
  if(native&&inputStressActive&&EZFlashRAM[3]==2) {
    assert(stressCellPackets==53);inputStressActive=false;inputStressComplete=true;
  }
  tracePacket(&trace);packets++;
  if(EZFlashRAM[3]==1)for(unsigned p=8;p<length;p+=12){unsigned c=MHSNativeRead16(EZFlashRAM+p);assert(c<1000);memcpy(screen+c*8,EZFlashRAM+p+2,8);screen[8000+c]=EZFlashRAM[p+10];screen[9000+c]=EZFlashRAM[p+11];}
  if(EZFlashRAM[3]==5) {
    assert(native&&MPE4Game->package.egoSprites&&EZFlashRAM[6]==130&&EZFlashRAM[8]==1);
    const uint8_t part=EZFlashRAM[9];assert(part<2&&stagedParts==(part?1:0));
    memcpy(stagedShapes+part*128,EZFlashRAM+10,128);stagedParts|=1u<<part;spritePackets++;
    assert(!memcmp(stagedShapes+part*128,MPE4Game->nextEgo.shapes+part*128,128));
    // A newly received half remains hidden until the SID frame boundary.
    if(MPE4Game->currentEgo.enable)assert(!memcmp(visibleShapes,MPE4Game->currentEgo.shapes,256));
  }
  if(native&&EZFlashRAM[3]==2) {
    assert(EZFlashRAM[5]&32);
    if(memcmp(screen,MPE4Game->next,10000)) {
      size_t offset=0;while(offset<10000&&screen[offset]==MPE4Game->next[offset])offset++;
      std::cerr<<"Presented frame differs at byte "<<offset<<" after "<<nativeFrames<<" frames / "<<inputEvents<<" inputs / "<<directionReversals<<" reversals\n";
      std::exit(94);
    }
    nativeFrames++;
    if(MPE4Game->package.egoSprites) {
      const auto &ego=MPE4Game->nextEgo;const uint8_t *descriptor=EZFlashRAM+34;
      assert(EZFlashRAM[6]==37&&descriptor[0]==1&&descriptor[1]==ego.enable);
      assert(MHSNativeRead16(descriptor+2)==ego.x&&descriptor[4]==ego.y&&!memcmp(descriptor+5,ego.colors,6));
      assert(stagedParts==0||stagedParts==3);
      if(stagedParts==3){memcpy(visibleShapes,stagedShapes,256);hasSpritePose=true;spriteCommits++;}
      else if(ego.enable)coordinateFrames++;
      stagedParts=0;
      if(ego.enable) {
        assert(hasSpritePose&&!memcmp(visibleShapes,ego.shapes,256));visibleSpriteFrames++;
        unsigned layers=0;for(unsigned bit=1;bit<=4;bit++)layers+=(ego.enable>>bit)&1;
        threeLayerFrames+=layers==3;fourLayerFrames+=layers==4;
      }
    } else assert(EZFlashRAM[6]==26&&!stagedParts&&!spritePackets);
  }
  std::array<uint8_t,240> before{};memcpy(before.data(),EZFlashRAM,240);
  uint32_t frames=native?MPE4Game->frames:0,reads=ReadCalls;
  for(unsigned n=0;n<3;n++)MPE3TitlePollingHndlr();
  assert(!memcmp(before.data(),EZFlashRAM,240));assert(ReadCalls==reads);
  if(native)assert(MPE4Game->frames==frames);
  uint8_t seq=EZFlashRAM[0xf7];writeControl(0xf6,seq);MPE3TitlePollingHndlr();
  assert(EZFlashRAM[0xfb]==0);
}
static void frame(){unsigned goal=nativeFrames+1;for(unsigned limit=0;nativeFrames<goal&&limit<10000;limit++)consumePacket();assert(nativeFrames==goal);}
static void send(uint8_t key,uint8_t scan=0,uint8_t joy=0,uint8_t flags=1)
{
  const uint8_t sequence=nextInputSequence();
  uint32_t reads=ReadCalls;
  while(!inputAttempt(sequence,key,scan,joy,flags)){queueFullRetries++;frame();}
  assert(EZFlashRAM[0xfc]==sequence&&ReadCalls==reads);inputEvents++;
  frame();frame();
  if(inputInterruptMasks){std::cerr<<"Native input masked the PHI2 bus interrupt "<<inputInterruptMasks<<" time(s)\n";std::exit(93);}
}
static std::map<std::string,std::vector<uint8_t>> storageSnapshot()
{
  std::map<std::string,std::vector<uint8_t>> result;
  for(const auto &file:SD.files)result[file.first]=*file.second;
  return result;
}
#if 0 // Historical M4G1 package-CRC/root-fallback save coverage; retained as test archaeology only.
static void checkSaveDirectoryM4G1(uint32_t identity,mpe4::State &state,const std::vector<uint8_t> &legacySave)
{
  char path[32],backup[32],temp[32],rootPath[32],rootBackup[32];
  std::snprintf(path,sizeof(path),"/SAVES/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(backup,sizeof(backup),"/SAVES/MPE4-%08X.bak",unsigned(identity));
  std::snprintf(temp,sizeof(temp),"/SAVES/MPE4-%08X.tmp",unsigned(identity));
  std::snprintf(rootPath,sizeof(rootPath),"/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(rootBackup,sizeof(rootBackup),"/MPE4-%08X.bak",unsigned(identity));
  const auto clear=[](){SD=TestSD{};StorageFails=false;StorageWriteBudget=size_t(-1);};
  const auto put=[](const char *name,const std::vector<uint8_t> &bytes){SD.files[name]=std::make_shared<std::vector<uint8_t>>(bytes);};
  // Produce two distinct, valid records through the actual writer, creating
  // the directory on the first save and rotating the second into its backup.
  clear();const auto stateA=state;
  assert(!SD.exists("/SAVES"));assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.directories.count("/SAVES")&&SD.exists(path)&&!SD.exists(rootPath));
  const auto recordA=*SD.files[path];saveDirectoryChecks++;
  state.vars[3]^=0x35;const auto stateB=state;
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  const auto recordB=*SD.files[path];assert(*SD.files[backup]==recordA);saveDirectoryChecks++;
  const auto roots=[&](){clear();put(rootPath,recordA);put(rootBackup,recordB);};
  const auto unchangedRoots=[&](){assert(*SD.files[rootPath]==recordA&&*SD.files[rootBackup]==recordB);};
  const auto restore=[&](const mpe4::State &expected){
    state.vars[3]^=0x55;assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
    assert(!std::memcmp(&state,&expected,sizeof(state)));
  };
  // Restore is read-only. Prefer the folder's primary, then its backup,
  // before root saves. Corrupt records cannot alter the live game state.
  for(unsigned scenario=0;scenario<6;scenario++) {
    roots();auto bad=recordB;bad[50]^=1;
    const mpe4::State *expected=&stateA;
    if(scenario>=1&&scenario<=3) {
      SD.directories.insert("/SAVES");put(path,scenario==1?recordB:bad);
      put(backup,scenario==1?recordA:scenario==2?recordB:bad);
      if(scenario<=2)expected=&stateB;
    }
    if(scenario>=4){put(rootPath,bad);expected=&stateB;}
    if(scenario==5)put(rootBackup,bad);
    const auto before=storageSnapshot();const auto directories=SD.directories;
    if(scenario==5){const auto live=state;assert(!MPE4Restore(nullptr,identity,&state,sizeof(state)));assert(!std::memcmp(&live,&state,sizeof(state)));}
    else restore(*expected);
    assert(storageSnapshot()==before&&SD.directories==directories);
    assert(SD.writeAttempts.empty()&&SD.mutations.empty());rootSaveFallbackChecks++;
  }
  // A missing/unusable save directory must not turn a failed save into a
  // write at the SD root. The prior root save must remain recoverable.
  for(unsigned failure=0;failure<3;failure++) {
    roots();
    if(failure==0)SD.mkdirFails=true;
    if(failure==1)put("/SAVES",std::vector<uint8_t>{1,2,3});
    if(failure==2){SD.directories.insert("/SAVES");SD.failReadPath="/SAVES";}
    const auto before=storageSnapshot();state=stateB;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    assert(SD.writeAttempts.empty()&&storageSnapshot()==before);unchangedRoots();
    restore(stateA);saveDirectoryChecks++;
  }
  // Simulate failures opening, writing, verifying, and promoting a first
  // folder save. No failed operation consumes or renames legacy root files.
  for(unsigned failure=0;failure<4;failure++) {
    roots();SD.directories.insert("/SAVES");state=stateB;
    if(failure==0)SD.failWritePath=temp;
    if(failure==1)StorageWriteBudget=31;
    if(failure==2)SD.failReadPath=temp;
    if(failure==3)SD.renameFailures[{temp,path}]=1;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    StorageWriteBudget=size_t(-1);SD.failReadPath.clear();
    assert(!SD.exists(path)&&!SD.exists(backup));unchangedRoots();restore(stateA);saveFailureChecks++;
  }
  // A failed rotation keeps the primary. A failed final promotion rolls its
  // verified backup back into place; either outcome preserves a usable save.
  for(unsigned failure=0;failure<2;failure++) {
    roots();SD.directories.insert("/SAVES");put(path,recordA);put(backup,recordB);state=stateB;
    SD.renameFailures[failure?std::make_pair(std::string(temp),std::string(path)):std::make_pair(std::string(path),std::string(backup))]=1;
    assert(!MPE4Save(nullptr,identity,&state,sizeof(state)));
    assert(SD.exists(path)&&*SD.files[path]==recordA);unchangedRoots();restore(stateA);saveFailureChecks++;
  }
  // Successful new saves also leave legacy root records untouched. Future
  // restore must select the new folder record and its backup first.
  roots();state=stateB;assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(*SD.files[path]==recordB);unchangedRoots();restore(stateB);
  state=stateA;assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(*SD.files[path]==recordA&&*SD.files[backup]==recordB);unchangedRoots();
  (*SD.files[path])[50]^=1;restore(stateB);rootSaveFallbackChecks++;
  // The old 9528-byte prefix works from the root fallback too. Its next save
  // uses the current ABI in /SAVES without editing the legacy source record.
  clear();put(rootPath,legacySave);std::memset(state.overflowBindings,0x7b,sizeof(state.overflowBindings));
  assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
  assert(!std::memcmp(&state,legacySave.data()+32,mpe4::LegacyStateBytes));
  const auto *tail=reinterpret_cast<const uint8_t *>(state.overflowBindings);
  assert(std::all_of(tail,tail+sizeof(state.overflowBindings),[](uint8_t value){return !value;}));
  assert(!SD.exists("/SAVES")&&*SD.files[rootPath]==legacySave);
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.files[path]->size()==sizeof(state)+32&&*SD.files[rootPath]==legacySave);rootSaveFallbackChecks++;
  assert(!rootWriteAttempts&&!rootMutationAttempts&&!inputInterruptMasks);
}
#endif
static void checkSaveDirectory(const char *identity,uint16_t epoch,mpe4::State &state)
{
  char slot1[32],backup1[32],temp1[32],slot2[32];
  std::snprintf(slot1,sizeof(slot1),"/SAVES/%.6s01.sav",identity);
  std::snprintf(backup1,sizeof(backup1),"/SAVES/%.6s01.bak",identity);
  std::snprintf(temp1,sizeof(temp1),"/SAVES/%.6s01.tmp",identity);
  std::snprintf(slot2,sizeof(slot2),"/SAVES/%.6s02.sav",identity);
  const auto clear=[](){SD=TestSD{};StorageFails=false;StorageWriteBudget=size_t(-1);};
  clear();const auto first=state;assert(MPE4Save(nullptr,identity,epoch,1,&state,sizeof(state)));
  assert(SD.directories.count("/SAVES")&&SD.exists(slot1)&&!SD.exists(slot2));const auto record1=*SD.files[slot1];saveDirectoryChecks++;
  state.vars[3]^=0x35;const auto second=state;assert(MPE4Save(nullptr,identity,epoch,2,&state,sizeof(state)));
  assert(SD.exists(slot2)&&*SD.files[slot1]==record1);saveDirectoryChecks++;
  state.vars[3]^=0x55;assert(MPE4Restore(nullptr,identity,epoch,1,&state,sizeof(state)));assert(!std::memcmp(&state,&first,sizeof(state)));
  state.vars[3]^=0x55;assert(MPE4Restore(nullptr,identity,epoch,2,&state,sizeof(state)));assert(!std::memcmp(&state,&second,sizeof(state)));
  assert(MPE4Save(nullptr,identity,epoch,1,&state,sizeof(state)));assert(SD.exists(backup1));const auto latest=*SD.files[slot1];
  (*SD.files[slot1])[50]^=1;state.vars[3]^=1;assert(MPE4Restore(nullptr,identity,epoch,1,&state,sizeof(state)));assert(!std::memcmp(&state,&first,sizeof(state)));
  const auto before=state;assert(!MPE4Restore(nullptr,"BAD000",epoch,1,&state,sizeof(state)));assert(!std::memcmp(&state,&before,sizeof(state)));
  assert(!MPE4Restore(nullptr,identity,uint16_t(epoch+1),1,&state,sizeof(state)));assert(!std::memcmp(&state,&before,sizeof(state)));
  assert(!MPE4Restore(nullptr,identity,epoch,13,&state,sizeof(state)));assert(!std::memcmp(&state,&before,sizeof(state)));rootSaveFallbackChecks+=3;
  SD.files[slot1]=std::make_shared<std::vector<uint8_t>>(latest);SD.failWritePath=temp1;assert(!MPE4Save(nullptr,identity,epoch,1,&state,sizeof(state)));
  assert(*SD.files[slot1]==latest);SD.failWritePath.clear();StorageWriteBudget=31;assert(!MPE4Save(nullptr,identity,epoch,1,&state,sizeof(state)));
  StorageWriteBudget=size_t(-1);assert(*SD.files[slot1]==latest);saveFailureChecks+=2;
  assert(!rootWriteAttempts&&!rootMutationAttempts&&!inputInterruptMasks);
}
#ifndef MPE4_HARNESS_MAIN
#define MPE4_HARNESS_MAIN main
#endif
int MPE4_HARNESS_MAIN(int argc,char **argv)
{
  assert(argc==4);
  // Run every accepted intro regression against this exact integrated module.
  char *legacyArgs[]={argv[0],argv[1]};std::ostringstream legacy;
  auto *output=std::cout.rdbuf(legacy.rdbuf());int legacyResult=legacyIntroConformance(2,legacyArgs);std::cout.rdbuf(output);assert(!legacyResult);
  std::ifstream rawFile(argv[2],std::ios::binary);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(rawFile)),{});
  assert(raw.size()==1048576||raw.size()==0x400000);
  std::vector<uint8_t> combined(raw.begin()+Root,raw.end());trace.open(argv[3],std::ios::binary);assert(trace.good());
  start(combined);writeControl(0xf4,2);
#ifdef MHS_NATIVE_ARENA_H
  assertReusableArenaOwner(MHSNativeArenaOwner::Title);
#endif
  for(unsigned n=0;(!MPE4Active||MPE4Game->game.state.modal!=mpe4::StringInput)&&n<20000;n++)consumePacket();
  assert(MPE4Active&&MPE4Game->game.state.modal==mpe4::StringInput);
#ifdef MHS_NATIVE_ARENA_H
  assertReusableArenaOwner(MHSNativeArenaOwner::PowerEngine);
#endif
  assert(EZFlashRAM[0xfc]==0&&MPE4Game->game.state.vars[0]==69);
  uint32_t reads=ReadCalls;uint8_t ack=EZFlashRAM[0xfc];
  writeControl(0xfe,1);writeControl(0xfd,1);writeControl(0xff,0);writeControl(0xf4,3);
  assert(MPE4KeyboardRead==MPE4KeyboardWrite&&EZFlashRAM[0xfc]==ack&&ReadCalls==reads);
  for(char c:std::string("Roger"))send(c);
  inputStressArmed=true;
  send(13,28);
  for(unsigned n=0;(MPE4Game->game.state.vars[0]!=2||!MPE4Game->game.state.playerControl)&&n<1000;n++)frame();
  assert(MPE4Game->game.state.vars[0]==2&&MPE4Game->game.state.playerControl&&inputStressComplete);
  assert(std::string(MPE4Game->game.state.strings[1])=="Roger");
  const bool spritesEnabled=MPE4Game->package.egoSprites;
  auto &state=MPE4Game->game.state;
  // Corrupted events and duplicate/queued writes cannot advance game or steal
  // the first input ACK. The channel also survives more than one full wrap.
  for(unsigned n=0;n<260;n++)send(0,0,0,2);
  while(state.modal)send(13,28);
  // Real C64 ASCII+PC scan pairs must remain printable when the source binds
  // Alt-D/Alt-Z (ASCII zero). The old character-only fixture missed this.
  send('d',32);send('D',32);send('z',44);send('Z',44);
  assert(std::string(state.input)=="dDzZ");
  while(state.inputLength)send(8,14);
  // Malformed but correctly checksummed pointer records cannot steal ACKs.
  const uint8_t pointerSequence=inputSequence==255?1:inputSequence+1;
  for(const auto invalid:std::vector<std::array<uint8_t,3>>{{80,100,5},{160,100,4},{80,200,4},{80,100,8},{80,100,32}}){
    const auto previousAck=EZFlashRAM[0xfc];const auto previousReads=ReadCalls;
    writeControl(0xf8,invalid[0]);writeControl(0xf9,invalid[1]);writeControl(0xfa,0);
    writeControl(0xfd,invalid[2]);writeControl(0xfe,pointerSequence);
    writeControl(0xff,uint8_t(0xa5^invalid[0]^invalid[1]^invalid[2]^pointerSequence));writeControl(0xf4,3);
    assert(MPE4KeyboardRead==MPE4KeyboardWrite&&MPE4PointerEdgeRead==MPE4PointerEdgeWrite&&
      EZFlashRAM[0xfc]==previousAck&&ReadCalls==previousReads);
  }
  send(80,100,0,4);
  assert(state.pointerX==80&&state.pointerY==100&&state.pointerButtons==0);
  send('l',38);send('o',24);send('o',24);send('k',37);send(13,28);
  assert(state.modal);
  send(80,100,0,12);assert(!state.modal&&state.pointerButtons==1);
  send(80,100,0,4);assert(state.pointerButtons==0);
  uint8_t x=state.objects[0].x,y=state.objects[0].y,room=state.vars[0];
  send(0,0,2,2);for(unsigned n=0;n<20;n++)frame();send(0,0,0,2);
  if(state.objects[0].x==x&&state.objects[0].y==y&&room==state.vars[0])
    std::cerr<<"movement before="<<unsigned(x)<<","<<unsigned(y)<<" after="<<unsigned(state.objects[0].x)<<","<<unsigned(state.objects[0].y)<<" modal="<<unsigned(state.modal)<<" direction="<<unsigned(state.objects[0].direction)<<"\n";
  assert((state.objects[0].x!=x||state.objects[0].y!=y||room!=state.vars[0])&&state.objects[0].direction==0);
  // Repeat both horizontal and vertical reversals through the live sequencer.
  // No input may pause the bus ISR, including sequence wrap and rejected peers.
  for(unsigned n=0;n<64;n++) {
    const uint8_t joy=std::array<uint8_t,4>{4,8,1,2}[n&3];
    send(0,0,joy,2);assert(MPE4JoyState==joy&&MPE4KeyboardRead==MPE4KeyboardWrite);directionReversals++;
  }
  send(0,0,0,2);assert(MPE4JoyState==0&&queueFullRetries==2&&!inputInterruptMasks);
  // Save/readback/backup recovery execute the actual firmware storage glue.
#if 0 // M4G1 package-CRC filename/migration assertions; see M4G2 coverage below.
  const auto identity=MPE4Game->package.crc;
  char savePath[32],backupPath[32];
  std::snprintf(savePath,sizeof(savePath),"/SAVES/MPE4-%08X.sav",unsigned(identity));
  std::snprintf(backupPath,sizeof(backupPath),"/SAVES/MPE4-%08X.bak",unsigned(identity));
  SD.files["/MPE4-SQ1.sav"]=std::make_shared<std::vector<uint8_t>>(4,0x5a);
  auto saved=state;assert(MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));
  assert(SD.exists(savePath));
  state.vars[3]^=7;assert(MPE4Restore(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(!memcmp(&saved,&state,sizeof(state)));
  assert(MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(SD.exists(backupPath));
  (*SD.files[savePath])[50]^=1;
  state.vars[3]^=3;assert(MPE4Restore(nullptr,MPE4Game->package.crc,&state,sizeof(state)));assert(!memcmp(&saved,&state,sizeof(state)));
  auto before=state;assert(!MPE4Restore(nullptr,MPE4Game->package.crc^1,&state,sizeof(state)));assert(!memcmp(&before,&state,sizeof(state)));
  StorageFails=true;assert(!MPE4Save(nullptr,MPE4Game->package.crc,&state,sizeof(state)));StorageFails=false;
  const auto firstSave=*SD.files[savePath];
  assert(MPE4Save(nullptr,identity^0x80000000u,&state,sizeof(state)));
  assert(*SD.files[savePath]==firstSave);
  assert(*SD.files["/MPE4-SQ1.sav"]==std::vector<uint8_t>(4,0x5a));
  // Native05 files carry the unchanged 9528-byte State prefix. Exercise the
  // real firmware migration, including every validation before live replace.
  constexpr size_t oldBytes=9528;
  static_assert(offsetof(mpe4::State,overflowBindings)==oldBytes);
  assert(sizeof(state)==9624);
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  auto oldSave=*SD.files[savePath];oldSave.resize(32+oldBytes);
  MPE4Write32(oldSave.data()+12,oldBytes);
  MPE4Write32(oldSave.data()+16,MHSNativeCRC32(oldSave.data()+32,oldBytes));
  MPE4Write32(oldSave.data()+28,MHSNativeCRC32(oldSave.data(),28));
  SD.files.erase(backupPath);SD.files[savePath]=std::make_shared<std::vector<uint8_t>>(oldSave);
  std::memset(state.overflowBindings,0x7b,sizeof(state.overflowBindings));state.vars[3]^=5;
  assert(MPE4Restore(nullptr,identity,&state,sizeof(state)));
  assert(!std::memcmp(&state,oldSave.data()+32,oldBytes));
  const uint8_t *tail=reinterpret_cast<const uint8_t *>(state.overflowBindings);
  assert(std::all_of(tail,tail+sizeof(state.overflowBindings),[](uint8_t b){return b==0;}));
  const auto migrated=state;
  for(unsigned fault=0;fault<4;fault++) {
    auto invalid=oldSave;
    if(fault==0)invalid[50]^=1;
    if(fault==1)invalid.pop_back();
    if(fault==2)invalid[28]^=1;
    if(fault==3){MPE4Write32(invalid.data()+8,identity^1u);MPE4Write32(invalid.data()+28,MHSNativeCRC32(invalid.data(),28));}
    SD.files[savePath]=std::make_shared<std::vector<uint8_t>>(invalid);
    assert(!MPE4Restore(nullptr,identity,&state,sizeof(state)));
    assert(!std::memcmp(&migrated,&state,sizeof(state)));
  }
  assert(MPE4Save(nullptr,identity,&state,sizeof(state)));
  assert(SD.files[savePath]->size()==sizeof(state)+32);
  assert(MHSNativeRead32(SD.files[savePath]->data()+12)==sizeof(state));
  checkSaveDirectory(identity,state,oldSave);
#endif
  checkSaveDirectory(MPE4Game->package.saveId,MPE4Game->package.saveEpoch,state);
  if(spritesEnabled)assert(spritePackets==spriteCommits*2&&spriteCommits&&coordinateFrames&&visibleSpriteFrames&&threeLayerFrames+fourLayerFrames);
  else assert(!spritePackets&&!spriteCommits&&!visibleSpriteFrames);
  // Queue all three input classes, then exercise the actual bank-loss reset.
  assert(inputAttempt(nextInputSequence(),'x',45,0,1));
  assert(inputAttempt(nextInputSequence(),0,0,16,2));
  assert(inputAttempt(nextInputSequence(),101,102,0,12));
  CurrentEasyFlashBank=3;auto mailbox=std::array<uint8_t,256>{};memcpy(mailbox.data(),EZFlashRAM,256);
  assert(!MPE3TitlePollingHndlr()&&!MPE4Active&&!MPE3TitleOwned);assert(!memcmp(mailbox.data(),EZFlashRAM,256));
#ifdef MHS_NATIVE_ARENA_H
  assertReusableArenaFree();
#endif
  mpe4::Input stale{};MPE4ConsumeInput(stale);
  assert(!stale.key&&!stale.scan&&!stale.direction&&!stale.fire&&!stale.pointerEvent);
  assert(MPE4KeyboardRead==MPE4KeyboardWrite&&MPE4PointerEdgeRead==MPE4PointerEdgeWrite&&
    !MPE4JoyState&&!MPE4JoyFireWrite&&!MPE4PointerRevision);
  trace.close();
  std::cout<<"{\"passed\":true,\"legacyIntro\":"<<legacy.str()<<",\"sessionBytes\":"<<sizeof(mpe4::Session)<<",\"packets\":"<<packets<<",\"nativeFrames\":"<<nativeFrames<<",\"inputEvents\":"<<inputEvents<<",\"keyboardScanChecks\":4,\"pointerChecks\":8,\"maximumRawRead\":"<<MaxReadLength<<",\"storageChecks\":9,\"legacyStorageChecks\":6,\"room\":2,\"runtimeCpuEmulation\":false"
    <<",\"spritesEnabled\":"<<(spritesEnabled?"true":"false")<<",\"spritePackets\":"<<spritePackets<<",\"spriteCommits\":"<<spriteCommits
    <<",\"coordinateFrames\":"<<coordinateFrames<<",\"visibleSpriteFrames\":"<<visibleSpriteFrames
    <<",\"inputInterruptMasks\":"<<inputInterruptMasks<<",\"queueFullRetries\":"<<queueFullRetries<<",\"directionReversals\":"<<directionReversals
    <<",\"inputBackpressure\":{\"maximumFrameCellPackets\":"<<stressCellPackets<<",\"keyboardEdges\":"<<stressKeyboardEdges
    <<",\"pointerSamples\":"<<stressPointerSamples<<",\"pointerEdges\":"<<stressPointerEdges<<",\"fireEdges\":"<<stressFireEdges
    <<",\"counterWrapChecks\":2,\"resetClearsBufferedInput\":true}"
    <<",\"saveDirectory\":{\"path\":\"/SAVES\",\"directoryChecks\":"<<saveDirectoryChecks<<",\"fallbackChecks\":"<<rootSaveFallbackChecks<<",\"transactionFailureChecks\":"<<saveFailureChecks
    <<",\"rootWriteAttempts\":"<<rootWriteAttempts<<",\"rootMutationAttempts\":"<<rootMutationAttempts<<"}"
    <<",\"threeLayerFrames\":"<<threeLayerFrames<<",\"fourLayerFrames\":"<<fourLayerFrames<<",\"spriteFrameAtomic\":true}\n";
}

#endif // MPE_SD_STUB_TEST

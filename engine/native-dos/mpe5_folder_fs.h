#ifndef MPE5_FOLDER_FS_H
#define MPE5_FOLDER_FS_H

#include <string.h>
#include "mpe5_redirector.h"

namespace mpe5 {

// SdFat is supplied by the firmware before this header. FsFile stores its
// handle inline; the complete adapter can live in the reset-only RAM1 arena.
// Never use Arduino File here: it allocates handles from the RAM2 heap.
class FolderFilesystem {
 public:
  static constexpr uint8_t HandleCount = 16;
  static constexpr uint32_t MaxExtensionBytes = 64u * 1024u;
  static constexpr uint16_t MaxDirectoryEntries = 1024;
  using IoHook = void (*)(void *);

  explicit FolderFilesystem(IoHook hook=nullptr, void *context=nullptr)
      : hook_(hook), hookContext_(context) {}

  // Call before guest RAM takes over RAM2. Counting free clusters may scan
  // the FAT once; DOS space queries subsequently use this conservative cache.
  MPE5_CODE bool begin(bool createRoot=true);

  MPE5_CODE void end();

  MPE5_CODE RedirectorHost host();

 private:
  static constexpr uint16_t PathBytes=Redirector::PathBytes+8;
  enum : uint16_t { Okay=0, InvalidFunction=1, FileNotFound=2, PathNotFound=3,
    TooManyFiles=4, AccessDenied=5, InvalidHandle=6, InvalidAccess=12,
    NoMoreFiles=18, NotReady=21, SeekError=25, WriteFault=29, ReadFault=30,
    SharingViolation=32, FileExists=80, DiskFull=112 };
  struct Slot { FsFile file; char path[PathBytes]{};uint8_t mode=0;bool used=false; };
  Slot slots_[HandleCount];
  FsFile enumDirectory_;
  char enumPath_[PathBytes]{};
  uint16_t enumOrdinal_=0,enumScanned_=0;
  uint32_t totalSectors_=0,freeSectors_=0,sectorsPerCluster_=0;
  bool ready_=false;
  IoHook hook_=nullptr;
  void *hookContext_=nullptr;
  uint8_t zeros_[512]{};

  static FolderFilesystem &self(void *p){return *static_cast<FolderFilesystem *>(p);}
  // Explicit callbacks keep their entry points in flash. A lambda's generated
  // function-pointer thunk otherwise ends up in ITCM, consuming the DOS stack.
  static MPE5_CODE uint16_t statCallback(void *p,const char *s,RedirectorFileInfo &i);
  static MPE5_CODE uint16_t enumerateCallback(void *p,const char *s,uint16_t n,RedirectorFileInfo &i);
  static MPE5_CODE uint16_t openCallback(void *p,uint8_t n,const char *s,uint16_t m,uint16_t a,uint8_t t,RedirectorFileInfo &i,uint16_t &r);
  static MPE5_CODE uint16_t closeCallback(void *p,uint8_t n);
  static MPE5_CODE uint16_t readCallback(void *p,uint8_t n,uint32_t o,uint8_t *b,uint16_t c,uint16_t &a);
  static MPE5_CODE uint16_t writeCallback(void *p,uint8_t n,uint32_t o,const uint8_t *b,uint16_t c,uint16_t &a);
  static MPE5_CODE uint16_t truncateCallback(void *p,uint8_t n,uint32_t s);
  static MPE5_CODE uint16_t flushCallback(void *p,uint8_t n);
  static MPE5_CODE uint16_t timeCallback(void *p,uint8_t n,uint16_t t,uint16_t d);
  static MPE5_CODE uint16_t attributesCallback(void *p,const char *s,uint8_t a);
  static MPE5_CODE uint16_t mkdirCallback(void *p,const char *s);
  static MPE5_CODE uint16_t rmdirCallback(void *p,const char *s);
  static MPE5_CODE uint16_t removeCallback(void *p,const char *s);
  static MPE5_CODE uint16_t renameCallback(void *p,const char *a,const char *b);
  static MPE5_CODE uint16_t spaceCallback(void *p,uint32_t &a,uint32_t &b);
  void io(){if(hook_)hook_(hookContext_);}
  static char upper(char c){return c>='a'&&c<='z'?char(c-'a'+'A'):c;}
  static bool separator(char c){return c=='/'||c=='\\';}
  static bool validCharacter(char c) {
    c=upper(c);
    return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("$%'-_@~`!(){}^#&",c)!=nullptr;
  }
  // No LFN truncation or fabricated aliases: every exported name must be
  // an actual 8.3 component that SdFat can open again without ambiguity.
  static MPE5_CODE bool shortName(const char *source,char *out);
  static MPE5_CODE bool path(const char *source,char out[PathBytes]);
  static const char *leaf(const char *name){const char *p=strrchr(name,'/');return p?p+1:name;}
  static bool subtree(const char *parent,const char *child) {
    const size_t n=strlen(parent);return !strncmp(parent,child,n)&&(child[n]==0||child[n]=='/');
  }
  MPE5_CODE bool inUse(const char *name,bool children=false)const;
  MPE5_CODE void invalidateEnumeration();
  MPE5_CODE uint16_t info(FsFile &file,const char *name,RedirectorFileInfo &out);
  MPE5_CODE uint16_t stat(const char *name,RedirectorFileInfo &out);
  MPE5_CODE uint16_t enumerate(const char *name,uint16_t ordinal,RedirectorFileInfo &out);
  static uint8_t accessMask(uint8_t mode){return (mode&3)==0?1:(mode&3)==1?2:3;}
  static uint8_t denyMask(uint8_t mode) {
    switch((mode>>4)&7){case 0:return (mode&3)==0?2:3;case 1:return 3;case 2:return 2;case 3:return 1;default:return 0;}
  }
  MPE5_CODE uint16_t open(uint8_t index,const char *name,uint16_t mode,uint16_t action,uint8_t attributes,RedirectorFileInfo &out,uint16_t &result);
  MPE5_CODE uint16_t close(uint8_t index);
  MPE5_CODE uint16_t read(uint8_t index,uint32_t offset,uint8_t *buffer,uint16_t requested,uint16_t &actual);
  MPE5_CODE void account(uint32_t before,uint32_t after);
  MPE5_CODE uint16_t extend(Slot &slot,uint32_t length);
  MPE5_CODE uint16_t write(uint8_t index,uint32_t offset,const uint8_t *buffer,uint16_t requested,uint16_t &actual);
  MPE5_CODE uint16_t truncate(uint8_t index,uint32_t size);
  MPE5_CODE uint16_t flush(uint8_t index);
  MPE5_CODE uint16_t setTime(uint8_t index,uint16_t time,uint16_t date);
  MPE5_CODE uint16_t setAttributes(const char *name,uint8_t attributes);
  MPE5_CODE uint16_t mkdir(const char *name);
  MPE5_CODE uint16_t rmdir(const char *name);
  MPE5_CODE uint16_t remove(const char *name);
  MPE5_CODE uint16_t rename(const char *from,const char *to);
  MPE5_CODE uint16_t space(uint32_t &total,uint32_t &available);
};

// Definitions deliberately live outside the class: inline COMDAT functions
// cannot share Teensy's .flashmem section with the firmware's ordinary code.
MPE5_CODE bool FolderFilesystem::begin(bool createRoot) {
    end();
    io();
    FsFile root=SD.sdfs.open("/DOSVM/D",O_RDONLY);
    if(!root && createRoot) {
      io();
      if(!SD.sdfs.mkdir("/DOSVM/D",true))return false;
      io();root=SD.sdfs.open("/DOSVM/D",O_RDONLY);
    }
    if(!root || !root.isDirectory())return false;
    if(!root.close())return false;
    io();
    const uint32_t clusters=SD.sdfs.clusterCount();
    const uint32_t freeClusters=SD.sdfs.freeClusterCount();
    sectorsPerCluster_=SD.sdfs.sectorsPerCluster();
    if(!clusters || freeClusters==UINT32_MAX || freeClusters>clusters ||
       !sectorsPerCluster_)return false;
    const uint64_t total=uint64_t(clusters)*sectorsPerCluster_;
    const uint64_t available=uint64_t(freeClusters)*sectorsPerCluster_;
    totalSectors_=uint32_t(total>UINT32_MAX?UINT32_MAX:total);
    freeSectors_=uint32_t(available>totalSectors_?totalSectors_:available);
    ready_=true;
    return true;
  }

MPE5_CODE void FolderFilesystem::end() {
    for(uint8_t i=0;i<HandleCount;i++) {
      if(slots_[i].file)slots_[i].file.close();
      slots_[i].used=false;slots_[i].path[0]=0;
    }
    invalidateEnumeration();ready_=false;
  }

MPE5_CODE RedirectorHost FolderFilesystem::host() {
    RedirectorHost result{};result.context=this;
    result.stat=statCallback;result.enumerate=enumerateCallback;result.open=openCallback;
    result.close=closeCallback;result.read=readCallback;result.write=writeCallback;
    result.truncate=truncateCallback;result.flush=flushCallback;result.setTime=timeCallback;
    result.setAttributes=attributesCallback;result.mkdir=mkdirCallback;result.rmdir=rmdirCallback;
    result.remove=removeCallback;result.rename=renameCallback;result.space=spaceCallback;
    return result;
  }

MPE5_CODE uint16_t FolderFilesystem::statCallback(void *p,const char *s,RedirectorFileInfo &i) {return self(p).stat(s,i);}

MPE5_CODE uint16_t FolderFilesystem::enumerateCallback(void *p,const char *s,uint16_t n,RedirectorFileInfo &i) {return self(p).enumerate(s,n,i);}

MPE5_CODE uint16_t FolderFilesystem::openCallback(void *p,uint8_t n,const char *s,uint16_t m,uint16_t a,uint8_t t,RedirectorFileInfo &i,uint16_t &r) {return self(p).open(n,s,m,a,t,i,r);}

MPE5_CODE uint16_t FolderFilesystem::closeCallback(void *p,uint8_t n) {return self(p).close(n);}

MPE5_CODE uint16_t FolderFilesystem::readCallback(void *p,uint8_t n,uint32_t o,uint8_t *b,uint16_t c,uint16_t &a) {return self(p).read(n,o,b,c,a);}

MPE5_CODE uint16_t FolderFilesystem::writeCallback(void *p,uint8_t n,uint32_t o,const uint8_t *b,uint16_t c,uint16_t &a) {return self(p).write(n,o,b,c,a);}

MPE5_CODE uint16_t FolderFilesystem::truncateCallback(void *p,uint8_t n,uint32_t s) {return self(p).truncate(n,s);}

MPE5_CODE uint16_t FolderFilesystem::flushCallback(void *p,uint8_t n) {return self(p).flush(n);}

MPE5_CODE uint16_t FolderFilesystem::timeCallback(void *p,uint8_t n,uint16_t t,uint16_t d) {return self(p).setTime(n,t,d);}

MPE5_CODE uint16_t FolderFilesystem::attributesCallback(void *p,const char *s,uint8_t a) {return self(p).setAttributes(s,a);}

MPE5_CODE uint16_t FolderFilesystem::mkdirCallback(void *p,const char *s) {return self(p).mkdir(s);}

MPE5_CODE uint16_t FolderFilesystem::rmdirCallback(void *p,const char *s) {return self(p).rmdir(s);}

MPE5_CODE uint16_t FolderFilesystem::removeCallback(void *p,const char *s) {return self(p).remove(s);}

MPE5_CODE uint16_t FolderFilesystem::renameCallback(void *p,const char *a,const char *b) {return self(p).rename(a,b);}

MPE5_CODE uint16_t FolderFilesystem::spaceCallback(void *p,uint32_t &a,uint32_t &b) {return self(p).space(a,b);}

MPE5_CODE bool FolderFilesystem::shortName(const char *source,char *out) {
    size_t base=0,extension=0,n=0;bool dot=false;
    for(;source[n];n++) {
      if(n>=12)return false;
      const char c=upper(source[n]);
      if(c=='.') {if(dot||base==0)return false;dot=true;}
      else {if(!validCharacter(c))return false;if(dot){if(++extension>3)return false;}else if(++base>8)return false;}
      out[n]=c;
    }
    if(!base||(dot&&!extension))return false;
    out[n]=0;return true;
  }

MPE5_CODE bool FolderFilesystem::path(const char *source,char out[PathBytes]) {
    if(!source||!separator(source[0]))return false;
    memcpy(out,"/DOSVM/D",8);size_t written=8,index=1;
    while(index<Redirector::PathBytes && source[index]) {
      if(separator(source[index])){++index;continue;}
      char component[13]{},name[13]{};size_t n=0;
      while(index<Redirector::PathBytes && source[index]&&!separator(source[index])) {
        if(n>=12)return false;
        component[n++]=source[index++];
      }
      if(index==Redirector::PathBytes||!shortName(component,name)||written+n+1>=PathBytes)return false;
      out[written++]='/';memcpy(out+written,name,n);written+=n;
    }
    if(index==Redirector::PathBytes)return false;
    out[written]=0;return true;
  }

MPE5_CODE bool FolderFilesystem::inUse(const char *name,bool children)const {
    for(const auto &slot:slots_)if(slot.used&&(children?subtree(name,slot.path):!strcmp(name,slot.path)))return true;
    return false;
  }

MPE5_CODE void FolderFilesystem::invalidateEnumeration() {
    if(enumDirectory_)enumDirectory_.close();
    enumPath_[0]=0;enumOrdinal_=enumScanned_=0;
  }

MPE5_CODE uint16_t FolderFilesystem::info(FsFile &file,const char *name,RedirectorFileInfo &out) {
    out=RedirectorFileInfo{};
    if(strcmp(name,"/DOSVM/D") && !shortName(leaf(name),out.name))return AccessDenied;
    if(file.fileSize()>UINT32_MAX)return AccessDenied;
    out.size=file.isDirectory()?0:uint32_t(file.fileSize());
    out.attributes=uint8_t((file.isDirectory()?0x10:0x20)|(file.isReadOnly()?1:0)|(file.isHidden()?2:0));
    io();
    if(!file.getModifyDateTime(&out.date,&out.time))return ReadFault;
    return Okay;
  }

MPE5_CODE uint16_t FolderFilesystem::stat(const char *name,RedirectorFileInfo &out) {
    if(!ready_)return NotReady;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    io();FsFile file=SD.sdfs.open(full,O_RDONLY);
    if(!file)return FileNotFound;
    const uint16_t error=info(file,full,out);
    if(!file.close()&&!error)return ReadFault;
    return error;
  }

MPE5_CODE uint16_t FolderFilesystem::enumerate(const char *name,uint16_t ordinal,RedirectorFileInfo &out) {
    if(!ready_)return NotReady;
    if(ordinal>=MaxDirectoryEntries)return NoMoreFiles;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    if(!enumDirectory_||strcmp(enumPath_,full)||ordinal!=enumOrdinal_) {
      invalidateEnumeration();io();enumDirectory_=SD.sdfs.open(full,O_RDONLY);
      if(!enumDirectory_||!enumDirectory_.isDirectory()){invalidateEnumeration();return PathNotFound;}
      strcpy(enumPath_,full);
    }
    while(enumScanned_<MaxDirectoryEntries) {
      io();FsFile entry;
      if(!entry.openNext(&enumDirectory_,O_RDONLY)) {
        const uint16_t error=enumDirectory_.getError()?ReadFault:NoMoreFiles;
        invalidateEnumeration();return error;
      }
      ++enumScanned_;
      char original[256]{},shortened[13]{};
      const size_t count=entry.getName(original,sizeof original);
      if(!count){entry.close();invalidateEnumeration();return ReadFault;}
      if(count>=sizeof original-1||!shortName(original,shortened)){entry.close();continue;}
      if(enumOrdinal_++<ordinal){entry.close();continue;}
      const uint16_t error=info(entry,shortened,out);
      if(!entry.close()&&!error)return ReadFault;
      return error;
    }
    invalidateEnumeration();return NoMoreFiles;
  }

MPE5_CODE uint16_t FolderFilesystem::open(uint8_t index,const char *name,uint16_t mode,uint16_t action,uint8_t attributes,RedirectorFileInfo &out,uint16_t &result) {
    result=0;if(!ready_)return NotReady;
    if(index>=HandleCount)return TooManyFiles;
    if(slots_[index].used)return InvalidHandle;
    if((mode&3)>2||((mode>>4)&7)>4)return InvalidAccess;
    if(action!=0x01&&action!=0x10&&action!=0x11&&action!=0x12)return InvalidFunction;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    if(!strcmp(full,"/DOSVM/D"))return AccessDenied;
    for(const auto &slot:slots_)if(slot.used&&!strcmp(slot.path,full))
      if((denyMask(slot.mode)&accessMask(uint8_t(mode)))||(denyMask(uint8_t(mode))&accessMask(slot.mode)))return SharingViolation;
    io();FsFile probe=SD.sdfs.open(full,O_RDONLY);const bool exists=bool(probe);
    uint32_t oldSize=0;
    if(exists) {
      if(probe.isDirectory()||probe.fileSize()>UINT32_MAX){probe.close();return AccessDenied;}
      oldSize=uint32_t(probe.fileSize());
      if((mode&3)!=0&&probe.isReadOnly()){probe.close();return AccessDenied;}
      if(!probe.close())return ReadFault;
      if(action==0x10)return FileExists;
    }else if(action==0x01)return FileNotFound;
    if((!exists||action==0x12)&&((attributes&~0x20u)||(mode&3)==0))return AccessDenied;
    int flags=(mode&3)==0?O_RDONLY:(mode&3)==1?O_WRONLY:O_RDWR;
    if(!exists)flags|=O_CREAT|O_EXCL;
    if(exists&&action==0x12)flags|=O_TRUNC;
    io();Slot &slot=slots_[index];slot.file=SD.sdfs.open(full,flags);
    if(!slot.file)return AccessDenied;
    slot.mode=uint8_t(mode);slot.used=true;strcpy(slot.path,full);
    const uint16_t error=info(slot.file,full,out);
    if(error){close(index);return error;}
    if(!exists||action==0x12){account(oldSize,uint32_t(slot.file.fileSize()));invalidateEnumeration();}
    result=!exists?2:action==0x12?3:1;return Okay;
  }

MPE5_CODE uint16_t FolderFilesystem::close(uint8_t index) {
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    Slot &slot=slots_[index];io();const bool okay=slot.file.close();
    slot.used=false;slot.path[0]=0;return okay?Okay:WriteFault;
  }

MPE5_CODE uint16_t FolderFilesystem::read(uint8_t index,uint32_t offset,uint8_t *buffer,uint16_t requested,uint16_t &actual) {
    actual=0;if(!ready_)return NotReady;
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    Slot &slot=slots_[index];if(!(accessMask(slot.mode)&1))return AccessDenied;
    if(!requested||offset>=slot.file.fileSize())return Okay;
    io();if(!slot.file.seekSet(offset))return SeekError;
    io();const int count=slot.file.read(buffer,requested);
    if(count<0)return ReadFault;
    actual=uint16_t(count);return Okay;
  }

MPE5_CODE void FolderFilesystem::account(uint32_t before,uint32_t after) {
    const uint64_t clusterBytes=uint64_t(sectorsPerCluster_)*512u;
    const uint32_t oldClusters=uint32_t((before+clusterBytes-1)/clusterBytes);
    const uint32_t newClusters=uint32_t((after+clusterBytes-1)/clusterBytes);
    if(newClusters>oldClusters) {
      const uint64_t used=uint64_t(newClusters-oldClusters)*sectorsPerCluster_;
      freeSectors_=used>freeSectors_?0:freeSectors_-uint32_t(used);
    }else {
      const uint64_t freed=uint64_t(oldClusters-newClusters)*sectorsPerCluster_;
      freeSectors_=uint32_t(freed+freeSectors_>totalSectors_?totalSectors_:freed+freeSectors_);
    }
  }

MPE5_CODE uint16_t FolderFilesystem::extend(Slot &slot,uint32_t length) {
    const uint32_t before=uint32_t(slot.file.fileSize());
    if(length<=before)return Okay;
    if(length-before>MaxExtensionBytes)return SeekError;
    io();if(!slot.file.seekSet(before))return SeekError;
    uint32_t remaining=length-before;
    while(remaining) {
      const size_t count=remaining>sizeof zeros_?sizeof zeros_:remaining;
      io();const size_t written=slot.file.write(zeros_,count);
      if(written!=count){account(before,uint32_t(slot.file.fileSize()));return DiskFull;}
      remaining-=uint32_t(written);
    }
    account(before,length);return Okay;
  }

MPE5_CODE uint16_t FolderFilesystem::write(uint8_t index,uint32_t offset,const uint8_t *buffer,uint16_t requested,uint16_t &actual) {
    actual=0;if(!ready_)return NotReady;
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    Slot &slot=slots_[index];if(!(accessMask(slot.mode)&2))return AccessDenied;
    if(uint64_t(offset)+requested>UINT32_MAX)return SeekError;
    if(!requested)return truncate(index,offset);
    uint16_t error=extend(slot,offset);if(error)return error;
    io();if(!slot.file.seekSet(offset))return SeekError;
    const uint32_t before=uint32_t(slot.file.fileSize());
    io();const size_t written=slot.file.write(buffer,requested);actual=uint16_t(written);
    account(before,uint32_t(slot.file.fileSize()));invalidateEnumeration();
    io();if(!slot.file.sync())return WriteFault;
    return written==requested?Okay:DiskFull;
  }

MPE5_CODE uint16_t FolderFilesystem::truncate(uint8_t index,uint32_t size) {
    if(!ready_)return NotReady;
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    Slot &slot=slots_[index];if(!(accessMask(slot.mode)&2))return AccessDenied;
    const uint32_t before=uint32_t(slot.file.fileSize());
    if(size>before) {const uint16_t error=extend(slot,size);if(error)return error;}
    else {io();if(!slot.file.truncate(size))return WriteFault;account(before,size);}
    invalidateEnumeration();io();return slot.file.sync()?Okay:WriteFault;
  }

MPE5_CODE uint16_t FolderFilesystem::flush(uint8_t index) {
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    io();return slots_[index].file.sync()?Okay:WriteFault;
  }

MPE5_CODE uint16_t FolderFilesystem::setTime(uint8_t index,uint16_t time,uint16_t date) {
    if(index>=HandleCount||!slots_[index].used)return InvalidHandle;
    Slot &slot=slots_[index];if(!(accessMask(slot.mode)&2))return AccessDenied;
    const uint8_t month=uint8_t((date>>5)&15),day=uint8_t(date&31);
    const uint8_t hour=uint8_t(time>>11),minute=uint8_t((time>>5)&63),second=uint8_t((time&31)*2);
    if(!month||month>12||!day||hour>23||minute>59||second>59)return InvalidFunction;
    io();if(!slot.file.timestamp(T_WRITE,uint16_t(1980+(date>>9)),month,day,hour,minute,second))return WriteFault;
    io();return slot.file.sync()?Okay:WriteFault;
  }

MPE5_CODE uint16_t FolderFilesystem::setAttributes(const char *name,uint8_t attributes) {
    RedirectorFileInfo found{};const uint16_t error=stat(name,found);if(error)return error;
    // Bundled FsFile has no public attribute setter. A no-op is truthful;
    // changes are explicitly rejected instead of disappearing after reboot.
    return attributes==found.attributes?Okay:AccessDenied;
  }

MPE5_CODE uint16_t FolderFilesystem::mkdir(const char *name) {
    if(!ready_)return NotReady;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    if(!strcmp(full,"/DOSVM/D"))return AccessDenied;
    io();if(SD.sdfs.exists(full))return AccessDenied;
    io();if(!SD.sdfs.mkdir(full,false))return PathNotFound;
    freeSectors_=freeSectors_>sectorsPerCluster_?freeSectors_-sectorsPerCluster_:0;
    invalidateEnumeration();return Okay;
  }

MPE5_CODE uint16_t FolderFilesystem::rmdir(const char *name) {
    if(!ready_)return NotReady;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    if(!strcmp(full,"/DOSVM/D")||inUse(full,true))return AccessDenied;
    invalidateEnumeration();io();return SD.sdfs.rmdir(full)?Okay:AccessDenied;
  }

MPE5_CODE uint16_t FolderFilesystem::remove(const char *name) {
    if(!ready_)return NotReady;
    char full[PathBytes];if(!path(name,full))return PathNotFound;
    if(inUse(full))return SharingViolation;
    RedirectorFileInfo found{};const uint16_t error=stat(name,found);if(error)return error;
    if(found.attributes&0x11)return AccessDenied;
    invalidateEnumeration();io();if(!SD.sdfs.remove(full))return AccessDenied;
    account(found.size,0);return Okay;
  }

MPE5_CODE uint16_t FolderFilesystem::rename(const char *from,const char *to) {
    if(!ready_)return NotReady;
    char a[PathBytes],b[PathBytes];if(!path(from,a)||!path(to,b))return PathNotFound;
    if(!strcmp(a,"/DOSVM/D")||!strcmp(b,"/DOSVM/D")||subtree(a,b))return AccessDenied;
    if(inUse(a,true)||inUse(b,true))return SharingViolation;
    invalidateEnumeration();io();return SD.sdfs.rename(a,b)?Okay:AccessDenied;
  }

MPE5_CODE uint16_t FolderFilesystem::space(uint32_t &total,uint32_t &available) {
    if(!ready_)return NotReady;
    total=totalSectors_;available=freeSectors_;return Okay;
  }

static_assert(sizeof(FolderFilesystem)<8u*1024u,"Folder handles must fit borrowed RAM1");

} // namespace mpe5
#endif

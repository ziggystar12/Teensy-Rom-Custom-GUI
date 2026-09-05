#include "mpe5_redirector.h"
#include <cstring>
#include <limits.h>

namespace mpe5 {
namespace {
// DOS 5+ wire offsets, independently encoded from the published redirector
// structures in eduardocasino/vmsmount dosdefs.h at
// c9a068c569778a32a5e929940882e30a0d52519d and the FreeDOS DOS-compatible SDA.
// These are protocol layout facts; no implementation from that project is
// incorporated here. All guest fields are accessed explicitly little-endian.
constexpr uint16_t Dta=0x0c, Psp=0x10, Name1=0x9e, Name2=0x11e;
constexpr uint16_t SearchBlock=0x19e, FoundEntry=0x1b3, SearchAttr=0x24d;
constexpr uint16_t OpenMode=0x24e, CurrentCds=0x282;
constexpr uint16_t ExtAction=0x2dd, ExtAttr=0x2df, ExtMode=0x2e1;
constexpr uint16_t SftCount=0, SftMode=2, SftAttr=4, SftFlags=5;
constexpr uint16_t SftTime=0x0d, SftSize=0x11, SftPos=0x15, SftHandle=0x19;
constexpr uint16_t CdsBytes=0x58, CdsFlags=0x43, CdsMagic=0x4d;
constexpr uint16_t Magic=0x4d35;
constexpr uint16_t InvalidFunction=1, FileNotFound=2, PathNotFound=3;
constexpr uint16_t TooMany=4, AccessDenied=5, InvalidHandle=6, InvalidData=13;
constexpr uint16_t InvalidDrive=15, NoMore=18, NotReady=21, Sharing=32;
constexpr uint32_t GuestLimit=ConventionalRamBytes;
constexpr uint32_t physical(uint16_t segment, uint16_t offset) {
  return (uint32_t(segment)<<4)+offset;
}
MPE5_CODE uint16_t load16(const uint8_t *p) { return uint16_t(p[0]|uint16_t(p[1])<<8); }
MPE5_CODE uint32_t load32(const uint8_t *p) { return uint32_t(load16(p))|uint32_t(load16(p+2))<<16; }
MPE5_CODE void store16(uint8_t *p,uint16_t n) { p[0]=uint8_t(n);p[1]=uint8_t(n>>8); }
MPE5_CODE void store32(uint8_t *p,uint32_t n) { store16(p,uint16_t(n));store16(p+2,uint16_t(n>>16)); }
MPE5_CODE char upper(char c) { return c>='a'&&c<='z' ? char(c-'a'+'A') : c; }
MPE5_CODE bool legal(char c) {
  return c>32&&c<127&&std::strchr("\"+,/:;<=>[\\]|",c)==nullptr;
}
MPE5_CODE bool shortName(const char *name,char out[11],bool wild=false) {
  std::memset(out,' ',11);
  if (!std::strcmp(name,".")||!std::strcmp(name,"..")) {
    std::memcpy(out,name,std::strlen(name)); return !wild;
  }
  unsigned field=0,used=0;
  if (!*name) return false;
  for (const char *p=name;*p;++p) {
    if (*p=='.') { if(field||!used) return false;field=8;used=0;continue; }
    if (*p=='*'&&wild) {
      while(used<(field?3u:8u)) out[field+used++]='?';
      while(p[1]&&p[1]!='.') ++p;
      continue;
    }
    if ((!legal(*p)||*p=='*'||*p=='?')&&!(wild&&*p=='?')) return false;
    if (used>=(field?3u:8u)) return false;
    out[field+used++]=upper(*p);
  }
  return true;
}
MPE5_CODE bool matches(const char name[11],const char pattern[11]) {
  for(unsigned i=0;i<11;++i) if(pattern[i]!='?'&&pattern[i]!=name[i]) return false;
  return true;
}
MPE5_CODE bool attributesMatch(uint8_t attr,uint8_t mask) {
  return (attr & uint8_t(0x1e & ~mask))==0;
}
MPE5_CODE bool shareDenied(uint16_t mode,bool readAccess,bool writeAccess) {
  unsigned deny=(mode>>4)&7u;
  if (!deny) deny=(mode&3u)?1u:2u;
  return (deny==1&&(readAccess||writeAccess))||(deny==2&&writeAccess)||(deny==3&&readAccess);
}
}

MPE5_CODE void Redirector::configure(const RedirectorMemory &memory,const RedirectorHost &host) {
  reset(); memory_=memory;host_=host;
}
MPE5_CODE void Redirector::reset() {
  for(unsigned i=0;i<HandleCount;++i) {
    if(handles_[i].used&&host_.close) host_.close(host_.context,uint8_t(i));
    handles_[i]={};
  }
  for(auto &search:searches_) search={};
  sda_=cds_=0;token_=0;drive_=3;installed_=memoryFailed_=false;
}
MPE5_CODE bool Redirector::read(uint32_t address,void *out,uint32_t length) {
  if(address>GuestLimit||length>GuestLimit-address||!memory_.read||
      !memory_.read(memory_.context,address,static_cast<uint8_t *>(out),length)) {
    memoryFailed_=true;return false;
  }
  return true;
}
MPE5_CODE bool Redirector::write(uint32_t address,const void *data,uint32_t length) {
  if(address>GuestLimit||length>GuestLimit-address||!memory_.write||
      !memory_.write(memory_.context,address,static_cast<const uint8_t *>(data),length)) {
    memoryFailed_=true;return false;
  }
  return true;
}
MPE5_CODE uint8_t Redirector::byte(uint32_t p) { uint8_t b=0;read(p,&b,1);return b; }
MPE5_CODE uint16_t Redirector::word(uint32_t p) { uint8_t b[2]{};read(p,b,2);return load16(b); }
MPE5_CODE uint32_t Redirector::dword(uint32_t p) { uint8_t b[4]{};read(p,b,4);return load32(b); }
MPE5_CODE uint32_t Redirector::farPointer(uint32_t p) { uint8_t b[4]{};read(p,b,4);return physical(load16(b+2),load16(b)); }
MPE5_CODE bool Redirector::putWord(uint32_t p,uint16_t n) { uint8_t b[2];store16(b,n);return write(p,b,2); }
MPE5_CODE bool Redirector::putDword(uint32_t p,uint32_t n) { uint8_t b[4];store32(b,n);return write(p,b,4); }

MPE5_CODE uint16_t Redirector::path(uint32_t address,char *out,bool wildcard) {
  char source[PathBytes];unsigned length=0;
  for(;length<PathBytes;++length) {source[length]=char(byte(address+length));if(!source[length])break;}
  if(memoryFailed_||length==PathBytes) return InvalidData;
  unsigned start=0;
  if(length>=2&&source[1]==':') {
    if(upper(source[0])!=char('A'+drive_)) return InvalidDrive;
    start=2;
  }
  if(source[start]!='\\'&&source[start]!='/') return PathNotFound;
  unsigned written=1;out[0]='/';out[1]=0;
  while(source[start]) {
    while(source[start]=='\\'||source[start]=='/') ++start;
    if(!source[start])break;
    char component[13];unsigned n=0;
    while(source[start]&&source[start]!='\\'&&source[start]!='/') {
      if(n>=12)return PathNotFound;
      component[n++]=upper(source[start++]);
    }
    component[n]=0;
    if(!std::strcmp(component,"."))continue;
    if(!std::strcmp(component,"..")) {
      if(written==1)return AccessDenied;
      while(written>1&&out[written-1]!='/')--written;
      if(written>1)--written;
      out[written]=0;continue;
    }
    char fcb[11];const bool last=!source[start];
    if(!shortName(component,fcb,wildcard&&last))return PathNotFound;
    if(written>1) {if(written+1>=PathBytes)return PathNotFound;out[written++]='/';}
    if(written+n>=PathBytes)return PathNotFound;
    std::memcpy(out+written,component,n+1);written+=n;
  }
  return 0;
}

MPE5_CODE bool Redirector::ours(uint8_t function,const RedirectorRegisters &r) {
  const uint32_t object=physical(r.es,r.di);
  if(function==0)return true;
  if((function>=6&&function<=11)||function==0x21) {
    const uint16_t flags=word(object+SftFlags);
    return !memoryFailed_&&(flags&0x8000)&&((flags&0x3f)==drive_);
  }
  if(function==0x1c) {
    const uint8_t drive=byte(sda_+SearchBlock);
    return !memoryFailed_&&(drive&0x80)&&((drive&0x1f)==drive_);
  }
  if(function==0x1d||function==0x22)return true;
  if(function==0x0c&&word(object+CdsMagic)==Magic)return true;
  const uint32_t current=farPointer(sda_+CurrentCds);
  if(memoryFailed_)return false;
  if(current==cds_||word(current+CdsMagic)==Magic)return !memoryFailed_;
  return false;
}

MPE5_CODE int Redirector::handle(uint32_t sft) {
  const uint32_t cookie=dword(sft+SftHandle);
  const uint8_t slot=uint8_t(cookie);
  if((cookie>>16)!=Magic||slot>=HandleCount||!handles_[slot].used||handles_[slot].sft!=sft)
    return -1;
  return slot;
}

MPE5_CODE uint16_t Redirector::openFile(RedirectorRegisters &r,uint8_t function) {
  char filename[PathBytes];uint16_t error=path(sda_+Name1,filename);
  if(error)return error;
  const uint32_t sft=physical(r.es,r.di);
  uint8_t old[43];if(!read(sft,old,sizeof old))return InvalidData;
  uint16_t mode=byte(sda_+OpenMode),action=1,attr=0,result=0;
  const uint16_t owner=word(sda_+Psp);
  if(function==0x17) {mode=2;action=0x12;attr=word(physical(r.ss,uint16_t(r.sp+6)));}
  if(function==0x2e) {mode=word(sda_+ExtMode);action=word(sda_+ExtAction);attr=word(sda_+ExtAttr);}
  if(memoryFailed_)return InvalidData;
  if((mode&3u)>2u||((mode>>4)&7u)>4u||(action&~0x13u)||!(action&0x13u)||(action&3u)==3u)
    return 12;
  if(attr&0x18)return AccessDenied;
  int slot=-1;
  for(unsigned i=0;i<HandleCount;++i) {
    if(!handles_[i].used) {if(slot<0)slot=int(i);continue;}
    if(!std::strcmp(handles_[i].path,filename)) {
      const uint16_t previous=word(handles_[i].sft+SftMode);
      // Compatibility opens by the same DOS process may coexist (including
      // create followed by write-only open, as GRAPHSET does). This exemption
      // applies only when BOTH opens are compatibility mode. Explicit sharing
      // modes still apply to this process, and other PSPs retain their checks.
      if(handles_[i].owner==owner && !((previous|mode)&0x70u))continue;
      if(shareDenied(previous,(mode&3u)!=1u,(mode&3u)!=0u)||
         shareDenied(mode,(previous&3u)!=1u,(previous&3u)!=0u))return Sharing;
    }
  }
  if(slot<0)return TooMany;
  if(!host_.open)return NotReady;
  RedirectorFileInfo info{};
  error=host_.open(host_.context,uint8_t(slot),filename,mode,action,uint8_t(attr),info,result);
  if(error)return error;
  if(info.attributes&0x18) {if(host_.close)host_.close(host_.context,uint8_t(slot));return AccessDenied;}
  Handle &entry=handles_[slot];entry.used=true;entry.sft=sft;entry.owner=owner;
  std::strcpy(entry.path,filename);
  uint8_t value[43]{};
  store16(value,load16(old));store16(value+SftMode,mode);
  value[SftAttr]=info.attributes;store16(value+SftFlags,uint16_t(0x8040u|drive_));
  store16(value+SftTime,info.time);store16(value+SftTime+2,info.date);
  store32(value+SftSize,info.size);store32(value+SftHandle,(uint32_t(Magic)<<16)|uint8_t(slot));
  const char *leaf=std::strrchr(filename,'/');char fcb[11];shortName(leaf?leaf+1:filename,fcb);
  std::memcpy(value+0x20,fcb,11);
  if(!write(sft,value,sizeof value)) {if(host_.close)host_.close(host_.context,uint8_t(slot));entry={};return InvalidData;}
  if(function==0x2e)r.cx=result;
  return 0;
}

MPE5_CODE uint16_t Redirector::transfer(RedirectorRegisters &r,bool writing) {
  const uint32_t sft=physical(r.es,r.di);const int slot=handle(sft);
  if(slot<0)return InvalidHandle;
  const uint16_t mode=word(sft+SftMode)&3u;
  if((writing&&mode==0)||(!writing&&mode==1))return AccessDenied;
  uint32_t position=dword(sft+SftPos),size=dword(sft+SftSize),destination=farPointer(sda_+Dta);
  const uint16_t requested=r.cx;r.cx=0;
  if(memoryFailed_)return InvalidData;
  if(writing&&!requested) {
    if(!host_.truncate)return InvalidFunction;
    const uint16_t error=host_.truncate(host_.context,uint8_t(slot),position);
    if(error)return error;
    putDword(sft+SftSize,position);putWord(sft+SftFlags,word(sft+SftFlags)&~0x4040u);
    return memoryFailed_?InvalidData:0;
  }
  uint32_t remaining=requested;
  if(!writing)remaining=position>=size?0:(remaining>size-position?size-position:remaining);
  if(position>UINT32_MAX-remaining||destination>GuestLimit||remaining>GuestLimit-destination)return InvalidData;
  uint16_t error=0;
  while(remaining) {
    const uint16_t count=uint16_t(remaining>sizeof buffer_?sizeof buffer_:remaining);
    uint16_t actual=0;
    if(writing) {
      if(!read(destination,buffer_,count)){error=InvalidData;break;}
      error=host_.write?host_.write(host_.context,uint8_t(slot),position,buffer_,count,actual):InvalidFunction;
    } else {
      error=host_.read?host_.read(host_.context,uint8_t(slot),position,buffer_,count,actual):InvalidFunction;
    }
    if(actual>count){error=InvalidData;break;}
    if(!writing&&actual&&!write(destination,buffer_,actual)){error=InvalidData;break;}
    position+=actual;destination+=actual;remaining-=actual;r.cx=uint16_t(r.cx+actual);
    if(error||actual<count)break;
  }
  putDword(sft+SftPos,position);
  if(writing&&r.cx) {
    if(position>size)putDword(sft+SftSize,position);
    putWord(sft+SftFlags,word(sft+SftFlags)&~0x4040u);
  }
  return memoryFailed_?InvalidData:error;
}

MPE5_CODE uint16_t Redirector::closeFile(uint32_t sft) {
  const int slot=handle(sft);if(slot<0)return InvalidHandle;
  const uint16_t references=word(sft+SftCount);
  if(references>1){putWord(sft+SftCount,references-1);return memoryFailed_?InvalidData:0;}
  uint16_t error=host_.flush?host_.flush(host_.context,uint8_t(slot)):0;
  const uint16_t flags=word(sft+SftFlags);
  if(!error&&host_.setTime&&(flags&0x4000u))
    error=host_.setTime(host_.context,uint8_t(slot),word(sft+SftTime),word(sft+SftTime+2));
  const uint16_t closed=host_.close?host_.close(host_.context,uint8_t(slot)):InvalidFunction;
  if(!error)error=closed;
  handles_[slot]={};putWord(sft+SftCount,0);
  return memoryFailed_?InvalidData:error;
}

MPE5_CODE uint16_t Redirector::findNext(Search &search) {
  RedirectorFileInfo info{};char fcb[11];
  for(uint32_t attempt=0;attempt<65536u;++attempt) {
    if(search.cursor==0xffffu){search.used=false;return NoMore;}
    uint16_t error=0;
    const bool nested=std::strcmp(search.path,"/")!=0;
    if(nested&&search.cursor<2) {
      std::strcpy(info.name,search.cursor?"..":".");info.attributes=0x10;
    } else {
      if(!host_.enumerate)return NotReady;
      error=host_.enumerate(host_.context,search.path,uint16_t(search.cursor-(nested?2:0)),info);
    }
    ++search.cursor;
    if(error){if(error==NoMore)search.used=false;return error;}
    if(!shortName(info.name,fcb)||!matches(fcb,search.pattern)||!attributesMatch(info.attributes,search.attributes))continue;
    uint8_t block[21]{};block[0]=uint8_t(0xc0|drive_);
    std::memcpy(block+1,search.pattern,11);block[12]=search.attributes;
    store16(block+13,search.cursor);
    store32(block+15,(uint32_t(Magic)<<16)|search.token);block[19]=nested?0:1;
    uint8_t entry[32]{};std::memcpy(entry,fcb,11);entry[11]=info.attributes;
    store16(entry+22,info.time);store16(entry+24,info.date);store32(entry+28,info.size);
    write(sda_+SearchBlock,block,sizeof block);write(sda_+FoundEntry,entry,sizeof entry);
    return memoryFailed_?InvalidData:0;
  }
  return NoMore;
}

MPE5_CODE uint16_t Redirector::find(RedirectorRegisters &,bool first) {
  if(!first) {
    const uint32_t cookie=dword(sda_+SearchBlock+15);
    if((cookie>>16)!=Magic)return NoMore;
    for(auto &search:searches_) if(search.used&&search.token==uint16_t(cookie)) {
      search.cursor=word(sda_+SearchBlock+13);
      return findNext(search);
    }
    return NoMore;
  }
  const uint8_t attr=byte(sda_+SearchAttr);
  if(attr==8) {
    uint8_t block[21]{},entry[32]{};block[0]=uint8_t(0x80|drive_);
    std::memcpy(entry,"DOSVM D    ",11);entry[11]=8;store16(entry+24,0x21);
    write(sda_+SearchBlock,block,sizeof block);write(sda_+FoundEntry,entry,sizeof entry);
    return memoryFailed_?InvalidData:0;
  }
  char filename[PathBytes];uint16_t error=path(sda_+Name1,filename,true);if(error)return error;
  char *leaf=std::strrchr(filename,'/');if(!leaf||!leaf[1])return FileNotFound;
  char pattern[11];if(!shortName(leaf+1,pattern,true))return FileNotFound;
  if(leaf==filename)leaf[1]=0;else *leaf=0;
  RedirectorFileInfo info{};
  if(!host_.stat)return NotReady;
  error=host_.stat(host_.context,filename,info);if(error)return error==FileNotFound?PathNotFound:error;
  if(!(info.attributes&0x10))return PathNotFound;
  // DOS copies its internal search block to/from the caller's DTA. A nested
  // program can begin another search with a different DTA while the old SDA
  // still contains the first cookie, so key replacement by DTA and PSP.
  const uint32_t dta=farPointer(sda_+Dta);const uint16_t owner=word(sda_+Psp);
  for(auto &s:searches_)if(s.used&&s.dta==dta&&s.owner==owner)s.used=false;
  Search *search=nullptr;for(auto &s:searches_)if(!s.used){search=&s;break;}
  if(!search)return TooMany;
  *search={};search->used=true;std::strcpy(search->path,filename);std::memcpy(search->pattern,pattern,11);
  search->attributes=attr;search->owner=owner;search->dta=dta;
  if(++token_==0)++token_;
  search->token=token_;
  error=findNext(*search);return error==NoMore?FileNotFound:error;
}

MPE5_CODE uint16_t Redirector::eraseOrRename(bool renaming) {
  char oldPath[PathBytes],newPath[PathBytes];
  uint16_t error=path(sda_+Name1,oldPath,true);if(error)return error;
  if(renaming){error=path(sda_+Name2,newPath,true);if(error)return error;}
  // File wildcard deletion is supported through a bounded enumeration. Rename
  // accepts literal names; wildcard rename has no safe host-wide atomic form.
  if(renaming&&(std::strpbrk(oldPath,"*?")||std::strpbrk(newPath,"*?")))return InvalidFunction;
  for(const auto &h:handles_)if(h.used&&(!std::strcmp(h.path,oldPath)||(renaming&&!std::strcmp(h.path,newPath))))return Sharing;
  if(!std::strpbrk(oldPath,"*?")) {
    if(renaming)return host_.rename?host_.rename(host_.context,oldPath,newPath):InvalidFunction;
    return host_.remove?host_.remove(host_.context,oldPath):InvalidFunction;
  }
  char *leaf=std::strrchr(oldPath,'/');char pattern[11];if(!shortName(leaf+1,pattern,true))return FileNotFound;
  if(leaf==oldPath)leaf[1]=0;else *leaf=0;
  uint16_t index=0;bool removed=false;
  while(index<0xffffu) {
    RedirectorFileInfo info{};if(!host_.enumerate)return NotReady;
    error=host_.enumerate(host_.context,oldPath,index,info);
    if(error==NoMore)return removed?0:FileNotFound;
    if(error)return error;
    char fcb[11];if(!shortName(info.name,fcb)||!matches(fcb,pattern)||(info.attributes&0x1e)){++index;continue;}
    const size_t parent=std::strlen(oldPath),name=std::strlen(info.name);
    if(parent+name+2>sizeof newPath)return PathNotFound;
    std::strcpy(newPath,oldPath);if(parent>1)std::strcat(newPath,"/");std::strcat(newPath,info.name);
    for(const auto &h:handles_)if(h.used&&!std::strcmp(h.path,newPath))return Sharing;
    if(!host_.remove)return InvalidFunction;
    error=host_.remove(host_.context,newPath);if(error)return error;
    removed=true; // Native enumeration compacts when the current entry disappears.
  }
  return InvalidData;
}

MPE5_CODE uint16_t Redirector::dispatch(uint8_t function,RedirectorRegisters &r) {
  const uint32_t object=physical(r.es,r.di);
  if(function==0){r.ax=0x00ff;return 0;}
  if(function==0x16||function==0x17||function==0x2e)return openFile(r,function);
  if(function==8||function==9)return transfer(r,function==9);
  if(function==6)return closeFile(object);
  if(function==7){const int slot=handle(object);return slot<0?InvalidHandle:host_.flush?host_.flush(host_.context,uint8_t(slot)):InvalidFunction;}
  if(function==0x1b||function==0x1c)return find(r,function==0x1b);
  if(function==0x11||function==0x13)return eraseOrRename(function==0x11);
  if(function==0x21) {
    if(handle(object)<0)return InvalidHandle;
    const int64_t target=int64_t(dword(object+SftSize))+int32_t((uint32_t(r.cx)<<16)|r.dx);
    if(target<0||target>UINT32_MAX)return 25;
    putDword(object+SftPos,uint32_t(target));r.ax=uint16_t(target);r.dx=uint16_t(uint32_t(target)>>16);return 0;
  }
  if(function==0x1d||function==0x22) {
    const uint16_t owner=word(sda_+Psp);
    if(function==0x1d)for(auto &h:handles_)if(h.used&&h.owner==owner)closeFile(h.sft);
    for(auto &s:searches_)if(s.owner==owner)s.used=false;
    return 0;
  }
  if(function==0x0c) {
    uint32_t total=0,free=0;if(!host_.space)return InvalidFunction;
    const uint16_t error=host_.space(host_.context,total,free);if(error)return error;
    r.ax=64;r.cx=512;r.bx=uint16_t(total/64>65535?65535:total/64);
    r.dx=uint16_t(free/64>r.bx?r.bx:free/64);return 0;
  }
  char filename[PathBytes];uint16_t error=path(sda_+Name1,filename);if(error)return error;
  if(function==1)return !std::strcmp(filename,"/")?AccessDenied:host_.rmdir?host_.rmdir(host_.context,filename):InvalidFunction;
  if(function==3)return host_.mkdir?host_.mkdir(host_.context,filename):InvalidFunction;
  if(function==5||function==0x0f) {
    RedirectorFileInfo info{};if(!host_.stat)return NotReady;
    error=host_.stat(host_.context,filename,info);if(error)return error;
    if(function==0x0f){r.ax=info.attributes;return 0;}
    if(!(info.attributes&0x10))return PathNotFound;
    char current[67];const size_t length=std::strlen(filename);
    if(length+3>sizeof current)return PathNotFound;
    current[0]=char('A'+drive_);current[1]=':';
    for(size_t i=0;i<=length;++i)current[i+2]=filename[i]=='/'?'\\':filename[i];
    write(cds_,current,uint32_t(length+3));return 0;
  }
  if(function==0x0e) {
    const uint8_t attr=uint8_t(word(physical(r.ss,uint16_t(r.sp+6))));
    return host_.setAttributes?host_.setAttributes(host_.context,filename,attr):InvalidFunction;
  }
  return InvalidFunction;
}

MPE5_CODE bool Redirector::service(uint8_t operation,RedirectorRegisters &r) {
  memoryFailed_=false;
  if(operation==0) {
    if(installed_){r.ax=AccessDenied;r.flags|=1u;return true;}
    const uint8_t drive=uint8_t(r.ax);const uint32_t sda=physical(r.ds,r.si),lol=physical(r.es,r.bx);
    const uint32_t table=farPointer(lol+0x16);const uint8_t last=byte(lol+0x21);
    const uint32_t cds=table+uint32_t(drive)*CdsBytes;
    uint16_t error=0;
    if(memoryFailed_||drive<3||drive>=26||drive>=last||sda>GuestLimit-0x300u||!table)error=InvalidDrive;
    else if(word(cds+CdsFlags)&0xc000u)error=AccessDenied;
    if(memoryFailed_)error=InvalidData;
    if(!error) {
      uint8_t entry[CdsBytes]{};entry[0]=uint8_t('A'+drive);entry[1]=':';entry[2]='\\';
      store16(entry+CdsFlags,0xc080);store16(entry+CdsMagic,Magic);store16(entry+0x4f,2);
      if(!write(cds,entry,sizeof entry))error=InvalidData;
      else{sda_=sda;cds_=cds;drive_=drive;installed_=true;}
    }
    r.ax=error;r.flags=uint16_t((r.flags&~1u)|(error?1u:0u));return true;
  }
  if(operation!=1||!installed_||(r.ax>>8)!=0x11)return false;
  const uint8_t function=uint8_t(r.ax);
  if(function>0x2e||!ours(function,r))return false;
  r.ax=0;const uint16_t result=dispatch(function,r);
  const uint16_t error=memoryFailed_?InvalidData:result;
  if(error)r.ax=error;
  r.flags=uint16_t((r.flags&~1u)|(error?1u:0u));return true;
}
} // namespace mpe5

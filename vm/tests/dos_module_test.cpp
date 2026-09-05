// Executes the actual DOS module against copied writable storage, never source images.
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "../dos/dosvm.cpp"
#include "helpers/dos-indexed-host.h"
namespace fs=std::filesystem;
struct TestFile {fs::path path;std::fstream stream;std::vector<fs::path> entries;size_t index=0;bool used=false,dir=false,dirtyCreation=false;uint32_t flags=0;};
static TestFile files[24];static fs::path base;
static uint64_t bytesRead,bytesWritten,flushes,packets,frames;
static uint8_t failure;
static uint32_t detail;
static bool failNextFlush;
static std::ofstream wire, checkpoints;
static uint8_t wireSequence;
static void capturePacket(const VmPacket &p){
 assert(p.type==1||p.type==2); // Stable DOS must never wait on indexed DMA.
 if(!wire.is_open())return;
 uint8_t bytes[240]{};wireSequence=wireSequence==255?1:wireSequence+1;
 bytes[0]='M';bytes[1]='3';bytes[2]=1;bytes[3]=p.type;bytes[4]=wireSequence;bytes[5]=p.flags;bytes[6]=p.length;
 memcpy(bytes+8,p.payload,p.length);uint16_t crc=0xffff;
 for(unsigned i=0;i<8u+p.length;i++){crc^=uint16_t(bytes[i])<<8;for(unsigned b=0;b<8;b++)crc=(crc<<1)^((crc&0x8000)?0x1021:0);}
 bytes[8+p.length]=crc;bytes[9+p.length]=crc>>8;wire.write((const char *)bytes,sizeof bytes);
}
static auto epoch=std::chrono::steady_clock::now();
static uint32_t now(){return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-epoch).count());}
static void info(const fs::path &p,VmFileInfo *i){*i={};i->directory=fs::is_directory(p);i->bytes=i->directory?0:fs::file_size(p);i->attributes=i->directory?16:32;i->date=33;auto name=p.filename().string();strncpy(i->name,name.c_str(),95);}
static uint32_t openFlags(const char *p,uint32_t flags,VmFileInfo *i){
 auto path=base/fs::path(p).relative_path();bool exists=fs::exists(path);
 if((!exists&&!(flags&VM_OPEN_CREATE))||(exists&&(flags&VM_OPEN_EXCLUSIVE)))return 0;
 // SdFat keeps create/truncate directory metadata on the opening handle.
 // Require it published before a second open, not merely when the first closes.
 for(const auto &f:files)if(f.used&&f.path==path)assert(!f.dirtyCreation);
 for(unsigned n=0;n<24;n++)if(!files[n].used){auto &f=files[n];f.path=path;f.flags=flags;f.dir=exists&&fs::is_directory(path);f.index=0;f.entries.clear();
  if(f.dir){for(auto &e:fs::directory_iterator(path))f.entries.push_back(e.path());}
  else{if(!exists)std::ofstream(path,std::ios::binary).close();auto mode=std::ios::binary|std::ios::in;if(flags&VM_OPEN_WRITE)mode|=std::ios::out;if(flags&VM_OPEN_TRUNCATE)mode|=std::ios::trunc;
   f.stream.clear();f.stream.open(path,mode);if(!f.stream)return 0;}
  f.dirtyCreation=!f.dir&&(!exists||(flags&VM_OPEN_TRUNCATE));f.used=true;info(path,i);return n+1;
 }return 0;
}
static uint32_t openRead(const char *p,VmFileInfo *i){return openFlags(p,1,i);}
static int32_t readFile(uint32_t h,uint32_t off,void *p,uint32_t n){
 if(!h||h>24)return -1;auto &f=files[h-1];if(!f.used||f.dir||!(f.flags&1))return -1;
 f.stream.clear();f.stream.seekg(off);f.stream.read((char *)p,n);auto got=f.stream.gcount();bytesRead+=got;return int32_t(got);
}
static int32_t writeFile(uint32_t h,uint32_t off,const void *p,uint32_t n){
 if(!h||h>24)return -1;auto &f=files[h-1];if(!f.used||f.dir||!(f.flags&2))return -1;
 f.stream.clear();f.stream.seekp(off);f.stream.write((const char *)p,n);if(!f.stream)return -1;bytesWritten+=n;return n;
}
static int32_t nextFile(uint32_t h,VmFileInfo *i){if(!h||h>24)return -1;auto &f=files[h-1];if(!f.used||!f.dir)return -1;if(f.index==f.entries.size())return 0;info(f.entries[f.index++],i);return 1;}
static void closeFile(uint32_t h){if(h&&h<=24){auto &f=files[h-1];if(f.stream.is_open())f.stream.close();f.used=false;}}
static int32_t fileOp(VmFsRequest *r){
 TestFile *f=r->handle&&r->handle<=24?&files[r->handle-1]:nullptr;
 if((unsigned)r->operation<=(unsigned)VmFsOp::Close){if(!f||!f->used)return -1;
  switch(r->operation){
   case VmFsOp::Flush:if(failNextFlush){failNextFlush=false;return -1;}if(!f->dir){f->stream.clear();f->stream.flush();}f->dirtyCreation=false;flushes++;return 0;
   case VmFsOp::Close:closeFile(r->handle);return 0;
   case VmFsOp::Truncate:if(!(f->flags&2))return -1;f->stream.flush();fs::resize_file(f->path,r->value);return 0;
   case VmFsOp::Timestamp:return (f->flags&2)?0:-1;
   default:return -1;}
 }
 if(r->operation==VmFsOp::Space){r->value=1024*1024;r->extra=512*1024;r->handle=8;return 0;}
 auto p=base/fs::path(r->path).relative_path();std::error_code ec;
 switch(r->operation){
  case VmFsOp::Mkdir:return (r->value?fs::create_directories(p,ec):fs::create_directory(p,ec))?0:-1;
  case VmFsOp::Rmdir:return fs::is_directory(p)&&fs::is_empty(p)&&fs::remove(p,ec)?0:-1;
  case VmFsOp::Remove:return !fs::is_directory(p)&&fs::remove(p,ec)?0:-1;
  case VmFsOp::Rename:fs::rename(p,base/fs::path(r->destination).relative_path(),ec);return ec?-1:0;
  default:return -1;}
}
static bool yield(){return false;}
static void fail(uint8_t code,uint32_t value){failure=code;detail=value;}
static std::string screen(){std::string s;if(MPE5PublishedShadow)for(unsigned i=0;i<2000;i++)s+=char(MPE5PublishedShadow[i*2]);return s;}
static void check(){if(failure||MPE5Error){std::cerr<<"DOS failure "<<unsigned(failure)<<"/"<<unsigned(MPE5Error)<<" detail "<<detail<<" CS:IP "<<std::hex<<regs16[REG_CS]<<":"<<reg_ip<<"\n"<<screen()<<"\n";std::abort();}}
static void tick(const VmModule *m){m->pump();VmPacket p{};if(m->packet(&p)){
 assert(p.length<=228);packets++;if(p.type==2)frames++;
 capturePacket(p);
 const auto frozen=ModulePacket;m->pump();assert(!memcmp(&frozen,&ModulePacket,sizeof frozen));m->ack();}check();}
static void until(const VmModule *m,const char *text){for(unsigned n=0;n<30000;n++){tick(m);if(screen().find(text)!=std::string::npos)return;}std::cerr<<"Timeout waiting for "<<text<<"\n"<<screen()<<"\n";std::abort();}
static void command(const VmModule *m,const char *s){while(*s){VmInput in{uint8_t(*s++),0,0,1};m->input(&in);for(unsigned n=0;n<4;n++)tick(m);}for(unsigned n=0;n<80;n++)tick(m);}

// Original test program: create, retain that handle, reopen write-only in DOS
// compatibility mode, write one Tandy byte, close the second handle and exit.
// DOS must close the original handle during termination (GRAPHSET's pattern).
static std::vector<uint8_t> compatibilitySaveProgram(){
 std::vector<uint8_t> code;std::vector<size_t> filenames,errors;
 auto emit=[&](std::initializer_list<uint8_t> b){code.insert(code.end(),b);};
 auto filename=[&](){emit({0xba,0,0});filenames.push_back(code.size()-2);}; // mov dx,name
 auto error=[&](){emit({0x72,0});errors.push_back(code.size()-1);}; // jc failure
 auto address=[&](size_t at,size_t target){code[at]=uint8_t(target+0x100);code[at+1]=uint8_t((target+0x100)>>8);};
 filename();emit({0x31,0xc9,0xb4,0x3c,0xcd,0x21});error(); // create, CX=0
 filename();emit({0xb8,1,0x3d,0xcd,0x21});error(); // open write-only compatibility
 emit({0x89,0xc3,0xba,0,0});const auto data=code.size()-2; // mov bx,ax; mov dx,byte
 emit({0xb9,1,0,0xb4,0x40,0xcd,0x21});error();
 emit({0x3d,1,0,0x75,0});errors.push_back(code.size()-1); // require exactly one byte
 emit({0xb4,0x3e,0xcd,0x21});error();
 emit({0xb8,0,0x4c,0xcd,0x21});
 const auto failure=code.size();emit({0xb8,1,0x4c,0xcd,0x21});
 const auto name=code.size();for(char c:std::string("D:\\COMPAT.DTA"))code.push_back(uint8_t(c));code.push_back(0);
 address(data,code.size());code.push_back(2);
 for(auto at:filenames)address(at,name);
 for(auto at:errors){assert(failure>at&&failure-at-1<128);code[at]=uint8_t(failure-at-1);}
 return code;
}

static void savedByte(const fs::path &p,uint8_t value){
 assert(fs::exists(p)&&fs::file_size(p)==1);std::ifstream f(p,std::ios::binary);assert(f.get()==value);
}

static void graphsetTest(const VmModule *m,const fs::path &exe){
 const auto mm=base/"VMS/DOSVM/D/MM";fs::create_directory(mm);
 fs::copy_file(exe,mm/"GRAPHSET.EXE");
 {std::ofstream f(mm/"GACARD.DTA",std::ios::binary);f.put(0);}
 // C: is a control run through the block-device path, including guest readback.
 command(m,"copy D:\\MM\\GRAPHSET.EXE C:\\GRAPHSET.EXE\r");
 command(m,"copy D:\\MM\\GACARD.DTA C:\\GACARD.DTA\r");
 command(m,"C:\\GRAPHSET\r");until(m,"Graphics Adapter #");command(m,"3");
 for(unsigned n=0;n<300;n++)tick(m);
 assert(screen().find("DISK ERROR!")==std::string::npos);
 command(m,"copy C:\\GACARD.DTA D:\\CVERIFY.DTA\r");
 savedByte(base/"VMS/DOSVM/D/CVERIFY.DTA",2);
 command(m,"D:\r");command(m,"cd \\MM\r");
 unsigned iteration=0;
 for(const auto choice:{"3","1","3"}){
  command(m,"GRAPHSET\r");until(m,"Graphics Adapter #");command(m,choice);
  for(unsigned n=0;n<300;n++)tick(m);
  assert(screen().find("DISK ERROR!")==std::string::npos);
  const auto value=uint8_t(choice[0]-'1');savedByte(mm/"GACARD.DTA",value);
  const auto verify="VFY"+std::to_string(iteration++)+".DTA";
  command(m,("copy GACARD.DTA D:\\"+verify+"\r").c_str());
  savedByte(base/"VMS/DOSVM/D"/verify,value);
 }
 std::cout<<"PASS: supplied GRAPHSET C: Tandy and D: Tandy/CGA/Tandy, one-byte saves and guest readback\n";
}
int main(int argc,char **argv){
 assert(argc==3||argc==4);base=argv[2];assert(base.string().find("dos-sandbox-")!=std::string::npos);
 fs::create_directories(base/"VMS");fs::copy(fs::path(argv[1])/"VMS/DOSVM",base/"VMS/DOSVM",fs::copy_options::recursive);
 wire.open(base/"video-wire.bin",std::ios::binary);checkpoints.open(base/"video-checkpoints.txt");assert(wire&&checkpoints);
 // A real DOS COM program exercises BIOS mode setup, VRAM, SID and return to text.
 const uint8_t tandy[]={0xb8,8,0,0xcd,0x10,0xb8,0,0xb8,0x8e,0xc0,0x31,0xff,
  0xb9,0,0x20,0xb8,0x12,0x12,0xf3,0xab,0x31,0xc0,0xcd,0x16,
  0xb8,9,0,0xcd,0x10,0xb8,0,0xb8,0x8e,0xc0,0x31,0xff,
  0xb9,0,0x40,0xb8,0x45,0x45,0xf3,0xab,0x31,0xc0,0xcd,0x16,
  0xb8,3,0,0xcd,0x10,0xb8,0,0x4c,0xcd,0x21};
 {std::ofstream f(base/"VMS/DOSVM/D/TANDY.COM",std::ios::binary);f.write((const char *)tandy,sizeof tandy);}
 {auto code=compatibilitySaveProgram();std::ofstream f(base/"VMS/DOSVM/D/COMPAT.COM",std::ios::binary);f.write((const char *)code.data(),code.size());}
 VmImageHeader image{};{std::ifstream f(base/"VMS/DOSVM/engine.mvm",std::ios::binary);f.read((char *)&image,sizeof image);assert(f&&image.abi==VM_ABI);}
 const uint32_t remaining=VM_DATA_BYTES-((image.data_bytes+image.bss_bytes+31)&~31u);
 auto allocation=VirtualAlloc((void *)0x20010000,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 assert(allocation==(void *)0x20010000);auto arena=reinterpret_cast<uint8_t *>(VM_DATA_BASE);
 alignas(32) static uint8_t guest[VM_RAM_BYTES+32];
 memset(arena,0xa5,VM_DATA_BYTES+32);memset(guest,0xa5,sizeof guest);
 VmHost h{VM_ABI,sizeof(VmHost),VM_SERVICES,arena,remaining,"/VMS/DOSVM","",now,openRead,readFile,nextFile,closeFile,
  guest,VM_RAM_BYTES,openFlags,writeFile,fileOp,yield,fail};
 h.services|=VM_SERVICE_INDEXED_VIDEO|VM_SERVICE_INDEXED_RASTER;
 h.video_configure=configureVideo;h.video_indexed=submitDosVideo;
 auto m=vm_entry(&h);check();assert(m&&Memory->guest==guest);
 assert(MPE5Host.conventionalRam==guest&&MPE5Host.conventionalRamBytes==524288);
 assert(WorkspaceUsed<=h.workspace_bytes);assert((uint8_t *)Memory>=arena&&(uint8_t *)Memory+sizeof(*Memory)<=arena+h.workspace_bytes);
 // Upper I/O ports are not aliases for low device latches; RAM2 end wraps only at 1 MiB.
 uint8_t byte=0x55,out=0;assert(Memory->transfer(mpe5::AddressMapBytes+0xf060,&byte,1,true));assert(Memory->transfer(mpe5::AddressMapBytes+0xf060,&out,1,false)&&out==0xff);
 until(m,"C:\\>");assert(MPE5Redirector->installed());
 command(m,"echo MODULAR-C-OK > C:\\VMTEST.TXT\r");command(m,"type C:\\VMTEST.TXT\r");until(m,"MODULAR-C-OK");
 command(m,"echo MODULAR-D-OK > D:\\VMTEST.TXT\r");command(m,"type D:\\VMTEST.TXT\r");until(m,"MODULAR-D-OK");
 assert(fs::exists(base/"VMS/DOSVM/D/VMTEST.TXT"));
 {std::ifstream f(base/"VMS/DOSVM/D/VMTEST.TXT");std::string s{std::istreambuf_iterator<char>(f),{}};assert(s.find("MODULAR-D-OK")!=std::string::npos);}
 // More than the 16 redirector slots: termination must release both handles.
 const auto openCount=[](){unsigned n=0;for(const auto &f:files)n+=f.used;return n;};
 const auto baseline=openCount();
 // A failed create/truncate flush must be reported and release the slot.
 {auto folder=MPE5Folder->host();mpe5::RedirectorFileInfo i{};uint16_t result=0;
  failNextFlush=true;assert(folder.open(folder.context,15,"/FLUSHERR.DTA",2,0x12,0,i,result)==29);
  assert(!failNextFlush&&openCount()==baseline);
  assert(folder.open(folder.context,15,"/FLUSHERR.DTA",2,0x12,0,i,result)==0);
  assert(folder.close(folder.context,15)==0&&openCount()==baseline);
 }
 for(unsigned n=0;n<20;++n){
  command(m,"D:\\COMPAT.COM\r");for(unsigned t=0;t<80;t++)tick(m);
  savedByte(base/"VMS/DOSVM/D/COMPAT.DTA",2);assert(openCount()==baseline);
 }
 command(m,"copy D:\\COMPAT.DTA D:\\COMPCHK.DTA\r");
 savedByte(base/"VMS/DOSVM/D/COMPCHK.DTA",2);
 command(m,"D:\\TANDY.COM\r");
 for(unsigned mode:{8u,9u}){
  for(unsigned n=0;n<20000&&mpe5::coreVideoState().mode!=mode;n++)tick(m);
  assert(mpe5::coreVideoState().mode==mode);
  for(unsigned n=0;n<200;n++)tick(m);
  assert(MPE5DisplayVideo.graphics()&&MPE5DisplayVideo.hires()==(mode==9));
  assert(MPE5DisplayHires==(mode==9)&&MPE5DisplayComplete);
  // Independently render stable VRAM, then compare ALL cells at this exact
  // packet boundary in the emitted 6510 receiver (not just the guest shadow).
  uint8_t arena[mpe5::CgaVideo::WorkspaceBytes],records[12000];mpe5::CgaVideo reference;
  assert(reference.start(arena,sizeof arena));reference.write(0,Memory->video,sizeof Memory->video);
  reference.setState(mpe5::coreVideoState());assert(reference.changes(records,1000)==1000);
  const auto name="tandy"+std::to_string(mode)+".cells";std::ofstream expected(base/name,std::ios::binary);
  expected.write((const char *)records,sizeof records);expected.close();
  checkpoints<<packets<<" "<<mode<<" "<<unsigned(reference.background())<<" "<<name<<"\n";
  assert(Memory->video[0]==(mode==8?0x12:0x45));
  command(m," ");
 }
 for(unsigned n=0;n<20000&&mpe5::coreVideoState().mode!=3;n++)tick(m);
 assert(mpe5::coreVideoState().mode==3);
 for(unsigned n=0;n<120;n++)tick(m);assert(MPE5DisplayHires&&MPE5DisplayComplete);
 checkpoints<<packets<<" 3 0 text\n";wire.close();checkpoints.close();
 assert(!videoConfigurations&&!videoTransfers); // F5 is NEVER automatic.
 // Re-run the real COM in both Tandy layouts, selecting F5 explicitly.
 auto chord=[&](uint8_t scan,uint8_t modifiers=6){
  VmInput in{0,scan,0,uint8_t(0x80|modifiers)};m->input(&in);m->pump();check();
 };
 auto enhancedTick=[&](){
  using namespace VmRuntime;
  m->pump();
  if(indexedVideo.phase==6){indexedVideoBorder();assert(transferIndexedVideoSlice());if(indexedVideo.phase==6)return;}
  if(indexedVideo.phase==2){assert(transferIndexedVideo());indexedVideo.phase=3;}
  VmPacket p{};bool host=indexedVideoPacket(p);
  if(!host&&!m->packet(&p)){host=indexedVideoPacket(p);if(!host)return;}
  if(host){
   indexedVideo.hostPacket=true;
   const auto phase=indexedVideo.phase;const auto frozen=*indexedVideo.frame;
   for(unsigned n=0;n<3;n++)m->pump();
   assert(indexedVideo.phase==phase&&!memcmp(&frozen,indexedVideo.frame,sizeof frozen));
   indexedVideoAck();
  }else{
   if(p.type==1&&(p.flags&16))indexedVideoLegacy();
   assert(p.type==1||p.type==2);m->ack();
  }
  check();
 };
 command(m,"D:\\TANDY.COM\r");
 for(unsigned mode:{8u,9u}){
  assert(mpe5::coreVideoState().mode==mode);
  for(uint8_t timing:{0x82,0x83}){
   VmRuntime::videoTiming=timing;
   chord(63);assert(MPE5EnhancedWanted);chord(63,0);assert(MPE5EnhancedWanted);chord(0,0);
   const auto prior=videoConfigurations;
   for(unsigned n=0;n<300;n++)enhancedTick();
   assert(MPE5EnhancedActive&&MPE5EnhancedFrame.frame.resolved_mode==2&&videoConfigurations==prior+1);
   assert(VmRuntime::indexedVideo.bank[0]&&VmRuntime::indexedVideo.bank[1]);
   // Change guest VRAM during a busy stream. The legacy observer must not
   // corrupt the overlaid frozen frame, and returning must rebuild this data.
   MPE5EnhancedNext=0;
   for(unsigned n=0;n<100&&VmRuntime::indexedVideo.phase!=6;n++)enhancedTick();
   assert(VmRuntime::indexedVideo.phase==6);
   const auto frozen=*VmRuntime::indexedVideo.frame;
   uint8_t pixel=0x67;assert(Memory->transfer(0xb8000,&pixel,1,true));MPE5VideoWrite(nullptr,0,&pixel,1);
   assert(!memcmp(&frozen,VmRuntime::indexedVideo.frame,sizeof frozen));
   // F1 is queued while a shared frame is still in flight, not after idle.
   chord(59);assert(!MPE5EnhancedWanted&&MPE5EnhancedActive);chord(0,0);
   for(unsigned n=0;n<200;n++)enhancedTick();
   assert(!MPE5EnhancedActive&&!MPE5EnhancedPending&&!VmRuntime::indexedVideo.phase);
   assert(MPE5DisplayComplete&&MPE5DisplayHires==(mode==9));
   assert(!memcmp(MPE5VideoWorkspace,Memory->video,sizeof Memory->video));
   assert(mpe5::coreVideoState().mode==mode); // No hotkey leaked to INT16.
  }
  command(m," ");
 }
 assert(mpe5::coreVideoState().mode==3);
 assert(videoConfigurations==4&&videoTransfers>0);
 std::cout<<"PASS: explicit F5 on Tandy 08/09, PAL/NTSC actual firmware conversion and both-bank streaming, frozen frames during live VRAM writes, F1 during Busy returns to original renderer, no function-key leakage\n";
 if(argc==4)graphsetTest(m,argv[3]);
 for(unsigned n=0;n<32;n++){assert(arena[h.workspace_bytes+n]==0xa5);assert(guest[VM_RAM_BYTES+n]==0xa5);}
 assert(bytesWritten&&flushes&&packets>100);
 std::cout<<"PASS: actual DOS module, FreeDOS prompt, full 512KiB guest RAM, RAM1 workspace "<<WorkspaceUsed
 <<", immutable packets, C:/D: persistent writes, 20 compatibility create/reopen saves without handle leaks, real Tandy modes 08/09 COM and return to text, "<<packets<<" packets, "<<bytesWritten<<" bytes written, "<<flushes<<" flushes; not physical speed proof\n";
 return 0;
}

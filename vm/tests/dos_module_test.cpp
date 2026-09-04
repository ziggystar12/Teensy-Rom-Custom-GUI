// Executes the actual DOS module against copied writable storage, never source images.
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "../dos/dosvm.cpp"
namespace fs=std::filesystem;
struct TestFile {fs::path path;std::fstream stream;std::vector<fs::path> entries;size_t index=0;bool used=false,dir=false;uint32_t flags=0;};
static TestFile files[24];static fs::path base;
static uint64_t bytesRead,bytesWritten,flushes,packets,frames;
static uint8_t failure;
static uint32_t detail;
static auto epoch=std::chrono::steady_clock::now();
static uint32_t now(){return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-epoch).count());}
static void info(const fs::path &p,VmFileInfo *i){*i={};i->directory=fs::is_directory(p);i->bytes=i->directory?0:fs::file_size(p);i->attributes=i->directory?16:32;i->date=33;auto name=p.filename().string();strncpy(i->name,name.c_str(),95);}
static uint32_t openFlags(const char *p,uint32_t flags,VmFileInfo *i){
 auto path=base/fs::path(p).relative_path();bool exists=fs::exists(path);
 if((!exists&&!(flags&VM_OPEN_CREATE))||(exists&&(flags&VM_OPEN_EXCLUSIVE)))return 0;
 for(unsigned n=0;n<24;n++)if(!files[n].used){auto &f=files[n];f.path=path;f.flags=flags;f.dir=exists&&fs::is_directory(path);f.index=0;f.entries.clear();
  if(f.dir){for(auto &e:fs::directory_iterator(path))f.entries.push_back(e.path());}
  else{if(!exists)std::ofstream(path,std::ios::binary).close();auto mode=std::ios::binary|std::ios::in;if(flags&VM_OPEN_WRITE)mode|=std::ios::out;if(flags&VM_OPEN_TRUNCATE)mode|=std::ios::trunc;
   f.stream.clear();f.stream.open(path,mode);if(!f.stream)return 0;}
  f.used=true;info(path,i);return n+1;
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
   case VmFsOp::Flush:if(!f->dir){f->stream.clear();f->stream.flush();}flushes++;return 0;
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
 const auto frozen=ModulePacket;m->pump();assert(!memcmp(&frozen,&ModulePacket,sizeof frozen));m->ack();}check();}
static void until(const VmModule *m,const char *text){for(unsigned n=0;n<30000;n++){tick(m);if(screen().find(text)!=std::string::npos)return;}std::cerr<<"Timeout waiting for "<<text<<"\n"<<screen()<<"\n";std::abort();}
static void command(const VmModule *m,const char *s){while(*s){VmInput in{uint8_t(*s++),0,0,1};m->input(&in);for(unsigned n=0;n<4;n++)tick(m);}for(unsigned n=0;n<80;n++)tick(m);}
int main(int argc,char **argv){
 assert(argc==3);base=argv[2];assert(base.string().find("dos-sandbox-")!=std::string::npos);
 fs::create_directories(base/"VMS");fs::copy(fs::path(argv[1])/"VMS/DOSVM",base/"VMS/DOSVM",fs::copy_options::recursive);
 // A real DOS COM program exercises BIOS mode setup, VRAM, SID and return to text.
 const uint8_t tandy[]={0xb8,8,0,0xcd,0x10,0xb8,0,0xb8,0x8e,0xc0,0x31,0xff,
  0xb9,0,0x20,0xb8,0x12,0x12,0xf3,0xab,0x31,0xc0,0xcd,0x16,
  0xb8,9,0,0xcd,0x10,0xb8,0,0xb8,0x8e,0xc0,0x31,0xff,
  0xb9,0,0x40,0xb8,0x45,0x45,0xf3,0xab,0x31,0xc0,0xcd,0x16,
  0xb8,3,0,0xcd,0x10,0xb8,0,0x4c,0xcd,0x21};
 {std::ofstream f(base/"VMS/DOSVM/D/TANDY.COM",std::ios::binary);f.write((const char *)tandy,sizeof tandy);}
 VmImageHeader image{};{std::ifstream f(base/"VMS/DOSVM/engine.mvm",std::ios::binary);f.read((char *)&image,sizeof image);assert(f&&image.abi==VM_ABI);}
 const uint32_t remaining=VM_DATA_BYTES-((image.data_bytes+image.bss_bytes+31)&~31u);
 alignas(32) static uint8_t arena[VM_DATA_BYTES+32],guest[VM_RAM_BYTES+32];
 memset(arena,0xa5,sizeof arena);memset(guest,0xa5,sizeof guest);
 VmHost h{VM_ABI,sizeof(VmHost),VM_SERVICES,arena,remaining,"/VMS/DOSVM","",now,openRead,readFile,nextFile,closeFile,
  guest,VM_RAM_BYTES,openFlags,writeFile,fileOp,yield,fail};
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
 command(m,"D:\\TANDY.COM\r");
 for(unsigned mode:{8u,9u}){
  for(unsigned n=0;n<20000&&mpe5::coreVideoState().mode!=mode;n++)tick(m);
  assert(mpe5::coreVideoState().mode==mode);
  for(unsigned n=0;n<200;n++)tick(m);
  assert(MPE5DisplayVideo.graphics()&&MPE5DisplayVideo.hires()==(mode==9));
  assert(Memory->video[0]==(mode==8?0x12:0x45));
  command(m," ");
 }
 for(unsigned n=0;n<20000&&mpe5::coreVideoState().mode!=3;n++)tick(m);
 assert(mpe5::coreVideoState().mode==3);
 for(unsigned n=0;n<32;n++){assert(arena[h.workspace_bytes+n]==0xa5);assert(guest[VM_RAM_BYTES+n]==0xa5);}
 assert(bytesWritten&&flushes&&packets>100);
 std::cout<<"PASS: actual DOS module, FreeDOS prompt, full 512KiB guest RAM, RAM1 workspace "<<WorkspaceUsed
 <<", immutable packets, C:/D: persistent writes, real Tandy modes 08/09 COM and return to text, "<<packets<<" packets, "<<bytesWritten<<" bytes written, "<<flushes<<" flushes; not physical speed proof\n";
}

// Exercise the real bundled BIOS disk ABI and resident redirector hook.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include "../../engine/native-dos/mpe5_platform.cpp"
#include "../../engine/native-dos/mpe5_8086tiny.cpp"

static void require(bool result, const char *message) { if (!result) throw std::runtime_error(message); }
struct Disk {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(40960u*512u);
  unsigned reads=0,writes=0,maxCalls=0,calls=0;
  bool failRead=false,failWrite=false;
  static bool read(void *p,uint32_t lba,uint8_t *out) {
    auto &d=*static_cast<Disk*>(p); ++d.reads; ++d.calls;
    if (d.failRead || lba>=40960) return false;
    std::copy_n(d.bytes.data()+512u*lba,512,out); return true;
  }
  static bool write(void *p,uint32_t lba,const uint8_t *in) {
    auto &d=*static_cast<Disk*>(p); ++d.writes; ++d.calls;
    if (d.failWrite || lba>=40960) return false;
    std::copy_n(in,512,d.bytes.data()+512u*lba); return true;
  }
};
struct Dispatch {
  unsigned resets=0,installs=0,services=0;
  bool handled=true;
  mpe5::RedirectorRegisters last{};
  static bool call(void *p,uint8_t op,mpe5::RedirectorRegisters &r) {
    auto &d=*static_cast<Dispatch*>(p); d.last=r;
    if (!op) { ++d.installs; r.ax=0; r.flags&=~1u; return true; }
    ++d.services;
    r.ax=0xabcd; r.bx=0x1234; r.flags|=1;
    return d.handled;
  }
  static void reset(void *p) { ++static_cast<Dispatch*>(p)->resets; }
};
int main(int argc,char **argv) try {
  require(argc==2,"usage: mpe5_core_services_test BIOS");
  std::ifstream file(argv[1],std::ios::binary);
  std::vector<uint8_t> bios{std::istreambuf_iterator<char>(file),{}};
  std::vector<uint8_t> memory(mpe5::NativeBackingBytes),decode(5120);
  Disk disk; disk.bytes[0]=0xeb; disk.bytes[1]=0xfe;
  disk.bytes[510]=0x55; disk.bytes[511]=0xaa;
  Dispatch dispatch;
  mpe5::CoreHost host{};
  host.addressMap=memory.data(); host.addressMapBytes=memory.size();
  host.decodeTable=decode.data(); host.decodeTableBytes=decode.size();
  host.bios=bios.data(); host.biosBytes=bios.size(); host.drive={&disk,Disk::read,40960,Disk::write};
  host.redirectorContext=&dispatch; host.redirector=Dispatch::call; host.redirectorReset=Dispatch::reset;
  require(mpe5::coreStart(host),"start");
  std::string bootScreen;
  for(unsigned row=0;row<3;++row){for(unsigned column=0;column<40;++column)
    bootScreen+=char(MPE5ConsoleViewport[(row*40+column)*2]);bootScreen+='\n';}
  require(bootScreen.find("Mean Hamster BIOS (C) 2026")!=std::string::npos&&
          bootScreen.find("512K OK")!=std::string::npos&&bootScreen.find("Booting drive C:")!=std::string::npos,
          "missing real preboot banner");
  require(disk.reads==0&&inst_counter==0,"banner appeared after guest boot IO");
  for (unsigned i=0;i<10000 && !(regs16[REG_CS]==0 && reg_ip==0x7c00);++i)
    require(mpe5::coreRun(1000),"BIOS startup");
  require(regs16[REG_CS]==0 && reg_ip==0x7c00,"BIOS did not reach boot sector");
  const auto interrupt13=[&](uint8_t function,uint8_t count,uint16_t chs=2,uint16_t driveHead=0x80) {
    memory[0x20000]=0xcd; memory[0x20001]=0x13; memory[0x20002]=0x90;
    regs16[REG_CS]=0x2000; reg_ip=0;
    regs16[REG_SS]=0x3000; regs16[REG_SP]=0xff00;
    regs16[REG_ES]=0x1000; regs16[REG_BX]=0;
    regs16[REG_AX]=uint16_t(function)<<8|count;
    regs16[REG_CX]=chs; regs16[REG_DX]=driveHead;
    set_flags(2); disk.maxCalls=0;
    for(unsigned i=0;i<10000 && !(regs16[REG_CS]==0x2000 && reg_ip==2);++i) {
      disk.calls=0; require(mpe5::coreRun(1),"INT13 halted");
      disk.maxCalls=std::max(disk.maxCalls,disk.calls);
    }
    require(regs16[REG_CS]==0x2000 && reg_ip==2,"INT13 failed to return");
  };
  std::fill_n(memory.data()+0x10000,9*512,0x5a);
  interrupt13(3,9);
  require(!regs8[FLAG_CF] && regs8[REG_AH]==0,"writable disk failed");
  require(disk.writes==9 && disk.maxCalls<=1,"write transfer ignored instruction bound");
  require(std::all_of(disk.bytes.begin()+512,disk.bytes.begin()+10*512,[](uint8_t b){return b==0x5a;}),"wrong write bytes");
  std::fill_n(memory.data()+0x10000,9*512,0);
  interrupt13(2,9);
  require(!regs8[FLAG_CF] && std::all_of(memory.begin()+0x10000,memory.begin()+0x11200,[](uint8_t b){return b==0x5a;}),"readback mismatch");
  disk.failWrite=true; interrupt13(3,1);
  require(regs8[FLAG_CF] && regs8[REG_AH]!=0,"write failure falsely succeeded");
  disk.failWrite=false; MPE5Host.drive.writeSector=nullptr;
  const auto writes=disk.writes; interrupt13(3,1);
  require(regs8[FLAG_CF] && regs8[REG_AH]!=0 && disk.writes==writes,"read-only disk was not protected");
  disk.failRead=true; interrupt13(2,1);
  require(regs8[FLAG_CF] && regs8[REG_AH]!=0,"read failure falsely succeeded");
  disk.failRead=false;
  const auto reads=disk.reads;
  interrupt13(2,1,0x8a8b); // Cylinder650, sector11 is LBA40960, past C:.
  require(regs8[FLAG_CF] && disk.reads==reads,"read beyond image reached backend");
  interrupt13(2,1,0xffc1,0xff80); // SI:BP LBA exceeds65535; must not wrap.
  require(regs8[FLAG_CF] && disk.reads==reads,"high LBA wrapped onto disk");

  // Execute the exact TSR hook shape and a real INT/IRET return frame.
  const uint8_t hook[]={0x0f,0x04,0xea,0x00,0x01,0x00,0x40,0xcf};
  std::copy(std::begin(hook),std::end(hook),memory.begin()+0x40000);
  memory[0x40100]=0xb8; memory[0x40101]=0x78; memory[0x40102]=0x56; memory[0x40103]=0xcf;
  memory[0xbc]=0; memory[0xbd]=0; memory[0xbe]=0; memory[0xbf]=0x40;
  const auto invoke=[&]() {
    memory[0x20000]=0xcd; memory[0x20001]=0x2f;
    regs16[REG_CS]=0x2000; reg_ip=0; regs16[REG_SS]=0x3000; regs16[REG_SP]=0xff00;
    regs16[REG_AX]=0x1100; regs16[REG_BX]=0x9999; set_flags(0x42);
    for(unsigned i=0;i<10 && !(regs16[REG_CS]==0x2000 && reg_ip==2);++i) require(mpe5::coreRun(1),"INT2F hook stopped");
    require(reg_ip==2 && regs16[REG_CS]==0x2000 && regs16[REG_SP]==0xff00,"INT2F return frame corrupted");
  };
  invoke();
  require(regs16[REG_AX]==0xabcd && regs16[REG_BX]==0x1234 && regs8[FLAG_CF],"handled registers or caller CF lost");
  require(dispatch.last.ax==0x1100 && (dispatch.last.flags&0x41)==0x40 && dispatch.last.sp==0xfefa,"incorrect caller snapshot");
  dispatch.handled=false; invoke();
  require(regs16[REG_AX]==0x5678 && regs16[REG_BX]==0x9999 && !regs8[FLAG_CF],"unhandled hook did not chain unchanged");
  auto guest=mpe5::coreRedirectorMemory(); uint8_t sample=0x7e,actual=0;
  require(guest.write(nullptr,0x12345,&sample,1) && guest.read(nullptr,0x12345,&actual,1) && actual==sample,"redirector memory view");
  mpe5::coreReset(); require(dispatch.resets==1 && !MPE5DiskPending && !MPE5DiskWrite,"reset retained service state");
  // A reset callback that leaves even the last byte dirty must not report
  // '512K OK'. Exercise the direct conventional-memory path used by Teensy.
  auto failedRam=host;failedRam.redirectorReset=nullptr;
  failedRam.conventionalRam=memory.data();failedRam.conventionalRamBytes=mpe5::ConventionalRamBytes;
  failedRam.fixedF000=memory.data()+0xf0000;failedRam.fixedF000Bytes=65536;
  failedRam.consoleShadow=memory.data()+mpe5::NativeTextShadowAddress;
  failedRam.consoleViewport=memory.data()+mpe5::NativeTextViewportAddress;
  failedRam.memory.context=memory.data();
  failedRam.memory.reset=[](void *p){std::fill_n(static_cast<uint8_t *>(p),mpe5::ConventionalRamBytes,0);static_cast<uint8_t *>(p)[mpe5::ConventionalRamBytes-1]=1;return true;};
  failedRam.memory.read=[](void *p,uint32_t a,uint8_t *b,uint32_t n){std::copy_n(static_cast<uint8_t *>(p)+a,n,b);return true;};
  failedRam.memory.write=[](void *p,uint32_t a,const uint8_t *b,uint32_t n){std::copy_n(b,n,static_cast<uint8_t *>(p)+a);return true;};
  require(!mpe5::coreStart(failedRam)&&mpe5::coreDiagnostic().address==mpe5::ConventionalRamBytes-1,
          "bad RAM reset falsely showed successful POST");
  std::cout<<"PASS real BIOS C read/write, read-only/failure statuses, bounded sectors, INT2F dispatch/chain/FLAGS, reset; boot banner before IO and failed RAM readback rejection\n";
  return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }

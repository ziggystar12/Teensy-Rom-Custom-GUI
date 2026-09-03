// Exercises the actual MinimalBoot loader, source-page resolver and native
// logical reader extracted by the runner. Only filesystem/hardware are fakes.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#define FLASHMEM
#define PROGMEM
#define DMAMEM
#include "MPE4Cartridge.h"
#include "mpe4_package.h"
static unsigned checks=0;
static void check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"check %u failed\n",checks); std::abort(); } }
struct FileState { std::vector<uint8_t> bytes; uint32_t cursor=0; bool failSeek=false; };
struct File {
  std::shared_ptr<FileState> state; bool opened=false;
  explicit operator bool() const { return opened; }
  uint32_t size() const { return state?uint32_t(state->bytes.size()):0; }
  uint32_t available() const { return opened?size()-state->cursor:0; }
  uint32_t position() const { return state->cursor; }
  int read() { return available()?state->bytes[state->cursor++]:-1; }
  bool seek(uint32_t where) { if(!opened||state->failSeek||where>size())return false;state->cursor=where;return true; }
  void close() { opened=false; }
};
struct FS {
  std::shared_ptr<FileState> state;
  File open(const char *,int) { state->cursor=0;return {state,true}; }
};
static FS SD;
static File myFile;
static constexpr int FILE_READ=0,MaxNamePathLength=128,RAM_ImageSize=16384;
static constexpr int CRT_MAIN_HDR_LEN=64,CRT_CHIP_HDR_LEN=16,MAX_CRT_CHIPS=128;
static constexpr uint32_t SwapSeekAddrMask=0xef000000u;
static constexpr uint8_t rtFileCrt=1;
struct StructMenuItem { uint8_t ItemType=rtFileCrt; const char *Name="cart.crt"; uint32_t Size=0; uint8_t *Code_Image=nullptr; };
struct StructCrtChip { uint8_t *ChipROM=nullptr; uint16_t LoadAddress=0,ROMSize=0,BankNum=0; };
static StructMenuItem DriveDirMenu;
static char DriveDirPath[]="/";
static uint8_t RAM_Image[RAM_ImageSize],NumCrtChips=0;
static StructCrtChip CrtChips[MAX_CRT_CHIPS];
static std::vector<std::unique_ptr<uint8_t[]>> chipMemory;
static mpe4cart::Index MPE4CrtDirectory{};
static std::string lastMessage;
template<class... T> static void SendMsgPrintfln(const char *text,T...) { lastMessage=text; }
template<class... T> static void Printf_dbg(const char *,T...) {}
static bool PathIsRoot() { return true; }
static bool ParseCRTHeader(StructMenuItem *menu,uint8_t *exrom,uint8_t *game) {
  *exrom=menu->Code_Image[24];*game=menu->Code_Image[25];return true;
}
static void FreeCrtChips() {
  chipMemory.clear();NumCrtChips=0;std::memset(&MPE4CrtDirectory,0,sizeof(MPE4CrtDirectory));
}
// The untouched legacy allocation algorithm is replaced with host-owned RAM;
// the real loader controls which CHIP packets are allowed to reach it.
static bool ParseChipHeader(uint8_t *h) {
  if(std::memcmp(h,"CHIP",4))return false;
  auto &c=CrtChips[NumCrtChips];c.BankNum=mpe4cart::be16(h+10);
  c.LoadAddress=mpe4cart::be16(h+12);c.ROMSize=mpe4cart::be16(h+14);
  chipMemory.emplace_back(new uint8_t[c.ROMSize]);c.ChipROM=chipMemory.back().get();return true;
}
static bool SetTypeFromCRT(StructMenuItem *,uint8_t,uint8_t) { return NumCrtChips>0; }
#include "native-load-file.inc"
static uint8_t *BankDecode[64][2];
static constexpr int Num8kSwapBuffers=14,AGIPicLayout_MagicDesk2=1;
static uint8_t AGIPicLayout=0;
static bool AGIPicAbortRequested=false,AGIPicSlotOwned[Num8kSwapBuffers];
static int8_t AGIPicSourceSlot=-1;
static constexpr uint8_t AGIPicError_InvalidSource=1,AGIPicError_NoWorkspace=2,
  AGIPicError_DMATimeout=3,AGIPicError_SourceIO=4,AGIPicError_MalformedGBC1=5;
static struct { uint8_t Image[8192];uint32_t Offset=0; } SwapBuffers[Num8kSwapBuffers];
static int8_t AGIPictureBorrowSlot() { return 13; }
#include "native-raw-pages.inc"
static bool AGIPictureFindCachedGBC1Span(uint32_t,uint16_t,const uint8_t **) { return false; }
static bool AGIPictureReadRaw(uint32_t raw,uint8_t *data,uint8_t *error) {
  uint8_t *page=nullptr;if(!AGIPictureResolveRawPage(raw,&page,error))return false;
  *data=page[raw&8191];return true;
}
#include "native-raw-bytes.inc"
static bool selected=true;
static bool MPE3TitleSelected() { return selected; }
#include "native-logical-read.inc"
static void put16(std::vector<uint8_t> &b,size_t at,uint16_t v) { b[at]=v>>8;b[at+1]=v; }
static void put32(std::vector<uint8_t> &b,size_t at,uint32_t v) { put16(b,at,v>>16);put16(b,at+2,v); }
static std::vector<uint8_t> header(bool native=true,const char *name="SQ1 MPE3 TITLE PULL") {
  std::vector<uint8_t> b(64);std::memcpy(b.data(),"C64 CARTRIDGE   ",16);
  put32(b,16,64);put16(b,20,256);put16(b,22,32);b[24]=1;
  if(native)std::memcpy(b.data()+32,name,std::strlen(name));return b;
}
static void chip(std::vector<uint8_t> &b,uint16_t page) {
  size_t at=b.size();b.resize(at+8208);std::memcpy(b.data()+at,"CHIP",4);
  put32(b,at+4,8208);put16(b,at+8,2);put16(b,at+10,page/2);
  put16(b,at+12,page&1?0xa000:0x8000);put16(b,at+14,8192);
  for(unsigned i=0;i<8192;i++)b[at+16+i]=uint8_t((page*37u+i*13u)^(page>>1));
}
static bool load(const std::vector<uint8_t> &b,bool failSeek=false) {
  SD.state=std::make_shared<FileState>();SD.state->bytes=b;SD.state->failSeek=failSeek;
  bool ok=LoadFile(&DriveDirMenu,&SD);std::memset(BankDecode,0,sizeof(BankDecode));
  for(unsigned i=0;i<NumCrtChips;i++)if(CrtChips[i].BankNum<64)
    BankDecode[CrtChips[i].BankNum][CrtChips[i].LoadAddress==0xa000]=CrtChips[i].ChipROM;
  for(auto &s:SwapBuffers)s.Offset=0;AGIPicSourceSlot=-1;return ok;
}
static std::vector<uint8_t> bytes(const char *name) {
  std::ifstream f(name,std::ios::binary);check(bool(f));return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char **argv) {
  for(const char *name:{"SQ1 MPE3 TITLE PULL","MHS DOSVM"}) {
    const auto h=header(true,name);check(mpe4cart::matches(h.data()));
    // Both identities use the complete fixed header, including name padding.
    for(unsigned offset=0;offset<64;offset++) {
      if(offset>=24&&offset<32)continue;
      auto bad=h;bad[offset]^=1;check(!mpe4cart::matches(bad.data()));
    }
    auto small=h;for(unsigned page=0;page<3;page++)chip(small,page);
    check(load(small));check(MPE4CrtDirectory.native&&NumCrtChips==3);
    check(MPE4CrtDirectory.pages[0]&&MPE4CrtDirectory.pages[1]&&MPE4CrtDirectory.pages[2]);
  }
  auto image=header();for(unsigned p=0;p<512;p++)if(p/2!=58)chip(image,p);
  check(load(image));check(NumCrtChips==126);check(chipMemory.size()==126);
  check(MPE4CrtDirectory.native&&MPE4CrtDirectory.pages[510]&&MPE4CrtDirectory.pages[511]);
  check(!MPE4CrtDirectory.pages[116]&&!MPE4CrtDirectory.pages[117]);
  uint8_t data[512];
  for(uint32_t start:{0u,0xe7f00u,0xe8000u,0xfbf00u,0xfc000u,0x3fbe00u}) {
    check(MPE4Read(nullptr,start,data,sizeof(data)));
    for(unsigned i=0;i<sizeof(data);i++) {
      uint32_t physical=start+i+(start+i>=mpe4cart::HoleStart?mpe4cart::HoleBytes:0);
      unsigned page=physical>>13,offset=physical&8191;
      check(data[i]==uint8_t((page*37u+offset*13u)^(page>>1)));
    }
  }
  uint8_t error=0;check(!AGIPictureReadRawBytes(0xe8000,data,1,&error));
  check(!MPE4Read(nullptr,0x3fc000,data,1));check(!MPE4Read(nullptr,0x3fbfff,data,2));
  check(!MPE4Read(nullptr,0xffffffff,data,2));check(!MPE4Read(nullptr,0,data,0));
  selected=false;check(!MPE4Read(nullptr,0,data,1));selected=true;
  // Concrete malformed containers must fail before any upper CHIP allocation.
  auto broken=image;broken.pop_back();check(!load(broken));check(!MPE4CrtDirectory.native&&NumCrtChips==0);
  broken=header();chip(broken,0);chip(broken,1);chip(broken,116);check(!load(broken));
  broken=header();chip(broken,0);chip(broken,1);chip(broken,512);check(!load(broken));
  broken=header();chip(broken,0);chip(broken,1);chip(broken,128);chip(broken,128);check(!load(broken));
  broken=header();chip(broken,0);check(!load(broken));
  broken=header();chip(broken,1);chip(broken,0);check(!load(broken));
  broken=image;put32(broken,64+4,0x4000);check(!load(broken));
  broken=image;put16(broken,64+12,0xe000);check(!load(broken));
  broken=image;put16(broken,64+8,1);check(!load(broken));
  broken=image;broken.push_back(0x43);check(!load(broken));
  check(!load(image,true));
  auto legacy=header(false);for(unsigned p=0;p<128;p++)chip(legacy,p);
  check(load(legacy));check(!MPE4CrtDirectory.native&&NumCrtChips==128);
  check(AGIPictureRawLimit()==0x100000u);chip(legacy,128);check(!load(legacy));
  uint32_t packageBytes=0,packageCrc=0;uint16_t resources=0;
  if(argc==3) {
    auto crt=bytes(argv[1]),raw=bytes(argv[2]);check(load(crt));
    check(raw.size()==0x100000u||raw.size()==0x400000u);
    // Compare each complete indexed physical page with the host raw image.
    for(unsigned page=0;page<512;page++)if(MPE4CrtDirectory.pages[page]) {
      check(page*8192u+8192u<=raw.size());
      for(unsigned off=0;off<8192;off+=sizeof(data)) {
        check(AGIPictureReadRawBytes(page*8192u+off,data,sizeof(data),&error));
        check(!std::memcmp(data,raw.data()+page*8192u+off,sizeof(data)));
      }
    }
    check(!std::memcmp(raw.data()+0x4000,"M3T1",4));
    const uint8_t *intro=raw.data()+0x4000;
    uint32_t total=intro[8]|uint32_t(intro[9])<<8|uint32_t(intro[10])<<16|uint32_t(intro[11])<<24;
    uint32_t root=(0x4000u+total+255u)&~255u;
    mpe4::Package package;check(package.open(MPE4Read,nullptr,root,mpe4cart::LogicalLimit));
    packageBytes=package.bytes;packageCrc=package.crc;resources=package.count;
    for(unsigned type=0;type<=6;type++)for(unsigned id=0;id<256;id++) {
      mpe4::Entry e;if(package.find(type,id,e))check(package.verify(type,id));
    }
  }
  std::printf("{\"passed\":true,\"checks\":%u,\"indexBytes\":%zu,\"maxPhysicalBytes\":4194304,\"maxLogicalBytes\":4177920,\"retainedLowChips\":126,\"packageBytes\":%u,\"packageCrc\":%u,\"resources\":%u}\n",checks,sizeof(MPE4CrtDirectory),packageBytes,packageCrc,resources);
}

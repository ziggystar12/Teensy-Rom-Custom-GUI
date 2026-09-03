#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#define FLASHMEM
#include "../MinimalBoot/Common/Menu_Regs.h"
static uint8_t registers[IO1Size];
static volatile uint8_t* IO1=registers;
static StructMenuItem* MenuSource;
static uint16_t SelItemFullIdx, NumItemsFull;
static char DriveDirPath[256];
static unsigned messages=0;
static const char* UpDirString="/.. <Up Dir>";
#include "../MinimalBoot/Common/IO_Handlers/DesktopMenuView.c"
static void SendMsgPrintfln(const char*, ...) { ++messages; }

// Read-only Arduino FS seam. Production discovery/CRC/guard/launch code runs
// below; this fixture deliberately has no rename/remove/write API.
#define FILE_READ 0
#define INPUT_PULLDOWN 2
struct FS;
struct TestFile { std::string name, data; bool directory=false; };
struct File {
   FS* fs=nullptr;
   std::shared_ptr<TestFile> entry;
   bool root=false;
   size_t position=0;
   File()=default;
   File(FS* source,bool isRoot,std::shared_ptr<TestFile> node=nullptr):fs(source),entry(node),root(isRoot){}
   operator bool() const;
   bool isDirectory() const { return root || (entry && entry->directory); }
   uint64_t size() const { return entry ? entry->data.size() : 0; }
   const char* name() const { return entry ? entry->name.c_str() : ""; }
   int read(uint8_t* out,size_t count);
   File openNextFile();
   void close(){fs=nullptr;entry.reset();root=false;}
};
struct FS {
   bool inserted=true,mounted=true,failInit=false,failRoot=false;
   unsigned rootOpens=0,fileOpens=0,initCalls=0,presenceProbes=0;
   size_t bytesRead=0,failReadAfter=SIZE_MAX;
   std::string failOpen;
   std::function<void()> duringRead;
   std::map<std::string,std::shared_ptr<TestFile>> files;
   std::vector<std::string> order;
   static std::string folded(std::string text) {
      for(char& c:text)if(c>='A'&&c<='Z')c=char(c+'a'-'A');
      return text;
   }
   void add(const std::string& name,const std::string& data="firmware HEX fixture",bool directory=false) {
      const std::string key=folded("/"+name);
      if(!files.count(key))order.push_back(key);
      std::shared_ptr<TestFile> entry(new TestFile);
      entry->name=name;entry->data=data;entry->directory=directory;files[key]=entry;
   }
   bool mediaPresent() const{return mounted&&inserted;}
   File open(const char* path,int=FILE_READ) {
      if(!mediaPresent())return {};
      const std::string key=folded(path);
      if(key=="/"){++rootOpens;return failRoot ? File() : File(this,true);}
      ++fileOpens;
      if(key==folded(failOpen)||!files.count(key))return {};
      return File(this,false,files[key]);
   }
};
File::operator bool() const{return fs&&fs->mediaPresent()&&(root||entry);}
File File::openNextFile() {
   if(!*this||!root)return {};
   while(position<fs->order.size()) {
      const std::string key=fs->order[position++];
      if(fs->files.count(key)&&key.find('/',1)==std::string::npos)return File(fs,false,fs->files[key]);
   }
   return {};
}
int File::read(uint8_t* out,size_t count) {
   if(!*this||!entry||entry->directory)return -1;
   if(fs->duringRead){auto callback=fs->duringRead;fs->duringRead=nullptr;callback();}
   if(!*this||fs->bytesRead>=fs->failReadAfter)return -1;
   size_t available=entry->data.size()-position;
   if(count>available)count=available;
   if(count>fs->failReadAfter-fs->bytesRead)count=fs->failReadAfter-fs->bytesRead;
   memcpy(out,entry->data.data()+position,count);position+=count;fs->bytesRead+=count;
   return int(count);
}
static FS firstPartition,SD;
static void pinMode(unsigned pin,unsigned mode){assert(pin==46&&mode==INPUT_PULLDOWN);++SD.presenceProbes;}
static bool digitalReadFast(unsigned pin){assert(pin==46);return SD.inserted;}
static bool SDFullInit(){++SD.initCalls;SD.mounted=SD.inserted&&!SD.failInit;return SD.mediaPresent();}
#include "../MinimalBoot/Common/IO_Handlers/DesktopFirmwareTarget.c"

// Execute the unmodified production launch function. Only external hardware and
// unrelated file parsers are stubbed; a flash call records its source and path
// and returns, as a rejected/failed flash would on the physical device.
static unsigned flashes=0;
static FS* flashSource;
static char flashPath[358];
static void DoFlashUpdate(FS* source,const char* path) {
   ++flashes; flashSource=source; strcpy(flashPath,path);
}
static const size_t MaxNamePathLength=358;
static bool PathIsRoot(){return !strcmp(DriveDirPath,"/");}
static void UpDirectory(){} static void LoadDirectory(FS*){}
static void LoadDxxDirectory(FS*,uint8_t){} static void SetNumItems(uint16_t){}
static uint16_t NumDrvDirMenuItems=0;
static bool LoadDxxFile(StructMenuItem*,FS*){return false;}
static unsigned ordinaryLoads=0;
static bool LoadFile(FS*,const char*,StructMenuItem*){++ordinaryLoads;return false;}
static void MenuChange(){}
static uint8_t RAM_Image[16], *XferImage, *LOROM_Image, *HIROM_Image;
static uint32_t XferSize,StreamOffsetAddr;
static bool ParseCRTHeader(StructMenuItem*,uint8_t*,uint8_t*){return false;}
static void FreeCrtChips(){} static void Printf_dbg(const char*,...){}
static bool ParseChipHeader(uint8_t*,const char*){return false;}
static bool SetTypeFromCRT(StructMenuItem*,uint8_t,uint8_t){return false;}
static void ParseP00File(StructMenuItem*){} static void ParseSIDHeader(const char*){}
static bool ParseKLAHeader(){return false;} static bool ParseARTHeader(){return false;}
static uint8_t ToPETSCII(uint8_t c){return c;}
static void IOHandlerSelectInit(){}
static char LatestSIDLoaded[512];
static StructMenuItem TeensyROMMenu[1];
static struct {uint8_t ChipROM[1];size_t ROMSize;} CrtChips[1];
static unsigned NumCrtChips;
static const unsigned CRT_MAIN_HDR_LEN=64,CRT_CHIP_HDR_LEN=16,eepAdNextIOHndlr=0;
static struct {uint8_t read(unsigned){return 0;}} EEPROM;
static bool EmulateVicCycles,doReset;
#define SetGameAssert ((void)0)
#define SetGameDeassert ((void)0)
#define SetExROMAssert ((void)0)
#define SetExROMDeassert ((void)0)
#define NVIC_DISABLE_IRQ(...) ((void)0)
#include "handle-execution-under-test.h"

// The command dispatcher is also production code; record the target passed to
// the already-tested transactional file engine without touching a filesystem.
static struct {
   unsigned state=0, deletes=0, copies=0;
   std::string target;
   void confirmDelete(){} void paste(uint8_t,const char*){}
   void reject(uint8_t,const char*){assert(false);}
   void copy(uint8_t,const char* directory,const char* name){++copies;target=std::string(directory)+"/"+name;}
   void prepareDelete(uint8_t,const char* directory,const char* name){++deletes;target=std::string(directory)+"/"+name;}
} DesktopFileEngine;
static void DesktopFilePublish(){} static void DesktopFileRefresh(){}
#include "file-command-under-test.h"

int main() {
   char first[]="MPE_Firmware-V1.0.4.hex", second[]="Other_Firmware.hex";
   StructMenuItem items[2]={{rtFileHex,0,first,nullptr,1234},{rtFileHex,0,second,nullptr,3456}};
   auto reset=[&]() {
      DesktopFirmwareCancel(); MenuSource=items;NumItemsFull=2;SelItemFullIdx=0;
      IO1[rWRegCurrMenuWAIT]=rmtSD;strcpy(DriveDirPath,"/firmware");
      items[0].ItemType=rtFileHex;items[0].Size=1234;items[0].Name=first;
   };
   auto prepare=[&]() { IO1[wRegControl]=rCtlFirmwarePrepareWAIT;DesktopFirmwareCommand(); };
   auto confirm=[&]() { IO1[wRegControl]=rCtlFirmwareCheckWAIT;DesktopFirmwareCommand(); };
   unsigned checks=0;
   for(unsigned fault=0;fault<9;++fault) {
      reset();prepare();assert(IO1[rRegFirmwareTargetState]==1);
      assert(!strcmp(DesktopFirmware.name,first));
      char path[358];assert(DesktopFirmware.pathName(path,sizeof path));
      assert(!strcmp(path,"/firmware/MPE_Firmware-V1.0.4.hex"));
      confirm();assert(DesktopFirmware.confirmed);
      switch(fault) {
         case 1:SelItemFullIdx=1;break;
         case 2:IO1[rWRegCurrMenuWAIT]=rmtUSBDrive;break;
         case 3:strcpy(DriveDirPath,"/other");break;
         case 4:items[0].Name=second;break;
         case 5:items[0].ItemType=rtFilePrg;break;
         case 6:items[0].Size=1235;break;
         case 7:MenuSource=items+1;NumItemsFull=1;break;
         case 8:MenuSource=nullptr;NumItemsFull=0;break;
      }
      StructMenuItem selected{};uint8_t source=0xff;
      const auto previousMessages=messages;
      assert(DesktopFirmwareBegin(selected,source)==(fault==0));
      assert(DesktopFirmware.armed && !DesktopFirmware.confirmed);
      if(!fault) {
         assert(source==rmtSD && selected.ItemType==rtFileHex && selected.Size==1234);
         assert(selected.Name==DesktopFirmware.name && !strcmp(selected.Name,first));
         // The launch record/path remain the captured values after validation.
         SelItemFullIdx=1;strcpy(DriveDirPath,"/changed-after-check");
         assert(!strcmp(selected.Name,first));
         assert(DesktopFirmware.pathName(path,sizeof path));
         assert(!strcmp(path,"/firmware/MPE_Firmware-V1.0.4.hex"));
         assert(messages==previousMessages);
      } else assert(messages==previousMessages+1 && IO1[rRegFirmwareTargetState]==2);
      assert(!DesktopFirmwareBegin(selected,source));
      assert(DesktopFirmware.armed && !DesktopFirmware.confirmed);
      ++checks;
   }
   reset();prepare();
   StructMenuItem selected{};uint8_t source=0xff;
   assert(!DesktopFirmwareBegin(selected,source)); // A remote start before affirmative cannot flash.
   assert(!DesktopFirmwareBegin(selected,source) && DesktopFirmware.armed);
   ++checks;
   reset();prepare();SelItemFullIdx=1;confirm();assert(IO1[rRegFirmwareTargetState]==2);
   assert(!DesktopFirmwareBegin(selected,source));++checks;
   reset();prepare();confirm();DesktopFirmwareCancel();
   assert(!DesktopFirmware.armed && !DesktopFirmware.confirmed && IO1[rRegFirmwareTargetState]==0);++checks;
   reset();prepare();confirm();prepare();assert(!DesktopFirmware.confirmed);++checks;
   for(const auto& invalid: {std::string(101,'x'),std::string("bad/name.hex"),std::string("bad\\name.hex"),std::string("bad\1name.hex")}) {
      reset();items[0].Name=const_cast<char*>(invalid.c_str());prepare();
      assert(IO1[rRegFirmwareTargetState]==3 && !DesktopFirmware.confirmed);++checks;
   }
   reset();items[0].ItemType=rtFilePrg;prepare();assert(IO1[rRegFirmwareTargetState]==3);++checks;
   reset();IO1[rWRegCurrMenuWAIT]=rmtTeensy;prepare();assert(IO1[rRegFirmwareTargetState]==3);++checks;
   reset();strcpy(DriveDirPath,"/");prepare();char root[358];
   assert(DesktopFirmware.pathName(root,sizeof root) && !strcmp(root,"/MPE_Firmware-V1.0.4.hex"));
   char tiny[5];assert(!DesktopFirmware.pathName(tiny,sizeof tiny));++checks;

   // No prepare means the classic recovery-menu HEX path remains reachable.
   reset();unsigned before=flashes;HandleExecution();
   assert(flashes==before+1 && flashSource==&SD);
   assert(!strcmp(flashPath,"/firmware/MPE_Firmware-V1.0.4.hex"));++checks;
   // Prepare alone must block every start, even if a remote selects another file.
   reset();prepare();before=flashes;HandleExecution();SelItemFullIdx=1;HandleExecution();
   assert(flashes==before && DesktopFirmware.armed && !DesktopFirmware.confirmed);++checks;
   // A changed selection after affirmative must never reach the flasher, on any attempt.
   reset();prepare();confirm();SelItemFullIdx=1;before=flashes;
   HandleExecution();HandleExecution();assert(flashes==before && DesktopFirmware.armed);++checks;
   // The approved source/name reach the actual flasher, then a returned flash
   // consumes its approval and subsequent starts remain guarded.
   reset();IO1[rWRegCurrMenuWAIT]=rmtUSBDrive;prepare();confirm();before=flashes;
   HandleExecution();assert(flashes==before+1 && flashSource==&firstPartition);
   assert(!strcmp(flashPath,"/firmware/MPE_Firmware-V1.0.4.hex"));
   HandleExecution();SelItemFullIdx=1;HandleExecution();assert(flashes==before+1);++checks;
   // Explicit close ends the capture; the next independent classic launch works.
   DesktopFirmwareCancel();HandleExecution();
   assert(flashes==before+2 && !strcmp(flashPath,"/firmware/Other_Firmware.hex"));++checks;
   // New preparation/affirmative authorizes exactly one fresh captured attempt.
   reset();prepare();HandleExecution();prepare();confirm();before=flashes;
   HandleExecution();HandleExecution();assert(flashes==before+1);++checks;
   // Invalid raw selection still cannot launch via the unguarded classic path.
   reset();SelItemFullIdx=2;before=flashes;HandleExecution();assert(flashes==before);++checks;

   // A scroll remaps the highlighted local slot, raw-name capture, launch and
   // file commands together, including a filtered parent before that target.
   reset();std::vector<std::string> names;names.reserve(34);
   std::vector<StructMenuItem> menu;menu.reserve(34);
   for(unsigned i=0;i<34;++i) {
      names.push_back(i==2 ? UpDirString : "Firmware-"+std::to_string(i)+".hex");
      menu.push_back({uint8_t(i==2 ? rtDirectory : rtFileHex),0,&names.back()[0],nullptr,100+i});
   }
   MenuSource=menu.data();NumItemsFull=menu.size();IO1[rwRegMenuView]=2;
   MenuViewRebuild();IO1[rwRegCursorItemOnPg]=3;MenuViewSetTop(0);
   assert(SelItemFullIdx==4);MenuViewWriteTopLow(4);MenuViewWriteTopHigh(0);
   assert(SelItemFullIdx==8 && IO1[rwRegCursorItemOnPg]==3);
   prepare();assert(!strcmp(DesktopFirmware.name,"Firmware-8.hex"));
   confirm();before=flashes;HandleExecution();
   assert(flashes==before+1 && !strcmp(flashPath,"/firmware/Firmware-8.hex"));++checks;
   IO1[wRegControl]=rCtlFileDeletePrepareWAIT;DesktopFileCommand();
   assert(DesktopFileEngine.deletes==1 && DesktopFileEngine.target=="/firmware/Firmware-8.hex");++checks;
   MenuViewSetTop(8);assert(SelItemFullIdx==12);
   IO1[wRegControl]=rCtlFileCopyWAIT;DesktopFileCommand();
   assert(DesktopFileEngine.copies==1 && DesktopFileEngine.target=="/firmware/Firmware-12.hex");++checks;
   std::printf("%u firmware target checks passed\n",checks);

   unsigned discoveryChecks=0;
   using DesktopFirmwareVersions::Version;
   Version installed;
   assert(DesktopFirmwareVersions::installed(installed));
   const std::string prefix="MPE_Firmware-V";
   const std::string newerVersion=std::to_string(installed.part[0]+1)+".0.0";
   const std::string newer=prefix+newerVersion+".hex";
   for(const char* good: {"MPE_Firmware-V0.0.0.hex","MPE_Firmware-V1.0.10.hex",
       "mpe_firmware-v1.2.3.HEX","MPE_FIRMWARE-V12.34.56.hEx","MPE_Firmware-V4294967295.0.0.hex"}) {
      Version parsed;assert(DesktopFirmwareVersions::filename(good,parsed));++discoveryChecks;
   }
   for(const char* bad: {"","MPE_Firmware-V.hex","MPE_Firmware-V1.hex","MPE_Firmware-V1.2.hex",
       "MPE_Firmware-V1.2.3.4.hex","MPE_Firmware-V1..3.hex","MPE_Firmware-V1.2..hex",
       "MPE_Firmware-V-1.2.3.hex","MPE_Firmware-V+1.2.3.hex","MPE_Firmware-V01.2.3.hex",
       "MPE_Firmware-V1.02.3.hex","MPE_Firmware-V1.2.03.hex","MPE_Firmware-V1.2.3.hex.bak",
       "MPE_Firmware-V1.2.3-restore.hex","MPE_Firmware-V1.2.3_RESTORE.hex",
       "MPE_Firmware-V1.2.3+meta.hex","MPE_Firmware-V1.2.3-beta.hex","MPE_Firmware-V1.2.3.hex ",
       " MPE_Firmware-V1.2.3.hex","MPE_Firmware-V1.2.3.bin","Other_Firmware-V1.2.3.hex",
       "/MPE_Firmware-V1.2.3.hex","sub/MPE_Firmware-V1.2.3.hex","MPE_Firmware-V4294967296.0.0.hex",
       "MPE_Firmware-V1.4294967296.0.hex","MPE_Firmware-V1.0.4294967296.hex"}) {
      Version parsed;assert(!DesktopFirmwareVersions::filename(bad,parsed));++discoveryChecks;
   }
   for(const auto& values: std::vector<std::pair<std::string,std::string>>{
       {"1.0.10","1.0.9"},{"1.10.0","1.9.99"},{"10.0.0","9.99.99"},{"2.0.0","1.99.99"}}) {
      Version a,b;const char* ap=values.first.c_str();const char* bp=values.second.c_str();
      assert(DesktopFirmwareVersions::parse(ap,a)&&!*ap&&DesktopFirmwareVersions::parse(bp,b)&&!*bp);
      assert(DesktopFirmwareVersions::compare(a,b)>0&&DesktopFirmwareVersions::compare(b,a)<0&&
         DesktopFirmwareVersions::compare(a,a)==0);++discoveryChecks;
   }
   auto boot=[&]() {
      reset();DesktopFirmwareResetDiscovery();SD=FS();
      IO1[rWRegCurrMenuWAIT]=rmtUSBDrive;IO1[rwRegMenuView]=2;
      IO1[rwRegViewTopLo]=32;IO1[rwRegViewTopHi]=1;IO1[rwRegCursorItemOnPg]=5;
      strcpy(DriveDirPath,"/Games/Keep.This.View");
   };
   auto discover=[&]() {IO1[wRegControl]=rCtlFirmwareDiscoverWAIT;DesktopFirmwareCommand();};
   auto viewIntact=[&]() {
      assert(MenuSource==items&&SelItemFullIdx==0&&NumItemsFull==2);
      assert(IO1[rWRegCurrMenuWAIT]==rmtUSBDrive&&IO1[rwRegMenuView]==2&&
         IO1[rwRegViewTopLo]==32&&IO1[rwRegViewTopHi]==1&&IO1[rwRegCursorItemOnPg]==5);
      assert(!strcmp(DriveDirPath,"/Games/Keep.This.View"));
   };
   boot();SD.inserted=SD.mounted=false;before=flashes;discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==0&&!DesktopFirmware.armed&&SD.initCalls==0&&SD.rootOpens==0&&flashes==before);
   discover();assert(SD.presenceProbes==1);++discoveryChecks;
   boot();SD.mounted=false;SD.add(newer);discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==1&&SD.initCalls==1&&SD.rootOpens==1);++discoveryChecks;
   boot();SD.mounted=false;SD.failInit=true;discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==0&&SD.initCalls==1&&SD.rootOpens==0);++discoveryChecks;
   boot();discover();discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==0&&SD.rootOpens==1&&SD.fileOpens==0);++discoveryChecks;
   boot();SD.failRoot=true;discover();viewIntact();assert(IO1[rRegFirmwareTargetState]==0);++discoveryChecks;
   boot();SD.add(prefix+MPE_FIRMWARE_VERSION+".hex");SD.add(prefix+"0.0.1.hex");
   SD.add("subfolder");SD.add("subfolder/"+newer);SD.add(newer,"",true);
   SD.add(prefix+newerVersion+"_restore.hex");discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==0&&SD.fileOpens==0);++discoveryChecks;

   // Numeric maximum, never the first or lexicographic maximum. CRC exactly
   // one candidate after scanning; malformed, old and directory entries cost no reads.
   boot();const std::string major=std::to_string(installed.part[0]+1);
   for(const char* suffix:{".0.9.hex",".0.10.hex",".9.999.hex",".10.0.hex"}) SD.add(prefix+major+suffix);
   const std::string highest=prefix+major+".10.0.hex";
   SD.add(prefix+MPE_FIRMWARE_VERSION+".hex");before=flashes;discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==1&&!strcmp(DesktopFirmware.name,highest.c_str()));
   assert(DesktopFirmware.armed&&!DesktopFirmware.confirmed&&flashes==before&&SD.fileOpens==1);
   assert(SD.bytesRead==SD.files[FS::folded("/"+highest)]->data.size());
   const size_t discoveredFiles=SD.files.size(),readBytes=SD.bytesRead;
   DesktopFirmwareCancel();viewIntact();discover();
   assert(IO1[rRegFirmwareTargetState]==0&&!DesktopFirmware.armed&&SD.rootOpens==1&&SD.bytesRead==readBytes);
   assert(SD.files.size()==discoveredFiles);++discoveryChecks;
   DesktopFirmwareResetDiscovery();discover();viewIntact();
   assert(IO1[rRegFirmwareTargetState]==1&&SD.rootOpens==2&&!strcmp(DesktopFirmware.name,highest.c_str()));++discoveryChecks;

   boot();std::string mixed="mpe_FIRMWARE-v"+newerVersion+".HEX";SD.add(mixed);discover();
   assert(!strcmp(DesktopFirmware.name,mixed.c_str()));confirm();before=flashes;HandleExecution();
   assert(flashes==before+1&&flashSource==&SD&&!strcmp(flashPath,("/"+mixed).c_str()));++discoveryChecks;

   for(unsigned fault=0;fault<4;++fault) {
      boot();SD.add(newer,std::string(700,'X'));
      if(fault==0)SD.failOpen="/"+newer;
      if(fault==1)SD.failReadAfter=260;
      if(fault==2)SD.files[FS::folded("/"+newer)]->data.clear();
      if(fault==3)SD.duringRead=[](){DesktopFirmwareCancel();};
      before=flashes;discover();viewIntact();
      assert(IO1[rRegFirmwareTargetState]==0&&!DesktopFirmware.armed&&!DesktopFirmware.confirmed&&flashes==before);
      // A failed optional startup scan must not poison ordinary file launches.
      items[0].ItemType=rtFilePrg;const unsigned loadsBefore=ordinaryLoads;
      HandleExecution();assert(ordinaryLoads==loadsBefore+1&&flashes==before&&!DesktopFirmware.armed);
      ++discoveryChecks;
   }

   boot();SD.add(newer);discover();before=flashes;
   HandleExecution();HandleExecution();assert(flashes==before&&DesktopFirmware.armed&&!DesktopFirmware.confirmed);++discoveryChecks;

   // Exercise both Check and the actual production HandleExecution. In-place
   // same-size replacement must fail just like removal, read error or no media.
   for(unsigned atStart=0;atStart<2;++atStart) for(unsigned fault=0;fault<7;++fault) {
      boot();SD.add(newer,std::string(700,'X'));discover();
      assert(IO1[rRegFirmwareTargetState]==1);
      if(atStart)confirm();
      const std::string key=FS::folded("/"+newer);
      switch(fault) {
         case 0:SD.files.erase(key);break;
         case 1:SD.files[key]->data.push_back('Y');break;
         case 2:SD.files[key]->data[321]='Y';break;
         case 3:SD.inserted=false;break;
         case 4:SD.files[key]->directory=true;break;
         case 5:SD.failOpen="/"+newer;break;
         case 6:SD.failReadAfter=SD.bytesRead+260;break;
      }
      before=flashes;
      if(!atStart){confirm();assert(IO1[rRegFirmwareTargetState]==2&&!DesktopFirmware.confirmed);}
      HandleExecution();HandleExecution();viewIntact();
      assert(flashes==before&&DesktopFirmware.armed&&!DesktopFirmware.confirmed&&IO1[rRegFirmwareTargetState]==2);
      ++discoveryChecks;
   }
   boot();SD.add(newer);discover();confirm();
   // Startup capture has its own immutable source/path; no normal selection
   // is required and remote browser changes cannot retarget the update.
   MenuSource=nullptr;NumItemsFull=0;strcpy(DriveDirPath,"/Changed/After/Confirm");
   before=flashes;HandleExecution();
   assert(flashes==before+1&&flashSource==&SD&&!strcmp(flashPath,("/"+newer).c_str()));
   HandleExecution();assert(flashes==before+1&&DesktopFirmware.armed&&!DesktopFirmware.confirmed);++discoveryChecks;
   boot();SD.add(newer);discover();confirm();prepare();
   assert(!DesktopFirmwareStartup&&!DesktopFirmware.confirmed&&DesktopFirmware.device==rmtUSBDrive);
   assert(!strcmp(DesktopFirmware.name,first));++discoveryChecks;
   std::printf("%u firmware discovery checks passed\n",discoveryChecks);
}

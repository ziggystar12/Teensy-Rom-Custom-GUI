#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
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
#include "../MinimalBoot/Common/IO_Handlers/DesktopFirmwareTarget.c"

// Execute the unmodified production launch function. Only external hardware and
// unrelated file parsers are stubbed; a flash call records its source and path
// and returns, as a rejected/failed flash would on the physical device.
struct FS {};
static FS firstPartition, SD;
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
static bool LoadFile(FS*,const char*,StructMenuItem*){return false;}
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
}

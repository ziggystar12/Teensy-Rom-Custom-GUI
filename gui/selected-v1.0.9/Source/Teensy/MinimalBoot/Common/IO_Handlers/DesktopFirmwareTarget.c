// Included in the full GUI backend. All captures/checks run from the main-loop
// WAIT dispatcher; the IO ISR only publishes an operation or returns one byte.
#include "../../../DesktopFirmwareTarget.h"
#include "../../../DesktopFirmwareVersion.h"
static DesktopFirmwareTarget DesktopFirmware;
static StructMenuItem DesktopFirmwareItem;
static bool DesktopFirmwareStartup = false, DesktopFirmwareScanned = false;
static uint32_t DesktopFirmwareCRC = 0;
static volatile uint32_t DesktopFirmwareGeneration = 0;

void DesktopFirmwareCancel() {
   ++DesktopFirmwareGeneration;
   DesktopFirmwareStartup = false;
   DesktopFirmware.cancel();
   IO1[rRegFirmwareTargetState] = DesktopFirmwareTarget::Idle;
}

void DesktopFirmwareResetDiscovery() { DesktopFirmwareScanned = false; }

// Read-only, bounded storage. The CRC detects same-size replacement between
// discovery, affirmative confirmation, and the final guarded launch.
FLASHMEM static bool DesktopFirmwareFingerprint(const char* path, uint32_t expectedSize, uint32_t& crc) {
   if (!SD.mediaPresent() || !expectedSize) return false;
   File file=SD.open(path,FILE_READ);
   if (!file || file.isDirectory() || file.size()!=expectedSize) { file.close(); return false; }
   uint8_t buffer[256];
   uint32_t remaining=expectedSize, value=UINT32_MAX;
   while (remaining) {
      const size_t count=remaining<sizeof buffer ? remaining : sizeof buffer;
      const int read=file.read(buffer,count);
      if (read<=0 || size_t(read)>count || !SD.mediaPresent()) { file.close(); return false; }
      for (int i=0; i<read; ++i) {
         value^=buffer[i];
         for (unsigned bit=0; bit<8; ++bit) value=(value>>1)^((value&1) ? 0xedb88320u : 0);
      }
      remaining-=uint32_t(read);
   }
   const bool complete=file.size()==expectedSize && file.read(buffer,1)<=0;
   file.close();
   if (!complete) return false;
   crc=~value;
   return true;
}

FLASHMEM static void DesktopFirmwareDiscover() {
   if (DesktopFirmwareScanned) return;
   DesktopFirmwareCancel();
   const uint32_t generation=DesktopFirmwareGeneration;
   // mediaPresent is false before the first SD begin. Use the established
   // DAT3 presence probe to avoid a multi-second SD begin on an empty socket.
   if (!SD.mediaPresent()) {
      pinMode(46,INPUT_PULLDOWN);
      delayMicroseconds(5); // Match Teensy SD's DAT3 input settling interval.
      if (!digitalReadFast(46) || !SDFullInit()) return;
   }
   DesktopFirmwareVersions::Version best;
   if (!DesktopFirmwareVersions::installed(best)) return;
   File directory=SD.open("/",FILE_READ);
   if (!directory || !directory.isDirectory()) { directory.close(); return; }
   char candidate[sizeof DesktopFirmware.name]="";
   uint32_t candidateSize=0;
   while (File entry=directory.openNextFile()) {
      DesktopFirmwareVersions::Version version;
      const char* name=entry.name();
      if (!entry.isDirectory() && name && strlen(name)<sizeof candidate &&
          DesktopFirmwareVersions::filename(name,version) && DesktopFirmwareVersions::compare(version,best)>0) {
         best=version;
         strcpy(candidate,name);
         const uint64_t size=entry.size();
         candidateSize=size<=UINT32_MAX ? uint32_t(size) : 0;
      }
      entry.close();
   }
   directory.close();
   if (generation!=DesktopFirmwareGeneration || !SD.mediaPresent()) return;
   // Failed scans/captures stay retryable by an explicit discovery request.
   // Only a complete no-candidate scan or prepared offer is deduplicated.
   if (!candidate[0]) { DesktopFirmwareScanned=true; return; }
   char path[sizeof candidate+1];
   snprintf(path,sizeof path,"/%s",candidate);
   uint32_t crc=0;
   const bool readable=DesktopFirmwareFingerprint(path,candidateSize,crc);
   if (!readable || generation!=DesktopFirmwareGeneration) return;
   DesktopFirmwareStartup=true;
   DesktopFirmwareCRC=crc;
   if (DesktopFirmware.prepare((uintptr_t)&DesktopFirmwareItem,0,rmtSD,rtFileHex,
       candidateSize,"/",candidate,true))
      DesktopFirmwareItem={rtFileHex,0,DesktopFirmware.name,NULL,candidateSize};
   if (DesktopFirmware.state!=DesktopFirmwareTarget::Ready || generation!=DesktopFirmwareGeneration) {
      DesktopFirmwareCancel();
      return;
   }
   DesktopFirmwareScanned=true; // A completed offer stays latched across Cancel.
   IO1[rRegFirmwareTargetState]=DesktopFirmware.state;
}

FLASHMEM static bool DesktopFirmwareCheck() {
   if (DesktopFirmwareStartup) {
      char path[sizeof DesktopFirmware.name+1];
      uint32_t crc=0;
      const uint32_t generation=DesktopFirmwareGeneration;
      const bool ready=DesktopFirmware.armed && DesktopFirmware.state==DesktopFirmwareTarget::Ready &&
         DesktopFirmware.pathName(path,sizeof path) &&
         DesktopFirmwareFingerprint(path,DesktopFirmware.size,crc) && crc==DesktopFirmwareCRC;
      if (generation!=DesktopFirmwareGeneration) return false;
      if (!ready) DesktopFirmware.state=DesktopFirmwareTarget::Changed;
      IO1[rRegFirmwareTargetState]=DesktopFirmware.state;
      return ready;
   }
   const bool valid = MenuViewSelectionValid();
   const uint8_t source = IO1[rWRegCurrMenuWAIT];
   const StructMenuItem* item = valid ? &MenuSource[SelItemFullIdx] : NULL;
   const bool ready = DesktopFirmware.check((uintptr_t)MenuSource,SelItemFullIdx,source,
      item ? item->ItemType : 0, item ? item->Size : 0, DriveDirPath,item ? item->Name : NULL,
      valid && (source==rmtSD || source==rmtUSBDrive) && item->ItemType==rtFileHex);
   IO1[rRegFirmwareTargetState] = DesktopFirmware.state;
   return ready;
}

FLASHMEM void DesktopFirmwareCommand() {
   if (IO1[wRegControl] == rCtlFirmwareDiscoverWAIT) {
      DesktopFirmwareDiscover();
      return;
   }
   if (IO1[wRegControl] == rCtlFirmwareCheckWAIT) {
      DesktopFirmware.confirmed = DesktopFirmwareCheck();
      return;
   }
   DesktopFirmwareStartup=false;
   const bool valid = MenuViewSelectionValid();
   const uint8_t source = IO1[rWRegCurrMenuWAIT];
   const StructMenuItem* item = valid ? &MenuSource[SelItemFullIdx] : NULL;
   if (DesktopFirmware.prepare((uintptr_t)MenuSource,SelItemFullIdx,source,
       item ? item->ItemType : 0, item ? item->Size : 0,DriveDirPath,item ? item->Name : NULL,
       valid && (source==rmtSD || source==rmtUSBDrive) && item->ItemType==rtFileHex))
      DesktopFirmwareItem = *item;
   IO1[rRegFirmwareTargetState] = DesktopFirmware.state;
}

FLASHMEM bool DesktopFirmwareBegin(StructMenuItem& item, uint8_t& source) {
   const bool ready = DesktopFirmwareCheck() && DesktopFirmware.confirmed;
   // Consume only the affirmative. Keep this flow guarded until explicit cancel,
   // new preparation or reset, including when a failed flash returns to the menu.
   DesktopFirmware.confirmed = false;
   if (!ready) {
      DesktopFirmware.state = DesktopFirmwareTarget::Changed;
      IO1[rRegFirmwareTargetState] = DesktopFirmwareTarget::Changed;
      IO1[rRegStrAvailable] = 0;
      SendMsgPrintfln("Firmware selection changed. Choose the file again.");
      return false;
   }
   item = DesktopFirmwareItem;
   item.Name = DesktopFirmware.name;
   source = DesktopFirmware.device;
   return true;
}

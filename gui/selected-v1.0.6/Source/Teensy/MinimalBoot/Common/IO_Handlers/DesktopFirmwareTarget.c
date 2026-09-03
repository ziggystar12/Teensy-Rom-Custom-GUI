// Included in the full GUI backend. All captures/checks run from the main-loop
// WAIT dispatcher; the IO ISR only publishes an operation or returns one byte.
#include "../../../DesktopFirmwareTarget.h"
static DesktopFirmwareTarget DesktopFirmware;
static StructMenuItem DesktopFirmwareItem;

FLASHMEM static bool DesktopFirmwareCheck() {
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
   if (IO1[wRegControl] == rCtlFirmwareCheckWAIT) {
      DesktopFirmware.confirmed = DesktopFirmwareCheck();
      return;
   }
   const bool valid = MenuViewSelectionValid();
   const uint8_t source = IO1[rWRegCurrMenuWAIT];
   const StructMenuItem* item = valid ? &MenuSource[SelItemFullIdx] : NULL;
   if (DesktopFirmware.prepare((uintptr_t)MenuSource,SelItemFullIdx,source,
       item ? item->ItemType : 0, item ? item->Size : 0,DriveDirPath,item ? item->Name : NULL,
       valid && (source==rmtSD || source==rmtUSBDrive) && item->ItemType==rtFileHex))
      DesktopFirmwareItem = *item;
   IO1[rRegFirmwareTargetState] = DesktopFirmware.state;
}

void DesktopFirmwareCancel() {
   DesktopFirmware.cancel();
   IO1[rRegFirmwareTargetState] = DesktopFirmwareTarget::Idle;
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

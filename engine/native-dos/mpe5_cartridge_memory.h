// Included inside the native M3 service, after its cartridge and DMA state.
#pragma once

#include <stdint.h>

extern uint8_t RAM_Image[RAM_ImageSize];
extern uint8_t NumCrtChips;
extern StructCrtChip CrtChips[MAX_CRT_CHIPS];

struct MPE5ResetOnlyArena
{
   uint8_t *workspace;
   uint32_t workspaceBytes;
   uint8_t *highChunks;
   uint32_t highStorageBytes;
   uint32_t highStride;
};

// Foreground only. Retain every loaded cartridge byte and borrow its unused
// RAM1 tail; never borrow swap images or dynamically allocated CRT chips.
static FLASHMEM bool MPE5BorrowCartridgeTail(uint8_t **Begin, uint32_t *Bytes)
{
   if (!Begin || !Bytes) return false;
   *Begin = nullptr;
   *Bytes = 0;

   if (!MPE3TitleOwned || !MPE3TitleSelected() ||
       DMA_State != DMA_S_DisableReady || !MPE4CrtDirectory.native ||
       NumCrtChips < 2 || NumCrtChips > MAX_CRT_CHIPS ||
       AGIPicActive || AGIPicPendingCommand || AGIPicResetPending ||
       AGIPicAbortRequested || MPEThinUpgradePending)
      return false;

   const uintptr_t Base = (uintptr_t)RAM_Image;
   if ((uintptr_t)RAM_ImageSize > UINTPTR_MAX - Base) return false;
   const uintptr_t Limit = Base + (uintptr_t)RAM_ImageSize;
   uintptr_t End = Base;

   for (uint16_t Index = 0; Index < NumCrtChips; ++Index)
   {
      const StructCrtChip &Chip = CrtChips[Index];
      if (Chip.ROMSize != 0x2000u || Chip.BankNum >= 64u ||
          Chip.BankNum == 58u ||
          (Chip.LoadAddress != 0x8000u && Chip.LoadAddress != 0xA000u))
         return false;

      const uint8_t Half = Chip.LoadAddress == 0xA000u;
      const uint16_t Page = Chip.BankNum * 2u + Half;
      // The native loader requires bank-zero low/high to arrive first.
      if ((Index < 2 && Page != Index) ||
          !MPE4CrtDirectory.pages[Page] ||
          BankDecode[Chip.BankNum][Half] != Chip.ChipROM ||
          (uintptr_t)Chip.ChipROM != End || End > Limit ||
          (uintptr_t)Chip.ROMSize > Limit - End)
         return false;
      End += Chip.ROMSize;
   }

   const uintptr_t Padding = (32u - (End & 31u)) & 31u;
   if (Padding >= Limit - End) return false;
   // Bank selection can change in the bus ISR while validating.
   if (!MPE3TitleOwned || !MPE3TitleSelected() ||
       DMA_State != DMA_S_DisableReady)
      return false;

   *Begin = (uint8_t *)(End + Padding);
   *Bytes = (uint32_t)(Limit - End - Padding);
   return true;
}

// DOS permanently takes the sixteen EasyFlash swap images after its tiny CRT
// has been proven fully resident. The four-byte metadata after each image is
// deliberately skipped by DirectMemory's strided B0000-CFFFF mapping.
static FLASHMEM bool MPE5BorrowResetOnlyArena(MPE5ResetOnlyArena *Arena)
{
   static_assert(Num8kSwapBuffers >= 16,
                 "DOS high memory needs sixteen 8 KiB swap images");
   static_assert(sizeof(SwapBuffers[0].Image) == 0x2000u,
                 "EasyFlash swap image size changed");
   if (!Arena) return false;
   memset(Arena, 0, sizeof(*Arena));
   if (!MPE5BorrowCartridgeTail(&Arena->workspace, &Arena->workspaceBytes))
      return false;

   const uintptr_t SwapBase = (uintptr_t)&SwapBuffers[0];
   const uintptr_t SwapLimit = SwapBase + sizeof(SwapBuffers);
   if (SwapLimit < SwapBase) return false;
   for (uint8_t Bank = 0; Bank < NumDecodeBanks; ++Bank)
      for (uint8_t Half = 0; Half < 2; ++Half)
      {
         const uintptr_t Pointer = (uintptr_t)BankDecode[Bank][Half];
         if (Pointer >= SwapBase && Pointer < SwapLimit) return false;
      }
#if defined(FeatAGIPictureDMA) && defined(Fab04_FullDMACapable)
   for (uint8_t Slot = 0; Slot < 16; ++Slot)
      if (AGIPictureSwapBufferIsOwned(Slot)) return false;
#endif

   Arena->highChunks = SwapBuffers[0].Image;
   Arena->highStorageBytes = (uint32_t)(SwapLimit -
      (uintptr_t)Arena->highChunks);
   Arena->highStride = sizeof(SwapBuffers[0]);
   return true;
}

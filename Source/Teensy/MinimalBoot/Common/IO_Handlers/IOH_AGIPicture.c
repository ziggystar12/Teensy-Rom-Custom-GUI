// MIT License
//
// Copyright (c) 2026
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// MHS Power Engine for TeensyROM+
// --------------------------------
// The original AGI+2/AGI+3 mailbox remains byte-for-byte compatible. MPE1
// adds a product-neutral service registry and bounded PowerVM tasks so other
// cartridges can use the same installed firmware without game-specific code.
// AGI+2 remains byte-for-byte compatible with the first EasyFlash prototype.
// AGI+3 adds bounded descriptor-driven picture decoding, Magic Desk 2 paging,
// Exomizer raw-forward streams, mutable compact-priority materialization,
// picture/scene prefetch, exact GAC3 cell-patch scatter DMA, and a retained
// room-art service.  The latter snapshots the live packed C64 background, then
// resolves GBC1 cels and publishes only bounded actor dirty-cell runs.

#include <util/atomic.h>

#if !defined(MinimumBuild) || !defined(Fab04_FullDMACapable)
   #error IOH_AGIPicture requires MinimalBoot on DMA-capable TeensyROM+ v0.4 hardware
#endif

static_assert(Num8kSwapBuffers >= 14,
              "AGI+3 needs eight scene slots, three picture slots, one source slot, and two mapped cartridge slots");

extern bool PerformDMA(bool RnW, uint16_t StartAddr, uint8_t *Buffer,
                       uint32_t Length, bool FixC64Addr);
extern bool AGIContinueDMA(bool RnW, uint16_t StartAddr, uint8_t *Buffer,
                           uint32_t Length, bool FixC64Addr);
extern bool CloseDMA();
extern "C" uint8_t external_psram_size;

enum enumAGIPictureLayout : uint8_t
{
   AGIPicLayout_EasyFlash = 0,
   AGIPicLayout_MagicDesk2 = 1,
};

enum enumAGIPictureRegister : uint8_t
{
   AGIPicReg_ID0           = 0xF0,
   AGIPicReg_ID1           = 0xF1,
   AGIPicReg_ID2           = 0xF2,
   AGIPicReg_ID3           = 0xF3,
   AGIPicReg_Version       = 0xF4,
   AGIPicReg_Capabilities  = 0xF5,
   AGIPicReg_Command       = 0xF6,
   AGIPicReg_Status        = 0xF7,
   AGIPicReg_Source0       = 0xF8,
   AGIPicReg_Source1       = 0xF9,
   AGIPicReg_Source2       = 0xFA,
   AGIPicReg_Error         = 0xFB,
   AGIPicReg_Argument0     = 0xFC,
   AGIPicReg_Argument1     = 0xFD,
   AGIPicReg_MachineFlags  = 0xFE,
   AGIPicReg_Token         = 0xFF,
};

enum enumAGIPictureCommand : uint8_t
{
   AGIPicCmd_Acknowledge       = 0x00,
   AGIPicCmd_V2DecodeDMA       = 0x01,
   AGIPicCmd_V2DecodeOnly      = 0x02,
   AGIPicCmd_V2DMAProbe        = 0x03,
   AGIPicCmd_V3DecodeDMA       = 0x10,
   AGIPicCmd_V3PrefetchPicture = 0x11,
   AGIPicCmd_V3CommitPrefetch  = 0x12,
   AGIPicCmd_V3PatchDMA        = 0x20,
   AGIPicCmd_V3PrefetchScene   = 0x21,
   AGIPicCmd_V3RoomSeed        = 0x22,
   AGIPicCmd_V3ActorFrame      = 0x23,
   AGIPicCmd_MPEQuery          = 0x2F,
   AGIPicCmd_MPEPowerVM        = 0x30,
   AGIPicCmd_MPEAGIScan        = 0x31,
   AGIPicCmd_MPEPriorityLine   = 0x32,
   AGIPicCmd_Reset             = 0x7F,
};

enum enumAGIPictureStatus : uint8_t
{
   AGIPicStatus_Ready             = 0x00,
   AGIPicStatus_Decoding          = 0x01,
   AGIPicStatus_DMA               = 0x02,
   AGIPicStatus_V2DoneDMA         = 0x80,
   AGIPicStatus_V2DoneDecodeOnly  = 0x81,
   AGIPicStatus_V2DoneDMAProbe    = 0x82,
   AGIPicStatus_V3DonePicture     = 0x90,
   AGIPicStatus_V3PictureReady    = 0x91,
   AGIPicStatus_V3PrefetchDone    = 0x92,
   AGIPicStatus_V3DonePatch       = 0xA0,
   AGIPicStatus_V3SceneReady      = 0xA1,
   AGIPicStatus_V3RoomSeeded      = 0xA2,
   AGIPicStatus_V3DoneActorFrame  = 0xA3,
   AGIPicStatus_MPEQueryDone      = 0xAF,
   AGIPicStatus_MPEPowerVMDone    = 0xB0,
   AGIPicStatus_MPEAGIScanDone    = 0xB1,
   AGIPicStatus_MPEPriorityDone   = 0xB2,
   AGIPicStatus_ErrorBase         = 0xE0,
};

enum enumAGIPictureError : uint8_t
{
   AGIPicError_None             = 0,
   AGIPicError_Locked           = 1,
   AGIPicError_Busy             = 2,
   AGIPicError_BadCommand       = 3,
   AGIPicError_InvalidSource    = 4,
   AGIPicError_SourceIO         = 5,
   AGIPicError_MalformedRLE     = 6,
   AGIPicError_NoWorkspace      = 7,
   AGIPicError_DMAUnavailable   = 8,
   AGIPicError_DMAProbeMismatch = 9,
   AGIPicError_DMATimeout       = 10,
   AGIPicError_BadDescriptor    = 11,
   AGIPicError_UnsupportedCodec = 12,
   AGIPicError_MalformedExo     = 13,
   AGIPicError_MalformedPriority = 14,
   AGIPicError_OutOfBounds      = 15,
   AGIPicError_PrefetchMiss     = 16,
   AGIPicError_MalformedGAC3    = 17,
   AGIPicError_MalformedPatch   = 18,
   AGIPicError_MalformedGBC1    = 19,
   AGIPicError_DirtyOverflow    = 20,
   AGIPicError_InvalidPowerTask = 21,
   AGIPicError_PowerVMFault     = 22,
   AGIPicError_PowerVMBudget    = 23,
   AGIPicError_InvalidScanTask  = 24,
   AGIPicError_ScanUnsupported  = 25,
   AGIPicError_ScanBudget       = 26,
   AGIPicError_InvalidPriorityQuery = 27,
};

static constexpr uint8_t AGIPicProtocolV2 = 2;
static constexpr uint8_t AGIPicProtocolV3 = 3;
static constexpr uint8_t AGIPicV2Capabilities = 0x0F;
static constexpr uint8_t AGIPicV3Capabilities = 0xFF;
static constexpr uint8_t AGIPicV3Challenge = 0x3C;
static constexpr uint8_t AGIPicV3Response = 0xC3;
static constexpr uint8_t MHSPEProtocolVersion = 1;
static constexpr uint8_t MHSPECapabilities = 0xFF;
static constexpr uint8_t MHSPEServiceAGI = 0x01;
static constexpr uint8_t MHSPEServicePowerVM = 0x10;
static constexpr uint8_t MHSPEServiceAGIScan = 0x11;
static constexpr uint8_t MHSPEServicePriorityLine = 0x12;
static constexpr uint8_t MHSPEDescriptorBytes = 24;
static constexpr uint16_t MHSPEMaximumIOBytes = 256;
static constexpr uint16_t MHSPEMaximumCodeBytes = 255;
static constexpr uint8_t MHSPEScanDescriptorBytes = 24;
static constexpr uint8_t MHSPEScanFormatPredecoded = 1;
static constexpr uint8_t MHSPEScanCapabilities = 0x03;
static constexpr uint8_t MHSPEPriorityLineCapabilities = 0x07;
static constexpr uint8_t MHSPEPriorityLinePolicyMask = 0x23;
static constexpr uint8_t MHSPEPriorityLineResultPass = 0x01;
static constexpr uint8_t MHSPEPriorityLineResultTrigger = 0x02;
static constexpr uint8_t MHSPEPriorityLineResultAllWater = 0x04;
static constexpr uint16_t MHSPEScanVariablesAddress = 0x08C2;
static constexpr uint16_t MHSPEScanFlagsAddress = 0x09C2;
static constexpr uint16_t MHSPEScanControllerAddress = 0x5A00;
static constexpr uint16_t MHSPEScanParsedCountAddress = 0x087A;
static constexpr uint16_t MHSPEScanParsedWordLowAddress = 0x5501;
static constexpr uint16_t MHSPEScanParsedWordHighAddress = 0x5515;
static constexpr uint8_t MHSPEScanParsedWordLimit = 20;
static_assert(MHSPEScanFlagsAddress == MHSPEScanVariablesAddress + 256u,
              "AGI variables and flags must remain one fixed DMA span");
static_assert(MHSPEScanParsedWordHighAddress ==
              MHSPEScanParsedWordLowAddress + MHSPEScanParsedWordLimit,
              "AGI parsed word halves must remain one fixed DMA span");
static constexpr uint32_t AGIPicV2PhysicalPrefixSize = 24u * 16384u;
static constexpr uint16_t AGIPicBitmapLength = 8000;
static constexpr uint16_t AGIPicScreenLength = 1000;
static constexpr uint16_t AGIPicColourLength = 1000;
static constexpr uint16_t AGIPicPriorityLength = 13440;
static constexpr uint16_t AGIPicPriorityFirstLength = 8192;
static constexpr uint16_t AGIPicPrioritySecondLength =
   AGIPicPriorityLength - AGIPicPriorityFirstLength;
static constexpr uint16_t AGIPicCompactPriorityMaximum = 0x3300;
static constexpr uint16_t AGIPicDMAProbeAddress = 0x0400;
static constexpr uint8_t AGIPicDMAProbeLength = 16;
static constexpr uint32_t AGIPicGAC3MaximumScene = 0xFFFFu;
static constexpr uint8_t AGIPicMaximumSceneSlots = 8;
static constexpr uint8_t AGIPicDescriptorBytes = 16;
// A valid patch has at most 1,000 one-cell runs. Each run contributes a
// three-byte header and each cell contributes eight bitmap bytes plus screen
// and colour. Materializing that strict upper bound guarantees every bounded
// write segment is sourced locally, with no cartridge read while /DMA is held.
static constexpr uint16_t AGIPicPatchMaximumBytes = 1000u * 13u;
static constexpr uint8_t AGIPicActorCount = 20;
static constexpr uint16_t AGIPicActorTableBytes = AGIPicActorCount * 8u;
static constexpr uint16_t AGIPicActorDirtyLimit = 212;
static constexpr uint16_t AGIPicGBC1IndexBytes = 256u * 3u;
static constexpr uint8_t AGIPicGBC1PatternBytes = 16;
static constexpr uint8_t AGIPicGBC1PatternCacheEntries = 128;
static constexpr uint8_t AGIPicGBC1ViewCacheEntries = AGIPicActorCount;
static constexpr uint32_t AGIPicGBC1ViewCacheSlotBytes = 0xFFFFu;
static constexpr uint32_t AGIPicGBC1ViewCacheCapacity = (uint32_t)AGIPicGBC1ViewCacheEntries * AGIPicGBC1ViewCacheSlotBytes;
static constexpr uint16_t AGIPicGBC1MaximumCels =
   (0xFFFFu - 20u) / 8u;
static constexpr uint16_t AGIPicGBC1MaximumPatterns = 0xFFFFu / 16u;
static_assert((uint32_t)AGIPicGBC1ViewCacheEntries *
              AGIPicGBC1ViewCacheSlotBytes <= AGIPicGBC1ViewCacheCapacity,
              "the complete current actor cohort must fit in PSRAM");
static constexpr uint8_t AGIPicMachine_NTSC = 0x01;
static constexpr uint8_t AGIPicMachine_InvalidateRoomArt = 0x80;

static constexpr uint8_t AGIPicCapability_FullPicture = 0x01;
static constexpr uint8_t AGIPicCapability_Prefetch = 0x02;
static constexpr uint8_t AGIPicCapability_Patch = 0x04;
static constexpr uint8_t AGIPicCapability_RLE = 0x08;
static constexpr uint8_t AGIPicCapability_Exomizer = 0x10;
static constexpr uint8_t AGIPicCapability_CompactPriority = 0x20;
static constexpr uint8_t AGIPicCapability_MD2Paged = 0x40;
static constexpr uint8_t AGIPicCapability_Timing = 0x80;
static_assert((AGIPicCapability_FullPicture | AGIPicCapability_Prefetch |
               AGIPicCapability_Patch | AGIPicCapability_RLE |
               AGIPicCapability_Exomizer | AGIPicCapability_CompactPriority |
               AGIPicCapability_MD2Paged | AGIPicCapability_Timing) ==
              AGIPicV3Capabilities, "AGI+3 capability ABI changed");

static constexpr uint8_t AGIPicDMAProbeSeed[AGIPicDMAProbeLength] =
{
   0xA5, 0xA4, 0xA7, 0xA6, 0xA1, 0xA0, 0xA3, 0xA2,
   0xAD, 0xAC, 0xAF, 0xAE, 0xA9, 0xA8, 0xAB, 0xAA,
};

struct stcAGIOutput
{
   uint8_t *First;
   uint16_t FirstLength;
   uint8_t *Second;
   uint16_t SecondLength;
   uint16_t Position;
   uint16_t Limit;
};

struct stcAGISource
{
   uint32_t Cursor;
   uint32_t End;
   uint8_t Error;
};

struct stcAGIDecodedPicture
{
   uint8_t *Bitmap;
   uint8_t *Screen;
   uint8_t *Colour;
   uint8_t *PriorityFirst;
   uint8_t *PrioritySecond;
   uint16_t PriorityLength;
   uint32_t DescriptorRaw;
   uint8_t Token;
   uint8_t PriorityFormat;
   bool HasPriority;
};

// The C64 object table is eight contiguous twenty-byte columns.  Retaining the
// normalized visual fields (and resolved geometry) is enough to restore both
// the old and new actor footprints without moving AGI game state to Teensy.
struct stcAGIActorState
{
   uint8_t X;
   uint8_t Y;
   uint8_t View;
   uint8_t Loop;
   uint8_t Cel;
   uint8_t Priority;
   uint8_t Flags;
   uint8_t Width;
   uint8_t Height;
};

struct stcAGIActorCel
{
   stcAGIActorState State;
   uint32_t ViewRaw;
   uint32_t PayloadRaw;
   uint16_t ViewLength;
   uint16_t PayloadOffset;
   uint16_t DictionaryEnd;
   uint16_t PatternDirectoryBytes;
   uint8_t Columns;
   uint8_t Rows;
   uint8_t Mirror;
   uint8_t EffectivePriority;
   bool Drawn;
};

struct stcAGIGBC1ViewCache
{
   uint32_t Raw;
   uint32_t Offset;
   uint32_t Generation;
   uint16_t Length;
   bool Cached;
   bool Validated;
};

struct stcAGIGBC1PayloadInfo
{
   uint16_t Offset;
   uint16_t End;
   uint8_t Width;
   uint8_t Height;
};

union stcAGIGBC1ValidationScratch
{
   stcAGIGBC1PayloadInfo Payloads[AGIPicGBC1MaximumCels];
   uint32_t PatternHashes[AGIPicGBC1MaximumPatterns];
};

struct stcAGIExoTableEntry
{
   uint8_t Bits;
   uint16_t Base;
};

static volatile uint8_t AGIPicRegisters[16];
static volatile uint8_t AGIPicUnlockStage;
static volatile bool AGIPicActive;
static volatile bool AGIPicChallengeSeen;
static volatile bool AGIPicChallengeResponsePending;
static volatile bool MHSPEActive;
static volatile bool AGIPicHoldHelperBank;
static volatile bool AGIPicAbortRequested, AGIPicResetPending;
static volatile uint8_t AGIPicPendingCommand;
static uint8_t AGIPicProtocol;
static uint8_t AGIPicLayout;
static volatile bool AGIPicSlotOwned[Num8kSwapBuffers];
static int8_t AGIPicPictureSlots[3] = {-1, -1, -1};
static int8_t AGIPicSourceSlot = -1;
static int8_t AGIPicSceneSlots[AGIPicMaximumSceneSlots] =
   {-1, -1, -1, -1, -1, -1, -1, -1};
static uint8_t AGIPicSceneSlotCount;
static bool AGIPicPrefetchValid;
static bool AGIPicSceneValid;
static stcAGIDecodedPicture AGIPicPicture;
static uint32_t AGIPicSceneRaw;
static uint32_t AGIPicSceneLength;
static uint8_t AGIPicScenePicture;
static uint8_t AGIPicDMAProbeBuffer[AGIPicDMAProbeLength];
// RAM1/DTCM is intentionally tight in MinimalBoot. The CPU-driven C64 bus
// writer can read OCRAM directly, so keep this bounded staging buffer in
// Teensy RAM2 instead of consuming the remaining DTCM margin.
static DMAMEM uint8_t AGIPicPatchEncoded[AGIPicPatchMaximumBytes];
static uint8_t AGIPicPatchBitmap[40 * 8];
static uint8_t AGIPicPatchScreen[40];
static uint8_t AGIPicPatchColour[40];
static DMAMEM uint8_t MHSPEPowerVMCode[MHSPEMaximumCodeBytes];
static DMAMEM uint8_t MHSPEPowerVMInput[MHSPEMaximumIOBytes];
static DMAMEM uint8_t MHSPEPowerVMOutput[MHSPEMaximumIOBytes];
static DMAMEM uint8_t MHSPEScanCoreState[256 + 32];
static DMAMEM uint8_t MHSPEScanControllers[256];
static uint8_t MHSPEScanParsedCount;
static uint8_t MHSPEScanParsedWords[MHSPEScanParsedWordLimit * 2u];
static bool MHSPEScanBufferedHadMatch;
static bool MHSPEScanValidated;
static uint32_t MHSPEScanValidatedDescriptorRaw;
static uint32_t MHSPEScanValidatedCodeRaw;
static uint16_t MHSPEScanValidatedCodeLength;
static uint16_t MHSPEScanValidatedCodeCRC;
static uint16_t MHSPEScanValidatedDescriptorCRC;

// Room graphics live in RAM2 independently of the three decode/prefetch swap
// slots.  Thus a prepared next picture cannot evict the immutable current-room
// background or its priority map.
static DMAMEM uint8_t AGIPicRoomBitmap[AGIPicBitmapLength];
static DMAMEM uint8_t AGIPicRoomScreen[AGIPicScreenLength];
static DMAMEM uint8_t AGIPicRoomColour[AGIPicColourLength];
static DMAMEM uint8_t AGIPicRoomPriority[AGIPicPriorityLength];
static DMAMEM uint8_t AGIPicActorTable[AGIPicActorTableBytes];
static DMAMEM uint8_t
   AGIPicGBC1PatternCache[AGIPicGBC1PatternCacheEntries][AGIPicGBC1PatternBytes];
static DMAMEM uint8_t AGIPicGBC1IndexCache[AGIPicGBC1IndexBytes];
static uint32_t AGIPicGBC1PatternRaw[AGIPicGBC1PatternCacheEntries];
static stcAGIGBC1ViewCache AGIPicGBC1ViewCache[AGIPicGBC1ViewCacheEntries];
static EXTMEM uint8_t AGIPicGBC1ViewCacheMemory[AGIPicGBC1ViewCacheCapacity];
static uint32_t AGIPicGBC1ViewCacheGeneration;
static DMAMEM stcAGIGBC1ValidationScratch AGIPicGBC1ValidationScratch;
static DMAMEM uint8_t
   AGIPicGBC1PatternReferenced[(AGIPicGBC1MaximumPatterns + 7u) / 8u];
static uint32_t AGIPicGBC1IndexRoot;
static uint8_t AGIPicGBC1PatternNext;
static bool AGIPicGBC1IndexValid;
static uint8_t AGIPicRoomPendingDirty[125];
static stcAGIActorState AGIPicPriorActors[AGIPicActorCount];
static stcAGIActorCel AGIPicCurrentActors[AGIPicActorCount];
static uint8_t AGIPicActorOrder[AGIPicActorCount];
static bool AGIPicRoomValid;
static bool AGIPicActorStateValid;
static bool AGIPicLivePictureValid;
static uint8_t AGIPicRoomToken;
static uint8_t AGIPicLivePictureToken;
static uint8_t AGIPicRoomPriorityFormat;
static uint16_t AGIPicRoomPriorityLength;
static volatile uint8_t AGIPicErrorMailbox;

static bool AGIPictureStatusBusy(uint8_t Status)
{
   return Status == AGIPicStatus_Decoding || Status == AGIPicStatus_DMA;
}

static void AGIPictureAbort();
static void AGIPictureDirtySet(uint8_t Dirty[125], uint16_t Cell);

static void AGIPictureSetError(uint8_t Error)
{
   if (AGIPicAbortRequested)
   {
      AGIPictureAbort();
      return;
   }
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = Error;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_ErrorBase | (Error & 0x1F);
}

static void AGIPictureSetDone(uint8_t Status, uint32_t StartMS)
{
   if (AGIPicAbortRequested)
   {
      AGIPictureAbort();
      return;
   }
   AGIPicHoldHelperBank = false;
   uint16_t ElapsedMS = (uint16_t)(millis() - StartMS);
   AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] = ElapsedMS;
   AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] = ElapsedMS >> 8;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = Status;
}

static void AGIPictureReleaseSource()
{
   if (AGIPicSourceSlot >= 0)
      AGIPicSlotOwned[(uint8_t)AGIPicSourceSlot] = false;
   AGIPicSourceSlot = -1;
}

static void AGIPictureReleasePicture()
{
   for (uint8_t Index = 0; Index < 3; Index++)
   {
      if (AGIPicPictureSlots[Index] >= 0)
      {
         uint8_t Slot = (uint8_t)AGIPicPictureSlots[Index];
         SwapBuffers[Slot].Offset = 0;
         AGIPicSlotOwned[Slot] = false;
      }
      AGIPicPictureSlots[Index] = -1;
   }
   memset(&AGIPicPicture, 0, sizeof(AGIPicPicture));
   AGIPicPrefetchValid = false;
}

static void AGIPictureReleaseScene()
{
   for (uint8_t Index = 0; Index < AGIPicMaximumSceneSlots; Index++)
   {
      if (AGIPicSceneSlots[Index] >= 0)
      {
         uint8_t Slot = (uint8_t)AGIPicSceneSlots[Index];
         SwapBuffers[Slot].Offset = 0;
         AGIPicSlotOwned[Slot] = false;
      }
      AGIPicSceneSlots[Index] = -1;
   }
   AGIPicSceneSlotCount = 0;
   AGIPicSceneValid = false;
   AGIPicSceneRaw = 0;
   AGIPicSceneLength = 0;
   AGIPicScenePicture = 0;
}

static void AGIPictureInvalidateRoomArt()
{
   AGIPicRoomValid = false;
   AGIPicActorStateValid = false;
   AGIPicRoomToken = 0;
   AGIPicRoomPriorityFormat = 0;
   AGIPicRoomPriorityLength = 0;
   memset(AGIPicRoomPendingDirty, 0, sizeof(AGIPicRoomPendingDirty));
   memset(AGIPicGBC1PatternRaw, 0xFF, sizeof(AGIPicGBC1PatternRaw));
   memset(AGIPicGBC1ViewCache, 0, sizeof(AGIPicGBC1ViewCache));
   AGIPicGBC1PatternNext = 0;
   AGIPicGBC1ViewCacheGeneration = 0;
   AGIPicGBC1IndexRoot = 0;
   AGIPicGBC1IndexValid = false;
}

static void AGIPictureInvalidateLivePicture()
{
   AGIPicLivePictureValid = false;
   AGIPicLivePictureToken = 0;
   AGIPictureInvalidateRoomArt();
}

// A reset posted while a command is busy is an abort request, not an ISR-side
// teardown. The poller owns all cache and DMA state, so it performs cleanup and
// atomically makes the bank passable before publishing the terminal result.
static void AGIPictureAbort()
{
   AGIPictureReleaseSource();
   AGIPictureReleasePicture();
   AGIPictureReleaseScene();
   AGIPictureInvalidateLivePicture();
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   AGIPicPendingCommand = AGIPicCmd_Acknowledge;
   AGIPicAbortRequested = false;
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_DMATimeout;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_ErrorBase | AGIPicError_DMATimeout;
   __set_primask(InterruptMask);
}

// Demand cartridge swaps are authoritative. If their replacement cursor lands
// on a retained prefetch slot, invalidate that optional work before overwrite.
void AGIPictureSwapBufferWillOverwrite(uint8_t Slot)
{
   if (Slot >= Num8kSwapBuffers) return;
   if (AGIPicSourceSlot == (int8_t)Slot) AGIPictureReleaseSource();
   for (uint8_t Index = 0; Index < 3; Index++)
   {
      if (AGIPicPictureSlots[Index] == (int8_t)Slot)
      {
         AGIPictureReleasePicture();
         if (AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] ==
             AGIPicStatus_V3PictureReady)
            AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_Ready;
         break;
      }
   }
   bool SceneSlot = false;
   for (uint8_t Index = 0; Index < AGIPicMaximumSceneSlots; Index++)
      if (AGIPicSceneSlots[Index] == (int8_t)Slot) SceneSlot = true;
   if (SceneSlot)
   {
      AGIPictureReleaseScene();
      if (AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] ==
          AGIPicStatus_V3SceneReady)
         AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_Ready;
   }
   AGIPicSlotOwned[Slot] = false;
}

// IO bank selection runs in the bus ISR while posted decode/prefetch work runs
// in the main poller. Never expose an accelerator-owned page as a normal cache
// hit; the ordinary cartridge swap path will pause the C64 and replace it only
// after the current poller call has safely returned.
bool AGIPictureSwapBufferIsOwned(uint8_t Slot)
{
   return Slot < Num8kSwapBuffers && AGIPicSlotOwned[Slot];
}

static int8_t AGIPictureBorrowSlot()
{
   // The C64 bank-select ISR consults these same cache tags. Claim a candidate
   // atomically with respect to that ISR so it cannot become live ROM in the
   // check-to-claim window. Restoring PRIMASK also makes this safe if a future
   // caller already has interrupts disabled.
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   for (uint8_t Slot = 0; Slot < Num8kSwapBuffers; Slot++)
   {
      uint8_t *Candidate = SwapBuffers[Slot].Image;
      if (AGIPicSlotOwned[Slot] || Candidate == LOROM_Image ||
          Candidate == HIROM_Image) continue;
      AGIPicSlotOwned[Slot] = true;
      SwapBuffers[Slot].Offset = 0;
      __set_primask(InterruptMask);
      return (int8_t)Slot;
   }
   __set_primask(InterruptMask);
   return -1;
}

static bool AGIPictureBorrowDecodedPicture()
{
   AGIPictureReleasePicture();
   for (uint8_t Index = 0; Index < 3; Index++)
   {
      AGIPicPictureSlots[Index] = AGIPictureBorrowSlot();
      if (AGIPicPictureSlots[Index] < 0)
      {
         AGIPictureReleasePicture();
         return false;
      }
   }

   uint8_t *Bitmap = SwapBuffers[(uint8_t)AGIPicPictureSlots[0]].Image;
   uint8_t *PriorityFirst = SwapBuffers[(uint8_t)AGIPicPictureSlots[1]].Image;
   uint8_t *Tail = SwapBuffers[(uint8_t)AGIPicPictureSlots[2]].Image;
   memset(Bitmap, 0, 8192);
   memset(PriorityFirst, 0, 8192);
   memset(Tail, 0, 8192);
   AGIPicPicture.Bitmap = Bitmap;
   AGIPicPicture.PriorityFirst = PriorityFirst;
   AGIPicPicture.PrioritySecond = Tail;
   AGIPicPicture.Screen = Tail + AGIPicPrioritySecondLength;
   AGIPicPicture.Colour = AGIPicPicture.Screen + AGIPicScreenLength;
   return true;
}

static uint32_t AGIPictureRawLimit()
{
   if (AGIPicLayout == AGIPicLayout_MagicDesk2)
      return (uint32_t)NumCrtChips * 0x4000u;
   return 64u * 0x4000u;
}

static bool AGIPictureRawSpanValid(uint32_t Raw, uint32_t Length)
{
   uint32_t Limit = AGIPictureRawLimit();
   return Length && Raw < Limit && Length <= Limit - Raw;
}

static bool AGIPictureOpenCRTFile()
{
   if (myFile) return true;
   char FullFilePath[MaxNamePathLength];
   if (PathIsRoot()) sprintf(FullFilePath, "%s%s", DriveDirPath, DriveDirMenu.Name);
   else sprintf(FullFilePath, "%s/%s", DriveDirPath, DriveDirMenu.Name);
   myFile = SD.open(FullFilePath, FILE_READ);
   return (bool)myFile;
}

static bool AGIPictureLoadTaggedPage(uint32_t Tag, uint8_t *Destination)
{
   if (!AGIPictureOpenCRTFile() || !myFile.seek(Tag & ~SwapSeekAddrMask))
      return false;
   for (uint16_t Index = 0; Index < 8192; Index++)
   {
      if (!(Index & 0x3F) && AGIPicAbortRequested) return false;
      int Value = myFile.read();
      if (Value < 0) return false;
      Destination[Index] = (uint8_t)Value;
   }
   return true;
}

static bool AGIPictureResolveRawPage(uint32_t Raw, uint8_t **Page,
                                     uint8_t *Error)
{
   if (Raw >= AGIPictureRawLimit())
   {
      *Error = AGIPicError_InvalidSource;
      return false;
   }

   uint8_t Bank = Raw >> 14;
   uint8_t Half = (Raw >> 13) & 1;
   uint8_t *Address = NULL;
   if (AGIPicLayout == AGIPicLayout_MagicDesk2)
   {
      if (Bank >= NumCrtChips || CrtChips[Bank].BankNum != Bank ||
          CrtChips[Bank].ROMSize != 0x4000)
      {
         *Error = AGIPicError_InvalidSource;
         return false;
      }
      Address = (uint8_t *)((uintptr_t)CrtChips[Bank].ChipROM +
                            (uintptr_t)Half * 0x2000u);
   }
   else
   {
      if (Bank >= 64 || BankDecode[Bank][Half] == NULL)
      {
         *Error = AGIPicError_InvalidSource;
         return false;
      }
      Address = BankDecode[Bank][Half];
   }

   uint32_t Tagged = (uint32_t)(uintptr_t)Address;
   if ((Tagged & SwapSeekAddrMask) != SwapSeekAddrMask)
   {
      *Page = Address;
      return true;
   }

   for (uint8_t Slot = 0; Slot < Num8kSwapBuffers; Slot++)
   {
      if (SwapBuffers[Slot].Offset == Tagged &&
          !AGIPicSlotOwned[Slot])
      {
         *Page = SwapBuffers[Slot].Image;
         return true;
      }
   }

   if (AGIPicSourceSlot < 0)
   {
      AGIPicSourceSlot = AGIPictureBorrowSlot();
      if (AGIPicSourceSlot < 0)
      {
         *Error = AGIPicError_NoWorkspace;
         return false;
      }
   }
   uint8_t Slot = (uint8_t)AGIPicSourceSlot;
   if (SwapBuffers[Slot].Offset != Tagged)
   {
      if (!AGIPictureLoadTaggedPage(Tagged, SwapBuffers[Slot].Image))
      {
         *Error = AGIPicAbortRequested ? AGIPicError_DMATimeout :
            AGIPicError_SourceIO;
         return false;
      }
      SwapBuffers[Slot].Offset = Tagged;
   }
   *Page = SwapBuffers[Slot].Image;
   return true;
}

static bool AGIPictureFindCachedGBC1Span(uint32_t Raw, uint16_t Length,
                                         const uint8_t **Data)
{
   if (external_psram_size < 2 || !Length ||
       Raw > UINT32_MAX - Length) return false;
   for (uint8_t Index = 0; Index < AGIPicGBC1ViewCacheEntries; Index++)
   {
      stcAGIGBC1ViewCache *Entry = &AGIPicGBC1ViewCache[Index];
      if (!Entry->Cached || Raw < Entry->Raw ||
          Entry->Offset > AGIPicGBC1ViewCacheCapacity ||
          Entry->Length > AGIPicGBC1ViewCacheCapacity - Entry->Offset)
         continue;
      uint32_t Relative = Raw - Entry->Raw;
      if (Relative > Entry->Length || Length > Entry->Length - Relative ||
          Relative > AGIPicGBC1ViewCacheCapacity - Entry->Offset ||
          Length > AGIPicGBC1ViewCacheCapacity - Entry->Offset - Relative)
         continue;
      *Data = AGIPicGBC1ViewCacheMemory + Entry->Offset + Relative;
      return true;
   }
   return false;
}

static bool AGIPictureGBC1CacheAvailable()
{
   // The linker-reserved EXTMEM arena is valid only with enough PSRAM.
   // Internal metadata gates every byte, so the arena needs no zero fill.
   // Two MiB is the exact minimum that contains all twenty VIEW slots.
   return external_psram_size >= 2;
}

static bool AGIPictureReadRaw(uint32_t Raw, uint8_t *Data, uint8_t *Error)
{
   if (AGIPicAbortRequested)
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   // Picture, priority, and scene streams never scan the actor VIEW cache.
   if (AGIPicSceneValid && Raw >= AGIPicSceneRaw)
   {
      uint32_t SceneOffset = Raw - AGIPicSceneRaw;
      if (SceneOffset < AGIPicSceneLength)
      {
         uint8_t SceneSlotIndex = (uint8_t)(SceneOffset >> 13);
         if (SceneSlotIndex >= AGIPicSceneSlotCount ||
             AGIPicSceneSlots[SceneSlotIndex] < 0)
         {
            *Error = AGIPicError_NoWorkspace;
            return false;
         }
         uint8_t Slot = (uint8_t)AGIPicSceneSlots[SceneSlotIndex];
         *Data = SwapBuffers[Slot].Image[SceneOffset & 0x1FFFu];
         return true;
      }
   }
   uint8_t *Page;
   if (!AGIPictureResolveRawPage(Raw, &Page, Error)) return false;
   *Data = Page[Raw & 0x1FFF];
   return true;
}

static bool AGIPictureSourceRead(stcAGISource *Source, uint8_t *Data)
{
   if (Source->Cursor >= Source->End)
   {
      Source->Error = AGIPicError_OutOfBounds;
      return false;
   }
   if (!AGIPictureReadRaw(Source->Cursor, Data, &Source->Error)) return false;
   Source->Cursor++;
   return true;
}

static bool AGIPictureOutputSet(stcAGIOutput *Output, uint16_t Offset,
                                 uint8_t Value)
{
   if (AGIPicAbortRequested) return false;
   if (Offset >= Output->Limit) return false;
   if (Offset < Output->FirstLength) Output->First[Offset] = Value;
   else
   {
      uint16_t Tail = Offset - Output->FirstLength;
      if (Tail >= Output->SecondLength || Output->Second == NULL) return false;
      Output->Second[Tail] = Value;
   }
   return true;
}

static bool AGIPictureOutputGet(stcAGIOutput *Output, uint16_t Offset,
                                uint8_t *Value)
{
   if (Offset >= Output->Position) return false;
   if (Offset < Output->FirstLength) *Value = Output->First[Offset];
   else
   {
      uint16_t Tail = Offset - Output->FirstLength;
      if (Tail >= Output->SecondLength || Output->Second == NULL) return false;
      *Value = Output->Second[Tail];
   }
   return true;
}

static bool AGIPictureOutputPut(stcAGIOutput *Output, uint8_t Value)
{
   if (!AGIPictureOutputSet(Output, Output->Position, Value)) return false;
   Output->Position++;
   return true;
}

static stcAGIOutput AGIPictureSingleOutput(uint8_t *Buffer, uint16_t Length)
{
   stcAGIOutput Output = {Buffer, Length, NULL, 0, 0, Length};
   return Output;
}

static stcAGIOutput AGIPicturePriorityOutput(uint16_t Length)
{
   stcAGIOutput Output =
   {
      AGIPicPicture.PriorityFirst,
      AGIPicPriorityFirstLength,
      AGIPicPicture.PrioritySecond,
      AGIPicPrioritySecondLength,
      0,
      Length
   };
   return Output;
}

static bool AGIPictureDecodeC64RLE(stcAGISource *Source,
                                   stcAGIOutput *Output)
{
   while (Output->Position < Output->Limit)
   {
      uint8_t Control;
      if (!AGIPictureSourceRead(Source, &Control)) return false;
      if (Control & 0x80)
      {
         uint16_t Length = (Control & 0x7F) + 3;
         uint8_t Value;
         if (Length > Output->Limit - Output->Position ||
             !AGIPictureSourceRead(Source, &Value))
         {
            if (Source->Error == AGIPicError_None)
               Source->Error = AGIPicError_MalformedRLE;
            return false;
         }
         while (Length-- && AGIPictureOutputPut(Output, Value)) {}
      }
      else
      {
         uint16_t Length = Control + 1;
         if (Length > Output->Limit - Output->Position)
         {
            Source->Error = AGIPicError_MalformedRLE;
            return false;
         }
         while (Length--)
         {
            uint8_t Value;
            if (!AGIPictureSourceRead(Source, &Value) ||
                !AGIPictureOutputPut(Output, Value)) return false;
         }
      }
   }
   return true;
}

// Adapted for a bounded callback/output surface from Magnus Lind's official
// rawdecrs/exodecrunch.c. The original permissive notice is retained below.
//
// Copyright (c) 2005-2017 Magnus Lind.
// This software is provided 'as-is', without any express or implied warranty.
// Permission is granted to anyone to use, alter, and redistribute it freely,
// provided its origin is not misrepresented, altered versions are marked, and
// this notice is not removed.
struct stcAGIExoContext
{
   stcAGISource *Source;
   stcAGIOutput *Output;
   stcAGIExoTableEntry Table[52];
   uint8_t BitBuffer;
   uint8_t ReuseState;
   uint16_t Offset;
   bool OffsetValid;
};

static bool AGIPictureExoReadBits(stcAGIExoContext *Context,
                                  uint8_t Count, uint16_t *Result)
{
   uint16_t Bits = 0;
   bool CopyByte = (Count & 8) != 0;
   Count &= 7;
   while (Count--)
   {
      bool Carry = (Context->BitBuffer & 0x80) != 0;
      Context->BitBuffer <<= 1;
      if (Context->BitBuffer == 0)
      {
         if (!AGIPictureSourceRead(Context->Source, &Context->BitBuffer))
            return false;
         Carry = (Context->BitBuffer & 0x80) != 0;
         Context->BitBuffer = (Context->BitBuffer << 1) | 1;
      }
      Bits = (Bits << 1) | (Carry ? 1 : 0);
   }
   if (CopyByte)
   {
      uint8_t Value;
      if (!AGIPictureSourceRead(Context->Source, &Value)) return false;
      Bits = (Bits << 8) | Value;
   }
   *Result = Bits;
   return true;
}

static bool AGIPictureExoGenerateTable(stcAGIExoContext *Context,
                                       uint8_t Start, uint8_t Count)
{
   uint32_t Base = 1;
   for (uint8_t Index = 0; Index < Count; Index++)
   {
      uint16_t Low, High;
      if (!AGIPictureExoReadBits(Context, 3, &Low) ||
          !AGIPictureExoReadBits(Context, 1, &High)) return false;
      uint8_t Bits = Low | (High << 3);
      if (Base > 0xFFFFu)
      {
         Context->Source->Error = AGIPicError_MalformedExo;
         return false;
      }
      Context->Table[Start + Index].Bits = Bits;
      Context->Table[Start + Index].Base = (uint16_t)Base;
      Base += 1u << Bits;
   }
   return true;
}

static bool AGIPictureExoLiteral(stcAGIExoContext *Context, uint16_t Length)
{
   if (!Length || Length > Context->Output->Limit - Context->Output->Position)
   {
      Context->Source->Error = AGIPicError_MalformedExo;
      return false;
   }
   while (Length--)
   {
      uint8_t Value;
      if (!AGIPictureSourceRead(Context->Source, &Value) ||
          !AGIPictureOutputPut(Context->Output, Value)) return false;
   }
   Context->ReuseState = (Context->ReuseState << 1) | 1;
   return true;
}

static bool AGIPictureDecodeExomizer(stcAGISource *Source,
                                     stcAGIOutput *Output,
                                     uint16_t ExpectedDestination)
{
   uint8_t DestinationHi, DestinationLo;
   if (!AGIPictureSourceRead(Source, &DestinationHi) ||
       !AGIPictureSourceRead(Source, &DestinationLo) ||
       (((uint16_t)DestinationHi << 8) | DestinationLo) != ExpectedDestination)
   {
      if (Source->Error == AGIPicError_None)
         Source->Error = AGIPicError_MalformedExo;
      return false;
   }

   stcAGIExoContext Context;
   memset(&Context, 0, sizeof(Context));
   Context.Source = Source;
   Context.Output = Output;
   Context.ReuseState = 1;
   if (!AGIPictureSourceRead(Source, &Context.BitBuffer) ||
       !AGIPictureExoGenerateTable(&Context, 0, 16) ||
       !AGIPictureExoGenerateTable(&Context, 16, 16) ||
       !AGIPictureExoGenerateTable(&Context, 32, 16) ||
       !AGIPictureExoGenerateTable(&Context, 48, 4) ||
       !AGIPictureExoLiteral(&Context, 1)) return false;

   for (;;)
   {
      uint16_t Bit;
      if (!AGIPictureExoReadBits(&Context, 1, &Bit)) return false;
      if (Bit)
      {
         if (!AGIPictureExoLiteral(&Context, 1)) return false;
         continue;
      }

      uint8_t LengthIndex = 0;
      do
      {
         if (!AGIPictureExoReadBits(&Context, 1, &Bit)) return false;
         if (!Bit && ++LengthIndex > 17)
         {
            Source->Error = AGIPicError_MalformedExo;
            return false;
         }
      } while (!Bit);

      if (LengthIndex == 16)
      {
         if (Output->Position != Output->Limit)
         {
            Source->Error = AGIPicError_MalformedExo;
            return false;
         }
         return true;
      }
      if (LengthIndex == 17)
      {
         uint8_t Hi, Lo;
         if (!AGIPictureSourceRead(Source, &Hi) ||
             !AGIPictureSourceRead(Source, &Lo) ||
             !AGIPictureExoLiteral(&Context, ((uint16_t)Hi << 8) | Lo))
            return false;
         continue;
      }

      stcAGIExoTableEntry *LengthEntry = &Context.Table[LengthIndex];
      uint16_t Extra;
      if (!AGIPictureExoReadBits(&Context, LengthEntry->Bits, &Extra))
         return false;
      uint32_t SequenceLength = (uint32_t)LengthEntry->Base + Extra;
      if (!SequenceLength || SequenceLength > (uint32_t)
          (Context.Output->Limit - Context.Output->Position))
      {
         Source->Error = AGIPicError_MalformedExo;
         return false;
      }

      bool ReadOffset = (Context.ReuseState & 3) != 1;
      if (!ReadOffset)
      {
         if (!AGIPictureExoReadBits(&Context, 1, &Bit)) return false;
         ReadOffset = Bit == 0;
      }
      if (ReadOffset)
      {
         uint16_t OffsetCode;
         uint8_t TableIndex;
         if (SequenceLength == 1)
         {
            if (!AGIPictureExoReadBits(&Context, 2, &OffsetCode)) return false;
            TableIndex = 48 + OffsetCode;
         }
         else if (SequenceLength == 2)
         {
            if (!AGIPictureExoReadBits(&Context, 4, &OffsetCode)) return false;
            TableIndex = 32 + OffsetCode;
         }
         else
         {
            if (!AGIPictureExoReadBits(&Context, 4, &OffsetCode)) return false;
            TableIndex = 16 + OffsetCode;
         }
         stcAGIExoTableEntry *OffsetEntry = &Context.Table[TableIndex];
         if (!AGIPictureExoReadBits(&Context, OffsetEntry->Bits, &Extra))
            return false;
         uint32_t Offset = (uint32_t)OffsetEntry->Base + Extra;
         if (!Offset || Offset > Context.Output->Position)
         {
            Source->Error = AGIPicError_MalformedExo;
            return false;
         }
         Context.Offset = (uint16_t)Offset;
         Context.OffsetValid = true;
      }
      if (!Context.OffsetValid || Context.Offset > Context.Output->Position)
      {
         Source->Error = AGIPicError_MalformedExo;
         return false;
      }

      while (SequenceLength--)
      {
         uint8_t Value;
         uint16_t From = Context.Output->Position - Context.Offset;
         if (!AGIPictureOutputGet(Context.Output, From, &Value) ||
             !AGIPictureOutputPut(Context.Output, Value))
         {
            Source->Error = AGIPicError_MalformedExo;
            return false;
         }
      }
      Context.ReuseState <<= 1;
   }
}

static bool AGIPictureDecodeCompactPriority(stcAGISource *Source,
                                            stcAGIOutput *Output,
                                            uint16_t *RuntimeLength)
{
   uint8_t Header[5];
   for (uint8_t Index = 0; Index < sizeof(Header); Index++)
      if (!AGIPictureSourceRead(Source, &Header[Index])) return false;
   uint16_t Length = Header[3] | ((uint16_t)Header[4] << 8);
   if (Header[0] != 0xA3 || Header[1] != 160 || Header[2] != 168 ||
       Length < 336 + 168 * 4 || Length > AGIPicCompactPriorityMaximum)
   {
      Source->Error = AGIPicError_MalformedPriority;
      return false;
   }
   Output->Limit = Length;
   memset(Output->First, 0, Output->FirstLength);
   memset(Output->Second, 0, Output->SecondLength);
   uint16_t Destination = 336;
   uint8_t Pixels[160];

   for (uint16_t Row = 0; Row < 168; Row++)
   {
      uint32_t RowStart = Source->Cursor;
      uint8_t CapacityByte, Descriptor;
      if (!AGIPictureSourceRead(Source, &CapacityByte) ||
          !AGIPictureSourceRead(Source, &Descriptor)) return false;
      uint16_t Capacity = CapacityByte ? CapacityByte : 160;
      uint8_t Modal = Descriptor >> 4;
      uint16_t Exceptions = Descriptor & 15;
      if (Exceptions == 15)
      {
         uint8_t Extended;
         if (!AGIPictureSourceRead(Source, &Extended)) return false;
         if (Extended < 15)
         {
            Source->Error = AGIPicError_MalformedPriority;
            return false;
         }
         Exceptions = Extended;
      }
      memset(Pixels, Modal, sizeof(Pixels));
      uint16_t PriorEnd = 0;
      for (uint16_t Exception = 0; Exception < Exceptions; Exception++)
      {
         uint8_t Start, Packed;
         if (!AGIPictureSourceRead(Source, &Start) ||
             !AGIPictureSourceRead(Source, &Packed)) return false;
         uint8_t Value = Packed >> 4;
         uint16_t Span = (Packed & 15) + 1;
         if ((Packed & 15) == 15)
         {
            uint8_t Extended;
            if (!AGIPictureSourceRead(Source, &Extended)) return false;
            Span = (uint16_t)Extended + 1;
            if (Span <= 15)
            {
               Source->Error = AGIPicError_MalformedPriority;
               return false;
            }
         }
         if (Start < PriorEnd || Start + Span > 160 || Value == Modal)
         {
            Source->Error = AGIPicError_MalformedPriority;
            return false;
         }
         memset(Pixels + Start, Value, Span);
         PriorEnd = Start + Span;
      }
      if (Source->Cursor - RowStart > 64)
      {
         Source->Error = AGIPicError_MalformedPriority;
         return false;
      }

      uint8_t RunEnds[160];
      uint8_t RunValues[160];
      uint16_t Runs = 0;
      for (uint16_t Start = 0; Start < 160;)
      {
         uint16_t End = Start + 1;
         while (End < 160 && Pixels[End] == Pixels[Start]) End++;
         RunEnds[Runs] = (uint8_t)End;
         RunValues[Runs] = Pixels[Start];
         Runs++;
         Start = End;
      }
      if (!Runs || Runs > Capacity || Destination + 2 + Capacity * 2 > Length)
      {
         Source->Error = AGIPicError_MalformedPriority;
         return false;
      }
      uint16_t Address = 0x8000 + Destination;
      if (!AGIPictureOutputSet(Output, Row, Address) ||
          !AGIPictureOutputSet(Output, 168 + Row, Address >> 8) ||
          !AGIPictureOutputSet(Output, Destination, Runs) ||
          !AGIPictureOutputSet(Output, Destination + 1, CapacityByte))
      {
         Source->Error = AGIPicError_MalformedPriority;
         return false;
      }
      uint16_t Cursor = Destination + 2;
      for (uint16_t Run = 0; Run < Runs; Run++)
      {
         if (!AGIPictureOutputSet(Output, Cursor++, RunEnds[Run]) ||
             !AGIPictureOutputSet(Output, Cursor++, RunValues[Run]))
         {
            Source->Error = AGIPicError_MalformedPriority;
            return false;
         }
      }
      Destination += 2 + Capacity * 2;
   }
   if (Destination != Length)
   {
      Source->Error = AGIPicError_MalformedPriority;
      return false;
   }
   Output->Position = Length;
   *RuntimeLength = Length;
   return true;
}

static bool AGIPictureReadDescriptor(uint32_t Raw, uint8_t ExpectedToken,
                                     uint8_t Descriptor[16], uint8_t *Error)
{
   if (!AGIPictureRawSpanValid(Raw, AGIPicDescriptorBytes))
   {
      *Error = AGIPicError_BadDescriptor;
      return false;
   }
   uint8_t Checksum = 0;
   for (uint8_t Index = 0; Index < AGIPicDescriptorBytes; Index++)
   {
      if (!AGIPictureReadRaw(Raw + Index, &Descriptor[Index], Error)) return false;
      if (Index < 15) Checksum ^= Descriptor[Index];
   }
   if (memcmp(Descriptor, "AGP3", 4) != 0 || Descriptor[4] != 16 ||
       Descriptor[5] > 2 || (Descriptor[6] & ~3) ||
       Descriptor[7] != ExpectedToken || Descriptor[15] != Checksum ||
       ((Descriptor[6] & 1) != 0) !=
          (AGIPicLayout == AGIPicLayout_MagicDesk2))
   {
      *Error = AGIPicError_BadDescriptor;
      return false;
   }
   return true;
}

static bool AGIPictureDecodeV3(uint32_t DescriptorRaw, uint8_t Token,
                               uint8_t *Error)
{
   uint8_t Descriptor[16];
   if (!AGIPictureReadDescriptor(DescriptorRaw, Token, Descriptor, Error))
      return false;
   uint16_t VisualLength = Descriptor[8] | ((uint16_t)Descriptor[9] << 8);
   uint32_t PriorityRaw = (uint32_t)Descriptor[10] |
      ((uint32_t)Descriptor[11] << 8) | ((uint32_t)Descriptor[12] << 16);
   uint16_t PriorityWord = Descriptor[13] | ((uint16_t)Descriptor[14] << 8);
   bool HasPriority = (Descriptor[6] & 2) != 0;
   if (!VisualLength ||
       !AGIPictureRawSpanValid(DescriptorRaw + 16, VisualLength))
   {
      *Error = AGIPicError_OutOfBounds;
      return false;
   }
   if (!AGIPictureBorrowDecodedPicture())
   {
      *Error = AGIPicError_NoWorkspace;
      return false;
   }

   stcAGISource Visual =
   {
      DescriptorRaw + 16,
      DescriptorRaw + 16 + VisualLength,
      AGIPicError_None
   };
   stcAGIOutput Bitmap = AGIPictureSingleOutput(AGIPicPicture.Bitmap,
                                                AGIPicBitmapLength);
   stcAGIOutput Screen = AGIPictureSingleOutput(AGIPicPicture.Screen,
                                                AGIPicScreenLength);
   stcAGIOutput Colour = AGIPictureSingleOutput(AGIPicPicture.Colour,
                                                AGIPicColourLength);
   bool Decoded = false;
   if (Descriptor[5] == 0)
   {
      Decoded = AGIPictureDecodeC64RLE(&Visual, &Bitmap) &&
         AGIPictureDecodeC64RLE(&Visual, &Screen) &&
         AGIPictureDecodeC64RLE(&Visual, &Colour);
   }
   else if (Descriptor[5] == 1)
   {
      Decoded = AGIPictureDecodeExomizer(&Visual, &Bitmap, 0x6000) &&
         AGIPictureDecodeC64RLE(&Visual, &Screen) &&
         AGIPictureDecodeC64RLE(&Visual, &Colour);
   }
   else if (Descriptor[5] == 2)
   {
      Decoded = AGIPictureDecodeExomizer(&Visual, &Bitmap, 0x6000) &&
         AGIPictureDecodeExomizer(&Visual, &Screen, 0x5C00) &&
         AGIPictureDecodeExomizer(&Visual, &Colour, 0xD800);
   }
   if (!Decoded || Visual.Cursor != Visual.End)
   {
      *Error = Visual.Error ? Visual.Error : AGIPicError_BadDescriptor;
      AGIPictureReleasePicture();
      AGIPictureReleaseSource();
      return false;
   }

   AGIPicPicture.HasPriority = HasPriority;
   AGIPicPicture.PriorityLength = 0;
   AGIPicPicture.PriorityFormat = 0;
   if (HasPriority)
   {
      uint16_t PriorityEncodedLength = PriorityWord & 0x3FFF;
      if (!PriorityEncodedLength ||
          !AGIPictureRawSpanValid(PriorityRaw, PriorityEncodedLength) ||
          (PriorityWord & 0xC000) == 0xC000)
      {
         *Error = AGIPicError_MalformedPriority;
         AGIPictureReleasePicture();
         AGIPictureReleaseSource();
         return false;
      }
      stcAGISource Priority =
      {
         PriorityRaw,
         PriorityRaw + PriorityEncodedLength,
         AGIPicError_None
      };
      uint16_t OutputLength = AGIPicPriorityLength;
      stcAGIOutput PriorityOutput = AGIPicturePriorityOutput(OutputLength);
      if (PriorityWord & 0x8000)
      {
         if (!AGIPictureDecodeCompactPriority(&Priority, &PriorityOutput,
                                              &OutputLength)) Decoded = false;
      }
      else if (PriorityWord & 0x4000)
      {
         Decoded = AGIPictureDecodeExomizer(&Priority, &PriorityOutput, 0x8000);
      }
      else
      {
         Decoded = AGIPictureDecodeC64RLE(&Priority, &PriorityOutput);
      }
      if (!Decoded || Priority.Cursor != Priority.End)
      {
         *Error = Priority.Error ? Priority.Error : AGIPicError_MalformedPriority;
         AGIPictureReleasePicture();
         AGIPictureReleaseSource();
         return false;
      }
      AGIPicPicture.PriorityLength = OutputLength;
      AGIPicPicture.PriorityFormat = (PriorityWord & 0x8000) ? 3 :
         ((PriorityWord & 0x4000) ? 4 : 2);
   }
   AGIPicPicture.DescriptorRaw = DescriptorRaw;
   AGIPicPicture.Token = Token;
   AGIPictureReleaseSource();
   return true;
}

static void AGIPictureApplyVideoTiming()
{
   bool NTSC = (AGIPicRegisters[AGIPicReg_MachineFlags - AGIPicReg_ID0] & 1) != 0;
   nS_DMASetup = NTSC ? Def_nS_DMASetupNTSC : Def_nS_DMASetupPAL;
   nS_MaxAdj = NTSC ? Def_nS_MaxAdjNTSC : Def_nS_MaxAdjPAL;
}

static bool AGIPictureDMAWriteSegment(bool *Started, uint16_t Address,
                                       uint8_t *Data, uint16_t Length)
{
   if (AGIPicAbortRequested) return false;
   if (!Length) return true;
   if (!*Started)
   {
      if (!PerformDMA(false, Address, Data, Length, false)) return false;
      *Started = true;
      return true;
   }
   return AGIContinueDMA(false, Address, Data, Length, false);
}

static bool AGIPictureCloseScatter(bool Started)
{
   return Started && CloseDMA();
}

static bool AGIPictureDMAWritePatchSegment(uint16_t Address,
                                            uint8_t *Data, uint16_t Length)
{
   if (AGIPicAbortRequested) return false;
   if (!Length) return true;
   return PerformDMA(false, Address, Data, Length, false) && CloseDMA();
}

static bool AGIPictureDMADecodedPicture()
{
   if (DMA_State != DMA_S_DisableReady) return false;
   AGIPictureApplyVideoTiming();
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_DMA;
   bool Started = false;
   bool Okay = AGIPictureDMAWriteSegment(&Started, 0x6000,
                                          AGIPicPicture.Bitmap,
                                          AGIPicBitmapLength) &&
      AGIPictureDMAWriteSegment(&Started, 0x5C00,
                                AGIPicPicture.Screen,
                                AGIPicScreenLength);
   if (Okay && AGIPicPicture.HasPriority)
   {
      uint16_t First = AGIPicPicture.PriorityLength;
      if (First > AGIPicPriorityFirstLength) First = AGIPicPriorityFirstLength;
      uint16_t Second = AGIPicPicture.PriorityLength - First;
      Okay = AGIPictureDMAWriteSegment(&Started, 0x8000,
                                        AGIPicPicture.PriorityFirst, First) &&
         AGIPictureDMAWriteSegment(&Started, 0x8000 + First,
                                   AGIPicPicture.PrioritySecond, Second);
   }
   if (Okay)
      Okay = AGIPictureDMAWriteSegment(&Started, 0xD800,
                                       AGIPicPicture.Colour,
                                       AGIPicColourLength);
   bool Closed = AGIPictureCloseScatter(Started);
   return Okay && Closed;
}

static void AGIPictureCommitLivePicture()
{
   // Any complete picture publication replaces the visual-script base.  A
   // later $22 seed deliberately recaptures the live C64 planes after the
   // interpreter has applied permanent room art.
   AGIPictureInvalidateRoomArt();
   AGIPicLivePictureValid = true;
   AGIPicLivePictureToken = AGIPicPicture.Token;
}

static void AGIPictureDMAProbe(uint32_t StartMS)
{
   if (DMA_State != DMA_S_DisableReady)
   {
      AGIPictureSetError(AGIPicError_DMAUnavailable);
      return;
   }
   AGIPictureApplyVideoTiming();
   if (!PerformDMA(true, AGIPicDMAProbeAddress, AGIPicDMAProbeBuffer,
                   AGIPicDMAProbeLength, false) || !CloseDMA())
   {
      AGIPictureSetError(AGIPicError_DMATimeout);
      return;
   }
   if (memcmp(AGIPicDMAProbeBuffer, AGIPicDMAProbeSeed,
              AGIPicDMAProbeLength) != 0)
   {
      AGIPictureSetError(AGIPicError_DMAProbeMismatch);
      return;
   }
   for (uint8_t Index = 0; Index < AGIPicDMAProbeLength; Index++)
      AGIPicDMAProbeBuffer[Index] ^= 0xFF;
   if (!PerformDMA(false, AGIPicDMAProbeAddress, AGIPicDMAProbeBuffer,
                   AGIPicDMAProbeLength, false) || !CloseDMA())
   {
      AGIPictureSetError(AGIPicError_DMATimeout);
      return;
   }
   AGIPictureSetDone(AGIPicStatus_V2DoneDMAProbe, StartMS);
}

static bool AGIPictureDecodeV2(uint32_t RawOffset, uint8_t *Error)
{
   if (RawOffset >= AGIPicV2PhysicalPrefixSize ||
       !AGIPictureBorrowDecodedPicture())
   {
      *Error = RawOffset >= AGIPicV2PhysicalPrefixSize ?
         AGIPicError_InvalidSource : AGIPicError_NoWorkspace;
      return false;
   }
   stcAGISource Source =
   {
      RawOffset,
      AGIPicV2PhysicalPrefixSize,
      AGIPicError_None
   };
   stcAGIOutput Bitmap = AGIPictureSingleOutput(AGIPicPicture.Bitmap,
                                                AGIPicBitmapLength);
   stcAGIOutput Screen = AGIPictureSingleOutput(AGIPicPicture.Screen,
                                                AGIPicScreenLength);
   stcAGIOutput Colour = AGIPictureSingleOutput(AGIPicPicture.Colour,
                                                AGIPicColourLength);
   if (!AGIPictureDecodeC64RLE(&Source, &Bitmap) ||
       !AGIPictureDecodeC64RLE(&Source, &Screen) ||
       !AGIPictureDecodeC64RLE(&Source, &Colour))
   {
      *Error = Source.Error ? Source.Error : AGIPicError_MalformedRLE;
      AGIPictureReleasePicture();
      AGIPictureReleaseSource();
      return false;
   }
   AGIPicPicture.HasPriority = false;
   AGIPictureReleaseSource();
   return true;
}

static uint32_t AGIPictureMailboxRaw()
{
   return (uint32_t)AGIPicRegisters[AGIPicReg_Source0 - AGIPicReg_ID0] |
      ((uint32_t)AGIPicRegisters[AGIPicReg_Source1 - AGIPicReg_ID0] << 8) |
      ((uint32_t)AGIPicRegisters[AGIPicReg_Source2 - AGIPicReg_ID0] << 16);
}

static bool AGIPictureReadGAC3Header(uint32_t Root, uint8_t Header[18],
                                     uint8_t *Error)
{
   if (!AGIPictureRawSpanValid(Root, 18))
   {
      *Error = AGIPicError_MalformedGAC3;
      return false;
   }
   for (uint8_t Index = 0; Index < 18; Index++)
      if (!AGIPictureReadRaw(Root + Index, &Header[Index], Error)) return false;
   uint8_t Count = Header[5];
   uint16_t Directory = Header[8] | ((uint16_t)Header[9] << 8);
   uint16_t DataOffset = Header[10] | ((uint16_t)Header[11] << 8);
   uint32_t Total = (uint32_t)Header[12] |
      ((uint32_t)Header[13] << 8) | ((uint32_t)Header[14] << 16);
   if (memcmp(Header, "GAC3", 4) != 0 || Header[4] != 3 || !Count ||
       Header[6] != 26 || (Header[7] & 0x0F) != 0x0F ||
       (Header[7] & ~0x1F) || Directory != 18 ||
       DataOffset != 18 + Count * 7 || Total <= DataOffset ||
       Header[15] != 8 || Header[16] != 10 || Header[17] != 7 ||
       !AGIPictureRawSpanValid(Root, Total))
   {
      *Error = AGIPicError_MalformedGAC3;
      return false;
   }
   return true;
}

static bool AGIPicturePrefetchScene(uint32_t Root, uint8_t Picture,
                                    uint8_t *Error)
{
   AGIPictureReleaseScene();
   uint8_t Header[18];
   if (!AGIPictureReadGAC3Header(Root, Header, Error)) return false;
   uint8_t Count = Header[5];
   uint16_t DataOffset = Header[10] | ((uint16_t)Header[11] << 8);
   uint32_t Total = (uint32_t)Header[12] |
      ((uint32_t)Header[13] << 8) | ((uint32_t)Header[14] << 16);
   uint32_t TargetStart = 0;
   uint32_t TargetEnd = Total;
   uint32_t PriorOffset = DataOffset - 1;
   int16_t PriorPicture = -1;
   bool Found = false;
   for (uint8_t Index = 0; Index < Count; Index++)
   {
      uint8_t Entry[7];
      for (uint8_t Byte = 0; Byte < 7; Byte++)
         if (!AGIPictureReadRaw(Root + 18 + (uint32_t)Index * 7 + Byte,
                                &Entry[Byte], Error)) return false;
      uint32_t Offset = (uint32_t)Entry[4] |
         ((uint32_t)Entry[5] << 8) | ((uint32_t)Entry[6] << 16);
      if (Entry[0] <= PriorPicture || (Entry[1] & 1) || (Entry[3] & 0x70) ||
          Offset <= PriorOffset || Offset >= Total)
      {
         *Error = AGIPicError_MalformedGAC3;
         return false;
      }
      if (Found && TargetEnd == Total) TargetEnd = Offset;
      if (Entry[0] == Picture)
      {
         TargetStart = Offset;
         Found = true;
      }
      PriorPicture = Entry[0];
      PriorOffset = Offset;
   }
   if (!Found) return false;
   uint32_t SceneLength = TargetEnd - TargetStart;
   if (!SceneLength || SceneLength > AGIPicGAC3MaximumScene)
   {
      *Error = AGIPicError_MalformedGAC3;
      return false;
   }
   uint8_t SceneHead[2];
   if (!AGIPictureReadRaw(Root + TargetStart, &SceneHead[0], Error) ||
       !AGIPictureReadRaw(Root + TargetStart + 1, &SceneHead[1], Error))
      return false;
   uint16_t Buckets = SceneHead[1] <= 8 ? 256u >> SceneHead[1] : 0;
   if (!SceneHead[0] || !Buckets || Buckets < SceneHead[0] ||
       2u + Buckets + (uint32_t)SceneHead[0] * 26u > SceneLength)
   {
      *Error = AGIPicError_MalformedGAC3;
      return false;
   }

   uint8_t RequiredSlots = (uint8_t)((SceneLength + 8191u) >> 13);
   if (!RequiredSlots || RequiredSlots > AGIPicMaximumSceneSlots)
   {
      *Error = AGIPicError_MalformedGAC3;
      return false;
   }
   for (uint8_t Index = 0; Index < RequiredSlots; Index++)
   {
      AGIPicSceneSlots[Index] = AGIPictureBorrowSlot();
      if (AGIPicSceneSlots[Index] < 0)
      {
         AGIPictureReleaseScene();
         AGIPictureReleaseSource();
         *Error = AGIPicError_NoWorkspace;
         return false;
      }
      AGIPicSceneSlotCount = Index + 1;
   }
   for (uint32_t Index = 0; Index < SceneLength; Index++)
   {
      uint8_t SceneSlotIndex = (uint8_t)(Index >> 13);
      uint8_t Slot = (uint8_t)AGIPicSceneSlots[SceneSlotIndex];
      uint8_t *Destination = SwapBuffers[Slot].Image;
      if (!AGIPictureReadRaw(Root + TargetStart + Index,
                              &Destination[Index & 0x1FFFu], Error))
      {
         AGIPictureReleaseScene();
         AGIPictureReleaseSource();
         return false;
      }
   }
   AGIPictureReleaseSource();
   AGIPicSceneRaw = Root + TargetStart;
   AGIPicSceneLength = SceneLength;
   AGIPicScenePicture = Picture;
   AGIPicSceneValid = true;
   return true;
}

static bool AGIPictureValidatePatch(uint32_t Raw, uint16_t Length,
                                    uint16_t *ChangedCells, uint8_t *Error)
{
   if (!Length || Length > AGIPicPatchMaximumBytes ||
       !AGIPictureRawSpanValid(Raw, Length))
   {
      *Error = AGIPicError_MalformedPatch;
      return false;
   }
   uint32_t Cursor = Raw;
   uint32_t End = Raw + Length;
   uint16_t Cells = 0;
   while (Cursor < End)
   {
      uint8_t Lo, Hi, Count;
      if (End - Cursor < 3 ||
          !AGIPictureReadRaw(Cursor++, &Lo, Error) ||
          !AGIPictureReadRaw(Cursor++, &Hi, Error) ||
          !AGIPictureReadRaw(Cursor++, &Count, Error)) return false;
      uint16_t Cell = Lo | ((uint16_t)Hi << 8);
      uint32_t Payload = (uint32_t)Count * 10u;
      if (!Count || Count > 40 || Cell >= 1000 || Cell + Count > 1000 ||
          Cell / 40 != (Cell + Count - 1) / 40 || Payload > End - Cursor ||
          Cells + Count > 1000)
      {
         *Error = AGIPicError_MalformedPatch;
         return false;
      }
      Cursor += Payload;
      Cells += Count;
   }
   if (Cursor != End || !Cells)
   {
      *Error = AGIPicError_MalformedPatch;
      return false;
   }
   *ChangedCells = Cells;
   return true;
}

static bool AGIPictureMaterializePatch(uint32_t Raw, uint16_t Length,
                                       uint8_t *Error)
{
   for (uint16_t Index = 0; Index < Length; Index++)
   {
      if (!AGIPictureReadRaw(Raw + Index, &AGIPicPatchEncoded[Index], Error))
      {
         AGIPictureReleaseSource();
         return false;
      }
   }
   // The complete stream is now local. Releasing a tagged source page before
   // asserting /DMA guarantees the scatter loop cannot touch SD or cartridge
   // cache ownership while the C64 is stopped.
   AGIPictureReleaseSource();
   return true;
}

static bool AGIPictureDMAPatch(uint32_t Raw, uint16_t Length,
                               uint8_t Picture, uint8_t *Error)
{
   if (!AGIPicSceneValid || AGIPicScenePicture != Picture ||
       Raw < AGIPicSceneRaw || Length > AGIPicSceneLength ||
       Raw - AGIPicSceneRaw > AGIPicSceneLength - Length)
   {
      *Error = AGIPicError_PrefetchMiss;
      return false;
   }
   uint16_t ChangedCells;
   if (!AGIPictureValidatePatch(Raw, Length, &ChangedCells, Error)) return false;
   if (!AGIPictureMaterializePatch(Raw, Length, Error)) return false;
   if (DMA_State != DMA_S_DisableReady)
   {
      *Error = AGIPicError_DMAUnavailable;
      return false;
   }
   AGIPictureApplyVideoTiming();
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_DMA;
   uint16_t Cursor = 0;
   bool Okay = true;
   while (Okay && Cursor < Length)
   {
      uint8_t Lo = AGIPicPatchEncoded[Cursor++];
      uint8_t Hi = AGIPicPatchEncoded[Cursor++];
      uint8_t Count = AGIPicPatchEncoded[Cursor++];
      uint16_t Cell = Lo | ((uint16_t)Hi << 8);
      for (uint8_t Index = 0; Index < Count; Index++)
      {
         for (uint8_t Byte = 0; Byte < 8; Byte++)
            AGIPicPatchBitmap[Index * 8 + Byte] = AGIPicPatchEncoded[Cursor++];
         AGIPicPatchScreen[Index] = AGIPicPatchEncoded[Cursor++];
         AGIPicPatchColour[Index] = AGIPicPatchEncoded[Cursor++];
      }
      if (AGIPicRoomValid && AGIPicRoomToken == Picture)
      {
         for (uint8_t Index = 0; Index < Count; Index++)
            AGIPictureDirtySet(AGIPicRoomPendingDirty, Cell + Index);
      }
      if (AGIPicAbortRequested)
      {
         *Error = AGIPicError_DMATimeout;
         Okay = false;
         break;
      }
      Okay = AGIPictureDMAWritePatchSegment(0x6000 + Cell * 8,
                                              AGIPicPatchBitmap, Count * 8) &&
         AGIPictureDMAWritePatchSegment(0x5C00 + Cell,
                                        AGIPicPatchScreen, Count) &&
         AGIPictureDMAWritePatchSegment(0xD800 + Cell,
                                        AGIPicPatchColour, Count);
   }
   if (!Okay)
   {
      if (*Error == AGIPicError_None) *Error = AGIPicError_DMATimeout;
      AGIPictureInvalidateRoomArt();
      return false;
   }
   return true;
}

static void AGIPictureDirtyClear(uint8_t Dirty[125])
{
   memset(Dirty, 0, 125);
}

static bool AGIPictureDirtyGet(const uint8_t Dirty[125], uint16_t Cell)
{
   return Cell < 1000 && (Dirty[Cell >> 3] & (1u << (Cell & 7))) != 0;
}

static void AGIPictureDirtySet(uint8_t Dirty[125], uint16_t Cell)
{
   if (Cell < 1000) Dirty[Cell >> 3] |= 1u << (Cell & 7);
}

static uint16_t AGIPictureDirtyCount(const uint8_t Dirty[125])
{
   uint16_t Count = 0;
   for (uint16_t Cell = 0; Cell < 1000; Cell++)
      if (AGIPictureDirtyGet(Dirty, Cell)) Count++;
   return Count;
}

static bool AGIPictureDMAReadRoomSegment(uint16_t Address, uint8_t *Data,
                                          uint16_t Length)
{
   if (AGIPicAbortRequested || !Length) return !AGIPicAbortRequested;
   return PerformDMA(true, Address, Data, Length, false) && CloseDMA();
}

FLASHMEM static bool AGIPictureValidateRoomPriority(uint8_t Format, uint16_t Length,
                                            uint8_t *Error)
{
   if (!Length && !Format) return true;
   if (Length > AGIPicPriorityLength ||
       (Format == 2 && Length != AGIPicPriorityLength) ||
       (Format != 2 && Format != 3))
   {
      *Error = AGIPicError_MalformedPriority;
      return false;
   }
   if (Format != 3) return true;
   if (Length < 336u + 168u * 4u)
   {
      *Error = AGIPicError_MalformedPriority;
      return false;
   }
   for (uint16_t Row = 0; Row < 168; Row++)
   {
      uint16_t Address = AGIPicRoomPriority[Row] |
         ((uint16_t)AGIPicRoomPriority[168 + Row] << 8);
      if (Address < 0x8000)
      {
         *Error = AGIPicError_MalformedPriority;
         return false;
      }
      uint16_t Offset = Address - 0x8000;
      if (Offset > Length - 2)
      {
         *Error = AGIPicError_MalformedPriority;
         return false;
      }
      uint8_t Runs = AGIPicRoomPriority[Offset];
      uint16_t Capacity = AGIPicRoomPriority[Offset + 1];
      if (!Capacity) Capacity = 160;
      if (!Runs || Runs > Capacity || Capacity > 160 ||
          (uint32_t)Offset + 2u + Capacity * 2u > Length)
      {
         *Error = AGIPicError_MalformedPriority;
         return false;
      }
      uint16_t PriorEnd = 0;
      for (uint8_t Run = 0; Run < Runs; Run++)
      {
         uint8_t End = AGIPicRoomPriority[Offset + 2 + Run * 2];
         uint8_t Value = AGIPicRoomPriority[Offset + 3 + Run * 2];
         if (End <= PriorEnd || End > 160 || Value > 15)
         {
            *Error = AGIPicError_MalformedPriority;
            return false;
         }
         PriorEnd = End;
      }
      if (PriorEnd != 160)
      {
         *Error = AGIPicError_MalformedPriority;
         return false;
      }
   }
   return true;
}

FLASHMEM static bool AGIPictureRoomPriorityAt(uint8_t X, uint8_t Y, uint8_t *Value)
{
   if (X >= 160 || Y >= 168) return false;
   if (!AGIPicRoomPriorityLength)
   {
      *Value = 0;
      return true;
   }
   if (AGIPicRoomPriorityFormat == 3)
   {
      uint16_t Address = AGIPicRoomPriority[Y] |
         ((uint16_t)AGIPicRoomPriority[168 + Y] << 8);
      if (Address < 0x8000) return false;
      uint16_t Offset = Address - 0x8000;
      if (Offset > AGIPicRoomPriorityLength - 2) return false;
      uint8_t Runs = AGIPicRoomPriority[Offset];
      for (uint8_t Run = 0; Run < Runs; Run++)
      {
         uint16_t Pair = Offset + 2u + Run * 2u;
         if (Pair + 1 >= AGIPicRoomPriorityLength) return false;
         if (X < AGIPicRoomPriority[Pair])
         {
            *Value = AGIPicRoomPriority[Pair + 1];
            return true;
         }
      }
      return false;
   }
   uint16_t Offset = (uint16_t)Y * 80u + (X >> 1);
   if (Offset >= AGIPicRoomPriorityLength) return false;
   uint8_t Packed = AGIPicRoomPriority[Offset];
   *Value = (X & 1) ? (Packed & 15) : (Packed >> 4);
   return true;
}

FLASHMEM static bool AGIPictureRoomPriorityAllows(uint8_t X, uint8_t Y,
                                          uint8_t ActorPriority)
{
   if (!AGIPicRoomPriorityLength || ActorPriority == 15) return true;
   for (uint16_t ScanY = Y; ScanY < 168; ScanY++)
   {
      uint8_t Background;
      if (!AGIPictureRoomPriorityAt(X, (uint8_t)ScanY, &Background))
         return false;
      if (Background >= 3) return Background <= ActorPriority;
   }
   return true;
}

FLASHMEM static bool AGIPictureSeedRoom(uint32_t, uint8_t Picture,
                               uint8_t PriorityFormat,
                               uint16_t PriorityLength, uint8_t *Error)
{
   AGIPictureInvalidateRoomArt();
   if (!AGIPicLivePictureValid || AGIPicLivePictureToken != Picture)
   {
      *Error = AGIPicError_PrefetchMiss;
      return false;
   }
   if ((!PriorityLength && PriorityFormat) ||
       (PriorityLength && PriorityFormat != 2 && PriorityFormat != 3) ||
       PriorityLength > AGIPicPriorityLength ||
       (PriorityFormat == 2 && PriorityLength != AGIPicPriorityLength))
   {
      *Error = AGIPicError_MalformedPriority;
      return false;
   }
   if (DMA_State != DMA_S_DisableReady)
   {
      *Error = AGIPicError_DMAUnavailable;
      return false;
   }
   AGIPictureApplyVideoTiming();
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_DMA;
   bool Okay = AGIPictureDMAReadRoomSegment(0x6000, AGIPicRoomBitmap,
                                             AGIPicBitmapLength) &&
      AGIPictureDMAReadRoomSegment(0x5C00, AGIPicRoomScreen,
                                   AGIPicScreenLength) &&
      AGIPictureDMAReadRoomSegment(0xD800, AGIPicRoomColour,
                                   AGIPicColourLength);
   if (Okay && PriorityLength)
      Okay = AGIPictureDMAReadRoomSegment(0x8000, AGIPicRoomPriority,
                                          PriorityLength);
   if (!Okay)
   {
      *Error = AGIPicError_DMATimeout;
      AGIPictureInvalidateRoomArt();
      return false;
   }
   for (uint16_t Cell = 0; Cell < AGIPicColourLength; Cell++)
      AGIPicRoomColour[Cell] &= 15;
   if (!AGIPictureValidateRoomPriority(PriorityFormat, PriorityLength, Error))
   {
      AGIPictureInvalidateRoomArt();
      return false;
   }
   AGIPicRoomToken = Picture;
   AGIPicRoomPriorityFormat = PriorityFormat;
   AGIPicRoomPriorityLength = PriorityLength;
   AGIPicRoomValid = true;
   AGIPicActorStateValid = false;
   return true;
}

static bool AGIPictureReadRawBytes(uint32_t Raw, uint8_t *Data,
                                    uint16_t Length, uint8_t *Error)
{
   if (AGIPicAbortRequested)
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   if (!AGIPictureRawSpanValid(Raw, Length))
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   const uint8_t *Cached;
   if (Length && AGIPictureFindCachedGBC1Span(Raw, Length, &Cached))
   {
      memcpy(Data, Cached, Length);
      return true;
   }
   for (uint16_t Index = 0; Index < Length; Index++)
      if (!AGIPictureReadRaw(Raw + Index, &Data[Index], Error)) return false;
   return true;
}

FLASHMEM static void MHSPowerEngineSetResultTag(char A, char B, char C,
                                       uint8_t Argument0, uint8_t Argument1,
                                       uint8_t Capabilities, uint8_t Status)
{
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Source0 - AGIPicReg_ID0] = A;
   AGIPicRegisters[AGIPicReg_Source1 - AGIPicReg_ID0] = B;
   AGIPicRegisters[AGIPicReg_Source2 - AGIPicReg_ID0] = C;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] = Argument0;
   AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] = Argument1;
   AGIPicRegisters[AGIPicReg_MachineFlags - AGIPicReg_ID0] = Capabilities;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = Status;
}

FLASHMEM static bool MHSPowerEngineQuery(uint8_t Page)
{
   if (Page == 0)
   {
      MHSPowerEngineSetResultTag('M', 'H', 'S', MHSPEProtocolVersion, 4,
                                 MHSPECapabilities,
                                 AGIPicStatus_MPEQueryDone);
      return true;
   }
   if (Page == 1)
   {
      MHSPowerEngineSetResultTag('A', 'G', 'I', MHSPEServiceAGI,
                                 AGIPicProtocolV3, AGIPicV3Capabilities,
                                 AGIPicStatus_MPEQueryDone);
      return true;
   }
   if (Page == 2)
   {
      // PVM capability bits: verified descriptor, bounded bytecode, bounded
      // C64 DMA spans, and an explicit instruction watchdog.
      MHSPowerEngineSetResultTag('P', 'V', 'M', MHSPEServicePowerVM,
                                 MHSPEProtocolVersion, 0x0F,
                                 AGIPicStatus_MPEQueryDone);
      return true;
   }
   if (Page == 3)
   {
      // SCN v1 evaluates only compiler-certified, predecoded IF/GOTO control
      // flow. It returns before the first normal AGI command and buffers
      // said()'s HADMATCH side effect for the C64 to publish atomically.
      MHSPowerEngineSetResultTag('S', 'C', 'N', MHSPEServiceAGIScan,
                                 MHSPEProtocolVersion,
                                 MHSPEScanCapabilities,
                                 AGIPicStatus_MPEQueryDone);
      return true;
   }
   if (Page == 4)
   {
      // PQL v1 answers from the retained room-priority representation only.
      // Capability bit 0 guarantees mailbox-only operation, bit 1 binds the
      // request to the room-seed token, and bit 2 returns exact trigger/water
      // results for the complete candidate foot line.
      MHSPowerEngineSetResultTag('P', 'Q', 'L',
                                 MHSPEServicePriorityLine,
                                 MHSPEProtocolVersion,
                                 MHSPEPriorityLineCapabilities,
                                 AGIPicStatus_MPEQueryDone);
      return true;
   }
   return false;
}

FLASHMEM static bool MHSPEPowerVMOperand(uint16_t *PC, uint16_t Length,
                                uint8_t *Value)
{
   if (*PC >= Length) return false;
   *Value = MHSPEPowerVMCode[(*PC)++];
   return true;
}

FLASHMEM static bool MHSPEPowerVMRun(uint16_t CodeLength, uint16_t InputLength,
                            uint16_t OutputCapacity, uint16_t MaximumSteps,
                            uint16_t *OutputLength, uint8_t *Error)
{
   uint16_t PC = 0, Steps = 0, Written = 0;
   uint8_t A = 0, X = 0;
   bool Zero = true;
   memset(MHSPEPowerVMOutput, 0, OutputCapacity);
   while (true)
   {
      if (Steps++ >= MaximumSteps)
      {
         *Error = AGIPicError_PowerVMBudget;
         return false;
      }
      if (PC >= CodeLength)
      {
         *Error = AGIPicError_PowerVMFault;
         return false;
      }
      uint8_t Opcode = MHSPEPowerVMCode[PC++];
      uint8_t Operand = 0;
      switch (Opcode)
      {
         case 0x00: // HALT
            *OutputLength = Written;
            return true;
         case 0x01: // LDA immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &A)) break;
            Zero = A == 0;
            continue;
         case 0x02: // LDX immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &X)) break;
            Zero = X == 0;
            continue;
         case 0x03: // LDA input, absolute byte index
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand) ||
                Operand >= InputLength) break;
            A = MHSPEPowerVMInput[Operand]; Zero = A == 0;
            continue;
         case 0x04: // STA output, absolute byte index
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand) ||
                Operand >= OutputCapacity) break;
            MHSPEPowerVMOutput[Operand] = A;
            if (Written <= Operand) Written = (uint16_t)Operand + 1u;
            continue;
         case 0x05: // ADD immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            A += Operand; Zero = A == 0;
            continue;
         case 0x06: // XOR immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            A ^= Operand; Zero = A == 0;
            continue;
         case 0x07: // AND immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            A &= Operand; Zero = A == 0;
            continue;
         case 0x08: // OR immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            A |= Operand; Zero = A == 0;
            continue;
         case 0x09: // CMP immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            Zero = A == Operand;
            continue;
         case 0x0A: // branch if zero, signed relative to next instruction
         case 0x0B: // branch if nonzero
         {
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            bool Take = Opcode == 0x0A ? Zero : !Zero;
            if (!Take) continue;
            int32_t Target = (int32_t)PC + (int8_t)Operand;
            if (Target < 0 || Target >= CodeLength) break;
            PC = (uint16_t)Target;
            continue;
         }
         case 0x0C: X++; Zero = X == 0; continue; // INX
         case 0x0D: X--; Zero = X == 0; continue; // DEX
         case 0x0E: // LDA input,X
            if (X >= InputLength) break;
            A = MHSPEPowerVMInput[X]; Zero = A == 0;
            continue;
         case 0x0F: // STA output,X
            if (X >= OutputCapacity) break;
            MHSPEPowerVMOutput[X] = A;
            if (Written <= X) Written = (uint16_t)X + 1u;
            continue;
         case 0x10: // ADD input,X
            if (X >= InputLength) break;
            A += MHSPEPowerVMInput[X]; Zero = A == 0;
            continue;
         case 0x11: A = X; Zero = A == 0; continue; // TXA
         case 0x12: X = A; Zero = X == 0; continue; // TAX
         case 0x13: // SUB immediate
            if (!MHSPEPowerVMOperand(&PC, CodeLength, &Operand)) break;
            A -= Operand; Zero = A == 0;
            continue;
         case 0x14: A++; Zero = A == 0; continue; // INC A
         case 0x15: A--; Zero = A == 0; continue; // DEC A
      }
      *Error = AGIPicError_PowerVMFault;
      return false;
   }
}

FLASHMEM static bool MHSPowerEngineExecuteVM(uint32_t Raw, uint16_t *OutputLength,
                                    uint8_t *Error)
{
   uint8_t Descriptor[MHSPEDescriptorBytes];
   if (!AGIPictureReadRawBytes(Raw, Descriptor, sizeof(Descriptor), Error))
   {
      AGIPictureReleaseSource();
      *Error = AGIPicError_InvalidPowerTask;
      return false;
   }
   uint8_t Checksum = 0;
   for (uint8_t Index = 0; Index < MHSPEDescriptorBytes - 1; Index++)
      Checksum ^= Descriptor[Index];
   uint16_t CodeLength = Descriptor[7];
   uint16_t MaximumSteps = Descriptor[8] | ((uint16_t)Descriptor[9] << 8);
   uint16_t InputAddress = Descriptor[10] |
      ((uint16_t)Descriptor[11] << 8);
   uint16_t InputLength = Descriptor[12] |
      ((uint16_t)Descriptor[13] << 8);
   uint16_t OutputAddress = Descriptor[14] |
      ((uint16_t)Descriptor[15] << 8);
   uint16_t OutputCapacity = Descriptor[16] |
      ((uint16_t)Descriptor[17] << 8);
   bool ReservedClear = true;
   for (uint8_t Index = 18; Index < 23; Index++)
      if (Descriptor[Index]) ReservedClear = false;
   bool DescriptorOkay = Descriptor[0] == 'M' && Descriptor[1] == 'P' &&
      Descriptor[2] == 'E' && Descriptor[3] == '1' &&
      Descriptor[4] == MHSPEProtocolVersion &&
      Descriptor[5] == MHSPEServicePowerVM && Descriptor[6] == 0 &&
      CodeLength && CodeLength <= MHSPEMaximumCodeBytes && MaximumSteps &&
      Descriptor[13] <= 1 && Descriptor[17] <= 1 &&
      InputLength <= MHSPEMaximumIOBytes && OutputCapacity &&
      OutputCapacity <= MHSPEMaximumIOBytes &&
      (uint32_t)InputAddress + InputLength <= 0x10000u &&
      (uint32_t)OutputAddress + OutputCapacity <= 0x10000u &&
      ReservedClear && Descriptor[23] == Checksum;
   if (!DescriptorOkay ||
       !AGIPictureReadRawBytes(Raw + MHSPEDescriptorBytes,
                               MHSPEPowerVMCode, CodeLength, Error))
   {
      AGIPictureReleaseSource();
      *Error = AGIPicError_InvalidPowerTask;
      return false;
   }
   AGIPictureReleaseSource();
   if (DMA_State != DMA_S_DisableReady)
   {
      *Error = AGIPicError_DMAUnavailable;
      return false;
   }
   AGIPictureApplyVideoTiming();
   if (InputLength &&
       (!PerformDMA(true, InputAddress, MHSPEPowerVMInput, InputLength, false) ||
        !CloseDMA()))
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   if (!MHSPEPowerVMRun(CodeLength, InputLength, OutputCapacity,
                        MaximumSteps, OutputLength, Error)) return false;
   if (*OutputLength &&
       (!PerformDMA(false, OutputAddress, MHSPEPowerVMOutput,
                    *OutputLength, false) || !CloseDMA()))
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   return true;
}

// MPS1 is a deliberately narrow AGI control-flow service. The descriptor is
// compiler generated and game independent: it identifies one predecoded
// Logic-0 byte stream, proves its complete contents with CRC16, and bounds the
// number of IF/GOTO nodes that firmware may visit. No AGI command is executed
// here. The first ordinary command remains unconsumed for the C64 interpreter.
struct stcMHSPEScanTask
{
   uint32_t Raw;
   uint16_t Length;
   uint16_t Entry;
   uint16_t MaximumSteps;
};

struct stcMHSPEScanCursor
{
   stcMHSPEScanTask Task;
   uint16_t IP;
   uint16_t Steps;
   uint8_t Error;
};

FLASHMEM static uint16_t MHSPEScanCRC16Byte(uint16_t CRC, uint8_t Byte)
{
   CRC ^= (uint16_t)Byte << 8;
   for (uint8_t Bit = 0; Bit < 8; Bit++)
      CRC = (CRC & 0x8000u) ? (uint16_t)((CRC << 1) ^ 0x1021u) :
         (uint16_t)(CRC << 1);
   return CRC;
}

FLASHMEM static uint16_t MHSPEScanCRC16(const uint8_t *Data, uint16_t Length)
{
   uint16_t CRC = 0xFFFFu;
   for (uint16_t Index = 0; Index < Length; Index++)
      CRC = MHSPEScanCRC16Byte(CRC, Data[Index]);
   return CRC;
}

FLASHMEM static bool MHSPEScanFetch(stcMHSPEScanCursor *Cursor,
                                    uint8_t *Value)
{
   if (AGIPicAbortRequested)
   {
      Cursor->Error = AGIPicError_DMATimeout;
      return false;
   }
   if (Cursor->IP >= Cursor->Task.Length)
   {
      Cursor->Error = AGIPicError_InvalidScanTask;
      return false;
   }
   uint8_t Error = AGIPicError_None;
   if (!AGIPictureReadRaw(Cursor->Task.Raw + Cursor->IP, Value, &Error))
   {
      Cursor->Error = AGIPicAbortRequested ? AGIPicError_DMATimeout :
         AGIPicError_InvalidScanTask;
      return false;
   }
   Cursor->IP++;
   return true;
}

FLASHMEM static bool MHSPEScanFetch16(stcMHSPEScanCursor *Cursor,
                                      uint16_t *Value)
{
   uint8_t Low, High;
   if (!MHSPEScanFetch(Cursor, &Low) || !MHSPEScanFetch(Cursor, &High))
      return false;
   *Value = Low | ((uint16_t)High << 8);
   return true;
}

FLASHMEM static bool MHSPEScanFlag(uint8_t Flag)
{
   return (MHSPEScanCoreState[256u + (Flag >> 3)] &
           (1u << (Flag & 7))) != 0;
}

FLASHMEM static void MHSPEScanSetFlag(uint8_t Flag)
{
   MHSPEScanCoreState[256u + (Flag >> 3)] |= 1u << (Flag & 7);
}

FLASHMEM static bool MHSPEScanCondition(stcMHSPEScanCursor *Cursor,
                                        uint8_t Token, bool Evaluate,
                                        bool *Result)
{
   uint8_t A, B;
   *Result = false;
   switch (Token)
   {
      case 0x41: // equaln
      case 0x42: // equalv
      case 0x43: // lessn
      case 0x44: // lessv
      case 0x45: // greatern
      case 0x46: // greaterv
         if (!MHSPEScanFetch(Cursor, &A) || !MHSPEScanFetch(Cursor, &B))
            return false;
         if (!Evaluate) return true;
         if (Token == 0x41) *Result = MHSPEScanCoreState[A] == B;
         else if (Token == 0x42)
            *Result = MHSPEScanCoreState[A] == MHSPEScanCoreState[B];
         else if (Token == 0x43) *Result = MHSPEScanCoreState[A] < B;
         else if (Token == 0x44)
            *Result = MHSPEScanCoreState[A] < MHSPEScanCoreState[B];
         else if (Token == 0x45) *Result = MHSPEScanCoreState[A] > B;
         else *Result = MHSPEScanCoreState[A] > MHSPEScanCoreState[B];
         return true;
      case 0x27: // isset
      case 0x28: // issetv
         if (!MHSPEScanFetch(Cursor, &A)) return false;
         if (Evaluate)
         {
            if (Token == 0x28) A = MHSPEScanCoreState[A];
            *Result = MHSPEScanFlag(A);
         }
         return true;
      case 0x2C: // controller
         if (!MHSPEScanFetch(Cursor, &A)) return false;
         if (Evaluate) *Result = MHSPEScanControllers[A] != 0;
         return true;
      case 0x8E: // said
      {
         uint8_t Count;
         if (!MHSPEScanFetch(Cursor, &Count)) return false;
         bool Match = Evaluate && MHSPEScanFlag(2) && !MHSPEScanFlag(4);
         bool RestOfLine = false;
         for (uint16_t Word = 0; Word < Count; Word++)
         {
            if (!MHSPEScanFetch(Cursor, &A) || !MHSPEScanFetch(Cursor, &B))
               return false;
            if (!Evaluate || RestOfLine) continue;
            if (A == 0x0F && B == 0x27)
            {
               RestOfLine = true;
               continue;
            }
            if (Word >= MHSPEScanParsedWordLimit ||
                Word >= MHSPEScanParsedCount)
            {
               Match = false;
               continue;
            }
            // AGI word $0001 is the any-word token.
            if (A == 0x01 && B == 0) continue;
            if (MHSPEScanParsedWords[Word] != A ||
                MHSPEScanParsedWords[MHSPEScanParsedWordLimit + Word] != B)
               Match = false;
         }
         if (Evaluate && !RestOfLine && MHSPEScanParsedCount != Count)
            Match = false;
         if (Match)
         {
            MHSPEScanSetFlag(4);
            MHSPEScanBufferedHadMatch = true;
         }
         *Result = Match;
         return true;
      }
      default:
         Cursor->Error = AGIPicError_ScanUnsupported;
         return false;
   }
}

// This mirrors the resident VM's left-to-right short circuit rules. Skipped
// terms are still decoded and capability checked, but side-effecting said()
// is evaluated only when the C64 VM would have reached it.
FLASHMEM static bool MHSPEScanExpression(stcMHSPEScanCursor *Cursor,
                                         bool *Result)
{
   bool Cycle = true;
   bool PendingNot = false;
   bool OrMode = false;
   bool OrResult = false;
   bool OrHasTerm = false;
   while (true)
   {
      uint8_t Token;
      if (!MHSPEScanFetch(Cursor, &Token)) return false;
      if (Token == 0xFF)
      {
         if (PendingNot || OrMode)
         {
            Cursor->Error = AGIPicError_InvalidScanTask;
            return false;
         }
         *Result = Cycle;
         return true;
      }
      if (Token == 0xFD)
      {
         PendingNot = !PendingNot;
         continue;
      }
      if (Token == 0xFC)
      {
         if (PendingNot)
         {
            Cursor->Error = AGIPicError_InvalidScanTask;
            return false;
         }
         if (!OrMode)
         {
            OrMode = true;
            OrResult = false;
            OrHasTerm = false;
         }
         else
         {
            if (!OrHasTerm)
            {
               Cursor->Error = AGIPicError_InvalidScanTask;
               return false;
            }
            OrMode = false;
            Cycle = Cycle && OrResult;
         }
         continue;
      }
      bool Evaluate = Cycle && (!OrMode || !OrResult);
      bool Term = false;
      if (!MHSPEScanCondition(Cursor, Token, Evaluate, &Term)) return false;
      if (PendingNot && Evaluate) Term = !Term;
      PendingNot = false;
      if (OrMode)
      {
         OrHasTerm = true;
         if (Evaluate && Term) OrResult = true;
      }
      else if (Evaluate) Cycle = Cycle && Term;
   }
}

// Every snapshot segment is input-only. CloseDMA is called regardless of the
// transfer result so an abort, timeout, or malformed task cannot publish a
// terminal mailbox state while /DMA or any bus driver is still asserted.
FLASHMEM static bool MHSPEScanSnapshotSegment(uint16_t Address, uint8_t *Data,
                                              uint16_t Length,
                                              uint8_t *Error)
{
   if (AGIPicAbortRequested)
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   if (DMA_State != DMA_S_DisableReady)
   {
      bool Closed = CloseDMA();
      *Error = Closed && DMA_State == DMA_S_DisableReady ?
         AGIPicError_DMAUnavailable : AGIPicError_DMATimeout;
      return false;
   }
   bool Transferred = PerformDMA(true, Address, Data, Length, false);
   bool Closed = CloseDMA();
   if (!Transferred || !Closed || DMA_State != DMA_S_DisableReady)
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   if (AGIPicAbortRequested)
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }
   return true;
}

FLASHMEM static bool MHSPEScanSnapshot(uint8_t *Error)
{
   AGIPictureApplyVideoTiming();
   return MHSPEScanSnapshotSegment(MHSPEScanVariablesAddress,
                                   MHSPEScanCoreState,
                                   sizeof(MHSPEScanCoreState), Error) &&
      MHSPEScanSnapshotSegment(MHSPEScanControllerAddress,
                               MHSPEScanControllers,
                               sizeof(MHSPEScanControllers), Error) &&
      MHSPEScanSnapshotSegment(MHSPEScanParsedCountAddress,
                               &MHSPEScanParsedCount, 1, Error) &&
      MHSPEScanSnapshotSegment(MHSPEScanParsedWordLowAddress,
                               MHSPEScanParsedWords,
                               sizeof(MHSPEScanParsedWords), Error);
}

FLASHMEM static bool MHSPowerEngineValidateScan(uint32_t DescriptorRaw,
                                                stcMHSPEScanTask *Task,
                                                uint8_t *Error)
{
   uint8_t Descriptor[MHSPEScanDescriptorBytes];
   if (!AGIPictureReadRawBytes(DescriptorRaw, Descriptor,
                               sizeof(Descriptor), Error))
   {
      AGIPictureReleaseSource();
      *Error = AGIPicError_InvalidScanTask;
      return false;
   }
   uint16_t DescriptorCRC = Descriptor[22] |
      ((uint16_t)Descriptor[23] << 8);
   Task->Raw = (uint32_t)Descriptor[8] |
      ((uint32_t)Descriptor[9] << 8) |
      ((uint32_t)Descriptor[10] << 16);
   Task->Length = Descriptor[12] | ((uint16_t)Descriptor[13] << 8);
   Task->Entry = Descriptor[14] | ((uint16_t)Descriptor[15] << 8);
   Task->MaximumSteps = Descriptor[16] |
      ((uint16_t)Descriptor[17] << 8);
   uint16_t ExpectedCodeCRC = Descriptor[18] |
      ((uint16_t)Descriptor[19] << 8);
   bool DescriptorOkay = Descriptor[0] == 'M' && Descriptor[1] == 'P' &&
      Descriptor[2] == 'S' && Descriptor[3] == '1' &&
      Descriptor[4] == MHSPEProtocolVersion &&
      Descriptor[5] == MHSPEServiceAGIScan && Descriptor[6] == 0 &&
      Descriptor[7] == MHSPEScanFormatPredecoded && !Descriptor[11] &&
      Task->Length && Task->Entry == 0 &&
      Task->MaximumSteps &&
      !Descriptor[20] && !Descriptor[21] &&
      DescriptorCRC == MHSPEScanCRC16(Descriptor, 22) &&
      AGIPictureRawSpanValid(Task->Raw, Task->Length);
   if (!DescriptorOkay)
   {
      AGIPictureReleaseSource();
      *Error = AGIPicError_InvalidScanTask;
      return false;
   }
   if (MHSPEScanValidated &&
       MHSPEScanValidatedDescriptorRaw == DescriptorRaw &&
       MHSPEScanValidatedCodeRaw == Task->Raw &&
       MHSPEScanValidatedCodeLength == Task->Length &&
       MHSPEScanValidatedCodeCRC == ExpectedCodeCRC &&
       MHSPEScanValidatedDescriptorCRC == DescriptorCRC)
   {
      AGIPictureReleaseSource();
      return true;
   }
   uint16_t CodeCRC = 0xFFFFu;
   for (uint16_t Index = 0; Index < Task->Length; Index++)
   {
      uint8_t Byte;
      if (AGIPicAbortRequested ||
          !AGIPictureReadRaw(Task->Raw + Index, &Byte, Error))
      {
         AGIPictureReleaseSource();
         *Error = AGIPicAbortRequested ? AGIPicError_DMATimeout :
            AGIPicError_InvalidScanTask;
         return false;
      }
      CodeCRC = MHSPEScanCRC16Byte(CodeCRC, Byte);
   }
   AGIPictureReleaseSource();
   if (CodeCRC != ExpectedCodeCRC)
   {
      *Error = AGIPicError_InvalidScanTask;
      return false;
   }
   // Cartridge bytes are immutable for the lifetime of an authenticated MPE
   // session. Cache only the successful complete-code proof; activation/reset
   // invalidates it so selecting another cartridge can never reuse a result.
   MHSPEScanValidatedDescriptorRaw = DescriptorRaw;
   MHSPEScanValidatedCodeRaw = Task->Raw;
   MHSPEScanValidatedCodeLength = Task->Length;
   MHSPEScanValidatedCodeCRC = ExpectedCodeCRC;
   MHSPEScanValidatedDescriptorCRC = DescriptorCRC;
   MHSPEScanValidated = true;
   return true;
}

FLASHMEM static bool MHSPowerEngineAGIScan(uint32_t DescriptorRaw,
                                           uint16_t *Continuation,
                                           uint8_t *Effects,
                                           uint8_t *Error)
{
   stcMHSPEScanTask Task;
   if (!MHSPEActive ||
       !MHSPowerEngineValidateScan(DescriptorRaw, &Task, Error)) return false;
   // Do not inspect C64 state until the signature, fixed ABI, both CRCs, raw
   // span, entry point, and watchdog have all been accepted.
   if (!MHSPEScanSnapshot(Error)) return false;
   if (MHSPEScanParsedCount > MHSPEScanParsedWordLimit)
   {
      *Error = AGIPicError_InvalidScanTask;
      return false;
   }
   MHSPEScanBufferedHadMatch = false;

   stcMHSPEScanCursor Cursor = {Task, Task.Entry, 0, AGIPicError_None};
   while (true)
   {
      if (AGIPicAbortRequested)
      {
         Cursor.Error = AGIPicError_DMATimeout;
         break;
      }
      if (Cursor.IP == Task.Length)
      {
         Cursor.Error = AGIPicError_InvalidScanTask;
         break;
      }
      uint16_t OpcodeIP = Cursor.IP;
      uint8_t Opcode;
      if (!MHSPEScanFetch(&Cursor, &Opcode)) break;
      // The watchdog counts only accelerator-owned control nodes. Reading the
      // ordinary hand-back command must not consume an extra branch step.
      if ((Opcode == 0xFF || Opcode == 0xFE) &&
          Cursor.Steps++ >= Task.MaximumSteps)
      {
         Cursor.Error = AGIPicError_ScanBudget;
         break;
      }
      if (Opcode == 0xFF)
      {
         bool Pass;
         uint16_t Failure;
         if (!MHSPEScanExpression(&Cursor, &Pass) ||
             !MHSPEScanFetch16(&Cursor, &Failure)) break;
         if (Failure > Task.Length)
         {
            Cursor.Error = AGIPicError_InvalidScanTask;
            break;
         }
         if (!Pass) Cursor.IP = Failure;
         continue;
      }
      if (Opcode == 0xFE)
      {
         uint16_t Target;
         if (!MHSPEScanFetch16(&Cursor, &Target)) break;
         if (Target > Task.Length)
         {
            Cursor.Error = AGIPicError_InvalidScanTask;
            break;
         }
         Cursor.IP = Target;
         continue;
      }
      if (Opcode >= 0xB7)
      {
         Cursor.Error = AGIPicError_InvalidScanTask;
         break;
      }
      // Any ordinary command is the certified hand-back boundary. It was read
      // only to classify the boundary and remains unconsumed by the C64 VM.
      *Continuation = OpcodeIP;
      *Effects = MHSPEScanBufferedHadMatch ? 1 : 0;
      AGIPictureReleaseSource();
      return true;
   }
   AGIPictureReleaseSource();
   *Error = Cursor.Error ? Cursor.Error : AGIPicError_InvalidScanTask;
   return false;
}

FLASHMEM static void MHSPowerEngineSetScanResult(uint16_t Continuation,
                                                 uint8_t Effects,
                                                 uint8_t Token)
{
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   if (AGIPicAbortRequested)
   {
      __set_primask(InterruptMask);
      AGIPictureAbort();
      return;
   }
   AGIPicRegisters[AGIPicReg_Source0 - AGIPicReg_ID0] = 'S';
   AGIPicRegisters[AGIPicReg_Source1 - AGIPicReg_ID0] = 'C';
   AGIPicRegisters[AGIPicReg_Source2 - AGIPicReg_ID0] = 'N';
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] = Continuation;
   AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] = Continuation >> 8;
   AGIPicRegisters[AGIPicReg_MachineFlags - AGIPicReg_ID0] = Effects & 1;
   AGIPicRegisters[AGIPicReg_Token - AGIPicReg_ID0] = Token;
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_MPEAGIScanDone;
   __set_primask(InterruptMask);
}

FLASHMEM static void MHSPowerEngineSetScanError(uint8_t Error)
{
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   if (AGIPicAbortRequested)
   {
      __set_primask(InterruptMask);
      AGIPictureAbort();
      return;
   }
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = Error;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_ErrorBase | (Error & 0x1F);
   __set_primask(InterruptMask);
}

// PQL v1 is deliberately stateless and mailbox-only. It reads the retained
// room priority representation already populated by command $22; it never
// opens DMA, reads the cartridge, or writes C64 memory. A valid blocked line is
// a successful command with a zero result so the C64 can distinguish it from a
// malformed/stale request and use its native fallback only for the latter.
FLASHMEM static bool MHSPowerEnginePriorityLine(uint8_t X, uint8_t Y,
                                       uint8_t Width, uint8_t Policy,
                                       uint16_t Reserved, uint8_t MachineFlags,
                                       uint8_t Token, uint8_t *Result)
{
   *Result = 0;
   if (!MHSPEActive || !AGIPicRoomValid ||
       Token != AGIPicRoomToken || !AGIPicRoomPriorityLength ||
       (AGIPicRoomPriorityFormat != 2 && AGIPicRoomPriorityFormat != 3) ||
       !Width || X >= 160 || Y >= 168 ||
       (uint16_t)X + Width > 160u ||
       (Policy & ~MHSPEPriorityLinePolicyMask) || Reserved ||
       (MachineFlags & ~AGIPicMachine_NTSC))
      return false;

   bool TouchedTrigger = false;
   bool AllWater = true;
   for (uint16_t Offset = 0; Offset < Width; Offset++)
   {
      uint8_t Priority;
      if (!AGIPictureRoomPriorityAt((uint8_t)(X + Offset), Y, &Priority))
         return false;

      bool Passable;
      if (Priority == 3)
         Passable = (Policy & 0x02) == 0;
      else
      {
         AllWater = false;
         if (Priority == 0)
            Passable = false;
         else if (Priority == 1)
            Passable = (Policy & 0x20) != 0 && (Policy & 0x01) == 0;
         else
         {
            if (Priority == 2) TouchedTrigger = true;
            Passable = (Policy & 0x01) == 0;
         }
      }
      if (!Passable) return true;
   }

   *Result = MHSPEPriorityLineResultPass |
      (TouchedTrigger ? MHSPEPriorityLineResultTrigger : 0) |
      (AllWater ? MHSPEPriorityLineResultAllWater : 0);
   return true;
}

FLASHMEM static void MHSPowerEngineSetPriorityLineResult(uint8_t Result,
                                                uint8_t Token)
{
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   if (AGIPicAbortRequested)
   {
      __set_primask(InterruptMask);
      AGIPictureAbort();
      return;
   }
   AGIPicRegisters[AGIPicReg_Source0 - AGIPicReg_ID0] = 'P';
   AGIPicRegisters[AGIPicReg_Source1 - AGIPicReg_ID0] = 'Q';
   AGIPicRegisters[AGIPicReg_Source2 - AGIPicReg_ID0] = 'L';
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] = Result;
   AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] = 0;
   AGIPicRegisters[AGIPicReg_MachineFlags - AGIPicReg_ID0] =
      MHSPEPriorityLineCapabilities;
   AGIPicRegisters[AGIPicReg_Token - AGIPicReg_ID0] = Token;
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_MPEPriorityDone;
   __set_primask(InterruptMask);
}

FLASHMEM static void MHSPowerEngineSetPriorityLineError()
{
   uint32_t InterruptMask = __get_primask();
   __disable_irq();
   if (AGIPicAbortRequested)
   {
      __set_primask(InterruptMask);
      AGIPictureAbort();
      return;
   }
   AGIPicHoldHelperBank = false;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] =
      AGIPicError_InvalidPriorityQuery;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      AGIPicStatus_ErrorBase | AGIPicError_InvalidPriorityQuery;
   __set_primask(InterruptMask);
}

static bool AGIPictureReadRaw16(uint32_t Raw, uint16_t *Value,
                                 uint8_t *Error)
{
   uint8_t Bytes[2];
   if (!AGIPictureReadRawBytes(Raw, Bytes, sizeof(Bytes), Error)) return false;
   *Value = Bytes[0] | ((uint16_t)Bytes[1] << 8);
   return true;
}

FLASHMEM static bool AGIPicturePrepareGBC1Index(uint32_t Root, uint8_t *Error)
{
   if (AGIPicGBC1IndexValid && AGIPicGBC1IndexRoot == Root) return true;
   if (!AGIPictureRawSpanValid(Root, AGIPicGBC1IndexBytes))
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   AGIPicGBC1IndexValid = false;
   memset(AGIPicGBC1ViewCache, 0, sizeof(AGIPicGBC1ViewCache));
   memset(AGIPicGBC1PatternRaw, 0xFF, sizeof(AGIPicGBC1PatternRaw));
   AGIPicGBC1ViewCacheGeneration = 0;
   AGIPicGBC1PatternNext = 0;
   if (!AGIPictureReadRawBytes(Root, AGIPicGBC1IndexCache,
                               AGIPicGBC1IndexBytes, Error)) return false;
   AGIPicGBC1IndexRoot = Root;
   AGIPicGBC1IndexValid = true;
   return true;
}

FLASHMEM static bool AGIPictureCacheGBC1View(uint32_t Raw, uint16_t Length,
                                    stcAGIGBC1ViewCache **Prepared,
                                    uint8_t *Error)
{
   int8_t Selected = -1;
   uint32_t OldestGeneration = UINT32_MAX;
   for (uint8_t Index = 0; Index < AGIPicGBC1ViewCacheEntries; Index++)
   {
      stcAGIGBC1ViewCache *Entry = &AGIPicGBC1ViewCache[Index];
      if ((Entry->Cached || Entry->Validated) && Entry->Raw == Raw &&
          Entry->Length == Length)
      {
         Entry->Generation = AGIPicGBC1ViewCacheGeneration;
         *Prepared = Entry;
         return true;
      }
      if (!Entry->Cached && !Entry->Validated)
      {
         if (Selected < 0) Selected = Index;
      }
      else if (Entry->Generation != AGIPicGBC1ViewCacheGeneration &&
               Entry->Generation < OldestGeneration)
      {
         Selected = Index;
         OldestGeneration = Entry->Generation;
      }
   }
   // At most twenty object records are resolved in one generation.  Therefore
   // an entry not used by the current cohort must exist before a 21st distinct
   // VIEW can be requested.  Keep a source-read fallback for defensive safety.
   if (Selected < 0)
   {
      *Prepared = NULL;
      return true;
   }
   stcAGIGBC1ViewCache *Entry = &AGIPicGBC1ViewCache[(uint8_t)Selected];
   memset(Entry, 0, sizeof(*Entry));
   Entry->Raw = Raw;
   Entry->Offset = (uint32_t)(uint8_t)Selected * AGIPicGBC1ViewCacheSlotBytes;
   Entry->Generation = AGIPicGBC1ViewCacheGeneration;
   Entry->Length = Length;
   if (AGIPictureGBC1CacheAvailable())
   {
      if (Length > AGIPicGBC1ViewCacheSlotBytes ||
          Entry->Offset > AGIPicGBC1ViewCacheCapacity - Length)
      {
         *Error = AGIPicError_MalformedGBC1;
         return false;
      }
      for (uint16_t Index = 0; Index < Length; Index++)
      {
         if (!AGIPictureReadRaw(Raw + Index,
               &AGIPicGBC1ViewCacheMemory[Entry->Offset + Index], Error))
            return false;
      }
      Entry->Cached = true;
   }
   *Prepared = Entry;
   return true;
}

FLASHMEM static bool AGIPictureMalformedGBC1(uint8_t *Error)
{
   *Error = AGIPicAbortRequested ? AGIPicError_DMATimeout :
      AGIPicError_MalformedGBC1;
   return false;
}

// Validate the complete canonical GBC1 record before any of its pixels can be
// published.  This mirrors the host inspector: loops cover every cel in order,
// exact blocks are contiguous in first-reference order, dictionary pointers
// are aligned 16-byte blocks, edge padding is transparent, and every unique
// nonempty dictionary pattern is referenced.
FLASHMEM static bool AGIPictureValidateGBC1View(uint32_t Raw, uint16_t Length,
                                                stcAGIGBC1ViewCache *Prepared,
                                                uint8_t *Error)
{
   if (Prepared && Prepared->Validated) return true;
   uint8_t Header[20];
   if (Length < sizeof(Header) ||
       !AGIPictureReadRawBytes(Raw, Header, sizeof(Header), Error))
      return AGIPictureMalformedGBC1(Error);
   uint8_t LoopCount = Header[6];
   uint16_t CelCount = Header[8] | ((uint16_t)Header[9] << 8);
   uint16_t LoopDirectory = Header[10] | ((uint16_t)Header[11] << 8);
   uint16_t CelDirectory = Header[12] | ((uint16_t)Header[13] << 8);
   uint16_t PayloadStart = Header[14] | ((uint16_t)Header[15] << 8);
   uint16_t Description = Header[16] | ((uint16_t)Header[17] << 8);
   uint16_t Total = Header[18] | ((uint16_t)Header[19] << 8);
   if (memcmp(Header, "GBC1", 4) != 0 || Header[4] != 1 ||
       Header[5] != 7 || !LoopCount || Header[7] != 8 || !CelCount ||
       CelCount > AGIPicGBC1MaximumCels || Total != Length ||
       LoopDirectory != 20 ||
       CelDirectory != LoopDirectory + (uint16_t)LoopCount * 3u ||
       PayloadStart != CelDirectory + (uint32_t)CelCount * 8u ||
       PayloadStart >= Total ||
       (Description && (Description < PayloadStart || Description >= Total)))
      return AGIPictureMalformedGBC1(Error);

   uint16_t ExpectedFirstCel = 0;
   for (uint8_t Loop = 0; Loop < LoopCount; Loop++)
   {
      uint8_t Entry[3];
      if (!AGIPictureReadRawBytes(Raw + LoopDirectory +
                                  (uint16_t)Loop * 3u,
                                  Entry, sizeof(Entry), Error)) return false;
      uint16_t FirstCel = Entry[0] | ((uint16_t)Entry[1] << 8);
      if (FirstCel != ExpectedFirstCel ||
          FirstCel + (uint16_t)Entry[2] > CelCount)
         return AGIPictureMalformedGBC1(Error);
      ExpectedFirstCel += Entry[2];
   }
   if (ExpectedFirstCel != CelCount)
      return AGIPictureMalformedGBC1(Error);

   uint16_t DictionaryEnd = Description ? Description : Total;
   uint16_t DictionaryStart = PayloadStart;
   uint16_t PayloadCount = 0;
   for (uint16_t CelIndex = 0; CelIndex < CelCount; CelIndex++)
   {
      if (AGIPicAbortRequested) return AGIPictureMalformedGBC1(Error);
      uint8_t Cel[8];
      if (!AGIPictureReadRawBytes(Raw + CelDirectory +
                                  (uint32_t)CelIndex * 8u,
                                  Cel, sizeof(Cel), Error)) return false;
      uint16_t Payload = Cel[0] | ((uint16_t)Cel[1] << 8);
      uint8_t Width = Cel[2];
      uint8_t Height = Cel[3];
      if (!Width || !Height || (Cel[4] & ~1u) ||
          ((Cel[4] & 1u) ? Cel[5] > 7 : Cel[5] != 0xFF) ||
          Cel[6] > 15 || Cel[7] || Payload < PayloadStart ||
          DictionaryEnd < 4 || Payload > DictionaryEnd - 4)
         return AGIPictureMalformedGBC1(Error);
      uint8_t Exact[4];
      if (!AGIPictureReadRawBytes(Raw + Payload, Exact, sizeof(Exact), Error))
         return false;
      uint8_t Columns = (Width + 3u) >> 2;
      uint8_t Rows = (Height + 7u) >> 3;
      uint32_t PayloadEnd = (uint32_t)Payload + 4u +
         (uint32_t)Columns * Rows * 2u;
      if (Exact[0] != 0x43 || Exact[1] != 1 || Exact[2] != Columns ||
          Exact[3] != Rows || PayloadEnd > DictionaryEnd)
         return AGIPictureMalformedGBC1(Error);

      stcAGIGBC1PayloadInfo *Prior = NULL;
      for (uint16_t Index = 0; Index < PayloadCount; Index++)
      {
         if (!(Index & 0x3F) && AGIPicAbortRequested)
            return AGIPictureMalformedGBC1(Error);
         if (AGIPicGBC1ValidationScratch.Payloads[Index].Offset == Payload)
         {
            Prior = &AGIPicGBC1ValidationScratch.Payloads[Index];
            break;
         }
      }
      if (Prior)
      {
         if (Prior->End != PayloadEnd || Prior->Width != Width ||
             Prior->Height != Height)
            return AGIPictureMalformedGBC1(Error);
      }
      else
      {
         if (Payload != DictionaryStart || PayloadCount >= CelCount)
            return AGIPictureMalformedGBC1(Error);
         stcAGIGBC1PayloadInfo *Added =
            &AGIPicGBC1ValidationScratch.Payloads[PayloadCount++];
         Added->Offset = Payload;
         Added->End = (uint16_t)PayloadEnd;
         Added->Width = Width;
         Added->Height = Height;
         DictionaryStart = (uint16_t)PayloadEnd;
      }
   }
   if (DictionaryEnd < DictionaryStart ||
       (DictionaryEnd - DictionaryStart) % AGIPicGBC1PatternBytes)
      return AGIPictureMalformedGBC1(Error);

   if (Description)
   {
      for (uint16_t Offset = Description; Offset < Total; Offset++)
      {
         uint8_t Byte;
         if (!AGIPictureReadRawBytes(Raw + Offset, &Byte, 1, Error))
            return false;
         if ((Offset == Total - 1) != (Byte == 0))
            return AGIPictureMalformedGBC1(Error);
      }
   }

   uint16_t PatternCount =
      (DictionaryEnd - DictionaryStart) / AGIPicGBC1PatternBytes;
   if (PatternCount > AGIPicGBC1MaximumPatterns)
      return AGIPictureMalformedGBC1(Error);
   memset(AGIPicGBC1PatternReferenced, 0,
          (PatternCount + 7u) / 8u);
   for (uint16_t CelIndex = 0; CelIndex < CelCount; CelIndex++)
   {
      uint8_t Cel[8];
      if (!AGIPictureReadRawBytes(Raw + CelDirectory +
                                  (uint32_t)CelIndex * 8u,
                                  Cel, sizeof(Cel), Error)) return false;
      uint16_t Payload = Cel[0] | ((uint16_t)Cel[1] << 8);
      uint8_t Exact[4];
      if (!AGIPictureReadRawBytes(Raw + Payload, Exact, sizeof(Exact), Error))
         return false;
      uint8_t Columns = Exact[2];
      uint8_t Rows = Exact[3];
      uint16_t Cells = (uint16_t)Columns * Rows;
      for (uint16_t Cell = 0; Cell < Cells; Cell++)
      {
         if (AGIPicAbortRequested) return AGIPictureMalformedGBC1(Error);
         uint16_t Relative;
         if (!AGIPictureReadRaw16(Raw + Payload + 4u + Cell * 2u,
                                  &Relative, Error)) return false;
         if (!Relative) continue;
         uint32_t Pointer = (uint32_t)Payload + Relative;
         if (Pointer < DictionaryStart ||
             Pointer > (uint32_t)DictionaryEnd - AGIPicGBC1PatternBytes ||
             (Pointer - DictionaryStart) % AGIPicGBC1PatternBytes)
            return AGIPictureMalformedGBC1(Error);
         uint16_t PatternIndex =
            (Pointer - DictionaryStart) / AGIPicGBC1PatternBytes;
         AGIPicGBC1PatternReferenced[PatternIndex >> 3] |=
            1u << (PatternIndex & 7u);
         uint8_t Pattern[AGIPicGBC1PatternBytes];
         if (!AGIPictureReadRawBytes(Raw + Pointer, Pattern,
                                     sizeof(Pattern), Error)) return false;
         bool Opaque = false;
         uint8_t CellX = Cell % Columns;
         uint8_t CellY = Cell / Columns;
         for (uint8_t Pixel = 0; Pixel < 32; Pixel++)
         {
            uint8_t Packed = Pattern[Pixel >> 1];
            uint8_t Value = (Pixel & 1u) ? (Packed & 15u) : (Packed >> 4);
            uint16_t SourceX = (uint16_t)CellX * 4u + (Pixel & 3u);
            uint16_t SourceY = (uint16_t)CellY * 8u + (Pixel >> 2);
            if (Value == 12 ||
                ((SourceX >= Cel[2] || SourceY >= Cel[3]) && Value != 8))
               return AGIPictureMalformedGBC1(Error);
            if (SourceX < Cel[2] && SourceY < Cel[3] && Value != 8)
               Opaque = true;
         }
         if (!Opaque) return AGIPictureMalformedGBC1(Error);
      }
   }
   for (uint16_t PatternIndex = 0; PatternIndex < PatternCount;
        PatternIndex++)
   {
      if (!(AGIPicGBC1PatternReferenced[PatternIndex >> 3] &
            (1u << (PatternIndex & 7u))))
         return AGIPictureMalformedGBC1(Error);
      uint8_t Pattern[AGIPicGBC1PatternBytes];
      if (!AGIPictureReadRawBytes(Raw + DictionaryStart +
                                  (uint32_t)PatternIndex *
                                     AGIPicGBC1PatternBytes,
                                  Pattern, sizeof(Pattern), Error)) return false;
      uint32_t Hash = 2166136261u;
      for (uint8_t Byte = 0; Byte < sizeof(Pattern); Byte++)
         Hash = (Hash ^ Pattern[Byte]) * 16777619u;
      for (uint16_t Prior = 0; Prior < PatternIndex; Prior++)
      {
         if (!(Prior & 0x3F) && AGIPicAbortRequested)
            return AGIPictureMalformedGBC1(Error);
         if (AGIPicGBC1ValidationScratch.PatternHashes[Prior] != Hash)
            continue;
         uint8_t PriorPattern[AGIPicGBC1PatternBytes];
         if (!AGIPictureReadRawBytes(Raw + DictionaryStart +
                                     (uint32_t)Prior *
                                        AGIPicGBC1PatternBytes,
                                     PriorPattern, sizeof(PriorPattern), Error))
            return false;
         if (!memcmp(Pattern, PriorPattern, sizeof(Pattern)))
            return AGIPictureMalformedGBC1(Error);
      }
      AGIPicGBC1ValidationScratch.PatternHashes[PatternIndex] = Hash;
   }
   if (Prepared) Prepared->Validated = true;
   return true;
}

static uint8_t AGIPictureAutomaticPriority(uint8_t Y)
{
   uint8_t Priority = Y / 12u + 1u;
   if (Priority < 4) Priority = 4;
   if (Priority > 15) Priority = 15;
   return Priority;
}

static bool AGIPictureActorStateEqual(const stcAGIActorState *Left,
                                       const stcAGIActorState *Right)
{
   return Left->X == Right->X && Left->Y == Right->Y &&
      Left->View == Right->View && Left->Loop == Right->Loop &&
      Left->Cel == Right->Cel && Left->Priority == Right->Priority &&
      (Left->Flags & 0x82) == (Right->Flags & 0x82);
}

static void AGIPictureReadActorState(uint8_t Object, stcAGIActorState *State)
{
   State->X = AGIPicActorTable[Object];
   State->Y = AGIPicActorTable[20 + Object];
   State->View = AGIPicActorTable[40 + Object];
   State->Loop = AGIPicActorTable[60 + Object];
   State->Cel = AGIPicActorTable[80 + Object];
   State->Priority = AGIPicActorTable[100 + Object];
   State->Flags = AGIPicActorTable[140 + Object] & 0x82;
   State->Width = 0;
   State->Height = 0;
}

FLASHMEM static bool AGIPictureResolveActorCel(uint32_t GBC1Root, uint8_t Object,
                                      stcAGIActorCel *Actor, uint8_t *Error)
{
   stcAGIActorState State;
   AGIPictureReadActorState(Object, &State);
   memset(Actor, 0, sizeof(*Actor));
   Actor->State = State;
   Actor->Drawn = (State.Flags & 2) != 0;
   if (!Actor->Drawn) return true;
   if ((State.Flags & 0x80) &&
       (State.Priority < 4 || State.Priority > 15))
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   Actor->EffectivePriority = (State.Flags & 0x80) ? State.Priority :
      AGIPictureAutomaticPriority(State.Y);

   uint16_t IndexOffset = (uint16_t)State.View * 3u;
   uint32_t ViewRaw = (uint32_t)AGIPicGBC1IndexCache[IndexOffset] |
      ((uint32_t)AGIPicGBC1IndexCache[IndexOffset + 1] << 8) |
      ((uint32_t)AGIPicGBC1IndexCache[IndexOffset + 2] << 16);
   if (ViewRaw == 0xFFFFFFu)
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   uint8_t Header[20];
   if (!AGIPictureReadRawBytes(ViewRaw, Header, sizeof(Header), Error))
      return false;
   uint8_t LoopCount = Header[6];
   uint16_t CelCount = Header[8] | ((uint16_t)Header[9] << 8);
   uint16_t LoopDirectory = Header[10] | ((uint16_t)Header[11] << 8);
   uint16_t CelDirectory = Header[12] | ((uint16_t)Header[13] << 8);
   uint16_t PayloadStart = Header[14] | ((uint16_t)Header[15] << 8);
   uint16_t Description = Header[16] | ((uint16_t)Header[17] << 8);
   uint16_t Total = Header[18] | ((uint16_t)Header[19] << 8);
   if (memcmp(Header, "GBC1", 4) != 0 || Header[4] != 1 || Header[5] != 7 ||
       !LoopCount || Header[7] != 8 || !CelCount ||
       LoopDirectory != 20 ||
       CelDirectory != LoopDirectory + (uint16_t)LoopCount * 3u ||
       PayloadStart != CelDirectory + CelCount * 8u ||
       Total <= PayloadStart || (Description &&
       (Description < PayloadStart || Description >= Total)) ||
       State.Loop >= LoopCount || !AGIPictureRawSpanValid(ViewRaw, Total))
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   stcAGIGBC1ViewCache *Prepared;
   if (!AGIPictureCacheGBC1View(ViewRaw, Total, &Prepared, Error) ||
       !AGIPictureValidateGBC1View(ViewRaw, Total, Prepared, Error))
      return false;

   uint8_t Loop[3];
   if (!AGIPictureReadRawBytes(ViewRaw + LoopDirectory +
                               (uint16_t)State.Loop * 3u,
                               Loop, sizeof(Loop), Error)) return false;
   uint16_t FirstCel = Loop[0] | ((uint16_t)Loop[1] << 8);
   if (!Loop[2] || FirstCel >= CelCount || FirstCel + Loop[2] > CelCount ||
       State.Cel >= Loop[2])
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   uint16_t CelIndex = FirstCel + State.Cel;
   uint8_t Cel[8];
   if (!AGIPictureReadRawBytes(ViewRaw + CelDirectory + CelIndex * 8u,
                               Cel, sizeof(Cel), Error)) return false;
   uint16_t Payload = Cel[0] | ((uint16_t)Cel[1] << 8);
   uint8_t Width = Cel[2];
   uint8_t Height = Cel[3];
   if (!Width || !Height || (Cel[4] & ~1u) ||
       ((Cel[4] & 1) ? Cel[5] > 7 : Cel[5] != 0xFF) ||
       Cel[6] > 15 || Cel[7] || Payload < PayloadStart ||
       Payload > Total - 4)
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   uint8_t Exact[4];
   if (!AGIPictureReadRawBytes(ViewRaw + Payload, Exact, sizeof(Exact), Error))
      return false;
   uint8_t Columns = (Width + 3u) >> 2;
   uint8_t Rows = (Height + 7u) >> 3;
   uint16_t DirectoryBytes = (uint16_t)Columns * Rows * 2u;
   uint16_t DictionaryEnd = Description ? Description : Total;
   if (Exact[0] != 0x43 || Exact[1] != 1 || Exact[2] != Columns ||
       Exact[3] != Rows || Payload + 4u + DirectoryBytes > DictionaryEnd)
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   Actor->State.Width = Width;
   Actor->State.Height = Height;
   Actor->ViewRaw = ViewRaw;
   Actor->PayloadRaw = ViewRaw + Payload;
   Actor->ViewLength = Total;
   Actor->PayloadOffset = Payload;
   Actor->DictionaryEnd = DictionaryEnd;
   Actor->PatternDirectoryBytes = DirectoryBytes;
   Actor->Columns = Columns;
   Actor->Rows = Rows;
   Actor->Mirror = Cel[4] & 1;
   return true;
}

FLASHMEM static void AGIPictureMarkActorBounds(uint8_t Dirty[125],
                                      const stcAGIActorState *Actor)
{
   if (!(Actor->Flags & 2) || !Actor->Width || !Actor->Height) return;
   int16_t Left = Actor->X;
   int16_t Right = Left + Actor->Width - 1;
   int16_t Top = (int16_t)Actor->Y + 1 - Actor->Height;
   int16_t Bottom = Actor->Y;
   if (Right < 0 || Left >= 160 || Bottom < 0 || Top >= 168) return;
   if (Left < 0) Left = 0;
   if (Right > 159) Right = 159;
   if (Top < 0) Top = 0;
   if (Bottom > 167) Bottom = 167;
   uint8_t FirstX = Left >> 2;
   uint8_t LastX = Right >> 2;
   uint8_t FirstY = (Top + 16) >> 3;
   uint8_t LastY = (Bottom + 16) >> 3;
   for (uint8_t CellY = FirstY; CellY <= LastY; CellY++)
      for (uint8_t CellX = FirstX; CellX <= LastX; CellX++)
         AGIPictureDirtySet(Dirty, (uint16_t)CellY * 40u + CellX);
}

FLASHMEM static bool AGIPictureActorIntersectsCell(const stcAGIActorCel *Actor,
                                           uint16_t Cell)
{
   if (!Actor->Drawn || !Actor->State.Width || !Actor->State.Height)
      return false;
   int16_t CellLeft = (Cell % 40) * 4;
   int16_t CellRight = CellLeft + 3;
   int16_t CellTop = (Cell / 40) * 8 - 16;
   int16_t CellBottom = CellTop + 7;
   int16_t ActorLeft = Actor->State.X;
   int16_t ActorRight = ActorLeft + Actor->State.Width - 1;
   int16_t ActorTop = (int16_t)Actor->State.Y + 1 - Actor->State.Height;
   int16_t ActorBottom = Actor->State.Y;
   return ActorLeft <= CellRight && ActorRight >= CellLeft &&
      ActorTop <= CellBottom && ActorBottom >= CellTop;
}

FLASHMEM static bool AGIPictureLoadGBC1Pattern(uint32_t Raw, uint8_t **Pattern,
                                      uint8_t *Error)
{
   for (uint8_t Index = 0; Index < AGIPicGBC1PatternCacheEntries; Index++)
   {
      if (AGIPicGBC1PatternRaw[Index] == Raw)
      {
         *Pattern = AGIPicGBC1PatternCache[Index];
         return true;
      }
   }
   uint8_t Slot = AGIPicGBC1PatternNext++;
   if (AGIPicGBC1PatternNext >= AGIPicGBC1PatternCacheEntries)
      AGIPicGBC1PatternNext = 0;
   if (!AGIPictureReadRawBytes(Raw, AGIPicGBC1PatternCache[Slot],
                               AGIPicGBC1PatternBytes, Error)) return false;
   for (uint8_t Index = 0; Index < AGIPicGBC1PatternBytes; Index++)
   {
      uint8_t Packed = AGIPicGBC1PatternCache[Slot][Index];
      if ((Packed >> 4) == 12 || (Packed & 15) == 12)
      {
         *Error = AGIPicError_MalformedGBC1;
         return false;
      }
   }
   AGIPicGBC1PatternRaw[Slot] = Raw;
   *Pattern = AGIPicGBC1PatternCache[Slot];
   return true;
}

FLASHMEM static bool AGIPictureActorPixel(const stcAGIActorCel *Actor,
                                  uint8_t SourceX, uint8_t SourceY,
                                  uint8_t *Colour, bool *Opaque,
                                  uint8_t *Error)
{
   uint8_t AuthoredX = Actor->Mirror ?
      Actor->State.Width - 1u - SourceX : SourceX;
   uint8_t Column = AuthoredX >> 2;
   uint8_t Row = SourceY >> 3;
   if (Column >= Actor->Columns || Row >= Actor->Rows) return false;
   uint16_t Entry = (uint16_t)Row * Actor->Columns + Column;
   uint16_t Relative;
   if (!AGIPictureReadRaw16(Actor->PayloadRaw + 4u + Entry * 2u,
                            &Relative, Error)) return false;
   if (!Relative)
   {
      *Opaque = false;
      return true;
   }
   uint32_t Local = (uint32_t)Actor->PayloadOffset + Relative;
   uint32_t Minimum = (uint32_t)Actor->PayloadOffset + 4u +
      Actor->PatternDirectoryBytes;
   if (Local < Minimum || Local + AGIPicGBC1PatternBytes > Actor->DictionaryEnd)
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   uint8_t *Pattern;
   if (!AGIPictureLoadGBC1Pattern(Actor->ViewRaw + Local, &Pattern, Error))
      return false;
   uint8_t Pixel = (SourceY & 7u) * 4u + (AuthoredX & 3u);
   uint8_t Packed = Pattern[Pixel >> 1];
   uint8_t Value = (Pixel & 1) ? (Packed & 15) : (Packed >> 4);
   *Opaque = Value != 8;
   *Colour = Value;
   return true;
}

FLASHMEM static void AGIPictureUnpackRoomCell(uint16_t Cell, uint8_t Pixels[32])
{
   uint8_t Screen = AGIPicRoomScreen[Cell];
   uint8_t Palette[4] =
   {
      0, (uint8_t)(Screen >> 4), (uint8_t)(Screen & 15),
      (uint8_t)(AGIPicRoomColour[Cell] & 15)
   };
   for (uint8_t Row = 0; Row < 8; Row++)
   {
      uint8_t Packed = AGIPicRoomBitmap[Cell * 8u + Row];
      for (uint8_t Column = 0; Column < 4; Column++)
         Pixels[Row * 4u + Column] =
            Palette[(Packed >> (6u - Column * 2u)) & 3u];
   }
}

FLASHMEM static void AGIPicturePackRoomCell(uint8_t Pixels[32], uint8_t Output[10])
{
   uint8_t Counts[16] = {0};
   for (uint8_t Pixel = 0; Pixel < 32; Pixel++) Counts[Pixels[Pixel] & 15]++;
   uint8_t Palette[4] = {0, 0, 0, 0};
   for (uint8_t Slot = 1; Slot < 4; Slot++)
   {
      uint8_t Selected = 0;
      uint8_t SelectedCount = 0;
      for (uint8_t Colour = 1; Colour < 16; Colour++)
      {
         bool Used = false;
         for (uint8_t Prior = 1; Prior < Slot; Prior++)
            if (Palette[Prior] == Colour) Used = true;
         if (!Used && Counts[Colour] > SelectedCount)
         {
            Selected = Colour;
            SelectedCount = Counts[Colour];
         }
      }
      Palette[Slot] = Selected;
   }
   for (uint8_t Row = 0; Row < 8; Row++)
   {
      uint8_t Packed = 0;
      for (uint8_t Column = 0; Column < 4; Column++)
      {
         uint8_t Colour = Pixels[Row * 4u + Column];
         uint8_t Code = 0;
         if (Colour)
         {
            Code = 1;
            for (uint8_t Candidate = 1; Candidate < 4; Candidate++)
               if (Palette[Candidate] == Colour)
               {
                  Code = Candidate;
                  break;
               }
         }
         Packed |= Code << (6u - Column * 2u);
      }
      Output[Row] = Packed;
   }
   Output[8] = (Palette[1] << 4) | Palette[2];
   Output[9] = Palette[3];
}

FLASHMEM static bool AGIPictureComposeActorCell(uint16_t Cell, uint8_t ActorCount,
                                        uint8_t Output[10], bool *Occupied,
                                        uint8_t *Error)
{
   uint8_t Pixels[32];
   AGIPictureUnpackRoomCell(Cell, Pixels);
   bool Any = false;
   uint8_t CellX = (Cell % 40) * 4u;
   int16_t CellY = (int16_t)(Cell / 40) * 8 - 16;
   for (uint8_t Order = 0; Order < ActorCount; Order++)
   {
      stcAGIActorCel *Actor = &AGIPicCurrentActors[AGIPicActorOrder[Order]];
      if (!AGIPictureActorIntersectsCell(Actor, Cell)) continue;
      int16_t ActorTop = (int16_t)Actor->State.Y + 1 - Actor->State.Height;
      for (uint8_t Row = 0; Row < 8; Row++)
      {
         int16_t TargetY = CellY + Row;
         if (TargetY < 0 || TargetY >= 168 || TargetY < ActorTop ||
             TargetY > Actor->State.Y) continue;
         uint8_t SourceY = (uint8_t)(TargetY - ActorTop);
         for (uint8_t Column = 0; Column < 4; Column++)
         {
            uint16_t TargetX = CellX + Column;
            if (TargetX < Actor->State.X ||
                TargetX >= (uint16_t)Actor->State.X + Actor->State.Width)
               continue;
            uint8_t SourceX = TargetX - Actor->State.X;
            uint8_t Colour = 0;
            bool Opaque = false;
            if (!AGIPictureActorPixel(Actor, SourceX, SourceY, &Colour,
                                      &Opaque, Error)) return false;
            if (!Opaque || !AGIPictureRoomPriorityAllows((uint8_t)TargetX,
                                                          (uint8_t)TargetY,
                                                          Actor->EffectivePriority))
               continue;
            Pixels[Row * 4u + Column] = Colour;
            Any = true;
         }
      }
   }
   AGIPicturePackRoomCell(Pixels, Output);
   *Occupied = Any;
   return true;
}

FLASHMEM static bool AGIPictureBuildActorOrder(uint32_t GBC1Root, uint8_t FirstObject,
                                       uint8_t *ActorCount, uint8_t *Error)
{
   if (FirstObject > 1 || !AGIPicturePrepareGBC1Index(GBC1Root, Error))
   {
      *Error = AGIPicError_MalformedGBC1;
      return false;
   }
   if (!++AGIPicGBC1ViewCacheGeneration)
      AGIPicGBC1ViewCacheGeneration = 1;
   uint8_t Count = 0;
   for (uint8_t Object = 0; Object < AGIPicActorCount; Object++)
   {
      if (Object < FirstObject)
      {
         memset(&AGIPicCurrentActors[Object], 0,
                sizeof(AGIPicCurrentActors[Object]));
         AGIPictureReadActorState(Object,
                                  &AGIPicCurrentActors[Object].State);
         AGIPicCurrentActors[Object].State.Flags &= ~2u;
         continue;
      }
      if (!AGIPictureResolveActorCel(GBC1Root, Object,
                                     &AGIPicCurrentActors[Object], Error))
         return false;
      if (!AGIPicCurrentActors[Object].Drawn) continue;
      uint8_t Insert = Count;
      while (Insert)
      {
         uint8_t PriorObject = AGIPicActorOrder[Insert - 1];
         stcAGIActorCel *Prior = &AGIPicCurrentActors[PriorObject];
         stcAGIActorCel *Current = &AGIPicCurrentActors[Object];
         if (Prior->EffectivePriority < Current->EffectivePriority ||
             (Prior->EffectivePriority == Current->EffectivePriority &&
              PriorObject < Object)) break;
         AGIPicActorOrder[Insert] = PriorObject;
         Insert--;
      }
      AGIPicActorOrder[Insert] = Object;
      Count++;
   }
   *ActorCount = Count;
   return true;
}

FLASHMEM static bool AGIPictureBuildActorDirty(uint8_t FirstObject,
                                       uint8_t Dirty[125], uint8_t *Error)
{
   memcpy(Dirty, AGIPicRoomPendingDirty, 125);
   for (uint8_t Object = 0; Object < AGIPicActorCount; Object++)
   {
      stcAGIActorState *Current = &AGIPicCurrentActors[Object].State;
      bool Changed = !AGIPicActorStateValid ||
         !AGIPictureActorStateEqual(&AGIPicPriorActors[Object], Current);
      if (!Changed) continue;
      if (AGIPicActorStateValid)
         AGIPictureMarkActorBounds(Dirty, &AGIPicPriorActors[Object]);
      AGIPictureMarkActorBounds(Dirty, Current);
   }
   if (AGIPictureDirtyCount(Dirty) > AGIPicActorDirtyLimit)
   {
      *Error = AGIPicError_DirtyOverflow;
      return false;
   }
   return true;
}

FLASHMEM static bool AGIPictureBuildOccupiedCells(uint8_t ActorCount,
                                          uint8_t Occupied[125],
                                          uint8_t *Error)
{
   uint8_t Candidates[125];
   AGIPictureDirtyClear(Candidates);
   AGIPictureDirtyClear(Occupied);
   for (uint8_t Order = 0; Order < ActorCount; Order++)
      AGIPictureMarkActorBounds(Candidates,
         &AGIPicCurrentActors[AGIPicActorOrder[Order]].State);
   uint8_t CellBytes[10];
   for (uint16_t Cell = 0; Cell < 1000; Cell++)
   {
      if (AGIPicAbortRequested)
      {
         *Error = AGIPicError_DMATimeout;
         return false;
      }
      if (!AGIPictureDirtyGet(Candidates, Cell)) continue;
      bool HasActorPixel;
      if (!AGIPictureComposeActorCell(Cell, ActorCount, CellBytes,
                                      &HasActorPixel, Error)) return false;
      if (HasActorPixel) AGIPictureDirtySet(Occupied, Cell);
   }
   if (AGIPictureDirtyCount(Occupied) > AGIPicActorDirtyLimit)
   {
      *Error = AGIPicError_DirtyOverflow;
      return false;
   }
   return true;
}

FLASHMEM static bool AGIPictureStageActorPatch(const uint8_t Dirty[125],
                                       uint8_t ActorCount,
                                       uint16_t *Length, uint8_t *Error)
{
   uint16_t Cursor = 0;
   for (uint16_t Cell = 0; Cell < 1000;)
   {
      if (!AGIPictureDirtyGet(Dirty, Cell))
      {
         Cell++;
         continue;
      }
      uint16_t First = Cell;
      uint8_t Count = 1;
      while (Count < 40 && First + Count < 1000 &&
             (First + Count) / 40 == First / 40 &&
             AGIPictureDirtyGet(Dirty, First + Count)) Count++;
      if ((uint32_t)Cursor + 3u + (uint32_t)Count * 10u >
          AGIPicPatchMaximumBytes)
      {
         *Error = AGIPicError_DirtyOverflow;
         return false;
      }
      AGIPicPatchEncoded[Cursor++] = First;
      AGIPicPatchEncoded[Cursor++] = First >> 8;
      AGIPicPatchEncoded[Cursor++] = Count;
      for (uint8_t Index = 0; Index < Count; Index++)
      {
         bool Occupied;
         if (!AGIPictureComposeActorCell(First + Index, ActorCount,
                                         &AGIPicPatchEncoded[Cursor],
                                         &Occupied, Error)) return false;
         Cursor += 10;
      }
      Cell = First + Count;
   }
   *Length = Cursor;
   return true;
}

FLASHMEM static bool AGIPictureStageActorFallback(const uint8_t Occupied[125],
                                          uint16_t PatchLength,
                                          uint16_t *PriorOffset,
                                          uint8_t *PriorCount,
                                          uint16_t *DescriptorOffset,
                                          uint8_t *Error)
{
   uint16_t Count = AGIPictureDirtyCount(Occupied);
   if (Count > AGIPicActorDirtyLimit ||
       (uint32_t)PatchLength + Count * 12u + 180u > AGIPicPatchMaximumBytes)
   {
      *Error = AGIPicError_DirtyOverflow;
      return false;
   }
   uint16_t Cursor = PatchLength;
   *PriorOffset = Cursor;
   for (uint16_t Cell = 0; Cell < 1000; Cell++)
   {
      if (!AGIPictureDirtyGet(Occupied, Cell)) continue;
      AGIPicPatchEncoded[Cursor++] = Cell;
      AGIPicPatchEncoded[Cursor++] = Cell >> 8;
      memcpy(&AGIPicPatchEncoded[Cursor], &AGIPicRoomBitmap[Cell * 8u], 8);
      Cursor += 8;
      AGIPicPatchEncoded[Cursor++] = AGIPicRoomScreen[Cell];
      AGIPicPatchEncoded[Cursor++] = AGIPicRoomColour[Cell];
   }
   *PriorCount = (uint8_t)Count;
   *DescriptorOffset = Cursor;
   memset(&AGIPicPatchEncoded[Cursor], 0, 180);
   for (uint8_t Object = 0; Object < AGIPicActorCount; Object++)
   {
      stcAGIActorCel *Actor = &AGIPicCurrentActors[Object];
      if (!Actor->Drawn) continue;
      AGIPicPatchEncoded[Cursor + Object] = Actor->State.X;
      AGIPicPatchEncoded[Cursor + 20 + Object] = Actor->State.Y;
      AGIPicPatchEncoded[Cursor + 40 + Object] = Actor->EffectivePriority;
      AGIPicPatchEncoded[Cursor + 60 + Object] = Actor->State.Width;
      AGIPicPatchEncoded[Cursor + 80 + Object] = Actor->State.Height;
      AGIPicPatchEncoded[Cursor + 100 + Object] = Actor->Mirror | 0x80;
      AGIPicPatchEncoded[Cursor + 120 + Object] = Actor->PayloadRaw;
      AGIPicPatchEncoded[Cursor + 140 + Object] = Actor->PayloadRaw >> 8;
      AGIPicPatchEncoded[Cursor + 160 + Object] = Actor->PayloadRaw >> 16;
   }
   return true;
}

FLASHMEM static bool AGIPicturePublishActorFrame(uint16_t PatchLength,
                                         uint16_t PriorOffset,
                                         uint8_t PriorCount,
                                         uint16_t DescriptorOffset)
{
   uint16_t Cursor = 0;
   bool Started = false, Okay = true;
   // Hold /DMA from the first pixel run through the complete native compositor
   // transaction tail. The C64 cannot time out into fallback between planes;
   // terminal $A3 follows the one close that retires all fields at $5B10-$5B12.
   uint8_t Transaction[3] = {PriorCount, 0, 0};
   while (Okay && Cursor < PatchLength)
   {
      uint16_t Cell = AGIPicPatchEncoded[Cursor] |
         ((uint16_t)AGIPicPatchEncoded[Cursor + 1] << 8);
      uint8_t Count = AGIPicPatchEncoded[Cursor + 2];
      Cursor += 3;
      for (uint8_t Index = 0; Index < Count; Index++)
      {
         memcpy(&AGIPicPatchBitmap[Index * 8], &AGIPicPatchEncoded[Cursor], 8);
         Cursor += 8;
         AGIPicPatchScreen[Index] = AGIPicPatchEncoded[Cursor++];
         AGIPicPatchColour[Index] = AGIPicPatchEncoded[Cursor++];
      }
      Okay = AGIPictureDMAWriteSegment(&Started, 0x6000 + Cell * 8u,
                                        AGIPicPatchBitmap, Count * 8u) &&
         AGIPictureDMAWriteSegment(&Started, 0x5C00 + Cell,
                                   AGIPicPatchScreen, Count) &&
         AGIPictureDMAWriteSegment(&Started, 0xD800 + Cell,
                                   AGIPicPatchColour, Count);
   }
   if (Okay && PriorCount)
      Okay = AGIPictureDMAWriteSegment(&Started, 0xB480,
         &AGIPicPatchEncoded[PriorOffset], (uint16_t)PriorCount * 12u);
   if (Okay)
      Okay = AGIPictureDMAWriteSegment(&Started, 0x4ED5,
         &AGIPicPatchEncoded[DescriptorOffset], 180);
   if (Okay)
      Okay = AGIPictureDMAWriteSegment(&Started, 0x5B10, Transaction,
                                        sizeof(Transaction));
   bool Closed = AGIPictureCloseScatter(Started);
   return Okay && Closed;
}

FLASHMEM static bool AGIPictureActorFrame(uint32_t GBC1Root, uint16_t ObjectBase,
                                  uint8_t FirstObject, uint8_t Picture,
                                  uint8_t *Error)
{
   if (!AGIPicRoomValid || AGIPicRoomToken != Picture)
   {
      *Error = AGIPicError_PrefetchMiss;
      return false;
   }
   if (FirstObject > 1 || ObjectBase < 0x0200 ||
       ObjectBase > 0x10000u - AGIPicActorTableBytes ||
       !AGIPictureRawSpanValid(GBC1Root, AGIPicGBC1IndexBytes))
   {
      *Error = AGIPicError_OutOfBounds;
      return false;
   }
   if (DMA_State != DMA_S_DisableReady)
   {
      *Error = AGIPicError_DMAUnavailable;
      return false;
   }
   AGIPictureApplyVideoTiming();
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_DMA;
   if (!AGIPictureDMAReadRoomSegment(ObjectBase, AGIPicActorTable,
                                     AGIPicActorTableBytes))
   {
      *Error = AGIPicError_DMATimeout;
      return false;
   }

   uint8_t ActorCount;
   uint8_t Dirty[125];
   uint8_t Occupied[125];
   uint16_t PatchLength, PriorOffset, DescriptorOffset;
   uint8_t PriorCount;
   bool Okay = AGIPictureBuildActorOrder(GBC1Root, FirstObject,
                                          &ActorCount, Error) &&
      AGIPictureBuildActorDirty(FirstObject, Dirty, Error) &&
      AGIPictureBuildOccupiedCells(ActorCount, Occupied, Error) &&
      AGIPictureStageActorPatch(Dirty, ActorCount, &PatchLength, Error) &&
      AGIPictureStageActorFallback(Occupied, PatchLength, &PriorOffset,
                                    &PriorCount, &DescriptorOffset, Error);
   AGIPictureReleaseSource();
   if (!Okay) return false;
   if (!AGIPicturePublishActorFrame(PatchLength, PriorOffset, PriorCount,
                                     DescriptorOffset))
   {
      *Error = AGIPicError_DMATimeout;
      AGIPictureInvalidateRoomArt();
      return false;
   }
   for (uint8_t Object = 0; Object < AGIPicActorCount; Object++)
      AGIPicPriorActors[Object] = AGIPicCurrentActors[Object].State;
   AGIPicActorStateValid = true;
   AGIPictureDirtyClear(AGIPicRoomPendingDirty);
   return true;
}

static void AGIPictureResetSession()
{
   AGIPictureReleaseSource();
   AGIPictureReleasePicture();
   AGIPictureReleaseScene();
   AGIPictureInvalidateLivePicture();
   memset((void *)AGIPicRegisters, 0, sizeof(AGIPicRegisters));
   AGIPicUnlockStage = 0;
   AGIPicChallengeSeen = false;
   AGIPicChallengeResponsePending = false;
   AGIPicHoldHelperBank = false;
   AGIPicAbortRequested = false; AGIPicResetPending = false;
   AGIPicPendingCommand = AGIPicCmd_Acknowledge;
   AGIPicErrorMailbox = 0;
   AGIPicProtocol = 0;
   MHSPEScanValidated = false;
   MHSPEActive = false;
   AGIPicActive = false;
}

static void AGIPictureActivate(uint8_t Protocol, uint8_t Layout)
{
   MHSPEScanValidated = false;
   MHSPEActive = false;
   AGIPicLivePictureValid = false;
   AGIPicRoomValid = false;
   AGIPicActorStateValid = false;
   AGIPicLivePictureToken = AGIPicRoomToken = 0;
   for (uint8_t Index = AGIPicReg_Command - AGIPicReg_ID0; Index < sizeof(AGIPicRegisters); Index++) AGIPicRegisters[Index] = 0;
   AGIPicRegisters[AGIPicReg_ID0 - AGIPicReg_ID0] = 'A';
   AGIPicRegisters[AGIPicReg_ID1 - AGIPicReg_ID0] = 'G';
   AGIPicRegisters[AGIPicReg_ID2 - AGIPicReg_ID0] = 'I';
   AGIPicRegisters[AGIPicReg_ID3 - AGIPicReg_ID0] = '+';
   AGIPicRegisters[AGIPicReg_Version - AGIPicReg_ID0] = Protocol;
   AGIPicRegisters[AGIPicReg_Capabilities - AGIPicReg_ID0] =
      Protocol == AGIPicProtocolV3 ? AGIPicV3Capabilities : AGIPicV2Capabilities;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_Ready;
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   AGIPicProtocol = Protocol;
   AGIPicLayout = Layout;
   AGIPicPendingCommand = AGIPicCmd_Acknowledge;
   AGIPicUnlockStage = 0;
   AGIPicChallengeSeen = false;
   AGIPicHoldHelperBank = false;
   AGIPicAbortRequested = false;
   AGIPicErrorMailbox = 0;
   AGIPicActive = true;
}

static void MHSPowerEngineActivate(uint8_t Layout)
{
   AGIPictureActivate(AGIPicProtocolV3, Layout);
   MHSPEActive = true;
   AGIPicRegisters[AGIPicReg_ID0 - AGIPicReg_ID0] = 'M';
   AGIPicRegisters[AGIPicReg_ID1 - AGIPicReg_ID0] = 'P';
   AGIPicRegisters[AGIPicReg_ID2 - AGIPicReg_ID0] = 'E';
   AGIPicRegisters[AGIPicReg_ID3 - AGIPicReg_ID0] = '+';
   AGIPicRegisters[AGIPicReg_Version - AGIPicReg_ID0] =
      MHSPEProtocolVersion;
   AGIPicRegisters[AGIPicReg_Capabilities - AGIPicReg_ID0] =
      MHSPECapabilities;
}

void AGIPictureInit()
{
   memset((void *)AGIPicSlotOwned, 0, sizeof(AGIPicSlotOwned));
   AGIPicPictureSlots[0] = AGIPicPictureSlots[1] = AGIPicPictureSlots[2] = -1;
   AGIPicSourceSlot = -1;
   for (uint8_t Index = 0; Index < AGIPicMaximumSceneSlots; Index++)
      AGIPicSceneSlots[Index] = -1;
   AGIPicSceneSlotCount = 0;
   AGIPicPrefetchValid = AGIPicSceneValid = false;
   AGIPictureResetSession();
}

void AGIPictureBankChanged()
{
   if (!AGIPicActive)
   {
      AGIPicUnlockStage = 0;
      AGIPicChallengeSeen = false;
      AGIPicChallengeResponsePending = false;
   }
}

static void AGIPictureUnlockWrite(uint8_t Address, uint8_t Data,
                                  uint8_t HelperProtocol, uint8_t Layout)
{
   if (Layout == AGIPicLayout_EasyFlash) EZFlashRAM[Address] = Data;
   if (Address == AGIPicReg_Error && HelperProtocol == AGIPicProtocolV3 &&
       Data == AGIPicV3Challenge)
   {
      AGIPicChallengeSeen = true;
      AGIPicChallengeResponsePending = true;
      AGIPicUnlockStage = 0;
      return;
   }
   if (Address == AGIPicReg_ID0 && Data == 'A') AGIPicUnlockStage = 1;
   else if (Address == AGIPicReg_ID0 && Data == 'M' &&
            HelperProtocol == AGIPicProtocolV3 && AGIPicChallengeSeen)
      AGIPicUnlockStage = 0x11;
   else if (Address == AGIPicReg_ID1 && Data == 'G' && AGIPicUnlockStage == 1)
      AGIPicUnlockStage = 2;
   else if (Address == AGIPicReg_ID1 && Data == 'P' &&
            AGIPicUnlockStage == 0x11) AGIPicUnlockStage = 0x12;
   else if (Address == AGIPicReg_ID2 && Data == 'I' && AGIPicUnlockStage == 2)
      AGIPicUnlockStage = 3;
   else if (Address == AGIPicReg_ID2 && Data == 'E' &&
            AGIPicUnlockStage == 0x12) AGIPicUnlockStage = 0x13;
   else if (Address == AGIPicReg_ID3 && Data == '+' && AGIPicUnlockStage == 3)
      AGIPicUnlockStage = 4;
   else if (Address == AGIPicReg_ID3 && Data == '+' &&
            AGIPicUnlockStage == 0x13) AGIPicUnlockStage = 0x14;
   else if (Address == AGIPicReg_Version && Data == HelperProtocol &&
            AGIPicUnlockStage == 4 &&
            (HelperProtocol == AGIPicProtocolV2 || AGIPicChallengeSeen))
   {
      AGIPictureActivate(HelperProtocol, Layout);
      return;
   }
   else if (Address == AGIPicReg_Version &&
            Data == MHSPEProtocolVersion && AGIPicUnlockStage == 0x14 &&
            HelperProtocol == AGIPicProtocolV3 && AGIPicChallengeSeen)
   {
      MHSPowerEngineActivate(Layout);
      return;
   }
   else
   {
      AGIPicUnlockStage = 0;
      if (HelperProtocol == AGIPicProtocolV3)
      {
         AGIPicChallengeSeen = false;
         AGIPicChallengeResponsePending = false;
      }
   }
}

static void AGIPictureCommandWrite(uint8_t Command)
{
   uint8_t Status = AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0];
   if (Command == AGIPicCmd_Reset)
   {
      if (AGIPictureStatusBusy(Status)) AGIPicAbortRequested = true;
      else { AGIPicHoldHelperBank = false; AGIPicResetPending = true; }
      return;
   }
   if (AGIPictureStatusBusy(Status))
   {
      AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_Busy;
      return;
   }
   if (Command == AGIPicCmd_Acknowledge)
   {
      AGIPicPendingCommand = AGIPicCmd_Acknowledge;
      AGIPicAbortRequested = false;
      AGIPicHoldHelperBank = false;
      AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
      AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] = AGIPicStatus_Ready;
      return;
   }
   bool V2 = Command >= AGIPicCmd_V2DecodeDMA && Command <= AGIPicCmd_V2DMAProbe;
   bool V3 = Command == AGIPicCmd_V3DecodeDMA ||
      Command == AGIPicCmd_V3PrefetchPicture ||
      Command == AGIPicCmd_V3CommitPrefetch ||
      Command == AGIPicCmd_V3PatchDMA || Command == AGIPicCmd_V3PrefetchScene ||
      Command == AGIPicCmd_V3RoomSeed || Command == AGIPicCmd_V3ActorFrame;
   bool MPE = Command == AGIPicCmd_MPEQuery ||
      Command == AGIPicCmd_MPEPowerVM || Command == AGIPicCmd_MPEAGIScan ||
      Command == AGIPicCmd_MPEPriorityLine;
   if (!V2 && !(V3 && AGIPicProtocol == AGIPicProtocolV3) &&
       !(MPE && AGIPicProtocol == AGIPicProtocolV3 && MHSPEActive))
   {
      AGIPictureSetError(AGIPicError_BadCommand);
      return;
   }
   AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_None;
   bool KeepArgument = Command == AGIPicCmd_V3PatchDMA ||
      Command == AGIPicCmd_V3RoomSeed || Command == AGIPicCmd_V3ActorFrame ||
      Command == AGIPicCmd_MPEPriorityLine;
   AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] =
      KeepArgument ?
         AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] : 0;
   AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] =
      KeepArgument ?
         AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] : 0;
   AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0] =
      (Command == AGIPicCmd_V2DMAProbe || Command == AGIPicCmd_V3CommitPrefetch ||
       Command == AGIPicCmd_V3PatchDMA || Command == AGIPicCmd_V3RoomSeed ||
       Command == AGIPicCmd_V3ActorFrame || Command == AGIPicCmd_MPEPowerVM ||
       Command == AGIPicCmd_MPEAGIScan) ?
         AGIPicStatus_DMA : AGIPicStatus_Decoding;
   // Picture/scene prefetch are posted work: the C64 gate restores its normal
   // bank immediately and continues. Every synchronous command waits in its
   // helper bank, so retain the original bank-write lock for those paths.
   AGIPicHoldHelperBank = Command != AGIPicCmd_V3PrefetchPicture &&
      Command != AGIPicCmd_V3PrefetchScene;
   AGIPicPendingCommand = Command;
}

bool AGIPictureIO2Hndlr(uint8_t Address, bool R_Wn,
                        uint8_t HelperProtocol, uint8_t Layout)
{
   // An activated session remains available when the game returns to its
   // matching helper bank, but must never shadow EasyFlash IO2 RAM (or the
   // floating Magic Desk IO2 range) in any other bank or cartridge layout.
   if (AGIPicActive &&
       (HelperProtocol != AGIPicProtocol || Layout != AGIPicLayout))
      return false;
   if (Address < AGIPicReg_ID0)
   {
      if (!AGIPicActive)
      {
         AGIPicUnlockStage = 0;
         AGIPicChallengeSeen = false;
         AGIPicChallengeResponsePending = false;
      }
      return false;
   }
   if (!AGIPicActive)
   {
      if (!HelperProtocol)
      {
         AGIPicUnlockStage = 0;
         AGIPicChallengeSeen = false;
         AGIPicChallengeResponsePending = false;
         return false;
      }
      bool UnlockAddress = HelperProtocol == AGIPicProtocolV2 ?
         Address <= AGIPicReg_Version :
         (Address == AGIPicReg_Error ||
          (AGIPicChallengeSeen && Address <= AGIPicReg_Version));
      if (!UnlockAddress)
      {
         AGIPicUnlockStage = 0;
         AGIPicChallengeSeen = false;
         AGIPicChallengeResponsePending = false;
         return false;
      }
      if (R_Wn)
      {
         if (HelperProtocol == AGIPicProtocolV3)
         {
            AGIPicUnlockStage = 0;
            AGIPicChallengeSeen = false;
            AGIPicChallengeResponsePending = false;
         }
         return false;
      }
      uint8_t Data = DataPortWaitRead();
      TraceLogAddValidData(Data);
      if (HelperProtocol == AGIPicProtocolV3 && Address == AGIPicReg_Error &&
          Data != AGIPicV3Challenge)
      {
         if (Layout == AGIPicLayout_EasyFlash) EZFlashRAM[Address] = Data;
         AGIPicUnlockStage = 0;
         AGIPicChallengeSeen = false;
         AGIPicChallengeResponsePending = false;
         return true;
      }
      AGIPictureUnlockWrite(Address, Data, HelperProtocol, Layout);
      return true;
   }

   uint8_t Register = Address - AGIPicReg_ID0;
   if (R_Wn)
   {
      if (Address == AGIPicReg_Error && AGIPicProtocol == AGIPicProtocolV3 &&
          AGIPicChallengeResponsePending)
      {
         DataPortWriteWaitLog(AGIPicV3Response);
         AGIPicChallengeResponsePending = false;
         return true;
      }
      DataPortWriteWaitLog(AGIPicRegisters[Register]);
      return true;
   }
   uint8_t Data = DataPortWaitRead();
   TraceLogAddValidData(Data);
   if (Address == AGIPicReg_Error && AGIPicProtocol == AGIPicProtocolV3)
   {
      uint8_t Status = AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0];
      if (Data == AGIPicV3Challenge)
         AGIPicChallengeResponsePending = true;
      else if (!AGIPictureStatusBusy(Status))
      {
         // Active v3 reuses $DFFB as the command-specific byte preceding the
         // normal argument pair. Keep non-challenge writes separate from the
         // public error byte so posting cannot lose a priority format or
         // object-table low address.
         AGIPicErrorMailbox = Data;
      }
      else AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_Busy;
   }
   else if (Address == AGIPicReg_Command) AGIPictureCommandWrite(Data);
   else if ((Address >= AGIPicReg_Source0 && Address <= AGIPicReg_Source2) ||
            (Address >= AGIPicReg_Argument0 && Address <= AGIPicReg_Token))
   {
      uint8_t Status = AGIPicRegisters[AGIPicReg_Status - AGIPicReg_ID0];
      if (!AGIPictureStatusBusy(Status)) AGIPicRegisters[Register] = Data;
      else AGIPicRegisters[AGIPicReg_Error - AGIPicReg_ID0] = AGIPicError_Busy;
   }
   return true;
}

bool AGIPictureBusy()
{
   return AGIPicActive && AGIPicHoldHelperBank;
}

FLASHMEM void AGIPicturePollingHndlr()
{
   if (AGIPicResetPending) { AGIPictureResetSession(); return; }
   if (!AGIPicActive || !AGIPicPendingCommand) return;
   uint8_t Command = AGIPicPendingCommand;
   AGIPicPendingCommand = AGIPicCmd_Acknowledge;
   if (AGIPicAbortRequested)
   {
      AGIPictureAbort();
      return;
   }
   uint32_t StartMS = millis();
   uint32_t Raw = AGIPictureMailboxRaw();
   uint8_t Token = AGIPicRegisters[AGIPicReg_Token - AGIPicReg_ID0];
   uint16_t Argument = AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] |
      ((uint16_t)AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0] << 8);
   uint8_t Error = AGIPicError_None;
   // Complete VIEW storage is linker-reserved EXTMEM, so the first posted
   // picture performs no heap allocation or 1.31 MiB zero fill. Generic
   // picture and priority reads bypass this actor-only cache. Boards without
   // enough PSRAM retain exact bounded cartridge reads plus the 128-pattern
   // cache. No picture, patch, prefetch, or actor feature is disabled here.

   if (Command == AGIPicCmd_MPEQuery)
   {
      if (!MHSPEActive || !MHSPowerEngineQuery(Token))
         AGIPictureSetError(AGIPicError_InvalidPowerTask);
      return;
   }
   if (Command == AGIPicCmd_MPEPowerVM)
   {
      uint16_t OutputLength = 0;
      if (!MHSPEActive ||
          !MHSPowerEngineExecuteVM(Raw, &OutputLength, &Error))
      {
         AGIPictureSetError(Error ? Error : AGIPicError_PowerVMFault);
         return;
      }
      MHSPowerEngineSetResultTag('P', 'V', 'M', OutputLength,
                                 OutputLength >> 8, 0x0F,
                                 AGIPicStatus_MPEPowerVMDone);
      return;
   }
   if (Command == AGIPicCmd_MPEAGIScan)
   {
      uint16_t Continuation = 0;
      uint8_t Effects = 0;
      if (!MHSPEActive ||
          !MHSPowerEngineAGIScan(Raw, &Continuation, &Effects, &Error))
      {
         // The scan path performs input DMA only and closes it before every
         // return. Defensively force cleanup before publishing any terminal
         // error if an underlying DMA timeout left a non-ready state.
         if (DMA_State != DMA_S_DisableReady) CloseDMA();
         MHSPowerEngineSetScanError(Error ? Error :
                                    AGIPicError_InvalidScanTask);
         return;
      }
      if (DMA_State != DMA_S_DisableReady)
      {
         CloseDMA();
         MHSPowerEngineSetScanError(AGIPicError_DMATimeout);
         return;
      }
      MHSPowerEngineSetScanResult(Continuation, Effects, Token);
      return;
   }
   if (Command == AGIPicCmd_MPEPriorityLine)
   {
      uint8_t Result = 0;
      if (!MHSPowerEnginePriorityLine((uint8_t)Raw, (uint8_t)(Raw >> 8),
                                     (uint8_t)(Raw >> 16),
                                     AGIPicErrorMailbox, Argument,
                                     AGIPicRegisters[AGIPicReg_MachineFlags -
                                                     AGIPicReg_ID0],
                                     Token, &Result))
      {
         MHSPowerEngineSetPriorityLineError();
         return;
      }
      MHSPowerEngineSetPriorityLineResult(Result, Token);
      return;
   }

   if (Command == AGIPicCmd_V2DMAProbe)
   {
      AGIPictureDMAProbe(StartMS);
      return;
   }
   if (Command == AGIPicCmd_V2DecodeDMA || Command == AGIPicCmd_V2DecodeOnly)
   {
      if (!AGIPictureDecodeV2(Raw, &Error))
      {
         AGIPictureSetError(Error);
         return;
      }
      if (Command == AGIPicCmd_V2DecodeOnly)
      {
         AGIPictureReleasePicture();
         AGIPictureSetDone(AGIPicStatus_V2DoneDecodeOnly, StartMS);
         return;
      }
      if (!AGIPictureDMADecodedPicture())
      {
         AGIPictureReleasePicture();
         AGIPictureSetError(AGIPicError_DMATimeout);
         return;
      }
      AGIPictureInvalidateLivePicture();
      AGIPictureReleasePicture();
      AGIPictureSetDone(AGIPicStatus_V2DoneDMA, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3DecodeDMA)
   {
      if (!AGIPictureDecodeV3(Raw, Token, &Error))
      {
         AGIPictureSetError(Error);
         return;
      }
      if (!AGIPictureDMADecodedPicture())
      {
         AGIPictureReleasePicture();
         AGIPictureSetError(AGIPicError_DMATimeout);
         return;
      }
      AGIPictureCommitLivePicture();
      AGIPictureReleasePicture();
      AGIPictureSetDone(AGIPicStatus_V3DonePicture, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3PrefetchPicture)
   {
      uint32_t IndexRaw = Raw + (uint32_t)Token * 8u;
      uint8_t DescriptorBytes[3] = {0, 0, 0};
      if (!AGIPictureRawSpanValid(IndexRaw, 8)) Error = AGIPicError_OutOfBounds;
      else for (uint8_t Index = 0; Index < 3 && !Error; Index++)
         AGIPictureReadRaw(IndexRaw + Index, &DescriptorBytes[Index], &Error);
      uint32_t DescriptorRaw = (uint32_t)DescriptorBytes[0] |
         ((uint32_t)DescriptorBytes[1] << 8) |
         ((uint32_t)DescriptorBytes[2] << 16);
      if (Error || !AGIPictureDecodeV3(DescriptorRaw, Token, &Error))
      {
         AGIPictureSetError(Error ? Error : AGIPicError_BadDescriptor);
         return;
      }
      AGIPicPrefetchValid = true;
      AGIPictureSetDone(AGIPicStatus_V3PictureReady, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3CommitPrefetch)
   {
      if (!AGIPicPrefetchValid || AGIPicPicture.DescriptorRaw != Raw)
      {
         AGIPictureSetError(AGIPicError_PrefetchMiss);
         return;
      }
      if (!AGIPictureDMADecodedPicture())
      {
         AGIPictureReleasePicture();
         AGIPictureSetError(AGIPicError_DMATimeout);
         return;
      }
      AGIPictureCommitLivePicture();
      AGIPictureReleasePicture();
      AGIPictureSetDone(AGIPicStatus_V3PrefetchDone, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3PatchDMA)
   {
      if (!AGIPictureDMAPatch(Raw, Argument, Token, &Error))
      {
         AGIPictureSetError(Error ? Error : AGIPicError_MalformedPatch);
         return;
      }
      AGIPictureSetDone(AGIPicStatus_V3DonePatch, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3PrefetchScene)
   {
      if (!AGIPicturePrefetchScene(Raw, Token, &Error))
      {
         AGIPictureSetError(Error ? Error : AGIPicError_PrefetchMiss);
         return;
      }
      AGIPictureSetDone(AGIPicStatus_V3SceneReady, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3RoomSeed)
   {
      if (!AGIPictureSeedRoom(Raw, Token, AGIPicErrorMailbox,
                              Argument, &Error))
      {
         AGIPictureSetError(Error ? Error : AGIPicError_PrefetchMiss);
         return;
      }
      AGIPictureSetDone(AGIPicStatus_V3RoomSeeded, StartMS);
      return;
   }
   if (Command == AGIPicCmd_V3ActorFrame)
   {
      uint16_t ObjectBase = AGIPicErrorMailbox |
         ((uint16_t)AGIPicRegisters[AGIPicReg_Argument0 - AGIPicReg_ID0] << 8);
      uint8_t FirstObject =
         AGIPicRegisters[AGIPicReg_Argument1 - AGIPicReg_ID0];
      if (!AGIPictureActorFrame(Raw, ObjectBase, FirstObject, Token, &Error))
      {
         // A native fallback changes pixels outside Teensy's retained state.
         // Disable the service until the next explicit room seed rather than
         // accepting a later frame against an uncertain framebuffer.
         AGIPictureInvalidateRoomArt();
         AGIPictureSetError(Error ? Error : AGIPicError_MalformedGBC1);
         return;
      }
      AGIPictureSetDone(AGIPicStatus_V3DoneActorFrame, StartMS);
      return;
   }
   AGIPictureSetError(AGIPicError_BadCommand);
}

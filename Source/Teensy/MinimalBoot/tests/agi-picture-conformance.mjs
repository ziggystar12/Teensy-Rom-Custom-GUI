#!/usr/bin/env node

// Host conformance checks for the AGI+2/AGI+3 MinimalBoot extension. Hardware
// DMA is still proven on a real Fab0.4 board; this test locks the mailbox,
// descriptor, bounded codecs, priority layout, paging, and scatter contracts.

import assert from "node:assert/strict";
import fs from "node:fs";

const read = (relative) => fs.readFileSync(new URL(relative, import.meta.url), "utf8");
const firmware = read("../Common/IO_Handlers/IOH_AGIPicture.c");
const easyFlash = read("../Common/IO_Handlers/IOH_EasyFlash.c");
const magicDesk2 = read("../Common/IO_Handlers/IOH_MagicDesk2.c");
const dma = read("../Common/DMAControl_Minimal.h");
const config = read("../Min_TeensyROM.h");

const mailbox = new Map([
  ["AGIPicReg_ID0", "0xF0"], ["AGIPicReg_ID1", "0xF1"],
  ["AGIPicReg_ID2", "0xF2"], ["AGIPicReg_ID3", "0xF3"],
  ["AGIPicReg_Version", "0xF4"], ["AGIPicReg_Capabilities", "0xF5"],
  ["AGIPicReg_Command", "0xF6"], ["AGIPicReg_Status", "0xF7"],
  ["AGIPicReg_Source0", "0xF8"], ["AGIPicReg_Source1", "0xF9"],
  ["AGIPicReg_Source2", "0xFA"], ["AGIPicReg_Error", "0xFB"],
  ["AGIPicReg_Argument0", "0xFC"], ["AGIPicReg_Argument1", "0xFD"],
  ["AGIPicReg_MachineFlags", "0xFE"], ["AGIPicReg_Token", "0xFF"]
]);
for (const [name, value] of mailbox) {
  assert.match(firmware, new RegExp(`\\b${name}\\s*=\\s*${value}\\b`));
}

for (const [name, value] of Object.entries({
  AGIPicCmd_V2DecodeDMA: "0x01", AGIPicCmd_V2DecodeOnly: "0x02",
  AGIPicCmd_V2DMAProbe: "0x03", AGIPicCmd_V3DecodeDMA: "0x10",
  AGIPicCmd_V3PrefetchPicture: "0x11", AGIPicCmd_V3CommitPrefetch: "0x12",
  AGIPicCmd_V3PatchDMA: "0x20", AGIPicCmd_V3PrefetchScene: "0x21",
  AGIPicCmd_V3RoomSeed: "0x22", AGIPicCmd_V3ActorFrame: "0x23",
  AGIPicStatus_V2DoneDMA: "0x80", AGIPicStatus_V2DoneDecodeOnly: "0x81",
  AGIPicStatus_V2DoneDMAProbe: "0x82", AGIPicStatus_V3DonePicture: "0x90",
  AGIPicStatus_V3PictureReady: "0x91", AGIPicStatus_V3PrefetchDone: "0x92",
  AGIPicStatus_V3DonePatch: "0xA0", AGIPicStatus_V3SceneReady: "0xA1",
  AGIPicStatus_V3RoomSeeded: "0xA2", AGIPicStatus_V3DoneActorFrame: "0xA3"
})) assert.match(firmware, new RegExp(`\\b${name}\\s*=\\s*${value}\\b`));

assert.match(firmware, /AGIPicProtocolV2\s*=\s*2/);
assert.match(firmware, /AGIPicProtocolV3\s*=\s*3/);
assert.match(firmware, /AGIPicV2Capabilities\s*=\s*0x0F/);
assert.match(firmware, /AGIPicV3Capabilities\s*=\s*0xFF/);
assert.match(firmware, /AGIPicV3Challenge\s*=\s*0x3C/);
assert.match(firmware, /AGIPicV3Response\s*=\s*0xC3/);
assert.match(firmware, /Address == AGIPicReg_Error[^]*?HelperProtocol == AGIPicProtocolV3[^]*?Data == AGIPicV3Challenge/);
assert.match(firmware, /for \(uint8_t Index = AGIPicReg_Command - AGIPicReg_ID0; Index < sizeof\(AGIPicRegisters\); Index\+\+\)[^]*?if \(Data == AGIPicV3Challenge\)[^]*?AGIPicChallengeResponsePending\s*=\s*true/);
assert.match(firmware, /Address == AGIPicReg_Error[^]*?AGIPicChallengeResponsePending[^]*?DataPortWriteWaitLog\(AGIPicV3Response\)[^]*?AGIPicChallengeResponsePending\s*=\s*false/);
assert.match(firmware, /HelperProtocol == AGIPicProtocolV2 \?[^]*?AGIPicChallengeSeen && Address <= AGIPicReg_Version/);
assert.match(firmware, /AGIPicActive &&[^]*?HelperProtocol != AGIPicProtocol[^]*?Layout != AGIPicLayout[^]*?return false/);
assert.match(firmware, /memcmp\(Descriptor, "AGP3", 4\)[^]*?Descriptor\[4\] != 16[^]*?Descriptor\[15\] != Checksum/);
assert.match(firmware, /Descriptor\[7\] != ExpectedToken/);
assert.match(firmware, /AGIPicPriorityLength\s*=\s*13440/);
assert.match(firmware, /AGIPicCompactPriorityMaximum\s*=\s*0x3300/);
assert.match(firmware, /Header\[0\] != 0xA3[^]*?Header\[1\] != 160[^]*?Header\[2\] != 168/);
assert.match(firmware, /AGIPictureOutputSet\(Output, Row, Address\)[^]*?AGIPictureOutputSet\(Output, 168 \+ Row, Address >> 8\)/);
assert.match(firmware, /Copyright \(c\) 2005-2017 Magnus Lind/);
assert.match(firmware, /AGIPictureExoGenerateTable\(&Context, 0, 16\)[^]*?16, 16[^]*?32, 16[^]*?48, 4/);
assert.match(firmware, /DestinationHi[^]*?DestinationLo[^]*?ExpectedDestination/);

assert.match(firmware, /AGIPicLayout == AGIPicLayout_MagicDesk2[^]*?NumCrtChips \* 0x4000u/);
assert.match(firmware, /BankDecode\[Bank\]\[Half\]/);
assert.match(firmware, /CrtChips\[Bank\]\.ChipROM[^]*?Half \* 0x2000u/);
assert.match(firmware, /SwapBuffers\[Slot\]\.Offset == Tagged/);
assert.match(firmware, /myFile\.seek\(Tag & ~SwapSeekAddrMask\)[^]*?Index < 8192[^]*?myFile\.read\(\)/);
assert.match(firmware, /AGIPictureSwapBufferWillOverwrite[^]*?AGIPictureReleasePicture[^]*?AGIPictureReleaseScene/);

assert.match(easyFlash, /CurrentEasyFlashBank == 62 \? 2[^]*?CurrentEasyFlashBank == 59 \? 3/);
function writeMagicDesk2(state, value, numCrtChips) {
  if (value & 0x80) return { ...state, enabled: false };
  const bank = value & 0x7f;
  if (bank >= numCrtChips) return state;
  return { enabled: true, bank };
}

const magicDesk2Disabled = writeMagicDesk2({ enabled: true, bank: 3 }, 0x80, 64);
assert.deepEqual(magicDesk2Disabled, { enabled: false, bank: 3 });
const magicDesk2Invalid = writeMagicDesk2(magicDesk2Disabled, 0x7f, 64);
assert.equal(magicDesk2Invalid, magicDesk2Disabled);
assert.equal(magicDesk2Invalid.enabled, false);
assert.deepEqual(writeMagicDesk2(magicDesk2Disabled, 59, 64), { enabled: true, bank: 59 });

assert.match(magicDesk2, /if \(Data & 0x80\)[^]*?MagicDesk2Enabled = false;[^]*?SetGameDeassert;[^]*?SetExROMDeassert;[^]*?return;/);
assert.match(magicDesk2, /if \(\(Data & 0x7f\) >= NumCrtChips\) return;[^]*?Data &= 0x7f;[^]*?MagicDesk2Enabled = true;[^]*?SetGameAssert;[^]*?SetExROMAssert;/);
assert.match(magicDesk2, /MagicDesk2Enabled && CurrentMagicDesk2Bank == 62 \? 2[^]*?MagicDesk2Enabled && CurrentMagicDesk2Bank == 59 \? 3/);
assert.match(magicDesk2, /AGIPictureIO2Hndlr\(Address, R_Wn/);
assert.match(easyFlash, /AGIPictureSwapBufferWillOverwrite\(NextSwapBuffNum\)/);
assert.match(easyFlash, /AGIPictureSwapBufferIsOwned\(BuffNum\)/);
assert.match(magicDesk2, /AGIPictureSwapBufferWillOverwrite\(NextSwapBuffNum\)/);
assert.match(firmware, /volatile bool AGIPicSlotOwned/);
assert.match(firmware, /bool AGIPictureSwapBufferIsOwned[^]*?Slot < Num8kSwapBuffers && AGIPicSlotOwned\[Slot\]/);
assert.match(firmware, /static int8_t AGIPictureBorrowSlot[^]*?__get_primask\(\)[^]*?__disable_irq\(\)[^]*?AGIPicSlotOwned\[Slot\] = true[^]*?SwapBuffers\[Slot\]\.Offset = 0[^]*?__set_primask\(InterruptMask\)/);

assert.match(dma,
  /AGIContinueDMA[^]*?DMA_State != DMA_S_TransferComplete[^]*?DMA_State = DMA_S_TransferExecuting[^]*?AGIDMAWaitForState\(DMA_S_TransferComplete\)[^]*?delayMicroseconds\(2\)[^]*?return true/,
  "held scatter segments retain the established settling interval");
assert.match(firmware, /AGIPictureDMAWriteSegment[^]*?PerformDMA\(false[^]*?AGIContinueDMA\(false/);
assert.match(firmware, /AGIPictureDMADecodedPicture[^]*?0x6000[^]*?0x5C00[^]*?0x8000[^]*?0xD800/);
assert.match(firmware, /AGIPictureDMAWritePatchSegment[^]*?PerformDMA\(false[^]*?CloseDMA\(\)/);
assert.match(firmware, /AGIPictureDMAPatch[^]*?AGIPictureDMAWritePatchSegment\(0x6000 \+ Cell \* 8[^]*?AGIPictureDMAWritePatchSegment\(0x5C00 \+ Cell[^]*?AGIPictureDMAWritePatchSegment\(0xD800 \+ Cell/);
assert.match(dma, /AGIDMAEmergencyRelease[^]*?SetRWInput[^]*?SetAddrPortDirIn[^]*?SetDataPortDirIn[^]*?SetDMADeassert/);
assert.match(config, /#define FeatAGIPictureDMA[^]*?#ifndef FeatAGIPictureDMA[^]*?#define FeatTCPListen/);

// Busy reset is cooperative: the IO ISR only latches the request. The poller
// owns cache teardown and publishes terminal $EA before releasing the bank
// interlock, so the C64 can observe and acknowledge a safe fallback boundary.
assert.match(firmware, /static volatile bool AGIPicAbortRequested/);
const commandWrite = firmware.slice(
  firmware.indexOf("static void AGIPictureCommandWrite"),
  firmware.indexOf("bool AGIPictureIO2Hndlr")
);
assert.ok(commandWrite.indexOf("if (Command == AGIPicCmd_Reset)") <
  commandWrite.indexOf("if (AGIPictureStatusBusy(Status))"));
assert.match(commandWrite, /Command == AGIPicCmd_Reset[^]*?AGIPictureStatusBusy\(Status\)[^]*?AGIPicAbortRequested = true[^]*?else[^]*?AGIPicResetPending = true/);
assert.match(commandWrite, /Command == AGIPicCmd_Acknowledge[^]*?AGIPicAbortRequested = false[^]*?AGIPicHoldHelperBank = false[^]*?AGIPicStatus_Ready/);
assert.match(firmware, /static void AGIPictureResetSession\(\)[^]*?AGIPicAbortRequested = false; AGIPicResetPending = false/);
assert.match(firmware, /static void AGIPictureActivate[^]*?AGIPicAbortRequested = false/);
assert.match(firmware, /static bool AGIPictureLoadTaggedPage[^]*?Index & 0x3F[^]*?AGIPicAbortRequested[^]*?return false/);
assert.match(firmware, /static bool AGIPictureResolveRawPage[^]*?AGIPicAbortRequested \? AGIPicError_DMATimeout[^]*?AGIPicError_SourceIO/);
assert.match(firmware, /static bool AGIPictureReadRaw[^]*?AGIPicAbortRequested[^]*?AGIPicError_DMATimeout[^]*?return false/);
assert.match(firmware, /static bool AGIPictureOutputSet[^]*?AGIPicAbortRequested[^]*?return false/);
assert.match(firmware, /static void AGIPictureSetError[^]*?AGIPicAbortRequested[^]*?AGIPictureAbort\(\)/);
assert.match(firmware, /static void AGIPictureSetDone[^]*?AGIPicAbortRequested[^]*?AGIPictureAbort\(\)/);
const abortFunction = firmware.slice(
  firmware.lastIndexOf("static void AGIPictureAbort()"),
  firmware.indexOf("// Demand cartridge swaps")
);
assert.match(abortFunction, /AGIPictureReleaseSource\(\)[^]*?AGIPictureReleasePicture\(\)[^]*?AGIPictureReleaseScene\(\)[^]*?__get_primask\(\)[^]*?__disable_irq\(\)[^]*?AGIPicPendingCommand = AGIPicCmd_Acknowledge[^]*?AGIPicAbortRequested = false[^]*?AGIPicHoldHelperBank = false[^]*?AGIPicError_DMATimeout[^]*?AGIPicStatus_ErrorBase \| AGIPicError_DMATimeout[^]*?__set_primask\(InterruptMask\)/);
assert.ok(abortFunction.indexOf("AGIPicHoldHelperBank = false") <
  abortFunction.indexOf("AGIPicRegisters[AGIPicReg_Status"));
assert.ok(abortFunction.indexOf("AGIPicRegisters[AGIPicReg_Status") <
  abortFunction.indexOf("__set_primask(InterruptMask)"));
assert.match(firmware, /void AGIPicturePollingHndlr\(\)[^]*?AGIPicResetPending[^]*?AGIPictureResetSession\(\)[^]*?uint8_t Command = AGIPicPendingCommand[^]*?AGIPicAbortRequested[^]*?AGIPictureAbort\(\)[^]*?uint32_t StartMS/);
assert.equal(0xE0 | 10, 0xEA);

const patchDma = firmware.slice(
  firmware.indexOf("static bool AGIPictureDMAPatch"),
  firmware.indexOf("static void AGIPictureDirtyClear")
);
const materializePatch = firmware.slice(
  firmware.indexOf("static bool AGIPictureMaterializePatch"),
  firmware.indexOf("static bool AGIPictureDMAPatch")
);
assert.match(firmware, /AGIPicPatchMaximumBytes\s*=\s*1000u \* 13u/);
assert.match(firmware, /static DMAMEM uint8_t AGIPicPatchEncoded\[AGIPicPatchMaximumBytes\]/,
  "the 13 KB patch staging buffer must use roomy RAM2, not tight RAM1/DTCM");
assert.match(materializePatch,
  /Index < Length[^]*?AGIPictureReadRaw\(Raw \+ Index, &AGIPicPatchEncoded\[Index\], Error\)[^]*?AGIPictureReleaseSource\(\)/);
assert.ok(patchDma.indexOf("AGIPictureMaterializePatch") <
  patchDma.indexOf("AGIPicStatus_DMA"));
assert.match(patchDma,
  /!AGIPicSceneValid \|\| AGIPicScenePicture != Picture[^]*?Raw < AGIPicSceneRaw[^]*?Length > AGIPicSceneLength[^]*?Raw - AGIPicSceneRaw > AGIPicSceneLength - Length/);
assert.ok(patchDma.indexOf("!AGIPicSceneValid") <
  patchDma.indexOf("AGIPictureValidatePatch"));
assert.match(patchDma,
  /while \(Okay && Cursor < Length\)[^]*?AGIPicPatchEncoded\[Cursor\+\+\][^]*?AGIPictureDMAWritePatchSegment/);
assert.doesNotMatch(patchDma,
  /AGIPictureDMAWriteSegment|AGIContinueDMA|AGIPictureCloseScatter|bool Started/);
assert.doesNotMatch(patchDma, /AGIPictureReadRaw/,
  "no cartridge or scene-cache read may occur during bounded patch transfers");
assert.match(patchDma,
  /AGIPicRoomValid && AGIPicRoomToken == Picture[^]*?AGIPictureDirtySet\(AGIPicRoomPendingDirty, Cell \+ Index\)/);
assert.doesNotMatch(patchDma,
  /AGIPicRoomBitmap\[[^]*?=|AGIPicRoomScreen\[[^]*?=|AGIPicRoomColour\[[^]*?=/,
  "$20 actor patches must not mutate the immutable room seed");
assert.match(firmware,
  /Command == AGIPicCmd_V3PatchDMA[^]*?AGIPictureDMAPatch\(Raw, Argument, Token, &Error\)/);

assert.match(firmware, /AGIPicCmd_V3RoomSeed\s*=\s*0x22/);
assert.match(firmware, /AGIPicCmd_V3ActorFrame\s*=\s*0x23/);
assert.match(firmware, /AGIPicStatus_V3RoomSeeded\s*=\s*0xA2/);
assert.match(firmware, /AGIPicStatus_V3DoneActorFrame\s*=\s*0xA3/);
assert.match(firmware, /AGIPicActorDirtyLimit\s*=\s*212/);
assert.match(firmware,
  /Address == AGIPicReg_Error && AGIPicProtocol == AGIPicProtocolV3[^]*?AGIPicErrorMailbox = Data/,
  "$DFFB must be a writable command byte after v3 activation");
assert.match(firmware,
  /AGIPictureSeedRoom\(Raw, Token, AGIPicErrorMailbox,[^]*?Argument, &Error\)/);
const roomSeed = firmware.slice(
  firmware.indexOf("FLASHMEM static bool AGIPictureSeedRoom"),
  firmware.indexOf("static bool AGIPictureReadRawBytes")
);
assert.doesNotMatch(roomSeed, /AGIPictureReadGAC3Header|AGIPictureReadRaw/,
  "$22 must seed live planes even when the room has no GAC3 cache");
assert.match(firmware,
  /AGIPictureDMAReadRoomSegment\(0x6000[^]*?0x5C00[^]*?0xD800[^]*?0x8000/);
assert.match(firmware,
  /AGIPictureValidateRoomPriority[^]*?Format != 2 && Format != 3/);
assert.match(firmware,
  /memcmp\(Header, "GBC1", 4\)[^]*?Header\[4\] != 1[^]*?Header\[5\] != 7/);
assert.match(firmware,
  /AGIPicGBC1ViewCacheCapacity\s*=\s*\(uint32_t\)AGIPicGBC1ViewCacheEntries \* AGIPicGBC1ViewCacheSlotBytes/);
assert.match(firmware, /static EXTMEM uint8_t AGIPicGBC1ViewCacheMemory\[AGIPicGBC1ViewCacheCapacity\]/);
assert.doesNotMatch(firmware, /extmem_malloc/);
const cartridgeStartup = firmware.slice(
  firmware.indexOf("static void AGIPictureActivate"),
  firmware.indexOf("static void AGIPictureUnlockWrite")
);
const poller = firmware.slice(
  firmware.indexOf("void AGIPicturePollingHndlr"),
  firmware.length
);
assert.doesNotMatch(cartridgeStartup, /extmem_malloc|malloc|new\s/,
  "cartridge startup and IO2 activation must never allocate");
assert.doesNotMatch(poller,
  /extmem_malloc|AGIPicGBC1ViewCacheAllocationAttempted/,
  "the first posted picture must not allocate or zero the actor cache");
assert.match(firmware,
  /AGIPictureFindCachedGBC1Span[^]*?Entry->Offset > AGIPicGBC1ViewCacheCapacity[^]*?Length > Entry->Length - Relative[^]*?AGIPicGBC1ViewCacheMemory \+ Entry->Offset \+ Relative/);
assert.match(firmware,
  /AGIPictureReadRawBytes[^]*?AGIPicAbortRequested[^]*?AGIPictureFindCachedGBC1Span\(Raw, Length, &Cached\)[^]*?memcpy\(Data, Cached, Length\)/,
  "all directories and cel patterns within a cached VIEW must use PSRAM");
assert.match(firmware,
  /AGIPicGBC1ViewCacheSlotBytes\s*=\s*0xFFFFu[^]*?AGIPicGBC1ViewCacheEntries \*[^]*?AGIPicGBC1ViewCacheSlotBytes <= AGIPicGBC1ViewCacheCapacity/);
assert.match(firmware,
  /AGIPictureCacheGBC1View[^]*?Entry->Generation != AGIPicGBC1ViewCacheGeneration[^]*?Entry->Offset =[^]*?Selected[^]*?AGIPicGBC1ViewCacheSlotBytes/,
  "fixed PSRAM slots must replace a stale VIEW without evicting this frame's cohort");
const strictGbc1 = firmware.slice(
  firmware.indexOf("AGIPictureValidateGBC1View"),
  firmware.indexOf("static uint8_t AGIPictureAutomaticPriority")
);
assert.match(strictGbc1,
  /FirstCel != ExpectedFirstCel[^]*?ExpectedFirstCel != CelCount/,
  "loop ranges must be contiguous and cover every cel");
assert.match(strictGbc1,
  /Payload != DictionaryStart[^]*?DictionaryStart = \(uint16_t\)PayloadEnd/,
  "unique exact blocks must form one contiguous payload area");
assert.match(strictGbc1,
  /Pointer < DictionaryStart[^]*?Pointer - DictionaryStart\) % AGIPicGBC1PatternBytes/,
  "nonzero cell pointers must select aligned 16-byte dictionary patterns");
assert.match(strictGbc1,
  /SourceX >= Cel\[2\] \|\| SourceY >= Cel\[3\][^]*?Value != 8[^]*?!Opaque/,
  "edge padding must stay transparent and referenced patterns must be nonempty");
assert.match(strictGbc1,
  /AGIPicGBC1PatternReferenced[^]*?PatternHashes\[Prior\][^]*?!memcmp\(Pattern, PriorPattern/,
  "the canonical dictionary has no unreferenced or duplicate patterns");
assert.match(firmware,
  /State\.Flags & 0x80[^]*?State\.Priority < 4 \|\| State\.Priority > 15/,
  "fixed AGI priorities are a raw low nibble in the drawable 4..15 range");
const actorFrame = firmware.slice(
  firmware.indexOf("static bool AGIPictureActorFrame"),
  firmware.indexOf("static void AGIPictureResetSession")
);
assert.ok(actorFrame.indexOf("AGIPictureBuildActorOrder") <
  actorFrame.indexOf("AGIPicturePublishActorFrame"));
assert.match(actorFrame, /AGIPictureStageActorPatch[^]*?AGIPictureStageActorFallback/);
const publishActorFrame = firmware.slice(
  firmware.indexOf("static bool AGIPicturePublishActorFrame"),
  firmware.indexOf("static bool AGIPictureActorFrame")
);
assert.match(publishActorFrame,
  /0x6000[^]*?0x5C00[^]*?0xD800[^]*?0xB480[^]*?0x4ED5[^]*?0x5B10/);
assert.match(publishActorFrame,
  /uint8_t Transaction\[3\] = \{PriorCount, 0, 0\}[^]*?AGIPictureDMAWriteSegment\(&Started, 0x5B10, Transaction,[^]*?sizeof\(Transaction\)\)/,
  "$23 terminal publication retires C64 stage and transaction state");
assert.match(publishActorFrame,
  /bool Started = false, Okay = true[^]*?AGIPictureDMAWriteSegment\(&Started, 0x6000[^]*?0x5C00[^]*?0xD800/,
  "$23 holds one scatter transaction across every pixel plane");
assert.doesNotMatch(publishActorFrame,
  /AGIPictureDMAWritePatchSegment|PerformDMA|CloseDMA\(/,
  "$23 must never close and reacquire /DMA between actor runs");
assert.match(publishActorFrame,
  /bool Closed = AGIPictureCloseScatter\(Started\)[^]*?return Okay && Closed/,
  "$23 closes its held transaction even after a failed continuation");
assert.match(firmware,
  /uint16_t ObjectBase = AGIPicErrorMailbox \|[^]*?AGIPicReg_Argument0[^]*?uint8_t FirstObject =[^]*?AGIPicReg_Argument1/,
  "$23 uses DFFB-DFFC for object_x and DFFD for native-first-object");

function resetWhileBusy(state) {
  if (state.status === 1 || state.status === 2)
    return { ...state, abortRequested: true };
  return { ...state, active: false, status: 0, abortRequested: false };
}
assert.deepEqual(resetWhileBusy({ active: true, status: 1, abortRequested: false }),
  { active: true, status: 1, abortRequested: true });
assert.deepEqual(resetWhileBusy({ active: true, status: 2, abortRequested: false }),
  { active: true, status: 2, abortRequested: true });
assert.deepEqual(resetWhileBusy({ active: true, status: 0x90, abortRequested: false }),
  { active: false, status: 0, abortRequested: false });

assert.match(firmware, /memcmp\(Header, "GAC3", 4\)[^]*?Header\[4\] != 3[^]*?Header\[6\] != 26/);
assert.match(firmware, /TargetEnd - TargetStart[^]*?AGIPicGAC3MaximumScene/);
assert.match(firmware, /AGIPicSceneValid && Raw >= AGIPicSceneRaw/);
assert.match(firmware, /AGIPicGAC3MaximumScene\s*=\s*0xFFFFu/);
assert.match(firmware, /AGIPicMaximumSceneSlots\s*=\s*8/);
assert.match(firmware, /AGIPicturePrefetchScene[^]*?AGIPictureReleaseScene\(\)[^]*?AGIPictureReadGAC3Header[^]*?RequiredSlots[^]*?AGIPictureBorrowSlot\(\)[^]*?AGIPictureReleaseSource\(\)/);
assert.match(firmware, /AGIPicScenePicture = Picture[^]*?AGIPicSceneValid = true/);
assert.match(firmware, /for \(uint32_t Index = 0; Index < SceneLength; Index\+\+\)/);
assert.match(firmware, /SceneOffset >> 13[^]*?SceneOffset & 0x1FFFu/);
assert.match(firmware, /Count \* 10u[^]*?Cell \/ 40 != \(Cell \+ Count - 1\) \/ 40/);
assert.match(firmware, /AGIPicHoldHelperBank = Command != AGIPicCmd_V3PrefetchPicture &&[^]*?Command != AGIPicCmd_V3PrefetchScene/);
assert.match(firmware, /bool AGIPictureBusy\(\)[^]*?return AGIPicActive && AGIPicHoldHelperBank/);

function encodeRle(input) {
  const output = [];
  for (let cursor = 0; cursor < input.length;) {
    let run = 1;
    while (cursor + run < input.length && input[cursor + run] === input[cursor] && run < 130) run++;
    if (run >= 3) {
      output.push(0x80 | (run - 3), input[cursor]);
      cursor += run;
      continue;
    }
    const start = cursor;
    cursor += run;
    while (cursor < input.length && cursor - start < 128) {
      run = 1;
      while (cursor + run < input.length && input[cursor + run] === input[cursor] && run < 130) run++;
      if (run >= 3) break;
      cursor += run;
    }
    output.push(cursor - start - 1, ...input.subarray(start, cursor));
  }
  return Buffer.from(output);
}

function decodeRle(input, length) {
  const output = Buffer.alloc(length);
  let source = 0;
  let target = 0;
  while (target < length) {
    if (source >= input.length) throw new Error("truncated RLE");
    const control = input[source++];
    if (control & 0x80) {
      const count = (control & 0x7f) + 3;
      if (source >= input.length || target + count > length) throw new Error("bad RLE run");
      output.fill(input[source++], target, target += count);
    } else {
      const count = control + 1;
      if (source + count > input.length || target + count > length) throw new Error("bad RLE literal");
      input.copy(output, target, source, source += count);
      target += count;
    }
  }
  return { output, bytes: source };
}

const plane = Buffer.alloc(8000);
for (let index = 0; index < plane.length; index++) plane[index] = index % 97 < 8 ? 6 : index & 0xff;
const encoded = encodeRle(plane);
assert.deepEqual(decodeRle(encoded, plane.length), { output: plane, bytes: encoded.length });
assert.throws(() => decodeRle(Buffer.from([0xff, 1]), 10), /run/);

// This stream was produced by the compiler's bundled Exomizer 3.1.1 with
// `raw -q -p1 -T4` from the deterministic 8,000-byte bitmap below. The decoder
// intentionally mirrors IOH_AGIPicture.c, including its bit-buffer refill,
// four table groups, literal blocks, and reusable-offset state machine.
const exomizerFixture = Buffer.from(
  "60004051004d9c00000000048d159c08d159c80000000008919c20048000381101cc0273031cc7043105cc06730739ffbbe0052a4f7499beffe3082d52779cc1e6ac0b6c2309980ae60b398e0c630d980ee60f767620300bf707625500bf7076207a0bf707629f00bf707620c40bf70762e900bf7076200e0bf707623300bf707620580bf707627d00bf707620a20bf60762c700bf607620ec0bf607621100bf607620360bf607625b00bf4400ea20800bf7e762a500bf607620ca0bf62762ef00bf647620140bf667623900bf6876205e0bf6a7628300bf6c7620a80bf6e762cd00bf707620f20bf707621700bf7076203c0bf70762613cfc0001",
  "hex"
);
function decodeFirmwareExomizer(input, expectedDestination, outputLength) {
  let cursor = 0;
  const readByte = () => {
    if (cursor >= input.length) throw new Error("truncated Exomizer stream");
    return input[cursor++];
  };
  const destination = (readByte() << 8) | readByte();
  if (destination !== expectedDestination) throw new Error("bad Exomizer destination");
  const output = Buffer.alloc(outputLength);
  const table = Array.from({ length: 52 }, () => ({ bits: 0, base: 0 }));
  let position = 0;
  let bitBuffer = readByte();
  let reuseState = 1;
  let offset = 0;
  let offsetValid = false;
  const readBits = (requested) => {
    let result = 0;
    const copyByte = (requested & 8) !== 0;
    let count = requested & 7;
    while (count-- > 0) {
      let carry = (bitBuffer & 0x80) !== 0;
      bitBuffer = (bitBuffer << 1) & 0xff;
      if (bitBuffer === 0) {
        bitBuffer = readByte();
        carry = (bitBuffer & 0x80) !== 0;
        bitBuffer = ((bitBuffer << 1) | 1) & 0xff;
      }
      result = (result << 1) | (carry ? 1 : 0);
    }
    if (copyByte) result = (result << 8) | readByte();
    return result;
  };
  const generateTable = (start, count) => {
    let base = 1;
    for (let index = 0; index < count; index++) {
      const bits = readBits(3) | (readBits(1) << 3);
      if (base > 0xffff) throw new Error("bad Exomizer table");
      table[start + index] = { bits, base };
      base += 2 ** bits;
    }
  };
  const literal = (length) => {
    if (!length || length > output.length - position) throw new Error("bad Exomizer literal");
    while (length-- > 0) output[position++] = readByte();
    reuseState = ((reuseState << 1) | 1) & 0xff;
  };
  generateTable(0, 16);
  generateTable(16, 16);
  generateTable(32, 16);
  generateTable(48, 4);
  literal(1);
  for (;;) {
    if (readBits(1)) {
      literal(1);
      continue;
    }
    let lengthIndex = 0;
    let bit;
    do {
      bit = readBits(1);
      if (!bit && ++lengthIndex > 17) throw new Error("bad Exomizer length index");
    } while (!bit);
    if (lengthIndex === 16) {
      if (position !== output.length || cursor !== input.length) throw new Error("bad Exomizer end marker");
      return output;
    }
    if (lengthIndex === 17) {
      literal((readByte() << 8) | readByte());
      continue;
    }
    const lengthEntry = table[lengthIndex];
    let sequenceLength = lengthEntry.base + readBits(lengthEntry.bits);
    if (!sequenceLength || sequenceLength > output.length - position)
      throw new Error("bad Exomizer sequence length");
    let readOffset = (reuseState & 3) !== 1;
    if (!readOffset) readOffset = readBits(1) === 0;
    if (readOffset) {
      const tableIndex = sequenceLength === 1 ? 48 + readBits(2) :
        (sequenceLength === 2 ? 32 + readBits(4) : 16 + readBits(4));
      const entry = table[tableIndex];
      offset = entry.base + readBits(entry.bits);
      if (!offset || offset > position) throw new Error("bad Exomizer offset");
      offsetValid = true;
    }
    if (!offsetValid || offset > position) throw new Error("missing Exomizer offset");
    while (sequenceLength-- > 0) output[position] = output[position++ - offset];
    reuseState = (reuseState << 1) & 0xff;
  }
}
const exomizerExpected = Buffer.alloc(8000);
for (let index = 0; index < exomizerExpected.length; index++)
  exomizerExpected[index] = index % 257 < 240 ? (index >> 5) & 15 : (index * 37 + 11) & 0xff;
assert.deepEqual(decodeFirmwareExomizer(exomizerFixture, 0x6000, 8000), exomizerExpected);
const badExomizerDestination = Buffer.from(exomizerFixture);
badExomizerDestination[0] ^= 1;
assert.throws(() => decodeFirmwareExomizer(badExomizerDestination, 0x6000, 8000), /destination/);

function makeDescriptor({ codec = 0, md2 = false, priority = true, token = 7,
                          visualLength = 1234, priorityRaw = 0x123456,
                          priorityWord = 0x8123 } = {}) {
  const data = Buffer.alloc(16);
  data.write("AGP3", 0, "ascii");
  data[4] = 16;
  data[5] = codec;
  data[6] = (md2 ? 1 : 0) | (priority ? 2 : 0);
  data[7] = token;
  data.writeUInt16LE(visualLength, 8);
  data.writeUIntLE(priorityRaw, 10, 3);
  data.writeUInt16LE(priorityWord, 13);
  data[15] = data.subarray(0, 15).reduce((sum, value) => sum ^ value, 0);
  return data;
}
const descriptor = makeDescriptor({ codec: 2, md2: true });
assert.equal(descriptor.subarray(0, 4).toString("ascii"), "AGP3");
assert.equal(descriptor[15], descriptor.subarray(0, 15).reduce((sum, value) => sum ^ value, 0));
const corrupted = Buffer.from(descriptor);
corrupted[8] ^= 1;
assert.notEqual(corrupted[15], corrupted.subarray(0, 15).reduce((sum, value) => sum ^ value, 0));

// Uniform compact-priority fixture: 168 one-run rows, each with capacity one.
// The exact runtime representation is 336 pointer bytes plus four bytes/row.
const compactRows = Buffer.concat(Array.from({ length: 168 }, () => Buffer.from([1, 0x40])));
const compactRuntimeBytes = 336 + 168 * 4;
const compact = Buffer.concat([
  Buffer.from([0xa3, 160, 168, compactRuntimeBytes & 0xff, compactRuntimeBytes >> 8]),
  compactRows
]);
function materializeUniformPriority(data) {
  assert.equal(data[0], 0xa3);
  const runtimeBytes = data.readUInt16LE(3);
  const runtime = Buffer.alloc(runtimeBytes);
  let source = 5;
  let target = 336;
  for (let row = 0; row < 168; row++) {
    const capacity = data[source++] || 160;
    const descriptorByte = data[source++];
    assert.equal(descriptorByte & 15, 0);
    const address = 0x8000 + target;
    runtime[row] = address & 0xff;
    runtime[168 + row] = address >> 8;
    runtime[target] = 1;
    runtime[target + 1] = capacity === 160 ? 0 : capacity;
    runtime[target + 2] = 160;
    runtime[target + 3] = descriptorByte >> 4;
    target += 2 + capacity * 2;
  }
  assert.equal(source, data.length);
  assert.equal(target, runtime.length);
  return runtime;
}
const priorityRuntime = materializeUniformPriority(compact);
assert.equal(priorityRuntime.length, 1008);
assert.equal(priorityRuntime[336], 1);
assert.equal(priorityRuntime[338], 160);
assert.equal(priorityRuntime[339], 4);

function validatePatch(patch) {
  if (!patch.length || patch.length > 13000) throw new Error("invalid patch length");
  let cursor = 0;
  let cells = 0;
  while (cursor < patch.length) {
    if (cursor + 3 > patch.length) throw new Error("truncated patch header");
    const cell = patch.readUInt16LE(cursor);
    const count = patch[cursor + 2];
    cursor += 3;
    if (!count || count > 40 || cell + count > 1000 ||
        Math.floor(cell / 40) !== Math.floor((cell + count - 1) / 40) ||
        cursor + count * 10 > patch.length) throw new Error("invalid patch");
    cursor += count * 10;
    cells += count;
  }
  if (!cells) throw new Error("empty patch");
  return cells;
}
const patch = Buffer.alloc(3 + 2 * 10);
patch.writeUInt16LE(41, 0);
patch[2] = 2;
for (let index = 3; index < patch.length; index++) patch[index] = index;
assert.equal(validatePatch(patch), 2);
const crossRow = Buffer.from(patch);
crossRow.writeUInt16LE(39, 0);
assert.throws(() => validatePatch(crossRow), /invalid/);

// Model the firmware's bounded patch-DMA contract. This is the six-run shape
// used by SQ1's Room 1 cartridge-reader door: each contiguous bitmap, screen,
// and colour segment is acquired and closed independently.
function buildPatch(runCells, count = 2) {
  const records = runCells.map((cell, run) => {
    const record = Buffer.alloc(3 + count * 10);
    record.writeUInt16LE(cell, 0);
    record[2] = count;
    for (let index = 3; index < record.length; index++)
      record[index] = (run * 29 + index) & 0xff;
    return record;
  });
  return Buffer.concat(records);
}

function planPatchScatter(patch) {
  validatePatch(patch);
  let cursor = 0;
  let acquisitions = 0;
  let closes = 0;
  const segments = [];
  while (cursor < patch.length) {
    const cell = patch.readUInt16LE(cursor);
    const count = patch[cursor + 2];
    cursor += 3;
    const bitmap = Buffer.alloc(count * 8);
    const screen = Buffer.alloc(count);
    const colour = Buffer.alloc(count);
    for (let index = 0; index < count; index++) {
      patch.copy(bitmap, index * 8, cursor, cursor + 8);
      cursor += 8;
      screen[index] = patch[cursor++];
      colour[index] = patch[cursor++];
    }
    segments.push(
      { address: 0x6000 + cell * 8, data: bitmap },
      { address: 0x5c00 + cell, data: screen },
      { address: 0xd800 + cell, data: colour }
    );
    acquisitions += 3;
    closes += 3;
  }
  return { acquisitions, closes, segments };
}

const sq1DoorPatch = buildPatch([433, 473, 513, 553, 593, 633]);
assert.equal(sq1DoorPatch.length, 138);
assert.equal(validatePatch(sq1DoorPatch), 12);
const sq1DoorScatter = planPatchScatter(sq1DoorPatch);
assert.deepEqual({ acquisitions: sq1DoorScatter.acquisitions,
  closes: sq1DoorScatter.closes, segments: sq1DoorScatter.segments.length },
{ acquisitions: 18, closes: 18, segments: 18 });
assert.deepEqual(sq1DoorScatter.segments
  .filter((_, index) => index % 3 === 0)
  .map(({ address, data }) => [address, data.length]),
[[0x6000 + 433 * 8, 16], [0x6000 + 473 * 8, 16],
 [0x6000 + 513 * 8, 16], [0x6000 + 553 * 8, 16],
 [0x6000 + 593 * 8, 16], [0x6000 + 633 * 8, 16]]);
const maximumPatch = buildPatch(Array.from({ length: 1000 }, (_, cell) => cell), 1);
assert.equal(maximumPatch.length, 13000);
assert.equal(validatePatch(maximumPatch), 1000);
assert.throws(() => validatePatch(Buffer.concat([maximumPatch, Buffer.from([0])])),
  /length/);

// The v3 response has its own one-shot latch. Command completion is allowed to
// update the ordinary error register between challenge and response read.
let protocolActive = false;
let challengeSeen = false;
let responsePending = false;
let unlockStage = 0;
let ordinaryError = 0;
function mailboxWrite(address, value) {
  if (address === 0xfb && value === 0x3c) {
    challengeSeen = true;
    responsePending = true;
    unlockStage = 0;
    return;
  }
  if (protocolActive) return;
  const expected = [[0xf0, 0x41], [0xf1, 0x47], [0xf2, 0x49], [0xf3, 0x2b], [0xf4, 3]];
  const step = expected[unlockStage];
  if (challengeSeen && step && address === step[0] && value === step[1]) {
    if (++unlockStage === expected.length) {
      protocolActive = true;
      challengeSeen = false;
    }
    return;
  }
  challengeSeen = responsePending = false;
  unlockStage = 0;
}
function mailboxReadError() {
  if (responsePending) {
    responsePending = false;
    return 0xc3;
  }
  return ordinaryError;
}
mailboxWrite(0xfb, 0x3c);
for (const [address, value] of [[0xf0, 0x41], [0xf1, 0x47], [0xf2, 0x49], [0xf3, 0x2b], [0xf4, 3]])
  mailboxWrite(address, value);
ordinaryError = 0x12; // asynchronous completion/error cannot consume response
assert.equal(protocolActive, true);
assert.equal(mailboxReadError(), 0xc3);
assert.equal(mailboxReadError(), 0x12);

console.log("AGI+2/AGI+3 firmware conformance: PASS");
console.log("  mailbox: race-proof challenge, capabilities, commands, terminal statuses");
console.log("  picture: RLE + real Exomizer P1 vector, AGP3, compact/full priority");

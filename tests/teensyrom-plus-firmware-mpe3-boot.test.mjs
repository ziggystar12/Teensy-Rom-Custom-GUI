import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { patchAdditions, applyFilePatch } from "./firmware-patch-text.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const patchPath = path.join(root, "engine/patches/0032-Route-native-title-to-MinimalBoot.patch");
const patch = fs.readFileSync(patchPath, "utf8");
const identityPatchPath = path.join(root, "engine/patches/0044-Recognize-DOSVM-cartridge-identity.patch");
const identityPatch = fs.readFileSync(identityPatchPath, "utf8");

function addedFunction(source, name) {
  const start = source.indexOf(`FLASHMEM bool ${name}(`);
  assert.ok(start >= 0);
  const open = source.indexOf("{", start);
  let depth = 0;
  for (let index = open; index < source.length; index++) {
    if (source[index] === "{") depth++;
    if (source[index] === "}" && --depth === 0) return source.slice(start, index + 1);
  }
  throw new Error(`Incomplete added function ${name}`);
}

const parserAdditions = patchAdditions(patch, "Source/Teensy/FileParsers.ino");
const finalParser = applyFilePatch(parserAdditions, identityPatch, "Source/Teensy/FileParsers.ino");
const selector = addedFunction(finalParser, "CRTRequiresMPE3MinimalBoot");
const launch = addedFunction(parserAdditions, "LaunchCRTInMinimal");
const loaderRoute = patchAdditions(patch, "Source/Teensy/DriveDirLoad.ino");

test("0032 confines native title launch routing to the full firmware loader", () => {
  const changed = [...patch.matchAll(/^diff --git a\/(\S+) b\/(\S+)$/gm)].map(match => match[2]);
  assert.deepEqual(changed.sort(), ["Source/Teensy/DriveDirLoad.ino", "Source/Teensy/FileParsers.ino"]);
  assert.match(parserAdditions, /return LaunchCRTInMinimal\(FullFilePath\);/,
    "the old allocation fallback must reuse the same guarded launch helper");
  assert.equal((patch.match(/^\+\s+EEPROM\.write\(eepAdMinBootInd/gm) ?? []).length, 1,
    "there must be a single shared boot-flag writer");
  // The actual C++ test below checks every byte of the fixed cartridge identity.
  // AGI-64 independently checks the compiler's header against this same ABI.
  assert.match(loaderRoute, /myFile\.close\(\);\s+return LaunchCRTInMinimal\(FullFilePath\);/,
    "a launch failure must not continue into the unserviced full-image loader");
});

test("actual final C++ routing accepts DOSVM and Sierra, rejects near matches, and checks SD/dual image", () => {
  const candidates = [process.env.CXX,
    ...(process.platform === "win32" ? ["C:/msys64/mingw64/bin/g++.exe", "C:/msys64/ucrt64/bin/g++.exe"] : []),
    "g++", "clang++"].filter(Boolean);
  const compiler = candidates.find(candidate => spawnSync(candidate, ["--version"], {
    encoding: "utf8", windowsHide: true
  }).status === 0);
  assert.ok(compiler, "a native C++ compiler is required for the actual launch decision test");
  const output = path.resolve(process.env.MPE3_BOOT_TEST_OUTPUT ?? path.join(root, "build/mpe3-boot-routing"));
  fs.mkdirSync(output, { recursive: true });
  const sourcePath = path.join(output, "native-title-boot-test.cpp");
  const executable = path.join(output, process.platform === "win32" ? "native-title-boot-test.exe" : "native-title-boot-test");
  const source = `#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <utility>
#define FLASHMEM
#define CRT_MAIN_HDR_LEN 0x40
#define Cart_EasyFlash 32
alignas(4096) static uint8_t image[0x4000];
#define FLASH_BASEADDRESS static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image))
static constexpr uint32_t UpperAddr = 0;
static constexpr uint16_t eepAdCrtBootName = 1919, eepAdMinBootInd = 2175;
static constexpr uint8_t MinBootInd_ExecuteMin = 1;
static unsigned reboots, closes, normalLoads, pathWrites, checks;
static std::string lastMessage, savedPath;
static std::vector<std::pair<uint16_t, uint8_t>> eepromWrites;
struct EEPROMStub { void write(uint16_t a, uint8_t b) { eepromWrites.emplace_back(a,b); } } EEPROM;
static void EEPwriteStr(uint16_t a, const char *p) { assert(a == eepAdCrtBootName); savedPath=p; pathWrites++; }
static void SendMsgPrintfln(const char *s, ...) { lastMessage=s; }
#define Printf_dbg(...) do {} while (false)
#define REBOOT (++reboots)
${selector}
${launch}
struct FileStub { void close() { closes++; } };
static bool load(const uint8_t *Header, const char *FullFilePath) {
  uint8_t lclBuf[64]; std::memcpy(lclBuf, Header, sizeof(lclBuf)); FileStub myFile;
${loaderRoute}
  normalLoads++; return true;
}
static void reset() {
  std::memset(image, 0, sizeof(image));
  *reinterpret_cast<uint32_t *>(image) = 0x42464346;
  *reinterpret_cast<uint32_t *>(image+0x1000) = 0x432000D1;
  *reinterpret_cast<uint32_t *>(image+0x1004) = FLASH_BASEADDRESS+0x1100;
  reboots=closes=normalLoads=pathWrites=0; lastMessage.clear(); savedPath.clear(); eepromWrites.clear();
}
static void check(bool condition) { assert(condition); checks++; }
static void untouched() { check(reboots==0 && pathWrites==0 && eepromWrites.empty()); }
int main() {
  // The production function reads 32-bit flash addresses. The native executable
  // is linked below 4 GiB so the actual guard code can read this harmless fixture.
  check(reinterpret_cast<uintptr_t>(image) <= UINT32_MAX-sizeof(image));
  for (const auto &fixture : {
      std::pair<const char *,const char *>{"SQ1 MPE3 TITLE PULL", "/SQ1-64-MPE3-TITLE-DIAGNOSTIC.crt"},
      {"MHS DOSVM", "/DOSVM.CRT"}}) {
  uint8_t title[64] = {};
  std::memcpy(title, "C64 CARTRIDGE   ", 16);
  title[0x13]=64; title[0x14]=1; title[0x17]=32; title[0x18]=1;
  std::memcpy(title+0x20, fixture.first, std::strlen(fixture.first));
  check(CRTRequiresMPE3MinimalBoot(title));
  reset();
  check(!load(title, fixture.second));
  check(closes==1 && normalLoads==0 && reboots==1 && pathWrites==1);
  check(savedPath==fixture.second);
  check(eepromWrites.size()==1 && eepromWrites[0].first==eepAdMinBootInd && eepromWrites[0].second==MinBootInd_ExecuteMin);
  // Every signature, version, type, length and padded-name byte is significant.
  for (unsigned offset=0; offset<64; offset++) {
    if (offset>=0x18 && offset<0x20) continue;
    uint8_t other[64]; std::memcpy(other,title,64); other[offset]^=1;
    check(!CRTRequiresMPE3MinimalBoot(other));
    reset(); check(load(other,"/Other.crt")); check(normalLoads==1 && closes==0); untouched();
  }
  reset(); check(!load(title,"")); check(lastMessage=="Must run this crt from SD");
  check(closes==1 && normalLoads==0); untouched();
  for (unsigned offset : {0u,0x1000u}) {
    reset(); image[offset]^=1; check(!load(title,"/Title.crt"));
    check(lastMessage=="Dual boot image not found (NoMagNums)"); check(normalLoads==0); untouched();
  }
  for (uint32_t offset : {0x0fffu,0x3001u}) {
    reset(); *reinterpret_cast<uint32_t *>(image+0x1004)=FLASH_BASEADDRESS+offset;
    check(!load(title,"/Title.crt")); check(lastMessage=="Dual boot image not found (NoFirstInst)"); untouched();
  }
  for (uint32_t offset : {0x1000u,0x3000u}) {
    reset(); *reinterpret_cast<uint32_t *>(image+0x1004)=FLASH_BASEADDRESS+offset;
    check(!LaunchCRTInMinimal("/Large-existing-cart.crt")); check(reboots==1 && pathWrites==1);
  }
  }
  std::cout << "{\\"passed\\":true,\\"checks\\":" << checks << "}\\n";
}
`;
  fs.writeFileSync(sourcePath, source);
  const args = ["-std=c++17", "-O2", "-Wall", "-Wextra"];
  if (process.platform === "win32") args.push("-static", "-static-libgcc", "-static-libstdc++", "-Wl,--image-base,0x400000");
  else args.push("-no-pie");
  args.push(sourcePath, "-o", executable);
  execFileSync(compiler, args, { cwd: path.isAbsolute(compiler) ? path.dirname(compiler) : root,
    windowsHide: true, stdio: "pipe", timeout: 60000 });
  const result = JSON.parse(execFileSync(executable, [], { cwd: output, encoding: "utf8", windowsHide: true, timeout: 10000 }));
  assert.equal(result.passed, true);
  assert.ok(result.checks > 400);
  fs.writeFileSync(path.join(output, "result.json"), JSON.stringify({ ...result, compiler,
    patchSha256: createHash("sha256").update(patch).digest("hex"),
    identityPatchSha256: createHash("sha256").update(identityPatch).digest("hex"),
    sourceSha256: createHash("sha256").update(source).digest("hex"),
    scope: "Actual C++ header decision and guarded main-to-MinimalBoot launch; no hardware acceptance"
  }, null, 2) + "\n");
});

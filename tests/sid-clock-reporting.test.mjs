import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { execFileSync } from 'node:child_process';

const root = path.resolve(import.meta.dirname, '..');
const parser = fs.readFileSync(path.join(root, 'Source/Teensy/FileParsers.ino'), 'utf8');
const bitmap = fs.readFileSync(path.join(root, 'Source/C64/MainMenuCRT/source/GeosBitmap.s'), 'utf8');
const bodyReset = bitmap.slice(bitmap.indexOf('GeosBitmapWaitMessageReset:'),
  bitmap.indexOf('GeosBitmapWaitMessageChar:'));
const columns = Number(bodyReset.match(/lda #(\d+)\s+sta GeosBitmapWaitCol/)[1]);
const bodyCapacity = Number(bodyReset.match(/lda #(\d+)\s+sta GeosBitmapCount/)[1]);
const section = (start, end) => {
  const first = parser.indexOf(start), last = parser.indexOf(end, first + start.length);
  assert.ok(first >= 0 && last > first, `${start} through ${end}`);
  return parser.slice(first, last);
};

// Compile the complete production SID parser and its real message formatter.
// Only transport/Arduino services are stubbed: the test does not recreate the
// parser, its format strings, clock selection, or timer table in JavaScript.
test('executed SID parser separates tune metadata and hardware without changing playback', () => {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'teensy-sid-clock-'));
  const compiler = process.env.CXX ?? (process.platform === 'win32' ? 'C:/msys64/mingw64/bin/g++.exe' : 'g++');
  const cpp = path.join(temporary, 'clock.cpp');
  const exe = path.join(temporary, process.platform === 'win32' ? 'clock.exe' : 'clock');
  fs.writeFileSync(cpp, `
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include "Menu_Regs.h"
#define FLASHMEM
#define Printf_dbg(...) ((void)0)
static uint8_t Image[256], IO1[512];
static uint8_t *XferImage=Image;
static uint32_t XferSize, StreamOffsetAddr;
static char Info[512], StrMachineInfo[16], SerialStringBuf[256];
static char *StrSIDInfo=Info;
static std::vector<std::string> Messages;
static constexpr uint16_t GoodSIDToken=1, BadSIDToken=2;
static uint16_t LastToken;
static uint16_t toU16(const uint8_t *p) { return uint16_t(p[0])<<8|p[1]; }
static uint32_t toU32(const uint8_t *p) { return uint32_t(toU16(p))<<16|toU16(p+2); }
static void SendU16(uint16_t n) { LastToken=n; }
static void SendMsgSerialStringBuf() { Messages.emplace_back(SerialStringBuf); }
void SendMsgPrintfln(const char *, ...);
${section('FLASHMEM void SIDLoadError(', 'void RedirectEmptyDriveDirMenu(')}
${section('void SendMsgPrintfln(', 'void SendMsgPrintf(')}
static void hex(const char *p) { for(;*p;p++) std::printf("%02x",static_cast<unsigned char>(*p)); }
int main() {
  for(unsigned machine=0;machine<4;machine++) for(unsigned tune=0;tune<4;tune++) {
    std::memset(Image,0,sizeof(Image)); std::memset(IO1,0,sizeof(IO1));
    std::memset(Info,0,sizeof(Info)); std::memset(StrMachineInfo,0,sizeof(StrMachineInfo));
    Messages.clear(); LastToken=0; XferSize=sizeof(Image);
    std::memcpy(Image,"PSID",4); Image[5]=2; Image[7]=0x7c;
    Image[10]=0xe0; Image[12]=0xe0; Image[13]=3; Image[15]=Image[17]=1;
    std::memcpy(Image+0x16,"Clock fixture",13);
    Image[0x77]=0x10|(tune<<2); Image[0x7d]=0xe0;
    IO1[wRegVid_TOD_Clks]=machine; IO1[rwRegCodeStartPage]=0x48; IO1[rwRegCodeLastPage]=0x9f;
    ParseSIDHeader("clock.sid");
    if(LastToken!=GoodSIDToken||Messages.size()!=2||IO1[rRegStrAvailable]!=0xff) return 1;
    std::printf("%u\\t%u\\t%u\\t%u\\t",machine,tune,
      unsigned(IO1[rRegSIDDefSpeedHi])*256+IO1[rRegSIDDefSpeedLo],unsigned(IO1[wRegVid_TOD_Clks]));
    hex(Messages.back().c_str()); std::printf("\\t"); hex(StrMachineInfo); std::puts("");
  }
}
`);
  try {
    execFileSync(compiler, ['-std=c++17', '-O2', ...(process.platform === 'win32' ? ['-static'] : []),
      '-I', path.join(root, 'Source/Teensy/MinimalBoot/Common'), cpp, '-o', exe],
    { cwd: path.isAbsolute(compiler) ? path.dirname(compiler) : root, windowsHide: true, timeout: 60000 });
    const output = execFileSync(exe, [], { encoding: 'utf8', windowsHide: true, timeout: 10000 });
    const rows = output.trim().split(/\r?\n/);
    assert.equal(rows.length, 16, 'all tune clocks crossed with both video and TOD frequencies');
    const tuneNames = ['Unknown', 'PAL', 'NTSC', 'Either'];
    const calibratedTimers = [0x4cc7, 0x4fb2, 0x4058, 0x42c6];
    for (const row of rows) {
      const [m, t, timer, unchanged, messageHex, machineHex] = row.split('\t');
      const machine = Number(m), tune = Number(t), video = machine & 1 ? 'NTSC' : 'PAL';
      const hz = machine & 2 ? '60' : '50';
      assert.equal(Number(unchanged), machine, 'reporting never changes detected machine bits');
      assert.equal(Number(timer), calibratedTimers[(machine & 1) | (tune & 2)], 'calibrated CIA timer retained');
      assert.equal(Buffer.from(machineHex, 'hex').toString(), `${video} Vid, ${hz[0]}`, 'detailed SID page retained');
      const message = Buffer.from(messageHex, 'hex').toString();
      assert.ok(message.startsWith('\r\n'), 'real transport formatter supplies its normal prefix');
      const [tuneLine, machineLine, extra] = message.slice(2).split('\r');
      assert.equal(extra, undefined);
      assert.equal(tuneLine.trimEnd(), `SID tune timing: ${tuneNames[tune]}`);
      assert.equal(machineLine, `C64 video: ${video}, TOD: ${hz}Hz`);
      assert.equal(tuneLine.length, columns, 'first row wraps exactly before the bitmap renderer ignores CR');
      assert.ok(machineLine.length <= columns, 'machine description fits on one modal row');
      assert.ok(tuneLine.length + machineLine.length <= bodyCapacity, 'both rows remain visible together');
    }
  } finally {
    assert.equal(path.dirname(path.resolve(temporary)), path.resolve(os.tmpdir()), 'cleanup stays in the temporary directory');
    assert.ok(path.basename(temporary).startsWith('teensy-sid-clock-'));
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});

test('the shipped default tune declares PAL independently of C64 startup detection', () => {
  const header = fs.readFileSync(path.join(root, 'Source/Teensy/TRMenuFiles/SIDs/Div_Death_Is_no_Evil.sid.h'), 'utf8');
  const tune = Buffer.from([...header.matchAll(/0x([0-9a-f]{2})/gi)].map(match => Number.parseInt(match[1], 16)));
  assert.equal(tune.subarray(0x16, 0x36).toString('ascii').split('\0')[0], 'Death Is No Evil');
  assert.equal(tune.readUInt16BE(0x76), 0x14);
  assert.equal((tune.readUInt16BE(0x76) >> 2) & 3, 1, 'PAL is tune metadata');
  assert.equal(tune.readUInt32BE(0x12), 0, 'the default tune uses vertical-blank speed');
  const main = fs.readFileSync(path.join(root, 'Source/C64/MainMenuCRT/source/MainMenu.asm'), 'utf8');
  assert.ok(main.indexOf('stx wRegVid_TOD_Clks+IO1Port') < main.indexOf('lda #rCtlLoadSIDWAIT'),
    'startup publishes detected machine clocks before parsing its selected SID');
});

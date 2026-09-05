// SPDX-License-Identifier: GPL-2.0-or-later
// Assemble a local E1M1 test kit from measured, matching inputs. Never flashes.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {loadDosTerminal} from '../dos/tools/dos_terminal.mjs';
import {buildCartridgeBootBank} from '../vm/client/host/install-boot-bank.mjs';
import {crc32} from './vm-image.mjs';
import {assertGuiFirmwareVersion} from './firmware-version.mjs';
const root=fs.realpathSync(path.resolve(import.meta.dirname,'..'));
const firmwareBuild=path.resolve(root,process.argv[2]??'build/gba');
const audit=path.join(root,'build/doom/gbadoom-audit'),out=path.join(root,'build/doom/e1m1-test');
const sd=path.join(out,'SD'),pkg=path.join(sd,'VMS/DOOMVM');
const read=p=>fs.readFileSync(p),sha=p=>crypto.createHash('sha256').update(read(p)).digest('hex');
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b);};
const report=JSON.parse(read(path.join(audit,'report.json')));
assert.ok(report.status==='AUDIT_COMPLETE'&&report.loadable&&report.levelCycles.tics===2100);
for(const p of report.inputs)assert.equal(sha(path.join(root,p.path)),p.sha256,'Audit input drift: '+p.path);
assert.equal(sha(report.module.path),report.module.sha256);
assert.equal(sha(path.join(audit,'doom1.gbd')),report.wad.envelopedSha256);
const built=JSON.parse(read(path.join(firmwareBuild,'build-inputs.json')));
assert.equal(built.mode,'all');
for(const p of built.files)assert.equal(sha(path.join(root,p.path)),p.sha256,'Firmware input drift: '+p.path);
const version=assertGuiFirmwareVersion();
assert.equal(built.version,version.version);
const {buildMpe3TitleTerminal}=await loadDosTerminal(path.join(root,'vm/client'));
const terminal=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false,
    diagnosticTitle:'DOOMVM E1M1 - WAITING FOR HOST',diagnosticFooter:'CTRL FIRE  SPACE USE  RESET TO GUI'});
const boot=buildCartridgeBootBank(terminal.prg,{loadingText:'MHS DOOMVM E1M1',cartridgeFormat:'easyflash-1m'});
assert.equal(boot.length,16384);
const descriptor=Buffer.alloc(128);descriptor.write('VMH1');descriptor[4]=2;descriptor[5]=128;descriptor[6]=3;
descriptor.writeUInt32LE(crc32(boot),8);descriptor.write('DOOMVM',16);descriptor.write('/VMS/DOOMVM',48);
descriptor.write('DOS-INPUT-V2',80);descriptor.write('DOS-SID-V1',112);
descriptor.writeUInt32LE(crc32(descriptor.subarray(0,124)),124);
const header=Buffer.alloc(64);header.write('C64 CARTRIDGE   ');header.writeUInt32BE(64,16);
header.writeUInt16BE(0x100,20);header.writeUInt16BE(0x20,22);header[24]=1;header.write('MHS VM CLIENT ABI2',32);
const chip=(bank,address,bytes)=>{assert.equal(bytes.length,8192);const h=Buffer.alloc(16);h.write('CHIP');
    h.writeUInt32BE(8208,4);h.writeUInt16BE(2,8);h.writeUInt16BE(bank,10);h.writeUInt16BE(address,12);h.writeUInt16BE(8192,14);return Buffer.concat([h,bytes]);};
const native=Buffer.alloc(8192);descriptor.copy(native);
const crt=Buffer.concat([header,chip(0,0x8000,boot.subarray(0,8192)),chip(0,0xa000,boot.subarray(8192)),chip(1,0x8000,native)]);
assert.equal(crt.length,0x6070);
write(path.join(out,'doomvm.prg'),terminal.prg);write(path.join(out,'doomvm-boot.bin'),boot);
write(path.join(pkg,'client.crt'),crt);write(path.join(sd,'DOOMVM.crt'),crt);
write(path.join(pkg,'manifest.vmi'),'VM1\nDOOMVM\ngbd\nengine.mvm\nclient.crt\nEND\n');
write(path.join(pkg,'engine.mvm'),read(report.module.path));write(path.join(pkg,'doom1.gbd'),read(path.join(audit,'doom1.gbd')));
write(path.join(sd,version.filename),read(path.join(firmwareBuild,'SD',version.filename)));
// Same receiver as DOS; gate mask plus 25 SID registers accompany frame end.
write(path.join(out,'client.json'),JSON.stringify({format:'M3TP-DOOMVM-terminal',terminalPrgBytes:terminal.prg.length,
    terminalPrgSha256:sha(path.join(out,'doomvm.prg')),diagnosticTitle:'DOOMVM E1M1 - WAITING FOR HOST',
    diagnosticFooter:'CTRL FIRE  SPACE USE  RESET TO GUI',
    stageAddress:terminal.stageAddress,codeEnd:terminal.codeEnd,labels:terminal.labels,bootBankBytes:boot.length},null,2)+'\n');
const artifacts=[];
const walk=dir=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())walk(p);else artifacts.push({path:path.relative(sd,p).replaceAll('\\','/'),bytes:read(p).length,sha256:sha(p)});}};
walk(sd);
write(path.join(out,'package.json'),JSON.stringify({scope:'E1M1 local test only',abi:2,profile:1,firmwareVersion:version.version,
    hardwareTested:false,flashed:false,sourceCommit:report.sourceCommit,auditSha256:sha(path.join(audit,'report.json')),
    inputProtocol:'DOS-INPUT-V2',audioPacketBytes:26,artifacts},null,2)+'\n');
write(path.join(out,'README.txt'),`DOOMVM E1M1 - LOCAL HARDWARE TEST CANDIDATE\n\nUses the new RAM-only profile; older firmware rejects this module.\nThe SD folder contains matching test firmware and /VMS/DOOMVM.\nThis build script has not installed or flashed anything.\n\nAfter installing the matching test firmware and SD files, launch DOOMVM.crt\nor /VMS/DOOMVM/doom1.gbd in the GUI. Reset returns to the GUI.\nOnly E1M1 is supported. Either level exit restarts E1M1.\nW/S or up/down move; left/right turn; A/D strafe; Ctrl/fire shoots;\nSpace/Return uses/runs; Tab opens the automap; Escape opens the menu.\nSynthesized SID effects are enabled. Music and persistent saves are not implemented.\n\nARM linking and host execution passed; physical speed, rendering, input,\nRAM2 MPU access and SD behavior still need hardware acceptance.\nThis is a private test kit using supplied game data and upstream assets.\nDo not publish as a cleared release; see doom/LICENSE.md.\n`);
console.log(JSON.stringify({testKit:out,artifacts:artifacts.length,moduleBytes:report.module.bytes,hardwareTested:false},null,2));

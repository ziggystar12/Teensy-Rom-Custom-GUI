#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {loadNesTerminal,NES_INPUT} from '../tools/nes_terminal.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..'),agi=path.resolve(root,'../AGI-64');
const crtPath=path.resolve(process.argv[2]??path.join(root,'nes/sd-card/NESVM.CRT'));
const terminalManifest=JSON.parse(fs.readFileSync(process.argv[3]??path.join(root,'nes/build/crt/terminal.json'),'utf8'));
const cartManifest=JSON.parse(fs.readFileSync(process.argv[4]??path.join(root,'nes/build/crt/cartridge.json'),'utf8'));
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const crcTable=Array.from({length:256},(_,n)=>{let c=n;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);return c>>>0;});
const crc32=b=>{let c=0xffffffff;for(const n of b)c=(c>>>8)^crcTable[(c^n)&255];return(c^0xffffffff)>>>0;};
const crt=fs.readFileSync(crtPath);assert.equal(digest(crt),cartManifest.cartridgeSha256);assert.equal(crt.length,0x6070);
assert.equal(crt.subarray(0,16).toString('ascii'),'C64 CARTRIDGE   ');assert.equal(crt.readUInt32BE(16),64);assert.equal(crt.readUInt16BE(22),32);
assert.equal(crt.subarray(32,64).toString('ascii').replace(/\0+$/,''),'MHS NESVM');
const chips=[];for(let p=64;p<crt.length;){assert.equal(crt.subarray(p,p+4).toString(),'CHIP');const len=crt.readUInt32BE(p+4),bank=crt.readUInt16BE(p+10),address=crt.readUInt16BE(p+12),size=crt.readUInt16BE(p+14);assert.equal(len,0x2010);assert.equal(size,0x2000);chips.push({bank,address,data:crt.subarray(p+16,p+len)});p+=len;}
assert.deepEqual(chips.map(c=>[c.bank,c.address]),[[0,0x8000],[0,0xa000],[1,0x8000]]);assert.ok(!chips.some(c=>c.bank===58));
const descriptor=chips[2].data.subarray(0,128);assert.equal(descriptor.subarray(0,4).toString(),'N6D1');assert.equal(descriptor[4],1);assert.equal(descriptor[5],128);assert.equal(descriptor[6]&3,3);
const cstr=(offset,n)=>descriptor.subarray(offset,offset+n).toString().replace(/\0.*$/s,'');
assert.equal(cstr(16,32),'/NESVM/ROMS');assert.equal(cstr(48,32),'/NESVM/SAVES');assert.equal(cstr(80,32),'NES-INPUT-V1');assert.equal(cstr(112,12),'NES-SID-V1');assert.equal(descriptor.readUInt32LE(124),crc32(descriptor.subarray(0,124)));
const {buildMpe3TitleTerminal}=await loadNesTerminal(agi);const terminal=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false,diagnosticTitle:terminalManifest.diagnosticTitle,diagnosticFooter:terminalManifest.diagnosticFooter});
assert.equal(digest(terminal.prg),terminalManifest.terminalPrgSha256);assert.deepEqual(chips[0].data.subarray(0xfd,0xfd+terminal.prg.length-2),terminal.prg.subarray(2));
assert.ok(terminal.labels.nes_capture_input);assert.ok(terminal.labels.nes_queue_buttons);assert.ok(terminal.labels.apply_sid);assert.ok(terminal.labels.clear_sid);assert.equal(NES_INPUT.protocol,0x81);assert.ok(terminal.codeEnd<=terminal.stageAddress);
console.log(JSON.stringify({passed:true,crt:crtPath,bytes:crt.length,sha256:digest(crt),chips:chips.length,title:'MHS NESVM',descriptor:'N6D1',romDirectory:'/NESVM/ROMS',saveDirectory:'/NESVM/SAVES',inputProtocol:'NES-INPUT-V1',audioProtocol:'NES-SID-V1',audioPacketBytes:26,basicSid:true,sharpDefault:true,terminalBytes:terminal.prg.length,codeEnd:terminal.codeEnd,stageAddress:terminal.stageAddress,scope:'CRT structure, source identity and embedded C64 payload; firmware integration and physical hardware are checked separately'},null,2));

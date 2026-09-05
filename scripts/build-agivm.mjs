// Build only AGIVM. No firmware rebuild, version bump, flashing or game publication.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
import {buildMpe3TitleTerminal} from '../vm/client/host/mpe3-title-terminal.mjs';import {buildCartridgeBootBank} from '../vm/client/host/install-boot-bank.mjs';
import {crc32,encodeFixture} from '../agi/tools/agi_content.mjs';
const root=path.resolve(import.meta.dirname,'..'),out=path.join(root,'build/agivm'),pkg=path.join(out,'SD/VMS/AGIVM');
const arm=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),read=p=>fs.readFileSync(p);
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b);};
function run(exe,args){const r=spawnSync(exe,args,{cwd:root,windowsHide:true,encoding:'utf8',maxBuffer:4*1024*1024});if(r.status!==0)throw Error((r.error??'')+(r.stdout+r.stderr).slice(-6000));return r.stdout+r.stderr;}
function inputs(){const files=[];function walk(dir){for(const e of fs.readdirSync(path.join(root,dir),{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())walk(p);else files.push({path:p.replaceAll('\\','/'),sha256:sha(read(path.join(root,p)))});}}
 for(const p of ['vm/agi','vm/client','vm/abi','engine/native-game','agi/tools','agi/tests','vm/tests/helpers'])walk(p);
 for(const p of ['vm/nes/font8x8.h','nes/tools/build_nesvm_cartridge.mjs','Source/Teensy/MinimalBoot/Common/VMABI.h','Source/Teensy/MinimalBoot/Common/VMRegistry.h','Source/Teensy/MinimalBoot/Common/VMFiles.h','scripts/build-agivm.mjs','scripts/verify-agivm.mjs','vm/tests/agi_module_test.cpp','vm/tests/fake_sd.h','vm/tests/image_test.cpp',...fs.readdirSync(path.join(root,'firmware')).filter(p=>/^MPE_Firmware-V[0-9.]+[.]hex$/.test(p)).map(p=>'firmware/'+p)])files.push({path:p,sha256:sha(read(path.join(root,p)))});
 return files.sort((a,b)=>a.path.localeCompare(b.path));}
const snapshot=inputs();fs.mkdirSync(out,{recursive:true});
const elf=path.join(out,'agivm.elf');run(arm+'g++.exe',['-std=c++17','-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard','-Os','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage','-nostartfiles','-T','vm/abi/module.ld','-Wl,--gc-sections','-Wl,-Map='+path.join(out,'agivm.map'),'vm/agi/agivm.cpp','-lc','-lm','-lgcc','-o',elf]);
const nm=run(arm+'nm.exe',['-n',elf]);write(path.join(out,'agivm.nm'),nm);assert.ok(!run(arm+'nm.exe',['-u',elf]).trim());assert.ok(!nm.includes('_GLOBAL__sub_I'));
for(const section of ['text','data'])run(arm+'objcopy.exe',['-O','binary','--only-section=.'+section,elf,path.join(out,section+'.bin')]);
const sizes=run(arm+'size.exe',['-A',elf]),code=read(path.join(out,'text.bin')),data=read(path.join(out,'data.bin')),bss=Number(sizes.match(/^\.bss\s+(\d+)/m)[1]);write(path.join(out,'size.txt'),sizes);
assert.ok(code.length<=98304&&data.length+bss<196608);const entry=parseInt(nm.match(/^([0-9a-f]+) T vm_entry$/m)[1],16)|1;
const h=Buffer.alloc(64);[0x314d564d,2,64,code.length,data.length,bss,entry,0x18000,0x20014000,31,crc32(Buffer.concat([code,data])),0].forEach((v,i)=>h.writeUInt32LE(v>>>0,i*4));h.writeUInt32LE(crc32(h),44);
write(path.join(pkg,'engine.mvm'),Buffer.concat([h,code,data]));write(path.join(pkg,'manifest.vmi'),'VM1\nAGIVM\nagi\nengine.mvm\nclient.crt\nEND\n');
// Packet-only AGI keeps the published V1.1.1 client byte-for-byte; optional
// DMA timing negotiation belongs to clients which use that newer service.
const options={gameplay:true,enable1351Mouse:true,publishVideoTiming:false,diagnosticTitle:'MHS AGIVM - WAITING FOR HOST',diagnosticFooter:'KEYBOARD / P2 JOYSTICK / P1 MOUSE'};
const terminal=buildMpe3TitleTerminal(options),boot=buildCartridgeBootBank(terminal.prg,{loadingText:'AGIVM MODULAR 2',cartridgeFormat:'easyflash-1m'});
write(path.join(out,'agivm.prg'),terminal.prg);write(path.join(out,'boot.bin'),boot);
write(path.join(out,'client.json'),JSON.stringify({...options,terminalPrgSha256:sha(terminal.prg),codeEnd:terminal.codeEnd,stageAddress:terminal.stageAddress,labels:terminal.labels},null,2));
run(process.execPath,['nes/tools/build_nesvm_cartridge.mjs','--id','AGIVM','--boot-bank',path.join(out,'boot.bin'),'--output',path.join(pkg,'client.crt'),'--manifest',path.join(out,'cartridge.json')]);
write(path.join(out,'SD/AGIVM.crt'),read(path.join(pkg,'client.crt')));
// Authored tiny AGI program: text title + parser, no Sierra assets.
const init=[106,103,3,5,1,120,3,200,1],instructions=Buffer.from([255,1,200,0,255,init.length,0,...init,1,201,0]);
const title=Buffer.from('AGIVM IS RUNNING!\0','ascii'),logic=Buffer.alloc(2+instructions.length+5+title.length);
logic.writeUInt16LE(instructions.length);instructions.copy(logic,2);const m=2+instructions.length;logic[m]=1;logic.writeUInt16LE(4+title.length,m+1);logic.writeUInt16LE(4,m+3);title.copy(logic,m+5);
const fontText=read(path.join(root,'vm/nes/font8x8.h')).toString(),glyphs=[...fontText.matchAll(/\{\s*((?:0x[0-9a-fA-F]+\s*,?\s*){8})\}/g)];assert.equal(glyphs.length,128);
const font=Buffer.from(glyphs.flatMap(g=>g[1].match(/0x[0-9a-f]+/gi).map(s=>{let n=parseInt(s,16),r=0;for(let i=0;i<8;i++){r=(r<<1)|(n&1);n>>=1;}return r;})));
const fixture=encodeFixture([{type:0,id:0,data:logic},{type:4,id:0,data:Buffer.from([0,0])},{type:5,id:0,data:Buffer.from([1,0,1,0,4,108,111,111,107])},{type:6,id:0,data:font}]);
write(path.join(pkg,'GAMES/AGITEST.AGI'),fixture);
assert.deepEqual(inputs(),snapshot,'Sources changed during build');write(path.join(out,'build-inputs.json'),JSON.stringify({files:snapshot},null,2));
write(path.join(out,'module.json'),JSON.stringify({abi:2,firmwareVersion:'1.1.1',firmwareUnchanged:true,codeBytes:code.length,dataBytes:data.length,bssBytes:bss,workspaceBytes:196608-data.length-bss,guestRamBytes:524288,sha256:sha(read(path.join(pkg,'engine.mvm')))},null,2));
console.log(`AGIVM: ${code.length} RAM1 code bytes, ${data.length+bss} static bytes; existing V1.1.1 firmware unchanged`);

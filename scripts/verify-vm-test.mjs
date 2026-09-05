import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {assertGuiFirmwareVersion} from './firmware-version.mjs';
const version=assertGuiFirmwareVersion();
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),out=path.resolve(root,process.env.MPE_VM_TEST_OUT??'build/vm-test');
const tool=path.join(root,'build/toolchain'),arm=path.join(tool,'Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const acme=path.join(tool,'acme-0.97-r20/acme0.97win/acme/acme.exe');
const compiler='C:/msys64/mingw64/bin/g++.exe';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const env={...process.env,ACME_EXE:acme,PATH:path.dirname(compiler)+';'+process.env.PATH};
const logs=[];
const buildInputs=JSON.parse(fs.readFileSync(path.join(out,'build-inputs.json')));
assert.equal(buildInputs.mode,'all','Run a complete matched build before verification');
for(const input of buildInputs.files)assert.equal(sha(fs.readFileSync(path.join(root,input.path))),input.sha256,'Built source drift: '+input.path);
function run(exe,args,cwd=root){const r=spawnSync(exe,args,{cwd,env,encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});if(r.error||r.status)throw Error((r.error??'')+'\n'+(r.stdout+r.stderr).slice(-9000));return r.stdout+r.stderr;}
function native(name,args){const exe=path.join(out,name+'.exe');run(compiler,['-std=c++17','-O2','-static','-I',root,path.join(root,'vm/tests',name+'.cpp'),'-o',exe]);const log=run(exe,args);logs.push(log);console.log(log.trim());return exe;}
native('image_test',[path.join(out,'SD/VMS/NESVM/engine.mvm')]);
logs.push(run(path.join(out,'image_test.exe'),[path.join(out,'SD/VMS/DOSVM/engine.mvm')]));
native('mpe_video_live_test',[path.join(out,'kernel')]);native('indexed_host_test',[]);
native('dos_video_test',[]);
logs.push(run(process.execPath,['nes/tests/video_controls.mjs']));
logs.push(run(process.execPath,['dos/tests/shared_video_test.mjs']));
for(const standard of ['pal','ntsc'])for(const variant of ['full','mixed'])for(const transport of ['legacy','stream'])
 logs.push(run(process.execPath,['nes/tests/video_raster.mjs',out,standard,variant,transport]));
const pickerWire=path.join(out,'nes-picker-wire.bin');
const moduleTest=native('module_test',[path.join(root,'nes/DEMO/Crossbow.nes'),'--picker-wire',pickerWire]);logs.push(run(moduleTest,[path.join(root,'nes/DEMO/Crossbow.nes'),'direct']));
native('picker_scheduler_test',[path.join(root,'nes/DEMO/Crossbow.nes')]);
native('nes_timing_test',[path.join(root,'nes/DEMO/Crossbow.nes')]);
native('nofrendo_test',[path.join(root,'nes/DEMO/Crossbow.nes')]);
const pickerLog=run(process.execPath,['nes/tests/picker_idle.mjs',pickerWire,path.join(out,'nesvm.prg'),path.join(out,'client.json'),path.join(out,'nes-picker-input.json')]);logs.push(pickerLog);console.log(pickerLog.trim());
native('registry_test',[path.join(out,'SD'),fs.mkdtempSync(path.join(out,'registry-sandbox-'))]);
native('files_test',[fs.mkdtempSync(path.join(out,'files-sandbox-'))]);
native('dos_module_test',[path.join(out,'SD'),fs.mkdtempSync(path.join(out,'dos-sandbox-'))]);
logs.push(run(process.execPath,['nes/tools/nes.mjs','test']));
for(const standard of ['ntsc','pal'])logs.push(run(process.execPath,['nes/tests/nes_c64_boot_test.mjs','--crt',path.join(out,'SD/NESVM.crt'),'--manifest',path.join(out,'client.json'),'--out',path.join(out,'vice-'+standard),'--standard',standard]));
for(const standard of ['ntsc','pal'])logs.push(run(process.execPath,['dos/tests/mpe5_c64_boot_test.mjs','--crt',path.join(out,'SD/DOSVM.crt'),'--manifest',path.join(out,'dos-client.json'),'--out',path.join(out,'dos-vice-'+standard),'--standard',standard]));
logs.push(run(process.execPath,['--test','dos/tests/mpe5_packet_recovery_test.mjs']));
const speaker=path.join(out,'dos-speaker.exe');
run(compiler,['-std=c++17','-O2','-static','dos/tests/mpe5_speaker_test.cpp','engine/native-dos/mpe5_platform.cpp','engine/native-dos/mpe5_speaker.cpp','-o',speaker]);logs.push(run(speaker,[]));
const video=path.join(out,'dos-video.exe');
run(compiler,['-std=c++17','-O2','-static','dos/tests/mpe5_video_test.cpp','engine/native-dos/mpe5_paged_memory.cpp','engine/native-dos/mpe5_video.cpp','-o',video]);
logs.push(run(video,['engine/native-dos/vendor/8086tiny/bios','dos/sd-card/DOSVM/DOSVM.IMG',path.join(out,'dos-video')]));
const guiTests=['geos-launch-routing.test.js','geos-browser.test.js','geos-firmware-startup.test.js','geos-settings.test.js'];
logs.push(run(process.execPath,['--test',...guiTests.map(p=>'Source/C64/MainMenuCRT/tests/'+p)]));
const symbols=(elf)=>run(arm+'nm.exe',['-n','-C',elf]);
const min=symbols(path.join(out,'minimal/MinimalBoot.ino.elf')),gui=symbols(path.join(out,'gui/Teensy.ino.elf'));
const symbol=(s,n)=>{const m=s.match(new RegExp('^([0-9a-f]+) \\w '+n+'$','m'));assert.ok(m,'Missing '+n);return parseInt(m[1],16);};
assert.ok(!/MPE[4567]|AGIPicture|nes::|doomgeneric|doom::/.test(min+gui),'Engine leaked into firmware');
assert.equal(symbol(min,'_itcm_block_count'),6);assert.equal(symbol(min,'_flexram_bank_config'),0xaaaaafff);
assert.equal(symbol(min,'_estack'),0x20050000);assert.ok(symbol(min,'_etext')<=0x18000);
const stack=symbol(min,'_estack')-symbol(min,'_vm_data_end');assert.ok(stack>=49152);
assert.equal(symbol(min,'_vm_data_start'),0x20014000);assert.equal(symbol(min,'_vm_data_end'),0x20044000);
assert.ok(symbol(min,'_heap_start')>=0x20000000&&symbol(min,'_heap_end')<=0x20014000);
const size=run(arm+'size.exe',['-A',path.join(out,'minimal/MinimalBoot.ino.elf')]);
assert.equal(Number(size.match(/^\.bss.dma\s+(\d+)/m)?.[1]??0),0);assert.equal(Number(size.match(/^\.bss.extram\s+(\d+)/m)?.[1]??0),0);
for(const name of ['VMHostIO2','IO2Hndlr_EasyFlash','isrPHI2']){
  const m=min.match(new RegExp('^([0-9a-f]+) [Tt] '+name+'(?:\\(|$)','m'));assert.ok(m,name+' not found');assert.ok(parseInt(m[1],16)<0x18000,name+' not in host ITCM');
}
const moduleSymbols=symbols(path.join(out,'nesvm.elf'));assert.ok(!/_GLOBAL__sub_I/.test(moduleSymbols),'Module needs unsupported constructors');
assert.ok(moduleSymbols.includes('nes::NofrendoMachine::run_cycles'));
assert.ok(!moduleSymbols.includes('m6502_tick'),'Reference cycle CPU leaked into released module');
assert.ok(JSON.parse(fs.readFileSync(path.join(out,'module.json'))).workspaceBytes>=163840,'NES tested workspace floor');
assert.ok(!run(arm+'nm.exe',['-u',path.join(out,'nesvm.elf')]).trim());
for(const stem of ['nesvm','dosvm']){
 const elf=path.join(out,stem+'.elf'),sym=symbols(elf);assert.ok(!/_GLOBAL__sub_I/.test(sym));assert.ok(!run(arm+'nm.exe',['-u',elf]).trim());
 const size=run(arm+'size.exe',['-A',elf]);
 for(const section of ['data','bss']){const m=size.match(new RegExp('^\\.'+section+'\\s+(\\d+)\\s+(\\d+)','m'));assert.ok(m);assert.ok(Number(m[2])>=0x20014000&&Number(m[2])+Number(m[1])<=0x20044000);}
}
// Build a differently-sized, engine-free diagnostic module with the same ABI.
// It is test-only and never installed in the NES-only SD tree.
const diagElf=path.join(out,'diagnostic.elf');
run(arm+'g++.exe',['-std=c++17','-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard','-Os','-ffunction-sections','-fdata-sections','-fno-exceptions','-fno-rtti','-nostartfiles','-T',path.join(root,'vm/abi/module.ld'),'-Wl,--gc-sections',path.join(root,'vm/tests/diagnostic_module.cpp'),'-o',diagElf]);
assert.ok(!run(arm+'nm.exe',['-u',diagElf]).trim());
const crc32=b=>{let c=0xffffffff;for(const v of b){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;};
for(const s of ['text','data'])run(arm+'objcopy.exe',['-O','binary','--only-section=.'+s,diagElf,path.join(out,'diagnostic-'+s+'.bin')]);
const diagCode=fs.readFileSync(path.join(out,'diagnostic-text.bin')),diagData=fs.readFileSync(path.join(out,'diagnostic-data.bin'));
const diagSizes=run(arm+'size.exe',['-A',diagElf]),diagBss=Number(diagSizes.match(/^\.bss\s+(\d+)/m)?.[1]??0);
const diagHeader=Buffer.alloc(64);[0x314d564d,2,64,diagCode.length,diagData.length,diagBss,symbol(symbols(diagElf),'vm_entry')|1,0x18000,0x20014000,23,crc32(Buffer.concat([diagCode,diagData])),0].forEach((v,i)=>diagHeader.writeUInt32LE(v>>>0,i*4));diagHeader.writeUInt32LE(crc32(diagHeader),44);
fs.writeFileSync(path.join(out,'diagnostic.mvm'),Buffer.concat([diagHeader,diagCode,diagData]));
logs.push(run(path.join(out,'image_test.exe'),[path.join(out,'diagnostic.mvm')]));
const artifacts=[];const sd=path.join(out,'SD');
function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,entry.name);if(entry.isDirectory())walk(p);else artifacts.push({path:path.relative(sd,p).replaceAll('\\','/'),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))});}}
walk(sd);artifacts.sort((a,b)=>a.path.localeCompare(b.path));
const support=['DOSVM.IMG','D/README.TXT',...['UPDDOS.BAT','FDCONFIG.SYS','CONFIG.SYS','AUTOEXEC.BAT','CGA80.COM','EDIT.EXE','EDIT.HLP'].map(p=>'D/DOSVMUPD/'+p)];
assert.deepEqual(artifacts.map(x=>x.path).sort(),[version.filename,'NESVM.crt','VMS/NESVM/client.crt','VMS/NESVM/engine.mvm','VMS/NESVM/manifest.vmi','VMS/NESVM/ROMS/Crossbow.nes',
 'DOSVM.crt','VMS/DOSVM/client.crt','VMS/DOSVM/engine.mvm','VMS/DOSVM/manifest.vmi','VMS/DOSVM/bios.bin',...support.map(p=>'VMS/DOSVM/'+p)].sort());
for(const p of support)assert.equal(sha(fs.readFileSync(path.join(sd,'VMS/DOSVM',p))),sha(fs.readFileSync(path.join(root,'dos/sd-card/DOSVM',p))),'Source disk template changed: '+p);
assert.equal(sha(fs.readFileSync(path.join(sd,'VMS/DOSVM/bios.bin'))),sha(fs.readFileSync(path.join(root,'engine/native-dos/vendor/8086tiny/bios'))));
assert.equal(sha(fs.readFileSync(path.join(sd,'DOSVM.crt'))),sha(fs.readFileSync(path.join(sd,'VMS/DOSVM/client.crt'))));
assert.equal(artifacts.find(x=>x.path==='NESVM.crt').sha256,artifacts.find(x=>x.path==='VMS/NESVM/client.crt').sha256);
assert.equal(artifacts.find(x=>x.path.endsWith('Crossbow.nes')).sha256,'93c1eff05b4d39992c0fd05dce9bb3d5b8349ca3a2416717d75ef4336fc715ea');
const videoProof=Object.fromEntries(['pal','ntsc'].flatMap(s=>['full','mixed'].map(v=>[v+'-'+s,JSON.parse(fs.readFileSync(path.join(out,'raster-'+v+'-'+s,'result.json')))])));
for(const s of ['pal','ntsc'])for(const v of ['full','mixed'])videoProof[v+'-'+s+'-stream']=JSON.parse(fs.readFileSync(path.join(out,'raster-'+v+'-'+s+'-stream','result.json')));
const report={version:version.version,profile:version.releaseId,physicalAcceptance:false,videoProof,priorHardwareReport:'ABI1 NES SMB launches but severe slowdown and visible line-block drawing; fast DMA/indexed candidate physical acceptance pending',hostStackBudgetBytes:stack,hostRam2StaticBytes:0,hostHeapBytes:symbol(min,'_heap_end')-symbol(min,'_heap_start'),module:JSON.parse(fs.readFileSync(path.join(out,'module.json'))),dosModule:JSON.parse(fs.readFileSync(path.join(out,'dos-module.json'))),artifacts,
  passed:['MVM1 malformed image and integrity checks for both modules','actual NES module menu, page, immutable ACK lifecycle, exact direct selection and Crossbow 120 presented frames','generic registry/preflight negative tests','generic host storage service negative tests','actual DOS module boots FreeDOS with 512KiB guest RAM, C/D writes and Tandy modes 08/09','Tandy three-voice speaker and full CGA/Tandy/Boulder renderer regressions','164369 portable NES checks','both C64 clients reset/START/timeout in VICE PAL and NTSC','12 DOS packet-retry fault tests','30 focused assembled GUI checks','both engines have RAM1 text/data/BSS; no engines or RAM2 static data in host','independent diagnostic ARM module links'],
  indexedVideoPassed:['native indexed producer and frozen Busy retries','host configuration/range checks and DMA failure release','exact direct-selection chords, Shift/multi-key exclusion and F3 ghost suppression','enhanced to Sharp to Default receiver transitions','PAL/NTSC full and mixed plans, all seven splits, 2412 timed writes and bitmap row/color checks'],
  pending:['physical GUI boot/update/reset','physical DOS speed, keyboard, Tandy and save tests','physical NES speed/input and all four modes','enhanced-mode cadence, blanking and leftmost 24-pixel FLI artifact acceptance','long-run transport stress','old field crash root cause confirmation','future flash/XIP module profiles']};
fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(out,'verification.log'),logs.join('\n'));
// System.IO ZIP avoids requiring a global archiver and does not include logs,
// toolchains, private ROMs, launch records or any other VM package.
const zip=path.join(out,'MODULAR-VM-TEST-V'+version.version+'.zip');
if(fs.existsSync(zip))fs.unlinkSync(zip); // exact generated file in this build directory
const ps=`Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory('${sd.replaceAll("'","''")}','${zip.replaceAll("'","''")}')`;
run('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps]);
console.log(`PASS: matched NES/DOS kit, one engine at a time; host stack budget ${stack} bytes; ${zip}`);

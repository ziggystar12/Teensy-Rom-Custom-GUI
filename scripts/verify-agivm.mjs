// Focused AGI-only gate. Existing firmware/downloads are inputs, never rewritten.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
import {dialogFixture} from '../agi/tests/dialog_fixture.mjs';
import {saveSlotsFixture} from '../agi/tests/save_slots_fixture.mjs';
const root=path.resolve(import.meta.dirname,'..'),out=path.join(root,'build/agivm'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const inputs=JSON.parse(fs.readFileSync(path.join(out,'build-inputs.json'))).files;
const fresh=()=>{for(const f of inputs)assert.equal(sha(fs.readFileSync(path.join(root,f.path))),f.sha256,'Stale build: '+f.path);};fresh();
const logs=[];function run(exe,args){const r=spawnSync(exe,args,{cwd:root,env:{...process.env,PATH:'C:/msys64/mingw64/bin;'+process.env.PATH},windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});logs.push(r.stdout+r.stderr);if(r.status!==0)throw Error((r.error??'')+(r.stdout+r.stderr).slice(-6000));return r.stdout.trim();}
const cc='C:/msys64/mingw64/bin/g++.exe',exe=path.join(out,'agi_module_test.exe');
run(cc,['-std=c++17','-O2','-funsigned-char','-static','vm/tests/agi_module_test.cpp','-o',exe]);
run(cc,['-std=c++17','-O2','-static','vm/tests/image_test.cpp','-o',path.join(out,'image_test.exe')]);
run(path.join(out,'image_test.exe'),[path.join(out,'SD/VMS/AGIVM/engine.mvm')]);
const fixtureSandbox=fs.mkdtempSync(path.join(out,'agi-sandbox-'));
console.log(run(exe,[path.join(out,'SD'),fixtureSandbox]));
console.log(run(process.execPath,['agi/tests/picker_idle.mjs',path.join(fixtureSandbox,'wire.bin'),path.join(out,'agivm.prg'),path.join(out,'client.json')]));
console.log(run(process.execPath,['agi/tests/replay_wire.mjs',path.join(fixtureSandbox,'wire.bin'),path.join(out,'agivm.prg'),path.join(fixtureSandbox,'wire-result.json')]));
const dialogSandbox=fs.mkdtempSync(path.join(out,'agi-sandbox-dialog-')),dialogContent=path.join(dialogSandbox,'DIALOG.AGI');
fs.writeFileSync(dialogContent,dialogFixture(fs.readFileSync(path.join(out,'SD/VMS/AGIVM/GAMES/AGITEST.AGI'))));
console.log(run(exe,[path.join(out,'SD'),dialogSandbox,dialogContent,'--dialogs']));
console.log(run(process.execPath,['agi/tests/replay_wire.mjs',path.join(dialogSandbox,'wire.bin'),path.join(out,'agivm.prg'),path.join(dialogSandbox,'wire-result.json'),path.join(dialogSandbox,'dialog-frames.json')]));
run(process.execPath,['--test','--test-reporter=dot','agi/tests/content.test.mjs','agi/tests/keyboard.test.mjs']);
const savesSandbox=fs.mkdtempSync(path.join(out,'agi-sandbox-saves-')),savesContent=path.join(savesSandbox,'SAVES.AGI');
fs.writeFileSync(savesContent,saveSlotsFixture(fs.readFileSync(path.join(out,'SD/VMS/AGIVM/GAMES/AGITEST.AGI'))));
console.log(run(exe,[path.join(out,'SD'),savesSandbox,savesContent,'--saves']));
console.log(run(process.execPath,['agi/tests/replay_wire.mjs',path.join(savesSandbox,'wire.bin'),path.join(out,'agivm.prg'),path.join(savesSandbox,'wire-result.json')]));
const games=[];for(const name of process.argv.slice(2)){
 const file=path.resolve(name),before=sha(fs.readFileSync(file)),sandbox=fs.mkdtempSync(path.join(out,'agi-sandbox-'));
 console.log(run(exe,[path.join(out,'SD'),sandbox,file]));
 const replay=path.join(sandbox,'wire-result.json');console.log(run(process.execPath,['agi/tests/replay_wire.mjs',path.join(sandbox,'wire.bin'),path.join(out,'agivm.prg'),replay]));
 assert.equal(sha(fs.readFileSync(file)),before);games.push({name:path.basename(file),sha256:before,replay:JSON.parse(fs.readFileSync(replay))});
}
for(const standard of ['pal','ntsc'])run(process.execPath,['agi/tests/c64_boot_test.mjs','--standard',standard,'--out',path.join(out,'vice-'+standard)]);
const arm=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-'),elf=path.join(out,'agivm.elf');
assert.ok(!run(arm+'nm.exe',['-u',elf]));const sizes=run(arm+'size.exe',['-A',elf]);
for(const section of ['text','data','bss']){const m=sizes.match(new RegExp('^\\.'+section+'\\s+(\\d+)\\s+(\\d+)','m'));assert.ok(m);const begin=section==='text'?0x18000:0x20014000,end=section==='text'?0x30000:0x20044000;assert.ok(Number(m[2])>=begin&&Number(m[2])+Number(m[1])<=end);}
const artifacts=[];function walk(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())walk(p);else artifacts.push({path:path.relative(path.join(out,'SD'),p).replaceAll('\\','/'),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))});}}walk(path.join(out,'SD'));
assert.deepEqual(artifacts.map(a=>a.path).sort(),['AGIVM.crt','VMS/AGIVM/GAMES/AGITEST.AGI','VMS/AGIVM/client.crt','VMS/AGIVM/engine.mvm','VMS/AGIVM/manifest.vmi'].sort());fresh();
const report={passed:true,physicalAcceptance:false,firmwareVersion:'1.1.1',firmwareUnchanged:true,module:JSON.parse(fs.readFileSync(path.join(out,'module.json'))),artifacts,games,
 checks:['real module idle picker frame ends without cells; keyboard/joystick picker/paging/fire/release/no blank and corrupt-file error','exact C64 main-loop input after an idle picker display using native module packets','centered/timed dialog open/close stays visible in exact C64 replay; low dialogs and authored mode changes remain intact','generic registry direct AGI routing','RAM1 support and RAM2 state/cache guards','parser/edit, pointer and held input','save/readback and write/flush failures','immutable frame/packet lifecycle','content and actual-6510 keyboard/mouse tests','ARM image bounds/imports/integrity','PAL/NTSC actual CRT reset/start/timeout in VICE'],
 pending:['physical launcher/direct-game startup','physical gameplay speed, keyboard/joystick/mouse and audio','long-run game/crash and stack high-water acceptance']};
fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(out,'verification.log'),logs.join('\n'));
console.log('PASS AGIVM: independent module/client/content; firmware V1.1.1 unchanged; physical gameplay awaits hardware');

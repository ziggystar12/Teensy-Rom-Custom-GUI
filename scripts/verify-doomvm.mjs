// SPDX-License-Identifier: GPL-2.0-or-later
// Focused host, legacy-image, package and C64 receiver checks; no physical claims.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {loadDosTerminal} from '../dos/tools/dos_terminal.mjs';
const root=fs.realpathSync(path.resolve(import.meta.dirname,'..'));
const built=path.resolve(root,process.argv[2]??'build/gba'),out=path.join(root,'build/doom/e1m1-test');
const audit=JSON.parse(fs.readFileSync(path.join(root,'build/doom/gbadoom-audit/report.json')));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const input=JSON.parse(fs.readFileSync(path.join(built,'build-inputs.json')));
for(const p of [...input.files,...audit.inputs])assert.equal(sha(path.join(root,p.path)),p.sha256,'Input drift: '+p.path);
const kit=JSON.parse(fs.readFileSync(path.join(out,'package.json')));
for(const p of kit.artifacts)assert.equal(sha(path.join(out,'SD',p.path)),p.sha256);
const compiler='C:/msys64/mingw64/bin/g++.exe';
const env={...process.env,PATH:path.dirname(compiler)+';'+process.env.PATH};
const logs=[];
function run(exe,args){const p=spawnSync(exe,args,{cwd:root,env,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:8*1024*1024});
    assert.ifError(p.error);assert.equal(p.status,0,(p.stdout+p.stderr).slice(-4500));return p.stdout+p.stderr;}
function native(name,args){const exe=path.join(out,name+'.exe');run(compiler,['-std=c++17','-O2','-static','-I',root,'vm/tests/'+name+'.cpp','-o',exe]);
    const log=run(exe,args);logs.push(log);console.log(log.trim());return exe;}
const oldImage=native('image_test',[path.join(built,'SD/VMS/NESVM/engine.mvm')]);
logs.push(run(oldImage,[path.join(built,'SD/VMS/DOSVM/engine.mvm')]));
native('indexed_host_test',[]);
native('registry_test',[path.join(built,'SD'),fs.mkdtempSync(path.join(out,'registry-sandbox-legacy-'))]);
native('doom_registry_test',[path.join(out,'SD'),fs.mkdtempSync(path.join(out,'registry-sandbox-doom-'))]);
native('dos_module_test',[path.join(built,'SD'),fs.mkdtempSync(path.join(out,'dos-sandbox-'))]);
const arm=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const elf=path.join(built,'minimal/MinimalBoot.ino.elf');
const symbols=run(arm+'nm.exe',['-n','-C',elf]);
const symbol=n=>{const v=symbols.match(new RegExp('^([0-9a-f]+) \\w '+n+'$','m'));assert.ok(v,n);return parseInt(v[1],16);};
assert.equal(symbol('_itcm_block_count'),6);assert.equal(symbol('_flexram_bank_config'),0xaaaaafff);
assert.ok(symbol('_etext')<=0x18000);assert.ok(symbol('_heap_end')<=0x20014000);
assert.equal(symbol('_estack'),0x20050000);assert.equal(symbol('_vm_data_end'),0x20044000);
const sizes=run(arm+'size.exe',['-A',elf]);
for(const section of ['bss.dma','bss.extram'])assert.equal(Number(sizes.match(new RegExp('^\\.'+section+'\\s+(\\d+)','m'))?.[1]??0),0);
assert.ok(!/GbaCore|gbadoomvm|doomgeneric/.test(symbols),'Doom leaked into firmware');
const host={textBytes:symbol('_etext'),heapStart:symbol('_heap_start'),heapEnd:symbol('_heap_end'),
    stackReservedBytes:symbol('_estack')-symbol('_vm_data_end'),ram2StaticBytes:0};
// Boot the actual Doom client in VICE, then pass a real core-generated sound
// packet through its assembled SID routine. VICE's SID register writes and
// subsequent silence are observed, not an audio-quality or Teensy bus test.
const manifest=JSON.parse(fs.readFileSync(path.join(out,'client.json')));
const {buildMpe3TitleTerminal,MPE3_TITLE_TERMINAL_STATE:state}=await loadDosTerminal(path.join(root,'vm/client'));
const terminal=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false,
    diagnosticTitle:manifest.diagnosticTitle,diagnosticFooter:manifest.diagnosticFooter});
assert.equal(crypto.createHash('sha256').update(terminal.prg).digest('hex'),manifest.terminalPrgSha256);
const sound=Buffer.from(audit.constrained.soundSample,'hex');assert.equal(sound.length,26);assert.equal(sound[25],15);
const vice=path.resolve(root,'../AGI-64/tools/VICE-3.10/GTK3VICE-3.10-win64/bin/x64sc.exe');
const receiver=[];
for(const standard of ['pal','ntsc']){
    const dir=path.join(out,'sid-'+standard);fs.mkdirSync(dir,{recursive:true});
    const file=n=>path.join(dir,n).replaceAll('\\','/'),hex=n=>n.toString(16).padStart(4,'0');
    const bytes=b=>[...b].map(n=>n.toString(16).padStart(2,'0')).join(' ');
    const save=(name,lo,hi)=>`bsave "${file(name)}" 0 $${hex(lo)} $${hex(hi)}`;
    const play=(id,name)=>`command ${id} "playback \\"${file(name)}\\""`;
    const write=(name,lines)=>fs.writeFileSync(file(name),lines.join('\n')+'\n');
    write('reset.mon',['bank cpu','break $0810',play(1,'sound.mon'),'x']);
    write('sound.mon',['disable 1',save('program.bin',0x0801,terminal.codeEnd-1),
        `> ${hex(state.skipSent)} 00`,`> ${hex(terminal.stageAddress+6)} 1a`,
        `> ${hex(terminal.stageAddress+8)} ${bytes(sound)}`,
        `> 3000 20 ${bytes(Buffer.from([terminal.labels.apply_sid&255,terminal.labels.apply_sid>>8]))} ea`,
        'break $3003',play(2,'silence.mon'),'g $3000']);
    write('silence.mon',['disable 2',save('sound.bin',0xd400,0xd418),
        `> ${hex(terminal.stageAddress+8)} ${bytes(Buffer.alloc(26))}`,
        'break $3003',play(3,'done.mon'),'g $3000']);
    write('done.mon',['disable 3',save('silence.bin',0xd400,0xd418),'quit']);
    const p=spawnSync(vice,['-default','-'+standard,'-console','-directory',path.dirname(path.dirname(vice)),
        '-initbreak','reset','-warp','+sound','+easyflashcrtwrite','-cartcrt',path.join(out,'SD/DOOMVM.crt'),
        '-moncommands',file('reset.mon'),'-monlog','-monlogname',file('monitor.log'),'-limitcycles','40000000'],
        {cwd:dir,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:4*1024*1024});
    fs.writeFileSync(file('vice.log'),(p.stdout??'')+(p.stderr??''));assert.ifError(p.error);assert.equal(p.status,0,'VICE: '+dir);
    assert.deepEqual(fs.readFileSync(file('program.bin')),terminal.prg.subarray(2));
    assert.deepEqual(fs.readFileSync(file('sound.bin')),sound.subarray(1));
    assert.deepEqual(fs.readFileSync(file('silence.bin')),Buffer.alloc(25));
    receiver.push(standard+': Doom CRT boot, real SID packet and silence passed');
}
for(const p of [...input.files,...audit.inputs])assert.equal(sha(path.join(root,p.path)),p.sha256,'Input changed during verification: '+p.path);
fs.writeFileSync(path.join(out,'verification.log'),logs.join('\n'));
const result={host,legacyImages:true,legacyDosExecution:true,registry:true,receiver,hardwareTested:false,
    packageSha256:sha(path.join(out,'package.json')),auditSha256:sha(path.join(root,'build/doom/gbadoom-audit/report.json'))};
fs.writeFileSync(path.join(out,'verification.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));

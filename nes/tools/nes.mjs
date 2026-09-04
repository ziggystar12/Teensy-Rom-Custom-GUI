import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync,execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {deflateSync} from 'node:zlib';
import assert from 'node:assert/strict';

// CLI failures should show the actionable compiler/test message, not buffer dumps.
process.on('uncaughtException',error=>{
  console.error(error.stderr?.toString().trim()||error.message);
  process.exitCode=1;
});

const here=path.dirname(fileURLToPath(import.meta.url));
const nes=path.resolve(here,'..'),root=path.resolve(nes,'..'),build=path.join(nes,'build');
const core=path.join(root,'engine/native-nes');
const vendor='c8fb5979be406283db60ae5864da601cebb27dad2b114187a6dea2f90f8925dc';
const demo='93c1eff05b4d39992c0fd05dce9bb3d5b8349ca3a2416717d75ef4336fc715ea';
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const fileHash=p=>hash(fs.readFileSync(p));
const command=process.argv[2]??'test';
const opts={};
for(let i=3;i<process.argv.length;i+=2){
  const key=process.argv[i];
  if(!['--rom','--folder','--frames','--input','--name','--compiler','--sharp','--arm-compiler'].includes(key)||!process.argv[i+1]) throw new Error(`Unknown/incomplete option ${key}`);
  if(key in opts) throw new Error(`Duplicate ${key}`);
  opts[key]=process.argv[i+1];
}
fs.mkdirSync(build,{recursive:true});
assert.equal(fileHash(path.join(core,'vendor/chips/m6502.h')),vendor,'CPU vendor hash mismatch');
function auditRoms(){
  const tracked=execFileSync('git',['ls-files','-z','--','nes'],{cwd:root,encoding:'utf8',windowsHide:true}).split('\0').filter(Boolean);
  const allowed='nes/DEMO/Crossbow.nes';
  for(const f of tracked) {
    assert.ok(!/^nes\/(ROMS|build|work)\//i.test(f),`Private input/evidence was staged: ${f}`);
    assert.ok(!(/\.nes$/i.test(f))||f===allowed,`Forbidden tracked ROM: ${f}`);
  }
  const ignored=spawnSync('git',['check-ignore','-q','--','nes/ROMS/SMB.NES'],{cwd:root,windowsHide:true});
  assert.equal(ignored.status,0,'Private ROM folder must be ignored');
  if(fs.existsSync(path.join(nes,'DEMO/Crossbow.nes'))) assert.equal(fileHash(path.join(nes,'DEMO/Crossbow.nes')),demo,'Authorized demo identity changed');
}
auditRoms();
if(command==='audit') { console.log('ROM protection PASS: private library/evidence excluded; only the exact authorized demo is permitted.'); process.exit(0); }
const compiler=[opts['--compiler'],process.env.CXX,'C:\\msys64\\mingw64\\bin\\g++.exe','g++','clang++'].filter(Boolean)
  .find(cc=>spawnSync(cc,['--version'],{windowsHide:true}).status===0);
if(!compiler) throw new Error('Native C++17 compiler unavailable');
const sourceFiles=['nes_rom.cpp','nes_machine.cpp','nes_sid.cpp','nes_video.cpp'].map(f=>path.join(core,f));
const provenanceFiles=[...sourceFiles,...['nes_rom.h','nes_machine.h','nes_input.h','nes_sid.h','nes_video.h','vendor/chips/m6502.h'].map(f=>path.join(core,f)),path.join(here,'nes_host.cpp'),path.join(nes,'tests/nes_tests.cpp'),path.join(nes,'tests/nes_layout.cpp'),fileURLToPath(import.meta.url)];
const sources=Object.fromEntries(provenanceFiles.map(f=>[path.relative(root,f).replaceAll('\\','/'),fileHash(f)]));
function compile(test){
  const exe=path.join(build,`${test?'nes-tests':'nes-host'}${process.platform==='win32'?'.exe':''}`);
  const args=['-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-misleading-indentation','-I',core,...sourceFiles,test?path.join(nes,'tests/nes_tests.cpp'):path.join(here,'nes_host.cpp'),'-o',exe];
  if(process.platform==='win32') args.unshift('-static','-static-libgcc','-static-libstdc++');
  execFileSync(compiler,args,{cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,windowsHide:true,stdio:'pipe',timeout:60000});
  return exe;
}
function writeJson(name,value){fs.writeFileSync(path.join(build,name),JSON.stringify(value,null,2)+'\n');}
function invoke(exe,args){return spawnSync(exe,args,{windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});}
// PNG output is rendered directly from the emulator's RGB arrays, not a screen capture.
function crc32(bytes){let c=0xffffffff;for(const b of bytes){c^=b;for(let i=0;i<8;++i)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
function chunk(type,data){const t=Buffer.from(type);const n=Buffer.alloc(4);n.writeUInt32BE(data.length);const c=Buffer.alloc(4);c.writeUInt32BE(crc32(Buffer.concat([t,data])));return Buffer.concat([n,t,data,c]);}
function png(file,rgb,width,height){
  assert.equal(rgb.length,width*height*3);
  const header=Buffer.alloc(13);header.writeUInt32BE(width,0);header.writeUInt32BE(height,4);header[8]=8;header[9]=2;
  const rows=Buffer.alloc(height*(1+width*3));for(let y=0;y<height;++y)rgb.copy(rows,y*(1+width*3)+1,y*width*3,(y+1)*width*3);
  fs.writeFileSync(file,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]));
}
if(command==='layout'){
  const arm=opts['--arm-compiler']??path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-g++.exe');
  const dir=path.join(build,'arm');fs.mkdirSync(dir,{recursive:true});
  const armSources=[...sourceFiles,path.join(nes,'tests/nes_layout.cpp')];
  const flags=['-std=c++17','-Os','-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-ffunction-sections','-fdata-sections','-fstack-usage','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',core];
  for(const source of armSources) execFileSync(arm,[...flags,'-c',source,'-o',path.join(dir,path.basename(source,'.cpp')+'.o')],{windowsHide:true,stdio:'pipe',timeout:60000});
  const nm=path.join(path.dirname(arm),'arm-none-eabi-nm'+(process.platform==='win32'?'.exe':''));
  const symbols=execFileSync(nm,['-S','--radix=d',path.join(dir,'nes_layout.o')],{windowsHide:true,encoding:'utf8'});
  const sizes={};for(const line of symbols.split(/\r?\n/)){const m=line.match(/^\d+\s+(\d+)\s+\w\s+nes_size_(\w+)$/);if(m)sizes[m[2]]=Number(m[1]);}
  assert.equal(Object.keys(sizes).length,4,'ARM layout symbols missing');
  const base=sizes.machine+sizes.renderer+sizes.presented+sizes.sid;
  const result={scope:'Cortex-M7 object compilation and type sizes only; not a firmware link, runtime high-water, or hardware proof',sizes,
    nrom128Subtotal:base+16384+8192,nrom256Subtotal:base+32768+8192,demoSubtotal:base+32768+65536,
    subtotalExcludes:'loader, path, wire queues, extra APU work, arena alignment and stack',sources,compiler:arm};
  writeJson('arm-layout.json',result);const {sources:manifest,compiler:cc,...summary}=result;
  console.log(JSON.stringify({...summary,report:path.join(build,'arm-layout.json')},null,2));
} else if(command==='test'){
  const exe=compile(true),r=invoke(exe,[]);
  if(r.status!==0) throw new Error(r.stderr||r.error?.message||'tests failed');
  const result={...JSON.parse(r.stdout),compiler,sources,romProtection:true};
  writeJson('tests.json',result);console.log(JSON.stringify({...JSON.parse(r.stdout),romProtection:true,report:path.join(build,'tests.json')},null,2));
} else if(command==='scan'){
  const folder=path.resolve(opts['--folder']??path.join(nes,'ROMS'));
  const files=fs.readdirSync(folder,{withFileTypes:true}).filter(e=>e.isFile()&&/\.nes$/i.test(e.name))
    .map(e=>path.join(folder,e.name)).sort((a,b)=>a.localeCompare(b,undefined,{sensitivity:'base'}));
  const exe=compile(false),rows=[];
  for(let i=0;i<files.length;i+=32){const r=invoke(exe,['inspect',...files.slice(i,i+32)]);if(r.status!==0)throw new Error(r.stderr);rows.push(...JSON.parse(r.stdout));}
  const result={folder,count:rows.length,supported:rows.filter(r=>r.supported).length,files:rows,sources};
  writeJson('rom-inventory.private.json',result);
  console.log(JSON.stringify({count:result.count,supported:result.supported,mappers:rows.reduce((a,r)=>(a[r.mapper??'invalid']=(a[r.mapper??'invalid']??0)+1,a),{}),report:path.join(build,'rom-inventory.private.json')},null,2));
} else if(command==='run'){
  if(!opts['--rom']) throw new Error('--rom is required; no automatic game selection');
  const rom=path.resolve(opts['--rom']),before=fileHash(rom),name=opts['--name']??'latest';
  if(!/^[A-Za-z0-9_-]+$/.test(name))throw new Error('--name must contain only letters, digits, underscore or dash');
  const output=path.join(build,name),exe=compile(false);
  const r=invoke(exe,['run',rom,opts['--frames']??'180',opts['--input']??'',output,opts['--sharp']??'on']);
  assert.equal(fileHash(rom),before,'Source ROM changed');
  if(r.error) throw r.error;
  if(r.status!==0 && !r.stdout.startsWith('{'))throw new Error(r.stderr||'host run failed');
  const result={...JSON.parse(r.stdout),romSha256:before,input:opts['--input']??'',sources,compiler,executableSha256:fileHash(exe)};
  for(const suffix of ['','-vic']){
    const rgb=fs.readFileSync(path.join(output,`frame${suffix}.rgb`));
    png(path.join(output,`frame${suffix}.png`),rgb,suffix?result.displayWidth:256,suffix?200:240);
  }
  result.frameSha256=fileHash(path.join(output,'frame.idx')); result.vicSha256=fileHash(path.join(output,'frame.vic'));
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
  const {sources:manifest,compiler:cc,executableSha256:exeHash,...summary}=result;
  console.log(JSON.stringify({...summary,report:path.join(output,'result.json')},null,2));
  if(r.status!==0)process.exitCode=r.status;
} else throw new Error('Use test, layout, audit, scan, or run');

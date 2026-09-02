import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const options={source:null,out:null,crt:null,raw:null,compiler:process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++')};
for(let i=2;i<process.argv.length;i+=2) { const k=process.argv[i].slice(2);assert.ok(k in options&&process.argv[i+1]);options[k]=k==='compiler'?process.argv[i+1]:path.resolve(process.argv[i+1]); }
assert.ok(options.source&&options.out,'--source PATCHED_CLONE --out DIR are required');
assert.equal(Boolean(options.crt),Boolean(options.raw),'--crt and --raw are a pair');
fs.mkdirSync(options.out,{recursive:true});
const common=path.join(options.source,'Source/Teensy/MinimalBoot/Common');
const sourceFiles={loader:path.join(options.source,'Source/Teensy/MinimalBoot/Min_DriveDirLoad.ino'),
  pages:path.join(common,'IO_Handlers/IOH_AGIPicture.c'),header:path.join(common,'MPE4Cartridge.h'),
  firmware:path.join(root,'engine/native-game/mpe4_firmware.h'),package:path.join(root,'engine/native-game/mpe4_package.cpp')};
const loaded=Object.fromEntries(Object.entries(sourceFiles).map(([k,p])=>[k,fs.readFileSync(p,'utf8')]));
function fn(source,name) {
  const marker=new RegExp(`^(?:static )?(?:FLASHMEM )?(?:static )?[\\w *]+\\b${name}\\([^]*?^\\}`, 'm');
  const match=source.match(marker);assert.ok(match,`Function ${name} not found`);return match[0]+'\n';
}
fs.writeFileSync(path.join(options.out,'native-load-file.inc'),fn(loaded.loader,'LoadFile'));
fs.writeFileSync(path.join(options.out,'native-raw-pages.inc'),['AGIPictureRawLimit','AGIPictureRawSpanValid','AGIPictureOpenCRTFile','AGIPictureLoadTaggedPage','AGIPictureResolveRawPage'].map(n=>fn(loaded.pages,n)).join('\n'));
fs.writeFileSync(path.join(options.out,'native-raw-bytes.inc'),fn(loaded.pages,'AGIPictureReadRawBytes'));
fs.writeFileSync(path.join(options.out,'native-logical-read.inc'),fn(loaded.firmware,'MPE4Read'));
const exe=path.join(options.out,process.platform==='win32'?'cartridge-harness.exe':'cartridge-harness');
execFileSync(options.compiler,['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation','-fpermissive',
  ...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),
  '-I',common,'-I',options.out,'-I',path.join(root,'engine/native-game'),path.join(import.meta.dirname,'mpe4-cartridge-harness.cpp'),sourceFiles.package,'-o',exe],
  {cwd:path.isAbsolute(options.compiler)?path.dirname(options.compiler):root,windowsHide:true,stdio:'pipe',timeout:60000});
const result=JSON.parse(execFileSync(exe,options.crt?[options.crt,options.raw]:[],{windowsHide:true,encoding:'utf8',timeout:120000}));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
result.sources=Object.entries(sourceFiles).map(([kind,file])=>({kind,file,sha256:hash(file)}));
result.executableSha256=hash(exe);result.scope='Actual native loader, source-page resolver, logical reader and package CRCs with a simulated SD file; no hardware timing claim.';
if(options.crt)result.fixture={crt:options.crt,crtSha256:hash(options.crt),raw:options.raw,rawSha256:hash(options.raw)};
fs.writeFileSync(path.join(options.out,'cartridge-result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));

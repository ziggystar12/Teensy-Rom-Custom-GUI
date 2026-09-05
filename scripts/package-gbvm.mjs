// Build a self-contained, GPL-compliant GBVM SD package. Never reads test ROMs.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const out=path.resolve(process.argv[2]??path.join(root,'build/gb-package'));
const pkg=path.join(out,'SD/VMS/GBVM');fs.mkdirSync(pkg,{recursive:true});
const run=(exe,args)=>{const r=spawnSync(exe,args,{cwd:root,encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});assert.equal(r.status,0,r.stderr||r.error);return r.stdout;};
const node=(script,args)=>run(process.execPath,[path.join(root,script),...args]);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
node('scripts/build-gb-core.mjs',[path.join(out,'core')]);
node('vm/gb/build-client.mjs',['--output-prg',path.join(out,'gbvm.prg'),'--output-boot-bank',path.join(out,'boot.bin'),'--manifest',path.join(out,'client.json')]);
node('nes/tools/build_nesvm_cartridge.mjs',['--id','GBVM','--boot-bank',path.join(out,'boot.bin'),'--output',path.join(pkg,'client.crt'),'--manifest',path.join(out,'cartridge.json')]);
fs.copyFileSync(path.join(out,'core/engine.mvm'),path.join(pkg,'engine.mvm'));
fs.copyFileSync(path.join(pkg,'client.crt'),path.join(out,'SD/GBVM.crt'));
fs.writeFileSync(path.join(pkg,'manifest.vmi'),'VM1\nGBVM\ngb,gbc\nengine.mvm\nclient.crt\nEND\n');
for(const name of ['README.md','NOTICES.md'])fs.copyFileSync(path.join(root,'vms/GBVM',name),path.join(pkg,name));
fs.copyFileSync(path.join(root,'engine/gnuboy/COPYING'),path.join(pkg,'LICENSE-gnuboy.txt'));
fs.mkdirSync(path.join(pkg,'ROMS'),{recursive:true});fs.writeFileSync(path.join(pkg,'ROMS/README.txt'),'Place your own .gb and .gbc ROMs here. No games included.\n');
const source=path.join(pkg,'SOURCE');fs.mkdirSync(source,{recursive:true});
for(const rel of ['engine/gnuboy','vm/gb','vm/abi','vm/video','vm/client','vm/nes/font8x8.h','nes/tools/nes_terminal.mjs','nes/tools/build_nesvm_cartridge.mjs','scripts/build-gb-core.mjs','scripts/package-gbvm.mjs','Source/Teensy/MinimalBoot/Common/VMABI.h','vms/GBVM/README.md','vms/GBVM/NOTICES.md']){
    const dest=path.join(source,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.cpSync(path.join(root,rel),dest,{recursive:true});
}
const prefix=process.env.MPE_ARM_PREFIX??path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const r=spawnSync(process.execPath,[path.join(source,'scripts/build-gb-core.mjs'),path.join(out,'relink')],{env:{...process.env,MPE_ARM_PREFIX:prefix},encoding:'utf8',windowsHide:true});
assert.equal(r.status,0,r.stderr);assert.equal(sha(fs.readFileSync(path.join(out,'relink/engine.mvm'))),sha(fs.readFileSync(path.join(pkg,'engine.mvm'))));
const files=[];function walk(dir,rel=''){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name),name=rel+e.name;if(e.isDirectory())walk(p,name+'/');else{assert.ok(!/\.(gb|gbc|nes)$/i.test(name));files.push({path:name,sha256:sha(fs.readFileSync(p))});}}}
walk(path.join(out,'SD'));files.sort((a,b)=>a.path.localeCompare(b.path));
const zip=path.join(out,'GBVM.zip');assert.ok(!fs.existsSync(zip),'Use a fresh output directory');
const quote=s=>s.replaceAll("'","''");
const rows=JSON.parse(run('powershell.exe',['-NoProfile','-NonInteractive','-Command',`
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory('${quote(path.join(out,'SD'))}','${quote(zip)}')
$gbArchive=[IO.Compression.ZipFile]::OpenRead('${quote(zip)}');$gbHash=[Security.Cryptography.SHA256]::Create()
try { $gbRows=@(foreach($entry in $gbArchive.Entries){if($entry.Name){$stream=$entry.Open();try{$digest=[BitConverter]::ToString($gbHash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose()};[pscustomobject]@{path=$entry.FullName.Replace('\\','/');sha256=$digest}}});ConvertTo-Json -InputObject $gbRows -Compress } finally {$gbArchive.Dispose();$gbHash.Dispose()}
`]));
assert.deepEqual(rows.sort((a,b)=>a.path.localeCompare(b.path)),files);
fs.writeFileSync(path.join(out,'checksums.json'),JSON.stringify({firmwareVersion:'1.1.7',minimumFirmwareVersion:'1.1.6',release:'gb-battery-carts',physicalAcceptance:false,abi:2,files,download:{file:'GBVM.zip',bytes:fs.statSync(zip).size,sha256:sha(fs.readFileSync(zip))}},null,2)+'\n');
console.log('Verified GBVM module, client, corresponding-source relink and ZIP: '+zip);

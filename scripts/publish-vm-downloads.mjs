// Copy only verified artifacts into the tracked download layout. No flashing.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(root,process.env.MPE_VM_TEST_OUT??'build/vm-test');
const ids=process.argv.includes('--nes-only')?['NESVM']:['NESVM','DOSVM'];
const report=JSON.parse(fs.readFileSync(path.join(build,'verification.json')));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
for(const input of JSON.parse(fs.readFileSync(path.join(build,'build-inputs.json'))).files)
  assert.equal(sha(fs.readFileSync(path.join(root,input.path))),input.sha256,'Stale build: '+input.path);
for(const a of report.artifacts){
  if(!a.path.endsWith('.hex')&&!ids.some(id=>a.path.startsWith('VMS/'+id+'/')))continue;
  const source=path.join(build,'SD',a.path),bytes=fs.readFileSync(source);
  assert.equal(sha(bytes),a.sha256,'Unverified artifact: '+a.path);
  let dest;
  if(a.path.endsWith('.hex'))dest=path.join(root,'firmware',a.path);
  else if(a.path.startsWith('VMS/'))dest=path.join(root,'vms',a.path.slice(4));
  else continue;
  fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source,dest);
}
// Stage one package at a time. Verify ZIP members after reopening the archive.
const quote=p=>p.replaceAll("'","''");
function members(dir,prefix=''){
  const rows=[];for(const e of fs.readdirSync(dir,{withFileTypes:true})){
    const p=path.join(dir,e.name),name=prefix+e.name;
    if(e.isDirectory())rows.push(...members(p,name+'/'));else rows.push({path:name,sha256:sha(fs.readFileSync(p))});
  }return rows.sort((a,b)=>a.path.localeCompare(b.path));
}
function zipVerified(stage,name){
  const zip=path.resolve(root,'vms',name);
  assert.equal(path.dirname(zip),path.join(root,'vms'));
  if(fs.existsSync(zip))fs.unlinkSync(zip); // exact generated download
  const ps=`Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory('${quote(stage)}','${quote(zip)}')
$archive=[IO.Compression.ZipFile]::OpenRead('${quote(zip)}')
$hash=[Security.Cryptography.SHA256]::Create()
try {
 $rows=@(foreach($entry in $archive.Entries) {
  if($entry.Name) {
   $stream=$entry.Open()
   try {$digest=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
   finally {$stream.Dispose()}
   [pscustomobject]@{path=$entry.FullName.Replace('\\','/');sha256=$digest}
  }
 })
 ConvertTo-Json -InputObject $rows -Compress
} finally {$hash.Dispose();$archive.Dispose()}`;
  const r=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps],{encoding:'utf8',windowsHide:true,maxBuffer:1024*1024});
  assert.equal(r.status,0,r.stderr);
  assert.deepEqual(JSON.parse(r.stdout.trim()).sort((a,b)=>a.path.localeCompare(b.path)),members(stage),'ZIP member mismatch: '+name);
  return {file:name,sha256:sha(fs.readFileSync(zip)),bytes:fs.statSync(zip).size};
}
for(const id of ids){
  const extras=['README.md','NOTICES.md'];
  const stage=fs.mkdtempSync(path.join(build,'download-'+id.toLowerCase()+'-'));
  const pkg=path.join(stage,'VMS',id);
  fs.cpSync(path.join(build,'SD/VMS',id),pkg,{recursive:true});
  fs.copyFileSync(path.join(build,'SD',id+'.crt'),path.join(stage,id+'.crt'));
  for(const name of extras)fs.copyFileSync(path.join(root,'vms',id,name),path.join(pkg,name));
  if(id==='NESVM'){
    // Corresponding, relinkable source travels WITH the Library-GPL binary.
    // Explicit source roots only; never enumerate nes/NES or private ROMs.
    const source=path.join(pkg,'SOURCE');fs.mkdirSync(source,{recursive:true});
    for(const rel of ['vm/nes','vm/abi','engine/native-nes','engine/nofrendo'])
      fs.cpSync(path.join(root,rel),path.join(source,rel),{recursive:true});
    fs.mkdirSync(path.join(source,'scripts'),{recursive:true});
    fs.copyFileSync(path.join(root,'scripts/build-nes-core.mjs'),path.join(source,'scripts/build-nes-core.mjs'));
    const abi='Source/Teensy/MinimalBoot/Common/VMABI.h';
    fs.mkdirSync(path.dirname(path.join(source,abi)),{recursive:true});
    fs.copyFileSync(path.join(root,abi),path.join(source,abi));
    fs.copyFileSync(path.join(root,'engine/nofrendo/README.md'),path.join(source,'README.md'));
    for(const dir of [pkg,path.join(root,'vms',id)])fs.copyFileSync(path.join(root,'engine/nofrendo/COPYING'),path.join(dir,'LICENSE-Nofrendo.txt'));
    assert.ok(!members(source).some(a=>/\.(nes|gb|gbc)$/i.test(a.path)),'No game media in corresponding source');
    const rebuilt=path.join(stage,'relink-check');
    const prefix=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
    const r=spawnSync(process.execPath,[path.join(source,'scripts/build-nes-core.mjs'),rebuilt],{
      env:{...process.env,MPE_ARM_PREFIX:prefix},encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});
    assert.equal(r.status,0,r.stderr);
    assert.equal(sha(fs.readFileSync(path.join(rebuilt,'engine.mvm'))),report.module.sha256,'Bundled source must reproduce released module');
    // Keep rebuild outputs out of the SD download; exact validated staging child.
    assert.equal(path.dirname(rebuilt),stage);fs.rmSync(rebuilt,{recursive:true});
  }
  if(id==='DOSVM'){
    for(const [from,to] of [['engine/native-dos/vendor/8086tiny/LICENSE.txt','LICENSE-8086tiny.txt'],['dos/vendor/freedos-boot/COPYING','LICENSE-FreeDOS-boot.txt'],['dos/image-manifest.json','source-image-manifest.json']]){
      for(const dir of [pkg,path.join(root,'vms',id)])fs.copyFileSync(path.join(root,from),path.join(dir,to));
    }
  }
  const downloads=[zipVerified(stage,id+'.zip')];
  if(id==='DOSVM'){
    const update=fs.mkdtempSync(path.join(build,'download-dos-update-')),target=path.join(update,'VMS/DOSVM');
    fs.mkdirSync(target,{recursive:true});
    for(const name of ['manifest.vmi','engine.mvm','client.crt','bios.bin',...extras,'LICENSE-8086tiny.txt','LICENSE-FreeDOS-boot.txt'])fs.copyFileSync(path.join(pkg,name),path.join(target,name));
    fs.copyFileSync(path.join(stage,id+'.crt'),path.join(update,id+'.crt'));
    downloads.push(zipVerified(update,'DOSVM-update.zip'));
    assert.ok(!members(update).some(a=>a.path.endsWith('.IMG')||a.path.startsWith('VMS/DOSVM/D/')),'Update package must never contain user drives');
  }
  fs.writeFileSync(path.join(root,'vms',id,'checksums.json'),JSON.stringify({
    firmwareVersion:report.version,abi:2,module:id==='NESVM'?report.module:report.dosModule,
    files:report.artifacts.filter(a=>a.path.startsWith('VMS/'+id+'/')||a.path===id+'.crt'),downloads
  },null,2)+'\n');
  console.log(id+': verified '+downloads.map(d=>d.file).join(', '));
}
console.log('Verified firmware and independent VM downloads copied into tracked folders');

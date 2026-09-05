// Publish a verified, byte-identical DOS restoration without rebuilding or
// replacing firmware/other VMs. Every ZIP member is reopened and hash checked.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(root,process.env.MPE_VM_TEST_OUT??'build/dosvm');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),read=p=>fs.readFileSync(p);
const f5=process.argv.includes('--f5-opt-in');
const release=f5?'dos-f5-opt-in':'dos-video-restoration';
const report=JSON.parse(read(path.join(out,release+'.json')));
if(f5)assert.equal(report.release,release);
assert.equal(report.baseline,'ef9cc9114fadcf00a64e399b110744a5b84d5696');
const lock=path.join(out,'dos-module-inputs.json');assert.equal(sha(read(lock)),report.inputLockSha256);
for(const f of JSON.parse(read(lock)).files)assert.equal(sha(read(path.join(root,f.path))),f.sha256,'Stale source: '+f.path);
for(const f of report.artifacts)assert.equal(sha(read(path.join(out,'SD',f.path))),f.sha256,'Stale artifact: '+f.path);
assert.equal(report.artifacts.find(f=>f.path==='DOSVM.crt').sha256,report.artifacts.find(f=>f.path==='VMS/DOSVM/client.crt').sha256);
const published=path.join(root,'vms/DOSVM');
for(const name of ['engine.mvm','client.crt','bios.bin','manifest.vmi'])fs.copyFileSync(path.join(out,'SD/VMS/DOSVM',name),path.join(published,name));
const members=(dir,prefix='')=>fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>{
 const p=path.join(dir,e.name),name=prefix+e.name;
 return e.isDirectory()?members(p,name+'/'):[{path:name,sha256:sha(read(p))}];
}).sort((a,b)=>a.path.localeCompare(b.path));
const downloads=[];
for(const update of [true,false]){
 const stage=fs.mkdtempSync(path.join(out,update?'dos-update-':'dos-full-')),pkg=path.join(stage,'VMS/DOSVM');fs.mkdirSync(pkg,{recursive:true});
 if(!update)fs.cpSync(path.join(out,'SD/VMS/DOSVM'),pkg,{recursive:true});
 for(const name of ['engine.mvm','client.crt','bios.bin','manifest.vmi','README.md','NOTICES.md','LICENSE-8086tiny.txt','LICENSE-FreeDOS-boot.txt','source-image-manifest.json'])fs.copyFileSync(path.join(published,name),path.join(pkg,name));
 fs.copyFileSync(path.join(out,'SD/DOSVM.crt'),path.join(stage,'DOSVM.crt'));
 const expected=members(stage);if(update)assert.ok(!expected.some(f=>/\.img$/i.test(f.path)||f.path.startsWith('VMS/DOSVM/D/')));
 const name=update?'DOSVM-update.zip':'DOSVM.zip',zip=path.join(root,'vms',name),candidate=path.join(out,name);
 if(fs.existsSync(candidate))fs.unlinkSync(candidate); // exact generated ZIP only
 const quote=p=>p.replaceAll("'","''");
 const ps=`Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory('${quote(stage)}','${quote(candidate)}')
$dosArchive=[IO.Compression.ZipFile]::OpenRead('${quote(candidate)}');$dosHash=[Security.Cryptography.SHA256]::Create()
try {$rows=@(foreach($entry in $dosArchive.Entries){if($entry.Name){$stream=$entry.Open();try{$digest=[BitConverter]::ToString($dosHash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose()};[pscustomobject]@{path=$entry.FullName.Replace('\\','/');sha256=$digest}}});ConvertTo-Json -InputObject $rows -Compress}finally{$dosHash.Dispose();$dosArchive.Dispose()}`;
 const r=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps],{cwd:root,encoding:'utf8',windowsHide:true,maxBuffer:1024*1024});assert.equal(r.status,0,r.stderr);
 assert.deepEqual(JSON.parse(r.stdout).sort((a,b)=>a.path.localeCompare(b.path)),expected);
 fs.copyFileSync(candidate,zip);downloads.push({file:name,sha256:sha(read(zip)),bytes:fs.statSync(zip).size});
}
fs.writeFileSync(path.join(published,'checksums.json'),JSON.stringify({firmwareVersion:report.firmwareVersion,release,baseline:report.baseline,physicalAcceptance:false,abi:2,module:report.module,files:report.artifacts,downloads},null,2)+'\n');
console.log('PASS: '+release+' DOSVM.zip and disk-free DOSVM-update.zip verified; firmware and all other VM downloads untouched');

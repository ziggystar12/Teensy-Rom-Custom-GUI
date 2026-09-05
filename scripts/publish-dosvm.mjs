// Publish a verified engine-only fix and refresh the fresh-install DOS ZIP.
// No firmware, other VM, user drive or disk-template writes. No SD-card access.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'build/dosvm'),published=path.join(root,'vms/DOSVM');
const read=p=>fs.readFileSync(p),sha=p=>crypto.createHash('sha256').update(read(p)).digest('hex');
const report=JSON.parse(read(path.join(build,'dos-save-verification.json')));
const lock=path.join(build,'dos-module-inputs.json'),engine=path.join(build,'SD/VMS/DOSVM/engine.mvm');
function sourceLock(){
 assert.equal(sha(lock),report.inputLockSha256,'Verification/build mismatch');
 for(const f of JSON.parse(read(lock)).files)assert.equal(sha(path.join(root,f.path)),f.sha256,'Stale source: '+f.path);
 assert.equal(sha(engine),report.module.sha256,'Unverified engine');
}
sourceLock();
const checks=JSON.parse(read(path.join(published,'checksums.json')));
assert.equal(checks.abi,2);assert.equal(checks.firmwareVersion,report.firmwareVersion);
// An explicit allowlist from the existing release prevents private games or
// writable test sandboxes from entering the download.
const extras=['README.md','NOTICES.md','LICENSE-8086tiny.txt','LICENSE-FreeDOS-boot.txt','source-image-manifest.json'];
const stage=fs.mkdtempSync(path.join(build,'download-')),tree=path.join(stage,'SD');
const members=[];
for(const f of checks.files){
 assert.ok(f.path==='DOSVM.crt'||f.path.startsWith('VMS/DOSVM/'));
 assert.ok(!f.path.split('/').includes('..'));
 const original=f.path==='DOSVM.crt'?path.join(published,'client.crt'):path.join(root,'vms',f.path.slice(4));
 assert.equal(sha(original),f.sha256,'Published input changed: '+f.path);
 const source=f.path==='VMS/DOSVM/engine.mvm'?engine:original;
 const target=path.join(tree,f.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(source,target);
 members.push({path:f.path,bytes:fs.statSync(source).size,sha256:sha(source)});
}
for(const name of extras){
 const source=path.join(published,name),member='VMS/DOSVM/'+name;
 fs.copyFileSync(source,path.join(tree,member));members.push({path:member,bytes:fs.statSync(source).size,sha256:sha(source)});
}
const zip=path.join(stage,'DOSVM.zip'),quote=s=>s.replaceAll("'","''");
const ps=`Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory('${quote(tree)}','${quote(zip)}')
$archive=[IO.Compression.ZipFile]::OpenRead('${quote(zip)}')
$hash=[Security.Cryptography.SHA256]::Create()
try {
 $rows=@(foreach($entry in $archive.Entries) {if($entry.Name) {
  $stream=$entry.Open()
  try {$digest=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()} finally {$stream.Dispose()}
  [pscustomobject]@{path=$entry.FullName.Replace('\\','/');bytes=$entry.Length;sha256=$digest}
 }})
 ConvertTo-Json -InputObject $rows -Compress
} finally {$hash.Dispose();$archive.Dispose()}`;
const result=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps],{encoding:'utf8',windowsHide:true});
assert.equal(result.status,0,result.stderr);
const sorted=rows=>rows.sort((a,b)=>a.path.localeCompare(b.path));
assert.deepEqual(sorted(JSON.parse(result.stdout)),sorted(members),'ZIP member verification failed');
sourceLock();
// Recoverable local backups before replacing these exact generated outputs.
const backup=path.join(stage,'previous');fs.mkdirSync(backup);
for(const name of ['engine.mvm','checksums.json'])fs.copyFileSync(path.join(published,name),path.join(backup,name));
fs.copyFileSync(path.join(root,'vms/DOSVM.zip'),path.join(backup,'DOSVM.zip'));
fs.copyFileSync(engine,path.join(published,'engine.mvm'));
fs.copyFileSync(zip,path.join(root,'vms/DOSVM.zip'));
checks.module=report.module;
checks.files=checks.files.map(f=>members.find(m=>m.path===f.path));
checks.downloads=[{file:'DOSVM.zip',bytes:fs.statSync(zip).size,sha256:sha(zip)}];
fs.writeFileSync(path.join(published,'checksums.json'),JSON.stringify(checks,null,2)+'\n');
assert.equal(sha(path.join(published,'engine.mvm')),report.module.sha256);
console.log('Published verified DOSVM engine + fresh-install ZIP; firmware and drive files untouched.');
console.log('Previous generated outputs backed up at '+backup);

// Copy only verified AGI outputs into vms/. Does not push Git or flash firmware.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..'),build=path.join(root,'build/agivm'),out=path.join(root,'vms/AGIVM');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),read=p=>fs.readFileSync(p),report=JSON.parse(read(path.join(build,'verification.json')));
assert.equal(report.passed,true);assert.equal(report.firmwareUnchanged,true);
for(const f of JSON.parse(read(path.join(build,'build-inputs.json'))).files)assert.equal(sha(read(path.join(root,f.path))),f.sha256,'Stale AGI build: '+f.path);
const stage=fs.mkdtempSync(path.join(build,'download-')),files=[];
for(const a of report.artifacts){const source=path.join(build,'SD',a.path),bytes=read(source);assert.equal(sha(bytes),a.sha256,a.path);
 const target=path.join(stage,a.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);files.push(a);
 if(a.path.startsWith('VMS/AGIVM/')){const p=path.join(out,a.path.slice('VMS/AGIVM/'.length));fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,bytes);}
}
for(const name of ['README.md','NOTICES.md']){const bytes=read(path.join(out,name)),p='VMS/AGIVM/'+name;fs.writeFileSync(path.join(stage,p),bytes);files.push({path:p,bytes:bytes.length,sha256:sha(bytes)});}
const zip=path.join(root,'vms/AGIVM.zip');assert.equal(path.dirname(zip),path.join(root,'vms'));if(fs.existsSync(zip))fs.unlinkSync(zip);
const q=s=>s.replaceAll("'","''"),ps=`Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory('${q(stage)}','${q(zip)}')
$archive=[IO.Compression.ZipFile]::OpenRead('${q(zip)}')
$hash=[Security.Cryptography.SHA256]::Create()
try {
 $rows=@(foreach($entry in $archive.Entries){
  if($entry.Name){
   $stream=$entry.Open()
   try{$digest=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
   finally{$stream.Dispose()}
   [pscustomobject]@{path=$entry.FullName.Replace('\\','/');sha256=$digest}
  }
 })
 ConvertTo-Json -InputObject $rows -Compress
} finally {$hash.Dispose();$archive.Dispose()}`;
const result=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',ps],{encoding:'utf8',windowsHide:true});assert.equal(result.status,0,result.stderr);
const order=(a,b)=>a.path.localeCompare(b.path);assert.deepEqual(JSON.parse(result.stdout).sort(order),files.map(({path,sha256})=>({path,sha256})).sort(order));
fs.writeFileSync(path.join(out,'checksums.json'),JSON.stringify({firmwareVersion:'1.1.1',abi:2,module:report.module,files:files.sort(order),download:{file:'AGIVM.zip',bytes:fs.statSync(zip).size,sha256:sha(read(zip))},physicalAcceptance:false},null,2)+'\n');
console.log('AGIVM.zip verified and copied into vms/AGIVM; only authored AGITEST content included; no firmware change or Git push');

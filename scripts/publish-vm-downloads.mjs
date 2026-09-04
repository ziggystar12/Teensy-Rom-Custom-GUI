// Copy only verified artifacts into the tracked download layout. No flashing.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'build/vm-test');
const report=JSON.parse(fs.readFileSync(path.join(build,'verification.json')));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
for(const input of JSON.parse(fs.readFileSync(path.join(build,'build-inputs.json'))).files)
  assert.equal(sha(fs.readFileSync(path.join(root,input.path))),input.sha256,'Stale build: '+input.path);
for(const a of report.artifacts){
  const source=path.join(build,'SD',a.path),bytes=fs.readFileSync(source);
  assert.equal(sha(bytes),a.sha256,'Unverified artifact: '+a.path);
  let dest;
  if(a.path.endsWith('.hex'))dest=path.join(root,'firmware',a.path);
  else if(a.path.startsWith('VMS/'))dest=path.join(root,'vms',a.path.slice(4));
  else continue;
  fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(source,dest);
}
// Stage only the selected VM and root launcher, never the full SD/private data.
const stage=fs.mkdtempSync(path.join(build,'download-nes-'));
fs.cpSync(path.join(build,'SD/VMS/NESVM'),path.join(stage,'VMS/NESVM'),{recursive:true});
fs.copyFileSync(path.join(build,'SD/NESVM.crt'),path.join(stage,'NESVM.crt'));
const zip=path.join(root,'vms/NESVM.zip');
if(fs.existsSync(zip))fs.unlinkSync(zip); // exact generated download, not user data
const quote=p=>p.replaceAll("'","''");
const r=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-Command',
  `Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory('${quote(stage)}','${quote(zip)}')`],{encoding:'utf8',windowsHide:true});
assert.equal(r.status,0,r.stderr);
fs.writeFileSync(path.join(root,'vms/NESVM/checksums.json'),JSON.stringify({
  firmwareVersion:report.version,module:report.module,
  files:report.artifacts.filter(a=>a.path.startsWith('VMS/NESVM/')||a.path==='NESVM.crt'),
  zipSha256:sha(fs.readFileSync(zip))},null,2)+'\n');
console.log('Verified firmware and independent NES download copied into tracked folders');

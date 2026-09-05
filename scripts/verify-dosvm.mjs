// Focused module-only verification; optional private GRAPHSET is never packaged.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.join(root,'build/dosvm'),compiler='C:/msys64/mingw64/bin/g++.exe';
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const inputs=JSON.parse(fs.readFileSync(path.join(out,'dos-module-inputs.json')));
assert.equal(inputs.mode,'dos-module');
function sourceLock(){for(const f of inputs.files)assert.equal(sha(path.join(root,f.path)),f.sha256,'Rebuild changed input: '+f.path);}
sourceLock();
const logs=[];
function run(exe,args){
 const r=spawnSync(exe,args,{cwd:root,env:{...process.env,PATH:path.dirname(compiler)+';'+process.env.PATH},encoding:'utf8',windowsHide:true,maxBuffer:4*1024*1024});
 if(r.error||r.status)throw Error(`${exe}: ${r.error??''}\n${(r.stdout+r.stderr).slice(-6000)}`);
 if(r.stdout.trim()){logs.push(r.stdout.trim());console.log(r.stdout.trim());}
}
function native(name,sources,args){
 const exe=path.join(out,name+'.exe');
 run(compiler,['-std=c++17','-O2','-static',...sources,'-o',exe]);run(exe,args);
}
const module=path.join(out,'SD/VMS/DOSVM/engine.mvm');
native('dos-redirector-test',['dos/tests/mpe5_redirector_test.cpp','engine/native-dos/mpe5_redirector.cpp'],[]);
native('image_test',['vm/tests/image_test.cpp'],[module]);
const graphset=process.argv[2]?path.resolve(process.argv[2]):null;
const sandbox=fs.mkdtempSync(path.join(out,'dos-sandbox-save-'));
native('dos_module_test',['vm/tests/dos_module_test.cpp'],[path.join(out,'SD'),sandbox,...(graphset?[graphset]:[])]);
const profile=JSON.parse(fs.readFileSync(path.join(out,'dos-module.json')));
assert.equal(profile.sha256,sha(module));assert.equal(profile.abi,2);
assert.ok(profile.codeBytes<=98304&&profile.workspaceBytes>175464);
assert.equal(profile.guestRamBytes,524288);
// This update is engine-only, even though the builder also checks the client.
for(const file of ['client.crt','bios.bin','manifest.vmi','DOSVM.IMG'])
 assert.equal(sha(path.join(out,'SD/VMS/DOSVM',file)),sha(path.join(root,'vms/DOSVM',file)),'Non-engine change requires separate review: '+file);
sourceLock();
fs.writeFileSync(path.join(out,'dos-save-verification.json'),JSON.stringify({
 firmwareVersion:inputs.version,physicalAcceptance:false,module:profile,
 inputLockSha256:sha(path.join(out,'dos-module-inputs.json')),
 graphset:graphset?{sha256:sha(graphset),checks:'C: Tandy; D: Tandy/CGA/Tandy; exact one-byte persistence and guest readback'}:null,
 checks:logs,sandbox
},null,2)+'\n');
console.log('PASS: DOSVM-only update verified; firmware, client, BIOS and disk unchanged.');

// Focused module-only verification; optional private GRAPHSET is never packaged.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(root,process.env.MPE_VM_TEST_OUT??'build/dosvm'),compiler='C:/msys64/mingw64/bin/g++.exe';
const restore=process.argv.includes('--restore-video'),baseline='ef9cc9114fadcf00a64e399b110744a5b84d5696';
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
 run(compiler,['-I',root,'-std=c++17','-O2','-static',...sources,'-o',exe]);run(exe,args);
}
const module=path.join(out,'SD/VMS/DOSVM/engine.mvm');
native('dos-redirector-test',['dos/tests/mpe5_redirector_test.cpp','engine/native-dos/mpe5_redirector.cpp'],[]);
native('image_test',['vm/tests/image_test.cpp'],[module]);
const graphset=process.argv.slice(2).find(a=>!a.startsWith('--'));const graphsetPath=graphset?path.resolve(graphset):null;
const sandbox=fs.mkdtempSync(path.join(out,'dos-sandbox-save-'));
native('dos_module_test',['vm/tests/dos_module_test.cpp'],[path.join(out,'SD'),sandbox,...(graphsetPath?[graphsetPath]:[])]);
const profile=JSON.parse(fs.readFileSync(path.join(out,'dos-module.json')));
assert.equal(profile.sha256,sha(module));assert.equal(profile.abi,2);
assert.ok(profile.codeBytes<=98304&&profile.workspaceBytes>175464);
assert.equal(profile.guestRamBytes,524288);
// Restoration must reproduce BOTH previously shipped executables, not merely
// approximate their appearance with the new converter or transfer mechanism.
if(restore){
 for(const file of ['engine.mvm','client.crt','bios.bin','manifest.vmi']){
  const old=spawnSync('git',['show',baseline+':vms/DOSVM/'+file],{cwd:root,windowsHide:true,maxBuffer:1024*1024});
  assert.equal(old.status,0);assert.equal(sha(path.join(out,'SD/VMS/DOSVM',file)),crypto.createHash('sha256').update(old.stdout).digest('hex'),'Not exact pre-port DOS: '+file);
 }
 assert.equal(fs.readFileSync(module).readUInt32LE(36),31,'Restored module must not require indexed video');
 run(process.execPath,['dos/tests/video_transport_test.mjs',out,sandbox]);
 run(process.execPath,['dos/tests/mpe5_c64_wire_test.mjs','--scenario','input','--terminal',path.join(out,'dosvm.prg'),'--manifest',path.join(out,'dos-client.json'),'--output',path.join(out,'input-verification.json')]);
 run(process.execPath,['--test','dos/tests/mpe5_packet_recovery_test.mjs']);
 native('dos-video',['dos/tests/mpe5_video_test.cpp','engine/native-dos/mpe5_paged_memory.cpp','engine/native-dos/mpe5_video.cpp'],['engine/native-dos/vendor/8086tiny/bios','dos/sd-card/DOSVM/DOSVM.IMG',path.join(out,'video')]);
 native('dos-speaker',['dos/tests/mpe5_speaker_test.cpp','engine/native-dos/mpe5_platform.cpp','engine/native-dos/mpe5_speaker.cpp'],[]);
 for(const standard of ['pal','ntsc'])run(process.execPath,['dos/tests/mpe5_c64_boot_test.mjs','--crt',path.join(out,'SD/DOSVM.crt'),'--manifest',path.join(out,'dos-client.json'),'--out',path.join(out,'vice-'+standard),'--standard',standard]);
}
for(const file of [...(restore?[]:['client.crt']),'bios.bin','manifest.vmi','DOSVM.IMG'])
 assert.equal(sha(path.join(out,'SD/VMS/DOSVM',file)),sha(path.join(root,'vms/DOSVM',file)),'Non-engine change requires separate review: '+file);
sourceLock();
const artifacts=['engine.mvm','client.crt','bios.bin','manifest.vmi'].map(file=>({path:'VMS/DOSVM/'+file,sha256:sha(path.join(out,'SD/VMS/DOSVM',file))}));
artifacts.push({path:'DOSVM.crt',sha256:sha(path.join(out,'SD/DOSVM.crt'))});
fs.writeFileSync(path.join(out,restore?'dos-video-restoration.json':'dos-save-verification.json'),JSON.stringify({
 firmwareVersion:inputs.version,physicalAcceptance:false,module:profile,
 baseline:restore?baseline:null,artifacts,
 inputLockSha256:sha(path.join(out,'dos-module-inputs.json')),
 graphset:graphsetPath?{sha256:sha(graphsetPath),checks:'C: Tandy; D: Tandy/CGA/Tandy; exact one-byte persistence and guest readback'}:null,
 checks:logs,sandbox
},null,2)+'\n');
console.log(restore?'PASS: exact pre-port DOS engine/client restored; C64 Tandy replay and input verified; firmware, BIOS and disk unchanged.':'PASS: DOSVM-only update verified; firmware, client, BIOS and disk unchanged.');

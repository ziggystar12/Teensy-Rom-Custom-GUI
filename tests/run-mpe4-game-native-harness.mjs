import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync,spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,'..');
const options={};
for(let i=2;i<process.argv.length;i+=2){
  const key=process.argv[i];
  if(!['--package','--output','--compiler'].includes(key)||!process.argv[i+1])throw new Error(`Unknown/incomplete option ${key}`);
  options[key.slice(2)]=process.argv[i+1];
}
if(!options.package)throw new Error('--package M4G2.bin required');
const compiler=[options.compiler,process.env.CXX,'C:\\msys64\\mingw64\\bin\\g++.exe','g++','clang++'].filter(Boolean)
  .find(cc=>spawnSync(cc,['--version'],{windowsHide:true}).status===0);
if(!compiler)throw new Error('Native C++ compiler unavailable');
const output=path.resolve(options.output??path.join(root,'build/mpe4-native-harness'));
fs.mkdirSync(output,{recursive:true});
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const fixture=path.resolve(options.package),fixtureHash=hash(fixture);
const sourceHashes=[path.join(here,'mpe4-game-native-harness.cpp'),path.join(root,'engine/native-game/mpe4_game.cpp'),
  path.join(root,'engine/native-game/mpe4_game.h')].map(file=>({file,sha256:hash(file)}));
const exe=path.join(output,process.platform==='win32'?'mpe4-game-native-harness.exe':'mpe4-game-native-harness');
const args=['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation'];
if(process.platform==='win32')args.push('-static','-static-libgcc','-static-libstdc++');
args.push('-I',path.join(root,'engine/native-game'),path.join(here,'mpe4-game-native-harness.cpp'),
  path.join(root,'engine/native-game/mpe4_game.cpp'),'-o',exe);
execFileSync(compiler,args,{cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,windowsHide:true,stdio:'pipe',timeout:60000});
const result=JSON.parse(execFileSync(exe,[fixture],{windowsHide:true,encoding:'utf8',timeout:60000}));
assert.equal(result.passed,true);
assert.equal(hash(fixture),fixtureHash,'Game fixture changed during the native proof');
for(const source of sourceHashes)assert.equal(hash(source.file),source.sha256,'Native source changed during the proof');
Object.assign(result,{package:{file:fixture,sha256:fixtureHash},sourceHashes,compiler,executableSha256:hash(exe),
  scope:'Focused portable AGI core conformance with an explicitly supplied package; no hardware timing claim.'});
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));

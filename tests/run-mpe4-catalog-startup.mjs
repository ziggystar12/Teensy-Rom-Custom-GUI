import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync,spawnSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..');
const options={};
for(let i=2;i<process.argv.length;i+=2){assert.ok(['--catalog','--out'].includes(process.argv[i])&&process.argv[i+1]);options[process.argv[i].slice(2)]=path.resolve(process.argv[i+1]);}
assert.ok(options.catalog&&options.out,'--catalog BUILD_FOLDER --out PROOF_FOLDER required');
fs.mkdirSync(options.out,{recursive:true});
const native=path.join(root,'engine/native-game');
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const sources=['mpe4_game','mpe4_package','mpe4_session','mpe4_render'].flatMap(n=>[path.join(native,n+'.cpp'),path.join(native,n+'.h')]);
const sourceHashes=sources.map(file=>({file,sha256:hash(file)}));
const compiler=process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++');
const exe=path.join(options.out,process.platform==='win32'?'catalog-startup.exe':'catalog-startup');
execFileSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation',...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),'-I',native,path.join(import.meta.dirname,'mpe4-catalog-startup.cpp'),...sources.filter(f=>f.endsWith('.cpp')),'-o',exe],{cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,windowsHide:true,timeout:60000});
const games=[];
for(const id of ['kq1','kq2','kq3','kq4','sq1','sq2','sq3','colonel','bc','duck','lsl','goose','pq1','goldrush','mh1','mh2']){
  const file=path.join(options.catalog,id,`${id.toUpperCase()}-64-MPE-game.bin`);
  const run=spawnSync(exe,[file],{windowsHide:true,encoding:'utf8',timeout:60000});
  const result=run.status===0?JSON.parse(run.stdout):{passed:false,error:run.stderr||run.error?.message||`exit ${run.status}`};
  games.push({id,...result,packageSha256:hash(file)});console.log(JSON.stringify(games.at(-1)));
}
assert.ok(sourceHashes.every(item=>hash(item.file)===item.sha256),'Sources changed while checking');
const result={passed:games.every(g=>g.passed),scope:'Original startup for900 simulated60Hz frames pergame, CELL replay and parser strip. Waiting at original prompts is allowed; not a full playthrough or hardware timing claim.',games,sourceHashes};
fs.writeFileSync(path.join(options.out,'result.json'),JSON.stringify(result,null,2)+'\n');
if(!result.passed)process.exitCode=1;

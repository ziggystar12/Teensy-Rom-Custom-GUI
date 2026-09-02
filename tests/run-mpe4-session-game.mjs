import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
const support=path.dirname(fileURLToPath(import.meta.url));
const native=path.resolve(support,'../engine/native-game');
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function runMpe4SessionGame({raw,outDir,pickupCommand='',compiler=process.env.MPE4_CXX??process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++'),compile=true}){
  if(!raw||!outDir)throw new Error('raw and outDir required');raw=path.resolve(raw);outDir=path.resolve(outDir);fs.mkdirSync(outDir,{recursive:true});
  const sources=[path.join(support,'mpe4-session-game-harness.cpp'),...['mpe4_game','mpe4_package','mpe4_session','mpe4_render'].map(n=>path.join(native,`${n}.cpp`))];
  const evidence=[...sources,...['mpe4_game','mpe4_package','mpe4_session','mpe4_render'].map(n=>path.join(native,`${n}.h`))];
  const hashes=evidence.map(file=>({file,sha256:hash(file)})),rawHash=hash(raw),exe=path.join(outDir,process.platform==='win32'?'session-game.exe':'session-game');
  if(compile){const args=['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation',...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),'-I',native,...sources,'-o',exe];
    const build=spawnSync(compiler,args,{cwd:path.isAbsolute(compiler)?path.dirname(compiler):native,windowsHide:true,encoding:'utf8',timeout:60000});
    fs.writeFileSync(path.join(outDir,'session-game-build.log'),(build.stdout??'')+(build.stderr??''));if(build.status!==0)throw new Error(build.stderr||build.error?.message||'compile failed');}
  const run=spawnSync(exe,[raw,...(pickupCommand?[pickupCommand]:[])],{cwd:outDir,windowsHide:true,encoding:'utf8',timeout:120000});
  const name=pickupCommand?`pickup-${pickupCommand.replaceAll(/[^a-z0-9]/gi,'-')}`:'session-game';
  fs.writeFileSync(path.join(outDir,`${name}.log`),run.stderr??'');let result;try{result=JSON.parse(run.stdout);}catch{throw new Error(run.stderr||run.error?.message||run.stdout||'no report');}
  result.pickupCommand=pickupCommand;result.raw={file:raw,sha256:rawHash,unchangedDuringRun:hash(raw)===rawHash};result.executable={file:exe,sha256:hash(exe)};
  result.sourceHashes=hashes.map(item=>({...item,unchangedDuringBuild:hash(item.file)===item.sha256}));result.executedAt=new Date().toISOString();
  result.passed&&=run.status===0&&result.raw.unchangedDuringRun&&result.sourceHashes.every(item=>item.unchangedDuringBuild);
  result.report=path.join(outDir,`${name}-result.json`);fs.writeFileSync(result.report,JSON.stringify(result,null,2)+'\n');return result;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){const options={};for(let i=2;i<process.argv.length;i+=2){const key={'--raw':'raw','--out':'outDir','--pickup-command':'pickupCommand','--compiler':'compiler'}[process.argv[i]];if(!key||!process.argv[i+1])throw new Error('Expected --raw FILE --out DIR [--pickup-command TEXT]');options[key]=process.argv[i+1];}
  const result=runMpe4SessionGame(options);console.log(JSON.stringify(result,null,2));if(!result.passed)process.exitCode=1;}

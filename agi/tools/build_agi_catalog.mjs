// Build all 16 profile-selected games locally; never publish original game media.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {validateAgi} from './agi_content.mjs';

const args={};
for(let i=2;i<process.argv.length;i+=2){
 assert.ok(['--compiler-root','--out'].includes(process.argv[i])&&process.argv[i+1],
  'Expected --compiler-root AGI64 --out PRIVATE_OUTPUT');
 args[process.argv[i]]=path.resolve(process.argv[i+1]);
}
assert.ok(args['--compiler-root']&&args['--out'],'Explicit compiler and private output directories required');
const root=args['--compiler-root'],out=args['--out'];
const {MPE_CATALOG}=await import(pathToFileURL(path.join(root,'scripts/build-mpe-catalog.mjs')));
const {buildAgiVmContent}=await import(pathToFileURL(path.join(root,'host/build-agivm-content.mjs')));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const profiles=MPE_CATALOG.map(([id,title])=>{
 const file=path.join(root,'config',id+'-64.json'),bytes=fs.readFileSync(file);
 const profile=JSON.parse(bytes.toString().replace(/^\uFEFF/,''));
 const source=path.resolve(root,profile.sourceDir);assert.ok(fs.existsSync(source),source);
 return {id,title,file,bytes,profile,source};
});
assert.equal(profiles.length,16);
const gameDir=path.join(out,'VMS/AGIVM/GAMES');fs.mkdirSync(gameDir,{recursive:true});
const games=[];
for(const p of profiles){
 const built=buildAgiVmContent(p.source,p.profile),content=validateAgi(built.data);
 const name=p.id.toUpperCase()+'.AGI',file=path.join(gameDir,name);
 assert.ok(built.report.graphicsVerification.passed);
 fs.writeFileSync(file,built.data);assert.equal(sha(fs.readFileSync(file)),sha(built.data));
 assert.deepEqual(fs.readFileSync(p.file),p.bytes,'Profile changed during build');
 games.push({id:p.id,title:p.title,file:'VMS/AGIVM/GAMES/'+name,bytes:built.data.length,sha256:sha(built.data),
  saveId:content.saveId,saveEpoch:content.saveEpoch,resourceCount:content.entries.length,
  profileSha256:sha(p.bytes),source:built.report.source,graphicsVerification:built.report.graphicsVerification});
 console.log(`${name}: ${built.data.length} bytes, ${content.entries.length} resources, ${built.report.graphicsVerification.cels} cels verified`);
}
assert.equal(new Set(games.map(g=>g.saveId)).size,16,'Save identities must be unique');
fs.writeFileSync(path.join(out,'games-manifest.json'),JSON.stringify({format:'AGIVM-AGI',games,physicalAcceptance:false},null,2)+'\n');
fs.writeFileSync(path.join(out,'GAME-SHA256SUMS.txt'),games.map(g=>`${g.sha256}  ${g.file}`).join('\n')+'\n');
console.log(`Verified ${games.length} standalone .AGI games in ${out}`);

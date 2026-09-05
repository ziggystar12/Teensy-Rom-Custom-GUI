// Explicit compiler/profile input; never guesses SQ1 or writes into AGI-64.
import fs from 'node:fs';import path from 'node:path';import {pathToFileURL} from 'node:url';import crypto from 'node:crypto';import {execFileSync} from 'node:child_process';
import {validateAgi} from './agi_content.mjs';
const args={};for(let i=2;i<process.argv.length;i+=2){const k=process.argv[i];if(!['--compiler-root','--profile','--source','--output'].includes(k)||!process.argv[i+1])throw Error('Expected --compiler-root DIR --profile JSON --output FILE.AGI [--source DIR]');args[k]=path.resolve(process.argv[i+1]);}
for(const k of ['--compiler-root','--profile','--output'])if(!args[k])throw Error(k+' required');
if(!/\.agi$/i.test(args['--output']))throw Error('Output must end in .AGI');
const root=args['--compiler-root'],profile=JSON.parse(fs.readFileSync(args['--profile']));
const source=args['--source']??path.resolve(path.dirname(args['--profile']),'..',profile.sourceDir);
const load=name=>import(pathToFileURL(path.join(root,'host',name)).href);
const {buildAgiVmContent}=await load('build-agivm-content.mjs');
const result=buildAgiVmContent(source,profile);
const content=validateAgi(result.data),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
fs.mkdirSync(path.dirname(args['--output']),{recursive:true});fs.writeFileSync(args['--output'],result.data);
let commit='unavailable';try{commit=execFileSync('git',['-C',root,'rev-parse','HEAD'],{encoding:'utf8',windowsHide:true}).trim();}catch{}
fs.writeFileSync(args['--output']+'.json',JSON.stringify({content,sha256:sha(result.data),compilerRoot:root,compilerCommit:commit,profile:profile.id,
 profileSha256:sha(fs.readFileSync(args['--profile'])),sourceHashes:result.report.source.sourceMd5,report:result.report,physicalAcceptance:false},null,2)+'\n');
console.log(`Built ${path.basename(args['--output'])}: ${result.data.length} bytes, ${content.entries.length} resources; original startup, sprites, native views and C64 menus`);

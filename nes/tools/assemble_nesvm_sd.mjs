#!/usr/bin/env node
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..'),nes=path.join(root,'nes'),build=path.join(nes,'build');
const options={cartridge:path.join(nes,'sd-card/NESVM.CRT'),demo:path.join(nes,'DEMO/Crossbow.nes'),output:path.join(build,'package/sd-card'),manifest:path.join(build,'package/manifest.json')};
for(let i=2;i<process.argv.length;i+=2){const key=process.argv[i].replace(/^--/,'');assert.ok(Object.hasOwn(options,key)&&process.argv[i+1],`Unknown/incomplete option ${key}`);options[key]=path.resolve(process.argv[i+1]);}
const inside=(child,parent)=>child.startsWith(parent+path.sep);assert.ok(inside(options.output,build),'output must stay under nes/build');assert.ok(inside(options.manifest,build),'manifest must stay under nes/build');
const digest=b=>crypto.createHash('sha256').update(b).digest('hex'),expectedDemo='93c1eff05b4d39992c0fd05dce9bb3d5b8349ca3a2416717d75ef4336fc715ea';
for(const key of ['cartridge','demo'])assert.ok(fs.statSync(options[key],{throwIfNoEntry:false})?.isFile(),`Missing ${key}: ${options[key]}`);
assert.equal(digest(fs.readFileSync(options.demo)),expectedDemo,'Authorized Crossbow demo identity changed');
const stage=options.output+'.new';if(fs.existsSync(stage))fs.rmSync(stage,{recursive:true,force:true});
for(const p of [stage,path.join(stage,'NESVM/ROMS'),path.join(stage,'NESVM/SAVES')])fs.mkdirSync(p,{recursive:true});
const copies=[[options.cartridge,path.join(stage,'NESVM.CRT')],[options.demo,path.join(stage,'NESVM/ROMS/Crossbow.nes')],
  [path.join(nes,'sd-card/NESVM/ROMS/README.txt'),path.join(stage,'NESVM/ROMS/README.txt')],[path.join(nes,'sd-card/NESVM/SAVES/README.txt'),path.join(stage,'NESVM/SAVES/README.txt')]];
for(const [source,destination] of copies){assert.ok(fs.statSync(source,{throwIfNoEntry:false})?.isFile(),`Missing package input: ${source}`);fs.copyFileSync(source,destination);}
if(fs.existsSync(options.output)){assert.ok(fs.statSync(options.output).isDirectory());fs.rmSync(options.output,{recursive:true,force:true});}fs.renameSync(stage,options.output);
const files=fs.readdirSync(options.output,{recursive:true,withFileTypes:true}).filter(e=>e.isFile()).map(e=>path.join(e.parentPath??e.path,e.name));
assert.equal(files.length,4,'SD package must contain exactly CRT, one demo, and two README files');
const record={format:'NESVM SD card R1',rootFiles:['NESVM.CRT','NESVM/'],romDirectory:'/NESVM/ROMS',saveDirectory:'/NESVM/SAVES',
  files:Object.fromEntries(files.map(f=>[path.relative(options.output,f).replaceAll('\\','/'),{bytes:fs.statSync(f).size,sha256:digest(fs.readFileSync(f))}])),
  policy:'Only the exact owner-authorized Crossbow demo is copied; nes/ROMS is never read.'};
fs.mkdirSync(path.dirname(options.manifest),{recursive:true});fs.writeFileSync(options.manifest,JSON.stringify(record,null,2)+'\n');console.log(`Assembled NESVM SD tree with exactly one ROM: ${options.output}`);

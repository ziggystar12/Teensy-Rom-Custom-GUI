// Actual VIC-II timing, not the instruction-only receiver harness.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {inflateSync} from 'node:zlib';
const [base='build/vt',standard='pal',variant='full']=process.argv.slice(2);
assert.ok(['pal','ntsc'].includes(standard));
const root=path.resolve(import.meta.dirname,'../..'),out=path.resolve(root,base,'raster-'+variant+'-'+standard);
fs.mkdirSync(out,{recursive:true});
const m=JSON.parse(fs.readFileSync(path.resolve(root,base,'client.json'))),l=m.labels;
const file=n=>path.join(out,n).replaceAll('\\','/'),hex=n=>'$'+n.toString(16);
fs.rmSync(file('monitor.log'),{force:true});
const write=(n,lines)=>fs.writeFileSync(file(n),lines.join('\n')+'\n');
const play=(id,n)=>`command ${id} "playback \\"${file(n)}\\""`;
const stage=m.stageAddress;
const bitmap=Buffer.from(Array.from({length:8000},(_,i)=>0x80>>(i&7)));fs.writeFileSync(file('bitmap.bin'),bitmap);
write('boot.mon',['bank cpu',`break ${hex(l.terminal_error_hold)}`,play(1,'setup.mon'),'x']);
write('setup.mon',['disable 1',`bload "${path.resolve(root,base,'kernel-'+(variant==='mixed'?'mixed-':'')+standard+'.bin').replaceAll('\\','/')}" 0 $3000`,
 `bload "${file('bitmap.bin')}" 0 $6000`,'fill $5c00 $5fe7 $01','fill $5800 $5be7 $23',
 `> ${hex(stage+6).slice(1)} 03 00 02 02 01`,
 `break $3000`,play(2,'entry.mon'),`r a=$02`,`g ${hex(l.mpe_video_resume)}`]);
// Stop after the first generated frame, before mailbox timeout can fire.
write('entry.mon',['disable 2','r',`trace store $d011`,`break ${hex(l.mpe_video_irq_finish)}`,'ignore 4 2',play(4,'finish.mon'),'x']);
write('finish.mon',['r','screenshot "'+file('screen.png')+'" 2','quit']);
const vice=path.resolve(root,'../AGI-64/tools/VICE-3.10/GTK3VICE-3.10-win64/bin/x64sc.exe');
const r=spawnSync(vice,['-default','-'+standard,'-console','-directory',path.dirname(path.dirname(vice)),'-initbreak','reset','-warp','+sound','+easyflashcrtwrite','-cartcrt',path.resolve(root,base,'SD/NESVM.crt'),'-monlogname',file('monitor.log'),'-monlog','-moncommands',file('boot.mon'),'-limitcycles','40000000','-logfile',file('vice.log')],{cwd:out,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:1024*1024});
fs.writeFileSync(file('output.txt'),r.stdout+'\n'+r.stderr);assert.ifError(r.error);assert.equal(r.status,0);
const log=fs.readFileSync(file('monitor.log'),'utf8');
const stores=[...log.matchAll(/Trace store d011\)\s+(\d+)\/\$[0-9a-f]+,\s+(\d+)\/\$[0-9a-f]+\s*\n\.C:([0-9a-f]+)/gi)]
 .filter(m=>parseInt(m[3],16)>=0x3000&&parseInt(m[3],16)<0x5800);
const expected=[];for(let frame=0;frame<3;frame++)for(let y=0;y<200;y++){
 expected.push([51+y,y%8===0?57:14]);
 if(variant==='mixed'&&Math.floor(y/8)%2===0&&Math.floor(y/8)%7===6&&y%8===7)expected.push([51+y,60]);
}
assert.equal(stores.length,expected.length,'three complete enhanced frames');
for(let n=0;n<stores.length;n++)assert.deepEqual([+stores[n][1],+stores[n][2]],expected[n],`store ${n} timing`);
// Decode VICE's RGBA PNG to verify the bitmap row counter survives rescans,
// not merely that register writes are on time. No image processing/editing.
const png=fs.readFileSync(file('screen.png'));let width,height;const idat=[];
for(let pos=8;pos<png.length;){const n=png.readUInt32BE(pos),type=png.toString('ascii',pos+4,pos+8),b=png.subarray(pos+8,pos+8+n);
 if(type==='IHDR'){width=b.readUInt32BE(0);height=b.readUInt32BE(4);assert.equal(b[8],8);assert.equal(b[9],6);assert.equal(b[12],0);}
 if(type==='IDAT')idat.push(b);pos+=n+12;
}
const raw=inflateSync(Buffer.concat(idat)),rgba=Buffer.alloc(width*height*4),stride=width*4;
const paeth=(a,b,c)=>{const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);return pa<=pb&&pa<=pc?a:pb<=pc?b:c;};
for(let y=0;y<height;y++){const filter=raw[y*(stride+1)];assert.ok(filter<5);
 for(let x=0;x<stride;x++){const at=y*stride+x,a=x>=4?rgba[at-4]:0,b=y?rgba[at-stride]:0,c=y&&x>=4?rgba[at-stride-4]:0;
  rgba[at]=(raw[y*(stride+1)+1+x]+[0,a,b,(a+b)>>1,paeth(a,b,c)][filter])&255;
 }}
const pixel=(x,y)=>rgba.subarray((y*width+x)*4,(y*width+x)*4+3).toString('hex');
// Active area starts 32 pixels after the cropped border; find its first
// white pixel using the row-zero barcode, independently of PAL/NTSC crop.
let top=0;while(top<height&&pixel(65,top)!=='ffffff')top++;assert.ok(top+200<=height);
const colors=[pixel(64,top),pixel(65,top),pixel(68,top+4),pixel(69,top+4)];
assert.equal(new Set(colors).size,4,'both screen maps must be visible');
for(let y=0;y<200;y++)for(let x=0;x<8;x++){
 const band=Math.floor(y/8),lower=variant==='full'?(y&7)>=4:band%2===0&&(y&7)>=1+band%7;
 const bit=x===(y&7);assert.equal(pixel(64+x,top+y),colors[(lower?2:0)+(bit?0:1)],`bitmap row ${y}, pixel ${x}`);
}
fs.writeFileSync(file('result.json'),JSON.stringify({passed:true,standard,variant,frames:3,stores:stores.length,bitmapRowAndColorChecks:true,physicalAcceptance:false},null,2));
console.log(`PASS ${standard}/${variant}: three stabilized enhanced frames, ${stores.length} checked D011 writes, two-screen bitmap output; no physical DMA proof`);

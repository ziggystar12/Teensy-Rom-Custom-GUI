// Actual VIC-II timing, not the instruction-only receiver harness.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {inflateSync} from 'node:zlib';
const [base='build/vt',standard='pal',variant='full',stream='legacy',vm='NESVM']=process.argv.slice(2);
assert.ok(['NESVM','DOSVM'].includes(vm));
const streaming=stream==='stream';
assert.ok(['pal','ntsc'].includes(standard));
const root=path.resolve(import.meta.dirname,'../..'),out=path.resolve(root,base,'raster-'+variant+'-'+standard+(streaming?'-stream':''));
fs.mkdirSync(out,{recursive:true});
const m=JSON.parse(fs.readFileSync(path.resolve(root,base,vm==='DOSVM'?'dos-client.json':'client.json'))),l=m.labels;
const file=n=>path.join(out,n).replaceAll('\\','/'),hex=n=>'$'+n.toString(16);
fs.rmSync(file('monitor.log'),{force:true});
const write=(n,lines)=>fs.writeFileSync(file(n),lines.join('\n')+'\n');
const play=(id,n)=>`command ${id} "playback \\"${file(n)}\\""`;
const stage=m.stageAddress;
const bitmap=Buffer.from(Array.from({length:8000},(_,i)=>0x80>>(i&7)));fs.writeFileSync(file('bitmap.bin'),bitmap);
write('boot.mon',['bank cpu',`break ${hex(l.terminal_error_hold)}`,play(1,'setup.mon'),'x']);
const kernel=bank=>path.resolve(root,base,'kernel-'+(bank?'bank1-':'')+(variant==='mixed'?'mixed-':'')+standard+'.bin').replaceAll('\\','/');
write('setup.mon',['disable 1',`bload "${kernel(0)}" 0 $3000`,
 `bload "${file('bitmap.bin')}" 0 $6000`,'fill $5c00 $5fe7 $01','fill $5800 $5be7 $23',
 `> ${hex(stage+6).slice(1)} 03 00 02 02 01`,
 `break $3000`,play(2,'entry.mon'),`r a=$02`,`g ${hex(l.mpe_video_resume)}`]);
if(streaming){
 write('setup.mon',['disable 1',`bload "${kernel(0)}" 0 $3000`,`bload "${kernel(1)}" 0 $c000`,
  `bload "${file('bitmap.bin')}" 0 $6000`,`bload "${file('bitmap.bin')}" 0 $a000`,
  'fill $5c00 $5fe7 $01','fill $5800 $5be7 $23','fill $8c00 $8fe7 $01','fill $8800 $8be7 $23',
  `> ${hex(stage+6).slice(1)} 03 00 02 02 01`,`break ${hex(l.ack_packet)}`,play(2,'arm.mon'),`r a=$02`,`g ${hex(l.mpe_video_resume)}`]);
 write('arm.mon',['disable 2','trace store $d011','trace store $dd00','trace store $dff4',
  `> ${hex(stage+8).slice(1)} 03 02 03`,`break ${hex(l.ack_packet)}`,play(6,'flip.mon'),`g ${hex(l.mpe_video_stream)}`]);
 write('flip.mon',['disable 6',`> ${hex(stage+8).slice(1)} 04`,`break $c000`,play(7,'stream-entry.mon'),`g ${hex(l.mpe_video_flip)}`]);
 write('stream-entry.mon',['disable 7','r',`break ${hex(l.mpe_video_irq_finish)}`,'ignore 8 2',play(8,'finish.mon'),'x']);
}
// Stop after the first generated frame, before mailbox timeout can fire.
write('entry.mon',['disable 2','r',`trace store $d011`,`break ${hex(l.mpe_video_irq_finish)}`,'ignore 4 2',play(4,'finish.mon'),'x']);
write('finish.mon',['r','screenshot "'+file('screen.png')+'" 2','quit']);
// Exercise the DOS keyboard sampler on EVERY enhanced IRQ, not its inactive
// early return. In particular it must never overwrite the $02e0 trampoline.
if(vm==='DOSVM')fs.writeFileSync(file('setup.mon'),fs.readFileSync(file('setup.mon'),'utf8').replace('disable 1\n','disable 1\n> 02c0 01 01\n'));
const vice=path.resolve(root,'../AGI-64/tools/VICE-3.10/GTK3VICE-3.10-win64/bin/x64sc.exe');
const r=spawnSync(vice,['-default','-'+standard,'-console','-directory',path.dirname(path.dirname(vice)),'-initbreak','reset','-warp','+sound','+easyflashcrtwrite','-cartcrt',path.resolve(root,base,'SD',vm+'.crt'),'-monlogname',file('monitor.log'),'-monlog','-moncommands',file('boot.mon'),'-limitcycles','40000000','-logfile',file('vice.log')],{cwd:out,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:1024*1024});
fs.writeFileSync(file('output.txt'),r.stdout+'\n'+r.stderr);assert.ifError(r.error);assert.equal(r.status,0);
const log=fs.readFileSync(file('monitor.log'),'utf8');
const stores=[...log.matchAll(/Trace store d011\)\s+(\d+)\/\$[0-9a-f]+,\s+(\d+)\/\$[0-9a-f]+\s*\n\.C:([0-9a-f]+)/gi)]
 .filter(m=>parseInt(m[3],16)>=(streaming?0xc000:0x3000)&&parseInt(m[3],16)<(streaming?0xd000:0x4000));
if(streaming){
 const writes=[...log.matchAll(/Trace store d011\)[^\n]*\n[^\n]*- A:([0-9a-f]{2})/gi)];
 assert.ok(writes.length>=600);for(const w of writes)assert.ok(parseInt(w[1],16)&16,'streaming must never clear DEN');
 const flips=[...log.matchAll(/Trace store dd00\)\s+(\d+)\/[^\n]*\n[^\n]*- A:([0-9a-f]{2})/gi)];
 assert.equal(flips.length,1);assert.ok(+flips[0][1]>=251&&+flips[0][1]<=254);assert.equal(parseInt(flips[0][2],16)&3,1);
}
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
fs.writeFileSync(file('result.json'),JSON.stringify({passed:true,standard,variant,streaming,frames:3,stores:stores.length,bitmapRowAndColorChecks:true,physicalAcceptance:false},null,2));
console.log(`PASS ${standard}/${variant}/${stream}: three stabilized enhanced frames, ${stores.length} checked D011 writes, two-screen bitmap output${streaming?', real inactive-bank flip with no DEN blanking':''}; no physical DMA proof`);

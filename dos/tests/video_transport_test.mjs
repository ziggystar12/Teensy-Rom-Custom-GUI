// Replay actual DOS-module packets through the actual generated C64 receiver.
// This is protocol/display evidence, not a physical cartridge timing model.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {MPE3_TITLE_TERMINAL_STATE as state} from '../../vm/client/host/mpe3-title-terminal.mjs';
const [build,sandbox]=process.argv.slice(2).map(p=>path.resolve(p));
assert.ok(build&&sandbox);
const manifest=JSON.parse(fs.readFileSync(path.join(build,'dos-client.json')));
const prg=fs.readFileSync(path.join(build,'dosvm.prg'));
assert.equal(crypto.createHash('sha256').update(prg).digest('hex'),manifest.terminalPrgSha256);
const program={prg,labels:manifest.labels,stageAddress:manifest.stageAddress};
// The optional receiver may exist, but default DOS must never send it a
// packet or arm its IRQ. Check the actual stream and state, not label absence.
const wire=fs.readFileSync(path.join(sandbox,'video-wire.bin'));
assert.equal(wire.length%240,0);
const checks=new Map(fs.readFileSync(path.join(sandbox,'video-checkpoints.txt'),'utf8').trim().split('\n').map(line=>{
 const [at,mode,background,file]=line.trim().split(' ');return [+at,{mode:+mode,background:+background,file}];
}));
let started=false,index=0,checked=0,quietRequests=0;
const packet=()=>wire.subarray(index*240,(index+1)*240);
function publish(cpu){const p=packet();assert.ok(p[3]===1||p[3]===2);cpu.ram.set(p,0xdf00);cpu.ram[0xdff7]=p[4];cpu.ram[0xdff5]=2;}
function checkpoint(cpu,c){
 assert.equal(cpu.ram[state.error],0);assert.equal(cpu.ram[0xd011]&0x70,0x30,'bitmap must be visible');
 assert.equal(cpu.ram[0xdd00]&3,2);assert.equal(cpu.ram[0xd018],0x78);assert.equal(cpu.ram[0xd021],c.background);
 assert.equal(cpu.ram[0xd016],c.mode===8?0x18:8);
 assert.equal(cpu.ram[0x02e3],0);assert.equal(cpu.ram[0x02e4],0);assert.equal(cpu.ram[0x02e8],0);
 if(c.file!=='text'){
  const records=fs.readFileSync(path.join(sandbox,c.file));assert.equal(records.length,12000);
  for(let n=0;n<1000;n++){
   const r=records.subarray(n*12,n*12+12),cell=r.readUInt16LE(0);
   assert.deepEqual(Buffer.from(cpu.ram.subarray(0x6000+cell*8,0x6008+cell*8)),r.subarray(2,10),`mode ${c.mode} cell ${cell} bitmap`);
   assert.equal(cpu.ram[0x5c00+cell],r[10],`mode ${c.mode} cell ${cell} colors`);
   if(c.mode===8)assert.equal(cpu.ram[0xd800+cell]&15,r[11]&15);
  }
 }
 checked++;
}
const service={onWrite(cpu,address,value){
 if(address===0xdff4&&value===1&&!started){started=true;publish(cpu);}
 else if(address===0xdff4&&value===3)cpu.ram[0xdffc]=cpu.ram[0xdffe];
 else if(address===0xdff4&&value===4){quietRequests++;cpu.ram[0xdff5]=0x12;}
 else if(address===0xdff6&&started){
  assert.equal(value,packet()[4]);index++;if(checks.has(index))checkpoint(cpu,checks.get(index));
  if(index*240<wire.length)publish(cpu);
 }
}};
const cpu=new C64TerminalCpu(program,service,{rasterInterruptPeriod:2000,recordWrites:false});
cpu.runUntil(c=>index*240===wire.length||c.pc===program.labels.terminal_error_hold,100_000_000);
assert.equal(cpu.ram[state.error],0);assert.equal(index*240,wire.length);assert.equal(checked,3);assert.equal(quietRequests,0);
if(program.labels.mpe_video_disable){
 // The F1 replacement invokes this shared receiver handoff before copying
 // original CELLs. Check return from a streaming bank-1 enhanced display.
 cpu.ram[0x02e3]=1;cpu.ram[0x02e4]=1;cpu.ram[0x02e8]=1;
 cpu.ram[0xdd00]=(cpu.ram[0xdd00]&252)|1;cpu.ram[0xd018]=0x38;
 cpu.push(1);cpu.push(255);cpu.pc=program.labels.mpe_video_disable;
 cpu.runUntil(c=>c.pc===0x0200,1_000_000);
 for(const a of [0x02e3,0x02e4,0x02e8,0x02e9])assert.equal(cpu.ram[a],0);
 assert.equal(cpu.ram[0xdd00]&3,2);assert.equal(cpu.ram[0xd018],0x78);
 assert.equal(cpu.ram[0xfffe]|cpu.ram[0xffff]<<8,program.labels.raster_irq);
 assert.equal(cpu.ram[0xd011]&0x70,0x30);
}
console.log(`PASS: ${index} real DOS packets through generated 6510 client; BIOS/CGA80 boot, Tandy 160-wide multicolor, Tandy 320-wide hires (all 1000 cells), and visible return to text; no indexed DMA, errors or quiet retries`);

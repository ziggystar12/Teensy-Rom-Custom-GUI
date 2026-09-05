// Run the packaged C64 main loop against actual native picker packets. Input
// changes happen AFTER the initial display: never call scan routines directly.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {buildMpe3TitleTerminal} from '../../vm/client/host/mpe3-title-terminal.mjs';
import {MPE4_INPUT as S} from '../../vm/client/host/mpe4-keyboard.mjs';
const [wirePath,terminalPath,manifestPath]=process.argv.slice(2);
assert.ok(wirePath&&terminalPath&&manifestPath);
const bytes=fs.readFileSync(wirePath),frames=[[]];
for(let offset=0;offset<bytes.length&&frames.length<=5;){
 const length=bytes.readUInt16LE(offset);offset+=2;assert.ok(length>=10&&offset+length<=bytes.length);
 const p=Buffer.from(bytes.subarray(offset,offset+length));offset+=length;frames.at(-1).push(p);
 if(p[3]===2)frames.push([]);
}
assert.equal(frames.length,6,'native picker must keep producing idle frame ends');
for(const frame of frames.slice(1,5))assert.ok(frame.length===1&&frame[0][3]===2&&frame[0][5]===0x25,
 'idle picker must send frame ends only, with no bitmap replacements');
const manifest=JSON.parse(fs.readFileSync(manifestPath));
const program=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:true,publishVideoTiming:manifest.publishVideoTiming,
 diagnosticTitle:manifest.diagnosticTitle,diagnosticFooter:manifest.diagnosticFooter});
assert.deepEqual(program.prg,fs.readFileSync(terminalPath),'must exercise the exact packaged client');
// Reuse the captured native idle frame to leave enough time for neutral,
// press, hold and release intervals. Only wire sequence/CRC change.
const packets=[...frames[0],...Array.from({length:25},()=>frames[1][0])].map((p,i)=>{
 const b=Buffer.from(p);b[4]=i+1;let crc=65535;
 for(const v of b.subarray(0,-2)){crc^=v<<8;for(let n=0;n<8;n++)crc=((crc<<1)^((crc&0x8000)?0x1021:0))&65535;}
 b.writeUInt16LE(crc,b.length-2);return b;
});
const changes=new Map([
 [2,{keys:[[0,7]]}],[3,{}],[4,{keys:[[0,7],[1,7]]}],[5,{}],
 [6,{joy:2}],[7,{joy:2}],[8,{}],[9,{joy:1}],[10,{}],
 [11,{keys:[[0,2]]}],[12,{}],[13,{keys:[[0,2],[6,4]]}],[14,{}],
 [15,{joy:8}],[16,{}],[17,{joy:4}],[18,{}],
 [19,{keys:[[0,1]]}],[20,{}],[21,{joy:16}],[22,{}]
]);
const events=[];let index=0,ends=0;
const publish=cpu=>{cpu.ram.set(packets[index],0xdf00);cpu.ram[0xdff7]=packets[index][4];cpu.ram[0xdff5]=2;};
const service={onWrite(cpu,address,value){
 if(address===0xdff4&&value===1)publish(cpu);
 if(address===0xdff4&&value===3){
  const event=[S.keyRegister,S.scanRegister,S.joyRegister,S.flagsRegister,S.sequenceRegister,S.checksumRegister].map(a=>cpu.ram[a]);
  assert.equal(event[5],event.slice(0,5).reduce((sum,n)=>sum^n,0xa5));events.push(event.slice(0,4));cpu.ram[S.ack]=event[4];
 }
 if(address===0xdff6&&value){
  assert.equal(value,packets[index][4]);
  if(packets[index][3]===2){
   const change=changes.get(++ends);
   if(change){cpu.controls.matrix.fill(0);cpu.controls.port2Bits=change.joy??0;
    for(const [row,col] of change.keys??[])cpu.controls.matrix[row]|=1<<col;}
  }
  if(++index<packets.length)publish(cpu);
 }
}};
const cpu=new C64TerminalCpu(program,service,{recordWrites:false});
cpu.runUntil(c=>index===packets.length||c.pc===program.labels.terminal_error_hold,10000000);
assert.equal(index,packets.length);assert.equal(cpu.ram[S.active],1);assert.equal(cpu.ram[S.armed],1);
assert.deepEqual(events,[
 [0x83,80,0,1],[0x82,72,0,1], // native C64 cursor down/up (Shift)
 [0,0,2,2],[0,0,0,2],[0,0,1,2],[0,0,0,2], // joystick down, hold, release, up
 [0x81,77,0,1],[0x80,75,0,1], // native cursor right/left (Shift)
 [0,0,8,2],[0,0,0,2],[0,0,4,2],[0,0,0,2], // joystick page right/left
 [13,28,0,1],[0,0,16,2],[0,0,0,2] // Return and fire, including release
]);
console.log('PASS AGI idle picker: exact C64 client main loop sends cursor up/down, page left/right, Return, port-2 joystick/fire and release; held stick produces no duplicate press');

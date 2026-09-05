// Actual C64 main loop plus native NES picker packets. Never invoke scanners
// directly: input changes only after the initial menu has been acknowledged.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import path from 'node:path';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {loadNesTerminal,NES_INPUT as S} from '../tools/nes_terminal.mjs';
const [wirePath,prgPath,manifestPath,reportPath]=process.argv.slice(2);
assert.ok(wirePath&&prgPath&&manifestPath&&reportPath);
const manifest=JSON.parse(fs.readFileSync(manifestPath));
const {buildMpe3TitleTerminal,MPE3_TITLE_TERMINAL_STATE:state}=await loadNesTerminal(path.resolve(import.meta.dirname,'../../vm/client'));
const program=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false,
 diagnosticTitle:manifest.diagnosticTitle,diagnosticFooter:manifest.diagnosticFooter});
assert.deepEqual(program.prg,fs.readFileSync(prgPath),'exact packaged C64 client required');
const wire=fs.readFileSync(wirePath),frames=[[]];
for(let at=0;at<wire.length;){
 const size=wire.readUInt16LE(at);at+=2;assert.ok(size>=10&&size<=238&&at+size<=wire.length);
 const packet=wire.subarray(at,at+size);at+=size;frames.at(-1).push(packet);
 if(packet[3]===2&&(packet[5]&1))frames.push([]);
}
assert.equal(frames.length,6,'native picker must emit initial + four idle frame ends');
for(const frame of frames.slice(1,5))assert.ok(frame.length===1&&frame[0][3]===2&&frame[0][5]===0x25,
 'idle menu must emit frame ends only, not audio-only heartbeats or bitmap cells');
const packets=[...frames[0],...Array.from({length:105},()=>frames[1][0])].map((packet,index)=>{
 const b=Buffer.from(packet);b[4]=index%255+1;let crc=65535;
 for(const value of b.subarray(0,-2)){crc^=value<<8;for(let n=0;n<8;n++)crc=((crc<<1)^((crc&0x8000)?0x1021:0))&65535;}
 b.writeUInt16LE(crc,b.length-2);return b;
});
const changes=new Map([
 [2,{keys:[[0,7]]}],[5,{}], // Down, held, release
 [8,{keys:[[0,7],[1,7]]}],[11,{keys:[[1,7]]}],[14,{}], // Up; release cursor before Shift
 [17,{keys:[[0,2],[6,4]]}],[20,{}], // Left with right Shift
 [23,{keys:[[0,2]]}],[26,{}], // Right
 [29,{joy:2}],[32,{}],[35,{joy:1}],[38,{}],
 [41,{joy:8}],[44,{}],[47,{joy:4}],[50,{}],
 [53,{keys:[[0,1]]}],[56,{}],[59,{joy:16}],[62,{}], // Return / Fire
 [65,{keys:[[7,4]]}],[68,{}],[71,{keys:[[1,7]]}],[74,{}], // Space / standalone Shift
 [77,{keys:[[0,7],[0,2]],joy:16}],[80,{}], // keyboard diagonal + Fire
 [83,{joy:3}],[86,{}], // opposing joystick directions neutralize
 [89,{keys:[[0,3],[7,2],[7,5]]}],[92,{}], // existing Sharp hotkey, held then released
 [95,{keys:[[0,3],[7,2],[7,5]]}],[98,{}]
]);
let index=0,ends=0,initialMode,hiddenWrites=0;
const events=[];
const publish=cpu=>{const packet=packets[index];cpu.ram.set(packet,0xdf00);cpu.ram[0xdff7]=packet[4];cpu.ram[0xdff5]=2;};
const service={onWrite(cpu,address,value){
 if(ends&&((address===0xd011&&!(value&16))||(address===state.transitionHidden&&value)))hiddenWrites++;
 if(address===0xdff4&&value===1)publish(cpu);
 if(address===0xdff4&&value===3){
  const event=[S.buttonsRegister,S.displayRegister,S.overflowRegister,S.protocolRegister,S.sequenceRegister,S.checksumRegister].map(a=>cpu.ram[a]);
  assert.equal(event[5],event.slice(0,5).reduce((sum,n)=>sum^n,0xa5));assert.equal(event[2],0);assert.equal(event[3],S.protocol);
  initialMode??=event[1];events.push(event.slice(0,2));cpu.ram[S.ack]=event[4];
 }
 if(address===0xdff6&&value){
  assert.equal(value,packets[index][4]);
  if(packets[index][3]===2&&(packets[index][5]&1)){
   const change=changes.get(++ends);
   if(change){cpu.controls.matrix.fill(0);cpu.controls.port2Bits=change.joy??0;
    for(const [row,column] of change.keys??[])cpu.controls.matrix[row]|=1<<column;}
  }
  if(++index<packets.length)publish(cpu);
 }
}};
const cpu=new C64TerminalCpu(program,service,{recordWrites:false});
cpu.runUntil(c=>index===packets.length||c.pc===program.labels.terminal_error_hold,30000000);
assert.equal(index,packets.length);assert.equal(hiddenWrites,0,'idle input must not blank the menu');
assert.equal(cpu.ram[S.active],1);
const buttons=[0,32,0,16,0,64,0,128,0,32,0,16,0,128,0,64,0,8,0,1,0,2,0,4,0,161,0];
const expected=buttons.map(b=>[b,initialMode]);
// Old fast candidate toggles Sharp; the indexed candidate directly selects it.
expected.push([0,S.protocol===0x81?0:3]);if(S.protocol===0x81)expected.push([0,1]);
assert.deepEqual(events,expected,'cursor/joystick/Return/Fire/held/release/chord events from the real main loop');
fs.writeFileSync(reportPath,JSON.stringify({passed:true,physicalAcceptance:false,packets:packets.length,frames:ends,
 protocol:S.protocol,events,hiddenWrites,checks:['idle frame-end handshake','keyboard cursor directions and Shift modifier release',
 'joystick directions, Return/Fire launch inputs','held/release and opposing directions','diagonal keyboard plus Fire with physical matrix model','Sharp hotkey preserved']},null,2)+'\n');
console.log(`PASS NES picker C64 main loop: ${events.length} input transitions, arrows/Shift, joystick, Return/Fire, held/release/matrix combinations, no blanking; no physical timing claim`);

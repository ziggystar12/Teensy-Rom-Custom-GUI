import assert from 'node:assert/strict';
import path from 'node:path';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {MPE4_INPUT as k} from '../../vm/client/host/mpe4-keyboard.mjs';
import {loadDosTerminal} from '../tools/dos_terminal.mjs';
const {buildMpe3TitleTerminal,MPE3_TITLE_TERMINAL_STATE:s}=await loadDosTerminal(path.resolve(import.meta.dirname,'../../vm/client'));
const p=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false});
const cpu=new C64TerminalCpu(p,null,{rasterInterruptPeriod:0,recordWrites:false}),l=p.labels;
const call=name=>{cpu.sp=0xfd;cpu.push(0x7f);cpu.push(0xfe);cpu.pc=l[name];cpu.runUntil(c=>c.pc===0x7fff,100000);};
const scan=keys=>{cpu.controls.matrix.fill(0);for(const [r,c] of keys)cpu.controls.matrix[r]|=1<<c;call('dos_capture_input');};
const drain=()=>{const events=[];for(let n=0;n<40;n++){
 call('sample_game_input');if(!cpu.ram[k.pending])return events;
 events.push({protocol:cpu.ram[k.flags],scan:cpu.ram[k.scan],key:cpu.ram[k.key]});cpu.ram[k.ack]=cpu.ram[k.sequence];
}assert.fail('input did not drain');};
call('game_input_init');scan([]);drain();const mods=[[7,2],[7,5]];
for(const [column,mode] of [[3,3],[4,0],[5,1],[6,2]]){
 scan([]);drain();scan([[0,column]]);assert.ok(drain().some(e=>e.protocol===0x80&&e.scan===59+mode*2));
 for(const shift of [[1,7],[6,4]]){scan([]);drain();scan([[0,column],...mods,shift]);assert.ok(!drain().some(e=>e.protocol===0x90));}
 scan([]);drain();scan([[0,column],...mods]);let events=drain();
 assert.ok(events.some(e=>e.protocol===0x90&&e.scan===mode));
 assert.ok(events.filter(e=>e.protocol!==0x90).every(e=>!e.scan&&!e.key));
 scan([[0,column],...mods]);assert.ok(!drain().some(e=>e.protocol===0x90),'hold is not toggle');
 scan([[0,column]]);assert.ok(drain().every(e=>!e.scan&&!e.key),'modifier-first release must not leak Fn');
 scan([]);drain();
}
// Non-video Shift+Ctrl keys retain their original 83h snapshot.
scan([[1,2],[1,7],[7,2]]);assert.ok(drain().some(e=>e.protocol===0x83&&e.scan===30));scan([]);drain();
for(const mode of [0,1,2,3]){
 cpu.ram[p.stageAddress+6]=4;cpu.ram[p.stageAddress+8]=2;cpu.ram[p.stageAddress+9]=mode;
 cpu.ram[p.stageAddress+10]=mode===1||mode===2?1:0;cpu.ram[p.stageAddress+11]=mode===0?6:0;
 cpu.a=2;cpu.pc=l.mpe_video_resume;cpu.runUntil(c=>c.pc===l.ack_packet,10000);
 assert.equal(cpu.ram[0xd021],mode===0?6:0);assert.equal(cpu.ram[0xd016],mode===0?0x18:8);
 const rasterState=cpu.ram.slice(0x02e0,0x02ee);scan([[1,2]]);
 assert.deepEqual(cpu.ram.slice(0x02e0,0x02ee),rasterState,'DOS matrix must not overlap raster code/state');scan([]);drain();
 assert.equal(cpu.ram[s.frameMode],mode===0?0x18:8);
}
console.log('PASS: DOS ordinary/shifted keys, direct selectors, hold/release suppression, independent protocol, nonblack backgrounds, matrix/raster isolation');

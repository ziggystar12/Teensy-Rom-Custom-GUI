import assert from 'node:assert/strict';
import path from 'node:path';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {loadNesTerminal} from '../tools/nes_terminal.mjs';
const {buildMpe3TitleTerminal,MPE3_TITLE_TERMINAL_STATE:s}=await loadNesTerminal(path.resolve(import.meta.dirname,'../../vm/client'));
const p=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false});
const cpu=new C64TerminalCpu(p,null,{rasterInterruptPeriod:0,recordWrites:false}),l=p.labels;
const call=name=>{cpu.sp=0xfd;cpu.push(0x7f);cpu.push(0xfe);cpu.pc=l[name];cpu.runUntil(c=>c.pc===0x7fff,100000);};
const scan=keys=>{cpu.controls.matrix.fill(0);for(const [r,c] of keys)cpu.controls.matrix[r]|=1<<c;call('nes_capture_input');};
call('game_input_init');assert.equal(cpu.ram[l.nes_sharp],0);
const mods=[[7,2],[7,5]];
for(const [column,mode] of [[3,3],[4,0],[5,1],[6,2]]){
 scan([]);scan([[0,column]]);assert.notEqual(cpu.ram[l.nes_sharp_held],1<<column,'plain Fn is not a selector');
 scan([]);scan([[0,column],mods[0]]);assert.equal(cpu.ram[l.nes_sharp_held],0);
 scan([]);scan([[0,column],mods[1]]);assert.equal(cpu.ram[l.nes_sharp_held],0);
 for(const shift of [[1,7],[6,4]]){scan([]);scan([[0,column],...mods,shift]);assert.equal(cpu.ram[l.nes_sharp_held],0);}
 scan([]);scan([[0,column],...mods]);assert.equal(cpu.ram[l.nes_sharp],mode);
 scan([[0,column],...mods]);assert.equal(cpu.ram[l.nes_sharp],mode,'held selection is not a toggle');
 if(mode===1)assert.equal(cpu.ram[l.nes_candidate]&128,0,'F3 ghost must not move right');
 scan([[0,column]]);assert.equal(cpu.ram[l.nes_sharp],mode,'modifiers released first');
 scan([]);assert.equal(cpu.ram[l.nes_sharp_held],0);
}
scan([[0,3],[0,4],...mods]);assert.equal(cpu.ram[l.nes_sharp_held],0,'multiple Fn rejected');
// Exercise the emitted receiver directly; VIC cycle timing is separately
// covered by video_raster.mjs, not this instruction-only CPU.
for(const mode of [1,2,3,0]){
 cpu.ram[p.stageAddress+6]=3;cpu.ram[p.stageAddress+8]=2;cpu.ram[p.stageAddress+9]=mode;cpu.ram[p.stageAddress+10]=mode===1||mode===2?1:0;
 cpu.a=2;cpu.pc=l.mpe_video_resume;cpu.runUntil(c=>c.pc===l.ack_packet,10000);
 assert.equal(cpu.ram[0xd016],mode===0?0x18:8);assert.equal(cpu.ram[s.frameMode],mode===0?0x18:8);
 assert.equal(cpu.ram[0xd018],0x78);assert.equal(cpu.ram[0xd011],0x3b);assert.equal(cpu.ram[0xd01a],1);
 assert.equal(cpu.ram[0xfffe]|cpu.ram[0xffff]<<8,mode===1||mode===2?l.mpe_video_irq:l.raster_irq);
}
// Override raster reads only for functional handshake tests; real cycle
// placement is independently exercised by the VICE test.
let line=251;
const read=cpu.read.bind(cpu);cpu.read=a=>a===0xd012?line&255:a===0xd011?(cpu.ram[a]&127)|(line>=256?128:0):read(a);
cpu.recordWrites=true;
for(const [bank,enhanced] of [[1,1],[0,0],[1,0],[0,1]]){
 cpu.writes.length=0;cpu.ram[p.stageAddress+9]=2;cpu.ram[p.stageAddress+10]=enhanced|bank<<1;
 cpu.pc=l.mpe_video_stream;cpu.runUntil(c=>c.pc===l.ack_packet,10000);
 assert.equal(cpu.ram[0x02e4],1);assert.ok(!cpu.writes.some(w=>w.address===0xd011||w.address===0xdd00));
 // Late grants do not authorize DMA, including line 251 in the upper half.
 for(line of [10,249,253,507]){cpu.ram[0xdff4]=0;call('mpe_video_border_tick');assert.equal(cpu.ram[0xdff4],0);}
 line=251;call('mpe_video_border_tick');assert.equal(cpu.ram[0xdff4],5);
 cpu.pc=l.mpe_video_flip;cpu.runUntil(c=>c.pc===l.mpe_flip_wait,10000);
 assert.equal(cpu.ram[0x02e9],1);const old=cpu.ram[0xdd00];line=100;call('mpe_video_border_tick');assert.equal(cpu.ram[0xdd00],old);
 line=251;call('mpe_video_border_tick');assert.equal(cpu.ram[0xdd00]&3,bank?1:2);assert.equal(cpu.ram[0x02ed],bank?0xc0:0x30);
 assert.equal(cpu.ram[0x02e9],0);assert.equal(cpu.ram[0x02e4],0);assert.equal(cpu.ram[0x02e3],enhanced);
 assert.equal(cpu.ram[0xfffe]|cpu.ram[0xffff]<<8,enhanced?l.mpe_video_irq:l.raster_irq);
 cpu.pc=l.mpe_flip_wait;cpu.runUntil(c=>c.pc===l.ack_packet,10000);
 assert.ok(!cpu.writes.some(w=>w.address===0xd011&&!(w.value&16)),'no blank frame during stream or flip');
}
console.log('PASS: exact video keys and mode transitions; invisible stream arm; late-grant rejection; border-only alternating bank/kernel flips; no DEN blanking');

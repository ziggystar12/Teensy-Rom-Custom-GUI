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
console.log('PASS: exact Commodore+Control F1/F3/F5/F7, Shift exclusions, held/release, multi-key rejection, F3 ghost suppression, enhanced -> Sharp -> Default receiver transitions');

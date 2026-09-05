import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {C64TerminalCpu} from '../../vm/tests/helpers/c64-terminal-cpu.mjs';
import {buildMpe3TitleTerminal,MPE3_TITLE_TERMINAL_STATE} from '../../vm/client/host/mpe3-title-terminal.mjs';
import {MPE4_EGO_SPRITES} from '../../vm/client/host/mpe4-ego-sprites.mjs';
const [wirePath,terminalPath,outputPath]=process.argv.slice(2);
assert.ok(wirePath&&terminalPath&&outputPath,'wire.bin terminal.prg report.json required');
const bytes=fs.readFileSync(wirePath),packets=[];
for(let offset=0;offset<bytes.length;){const length=bytes.readUInt16LE(offset);offset+=2;assert.ok(length>=10&&length<=238&&offset+length<=bytes.length);packets.push(bytes.subarray(offset,offset+length));offset+=length;}
const manifest=JSON.parse(fs.readFileSync(new URL('../../build/agivm/client.json',import.meta.url)));
const program=buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:true,publishVideoTiming:manifest.publishVideoTiming,diagnosticTitle:manifest.diagnosticTitle,diagnosticFooter:manifest.diagnosticFooter});
assert.deepEqual(fs.readFileSync(terminalPath),program.prg,'exact packaged presenter required');
const reference=Buffer.alloc(10000);let index=0,sidFrames=0,gameFrames=0,acknowledged=0;
const stagedShape=Buffer.alloc(256);let shapeParts=0,visibleShape=null,spritePackets=0,spriteFrames=0;
function publish(cpu){const p=packets[index];cpu.ram.set(p,0xdf00);cpu.ram[0xdff7]=p[4];cpu.ram[0xdff5]=2;
  if(p[3]===5){assert.equal(p[6],130);assert.equal(p[8],1);assert.equal(p[9],shapeParts===0?0:1);p.copy(stagedShape,p[9]*128,10,138);shapeParts|=1<<p[9];spritePackets++;}
  if(p[3]===1)for(let r=8;r<8+p[6];r+=12){const c=p.readUInt16LE(r);assert.ok(c<1000);p.copy(reference,c*8,r+2,r+10);reference[8000+c]=p[r+10];reference[9000+c]=p[r+11];}}
const service={onWrite(cpu,address,value){
  if(address===0xdff4&&value===1)publish(cpu);
  if(address===0xdff6&&value){const p=packets[index];assert.equal(value,p[4]);
    if(p[3]===2){assert.deepEqual(Buffer.from(cpu.ram.subarray(0x6000,0x7f40)),reference.subarray(0,8000));assert.deepEqual(Buffer.from(cpu.ram.subarray(0x5c00,0x5fe8)),reference.subarray(8000,9000));assert.deepEqual(Buffer.from(cpu.ram.subarray(0xd800,0xdbe8)),reference.subarray(9000));assert.equal(cpu.ram[MPE3_TITLE_TERMINAL_STATE.frameMode]&16,p[5]&4?0:16);assert.equal(cpu.ram[MPE3_TITLE_TERMINAL_STATE.parserSplit],p[5]&4?0:p[5]&0x40);sidFrames++;if(p[5]&32)gameFrames++;
      if(p[6]===37){const d=p.subarray(34,45);assert.equal(d[0],1);assert.equal(cpu.ram[0xd015]&0x1e,d[1]);
        assert.ok(shapeParts===0||shapeParts===3);if(shapeParts===3){visibleShape=Buffer.from(stagedShape);shapeParts=0;}
        if(visibleShape)for(let layer=0;layer<4;layer++){
          const address=0x4000+cpu.ram[MPE4_EGO_SPRITES.pointerTable+layer]*64;
          assert.deepEqual(Buffer.from(cpu.ram.subarray(address,address+64)),visibleShape.subarray(layer*64,layer*64+64));
        }
        for(let layer=0;layer<4;layer++){
          assert.equal(cpu.ram[0xd002+layer*2],d[2]);assert.equal((cpu.ram[0xd010]>>(layer+1))&1,d[3]);
          assert.equal(cpu.ram[0xd003+layer*2],(d[4]+(layer%2)*21)&255);assert.equal(cpu.ram[0xd028+layer],d[7+layer]);
        }
        assert.equal(cpu.ram[0xd025],d[5]);assert.equal(cpu.ram[0xd026],d[6]);spriteFrames++;
      }else{assert.equal(p[6],26);assert.equal(cpu.ram[0xd015]&0x1e,0);}
    }
    acknowledged++;index++;if(index<packets.length)publish(cpu);
  }
}};
const cpu=new C64TerminalCpu(program,service,{recordWrites:false});
cpu.runUntil(c=>acknowledged===packets.length||c.pc===program.labels.terminal_error_hold,100000000);
assert.equal(acknowledged,packets.length);assert.notEqual(cpu.pc,program.labels.terminal_error_hold);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const result={passed:true,exactNativeWire:true,real6510Presenter:true,packets:packets.length,sidFrames,gameFrames,
  spritePackets,spriteFrames,
  instructions:cpu.instructions,terminalSha256:sha(program.prg),wireSha256:sha(bytes),finalDisplaySha256:sha(reference),
  scope:'Every acknowledged native frame matches C64 bitmap, screen, color RAM, sprite shapes, pointers and VIC coordinates/palette/enables; keyboard/input events have separate native ISR and actual6510 proofs. Deterministic execution is not physical timing proof.'};
fs.writeFileSync(outputPath,JSON.stringify(result,null,2)+'\n');console.log(`PASS AGI C64 wire: ${packets.length} packets, ${sidFrames} exact display frames, ${spritePackets} sprite packets; no physical timing claim`);

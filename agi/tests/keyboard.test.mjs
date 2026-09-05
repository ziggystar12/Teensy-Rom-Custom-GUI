import assert from "node:assert/strict";
import test from "node:test";
import { C64TerminalCpu } from "../../vm/tests/helpers/c64-terminal-cpu.mjs";
import { buildMpe3TitleTerminal, MPE3_TITLE_TERMINAL_STATE } from "../../vm/client/host/mpe3-title-terminal.mjs";
import { MPE4_INPUT as S } from "../../vm/client/host/mpe4-keyboard.mjs";
import { MPE4_MOUSE as M } from "../../vm/client/host/mpe4-mouse.mjs";
const crc16CcittFalse=bytes=>{let c=65535;for(const v of bytes){c^=v<<8;for(let i=0;i<8;i++)c=((c<<1)^((c&32768)?0x1021:0))&65535;}return c;};

const program=buildMpe3TitleTerminal({gameplay:true});
const registers=[S.keyRegister,S.scanRegister,S.joyRegister,S.flagsRegister,S.sequenceRegister,S.checksumRegister];
function setup({arm=true,enable1351Mouse=true}={}) {
  const selected=enable1351Mouse?program:buildMpe3TitleTerminal({gameplay:true,enable1351Mouse:false});
  const events=[];
  const service={onWrite(cpu,address,value){if(address===0xdff4&&value===3)events.push(registers.map(r=>cpu.ram[r]));}};
  const cpu=new C64TerminalCpu(selected,service,{rasterInterruptPeriod:0});
  cpu.call(selected.labels.game_input_init,{maxInstructions:1000});
  const scan=()=>cpu.call(selected.labels.sample_game_input,{maxInstructions:4000});
  const keys=(...coordinates)=>{
    cpu.controls.matrix.fill(0);
    for(const [row,column] of coordinates)cpu.controls.matrix[row]|=1<<column;
  };
  const ack=()=>{cpu.ram[S.ack]=cpu.ram[S.sequence];};
  if(arm)scan();
  return {cpu,events,scan,keys,ack};
}
function event(actual,key,scan,joy=0,flags=1,sequence=1) {
  assert.deepEqual(actual,[key,scan,joy,flags,sequence,0xa5^key^scan^joy^flags^sequence]);
}

// Independent physical positions from VICE's documented C64 matrix. Expected
// IBM scan codes describe AGI's input contract, not the generated lookup table.
const letters=[['a',1,2,30],['b',3,4,48],['c',2,4,46],['d',2,2,32],['e',1,6,18],['f',2,5,33],
  ['g',3,2,34],['h',3,5,35],['i',4,1,23],['j',4,2,36],['k',4,5,37],['l',5,2,38],
  ['m',4,4,50],['n',4,7,49],['o',4,6,24],['p',5,1,25],['q',7,6,16],['r',2,1,19],
  ['s',1,5,31],['t',2,6,20],['u',3,6,22],['v',3,7,47],['w',1,1,17],['x',2,7,45],['y',3,1,21],['z',1,4,44]];
const punctuation=[['1','!',7,0,2],['2','"',7,3,3],['3','#',1,0,4],['4','$',1,3,5],
  ['5','%',2,0,6],['6','&',2,3,7],['7',"'",3,0,8],['8','(',3,3,9],['9',')',4,0,10],['0','0',4,3,11],
  ['+','+',5,0,13],['-','_',5,3,12],['.','>',5,4,52],[':','[',5,5,39],['@','@',5,6,3],
  [',','<',5,7,51],['\\','\\',6,0,43],['*','*',6,1,55],[';',']',6,2,39],['=','=',6,5,13],
  ['^','^',6,6,7],['/','?',6,7,53],['_','_',7,1,12],[' ',' ',7,4,57]];

test("MPE4 real6510 scans all letters, digits and punctuation with either physical Shift",()=>{
  for(const [ch,row,col,scancode] of letters)for(const shift of [null,[1,7],[6,4]]) {
    const {events,keys,scan}=setup();keys([row,col],...(shift?[shift]:[]));scan();
    assert.equal(events.length,1);event(events[0],(shift?ch.toUpperCase():ch).charCodeAt(0),scancode);
  }
  for(const [plain,shifted,row,col,scancode] of punctuation)for(const shift of [false,true]) {
    const {events,keys,scan}=setup();keys([row,col],...(shift?[[1,7]]:[]));scan();
    event(events[0],(shift?shifted:plain).charCodeAt(0),scancode);
  }
});

test("MPE4 real6510 Return, delete, escape, home, cursors and all eight F keys use AGI codes",()=>{
  const expected=[[[0,0],false,8,14],[[0,1],false,13,28],[[7,7],false,27,1],[[6,3],false,0x84,71],
    [[0,2],false,0x81,77],[[0,2],true,0x80,75],[[0,7],false,0x83,80],[[0,7],true,0x82,72],
    [[0,4],false,0x90,59],[[0,4],true,0x91,60],[[0,5],false,0x92,61],[[0,5],true,0x93,62],
    [[0,6],false,0x94,63],[[0,6],true,0x95,64],[[0,3],false,0x96,65],[[0,3],true,0x97,66]];
  for(const [position,shift,key,scancode] of expected) {
    const {events,keys,scan}=setup();keys(position,...(shift?[[6,4]]:[]));scan();event(events[0],key,scancode);
  }
});

test("MPE4 CTRL translates letters only; Commodore retains IBM scan bindings with zero ASCII",()=>{
  for(const [ch,row,col,scancode] of letters)for(const shift of [false,true]) {
    const {events,keys,scan}=setup();keys([row,col],[7,2],...(shift?[[1,7]]:[]));scan();
    event(events[0],ch.charCodeAt(0)&31,scancode);
  }
  for(const [plain,shifted,row,col,scancode] of punctuation) {
    const {events,keys,scan}=setup();keys([row,col],[7,2],[1,7]);scan();event(events[0],shifted.charCodeAt(0),scancode);
  }
  for(const [ch,row,col,scancode] of letters) {
    const {events,keys,scan}=setup();keys([row,col],[7,5]);scan();event(events[0],0,scancode);
  }
});

test("MPE4 release arming rejects held launch keys and joystick; modifiers alone never send input",()=>{
  for(const held of [[0,1],[7,4],[7,7]]) {
    const {cpu,events,keys,scan}=setup({arm:false});keys(held);scan();scan();
    assert.equal(cpu.ram[S.armed],0);assert.equal(events.length,0);
    keys();scan();assert.equal(cpu.ram[S.armed],1);keys(held);scan();assert.equal(events.length,1);
  }
  const {cpu,events,keys,scan}=setup({arm:false});cpu.controls.port2Bits=16;scan();assert.equal(cpu.ram[S.armed],0);
  cpu.controls.port2Bits=0;scan();
  for(const modifier of [[1,7],[6,4],[7,2],[7,5]]){keys(modifier);scan();assert.equal(events.length,0);}
});

test("MPE4 repeat starts at20 raster ticks, repeats at4, wraps correctly, and release rearms a fresh press",()=>{
  const {cpu,events,keys,scan,ack}=setup();cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=250;
  keys([1,2]);scan();ack();assert.equal(events.length,1);
  for(const tick of [250,251,255,0,13]){cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=tick;scan();}
  assert.equal(events.length,1);
  cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=14;scan();ack();assert.equal(events.length,2);
  cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=17;scan();assert.equal(events.length,2);
  cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=18;scan();ack();assert.equal(events.length,3);
  keys();scan();assert.equal(events.length,3);keys([1,2]);scan();event(events.at(-1),97,30,0,1,4);
});

test("MPE4 joystick directions, diagonals, fire, and release are independent events",()=>{
  for(const pins of [1,2,4,8,5,9,6,10,16,17,18,20,24]) {
    const {cpu,events,scan,ack}=setup();cpu.controls.port2Bits=pins;scan();event(events[0],0,0,pins,2,1);
    ack();scan();assert.equal(events.length,1);cpu.controls.port2Bits=0;scan();event(events[1],0,0,0,2,2);
  }
  const {cpu,events,keys,scan}=setup();cpu.controls.port2Bits=9;keys([2,2]);scan();event(events[0],100,32,9,3,1);
});

test("MPE4 held directions and function/control keys do not repeat tap-toggle actions",()=>{
  for(const held of [[[0,2]],[[0,7]],[[0,2],[1,7]],[[0,7],[1,7]],[[6,3]],[[0,6]],[[0,1]],[[1,2],[7,2]],[[1,2],[7,5]]]) {
    const {cpu,events,keys,scan,ack}=setup();keys(...held);scan();ack();
    for(const tick of [20,24,28,80,240,8]){cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=tick;scan();ack();}
    assert.equal(events.length,1);
    keys();scan();keys(...held);scan();assert.equal(events.length,2,"release permits a distinct second press");
  }
  const {cpu,events,keys,scan,ack}=setup();keys([0,0]);scan();ack();
  cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=20;scan();assert.equal(events.length,2,"backspace retains editing repeat");
});

test("MPE4 masks port1 electrical lows without discarding keys in unaffected columns",()=>{
  for(const low of [1,2,4,8,16,31]) {
    const {cpu,events,keys,scan,ack}=setup();cpu.controls.port1Bits=low;scan();
    assert.equal(events.filter(e=>e[3]&1).length,0);ack();
    keys([2,6]);scan();const keyEvent=events.find(e=>e[3]&1);event(keyEvent,116,20,0,1,keyEvent[4]);
  }
});

test("MPE4 pending payload is immutable until matching ACK and input sequence skips zero",()=>{
  const {cpu,events,keys,scan,ack}=setup();cpu.ram[S.sequence]=254;
  cpu.ram.fill(0x5a,0xdf00,0xdff0);cpu.ram[0xdff6]=0x77;cpu.ram[0xdff7]=0x99;
  keys([1,2]);scan();event(events[0],97,30,0,1,255);
  keys([3,4]);cpu.controls.port2Bits=16;cpu.ram[S.ack]=254;scan();scan();
  assert.deepEqual(events[1],events[0]);assert.deepEqual(events[2],events[0]);
  ack();scan();event(events[3],98,48,16,3,1);
  assert.equal(cpu.ram[S.pending],1);assert.equal(cpu.ram[0xdff6],0x77);assert.equal(cpu.ram[0xdff7],0x99);
  assert.ok(cpu.ram.subarray(0xdf00,0xdff0).every(b=>b===0x5a));
  const writes=cpu.writes.filter(w=>registers.includes(w.address)||w.address===0xdff4).slice(-7);
  assert.deepEqual(writes.map(w=>w.address),[...registers,0xdff4],"command commits only after stable payload and checksum");
});

function packet(type,sequence,flags,payload=Buffer.alloc(0)) {
  const bytes=Buffer.alloc(240);bytes.set([0x4d,0x33,1,type,sequence,flags,payload.length,0]);bytes.set(payload,8);
  bytes.writeUInt16LE(crc16CcittFalse(bytes.subarray(0,8+payload.length)),8+payload.length);return bytes;
}
test("MPE4 SID0x20 activates gameplay once, releases intro mute, and requires held skip release",()=>{
  const sid=Buffer.alloc(26);sid[1]=0x55;sid[25]=15;
  const cells=Buffer.alloc(12);cells[10]=0x10;
  const packets=[packet(1,1,14,cells),packet(2,2,0x25,sid),packet(2,3,0x25,sid),packet(3,4,0)];
  let index=0;
  const service={onWrite(cpu,address,value){
    const publish=()=>{cpu.ram.set(packets[index],0xdf00);cpu.ram[0xdff7]=packets[index][4];cpu.ram[0xdff5]=1;};
    if(address===0xdff4&&value===1)publish();
    if(address===0xdff6&&value){index++;if(index===1){cpu.ram[MPE3_TITLE_TERMINAL_STATE.skipSent]=1;cpu.controls.space=true;}if(index<packets.length)publish();}
  }};
  const cpu=new C64TerminalCpu(program,service);cpu.runUntil(c=>c.pc===program.labels.finished||c.pc===program.labels.terminal_error_hold);
  assert.equal(cpu.pc,program.labels.finished);assert.equal(cpu.ram[S.active],1);assert.equal(cpu.ram[S.armed],0);
  assert.equal(cpu.ram[MPE3_TITLE_TERMINAL_STATE.skipSent],0);assert.equal(cpu.ram[0xd400],0x55);assert.equal(cpu.ram[0xd418],15);
  assert.equal(cpu.writes.filter(w=>w.address===S.active&&w.value===1).length,1);
  assert.equal(cpu.writes.filter(w=>w.address===0xdff4&&w.value===3).length,0);
  cpu.controls.space=false;cpu.call(program.labels.sample_skip_input,{maxInstructions:4000});assert.equal(cpu.ram[S.armed],1);
});

test("MPE4 malformed SID cannot activate gameplay or release the intro mute",()=>{
  const cpu=new C64TerminalCpu(program,null,{rasterInterruptPeriod:0});
  cpu.ram[MPE3_TITLE_TERMINAL_STATE.baseReady]=1;cpu.ram[MPE3_TITLE_TERMINAL_STATE.skipSent]=1;
  cpu.ram[program.stageAddress+5]=0x25;cpu.ram[program.stageAddress+6]=25;cpu.pc=program.labels.dispatch_sid;
  cpu.runUntil(c=>c.pc===program.labels.terminal_error_hold);
  assert.equal(cpu.ram[S.active],0);assert.equal(cpu.ram[MPE3_TITLE_TERMINAL_STATE.skipSent],1);
  assert.equal(cpu.ram[MPE3_TITLE_TERMINAL_STATE.error],8);
});

function mouseSetup() {
  const fixture=setup({arm:false});const {cpu,scan,ack}=fixture;
  cpu.controls.potX=60;cpu.controls.potY=60;scan();
  const frame=(dx=0,dy=0,{ticks=1,acknowledge=true}={})=>{
    if(acknowledge)ack();
    cpu.controls.potX=(cpu.controls.potX+dx)&127;
    cpu.controls.potY=(cpu.controls.potY-dy)&127;
    for(let t=0;t<ticks;t++) {
      cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]++;
      scan();if(acknowledge)ack();
    }
  };
  return {...fixture,frame};
}

test("MPE4 1351 calibrates, confirms real motion, half-scales signed wrap deltas and renders only a local sprite",()=>{
  const {cpu,events,frame}=mouseSetup();
  assert.equal(cpu.ram[M.calibrated],1);assert.equal(events.length,0);assert.equal(cpu.ram[0xd015]&1,0);
  assert.equal(cpu.ram[M.spritePointer],16);assert.equal(cpu.ram[M.sprite],128);
  assert.equal(cpu.ram[M.sprite+21],255);assert.equal(cpu.ram[M.sprite+63],0);
  const before=cpu.writes.length;
  frame(3,0,{ticks:8});event(events.at(-1),81,100,0,4,1);assert.equal(cpu.ram[M.accumX],1);
  assert.equal(cpu.ram[M.present],0);
  frame(3,-4);event(events.at(-1),83,98,0,4,2);assert.equal(cpu.ram[M.accumX],0);
  assert.equal(cpu.ram[M.present],1);assert.equal(cpu.ram[0xd015]&1,1);
  assert.equal(cpu.ram[0xd000],24+83*2);assert.equal(cpu.ram[0xd001],148);
  frame(-6,4);event(events.at(-1),80,100,0,4,3);
  // Reach the modulo-128 boundary with accepted deltas, then cross both ways.
  frame(30,0);frame(30,0);frame(12,0);assert.equal(cpu.controls.potX,4);
  assert.equal(cpu.ram[M.x],116);assert.equal(cpu.ram[0xd000],0);assert.equal(cpu.ram[0xd010]&1,1);
  frame(-12,0);assert.equal(cpu.controls.potX,120);assert.equal(cpu.ram[M.x],110);
  assert.equal(cpu.ram[0xd010]&1,0);
  assert.ok(cpu.writes.slice(before).every(w=>!(w.address>=0x4000&&w.address<0x8000)&&!(w.address>=0xd800&&w.address<0xdc00)),
    'pointer movement leaves bitmap, screen, color and sprite shape bytes unchanged');
});

test("MPE4 1351 rejects analog wobble and ambiguous wraps, clamps all edges, and resets fractional pressure",()=>{
  const {cpu,events,frame}=mouseSetup();frame(4,4,{ticks:8});frame(4,4);
  const original=[cpu.ram[M.x],cpu.ram[M.y]],count=events.length;
  for(const delta of [1,-1,32,-33,63,-63,64])frame(delta,0);
  assert.deepEqual([cpu.ram[M.x],cpu.ram[M.y]],original);assert.equal(events.length,count);
  for(let i=0;i<15;i++)frame(30,30);
  assert.equal(cpu.ram[M.x],159);assert.equal(cpu.ram[M.y],199);
  assert.equal(cpu.ram[M.accumX],0);assert.equal(cpu.ram[M.accumY],0);
  assert.equal(cpu.ram[0xd000],86);assert.equal(cpu.ram[0xd010]&1,1);assert.equal(cpu.ram[0xd001],249);
  for(let i=0;i<15;i++)frame(-30,-30);
  assert.equal(cpu.ram[M.x],0);assert.equal(cpu.ram[M.y],0);
  assert.equal(cpu.ram[0xd000],24);assert.equal(cpu.ram[0xd010]&1,0);assert.equal(cpu.ram[0xd001],50);
  frame(-3,-3);assert.equal(cpu.ram[M.accumX],0);assert.equal(cpu.ram[M.accumY],0);
});

test("MPE4 1351 preserves slow continuous motion after menu clicks while cancelling one-count wobble",()=>{
  for(const direction of [1,-1]) {
    const {cpu,events,scan,ack,frame}=mouseSetup();
    frame(4,4,{ticks:8});frame(4,4);
    assert.equal(cpu.ram[M.present],1);
    // Mouse down/up events use the same path when opening and dismissing an
    // AGI menu. Their ACKs must not suppress subsequent local pointer motion.
    for(let click=0;click<2;click++) {
      ack();cpu.controls.port1Bits=16;scan();assert.equal(events.at(-1)[3],12);
      ack();cpu.controls.port1Bits=0;scan();assert.equal(events.at(-1)[3],4);
    }
    const start=[cpu.ram[M.x],cpu.ram[M.y]],count=events.length;
    for(let i=0;i<16;i++)frame(direction,direction);
    assert.deepEqual([cpu.ram[M.x],cpu.ram[M.y]],start.map(v=>v+direction*8),
      'sixteen raw POT counts become eight logical pixels instead of being lost');
    assert.ok(events.length>count);
    assert.equal(cpu.ram[0xd000],24+cpu.ram[M.x]*2);
    assert.equal(cpu.ram[0xd001],50+cpu.ram[M.y]);
    const settled=[cpu.ram[M.x],cpu.ram[M.y]],settledCount=events.length;
    for(let i=0;i<16;i++)frame(i&1?-1:1,i&1?1:-1);
    assert.deepEqual([cpu.ram[M.x],cpu.ram[M.y]],settled);
    assert.equal(events.length,settledCount,'alternating one-count noise produces no motion event');
  }
});

test("MPE4 1351 POT sampling selects port1, settles, runs once per raster tick and sleeps when idle",()=>{
  const {cpu,scan,frame}=mouseSetup();
  assert.equal(cpu.potReads.length,2);for(let i=0;i<10;i++)scan();assert.equal(cpu.potReads.length,2);
  frame(0,0,{ticks:7});assert.equal(cpu.potReads.length,2);
  frame(4,0);assert.equal(cpu.potReads.length,4);assert.equal(cpu.ram[M.live],1);
  frame(4,0);assert.equal(cpu.potReads.length,6);
  frame(0,0,{ticks:90});assert.equal(cpu.ram[M.live],0);
  const asleep=cpu.potReads.length;frame(0,0,{ticks:7});assert.equal(cpu.potReads.length,asleep);
  frame(4,0);assert.equal(cpu.potReads.length,asleep+2);assert.equal(cpu.ram[M.live],1);
  for(const sample of cpu.potReads) {
    assert.equal(sample.selected,true);
    const selected=cpu.writes.findLast(w=>w.address===0xdc00&&w.value===0x40&&w.instruction<sample.instruction);
    assert.ok(sample.instruction-selected.instruction>=800,'160 settle loops precede each POT read');
  }
  assert.equal(cpu.ram[0xdc02],0xc0);assert.equal(cpu.ram[0xdc00],0x40);assert.equal(cpu.ram[0xdc03],0);
});

test("MPE4 mouse buttons retain held state, release explicitly, and preserve keyboard priority and pending payload",()=>{
  const {cpu,events,keys,scan,ack,frame}=mouseSetup();
  cpu.controls.port1Bits=16;keys([2,2]);scan();event(events.at(-1),100,32,0,1,1);
  const keyboard=[...events.at(-1)];cpu.controls.potX+=20;cpu.controls.port1Bits=17;keys();
  scan();assert.deepEqual(events.at(-1),keyboard,'unacknowledged keyboard bytes remain immutable');
  ack();scan();event(events.at(-1),80,100,0,12,2,'first click is frozen behind keyboard');
  ack();scan();event(events.at(-1),80,100,0,28,3);
  ack();const count=events.length;scan();assert.equal(events.length,count,'held buttons do not repeat');
  cpu.controls.port1Bits=1;scan();event(events.at(-1),80,100,0,20,4);
  ack();cpu.controls.port1Bits=0;scan();event(events.at(-1),80,100,0,4,5);
  frame(0,0,{ticks:8});assert.equal(cpu.ram[M.x],90,'deferred movement remains measurable');
  assert.equal(cpu.ram[S.joy],0);
});

test("MPE4 matrix execution preserves every letter with joystick up and either Shift while mouse POTs move",()=>{
  for(const [ch,row,col,scancode] of letters)for(const shift of [null,[1,7],[6,4]]) {
    const {cpu,events,keys,scan}=mouseSetup();cpu.controls.port2Bits=1;
    cpu.controls.potX=68;cpu.ram[MPE3_TITLE_TERMINAL_STATE.rasterTicks]=8;cpu.ram[M.probe]=7;
    keys([row,col],...(shift?[shift]:[]));scan();
    event(events[0],(shift?ch.toUpperCase():ch).charCodeAt(0),scancode,1,3,1);
  }
  for(const buttons of [1,16,17]) {
    const {cpu,events,keys,scan}=mouseSetup();cpu.controls.port1Bits=buttons;cpu.controls.port2Bits=1;
    keys([2,2],[1,7]);scan();event(events[0],68,32,1,3,1);
  }
});

test("MPE4 electrical model propagates shared port grounds and suppresses unresolvable phantom matrix input",()=>{
  const {cpu,events,keys,scan}=setup();cpu.controls.port2Bits=1;keys([0,1]);
  cpu.ram[0xdc02]=cpu.ram[0xdc03]=0;
  assert.equal(cpu.read(0xdc01)&2,0,'port2 PA0 ground reaches PB1 through held Return');
  scan();event(events.at(-1),0,0,1,2,1);
  assert.equal(events.filter(e=>e[3]&1).length,0,'an electrically indistinguishable contact creates no false key');
});

test("MPE4 joystick-grounded keyboard rows cannot fabricate mouse clicks before or after analog detection",()=>{
  for(const detected of [false,true])for(let row=0;row<5;row++)for(const column of [0,4]) {
    const {cpu,events,keys,scan,ack,frame}=mouseSetup();
    if(detected){frame(4,0,{ticks:8});frame(4,0);assert.equal(cpu.ram[M.present],1);}
    const baseline=events.length;
    cpu.controls.port2Bits=1<<row;scan();ack();keys([row,column]);scan();ack();
    cpu.controls.port2Bits=0;scan();ack();keys();scan();ack();
    assert.equal(events.slice(baseline).filter(e=>e[3]&24).length,0,
      `row${row}/PB${column}, detected=${detected}: no false mouse down`);
    assert.equal(cpu.ram[M.lastButtons],0);
  }
});

test("MPE4 ambiguous joystick contacts preserve held mouse buttons while pointer motion and D remain usable",()=>{
  const {cpu,events,keys,scan,ack,frame}=mouseSetup();frame(4,0,{ticks:8});frame(4,0);
  cpu.controls.port1Bits=16;scan();ack();assert.equal(events.at(-1)[3],12);
  cpu.controls.port2Bits=1;scan();ack();
  cpu.controls.port1Bits=0;keys([0,0]);frame(4,0);
  assert.equal(events.at(-1)[3],12,'ambiguous right ground cannot release left or press right');
  assert.equal(cpu.ram[M.lastButtons],16);
  keys([2,2]);scan();ack();const keyEvent=events.at(-1);event(keyEvent,100,32,1,1,keyEvent[4]);
  keys();cpu.controls.port2Bits=0;scan();ack();scan();ack();
  assert.equal(events.at(-1)[3],4,'neutral port2 permits the real left release');
  cpu.controls.port1Bits=1;scan();ack();assert.equal(events.at(-1)[3],20,'normal right click remains available');
  cpu.controls.port1Bits=0;scan();assert.equal(events.at(-1)[3],4);
});

test("MPE4 mouse launch buttons must release and Disable1351Mouse omits all mouse behavior",()=>{
  const {cpu,events,scan}=setup({arm:false});cpu.controls.port1Bits=16;scan();scan();
  assert.equal(cpu.ram[S.armed],0);assert.equal(events.length,0);
  cpu.controls.port1Bits=0;scan();assert.equal(cpu.ram[S.armed],1);scan();assert.equal(events.length,0);
  cpu.controls.port1Bits=16;scan();event(events.at(-1),80,100,0,12,1);
  const disabled=setup({enable1351Mouse:false});disabled.cpu.controls.port1Bits=16;
  disabled.cpu.controls.potX=50;disabled.scan();disabled.keys([2,2]);disabled.scan();
  event(disabled.events.at(-1),100,32);assert.equal(disabled.cpu.potReads.length,0);
  assert.equal(disabled.cpu.program.labels.game_mouse_init,undefined);
  assert.equal(disabled.cpu.writes.filter(w=>w.address===0xd015||w.address===M.spritePointer).length,0);
  assert.equal(disabled.cpu.program.enable1351Mouse,false);
  const intro=buildMpe3TitleTerminal();assert.equal(intro.enable1351Mouse,false);
  assert.deepEqual(intro.prg,buildMpe3TitleTerminal({enable1351Mouse:true}).prg,'03 non-gameplay bytes are independent of mouse option');
});

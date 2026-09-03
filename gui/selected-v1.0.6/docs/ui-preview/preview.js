import {assets} from './assets.js';
import {createWidgets,geometry,contains,filenameLines} from './widgets.js';
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d'),ui=createWidgets(ctx,assets);
ctx.imageSmoothingEnabled=false;
const names=['Games','Utilities','Documents','SAVES','SQ1-64-MPE.crt','KQ1-64-MPE.crt','BlackCauldron.crt','Text.txt','Readme.md','MPE_Firmware-V1.0.5.hex','DeathIsNoEvil.sid','Music','Disk01.d64','Arcada.sav','Demo.prg','Notes.txt','KingQuest2.crt','KingQuest3.crt','SpaceQuest2.crt','PoliceQuest1.crt','LeisureSuitLarry.crt','GoldRush.crt','Manhunter1.crt','Manhunter2.crt','MixedCase.txt','AnotherFile.seq','LongFilenameWithExtension.txt','Backups'];
const folders=new Set(['Games','Utilities','Documents','SAVES','Music','Backups']);
const state={scene:'files',top:0,selected:7,drag:null,pressed:null,notice:'',open:true,calc:'128',choice:0,operand:null,operator:null,newNumber:true};
let targets=[],bar=null;
const hit=(id,r,action)=>targets.push({id,r,action});
function menu(){ui.fill(0,0,320,200,ui.paper);ui.fill(0,10,320,1);['TEENSY','File','Edit','View','Disk'].forEach((v,i)=>ui.text(v,[4,52,84,116,148][i],2));ui.text('2:45 PM',274,2);}
function background(){menu();for(let y=18;y<190;y+=8)for(let x=4+(y&8);x<316;x+=16)ui.fill(x,y,1,1);}
function footer(value){ui.fill(0,188,320,12,ui.paper);ui.text(value.slice(0,52),4,191);}
function files(){
  const win=ui.window('SD card');hit('close',win.close,()=>{state.open=false;});
  ui.glyph(['00100','01110','11111','00100','00100'],10,29);ui.text('/ ',24,29);ui.text('28 items',247,29);
  const g=geometry.grid;
  for(let i=0;i<16;i++){
    const index=state.top+i,name=names[index];if(!name)break;
    const x=g.x+(i%4)*g.width,y=g.y+Math.floor(i/4)*g.height;
    const id=folders.has(name)?'games':name.endsWith('.d64')?'drive8':name.endsWith('.crt')?'program':'document';
    ui.icon(id,x+24,y);
    filenameLines(name).forEach((s,line)=>{const tx=x+Math.floor((72-(s.length*6-1))/2);if(index===state.selected)ui.fill(tx-2,y+19+line*8,s.length*6+3,8);ui.text(s,tx,y+19+line*8,index===state.selected?ui.paper:ui.ink);});
    hit('file'+index,[x,y,72,35],()=>{state.selected=index;});
  }
  bar=ui.scrollbar(geometry.scroll,state.top,names.length,16);
  hit('up',bar.up,()=>scroll(-4));hit('down',bar.down,()=>scroll(4));
  footer(state.notice||names[state.selected]||'Select a file');
}
function setTop(top){const slot=state.selected-state.top;state.top=Math.max(0,Math.min(names.length-16,top));state.selected=Math.min(names.length-1,state.top+Math.max(0,Math.min(15,slot)));}
function scroll(delta){setTop(state.top+delta);}
function dialog(kind){
  files();targets=[];ui.fill(22,40,276,120,ui.paper);
  const title=kind==='firmware'?'Update firmware':kind==='delete'?'Delete file':'MHS Desktop';
  const win=ui.window(title,geometry.dialog);hit('dialog-close',win.close,()=>setScene('files'));
  const name=kind==='firmware'?'MPE_Firmware-V1.0.5.hex':'Notes.txt';
  ui.text(name,36,70);
  ui.text(kind==='firmware'?'Install this firmware on TeensyROM?':'Permanently delete this file?',36,89);
  ui.text(kind==='firmware'?'Keep power on until it restarts.':'This file cannot be recovered.',36,100);
  hit('cancel',ui.button('Cancel',118,130,70,{focused:state.choice===0,pressed:state.pressed==='cancel'}),()=>setScene('files'));
  hit('accept',ui.button(kind==='firmware'?'Update':'Delete',204,130,70,{focused:state.choice===1,pressed:state.pressed==='accept'}),()=>{state.notice='Preview only - no changes made.';setScene('files');});
}
function calculator(){
  background();const w=ui.window('Calculator');hit('close',w.close,()=>setScene('files'));
  ui.frame(86,42,148,22);ui.text(state.calc.slice(-21),226-state.calc.slice(-21).length*6,50);
  ['7','8','9','/','4','5','6','*','1','2','3','-','C','0','=','+'].forEach((v,i)=>hit('calc'+v,ui.button(v,86+(i%4)*38,72+Math.floor(i/4)*26,34,{pressed:state.pressed==='calc'+v}),()=>calculateKey(v)));
  footer('Calculator');
}
function calculateKey(key){
  if(key==='C'){state.calc='0';state.operator=null;state.operand=null;state.newNumber=true;return;}
  if(/^\d$/.test(key)){state.calc=state.newNumber?key:(state.calc==='0'?'':state.calc)+key;state.newNumber=false;return;}
  const value=Number(state.calc);
  if(state.operator&&!state.newNumber){
    const a=state.operand,op=state.operator;
    const result=op==='+'?a+value:op==='-'?a-value:op==='*'?a*value:value===0?NaN:Math.trunc(a/value);
    state.calc=Number.isFinite(result)&&result>=-32768&&result<=32767?String(result):'Error';
  }
  state.operand=Number(state.calc);state.operator=key==='='?null:key;state.newNumber=true;
}
function control(){
  background();const w=ui.window('Control panel',[32,24,256,156]);hit('close',w.close,()=>setScene('files'));
  ui.text('Desktop',47,51);ui.fill(47,62,225,1);
  [['Show hidden files',false],['Play music at startup',true],['24-hour clock',false]].forEach(([label,val],i)=>{
    const key='check'+i;if(!(key in state))state[key]=val;hit(key,ui.checkbox(label,48,76+i*21,state[key]),()=>{state[key]=!state[key];});
  });
  hit('done',ui.button('Done',200,150,70,{focused:true}),()=>setScene('files'));footer('Control panel');
}
function draw(){targets=[];bar=null;if(!state.open){background();footer('TEENSY menu - apps and settings');return;}
  menu();if(state.scene==='files')files();else if(state.scene==='calculator')calculator();else if(state.scene==='control')control();else dialog(state.scene);
}
function setScene(scene){state.scene=scene;state.open=true;state.choice=0;state.pressed=null;document.querySelectorAll('[data-scene]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.scene===scene)));draw();}
document.querySelectorAll('[data-scene]').forEach(b=>b.addEventListener('click',()=>setScene(b.dataset.scene)));
function point(e){const r=canvas.getBoundingClientRect();return[Math.floor((e.clientX-r.left)*320/r.width),Math.floor((e.clientY-r.top)*200/r.height)];}
canvas.addEventListener('pointerdown',e=>{const [x,y]=point(e);canvas.focus({preventScroll:true});canvas.setPointerCapture(e.pointerId);
  if(state.scene==='files'&&bar&&contains(bar.thumb,x,y)){state.drag={offset:y-bar.thumb[1]};return;}
  if(state.scene==='files'&&bar&&contains(bar.track,x,y)){scroll(y<bar.thumb[1]?-16:16);draw();return;}
  const t=[...targets].reverse().find(t=>contains(t.r,x,y));state.pressed=t?.id||null;draw();
});
canvas.addEventListener('pointermove',e=>{if(!state.drag||!bar)return;const [,y]=point(e),room=bar.track[3]-bar.thumb[3];setTop(4*Math.round(Math.max(0,Math.min(1,(y-state.drag.offset-bar.track[1])/room))*(names.length-16)/4));draw();});
canvas.addEventListener('pointerup',e=>{if(state.drag){state.drag=null;return;}const[x,y]=point(e);const t=[...targets].reverse().find(t=>t.id===state.pressed&&contains(t.r,x,y));state.pressed=null;t?.action();draw();});
canvas.addEventListener('pointercancel',()=>{state.drag=null;state.pressed=null;draw();});
canvas.addEventListener('wheel',e=>{if(state.scene==='files'){e.preventDefault();scroll(e.deltaY>0?4:-4);draw();}},{passive:false});
canvas.addEventListener('keydown',e=>{if(e.key==='Escape'){e.preventDefault();setScene('files');return;}if(['firmware','delete'].includes(state.scene)){if(e.key==='Tab'||e.key==='ArrowLeft'||e.key==='ArrowRight'){state.choice^=1;e.preventDefault();}else if(e.key==='Enter'){[...targets].reverse().find(t=>t.id===(state.choice?'accept':'cancel'))?.action();}draw();return;}
  if(state.scene==='calculator'&&/^[\d+*/=cC-]$/.test(e.key)){e.preventDefault();calculateKey(e.key.toUpperCase());draw();return;}
  if(state.scene==='files'&&e.key.startsWith('Arrow')){e.preventDefault();const delta={ArrowDown:4,ArrowUp:-4,ArrowLeft:-1,ArrowRight:1}[e.key];state.selected=Math.max(0,Math.min(names.length-1,state.selected+delta));if(state.selected<state.top)state.top=4*Math.floor(state.selected/4);if(state.selected>=state.top+16)state.top=4*Math.floor(state.selected/4)-12;draw();}
});
window.desktopPreview={state,draw,setScene};draw();

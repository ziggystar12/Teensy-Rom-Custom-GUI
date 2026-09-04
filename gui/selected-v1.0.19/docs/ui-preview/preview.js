import {assets} from './assets.js';
import {createWidgets,geometry,contains,filenameLines} from './widgets.js';
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d'),ui=createWidgets(ctx,assets);
ctx.imageSmoothingEnabled=false;
const names=['Games','Utilities','Documents','SAVES','SQ1-64-MPE.crt','KQ1-64-MPE.crt','BlackCauldron.crt','Text.txt','Readme.md','MPE_Firmware-V1.0.5.hex','DeathIsNoEvil.sid','Music','Disk01.d64','Arcada.sav','Demo.prg','Notes.txt','KingQuest2.crt','KingQuest3.crt','SpaceQuest2.crt','PoliceQuest1.crt','LeisureSuitLarry.crt','GoldRush.crt','Manhunter1.crt','Manhunter2.crt','MixedCase.txt','AnotherFile.seq','LongFilenameWithExtension.txt','Backups'];
const folders=new Set(['Games','Utilities','Documents','SAVES','Music','Backups']);
const controlItems=[
  ['Appearance','control','appearance'],['Input','teensy','input'],['Startup','utilities'],
  ['Storage','sd','storage'],['Clock','control'],['MIDI/NET','usb'],
  ['System','teensy'],['Advanced','utilities'],['Music','control']
];
const state={scene:'files',top:0,selected:7,drag:null,pressed:null,notice:'',open:true,calc:'128',choice:0,operand:null,operator:null,newNumber:true,
  controlPage:null,controlSelection:0,appearanceMode:'Light',desktopBackground:'Dots',ports:['Mouse','Joystick'],inputDropdown:null};
let targets=[],bar=null;
const hit=(id,r,action)=>targets.push({id,r,action});
function menu(){ui.fill(0,0,320,200,ui.paper);ui.fill(0,10,320,1);['TEENSY','File','Edit','View','Disk'].forEach((v,i)=>ui.text(v,[4,52,84,116,148][i],2));ui.text('2:45 PM',274,2);}
function background(){menu();for(let y=18;y<190;y+=8)for(let x=4+(y&8);x<316;x+=16)ui.fill(x,y,1,1);}
function footer(value){ui.fill(0,188,320,12,ui.paper);ui.text(value.slice(0,52),4,191);}
function files(){
  const win=ui.window('SD card',geometry.browserWindow);hit('close',win.close,()=>{state.open=false;});
  ui.glyph(['00100','01110','11111','00100','00100'],10,29);ui.text('/ ',24,29);ui.text('28 items',247,29);
  const g=geometry.grid;
  for(let i=0;i<20;i++){
    const index=state.top+i,name=names[index];if(!name)break;
    const x=g.x+(i%4)*g.width,y=g.rowY[Math.floor(i/4)];
    const id=folders.has(name)?'games':name.endsWith('.d64')?'drive8':name.endsWith('.crt')?'program':'document';
    ui.icon(id,x+24,y);
    if(index===state.selected)ui.fill(x+1,y+16,70,16);
    filenameLines(name).forEach((s,line)=>{const tx=x+Math.floor((72-(s.length*6-1))/2);ui.text(s,tx,y+17+line*8,index===state.selected?ui.paper:ui.ink);});
    const select=()=>{state.selected=index;};
    hit('file'+index,[x+24,y,24,16],select);hit('file'+index,[x+1,y+16,70,16],select);
  }
  bar=ui.scrollbar(geometry.scroll,state.top,names.length,20);
  hit('up',bar.up,()=>scroll(-4));hit('down',bar.down,()=>scroll(4));
}
function setTop(top){const slot=state.selected-state.top;state.top=Math.max(0,Math.min(names.length-20,top));state.selected=Math.min(names.length-1,state.top+Math.max(0,Math.min(19,slot)));}
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
function settingButton(id,label,rect,active,action){
  const [x,y,w,h]=rect;ui.frame(x,y,w,h,active?ui.ink:ui.paper);ui.centered(label,x,y+Math.floor((h-7)/2),w,active?ui.paper:ui.ink);hit(id,rect,action);
}
function closeSettings(){state.controlPage=null;state.inputDropdown=null;draw();}
function controlHome(){
  background();const w=ui.window('Control panel',geometry.control);hit('close',w.close,()=>setScene('files'));
  controlItems.forEach(([label,icon,page],i)=>{
    const x=[64,144,224][i%3],y=[40,84,128][Math.floor(i/3)],active=i===state.controlSelection;
    ui.icon(icon,x,y);if(active)ui.fill(x-24,y+19,72,9);
    ui.centered(label.toUpperCase(),x-24,y+20,72,active?ui.paper:ui.ink);
    const open=()=>{state.controlSelection=i;if(page)state.controlPage=page;};
    hit('control-icon-'+i,[x,y,24,16],open);hit('control-label-'+i,[x-24,y+19,72,9],open);
  });
  ui.text('ARROWS MOVE RETURN OPEN STOP CLOSE',58,171);
}
function settingsWindow(title){
  background();const w=ui.window(title,geometry.settings);hit('settings-close',w.close,closeSettings);return w;
}
function appearance(){
  settingsWindow('Appearance');ui.text('MODE',40,43);
  settingButton('light','LIGHT',[48,56,72,20],state.appearanceMode==='Light',()=>{state.appearanceMode='Light';});
  settingButton('dark','DARK',[136,56,72,20],state.appearanceMode==='Dark',()=>{state.appearanceMode='Dark';});
  ui.text('DESKTOP BACKGROUND',40,91);
  [['Dots',[32,106,64,20]],['Dithered',[104,106,112,20]],['Blank',[224,106,64,20]]].forEach(([label,rect])=>
    settingButton('background-'+label,label.toUpperCase(),rect,state.desktopBackground===label,()=>{state.desktopBackground=label;}));
  ui.text('ARROWS CHOOSE  RETURN APPLIES  ESC CLOSES',34,175);
}
function portDiagram(x,label){
  ui.text(label,x+23,31);ui.frame(x,42,88,48);
  [[12,10],[28,10],[44,10],[60,10],[76,10],[20,28],[36,28],[52,28],[68,28]].forEach(([dx,dy])=>ui.fill(x+dx,42+dy,3,3));
}
function choosePort(port,device){
  state.ports[port]=device;if(device==='Mouse')state.ports[1-port]='Joystick';state.inputDropdown=null;
}
function input(){
  settingsWindow('Input');portDiagram(48,'PORT 1');portDiagram(184,'PORT 2');
  [38,174].forEach((x,port)=>settingButton('port-'+port,state.ports[port].toUpperCase()+' v',[x,102,108,20],state.inputDropdown===port,()=>{state.inputDropdown=state.inputDropdown===port?null:port;}));
  if(state.inputDropdown!==null){
    const port=state.inputDropdown,x=port?174:38;
    settingButton('port-mouse','MOUSE',[x,123,108,16],state.ports[port]==='Mouse',()=>choosePort(port,'Mouse'));
    settingButton('port-joystick','JOYSTICK',[x,139,108,16],state.ports[port]==='Joystick',()=>choosePort(port,'Joystick'));
  }
  ui.text('ONE MOUSE MAXIMUM  ESC CLOSES',70,175);
}
function storageLine(icon,heading,status,y,details){
  ui.icon(icon,30,y);ui.text(heading,64,y);ui.text(status,184,y);
  details.forEach((line,i)=>ui.text(line,64,y+16+i*12));
}
function storage(){
  settingsWindow('Storage');
  storageLine('sd','SD CARD','READY',36,['TOTAL 32768 MB  FREE 24576','ID 1A2B3C4D']);
  storageLine('usb','USB STORAGE','NOT CONNECTED',84,[]);
  storageLine('teensy','INTERNAL FLASH','READY',132,['TOTAL 8192 KB  FREE 7936','ID BUILT-IN']);
  settingButton('refresh','REFRESH',[214,164,72,17],false,()=>{});ui.text('RETURN OR CLICK REFRESH',34,175);
}
function control(){
  if(state.controlPage==='appearance')appearance();else if(state.controlPage==='input')input();else if(state.controlPage==='storage')storage();else controlHome();
}
function draw(){targets=[];bar=null;if(!state.open){background();footer('TEENSY menu - apps and settings');return;}
  menu();if(state.scene==='files')files();else if(state.scene==='calculator')calculator();else if(state.scene==='control')control();else dialog(state.scene);
}
function setScene(scene){state.scene=scene;state.open=true;state.choice=0;state.pressed=null;state.controlPage=null;state.inputDropdown=null;document.querySelectorAll('[data-scene]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.scene===scene)));draw();}
document.querySelectorAll('[data-scene]').forEach(b=>b.addEventListener('click',()=>setScene(b.dataset.scene)));
function point(e){const r=canvas.getBoundingClientRect();return[Math.floor((e.clientX-r.left)*320/r.width),Math.floor((e.clientY-r.top)*200/r.height)];}
canvas.addEventListener('pointerdown',e=>{const [x,y]=point(e);canvas.focus({preventScroll:true});canvas.setPointerCapture(e.pointerId);
  if(state.scene==='files'&&bar&&contains(bar.thumb,x,y)){state.drag={offset:y-bar.thumb[1]};return;}
  if(state.scene==='files'&&bar&&contains(bar.track,x,y)){scroll(y<bar.thumb[1]?-20:20);draw();return;}
  const t=[...targets].reverse().find(t=>contains(t.r,x,y));state.pressed=t?.id||null;draw();
});
canvas.addEventListener('pointermove',e=>{if(!state.drag||!bar)return;const [,y]=point(e),room=bar.track[3]-bar.thumb[3];setTop(4*Math.round(Math.max(0,Math.min(1,(y-state.drag.offset-bar.track[1])/room))*(names.length-20)/4));draw();});
canvas.addEventListener('pointerup',e=>{if(state.drag){state.drag=null;return;}const[x,y]=point(e);const t=[...targets].reverse().find(t=>t.id===state.pressed&&contains(t.r,x,y));state.pressed=null;t?.action();draw();});
canvas.addEventListener('pointercancel',()=>{state.drag=null;state.pressed=null;draw();});
canvas.addEventListener('wheel',e=>{if(state.scene==='files'){e.preventDefault();scroll(e.deltaY>0?4:-4);draw();}},{passive:false});
canvas.addEventListener('keydown',e=>{if(e.key==='Escape'){e.preventDefault();if(state.scene==='control'&&state.controlPage){state.controlPage=null;state.inputDropdown=null;draw();}else setScene('files');return;}if(['firmware','delete'].includes(state.scene)){if(e.key==='Tab'||e.key==='ArrowLeft'||e.key==='ArrowRight'){state.choice^=1;e.preventDefault();}else if(e.key==='Enter'){[...targets].reverse().find(t=>t.id===(state.choice?'accept':'cancel'))?.action();}draw();return;}
  if(state.scene==='calculator'&&/^[\d+*/=cC-]$/.test(e.key)){e.preventDefault();calculateKey(e.key.toUpperCase());draw();return;}
  if(state.scene==='control'&&!state.controlPage&&e.key.startsWith('Arrow')){e.preventDefault();const row=Math.floor(state.controlSelection/3),col=state.controlSelection%3;state.controlSelection=e.key==='ArrowUp'?((row+2)%3)*3+col:e.key==='ArrowDown'?((row+1)%3)*3+col:e.key==='ArrowLeft'?row*3+(col+2)%3:row*3+(col+1)%3;draw();return;}
  if(state.scene==='control'&&!state.controlPage&&e.key==='Enter'){e.preventDefault();const page=controlItems[state.controlSelection][2];if(page)state.controlPage=page;draw();return;}
  if(state.scene==='files'&&e.key.startsWith('Arrow')){e.preventDefault();const delta={ArrowDown:4,ArrowUp:-4,ArrowLeft:-1,ArrowRight:1}[e.key];state.selected=Math.max(0,Math.min(names.length-1,state.selected+delta));if(state.selected<state.top)state.top=4*Math.floor(state.selected/4);if(state.selected>=state.top+20)state.top=4*Math.floor(state.selected/4)-16;state.top=Math.max(0,Math.min(names.length-20,state.top));draw();}
});
window.desktopPreview={state,draw,setScene};draw();

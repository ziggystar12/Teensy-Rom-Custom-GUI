const fs=require('node:fs'),zlib=require('node:zlib');
const {desktopMachine}=require('../Source/C64/MainMenuCRT/tests/desktop-machine.js');
const colors=['000000','ffffff','68372b','70a4b2','6f3d86','588d43','352879','b8c76f','6f4f25','433900','9a6759','444444','6c6c6c','9ad284','6c5eb5','959595'].map(s=>Buffer.from(s,'hex'));
const crc=b=>{let c=0xffffffff;for(const v of b){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;};
function png(cpu,s,out){
 const rows=Buffer.alloc(200*(1+320*3));for(let y=0;y<200;y++)for(let x=0;x<320;x++){
  const cell=(y>>3)*40+(x>>3),bit=(cpu.m[0x2000+cell*8+(y&7)]>>(7-(x&7)))&1;
  colors[bit?cpu.m[0x400+cell]>>4:cpu.m[0x400+cell]&15].copy(rows,y*961+1+x*3);
 }
 const chunk=(type,b)=>{const head=Buffer.from(type),r=Buffer.alloc(12+b.length);r.writeUInt32BE(b.length);head.copy(r,4);b.copy(r,8);r.writeUInt32BE(crc(Buffer.concat([head,b])),8+b.length);return r;};
 const ihdr=Buffer.alloc(13);ihdr.writeUInt32BE(320);ihdr.writeUInt32BE(200,4);ihdr[8]=8;ihdr[9]=2;
 fs.mkdirSync('build/ui-proof',{recursive:true});fs.writeFileSync(out,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',ihdr),chunk('IDAT',zlib.deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]));
}
desktopMachine({diagnostic:console.log},async({s,fresh})=>{
 for(const surface of [0,1]){
  const cpu=fresh();cpu.m[1]=0x37;cpu.m[s.GeosSurfaceMode]=surface;cpu.m[s.GeosViewMode]=1;
  cpu.m[s.GeosBitmapActive]=1;cpu.m[s.rRegNumItemsOnPage+s.IO1Port]=16;cpu.m[s.rRegViewCountLo+s.IO1Port]=28;
  cpu.m[s.rwRegCursorItemOnPg+s.IO1Port]=7;
  Buffer.from('SD card\0').copy(cpu.m,s.GeosBrowserTitle);Buffer.from('/Games\0').copy(cpu.m,s.GeosBrowserPath);
  const names=['Games','Utilities','Documents','SAVES','SQ1-64-MPE.crt','KQ1-64-MPE.crt','BlackCauldron.crt','Text.txt','Readme.md','MPE_Firmwar...1.0.5.hex','DeathIsNoEvil.sid','Music','Disk01.d64','Arcada.sav','Demo.prg','Notes.txt'];
  for(let i=0;i<names.length;i++){Buffer.from(names[i]+'\0').copy(cpu.m,s.GeosRichFileLabels+i*23);cpu.m[s.GeosBrowserIcons+i]=i<4||i===11?s.GeosIconFolder:i===12?s.GeosIconDisk:i===4||i===5||i===6||i===14?s.GeosIconProgram:s.GeosIconDocument;}
  cpu.call(s.GeosBrowserReadState);cpu.call(s.GeosBitmapConvertScreen);
  png(cpu,s,`build/ui-proof/native-${surface?'browser':'home'}.png`);
 }
},{apps:false}).catch(e=>{console.error(e);process.exitCode=1;});

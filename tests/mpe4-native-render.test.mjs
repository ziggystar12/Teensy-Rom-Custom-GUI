import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),"..");
assert.ok(process.env.AGI64_SOURCE_ROOT,"AGI64_SOURCE_ROOT must name the AGI-64 checkout containing the host reference decoders");
assert.ok(process.env.SQ1_SOURCE_DIR,"SQ1_SOURCE_DIR must name your original SQ1 AGI game directory");
const agiRoot=path.resolve(process.env.AGI64_SOURCE_ROOT),gameSource=path.resolve(process.env.SQ1_SOURCE_DIR);
const reference=name=>import(pathToFileURL(path.join(agiRoot,'host',name)));
const [{loadAgiV2Game},{decodeAgiPicture},{addViewCelToPicture,decodeAgiView},{createMpe3TitleRenderer},{buildMpe3TitleSequence},{convertAgiToC64Multicolor}]=
  await Promise.all(['agi-v2.mjs','agi-picture.mjs','agi-view.mjs','mpe3-title-renderer.mjs','mpe3-title-sequence.mjs','c64-bitmap.mjs'].map(reference));
const output=path.resolve(process.env.MPE4_RENDER_TEST_OUTPUT??path.join(root,'build/native-render-test'));
const hash=(bytes)=>createHash("sha256").update(bytes).digest("hex");
function planes(picture) {
  const result=Buffer.alloc(26880);
  for(const [offset,plane] of [[0,picture.visual],[13440,picture.priority]])
    for(let i=0;i<26880;i+=2)result[offset+i/2]=(plane[i]<<4)|plane[i+1];
  return result;
}
test("MPE4 native renderer preserves every SQ1 picture, overlay, VIEW cel and hires frame with compact gameplay text",()=>{
  fs.mkdirSync(output,{recursive:true});
  const config=JSON.parse(fs.readFileSync(path.join(agiRoot,"config/sq1-64.json"),"utf8"));
  const game=loadAgiV2Game(gameSource,config);
  const resources=[];
  for(const entry of game.entries.filter(e=>!e.ignored&&["picture","view"].includes(e.type))) {
    const header=Buffer.alloc(6);header[0]=entry.type==="picture"?1:2;header[1]=entry.id;header.writeUInt32LE(entry.data.length,2);
    resources.push(header,entry.data);
  }
  fs.writeFileSync(path.join(output,"resources.bin"),Buffer.concat(resources));
  const render=createMpe3TitleRenderer(game),font=Buffer.alloc(1024);
  for(let ascii=32;ascii<=126;ascii++)font.set(render({mode:"text",textRows:[{row:0,column:0,text:String.fromCharCode(ascii)}]}).bitmap.subarray(0,8),ascii*8);
  fs.writeFileSync(path.join(output,"font.bin"),font);
  const pictures=game.entries.filter(e=>!e.ignored&&e.type==="picture"),pictureTests=[];
  for(const [index,entry] of pictures.entries()) {
    pictureTests.push(Buffer.from([0,entry.id,0]),planes(decodeAgiPicture(entry.data)));
    const base=pictures[(index+pictures.length-1)%pictures.length];
    pictureTests.push(Buffer.from([base.id,entry.id,1]),planes(decodeAgiPicture(entry.data,160,168,decodeAgiPicture(base.data))));
  }
  fs.writeFileSync(path.join(output,"pictures.bin"),Buffer.concat(pictureTests));
  const celTests=[];
  for(const entry of game.entries.filter(e=>!e.ignored&&e.type==="view")) {
    const view=decodeAgiView(entry.data);
    for(const [loop,l] of view.loops.entries())for(const [cel,c] of l.cels.entries()) {
      const picture={width:160,height:168,visual:new Uint8Array(26880).fill(15),priority:new Uint8Array(26880).fill(4)};
      addViewCelToPicture(picture,view,{loop,cel,x:0,y:167,priority:15});
      celTests.push(Buffer.from([entry.id,loop,cel,0,167,15,c.width,c.height,view.loops.length,l.cels.length]),planes(picture));
    }
  }
  fs.writeFileSync(path.join(output,"cels.bin"),Buffer.concat(celTests));
  const visits=buildMpe3TitleSequence(game).visits,frameTests=[];
  for(const visit of visits) {
    const objects=visit.objects??[],text=Buffer.alloc(1000),attributes=Buffer.alloc(1000,15);
    for(const row of visit.clearRows??[])text.fill(32,row*40,row*40+40);
    for(const row of visit.textRows??[])for(let i=0;i<row.text.length;i++) {
      const cell=row.row*40+(row.column??0)+i;text[cell]=row.text.charCodeAt(i);
      attributes[cell]=((row.background??0)<<4)|(row.color??15);
    }
    frameTests.push(Buffer.from([visit.mode!=="text"?1:0,visit.pictureId!=null?1:0,visit.pictureId??0,visit.graphicsTop??8,objects.length]));
    for(const object of objects)frameTests.push(Buffer.from([object.object,object.view,object.loop,object.cel,object.x,object.y,object.priority,1|2|32]));
    const expected=render(visit).frame;
    if(visit.mode!=="text") {
      const canvas={width:160,height:200,visual:new Uint8Array(32000)};
      for(let cell=0;cell<1000;cell++)if(text[cell]) {
        const ascii=text[cell]>=97&&text[cell]<=122?text[cell]-32:text[cell];
        for(let y=0;y<8;y++)for(let x=0;x<4;x++)canvas.visual[((cell/40|0)*8+y)*160+(cell%40)*4+x]=
          x<3&&(font[ascii*8+y]&[0x60,0x18,0x06][x])?attributes[cell]&15:attributes[cell]>>4;
      }
      const compact=convertAgiToC64Multicolor(canvas,{top:0});
      for(let cell=0;cell<1000;cell++)if(text[cell]) {
        compact.bitmap.copy(expected,cell*8,cell*8,cell*8+8);
        expected[8000+cell]=compact.screen[cell];expected[9000+cell]=compact.color[cell];
      }
    }
    frameTests.push(text,attributes,expected);
  }
  fs.writeFileSync(path.join(output,"frames.bin"),Buffer.concat(frameTests));
  // Actual Roger cels over Room 2 exercise palette crossings independently of
  // game logic. Keep both the full-color target and the existing conversion.
  const motionTests=[],pic2=game.entries.find(e=>e.type==="picture"&&e.id===2&&!e.ignored),
    roger=decodeAgiView(game.entries.find(e=>e.type==="view"&&e.id===0&&!e.ignored).data);
  for(const [startX,y,count] of [[90,66,36],[66,100,36],[32,118,36]])for(let step=0;step<count;step++) {
    const x=startX+step,loop=0,cel=step%roger.loops[loop].cels.length,priority=Math.max(4,Math.min(15,Math.floor(y/12)+1));
    const base=decodeAgiPicture(pic2.data),composite=decodeAgiPicture(pic2.data);
    addViewCelToPicture(composite,roger,{loop,cel,x,y,priority,priorityMode:"agi"});
    const converted=convertAgiToC64Multicolor(composite,{top:8}),mask=Buffer.alloc(32000);
    for(let at=0;at<26880;at++)if(base.visual[at]!==composite.visual[at])mask[at+8*160]=1;
    motionTests.push(Buffer.from([2,0,loop,cel,x,y,priority]),Buffer.concat([converted.bitmap,converted.screen,converted.color]),
      Buffer.from(converted.pixels),mask);
  }
  fs.writeFileSync(path.join(output,"motion.bin"),Buffer.concat(motionTests));
  const compiler=process.env.CXX??(process.platform==='win32'?"C:/msys64/mingw64/bin/g++.exe":'g++');
  const executable=path.join(output,process.platform==='win32'?"mpe4-render-harness.exe":'mpe4-render-harness');
  const source=path.join(root,"engine/native-game/mpe4_render.cpp");
  const compileArgs=["-std=c++17","-O2","-Wall","-Wextra",...(process.platform==='win32'?["-static","-static-libgcc","-static-libstdc++"]:[]),
    "-I",path.dirname(source),path.join(root,"tests/mpe4-render-harness.cpp"),source,"-o",executable];
  const warnings=execFileSync(compiler,compileArgs,{cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,encoding:"utf8",windowsHide:true,timeout:60000});
  fs.writeFileSync(path.join(output,"compiler.log"),warnings);
  const actual=execFileSync(executable,[output],{encoding:"utf8",windowsHide:true,timeout:60000});
  const result={...JSON.parse(actual),rendererSha256:hash(fs.readFileSync(source)),fontSha256:hash(font),
    proof:"Real native renderer compared byte-for-byte with host picture/VIEW/hires rendering and established C64 compact gameplay glyph conversion"};
  fs.writeFileSync(path.join(output,"result.json"),JSON.stringify(result,null,2)+"\n");
  assert.equal(result.pictures,73);assert.equal(result.overlays,73);assert.equal(result.cels,1652);assert.equal(result.frames,132);
  assert.ok(result.maximumFillSeeds<=5000);assert.ok(result.rendererBytes<=768);assert.equal(result.guardsIntact,true);
  console.log(JSON.stringify(result));
});

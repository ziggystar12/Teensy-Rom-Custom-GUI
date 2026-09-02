import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const options={};for(let i=2;i<process.argv.length;i+=2){if(!['--package','--output','--compiler','--agi-root'].includes(process.argv[i])||!process.argv[i+1])throw Error('Expected --package/--output/--compiler/--agi-root value');options[process.argv[i].slice(2)]=process.argv[i+1];}
if(!options.package||!options.output||!options['agi-root'])throw Error('--package, --output and --agi-root required');
const agiRoot=path.resolve(options['agi-root']);
const [{decodeMpe3TitleFrame},{C64_PALETTE}]=await Promise.all(['mpe3-title-renderer.mjs','c64-bitmap.mjs'].map(name=>import(pathToFileURL(path.join(agiRoot,'host',name)))));
const output=path.resolve(options.output),compiler=options.compiler??process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++');fs.mkdirSync(output,{recursive:true});
const executable=path.join(output,process.platform==='win32'?'mpe4-game-preview.exe':'mpe4-game-preview');
execFileSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation',...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),
  '-I',path.join(root,'engine/native-game'),path.join(root,'tests/mpe4-game-preview.cpp'),
  path.join(root,'engine/native-game/mpe4_game.cpp'),path.join(root,'engine/native-game/mpe4_render.cpp'),'-o',executable],
  {cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,windowsHide:true,stdio:'pipe',timeout:60000});
const result=JSON.parse(execFileSync(executable,[path.resolve(options.package),output],{encoding:'utf8',windowsHide:true,timeout:60000}));
function bmp(image,width=960,height=720){
  const stride=(width*3+3)&~3,bytes=Buffer.alloc(54+stride*height);bytes.write('BM');bytes.writeUInt32LE(bytes.length,2);bytes.writeUInt32LE(54,10);
  bytes.writeUInt32LE(40,14);bytes.writeInt32LE(width,18);bytes.writeInt32LE(height,22);bytes.writeUInt16LE(1,26);bytes.writeUInt16LE(24,28);bytes.writeUInt32LE(stride*height,34);
  for(let y=0;y<height;y++)for(let x=0;x<width;x++){
    const color=C64_PALETTE[image.pixels[Math.floor(y*image.height/height)*image.width+Math.floor(x*image.width/width)]];
    const at=54+(height-1-y)*stride+x*3;bytes[at]=color[2];bytes[at+1]=color[1];bytes[at+2]=color[0];
  }return bytes;
}
const sha=b=>createHash('sha256').update(b).digest('hex');result.frames={};
for(const name of ['login','room2','look','parser','menu']){
  const frame=fs.readFileSync(path.join(output,name+'.frame'));result.frames[name]=sha(frame);
  fs.writeFileSync(path.join(output,name+'-4x3.bmp'),bmp(decodeMpe3TitleFrame(frame,name==='login'?'text':'graphics')));
}
result.rendererSha256=sha(fs.readFileSync(path.join(root,'engine/native-game/mpe4_render.cpp')));
result.packageSha256=sha(fs.readFileSync(options.package));result.aspectRatio='4:3';
fs.writeFileSync(path.join(output,'preview.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));

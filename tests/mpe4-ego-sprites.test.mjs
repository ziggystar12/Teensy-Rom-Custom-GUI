import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import test from 'node:test';

const root=path.resolve(import.meta.dirname,'..');
assert.ok(process.env.AGI64_SOURCE_ROOT,'AGI64_SOURCE_ROOT must name the AGI-64 compiler source');
const agi=path.resolve(process.env.AGI64_SOURCE_ROOT);
const source=path.resolve(process.env.AGI64_GAMES_ROOT??path.join(agi,'Sierra Games'));
const output=path.resolve(process.env.MPE4_SPRITE_TEST_OUTPUT??path.join(root,'build/ego-sprite-tests'));
const ref=name=>import(pathToFileURL(path.join(agi,'host',name)));
const [{loadAgiV2Game},{decodeAgiView},{buildMpe4GamePackage},{crc32}]=await Promise.all(
  ['agi-v2.mjs','agi-view.mjs','mpe4-game-pack.mjs','mpe-native-agi-codec.mjs'].map(ref));
fs.mkdirSync(output,{recursive:true});
const compiler=process.env.CXX??(process.platform==='win32'?'C:/msys64/mingw64/bin/g++.exe':'g++');
const exe=path.join(output,process.platform==='win32'?'ego-sprites.exe':'ego-sprites');
const native=path.join(root,'engine/native-game');
execFileSync(compiler,['-std=c++17','-O2','-Wall','-Wextra','-Wno-misleading-indentation',
  ...(process.platform==='win32'?['-static','-static-libgcc','-static-libstdc++']:[]),'-I',native,
  path.join(root,'tests/mpe4-ego-sprites.cpp'),...['mpe4_game.cpp','mpe4_package.cpp','mpe4_render.cpp','mpe4_session.cpp'].map(f=>path.join(native,f)),
  '-o',exe],{cwd:path.isAbsolute(compiler)?path.dirname(compiler):root,windowsHide:true,timeout:60000,stdio:'pipe'});
for(const [id,folder,profile] of [['sq1','SQ1',0],['kq1','KQ1',1],['kq2','KQ2',2]])test(`${id} uses original layered ego sprites and exact source visibility`,()=>{
  const config=JSON.parse(fs.readFileSync(path.join(agi,'config',`${id}-64.json`),'utf8'));
  const game=loadAgiV2Game(path.join(source,folder),config);
  const pack=Buffer.from(buildMpe4GamePackage(game).data);
  pack.writeUInt32LE(2|(profile<<8),32);pack.writeUInt32LE(0,28);pack.writeUInt32LE(crc32(pack.subarray(0,64)),28);
  const view=decodeAgiView(game.entries.find(e=>e.type==='view'&&e.id===0&&!e.ignored).data);
  const cels=[];
  for(const [loop,l] of view.loops.entries())for(const [cel,c] of l.cels.entries())
    cels.push(Buffer.from([loop,cel,c.width,c.height,c.transparentColor]),Buffer.from(c.pixels));
  const packagePath=path.join(output,`${id}.bin`),celPath=path.join(output,`${id}.cels`);
  fs.writeFileSync(packagePath,pack);fs.writeFileSync(celPath,Buffer.concat(cels));
  const result=JSON.parse(execFileSync(exe,[packagePath,celPath],{windowsHide:true,encoding:'utf8',timeout:60000}));
  assert.equal(result.stateBytes,9624);assert.ok(result.sessionBytes<=65536);
  assert.ok(result.accentFrames>0);if(profile)assert.ok(result.grayEyePixelsProtected>0);
  fs.writeFileSync(path.join(output,`${id}-result.json`),`${JSON.stringify(result,null,2)}\n`);console.log(id,result);
});

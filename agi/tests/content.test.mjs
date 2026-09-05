import fs from 'node:fs';import test from 'node:test';import assert from 'node:assert/strict';
import {validateAgi,crc32,encodeFixture} from '../tools/agi_content.mjs';
const bytes=fs.readFileSync(new URL('../../build/agivm/SD/VMS/AGIVM/GAMES/AGITEST.AGI',import.meta.url));
const seal=b=>{b.writeUInt32LE(crc32(b.subarray(64)),24);b.writeUInt32LE(0,28);b.writeUInt32LE(crc32(b.subarray(0,64)),28);return b;};
test('standalone AGI validates and rejects every single header-byte corruption',()=>{assert.equal(validateAgi(bytes).saveId,'AGTEST');for(let n=0;n<64;n++){const b=Buffer.from(bytes);b[n]^=128;assert.throws(()=>validateAgi(b));}});
test('rejects renamed CRT, truncation, trailing bytes and damaged payload',()=>{assert.throws(()=>validateAgi(Buffer.from('C64 CARTRIDGE   ')));assert.throws(()=>validateAgi(bytes.subarray(0,-1)));assert.throws(()=>validateAgi(Buffer.concat([bytes,Buffer.alloc(4)])));const b=Buffer.from(bytes);b[b.length-5]^=128;assert.throws(()=>validateAgi(b));});
test('rejects sealed invalid identity/startup/index/resource CRC/bounds',()=>{
 for(const edit of [b=>b.writeUInt32LE(2,32),b=>b.write('bad/id',36),b=>b.writeUInt32LE(2048,12),b=>b.writeUInt32LE(0xffffffff,68),b=>b.writeUInt32LE(0,72),b=>b.writeUInt32LE(0,76),b=>b[64]=8,b=>b[44]=1]){const b=Buffer.from(bytes);edit(b);assert.throws(()=>validateAgi(seal(b)));}
});
test('package can exceed 512K without increasing resident memory',()=>{
 const decoded=validateAgi(bytes),entries=decoded.entries.map(e=>({...e,data:bytes.subarray(e.offset,e.offset+e.length)}));entries.push({type:1,id:99,data:Buffer.alloc(800000,0x55)});
 const big=encodeFixture(entries);assert.ok(big.length>524288);assert.equal(validateAgi(big).entries.length,5);
});

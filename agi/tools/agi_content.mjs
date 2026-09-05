// SPDX-License-Identifier: MIT
// Standalone, checksummed M4G2 resources. No CRT container or C64 code.
import assert from 'node:assert/strict';
export const crc32=b=>{let c=0xffffffff;for(const v of b){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;};
export function validateAgi(b){
 assert.ok(b.length>=64&&b.length<=32*1024*1024,'AGI size');
 assert.equal(b.toString('ascii',0,4),'M4G2');assert.equal(b.readUInt16LE(4),2);assert.equal(b.readUInt16LE(6),64);
 assert.equal(b.readUInt32LE(8),b.length);const header=Buffer.from(b.subarray(0,64));header.writeUInt32LE(0,28);
 assert.equal(b.readUInt32LE(28),crc32(header),'Header CRC');assert.equal(b.readUInt32LE(24),crc32(b.subarray(64)),'Payload CRC');
 const flags=b.readUInt32LE(32);assert.ok((flags&1)&&!(flags&~0x303),'Standalone AGI requires original startup');
 assert.ok((flags>>>8)<=2&&((flags&2)||!(flags>>>8)));assert.match(b.toString('ascii',36,42),/^[A-Z0-9]{6}$/);assert.ok(b.readUInt16LE(42));
 assert.ok(b.subarray(44,64).every(v=>v===0));const count=b.readUInt16LE(16);assert.ok(count>0&&count<=2048);
 assert.equal(b.readUInt32LE(12),64);assert.equal(b.readUInt16LE(18),16);assert.equal(b.readUInt32LE(20),64+count*16);
 let end=64+count*16,key=-1;const entries=[];
 for(let n=0;n<count;n++){
  const at=64+n*16;assert.ok(at+16<=b.length);const type=b[at],id=b[at+1],offset=b.readUInt32LE(at+4),length=b.readUInt32LE(at+8);
  assert.ok(type<=7&&(!(type>=4&&type<=6)||id===0)&&type*256+id>key);assert.equal(b.readUInt16LE(at+2),0);
  assert.equal(offset,end);assert.ok(length>0&&offset+length<=b.length);assert.equal(crc32(b.subarray(offset,offset+length)),b.readUInt32LE(at+12));
  end=(offset+length+3)&~3;assert.ok(end<=b.length&&b.subarray(offset+length,end).every(v=>v===0));key=type*256+id;entries.push({type,id,offset,length});
 }assert.equal(end,b.length);for(const type of [0,4,5,6])assert.ok(entries.some(e=>e.type===type&&e.id===0));
 assert.equal(entries.find(e=>e.type===6).length,1024);
 return {format:'M4G2',standalone:true,bytes:b.length,saveId:b.toString('ascii',36,42),saveEpoch:b.readUInt16LE(42),flags,entries};
}
// Used only for our authored diagnostic fixture, not to normalize original AGI.
export function encodeFixture(entries){
 entries=entries.toSorted((a,b)=>a.type*256+a.id-b.type*256-b.id);let end=64+entries.length*16;
 entries=entries.map(e=>{const v={...e,offset:end};end=(end+e.data.length+3)&~3;return v;});const b=Buffer.alloc(end);
 b.write('M4G2');b.writeUInt16LE(2,4);b.writeUInt16LE(64,6);b.writeUInt32LE(end,8);b.writeUInt32LE(64,12);
 b.writeUInt16LE(entries.length,16);b.writeUInt16LE(16,18);b.writeUInt32LE(64+entries.length*16,20);b.writeUInt32LE(3,32);b.write('AGTEST',36);b.writeUInt16LE(1,42);
 entries.forEach((e,n)=>{const at=64+n*16;b[at]=e.type;b[at+1]=e.id;b.writeUInt32LE(e.offset,at+4);b.writeUInt32LE(e.data.length,at+8);b.writeUInt32LE(crc32(e.data),at+12);e.data.copy(b,e.offset);});
 b.writeUInt32LE(crc32(b.subarray(64)),24);b.writeUInt32LE(crc32(b.subarray(0,64)),28);validateAgi(b);return b;
}

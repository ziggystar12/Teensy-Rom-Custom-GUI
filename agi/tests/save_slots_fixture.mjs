// Authored save/restore commands for the actual module and C64 packet replay.
import {encodeFixture,validateAgi} from '../tools/agi_content.mjs';
export function saveSlotsFixture(baseline){
 const resources=validateAgi(baseline).entries.filter(e=>e.type>=4&&e.type<=6)
  .map(e=>({type:e.type,id:e.id,data:baseline.subarray(e.offset,e.offset+e.length)}));
 const when=(value,op)=>[255,1,210,value,255,5,0,3,210,0,op,0];
 const code=Buffer.from([3,10,0,...when(1,125),...when(2,126),0]);
 const logic=Buffer.alloc(2+code.length+3);logic.writeUInt16LE(code.length);code.copy(logic,2);
 return encodeFixture([...resources,{type:0,id:0,data:logic}]);
}

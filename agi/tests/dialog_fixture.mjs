// Authored regression content, generated only in the test sandbox. No game assets.
import {encodeFixture,validateAgi} from '../tools/agi_content.mjs';
export function dialogFixture(baseline){
 const resources=validateAgi(baseline).entries.filter(e=>e.type>=4&&e.type<=6)
  .map(e=>({type:e.type,id:e.id,data:baseline.subarray(e.offset,e.offset+e.length)}));
 const when=(variable,value,body)=>[255,1,variable,value,255,body.length&255,body.length>>8,...body];
 const code=Buffer.from([
  ...when(200,0,[3,200,1,3,10,0,3,201,0,24,201,25,201,26,120]),
  ...when(210,1,[3,210,0,101,1]), // centered print
  ...when(210,2,[3,210,0,151,1,22,4,30]), // print.at overlaps parser/separator
  ...when(210,3,[3,210,0,3,21,1,101,1]), // timed print
  ...when(210,4,[3,210,0,119,101,1,120]), // authored input disable/enable
  ...when(210,5,[3,210,0,106,101,1]), // genuine full-screen text mode
  0]);
 const message=Buffer.from('THE ALARM SOUNDS.\0'),logic=Buffer.alloc(2+code.length+5+message.length);
 logic.writeUInt16LE(code.length);code.copy(logic,2);const at=2+code.length;
 logic[at]=1;logic.writeUInt16LE(4+message.length,at+1);logic.writeUInt16LE(4,at+3);message.copy(logic,at+5);
 return encodeFixture([...resources,{type:0,id:0,data:logic},
  {type:1,id:0,data:Buffer.from([0xf0,1,0xf8,0,0,0xff])}]);
}

// Rebuild/relink just GBVM from the corresponding sources in its download.
// Node.js + GNU Arm Embedded 11.3.1; no firmware build, ROM or Arduino libraries.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.argv[2]??path.join(root,'build/gb-core'));
const arm=process.env.MPE_ARM_PREFIX??path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const suffix=process.platform==='win32'?'.exe':'';
const run=(name,args)=>{const r=spawnSync(arm+name+suffix,args,{encoding:'utf8',windowsHide:true,maxBuffer:8*1024*1024});if(r.error||r.status)throw Error((r.error??'')+'\n'+r.stdout+r.stderr);return r.stdout;};
fs.mkdirSync(out,{recursive:true});const elf=path.join(out,'gbvm.elf');
run('g++',['-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard','-std=c++17','-O2',
  '-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage','-nostartfiles',
  '-T',path.join(root,'vm/abi/module.ld'),'-Wl,--gc-sections','-Wl,-Map='+path.join(out,'gbvm.map'),path.join(root,'vm/gb/gbvm.cpp'),path.join(root,'engine/gnuboy/machine.cpp'),'-Wl,--start-group','-lc','-lm','-lgcc','-Wl,--end-group','-o',elf]);
if(run('nm',['-u',elf]).trim())throw Error('Unresolved imports');
const nm=run('nm',['-n',elf]);if(nm.includes('_GLOBAL__sub_I'))throw Error('Unsupported dynamic initialization');
for(const section of ['text','data'])run('objcopy',['-O','binary','--only-section=.'+section,elf,path.join(out,section+'.bin')]);
const code=fs.readFileSync(path.join(out,'text.bin')),data=fs.readFileSync(path.join(out,'data.bin'));
const size=run('size',['-A',elf]);const bss=Number(size.match(/^\.bss\s+(\d+)/m)?.[1]??0);
if(code.length>98304||data.length+bss>180224)throw Error('Module exceeds reserved RAM1 limits');
const crc=b=>{let c=0xffffffff;for(const v of b){c^=v;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;};
const entry=parseInt(nm.match(/^([0-9a-f]+) T vm_entry$/m)?.[1]??'',16)|1;
const header=Buffer.alloc(64);
[0x314d564d,2,64,code.length,data.length,bss,entry,0x18000,0x20014000,127,crc(Buffer.concat([code,data])),0].forEach((v,i)=>header.writeUInt32LE(v>>>0,i*4));header.writeUInt32LE(crc(header),44);
fs.writeFileSync(path.join(out,'engine.mvm'),Buffer.concat([header,code,data]));
console.log('Rebuilt independent GBVM: '+path.join(out,'engine.mvm'));

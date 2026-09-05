// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental standalone DoomVM extraction and honest RAM fit gate.
// Produces ignored evidence only. Never emits engine.mvm or touches releases.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const root=fs.realpathSync(path.resolve(import.meta.dirname,'..'));
const wadArg=process.argv.indexOf('--wad');
if(wadArg<0||!process.argv[wadArg+1])throw Error('Usage: node scripts/audit-doomvm.mjs --wad <user-supplied DOOM1.WAD> [--require-fit]');
const wad=fs.realpathSync(process.argv[wadArg+1]);
const out=path.join(root,'build/doom/modular-audit'),source=path.join(root,'build/doom/adapted/modular-audit');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const write=(p,data)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,data);};
function invoke(exe,args,options={}){
    const env={...process.env},key=Object.keys(env).find(k=>k.toUpperCase()==='PATH')??'PATH';
    env[key]=path.dirname(exe)+path.delimiter+(env[key]??'');
    const result=spawnSync(exe,args,{cwd:root,env,windowsHide:true,encoding:'utf8',maxBuffer:8*1024*1024,timeout:120000,...options});
    return {...result,log:(result.stdout??'')+(result.stderr??'')};
}
function run(exe,args,options={}){const r=invoke(exe,args,options);if(r.error||r.status!==0)throw Error(`${exe}: ${r.error??r.status}\n${r.log.slice(-5000)}`);return r.log;}
assert.equal(run('git',['ls-files','--',wad]).trim(),'','WAD must remain untracked');
const origin=JSON.parse(fs.readFileSync(path.join(root,'doom/third_party/mcume-teensydoom.origin.json')));
const inputs=['scripts/audit-doomvm.mjs','vm/abi/module.ld','Source/Teensy/MinimalBoot/Common/VMABI.h',
    'doom/patches/mcume-teensydoom-native-adapter.patch','doom/third_party/mcume-teensydoom.origin.json',
    'doom/tools/apply_mcume_native_adapter.ps1','doom/tools/fetch_mcume_teensydoom.ps1',
    'doom/host/Arduino.h','engine/native-doom/mpe_doom_runtime.cpp','engine/native-doom/mpe_doom_runtime.h',
    'vm/tests/doom_module_probe.cpp','vm/tests/doom_heap_test.cpp','vm/tests/doom_platform_test.cpp',
    ...fs.readdirSync(path.join(root,'vm/doom'),{recursive:true}).filter(p=>fs.statSync(path.join(root,'vm/doom',p)).isFile()).map(p=>'vm/doom/'+p)];
const snapshot=inputs.map(p=>({path:p,sha256:sha(path.join(root,p))}));
// Existing staging tool verifies the exact pinned checkout and original patch.
const overlay=JSON.parse(run('pwsh',['-NoProfile','-File','doom/tools/apply_mcume_native_adapter.ps1','-OutputRoot',source]));
assert.equal(overlay.status,'PASS');assert.equal(overlay.sourceCommit,origin.commit);
function change(file,old,replacement){const p=path.join(source,file),text=fs.readFileSync(p,'utf8').replaceAll('\r\n','\n');
    assert.equal(text.split(old).length,2,`Expected one adaptation point: ${file}`);write(p,text.replace(old,replacement));}
change('i_system.c','    zonemem = AutoAllocMemory(size, default_ram, min_ram);','    zonemem = DoomZone(size);');
change('i_system.c','    MHS_DoomFatal(error);',
    '    char message[160];\n    va_list args;\n    va_start(args,error);\n    vsnprintf(message,sizeof message,error,args);\n    va_end(args);\n    MHS_DoomFatal(message);');
// FatFs-shaped byte counts are unsigned int, not unsigned long pointers.
change('m_menu.c','    unsigned long count;','    unsigned int count;');
change('m_misc.c','\tunsigned long c;','\tunsigned int c;');
change('m_misc.c','\tunsigned long read;','\tunsigned int read;');
change('p_saveg.c','    unsigned long count;','    unsigned int count;');
change('p_saveg.c','\tunsigned long count;','\tunsigned int count;');
change('z_zone.c','            I_Error ("Z_Malloc: failed on allocation of %i bytes", size);',
    `            unsigned pinned=0,purgeable=0,available=0;
            memblock_t *block;
            for(block=mainzone->blocklist.next;block!=&mainzone->blocklist;block=block->next) {
                if(block->tag==PU_FREE)available+=block->size;
                else if(block->tag>=PU_PURGELEVEL)purgeable+=block->size;
                else pinned+=block->size;
            }
            DoomZoneFailure(size,pinned,purgeable,available);
            I_Error ("Z_Malloc: failed on allocation of %i bytes", size);`);
change('z_zone.c','    // mark as free','    DoomZoneReleased(block->size);\n    // mark as free');
change('z_zone.c','    return result;','    DoomZoneAllocated(base->size);\n    return result;');
// Failed seeks/short reads must not silently reuse an old file cursor or count.
change('w_file_stdc.c','\tf_lseek (&stdc_wad->fstream, offset);\n\n    // Read into the buffer.\n\n    f_readn (&stdc_wad->fstream, buffer, buffer_len, &count);',
    '\tcount = 0;\n    if (f_lseek (&stdc_wad->fstream, offset) != FR_OK) return 0;\n    if (f_readn (&stdc_wad->fstream, buffer, buffer_len, &count) != FR_OK) return 0;');
const arm=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const host='C:/msys64/mingw32/bin/';
assert.match(run(host+'gcc.exe',['-dumpmachine']),/^i[3-6]86/);
const cFiles=fs.readdirSync(source).filter(p=>p.endsWith('.c')&&p!=='i_main.c').sort();assert.equal(cFiles.length,78);
const includes=['-I',source,'-I',path.join(root,'doom/host')];
const common=['-Os','-funsigned-char','-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage',...includes];
const cortex=['-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard'];
const cxxFiles=['vm/doom/platform.cpp','vm/doom/doomvm.cpp','engine/native-doom/mpe_doom_runtime.cpp'];
function compile(prefix,extra,dir){
    const objects=[];let warnings='';fs.mkdirSync(dir,{recursive:true});
    for(const file of cFiles){const obj=path.join(dir,file+'.o');
        warnings+=run(prefix+'gcc.exe',[...extra,...common,'-std=gnu11','-DMHS_NATIVE_DOOM_ADAPTER=1','-include',path.join(root,'vm/doom/core_config.h'),'-c',path.join(source,file),'-o',obj]);objects.push(obj);}
    for(const file of cxxFiles){const obj=path.join(dir,path.basename(file)+'.o');
        warnings+=run(prefix+'g++.exe',[...extra,...common,'-std=c++17','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-c',file,'-o',obj]);objects.push(obj);}
    write(path.join(dir,'compile.log'),warnings);return objects;
}
console.log('DoomVM: building Cortex-M7 extraction and measuring actual ABI-2 limits...');
const objects=compile(arm,cortex,path.join(out,'arm'));
const wraps=['malloc','calloc','realloc','free','_malloc_r','_calloc_r','_realloc_r','_free_r'].map(n=>'-Wl,--wrap='+n);
const link=[...cortex,'-nostartfiles',...objects,...wraps,'-Wl,--gc-sections','-lc','-lm','-lgcc'];
const measured=path.join(out,'measurement-only.elf');
run(arm+'g++.exe',[...link,'-T','vm/doom/measure.ld','-Wl,-Map='+path.join(out,'measurement-only.map'),'-o',measured]);
assert.equal(run(arm+'nm.exe',['-u',measured]).trim(),'');
const nm=run(arm+'nm.exe',['-S','--size-sort',measured]);write(path.join(out,'symbols.txt'),nm);
assert.ok(!nm.includes('_GLOBAL__sub_I'),'Module must not depend on uncalled global constructors');
assert.ok(!/\b(?:MemPool|external_psram_size|extmem_malloc|malloc|calloc|realloc|free)\r?$/m.test(nm),'Unexpected external arena or libc allocation');
const sizes=run(arm+'size.exe',['-A',measured]);write(path.join(out,'size.txt'),sizes);
const section=name=>Number(sizes.match(new RegExp('^\\.'+name+'\\s+(\\d+)','m'))?.[1]??0);
const textBytes=section('text'),rodataBytes=section('rodata'),staticBytes=section('data')+section('bss');
const strict=invoke(arm+'g++.exe',[...link,'-T','vm/abi/module.ld','-Wl,-Map='+path.join(out,'strict.map'),'-o',path.join(out,'strict.elf')]);
if(strict.error)throw strict.error;write(path.join(out,'strict-link.log'),strict.log);
const codeFits=textBytes+rodataBytes<=98304,supportFits=staticBytes+24576+16<=196608;
assert.equal(strict.status===0,codeFits&&staticBytes<196608,'Strict link and measured sections disagree');
console.log(`DoomVM: text ${textBytes}, read-only ${rodataBytes}, static support ${staticBytes} bytes; running 32-bit memory probes...`);
const hostObjects=compile(host,[],path.join(out,'host'));
const probe=path.join(out,'doom-module-probe.exe');
run(host+'g++.exe',['-std=c++17','-Os',...includes,'vm/tests/doom_module_probe.cpp',...hostObjects,'-static','-Wl,--gc-sections','-o',probe]);
const heapTest=path.join(out,'doom-heap-test.exe');run(host+'g++.exe',['-std=c++17','-O2','vm/tests/doom_heap_test.cpp','-static','-o',heapTest]);
const heapResult=run(heapTest,[]).trim();
const platformTest=path.join(out,'doom-platform-test.exe');
run(host+'g++.exe',['-std=c++17','-O2',...includes,'vm/tests/doom_platform_test.cpp','vm/doom/platform.cpp','-static','-o',platformTest]);
const platformResult=run(platformTest,[]).trim();
function probeRun(workspace,zone,mode){const log=run(probe,[wad,String(workspace),String(zone),mode]);
    const lines=log.trim().split(/\r?\n/);return JSON.parse(lines.findLast(s=>s.startsWith('{')));}
const constrained=probeRun(Math.max(0,196608-staticBytes),524288,'module');
// These are positive controls / bottleneck diagnostics with oversized memory.
// Their success can never make the 1 MiB fit gate pass.
const zoneOnly=probeRun(1048576,524288,'diagnostic');
const baseline=probeRun(1048576,8*1048576,'diagnostic');
const moduleIntegration=probeRun(1048576,524288,'module-diagnostic');
const heapFailure=probeRun(65536,524288,'diagnostic');
const zoneFailure=probeRun(1048576,131072,'diagnostic');
assert.ok(!heapFailure.started&&heapFailure.heapFailure&&heapFailure.guardsIntact,'Private-heap exhaustion must fail safely');
assert.ok(!zoneFailure.started&&zoneFailure.zoneRequest&&zoneFailure.guardsIntact,'Zone exhaustion must fail safely');
assert.ok(baseline.started&&baseline.e1m1&&baseline.tics===140&&baseline.changedFrames>40,'Positive-control core run failed');
if(zoneOnly.started&&zoneOnly.tics===baseline.tics)assert.equal(zoneOnly.finalFrameHash,baseline.finalFrameHash,'Purging changed deterministic E1M1 rendering');
assert.deepEqual(inputs.map(p=>({path:p,sha256:sha(path.join(root,p))})),snapshot,'Inputs changed during audit');
const evidence={status:'AUDIT_COMPLETE',loadable:strict.status===0&&supportFits&&constrained.started,
    hardwareTested:false,firmwareChanged:false,packageProduced:false,abi:2,sourceCommit:origin.commit,
    sourceTreeSha256:origin.treeSha256,wad:{bytes:fs.statSync(wad).size,sha256:sha(wad)},
    ram1:{moduleCodeLimit:98304,textBytes,rodataBytes,codeFits,moduleSupportLimit:196608,staticBytes,videoWorkspaceBytes:24576,supportFits},
    ram2:{limit:524288,executableBytes:0},heapTest:heapResult,platformTest:platformResult,
    constrained,zoneOnly,baseline,moduleIntegration,heapFailure,zoneFailure,
    inputs:snapshot,adaptedSources:cFiles.map(p=>({path:p,sha256:sha(path.join(source,p))}))};
write(path.join(out,'report.json'),JSON.stringify(evidence,null,2)+'\n');
console.log(JSON.stringify({status:evidence.status,loadable:evidence.loadable,ram1:evidence.ram1,constrained,zoneOnly,baseline,moduleIntegration,report:path.join(out,'report.json')},null,2));
if(process.argv.includes('--require-fit')&&!evidence.loadable)process.exitCode=1;

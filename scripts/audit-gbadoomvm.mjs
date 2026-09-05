// SPDX-License-Identifier: GPL-2.0-or-later
// Pinned GBADoom extraction. Ignored local test outputs; never flashes hardware.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {packVmImage} from './vm-image.mjs';
const root=fs.realpathSync(path.resolve(import.meta.dirname,'..'));
const upstream=path.join(root,'build/doom/upstream/GBADoom');
const out=path.join(root,'build/doom/gbadoom-audit');
const source=path.join(out,'source'),inc=path.join(out,'include');
const pinned='89097b3ff31ac1e1b2cdce9854e49726cfa462bf';
const wadArg=process.argv.indexOf('--wad');
if(!process.argv.includes('--arm-only')&&(wadArg<0||!process.argv[wadArg+1]))throw Error('Usage: node scripts/audit-gbadoomvm.mjs --wad <user-supplied DOOM1.WAD> [--require-fit]');
const write=(p,s)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);};
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const inputFiles=['scripts/audit-gbadoomvm.mjs','scripts/vm-image.mjs','vm/abi/module.ld','vm/doom/measure.ld','vm/doom/heap.h','vm/tests/gbadoom_module_probe.cpp','vm/tests/ram2_profile_test.cpp',
    'vm/tests/gbadoom_sound_test.cpp','Source/Teensy/MinimalBoot/Common/VMABI.h','Source/Teensy/MinimalBoot/Common/VMImageLoad.h',
    'Source/Teensy/MinimalBoot/Common/VMRegistry.h','Source/Teensy/MinimalBoot/VMHost.h',
    ...fs.readdirSync(path.join(root,'vm/doom/gba')).map(p=>'vm/doom/gba/'+p)];
const snapshot=inputFiles.map(p=>({path:p,sha256:sha(path.join(root,p))}));
function invoke(exe,args,options={}){
    const env={...process.env},key=Object.keys(env).find(k=>k.toUpperCase()==='PATH')??'PATH';
    env[key]=path.dirname(exe)+path.delimiter+(env[key]??'');
    const r=spawnSync(exe,args,{cwd:root,env,windowsHide:true,encoding:'utf8',maxBuffer:16*1024*1024,timeout:120000,...options});
    return {...r,log:(r.stdout??'')+(r.stderr??'')};
}
function run(exe,args,options={}){const r=invoke(exe,args,options);if(r.error||r.status!==0)throw Error(`${exe}: ${r.error??r.status}\n${r.log.slice(-4500)}`);return r.log;}
assert.equal(run('git',['-C',upstream,'rev-parse','HEAD']).trim(),pinned);
assert.equal(run('git',['-C',upstream,'status','--porcelain']).trim(),'','Pinned source must be clean');
for(const dir of ['include','source']){
    for(const entry of fs.readdirSync(path.join(upstream,dir),{recursive:true})){
        if(!/\.(c|h)$/.test(entry))continue;
        write(path.join(out,dir,entry),fs.readFileSync(path.join(upstream,dir,entry)));
    }
}
function change(p,old,next){const f=path.join(out,p),s=fs.readFileSync(f,'utf8').replaceAll('\r\n','\n');assert.equal(s.split(old).length,2,`Adaptation drift: ${p}`);write(f,s.replace(old,next));}
function edit(p,fn){const f=path.join(out,p);write(f,fn(fs.readFileSync(f,'utf8').replaceAll('\r\n','\n')));}
// Exact integer replacement for the ROM reciprocal table; validate all entries.
const table=fs.readFileSync(path.join(upstream,'source/m_recip.c'),'utf8').split('{')[1].split('}')[0].match(/\d+/g).map(Number);
assert.equal(table.length,65537);
for(let i=0;i<table.length;i++)assert.equal(table[i],i===0?0:i===1?0xffffffff:Number(((1n<<32n)+BigInt(i>>1))/BigInt(i)),`reciprocal ${i}`);
change('include/m_fixed.h','(reciprocalTable[val] >> shift)','((val == 0 ? 0u : val == 1 ? UINT_MAX : (unsigned int)((UINT64_C(4294967296) + val/2u)/val)) >> shift)');
// Cache calls perform I/O and may allocate: upstream PUREFUNC assumptions no longer apply.
edit('include/w_wad.h',s=>s.replaceAll('PUREFUNC',''));
// Correct upstream pointer declarations for current strict C compilers.
for(const p of ['include/hu_lib.h','source/hu_lib.c'])edit(p,s=>s.replace(/const patch_t\s*\*\s*(f|font)\b/g,'const patch_t **$1'));
change('source/hu_lib.c','font[0].height','font[0]->height');
change('include/i_system_e32.h','const byte* srcBuffer','const unsigned short* srcBuffer');
change('include/global_data.h','gamestate_t oldgamestate;','int oldgamestate;');
change('include/global_data.h','unsigned int fps_framerate;','int fps_framerate;');
change('include/st_lib.h','short*  num;','const int* num;');
for(const [p,list] of [['source/p_ceilng.c','activeceilings'],['source/p_plats.c','activeplats']]){
    change(p,`Z_Malloc(sizeof *list, PU_LEVEL, &_g->${list})`,`Z_Malloc(sizeof *list, PU_LEVEL, NULL)`);
    change(p,'    list->prev = old_head;',`    list->prev = &_g->${list};\n    _g->${list} = list;`);
}
// Native RAM needs the desktop 16-bit duplicate-pixel writes, not GBA VRAM mirroring.
// Keep desktop spare-VRAM arrays as explicitly counted ordinary RAM.
edit('source/r_hotpath.iwram.c',s=>s.replaceAll('#pragma GCC optimize ("Ofast")','#pragma GCC optimize ("Os")'));
change('source/r_hotpath.iwram.c','inline fixed_t CONSTFUNC FixedMul','fixed_t CONSTFUNC FixedMul');
const hot=fs.readFileSync(path.join(source,'r_hotpath.iwram.c'),'utf8');
const timeStart=hot.indexOf('int I_GetTime(void)');assert.ok(timeStart>0);
write(path.join(source,'r_hotpath.iwram.c'),hot.slice(0,timeStart)+'int I_GetTime(void) { return GbaClockTics(); }\n');
// All core allocations are bounded; record actual zone usage including headers.
edit('source/z_zone.c',s=>s.replace(/    unsigned int heapSize = maxHeapSize;[\s\S]*?    heapSize \+= 4;/,'    unsigned int heapSize;\n    mainzone = GbaZone(&heapSize);'));
edit('source/z_zone.c',s=>s.replace(/#ifndef GBA[\s\S]*?#endif/g,''));
change('source/z_zone.c','    // mark as free','    GbaZoneReleased(block->size);\n    // mark as free');
change('source/z_zone.c','    return (void *) ((byte *)base + sizeof(memblock_t));','    GbaZoneAllocated(base->size);\n    return (void *) ((byte *)base + sizeof(memblock_t));');
change('source/z_zone.c','            I_Error ("Z_Malloc: failed on allocation of %i bytes", size);','            GbaZoneFailure(size);\n            I_Error ("Z_Malloc: failed on allocation of %i bytes", size);');
edit('source/z_zone.c',s=>s+'\nvoid GbaSetZoneTag(void *p,int tag){memblock_t *b=(memblock_t *)((byte *)p-sizeof(memblock_t));b->tag=tag;}\n');
// Texture definitions keep lump IDs instead of permanent ROM pointers.
change('include/r_data.h','const patch_t* patch;','int lump;');
change('source/r_data.c','patch->patch = (const patch_t*)W_CacheLumpName(pname);','patch->lump = W_GetNumForName(pname);');
change('source/r_data.c','patch->patch->width','GbaPatchWidth(patch->lump)');
change('source/r_data.c','p2->patch->width','GbaPatchWidth(p2->lump)');
edit('source/r_hotpath.iwram.c',s=>s.replaceAll('const patch_t* realpatch = patch->patch;','const patch_t* realpatch = GbaFrameLump(patch->lump);').replaceAll('W_CacheLumpNum(','GbaFrameLump('));
change('source/r_hotpath.iwram.c','texture->patches[0].patch','GbaFrameLump(texture->patches[0].lump)');
// Use directory metadata already read from SD, not a mapped whole-WAD pointer.
change('source/d_main.c','    CheckIWAD2(doom_iwad, doom_iwad_len, &_g->gamemode, &_g->haswolflevels);',
    '    if (W_CheckNumForName("E1M1") < 0) GbaFatal("E1M1 demo data required");\n    _g->gamemode = shareware;\n    _g->haswolflevels = false;');
change('source/d_main.c','static void D_DoomMainSetup(void)','void D_DoomMainSetup(void)');
change('source/d_main.c','static void D_Display (void)','void GbaDisplay (void)');
edit('source/d_main.c',s=>s.replaceAll('D_Display();','GbaDisplay();'));
change('source/d_main.c','    wipe = (_g->gamestate != _g->wipegamestate);','    wipe = false; // Nonblocking module frame ownership; no synchronous melt.');
change('source/d_main.c','    D_BuildNewTiccmds();','    // Module supplies exactly one command per gametic.');
change('source/d_main.c','const boolean nomusicparm = false;','const boolean nomusicparm = true;');
// This candidate supports only the shareware demo's first level. Both exits
// restart E1M1 without allocating intermission assets or advancing to E1M2.
change('source/g_game.c','void G_InitNew(skill_t skill, int episode, int map)\n{',
    'void G_InitNew(skill_t skill, int episode, int map)\n{\n    episode = 1; map = 1;');
edit('source/g_game.c',s=>{
    const start=s.indexOf('void G_DoCompleted (void)\n{'),end=s.indexOf('//\n// G_WorldDone',start);
    assert.ok(start>0&&end>start);
    return s.slice(0,start)+'void G_DoCompleted (void)\n{\n    G_InitNew(_g->gameskill, 1, 1);\n    _g->gameaction = ga_nothing;\n}\n\n'+s.slice(end);
});
change('source/g_game.c','_g->gamemap = _g->wminfo.next+1;','_g->gameepisode = 1; _g->gamemap = 1;');
// Three synthesized SID voices retain game-side attenuation and channel policy.
change('source/s_sound.c','static const unsigned int numChannels = 8;','static const unsigned int numChannels = 3;');
change('source/s_sound.c','(channel_t *) calloc(numChannels,sizeof(channel_t))','(channel_t *) GbaSupportAlloc(numChannels*sizeof(channel_t))');
change('source/s_sound.c','return (channel->tickend < ticknow);','return (channel->tickend >= ticknow);');
change('source/s_sound.c','        c->sfxinfo = 0;','        GbaSoundStop(cnum);\n        c->sfxinfo = 0;');
write(path.join(source,'lprintf.c'),'#include "lprintf.h"\nint lprintf(OutputLevels p,const char *s,...){(void)p;(void)s;return 0;}\n');
const arm=path.join(root,'build/toolchain/Arduino15/packages/teensy/tools/teensy-compile/11.3.1/arm/bin/arm-none-eabi-');
const host='C:/msys64/mingw32/bin/';
const cortex=['-mcpu=cortex-m7','-mthumb','-mfpu=fpv5-d16','-mfloat-abi=hard'];
const include=['-I',inc,'-I',path.join(root,'vm/doom/gba')];
const common=['-Os','-funsigned-char','-fno-strict-aliasing','-fwrapv','-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fstack-usage',...include,'-include',path.join(root,'vm/doom/gba/core_api.h')];
const excluded=new Set(['i_main.c','i_audio.c','doom_iwad.c','w_wad.c','m_recip.c']);
const files=fs.readdirSync(source).filter(p=>p.endsWith('.c')&&!excluded.has(p)).sort().map(p=>path.join(source,p));
files.push(...['core.c','w_wad.c','sound.c','platform.cpp','doomvm.cpp'].map(p=>path.join(root,'vm/doom/gba',p)));
function compile(prefix,flags,dir){
    fs.mkdirSync(dir,{recursive:true});let log='';const objects=[];
    for(const f of files){const cpp=f.endsWith('.cpp'),obj=path.join(dir,path.basename(f)+'.o');
        const r=invoke(prefix+(cpp?'g++':'gcc')+'.exe',[...flags,...common,cpp?'-std=c++17':'-std=gnu11',...(cpp?['-fno-exceptions','-fno-rtti','-fno-threadsafe-statics']:[]),'-c',f,'-o',obj]);
        log+=r.log;write(path.join(dir,'compile.log'),log);if(r.status!==0||r.error)throw Error(r.log.slice(-4000));objects.push(obj);
    }return objects;
}
console.log('GBADoom: exact reciprocal replacement passed 65,537 entries; compiling complete native core.');
const objects=compile(arm,cortex,path.join(out,'arm'));
const wraps=['malloc','calloc','realloc','free','_malloc_r','_calloc_r','_realloc_r','_free_r'].map(n=>'-Wl,--wrap='+n);
const link=[...cortex,'-nostartfiles','--specs=nano.specs',...objects,...wraps,'-Wl,--gc-sections','-lc','-lm','-lgcc'];
const elf=path.join(out,'measurement-only.elf');
run(arm+'g++.exe',[...link,'-T','vm/doom/measure.ld','-Wl,-Map='+path.join(out,'measurement-only.map'),'-o',elf]);
assert.equal(run(arm+'nm.exe',['-u',elf]).trim(),'');
const symbols=run(arm+'nm.exe',['-S','--size-sort',elf]);write(path.join(out,'symbols.txt'),symbols);
assert.ok(!/\b(?:reciprocalTable|doom_iwad|malloc|calloc|realloc|free|_malloc_r|_calloc_r|_realloc_r|_free_r|_GLOBAL__sub_I)\b/.test(symbols),'Unbounded heap, constructor or ROM dependency');
const sizes=run(arm+'size.exe',['-A',elf]);write(path.join(out,'size.txt'),sizes);
const section=n=>Number(sizes.match(new RegExp('^\\.'+n+'\\s+(\\d+)','m'))?.[1]??0);
const legacy=invoke(arm+'g++.exe',[...link,'-T','vm/abi/module.ld','-Wl,-Map='+path.join(out,'legacy.map'),'-o',path.join(out,'legacy.elf')]);
write(path.join(out,'legacy-link.log'),legacy.log);
const profileElf=path.join(out,'profile.elf');
const strict=invoke(arm+'g++.exe',[...link,'-T','vm/doom/gba/module.ld','-Wl,-Map='+path.join(out,'profile.map'),'-o',profileElf]);
write(path.join(out,'strict-link.log'),strict.log);
if(strict.error||strict.status!==0)throw Error(strict.log);
const profileSizes=run(arm+'size.exe',['-A',profileElf]);write(path.join(out,'profile-size.txt'),profileSizes);
const profileSection=n=>Number(profileSizes.match(new RegExp('^\\.'+n+'\\s+(\\d+)','m'))?.[1]??0);
const profileSymbols=run(arm+'nm.exe',['-n',profileElf]);write(path.join(out,'profile-symbols.txt'),profileSymbols);
assert.equal(run(arm+'nm.exe',['-u',profileElf]).trim(),'');
const report={status:'ARM_AUDIT_COMPLETE',sourceCommit:pinned,loadable:false,hardwareTested:false,packageProduced:false,
    ram1:{textBytes:profileSection('text'),dataBytes:profileSection('data'),bssBytes:profileSection('bss'),staticBytes:profileSection('data')+profileSection('bss'),codeLimit:98304,supportLimit:196608},
    ram2:{guestBytes:425984,roBase:0x20268000,roBytes:profileSection('rodata2'),roLimit:98304},
    oldLayout:{textBytes:section('text'),rodataBytes:section('rodata'),staticBytes:section('data')+section('bss'),linkPassed:legacy.status===0},
    strictLinkPassed:strict.status===0,reciprocalEntriesVerified:table.length,firmwareSourceChanged:true,firmwareFlashed:false,
    inputs:snapshot,toolchains:{arm:run(arm+'gcc.exe',['-dumpfullversion']).trim(),host:run(host+'gcc.exe',['-dumpfullversion']).trim()},
    adaptedSources:files.map(p=>({path:path.relative(root,p),sha256:sha(p)}))};
write(path.join(out,'report.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({ram1:report.ram1,strictLinkPassed:report.strictLinkPassed}));
if(process.argv.includes('--arm-only'))process.exit(0);
const sectionBytes={};for(const name of ['text','data','rodata2']){
    const file=path.join(out,name+'.bin');run(arm+'objcopy.exe',['-O','binary','--only-section=.'+name,profileElf,file]);sectionBytes[name]=fs.readFileSync(file);
}
const entry=parseInt(profileSymbols.match(/^([0-9a-f]+) T vm_entry$/m)?.[1]??'',16)|1;
const moduleBytes=packVmImage({code:sectionBytes.text,data:sectionBytes.data,bssBytes:profileSection('bss'),entry,requiredServices:211,profile:1,rodata:sectionBytes.rodata2});
const moduleFile=path.join(out,'engine.mvm');write(moduleFile,moduleBytes);
report.module={path:moduleFile,bytes:moduleBytes.length,sha256:sha(moduleFile)};
const loaderTest=path.join(out,'ram2-profile-test.exe');
run(host+'g++.exe',['-std=c++17','-O2','vm/tests/ram2_profile_test.cpp','-static','-o',loaderTest]);report.loaderTest=run(loaderTest,[moduleFile]).trim();
// Host execution is appended below as the resource adapter is brought up.
const hostObjects=compile(host,[],path.join(out,'host'));
const soundTest=path.join(out,'sound-test.exe');
run(host+'g++.exe',['-std=c++17','-O2',...include,'vm/tests/gbadoom_sound_test.cpp',path.join(out,'host/sound.c.o'),'-static','-o',soundTest]);
report.soundTest=run(soundTest,[]).trim();
const probe=path.join(out,'gbadoom-probe.exe');
run(host+'g++.exe',['-std=c++17','-Os',...include,'vm/tests/gbadoom_module_probe.cpp',...hostObjects,'-static','-Wl,--gc-sections','-o',probe]);
const original=fs.realpathSync(process.argv[wadArg+1]),converted=path.join(out,'doom1.gba.wad'),wad=path.join(out,'doom1.gbd');
assert.equal(run('git',['ls-files','--',original]).trim(),'','WAD must remain untracked');
const originalHash=sha(original),converter=path.join(upstream,'GbaWadUtil/GbaWadUtil.exe');
const conversion=run(converter,['-in',original,'-out',converted]);write(path.join(out,'conversion.log'),conversion);
const payload=fs.readFileSync(converted);assert.equal(payload.toString('ascii',0,4),'IWAD');
function crc32(bytes){let crc=0xffffffff;for(const b of bytes){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
const envelope=Buffer.alloc(16);envelope.write('GBDWAD1');envelope.writeUInt32LE(payload.length,8);envelope.writeUInt32LE(crc32(payload),12);write(wad,Buffer.concat([envelope,payload]));
report.wad={sourceBytes:fs.statSync(original).size,sourceSha256:originalHash,convertedBytes:payload.length,convertedSha256:sha(converted),envelopedSha256:sha(wad),
    converterSha256:sha(converter),converterDllSha256:sha(path.join(upstream,'GbaWadUtil/Qt5Core.dll')),supplementSha256:sha(path.join(upstream,'GbaWadUtil/gbadoom.wad'))};
function probeRun(workspace,zone,mode,input=wad){
    const log=run(probe,[input,String(workspace),String(zone),mode]);
    const result=JSON.parse(log.trim().split(/\r?\n/).findLast(s=>s.startsWith('{')));
    const sound=log.match(/^AUDIO (\d+) (\d+)/m);if(sound){result.audioFrames=Number(sound[1]);result.audioHash=Number(sound[2]);result.soundSample=log.match(/^AUDIO_SAMPLE ([0-9a-f]+)/m)?.[1];}return result;
}
report.constrained=probeRun(196608-report.ram1.staticBytes,425984,'module');
report.defaultLaunch=probeRun(196608-report.ram1.staticBytes,425984,'module-default');
report.zoneOnly=probeRun(196608-report.ram1.staticBytes,425984,'diagnostic');
report.baseline=probeRun(196608-report.ram1.staticBytes,4*1048576,'diagnostic');
report.purging=probeRun(196608-report.ram1.staticBytes,393216,'diagnostic');
report.levelCycles=probeRun(196608-report.ram1.staticBytes,425984,'diagnostic-cycle');
report.cycleBaseline=probeRun(196608-report.ram1.staticBytes,4*1048576,'diagnostic-cycle');
report.zoneFailure=probeRun(196608-report.ram1.staticBytes,131072,'diagnostic');
report.supportFailure=probeRun(24576+16,524288,'diagnostic');
report.rawWadRejected=probeRun(196608-report.ram1.staticBytes,524288,'diagnostic',original);
const badPayload=Buffer.concat([envelope,payload]);badPayload[20]^=1;
const corrupt=path.join(out,'corrupt.gbd');write(corrupt,badPayload);
report.checksumFailure=probeRun(196608-report.ram1.staticBytes,524288,'diagnostic',corrupt);
report.ioFailure=probeRun(196608-report.ram1.staticBytes,524288,'diagnostic-io');
const badDirectory=Buffer.from(payload);badDirectory.writeUInt32LE(0x7fffffff,8);
const badHeader=Buffer.from(envelope);badHeader.writeUInt32LE(crc32(badDirectory),12);
const malformed=path.join(out,'bad-directory.gbd');write(malformed,Buffer.concat([badHeader,badDirectory]));
report.directoryFailure=probeRun(196608-report.ram1.staticBytes,524288,'diagnostic',malformed);
for(const result of [report.constrained,report.zoneOnly,report.baseline,report.purging])assert.ok(result.started&&result.tics===140&&result.changedFrames>40&&result.e1m1&&!result.error,'Core run failed');
assert.equal(report.constrained.frames,140);
assert.equal(report.constrained.frameHash,report.defaultLaunch.frameHash);
assert.equal(report.constrained.audioHash,report.defaultLaunch.audioHash);
assert.equal(report.zoneOnly.frameHash,report.baseline.frameHash);
assert.equal(report.purging.frameHash,report.baseline.frameHash);
assert.equal(report.zoneOnly.audioHash,report.baseline.audioHash);
assert.equal(report.purging.audioHash,report.baseline.audioHash);
assert.ok(report.purging.resourceCount>report.baseline.resourceCount,'Purging probe did not reload assets');
for(const result of [report.levelCycles,report.cycleBaseline])assert.ok(result.started&&result.tics===2100&&result.e1m1&&!result.error,'E1M1 restart run failed');
assert.equal(report.levelCycles.frameHash,report.cycleBaseline.frameHash);
assert.equal(report.levelCycles.audioHash,report.cycleBaseline.audioHash);
for(const result of [report.zoneFailure,report.supportFailure,report.rawWadRejected,report.checksumFailure,report.ioFailure,report.directoryFailure])assert.ok(!result.started&&result.error&&result.guardsIntact&&result.handlesClosed,'Failure did not close cleanly');
assert.equal(sha(original),originalHash,'Input WAD was changed');
assert.deepEqual(inputFiles.map(p=>({path:p,sha256:sha(path.join(root,p))})),snapshot,'Inputs changed during audit');
report.loadable=report.strictLinkPassed&&report.constrained.started&&report.ram1.staticBytes+report.constrained.supportUsed<=196608;
report.status='AUDIT_COMPLETE';
write(path.join(out,'report.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({status:report.status,loadable:report.loadable,ram1:report.ram1,constrained:report.constrained,zoneOnly:report.zoneOnly,purging:report.purging,negativeChecks:6,report:path.join(out,'report.json')},null,2));
if(process.argv.includes('--require-fit')&&!report.loadable)process.exitCode=1;

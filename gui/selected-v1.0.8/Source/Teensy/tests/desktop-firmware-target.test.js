'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');
test('firmware confirmation and startup discovery reject changed or unconfirmed starts',t=>{
  const compiler=[process.env.CXX,'g++','clang++','C:/msys64/mingw64/bin/g++.exe'].filter(Boolean)
    .find(candidate=>spawnSync(candidate,['--version'],{encoding:'utf8'}).status===0);
  assert.ok(compiler,'C++11 host compiler required');
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'firmware-target-'));
  try {
    const execution=fs.readFileSync(path.join(__dirname,'../DriveDirLoad.ino'),'utf8');
    const begin=execution.indexOf('void HandleExecution()');
    const end=execution.indexOf('\nvoid MenuChange()',begin);
    assert.ok(begin>=0 && end>begin,'production HandleExecution body is available');
    fs.writeFileSync(path.join(temporary,'handle-execution-under-test.h'),execution.slice(begin,end));
    const operations=fs.readFileSync(path.join(__dirname,'../MinimalBoot/Common/IO_Handlers/DesktopFileOps.c'),'utf8');
    const commandBegin=operations.indexOf('FLASHMEM void DesktopFileCommand()');
    const commandEnd=operations.indexOf('\nFLASHMEM void DesktopFilePoll()',commandBegin);
    assert.ok(commandBegin>=0 && commandEnd>commandBegin,'production file-command body is available');
    fs.writeFileSync(path.join(temporary,'file-command-under-test.h'),operations.slice(commandBegin,commandEnd));
    const output=path.join(temporary,'guard.exe');
    const env={...process.env,PATH:path.dirname(compiler)+path.delimiter+process.env.PATH};
    const build=spawnSync(compiler,['-std=c++11','-Wall','-Wextra','-Werror','-Wno-implicit-fallthrough','-I',temporary,path.join(__dirname,'desktop-firmware-target.cpp'),'-o',output],{encoding:'utf8',env});
    assert.equal(build.status,0,build.stdout+build.stderr);
    const run=spawnSync(output,[],{encoding:'utf8',env});
    assert.equal(run.status,0,run.stdout+run.stderr);assert.match(run.stdout,/30 firmware target checks passed/);
    assert.match(run.stdout,/65 firmware discovery checks passed/);
    t.diagnostic(run.stdout.trim());
    const handlers=fs.readFileSync(path.join(__dirname,'../MinimalBoot/Common/IO_Handlers/IOH_TeensyROM.c'),'utf8');
    assert.match(handlers,/case rCtlFirmwarePrepareWAIT:\s+case rCtlFirmwareCheckWAIT:[\s\S]*?rsFirmwareTarget/);
    assert.match(handlers,/case rCtlFirmwareDiscoverWAIT:\s+IO1\[wRegControl\] = Data;\s+IO1\[rwRegStatus\] = rsFirmwareTarget/);
    assert.match(execution,/if \(capturedFirmware\) \{\s+if \(!DesktopFirmwareBegin\(MenuSelCpy, launchSource\)\) return/);
    assert.match(execution,/switch\(launchSource\)/);assert.match(execution,/DesktopFirmware\.pathName\(FullFilePath/);
    assert.doesNotMatch(execution.slice(begin,end),/sprintf\(FullFilePath/);
    assert.match(handlers,/void InitHndlr_TeensyROM\(\)\s*\{\s*DesktopFirmwareCancel\(\)/);
    assert.match(handlers,/DesktopFirmwareCancel\(\);\s*DesktopFirmwareResetDiscovery\(\)/);
  } finally { assert.equal(path.dirname(temporary),path.resolve(os.tmpdir()));fs.rmSync(temporary,{recursive:true,force:true}); }
});

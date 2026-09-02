'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const {mkdtempSync, rmSync, readFileSync} = require('node:fs');
const {tmpdir} = require('node:os');
const path = require('node:path');

test('production desktop map keeps raw selections coherent across parent positions and page boundaries', () => {
  const compiler = [process.env.CXX, 'g++', 'clang++', 'C:/msys64/mingw64/bin/g++.exe'].filter(Boolean)
    .find(candidate => spawnSync(candidate, ['--version'], {encoding: 'utf8'}).status === 0);
  assert.ok(compiler, 'Set CXX to a C++11-capable host compiler');
  const temporary = mkdtempSync(path.join(tmpdir(), 'tr-menu-view-'));
  try {
    const executable = path.join(temporary, process.platform === 'win32' ? 'view.exe' : 'view');
    const env = {...process.env, PATH: path.dirname(compiler) + path.delimiter + process.env.PATH};
    const build = spawnSync(compiler, ['-std=c++11', '-Wall', '-Wextra', '-Werror',
      path.join(__dirname, 'desktop-menu-view.cpp'), '-o', executable], {encoding: 'utf8', env});
    assert.equal(build.status, 0, build.stdout + build.stderr);
    const run = spawnSync(executable, [], {encoding: 'utf8', env});
    assert.equal(run.status, 0, run.stdout + run.stderr);
    assert.match(run.stdout, /55 desktop menu view scenarios passed/);
  } finally { rmSync(temporary, {recursive: true, force: true}); }
});

test('menu adapter maps selection, search and file-operation refresh without changing raw menus', () => {
  const source = name => readFileSync(path.join(__dirname, '..', name), 'utf8');
  const handlers = 'MinimalBoot/Common/IO_Handlers/';
  const io = source(handlers + 'IOH_TeensyROM.c');
  assert.match(io, /case rwRegSelItemOnPage:\s+MenuViewSelect\(Data\)/);
  assert.match(io, /case rwRegPageNumber:\s+MenuViewSetPage\(Data\)/);
  assert.match(io, /case rwRegMenuView:[\s\S]*?IO1\[rwRegStatus\] = rsMenuView/);
  assert.match(io, /case rsstItemName:\s+if \(!MenuViewSelectionValid\(\)\)/);
  assert.match(source('DriveDirLoad.ino'), /void HandleExecution\(\)\s*\{\s*if \(!MenuViewSelectionValid\(\)\)/);
  assert.match(source(handlers + 'DesktopFileOps.c'), /MenuViewSetPage\(page\)/);
  assert.match(source(handlers + 'StatusFunctions.c'), /MenuViewFromRaw\(ItemNum\) != MenuViewInvalid/);
  for (const name of [handlers + 'IOH_TeensyROM.c', handlers + 'StatusFunctions.c',
    handlers + 'DesktopFileOps.c', 'MeatloafComm.ino']) {
    assert.doesNotMatch(source(name), /SelItemFullIdx\s*=\s*(?:Data|IO1\[rwRegCursorItemOnPg\])\s*\+/,
      `${name}: no direct visible-to-raw index arithmetic`);
  }
});

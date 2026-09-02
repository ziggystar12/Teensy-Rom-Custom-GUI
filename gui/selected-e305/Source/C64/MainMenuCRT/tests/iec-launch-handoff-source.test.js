'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const sourceRoot = path.resolve(__dirname, '../../..');
const source = name => fs.readFileSync(path.join(sourceRoot, name), 'utf8');
const regs = source('Teensy/MinimalBoot/Common/Menu_Regs.h');
const regsI = source('C64/MainMenuCRT/source/Menu_Regs.i');
const handler = source('Teensy/MinimalBoot/Common/IO_Handlers/IOH_TeensyROM.c');
const status = source('Teensy/MinimalBoot/Common/IO_Handlers/StatusFunctions.c');
const handlers = source('Teensy/IOHandlers.ino');

test('IEC launch has the same unused command opcode in both register maps', () => {
  for (const map of [regs, regsI]) {
    assert.match(map, /rCtlRunningIEC\s*=\s*56\b/);
    const commands = [...map.matchAll(/\b(rCtl\w+)\s*=\s*(0x[\da-f]+|\d+)\b/gi)];
    assert.deepEqual(commands.filter(entry => Number(entry[2]) === 56).map(entry => entry[1]), ['rCtlRunningIEC']);
    assert.match(map, /rsIOHWNextInit\s*=\s*0x14\b/);
  }
});

test('IEC launch arms the existing synchronous swap handshake without a selected menu item', () => {
  const command = handler.match(/case rCtlRunningIEC:([\s\S]*?)\bbreak;/)?.[1];
  assert.ok(command, 'IEC launch command must be handled');
  assert.match(command, /IO1\[rwRegStatus\]\s*=\s*rsIOHWNextInit;/);
  assert.match(command, /HandshakeReady\s*=\s*false;\s*PendingfBusSnoop\s*=\s*NULL;\s*fBusSnoop\s*=\s*&HandshakeSnoop;/);
  assert.doesNotMatch(command, /MenuSource|SelItemFullIdx|rsIOHWSelInit|IOH_None|rwRegNextIOHndlr\]\s*=/);
  assert.match(handler, /if \(!HandshakeReady\)[\s\S]*?DataPortWriteWait\(rihsBusy\);[\s\S]*?DataPortWriteWait\(rihsReady\);\s*fBusSnoop = PendingfBusSnoop;/);
});

test('IEC status uses the configured default handler and retains its staged bus snoop', () => {
  const table = status.match(/void \(\*StatusFunction\[rsNumStatusTypes\]\)\(\)\s*=[\s\S]*?\{([\s\S]*?)\};/)?.[1];
  assert.ok(table, 'status dispatch table must exist');
  assert.equal([...table.matchAll(/&(\w+)/g)][0x14]?.[1], 'IOHandlerNextInit');
  const nextInit = handlers.match(/void IOHandlerNextInit\(\)\s*\{([\s\S]*?)\}/)?.[1];
  assert.ok(nextInit, 'default handler initializer must exist');
  assert.match(nextInit, /IOHandlerInit\(IO1\[rwRegNextIOHndlr\]\);/);
  assert.doesNotMatch(nextInit, /MenuSource|SelItemFullIdx|IOH_None/);
  assert.match(handlers, /if \(IOHandler\[NewIOHandler\]->InitHndlr != NULL\) IOHandler\[NewIOHandler\]->InitHndlr\(\);/);
  assert.match(handlers, /CurrentIOHandler = NewIOHandler;[\s\S]*?if \(fBusSnoop == &HandshakeSnoop\) HandshakeReady = true;\s*else fBusSnoop = PendingfBusSnoop;/);
});

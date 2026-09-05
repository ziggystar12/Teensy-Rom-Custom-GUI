#!/usr/bin/env node
// Run the actual DOS CRT from C64 reset in headless VICE. This validates the
// loader and memory mapping with no Teensy service; its expected endpoint is
// a stable, bounded WAIT timeout, not a DOS prompt or physical bus proof.
import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";
import { loadDosTerminal } from '../tools/dos_terminal.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const agiRoot = path.join(root, "vm/client");
const options = {
  crt: path.join(root, "build/dos-work/DOSVM.CRT"),
  manifest: path.join(root, "build/dos-work/dosvm-terminal.json"),
  out: path.join(root, "build/dos-work/c64-boot"),
  standard: "ntsc",
  vice: path.resolve(root, "../AGI-64/tools/VICE-3.10/GTK3VICE-3.10-win64/bin/x64sc.exe")
};
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index].replace(/^--/, "");
  assert.ok(Object.hasOwn(options, key) && process.argv[index + 1], `Unknown/incomplete option ${key}`);
  options[key] = process.argv[index + 1];
}
assert.ok(["pal", "ntsc"].includes(options.standard));
options.out = path.resolve(options.out);
options.crt = path.resolve(options.crt);
fs.mkdirSync(options.out, { recursive: true });
const manifest = JSON.parse(fs.readFileSync(path.resolve(options.manifest), "utf8"));
const { buildMpe3TitleTerminal, MPE3_TITLE_TERMINAL_STATE: stateAddress } =
  await loadDosTerminal(agiRoot);
const terminal = buildMpe3TitleTerminal({
  gameplay: true, enable1351Mouse: false,
  diagnosticTitle: manifest.diagnosticTitle,
  diagnosticFooter: manifest.diagnosticFooter
});
const digest = (bytes) => crypto.createHash("sha256").update(bytes).digest("hex");
assert.equal(digest(terminal.prg), manifest.terminalPrgSha256, "terminal source differs from the tested build");
const crt = fs.readFileSync(options.crt);
assert.equal(crt.subarray(0, 16).toString("ascii"), "C64 CARTRIDGE   ");
assert.equal(crt.readUInt16BE(0x16), 32);
const bank0 = Buffer.alloc(0x4000, 0xff);
const banks = [];
for (let cursor = crt.readUInt32BE(0x10); cursor < crt.length;) {
  assert.equal(crt.subarray(cursor, cursor + 4).toString("ascii"), "CHIP");
  const bank = crt.readUInt16BE(cursor + 10), address = crt.readUInt16BE(cursor + 12);
  const bytes = crt.readUInt16BE(cursor + 14), length = crt.readUInt32BE(cursor + 4);
  assert.equal(bytes, 0x2000);
  assert.equal(length, bytes + 16);
  assert.ok(address === 0x8000 || address === 0xa000);
  assert.ok(cursor + length <= crt.length);
  if (bank === 0) crt.copy(bank0, address - 0x8000, cursor + 16, cursor + length);
  banks.push(bank);
  cursor += length;
}
assert.ok(!banks.includes(58), "native mailbox bank 58 must remain omitted");
assert.equal(bank0.readUInt16LE(0x3ffc), 0x8009);
const payload = terminal.prg.subarray(2);
assert.deepEqual(bank0.subarray(0xfd, 0xfd + payload.length), payload,
  "CRT boot payload differs from the generated DOS terminal");
assert.equal(0x0801 + payload.length, terminal.codeEnd);
assert.ok(terminal.codeEnd <= terminal.stageAddress);
const startCommand = Buffer.from([0xa9, 1, 0x8d, 0xf4, 0xdf]);
const startOffset = payload.indexOf(startCommand);
assert.ok(startOffset >= 0 && payload.indexOf(startCommand, startOffset + 1) === -1);
const startWritten = 0x0801 + startOffset + startCommand.length;
const file = (name) => path.join(options.out, name);
const mpath = (value) => value.replaceAll("\\", "/");
const hex = (value) => `$${value.toString(16).padStart(4, "0")}`;
const save = (name, begin, end) => `bsave "${mpath(file(name))}" 0 ${hex(begin)} ${hex(end)}`;
const write = (name, lines) => fs.writeFileSync(file(name), `${lines.join("\n")}\n`);
const playback = (id, name) => `command ${id} "playback \\"${mpath(file(name))}\\""`;
// Reuse one working directory, but never let stale dumps satisfy this run.
for (const name of [
  "result.json", "screen.txt", "monitor.log", "vice-system.log", "stdout.txt", "stderr.txt",
  "entry-port.bin", "entry-payload.bin", "start-port.bin", "start-io2.bin",
  "start-screen.bin", "start-vectors-cpu.bin", "start-vectors-ram.bin",
  "timeout-state.bin", "timeout-screen.bin", "timeout-io2.bin",
  "settled-state.bin", "settled-screen.bin"
]) fs.rmSync(file(name), { force: true });
const copiedCrt = file("audit-input.crt");
fs.writeFileSync(copiedCrt, crt);
write("reset.mon", ["bank cpu", "r", "break $0810", playback(1, "entry.mon"), "x"]);
write("entry.mon", [
  "disable 1", "r", save("entry-port.bin", 0, 1),
  save("entry-payload.bin", 0x0801, terminal.codeEnd - 1),
  `break ${hex(startWritten)}`, playback(2, "start.mon"), "x"
]);
write("start.mon", [
  "disable 2", "r", "io", save("start-port.bin", 0, 1),
  save("start-io2.bin", 0xdff0, 0xdfff), save("start-screen.bin", 0x0400, 0x07e7),
  save("start-vectors-cpu.bin", 0xfffa, 0xffff),
  "bank ram", save("start-vectors-ram.bin", 0xfffa, 0xffff), "bank cpu",
  `break ${hex(terminal.labels.terminal_error_hold)}`, playback(3, "timeout.mon"), "x"
]);
write("timeout.mon", [
  "disable 3", "r", save("timeout-state.bin", 0x02a0, 0x02bf),
  save("timeout-screen.bin", 0x0400, 0x07e7), save("timeout-io2.bin", 0xdff0, 0xdfff),
  `break ${hex(terminal.labels.terminal_error_hold)}`, "ignore 4 $4000",
  playback(4, "settled.mon"), "x"
]);
write("settled.mon", [
  "disable 4", "r", save("settled-state.bin", 0x02a0, 0x02bf),
  save("settled-screen.bin", 0x0400, 0x07e7), "quit"
]);
const vice = path.resolve(options.vice);
const result = spawnSync(vice, [
  "-default", `-${options.standard}`, "-console", "-directory", path.dirname(path.dirname(vice)),
  "-initbreak", "reset", "-warp", "+sound", "+easyflashcrtwrite", "-cartcrt", copiedCrt,
  "-monlogname", file("monitor.log"), "-monlog", "-moncommands", file("reset.mon"),
  "-limitcycles", "40000000", "-logfile", file("vice-system.log")
], { cwd: options.out, encoding: "utf8", windowsHide: true, timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
fs.writeFileSync(file("stdout.txt"), result.stdout ?? "");
fs.writeFileSync(file("stderr.txt"), result.stderr ?? "");
assert.ifError(result.error);
assert.equal(result.status, 0, `VICE failed; inspect ${options.out}`);
const monitor = fs.readFileSync(file("monitor.log"), "utf8");
assert.doesNotMatch(monitor, /ERROR --/);
assert.match(monitor, />C:de00\s+3a 3a 87 87/i, "bank 58 and 16K EasyFlash mode must persist");
const read = (name) => fs.readFileSync(file(name));
assert.deepEqual(read("entry-payload.bin"), payload, "actual C64 ROM-to-RAM copy differs");
assert.equal(read("entry-port.bin")[0] & 7, 7);
assert.equal(read("entry-port.bin")[1] & 7, 7);
assert.equal(read("start-port.bin")[0] & 7, 7);
assert.equal(read("start-port.bin")[1] & 7, 5, "terminal must run with ROM mapped out and IO visible");
const io2 = read("start-io2.bin");
assert.equal(io2.subarray(0, 4).toString("ascii"), "M3TP");
assert.equal(io2[4], 1);
assert.equal(io2[11], (options.standard === "ntsc" ? 0x81 : 0x80) |
  (terminal.labels.mpe_video_stream ? 2 : 0),
  "START must publish the detected C64 video standard and shared-stream capability");
assert.equal(io2[6], 0);
assert.equal(io2[7], 0);
assert.deepEqual(read("start-vectors-cpu.bin"), read("start-vectors-ram.bin"));
for (const offset of [0, 4]) {
  const vector = read("start-vectors-cpu.bin").readUInt16LE(offset);
  assert.ok(vector >= 0x0810 && vector < terminal.codeEnd, "IRQ/NMI must use terminal RAM vectors");
}
const state = read("timeout-state.bin");
const value = (address) => state[address - 0x02a0];
assert.equal(value(stateAddress.error), 2, "missing firmware must report bounded no-packet timeout");
assert.equal(value(stateAddress.startupStage), 3);
for (const field of ["rasterTicks", "consumedTicks", "packetsLow", "packetsHigh", "baseReady"])
  assert.equal(value(stateAddress[field]), 0, `${field} advanced with no firmware`);
assert.deepEqual(read("settled-state.bin"), state, "no-service hold must stay stable");
const screen = read("timeout-screen.bin");
assert.deepEqual(read("settled-screen.bin"), screen, "terminal must not reset during hold");
const decode = (bytes) => [...bytes].map((byte) => String.fromCharCode(byte < 32 ? byte + 64 : byte)).join("");
const lines = Array.from({ length: 25 }, (_, row) => decode(screen.subarray(row * 40, row * 40 + 40)).trimEnd());
assert.equal(lines[0], manifest.diagnosticTitle);
assert.equal(lines[2], "STAGE 03   ERROR 02   PACKETS   0000");
assert.equal(lines[5], "NO PACKET - SERVICE OR LINK STALLED");
assert.equal(lines[10], "4D 33 54 50 01 00 00 00");
assert.equal(decode(read("start-screen.bin").subarray(0, 40)).trimEnd(), manifest.diagnosticTitle);
assert.deepEqual(fs.readFileSync(copiedCrt), crt);
const report = {
  crt: options.crt, sha256: digest(crt), standard: options.standard,
  diagnosticTitle: manifest.diagnosticTitle, codeEnd: terminal.codeEnd,
  stageAddress: terminal.stageAddress, bank58ChipRecords: 0,
  copiedPayloadBytes: payload.length, startWritten, startIO2: [...io2],
  startupStage: 3, noServiceError: 2, stableHold: true,
  checks: ["actual C64 reset", "exact ROM-to-RAM copy", "native IO2 START",
    "6510 ROM disabled with IO enabled", "RAM IRQ/NMI vectors", "bounded stable no-service timeout"],
  limit: "C64 CPU and memory-map proof in VICE; no Teensy bus timing or physical acceptance"
};
fs.writeFileSync(file("result.json"), `${JSON.stringify(report, null, 2)}\n`);
fs.writeFileSync(file("screen.txt"), `${lines.join("\n")}\n`);
console.log(`DOS C64 boot passed (${options.standard}): reset -> RAM terminal -> M3TP START -> stable WAIT timeout. ${file("result.json")}`);

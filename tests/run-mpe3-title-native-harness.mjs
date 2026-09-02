import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "..");
const options = {};
for (let index = 2; index < process.argv.length; index++) {
  const name = process.argv[index];
  if (name === "--help") {
    console.log("Usage: node tests/run-mpe3-title-native-harness.mjs --asset SQ1-MPE3-TITLE.bin [--module IOH_MPE3TitlePull.c] [--compiler g++] [--output result.json] [--trace-prefix PATH]");
    process.exit(0);
  }
  if (!["--asset", "--module", "--compiler", "--output", "--trace-prefix"].includes(name) || !process.argv[index + 1]) {
    throw new Error(`Unknown or incomplete option: ${name}`);
  }
  options[name.slice(2)] = process.argv[++index];
}
if (!options.asset) throw new Error("--asset must name the real SQ1 M3T1 title asset");
const asset = path.resolve(options.asset);
assert.ok(fs.statSync(asset).isFile(), "the title asset must be a file");
const patchPath = path.join(root, "engine", "patches", "0031-Add-MPE3-title-pull-service.patch");
const patchBytes = fs.readFileSync(patchPath);
const relativeModule = "Source/Teensy/MinimalBoot/Common/IO_Handlers/IOH_MPE3TitlePull.c";

function moduleFromPatch(patch) {
  const lines = patch.replace(/\r\n/g, "\n").split("\n");
  const start = lines.indexOf(`diff --git a/${relativeModule} b/${relativeModule}`);
  assert.ok(start >= 0, "0031 must add the complete native module");
  let end = lines.findIndex((line, index) => index > start && line.startsWith("diff --git "));
  if (end < 0) end = lines.length;
  const section = lines.slice(start, end);
  assert.ok(section.includes("new file mode 100644"));
  const hunk = section.findIndex((line) => /^@@ -0,0 \+1,\d+ @@/.test(line));
  assert.ok(hunk >= 0, "new-file hunk must contain the complete implementation");
  const declared = Number(section[hunk].match(/\+1,(\d+)/)[1]);
  const code = [];
  for (const line of section.slice(hunk + 1)) {
    if (line.startsWith("+")) code.push(line.slice(1));
    else if (line && !line.startsWith("\\ No newline")) throw new Error("Unexpected non-addition in native module hunk");
  }
  assert.equal(code.length, declared, "native module extraction must not truncate the patch");
  return `${code.join("\n")}\n`;
}
function applyModulePatch(source, patch) {
  const lines = patch.replace(/\r\n/g, "\n").split("\n");
  const start = lines.indexOf(`diff --git a/${relativeModule} b/${relativeModule}`);
  assert.ok(start >= 0, "native intro patch must update the title module");
  let end = lines.findIndex((line, index) => index > start && line.startsWith("diff --git "));
  if (end < 0) end = lines.length;
  const section = lines.slice(start, end);
  const input = source.trimEnd().split("\n"), output = [];
  let cursor = 0;
  for (let index = 0; index < section.length; index++) {
    const header = section[index].match(/^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@/);
    if (!header) continue;
    const oldStart = Number(header[1]) - 1;
    assert.ok(oldStart >= cursor);
    output.push(...input.slice(cursor, oldStart)); cursor = oldStart;
    for (index++; index < section.length && !section[index].startsWith("@@"); index++) {
      const line = section[index];
      if (line.startsWith(" ") || line.startsWith("-")) {
        assert.equal(input[cursor++], line.slice(1), "patch context must exactly match the native module");
      }
      if (line.startsWith(" ") || line.startsWith("+")) output.push(line.slice(1));
      else if (line && !line.startsWith("-") && !line.startsWith("\\ No newline")) throw new Error("Unexpected native patch line");
    }
    index--;
  }
  output.push(...input.slice(cursor));
  return `${output.join("\n")}\n`;
}
const introPatchPath = path.join(root, "engine/patches/0033-Stream-native-intro-and-skip-to-login.patch");
const introPatchBytes = fs.readFileSync(introPatchPath);
const transitionPatchPath = path.join(root, "engine/patches/0034-Publish-complete-frame-display-transitions.patch");
const transitionPatchBytes = fs.readFileSync(transitionPatchPath);
const patchModule = applyModulePatch(applyModulePatch(moduleFromPatch(patchBytes.toString("utf8")),
  introPatchBytes.toString("utf8")), transitionPatchBytes.toString("utf8"));
const moduleSource = options.module ? fs.readFileSync(path.resolve(options.module), "utf8").replace(/\r\n/g, "\n") : patchModule;
assert.equal(moduleSource.trimEnd(), patchModule.trimEnd(), "active clone module must exactly match 0031 plus 0033 plus 0034, ignoring line endings");

const candidates = options.compiler ? [options.compiler] : [
  process.env.CXX,
  ...(process.platform === "win32" ? ["C:\\msys64\\mingw64\\bin\\g++.exe", "C:\\msys64\\ucrt64\\bin\\g++.exe"] : []),
  "g++", "clang++"
].filter(Boolean);
let compiler;
for (const candidate of candidates) {
  if (spawnSync(candidate, ["--version"], { encoding: "utf8", windowsHide: true }).status === 0) {
    compiler = candidate;
    break;
  }
}
if (!compiler) throw new Error("No working native C++ compiler found; pass --compiler with a g++/clang++ path");

const tempParent = path.resolve(process.env.MPE3_NATIVE_TEST_OUTPUT ?? path.join(root, "build", "mpe3-native-harness"));
fs.mkdirSync(tempParent, { recursive: true });
const temp = fs.mkdtempSync(path.join(tempParent, "agi64-mpe3-native-"));
try {
  fs.writeFileSync(path.join(temp, "IOH_MPE3TitlePull.c"), moduleSource);
  const executable = path.join(temp, process.platform === "win32" ? "mpe3-title-native-harness.exe" : "mpe3-title-native-harness");
  const arguments_ = ["-std=c++17", "-O2", "-Wall", "-Wextra"];
  if (process.platform === "win32" && /g\+\+(?:\.exe)?$/i.test(compiler)) {
    arguments_.push("-static", "-static-libgcc", "-static-libstdc++");
  }
  arguments_.push("-I", temp, path.join(here, "mpe3-title-native-harness.cpp"), "-o", executable);
  // MinGW's compiler child processes find their DLLs from this working dir.
  const compilerCwd = path.isAbsolute(compiler) ? path.dirname(compiler) : root;
  execFileSync(compiler, arguments_, { cwd: compilerCwd, stdio: "pipe", windowsHide: true, timeout: 60000 });
  const executableArgs = [asset];
  if (options["trace-prefix"]) {
    const prefix = path.resolve(options["trace-prefix"]);
    fs.mkdirSync(path.dirname(prefix), { recursive: true });
    executableArgs.push(prefix);
  }
  const output = execFileSync(executable, executableArgs, { cwd: temp, encoding: "utf8", windowsHide: true, timeout: 60000 });
  const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
  const traces = options["trace-prefix"] ? Object.fromEntries([
    ["normal", { suffix: "normal", skipBeforeAckOrdinal: null }],
    ["skipBeforeLoad", { suffix: "skip-before-load", skipBeforeAckOrdinal: -1 }],
    ["skipCell", { suffix: "skip-cell", skipBeforeAckOrdinal: 0 }],
    ["skipSecondCell", { suffix: "skip-second-cell", skipBeforeAckOrdinal: 1 }],
    ["skipSid", { suffix: "skip-sid", skipBeforeAckOrdinal: "firstSid" }]
  ].map(([name, detail]) => {
    const file = `${path.resolve(options["trace-prefix"])}-${detail.suffix}.bin`;
    const bytes = fs.readFileSync(file);
    let cursor = 0, packets = 0, firstSid = null;
    while (cursor < bytes.length) {
      assert.ok(cursor + 2 <= bytes.length); const length = bytes.readUInt16LE(cursor); cursor += 2;
      assert.ok(length >= 10 && length <= 238 && cursor + length <= bytes.length);
      if (bytes[cursor + 3] === 2 && firstSid === null) firstSid = packets;
      cursor += length; packets++;
    }
    return [name, { file, bytes: bytes.length, packets,
      skipBeforeAckOrdinal: detail.skipBeforeAckOrdinal === "firstSid" ? firstSid : detail.skipBeforeAckOrdinal,
      framing: "u16le packetBytes followed by exact M3 packet header payload and CRC16", sha256: hash(bytes) }];
  })) : undefined;
  const result = {
    ...JSON.parse(output.trim()),
    compiler,
    nativeModuleFrom: options.module ? path.resolve(options.module) : [patchPath, introPatchPath, transitionPatchPath],
    nativeModuleSha256: hash(moduleSource),
    patchSha256: hash(patchBytes),
    introPatchSha256: hash(introPatchBytes),
    transitionPatchSha256: hash(transitionPatchBytes),
    assetSha256: hash(fs.readFileSync(asset)),
    ...(traces ? { traces } : {})
  };
  if (options.output) {
    const target = path.resolve(options.output);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, `${JSON.stringify(result, null, 2)}\n`);
  }
  console.log(JSON.stringify(result, null, 2));
} finally {
  const resolved = path.resolve(temp);
  if (path.dirname(resolved) !== tempParent || !path.basename(resolved).startsWith("agi64-mpe3-native-")) {
    throw new Error("Refusing cleanup outside the exact native-test temporary directory");
  }
  fs.rmSync(resolved, { recursive: true, force: true });
}

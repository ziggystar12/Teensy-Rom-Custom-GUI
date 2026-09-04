#!/usr/bin/env node
/** Build the generic C64 MPE3 terminal and EasyFlash boot bank for DOSVM. */

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { loadDosTerminal } from './dos_terminal.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDirectory, "..", "..");
const defaultAgi64Root = path.resolve(projectRoot, "..", "AGI-64");
const DOSVM_DIAGNOSTIC_TITLE = "MHS POWER ENGINE - DOSVM R16 DIAG";
const DOSVM_DIAGNOSTIC_FOOTER = "R16 - FAST DIRECT RAM";
const DOSVM_LOADING_TEXT = "MHS DOSVM LOADING";

function readOption(name, fallback = null) {
  const index = process.argv.indexOf(name);
  if (index < 0) return fallback;
  const value = process.argv[index + 1];
  if (!value || value.startsWith("--")) throw new Error(`${name} requires a path`);
  return value;
}

function writeOutput(target, data) {
  const output = path.resolve(target);
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, data);
  return output;
}

function projectRelative(target) {
  return path.relative(projectRoot, target).split(path.sep).join("/");
}

const agi64Root = path.resolve(readOption("--agi64-root", defaultAgi64Root));
const outputPrg = readOption("--output-prg");
const outputBootBank = readOption("--output-boot-bank");
const outputManifest = readOption("--manifest");
if (!outputPrg || !outputBootBank || !outputManifest) {
  throw new Error("Usage: node dos/tools/build_dosvm_terminal.mjs --output-prg <file> --output-boot-bank <file> --manifest <file> [--agi64-root <path>]");
}

const terminalModulePath = path.join(agi64Root, "host", "mpe3-title-terminal.mjs");
const bootModulePath = path.join(agi64Root, "host", "install-boot-bank.mjs");
for (const sourcePath of [terminalModulePath, bootModulePath]) {
  if (!fs.statSync(sourcePath, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`AGI-64 terminal source is missing: ${sourcePath}`);
  }
}

const { buildMpe3TitleTerminal } = await loadDosTerminal(agi64Root);
const { buildCartridgeBootBank } = await import(pathToFileURL(bootModulePath).href);
const terminal = buildMpe3TitleTerminal({
  gameplay: true,
  enable1351Mouse: false,
  diagnosticTitle: DOSVM_DIAGNOSTIC_TITLE,
  diagnosticFooter: DOSVM_DIAGNOSTIC_FOOTER
});
const bootBank = buildCartridgeBootBank(terminal.prg, {
  loadingText: DOSVM_LOADING_TEXT,
  cartridgeFormat: "easyflash-1m"
});
if (bootBank.length !== 0x4000) {
  throw new Error(`DOSVM boot bank must be 16384 bytes, got ${bootBank.length}`);
}

const prgPath = writeOutput(outputPrg, terminal.prg);
const bootBankPath = writeOutput(outputBootBank, bootBank);
const digest = (data) => crypto.createHash("sha256").update(data).digest("hex");
const manifest = {
  format: "M3TP-DOSVM-terminal",
  diagnosticTitle: DOSVM_DIAGNOSTIC_TITLE,
  diagnosticFooter: DOSVM_DIAGNOSTIC_FOOTER,
  loadingText: DOSVM_LOADING_TEXT,
  gameplay: true,
  enable1351Mouse: false,
  dosSidPayloadBytes: 27,
  dosInputProtocol: 'held-scan-v1',
  dosTerminalOverlaySha256: digest(fs.readFileSync(path.join(scriptDirectory, 'dos_terminal.mjs'))),
  terminalPrg: projectRelative(prgPath),
  terminalPrgSha256: digest(terminal.prg),
  terminalPrgBytes: terminal.prg.length,
  stageAddress: terminal.stageAddress,
  codeEnd: terminal.codeEnd,
  labels: terminal.labels,
  bootBank: projectRelative(bootBankPath),
  bootBankSha256: digest(bootBank),
  bootBankBytes: bootBank.length,
  agi64TerminalSource: projectRelative(terminalModulePath),
  agi64TerminalSourceSha256: digest(fs.readFileSync(terminalModulePath)),
  agi64KeyboardSourceSha256: digest(fs.readFileSync(path.join(agi64Root, 'host/mpe4-keyboard.mjs'))),
  agi64BootBankSource: projectRelative(bootModulePath),
  agi64BootBankSourceSha256: digest(fs.readFileSync(bootModulePath))
};
writeOutput(outputManifest, `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`Built DOSVM terminal ${prgPath}`);
console.log(`Built DOSVM boot bank ${bootBankPath}`);

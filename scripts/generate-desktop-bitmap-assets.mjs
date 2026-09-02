#!/usr/bin/env node
// Reuse the approved mock's original raster operations; never redraw its art.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const scriptPath = fileURLToPath(import.meta.url);
const repositoryRoot = resolve(dirname(scriptPath), '..');
export const mockPath = resolve(repositoryRoot, 'docs/mockup/index.html');
export const outputPath = resolve(
  repositoryRoot,
  'Source/C64/MainMenuCRT/source/GeosRichAssets.s',
);
const legacyIconPath = resolve(repositoryRoot, 'Source/C64/MainMenuCRT/source/GeosDesktop.s');

const iconDefinitions = [
  ['teensy', 'Teensy', 'drawChip'],
  ['sd', 'SD', 'drawSd'],
  ['usb', 'USB', 'drawUsb'],
  ['drive8', 'Drive8', 'drawDrive', '8'],
  ['drive9', 'Drive9', 'drawDrive', '9'],
  ['games', 'Games', 'drawFolder'],
  ['utilities', 'Utilities', 'drawFolder'],
  ['control', 'Control', 'drawControl'],
  ['trash', 'Trash', 'drawTrash'],
];

// The mock uses only integer, opaque black/white fillRect operations. Failing
// loudly on another canvas operation prevents silently approximate exports.
function createRaster() {
  let width = 0;
  let height = 0;
  let pixels = new Uint8Array();
  const context = {
    fillStyle: '#000',
    imageSmoothingEnabled: false,
    fillRect(x, y, w, h) {
      assert.ok([x, y, w, h].every(Number.isInteger), 'non-integer rectangle');
      assert.ok(w >= 0 && h >= 0, 'negative rectangle dimensions');
      assert.ok(x >= 0 && y >= 0 && x + w <= width && y + h <= height,
        `rectangle outside ${width}x${height}: ${x},${y},${w},${h}`);
      assert.ok(this.fillStyle === '#000' || this.fillStyle === '#fff',
        `unsupported raster color: ${this.fillStyle}`);
      const value = this.fillStyle === '#000' ? 1 : 0;
      for (let row = y; row < y + h; row++) {
        pixels.fill(value, row * width + x, row * width + x + w);
      }
    },
  };
  return {
    context,
    reset(w, h) {
      width = w;
      height = h;
      pixels = new Uint8Array(w * h); // White background; 1 means black ink.
    },
    pack() {
      assert.equal(width % 8, 0, 'packed width must be byte-aligned');
      const bytes = new Uint8Array(width * height / 8);
      for (let index = 0; index < pixels.length; index++) {
        bytes[index >> 3] |= pixels[index] << (7 - (index & 7));
      }
      for (let index = 0; index < pixels.length; index++) {
        assert.equal((bytes[index >> 3] >> (7 - (index & 7))) & 1, pixels[index]);
      }
      return bytes;
    },
  };
}

function loadMock(mockHtml, raster) {
  const scripts = [...mockHtml.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)];
  assert.equal(scripts.length, 1, 'expected one original mock script');
  const originalScript = scripts[0][1].replaceAll('\r\n', '\n');
  const tail = /  setInterval\(draw, 30000\);\n  draw\(\);\n\}\)\(\);\s*$/;
  assert.match(originalScript, tail, 'mock startup changed; review extraction');
  const instrumented = originalScript.replace(tail, `  globalThis.assets = {
    glyph, icons, pixelText, drawChip, drawSd, drawUsb, drawDrive,
    drawFolder, drawControl, drawTrash
  };
})();`);
  const canvas = {
    getContext(kind) {
      assert.equal(kind, '2d');
      return raster.context;
    },
    addEventListener() {},
  };
  const sandbox = {
    document: {
      getElementById(id) {
        if (id === 'teensyrom-screen') return canvas;
        assert.equal(id, 'teensyrom-desktop-preview');
        return {};
      },
    },
    window: { addEventListener() {} },
  };
  vm.runInNewContext(instrumented, sandbox, {
    filename: 'docs/mockup/index.html',
    timeout: 1000,
    contextCodeGeneration: { strings: false, wasm: false },
  });
  assert.ok(sandbox.assets, 'mock functions were not exported');
  return { ...sandbox.assets, originalScript };
}

function hex(value) {
  return `$${value.toString(16).padStart(2, '0')}`;
}

function toTiles(rowMajorBytes) {
  const tiles = new Uint8Array(48);
  for (let tileY = 0; tileY < 2; tileY++) {
    for (let tileX = 0; tileX < 3; tileX++) {
      for (let row = 0; row < 8; row++) {
        tiles[(tileY * 3 + tileX) * 8 + row] = rowMajorBytes[(tileY * 8 + row) * 3 + tileX];
      }
    }
  }
  return tiles;
}

function legacyDocumentAndProgram(legacySource) {
  const match = legacySource.match(/^GeosIconData:\s*([\s\S]*?)^GeosIconDataEnd:/m);
  assert.ok(match, 'existing browser icon block is missing');
  const bytes = [];
  for (const line of match[1].split(/\r?\n/)) {
    const directive = line.match(/^\s*!byte\s+([^;]+)/);
    if (!directive) continue;
    for (const token of directive[1].trim().split(/\s*,\s*/)) {
      assert.match(token, /^(?:%[01]{8}|\$[\da-fA-F]{2}|\d{1,3})$/,
        'browser source icons must be literal bytes');
      const value = token.startsWith('%') ? Number.parseInt(token.slice(1), 2)
        : token.startsWith('$') ? Number.parseInt(token.slice(1), 16) : Number(token);
      assert.ok(value >= 0 && value <= 255);
      bytes.push(value);
    }
  }
  assert.equal(bytes.length, 192, 'expected the four existing 24x16 browser icons');
  return Uint8Array.from(bytes.slice(96));
}

export function buildDesktopBitmapAssets(
  mockHtml = readFileSync(mockPath, 'utf8'),
  legacySource = readFileSync(legacyIconPath, 'utf8'),
) {
  const raster = createRaster();
  const mock = loadMock(mockHtml, raster);
  const font = new Uint8Array(96 * 8);
  for (let code = 0x20; code <= 0x7f; code++) {
    const character = String.fromCharCode(code);
    const rows = mock.glyph[character.toUpperCase()] || mock.glyph['?'];
    assert.equal(rows.length, 7);
    for (let row = 0; row < 7; row++) {
      assert.match(rows[row], /^[01]{5}$/);
      font[(code - 0x20) * 8 + row] = Number.parseInt(rows[row], 2) << 3;
    }
    // Independently render every ASCII character using the original pixelText
    // routine and compare all pixels, including the blank spacing and row 8.
    raster.reset(8, 8);
    mock.pixelText(character, 0, 0);
    assert.deepEqual(raster.pack(), font.slice((code - 0x20) * 8, (code - 0x1f) * 8));
  }

  assert.deepEqual(Array.from(mock.icons, icon => icon.id),
    iconDefinitions.map(([id]) => id), 'approved desktop icon order changed');
  const icons = iconDefinitions.map(([id, label, drawFunction, argument]) => {
    raster.reset(24, 16);
    mock[drawFunction](0, 0, argument);
    const bytes = raster.pack();
    assert.equal(bytes.length, 48);
    return { id, label, bytes };
  });
  raster.reset(24, 16);
  mock.drawDrive(0, 0, '');
  const legacy = legacyDocumentAndProgram(legacySource);
  const browserIcons = [
    { label: 'Folder', bytes: toTiles(icons[5].bytes) },
    { label: 'Disk', bytes: toTiles(raster.pack()) },
    { label: 'Document', bytes: legacy.slice(0, 48) },
    { label: 'Program', bytes: legacy.slice(48) },
  ];

  const digest = createHash('sha256').update(mock.originalScript).digest('hex');
  const source = [
    '; Generated by node scripts/generate-desktop-bitmap-assets.mjs. Do not edit.',
    '; Source: docs/mockup/index.html (original approved glyphs/drawing functions).',
    `; Mock script SHA-256: ${digest}`,
    '; 1 = black ink; 0 = white. All rows are MSB first, left to right.',
    '; Font is ASCII $20..$7f: five high bits, seven rows plus a blank eighth.',
    '; Lowercase uses uppercase art; unsupported characters use the mock question mark.',
    '; Icons are row-major: three consecutive bytes per row, sixteen rows each.',
    '; These are source rasters, not VIC-II cell-interleaved bitmap memory.',
    '',
    'GeosRichFontFirst = $20',
    'GeosRichFontCount = 96',
    'GeosRichFontWidth = 5',
    'GeosRichFontHeight = 7',
    'GeosRichFontAdvance = 6',
    'GeosRichFontStride = 8',
    'GeosRichIconWidth = 24',
    'GeosRichIconHeight = 16',
    'GeosRichIconRowBytes = 3',
    'GeosRichIconStride = 48',
    'GeosRichIconCount = 9',
    'GeosRichBrowserIconCount = 4',
    'GeosRichBrowserIconStride = 48',
    '',
    'GeosRichFont:',
  ];
  for (let code = 0x20; code <= 0x7f; code++) {
    const offset = (code - 0x20) * 8;
    const character = code === 0x7f ? 'DEL (question mark)' : JSON.stringify(String.fromCharCode(code));
    source.push(`   !byte ${Array.from(font.slice(offset, offset + 8), hex).join(',')} ; ${hex(code)} ${character}`);
  }
  source.push('GeosRichFontEnd:', '', 'GeosRichIcons:');
  for (const icon of icons) {
    source.push(`GeosRichIcon${icon.label}:`);
    for (let row = 0; row < 16; row++) {
      source.push(`   !byte ${Array.from(icon.bytes.slice(row * 3, row * 3 + 3), hex).join(',')}`);
    }
  }
  source.push('GeosRichIconsEnd:', '',
    '; Browser icons use 3x2 TILE ORDER: three eight-byte glyphs across the top,',
    '; then three across the bottom. Copy directly to the browser character font.',
    '; Folder and unnumbered disk come from the original mock; document/program',
    '; are preserved byte-for-byte from GeosDesktop.s GeosIconData (last two icons).',
    'GeosRichBrowserIconData:');
  for (const icon of browserIcons) {
    source.push(`GeosRichBrowserIcon${icon.label}:`);
    for (let tile = 0; tile < 6; tile++) {
      source.push(`   !byte ${Array.from(icon.bytes.slice(tile * 8, tile * 8 + 8), hex).join(',')}`);
    }
  }
  source.push('GeosRichBrowserIconDataEnd:', '');
  return { font, icons, browserIcons, source: source.join('\n') };
}

if (process.argv[1] && resolve(process.argv[1]) === scriptPath) {
  const arguments_ = process.argv.slice(2);
  assert.ok(arguments_.length === 0 || (arguments_.length === 1 && arguments_[0] === '--check'),
    'usage: node scripts/generate-desktop-bitmap-assets.mjs [--check]');
  const { font, icons, browserIcons, source } = buildDesktopBitmapAssets();
  if (arguments_[0] === '--check') {
    assert.equal(readFileSync(outputPath, 'utf8').replaceAll('\r\n', '\n'), source,
      'GeosRichAssets.s is stale; regenerate it from the original mock');
    console.log('Desktop bitmap assets match the original mock.');
  } else {
    writeFileSync(outputPath, source);
    console.log(`Generated ${outputPath}`);
  }
  console.log(`${font.length} font bytes + ${icons.length * 48} desktop icon bytes + ${browserIcons.length * 48} browser icon bytes = ${font.length + (icons.length + browserIcons.length) * 48} bytes.`);
}

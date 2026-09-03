// DOS-only extension of the shared terminal. Keep the AGI checkout untouched:
// existing Sierra builds retain their byte-identical 26-byte SID protocol.
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

export async function loadDosTerminal(agiRoot) {
  const filename = path.join(agiRoot, 'host/mpe3-title-terminal.mjs');
  let source = fs.readFileSync(filename, 'utf8');
  function replaceOnce(before, after) {
    const index = source.indexOf(before);
    if (index < 0 || source.indexOf(before, index + before.length) >= 0)
      throw new Error('Shared terminal changed: review the DOS background extension');
    source = source.slice(0, index) + after + source.slice(index + before.length);
  }
  // A DOS frame carries the existing SID bytes plus its global VIC colour.
  // The CRC and normal packet validation protect the extra byte as usual.
  replaceOnce("    e.abs(0x20, 'game_ego_validate_sid');",
    '    e.abs(0xad, stage + 6, "read");\n' +
    '    e.emit(0xc9, 27);\n' +
    "    e.branch(0xf0, 'dos_sid_video_valid');\n" +
    "    e.abs(0x20, 'game_ego_validate_sid');\n" +
    "    e.label('dos_sid_video_valid');");
  replaceOnce("    e.emit(0xc9, MPE3_TITLE_PULL.spriteSidPayloadBytes);",
    "    e.emit(0xc9, 27);\n" +
    "    e.branch(0xf0, 'apply_sid_length_ok');\n" +
    "    e.emit(0xc9, MPE3_TITLE_PULL.spriteSidPayloadBytes);");
  replaceOnce('  e.label("sid_copy");',
    '  e.label("sid_copy");\n' +
    '  e.abs(0xad, stage + 6, "read");\n' +
    '  e.emit(0xc9, 27);\n' +
    '  e.branch(0xd0, "dos_background_black");\n' +
    '  e.abs(0xad, stage + 34, "read");\n' +
    '  e.emit(0x29, 15);\n' +
    '  e.abs(0x4c, "dos_background_store");\n' +
    '  e.label("dos_background_black");\n' +
    '  e.emit(0xa9, 0);\n' +
    '  e.label("dos_background_store");\n' +
    '  e.abs(0x8d, 0xd021, "write");');
  // data: modules require absolute imports. Dependencies are the original
  // shared keyboard/sprite generators, including their own relative imports.
  source = source.replace(/from '(\.\/[^']+)'/g,
    (_, relative) => `from '${pathToFileURL(path.resolve(path.dirname(filename), relative)).href}'`);
  return import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
}

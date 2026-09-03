// DOS-only extension of the shared terminal. Keep the AGI checkout untouched:
// existing Sierra builds retain their byte-identical 26-byte SID protocol.
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

// DOS uses held PC keys, unlike AGI's press-to-toggle directions. Keep the
// six-register envelope, with flags bit 7 identifying a complete held-state
// snapshot. Bits 0..2 are Shift/Ctrl/Alt and bit 3 requests typematic repeat.
// The firmware owns PC make/break events; no cursor is encoded as ANSI Escape.
export function emitDosKeyboard(e, s, keys, shifted, scans, rasterTicks) {
  const get = a => e.abs(0xad, a, 'read'), put = a => e.abs(0x8d, a, 'write');
  const set = (a, v) => { e.emit(0xa9, v); put(a); };
  const jump = n => e.abs(0x4c, n);
  e.label('game_input_init');
  for (const a of [s.armed, s.lastKey, s.lastModifiers, s.lastJoy, s.sequence, s.pending]) set(a, 0);
  set(s.active, 1); e.emit(0x60);

  e.label('sample_game_input');
  get(s.pending); e.branch(0xf0, 'dos_input_scan');
  get(s.ack); e.abs(0xcd, s.sequence, 'read'); e.branch(0xf0, 'dos_input_accepted');
  jump('game_input_send');
  e.label('dos_input_accepted'); set(s.pending, 0);
  e.label('dos_input_scan');
  // Read port 2 with both CIA ports floating, then mask any grounded keyboard
  // columns while scanning. A joystick direction must not invent a key.
  set(0xdc02, 0); set(0xdc03, 0); get(0xdc00); e.emit(0x49, 255, 0x29, 31); put(s.currentJoy);
  get(0xdc01); put(s.baseline); set(0xdc02, 255); set(s.mask, 254); e.emit(0xa2, 0);
  e.label('dos_matrix_row'); get(s.mask); put(0xdc00); get(0xdc01); e.emit(0x49, 255);
  e.abs(0x2d, s.baseline, 'read'); e.abs(0x9d, s.matrix, 'write');
  e.abs(0x0e, s.mask, 'write'); e.abs(0xee, s.mask, 'write');
  e.emit(0xe8, 0xe0, 8); e.branch(0xd0, 'dos_matrix_row');
  set(0xdc00, 0x40); set(0xdc02, 0xc0);
  set(s.modifiers, 0);
  get(s.matrix + 1); e.emit(0x29, 128); e.branch(0xd0, 'dos_shift_on');
  get(s.matrix + 6); e.emit(0x29, 16); e.branch(0xf0, 'dos_shift_done');
  e.label('dos_shift_on'); set(s.modifiers, 1);
  e.label('dos_shift_done'); get(s.matrix + 7); e.emit(0x29, 4); e.branch(0xf0, 'dos_control_done');
  get(s.modifiers); e.emit(0x09, 2); put(s.modifiers);
  e.label('dos_control_done'); get(s.matrix + 7); e.emit(0x29, 32); e.branch(0xf0, 'dos_alt_done');
  get(s.modifiers); e.emit(0x09, 4); put(s.modifiers);
  e.label('dos_alt_done'); set(s.candidate, 0); e.emit(0xa2, 0);
  e.label('dos_find_row'); e.abs(0xbd, s.matrix, 'read'); e.abs(0x3d, 'dos_modifier_mask', 'read'); put(s.bits);
  e.emit(0xa0, 8);
  e.label('dos_find_bit'); e.abs(0x4e, s.bits, 'write'); e.branch(0xb0, 'dos_key_found');
  e.abs(0xee, s.candidate, 'write'); e.emit(0x88); e.branch(0xd0, 'dos_find_bit');
  e.emit(0xe8, 0xe0, 8); e.branch(0xd0, 'dos_find_row'); set(s.candidate, 255);
  e.label('dos_key_found'); get(s.armed); e.branch(0xd0, 'dos_input_armed');
  get(s.candidate); e.emit(0xc9, 255); e.branch(0xd0, 'dos_input_unarmed');
  get(s.currentJoy); e.branch(0xd0, 'dos_input_unarmed'); set(s.armed, 1);
  e.label('dos_input_unarmed'); e.emit(0x60);
  e.label('dos_input_armed'); set(s.key, 0); set(s.scan, 0); get(s.currentJoy); put(s.joy);
  get(s.candidate); e.emit(0xc9, 255); e.jumpUnless(0xd0, 'dos_mapping_done');
  e.emit(0xaa); e.abs(0xbd, 'dos_scan_table', 'read'); put(s.scan);
  get(s.modifiers); e.emit(0x29, 1); e.branch(0xf0, 'dos_unshifted');
  e.abs(0xbd, 'dos_shift_table', 'read'); put(s.key);
  e.emit(0xe0, 2); e.branch(0xd0, 'dos_shift_not_right'); set(s.scan, 75); jump('dos_cursor_shift');
  e.label('dos_shift_not_right'); e.emit(0xe0, 7); e.branch(0xd0, 'dos_shift_not_down'); set(s.scan, 72);
  e.label('dos_cursor_shift');
  // C64 Shift selects the missing Up/Left keys; it is not PC Shift+keypad.
  get(s.modifiers); e.emit(0x29, 0xfe); put(s.modifiers); jump('dos_key_modifiers');
  e.label('dos_shift_not_down'); e.emit(0xe0, 3); e.branch(0x90, 'dos_key_modifiers');
  e.emit(0xe0, 7); e.branch(0xb0, 'dos_key_modifiers'); e.abs(0xee, s.scan, 'write');
  jump('dos_cursor_shift'); // Shift selects C64 F2/F4/F6/F8, too.
  e.label('dos_unshifted'); e.abs(0xbd, 'dos_key_table', 'read'); put(s.key);
  e.label('dos_key_modifiers');
  get(s.key); e.emit(0xc9, 128); e.branch(0x90, 'dos_key_ascii'); set(s.key, 0);
  e.label('dos_key_ascii'); get(s.modifiers); e.emit(0x29, 2); e.branch(0xf0, 'dos_no_control');
  get(s.key); e.emit(0x09, 32, 0xc9, 97); e.branch(0x90, 'dos_no_control');
  e.emit(0xc9, 123); e.branch(0xb0, 'dos_no_control'); e.emit(0x29, 31); put(s.key);
  e.label('dos_no_control'); get(s.modifiers); e.emit(0x29, 4); e.branch(0xf0, 'dos_mapping_done'); set(s.key, 0);
  e.label('dos_mapping_done'); get(s.modifiers); e.emit(0x09, 0x80); put(s.flags);
  get(s.scan); e.abs(0xcd, s.lastKey, 'read'); e.branch(0xd0, 'dos_new_state');
  get(s.modifiers); e.abs(0xcd, s.lastModifiers, 'read'); e.branch(0xd0, 'dos_new_state');
  get(s.joy); e.abs(0xcd, s.lastJoy, 'read'); e.branch(0xd0, 'dos_new_state');
  // Only printable keys and Backspace repeat. Held cursor and joystick state
  // already remains down in the guest, so it never needs repeated make edges.
  get(s.modifiers); e.emit(0x29, 6); e.branch(0xd0, 'dos_input_return');
  get(s.key); e.emit(0xc9, 8); e.branch(0xf0, 'dos_repeatable');
  e.emit(0xc9, 32); e.branch(0x90, 'dos_input_return'); e.emit(0xc9, 127); e.branch(0xb0, 'dos_input_return');
  e.label('dos_repeatable'); get(rasterTicks); e.emit(0x38); e.abs(0xed, s.repeatTick, 'read');
  e.abs(0xcd, s.repeatDelay, 'read'); e.branch(0x90, 'dos_input_return');
  set(s.repeatDelay, 4); get(s.flags); e.emit(0x09, 8); put(s.flags); jump('dos_queue_state');
  e.label('dos_input_return'); e.emit(0x60);
  e.label('dos_new_state');
  get(s.scan); put(s.lastKey); get(s.modifiers); put(s.lastModifiers); get(s.joy); put(s.lastJoy);
  set(s.repeatDelay, 20);
  e.label('dos_queue_state'); get(rasterTicks); put(s.repeatTick);
  e.abs(0xee, s.sequence, 'write'); get(s.sequence); e.branch(0xd0, 'dos_sequence_ready'); e.abs(0xee, s.sequence, 'write');
  e.label('dos_sequence_ready'); set(s.pending, 1);
  e.label('game_input_send');
  for (const [from, to] of [[s.key, s.keyRegister], [s.scan, s.scanRegister], [s.joy, s.joyRegister],
      [s.flags, s.flagsRegister], [s.sequence, s.sequenceRegister]]) { get(from); put(to); }
  e.emit(0xa9, 0xa5);
  for (const a of [s.key, s.scan, s.joy, s.flags, s.sequence]) e.abs(0x4d, a, 'read');
  put(s.checksumRegister); set(0xdff4, s.command); e.emit(0x60);
  e.label('dos_modifier_mask'); e.emit(255, 127, 255, 255, 255, 255, 239, 219);
  e.label('dos_key_table'); e.emit(...keys);
  e.label('dos_shift_table'); e.emit(...shifted);
  e.label('dos_scan_table'); e.emit(...scans);
}

export async function loadDosTerminal(agiRoot) {
  const filename = path.join(agiRoot, 'host/mpe3-title-terminal.mjs');
  let source = fs.readFileSync(filename, 'utf8');
  function replaceOnce(before, after) {
    const index = source.indexOf(before);
    if (index < 0 || source.indexOf(before, index + before.length) >= 0)
      throw new Error('Shared terminal changed: review the DOS background extension');
    source = source.slice(0, index) + after + source.slice(index + before.length);
  }
  replaceOnce("import { emitMpe4Keyboard, MPE4_INPUT } from './mpe4-keyboard.mjs';",
    "import { MPE4_INPUT, MPE4_KEYS, MPE4_SHIFT_KEYS, MPE4_SCANS } from './mpe4-keyboard.mjs';\n" +
    `import { emitDosKeyboard } from '${import.meta.url}';`);
  replaceOnce('if (gameplay) emitMpe4Keyboard(e, state.rasterTicks, { enable1351Mouse });',
    'if (gameplay) emitDosKeyboard(e, MPE4_INPUT, MPE4_KEYS, MPE4_SHIFT_KEYS, MPE4_SCANS, state.rasterTicks);');
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

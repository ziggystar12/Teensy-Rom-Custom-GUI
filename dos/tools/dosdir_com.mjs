// Minimal 8086 DOS TSR. File-system work runs through the native redirector;
// DOS owns path parsing, handles, and its normal INT2F network dispatch.
export function buildDosdirCom() {
  const bytes = [], labels = new Map(), fixups = [];
  const emit = (...values) => bytes.push(...values);
  const label = name => labels.set(name, 0x100 + bytes.length);
  const word = value => emit(value & 255, value >>> 8 & 255);
  const address = name => { fixups.push({offset:bytes.length,name}); word(0); };
  const near = (opcode,name) => { emit(opcode); fixups.push({offset:bytes.length,name,relative:2}); word(0); };
  const branch = (opcode,name) => { emit(opcode); fixups.push({offset:bytes.length,name,relative:1}); emit(0); };
  near(0xe9,'install');
  label('handler');
  emit(0x0f,0x04,0xea); // host dispatch; unhandled calls chain unchanged
  label('old_offset'); word(0);
  label('old_segment'); word(0);
  emit(0xcf); // handled calls skip the five-byte far jump and IRET
  label('resident_end');

  label('install');
  emit(0x0e,0x1f); // push cs / pop ds
  emit(0xb8); word(0x352f); emit(0xcd,0x21); // old INT2F vector ES:BX
  emit(0x89,0x1e); address('old_offset');
  emit(0x8c,0xc0,0xa3); address('old_segment'); // mov ax,es / mov [oldseg],ax
  emit(0xb8); word(0x5d06); emit(0xcd,0x21); // DS:SI DOS SDA
  emit(0x1e,0x56,0xb4,0x52,0xcd,0x21,0x5e,0x1f); // ES:BX LOL, preserve SDA
  emit(0xb8); word(3); emit(0x0f,0x05); // install drive D (zero based 3)
  branch(0x72,'failed');
  emit(0x0e,0x1f,0xba); address('handler');
  emit(0xb8); word(0x252f); emit(0xcd,0x21); // set INT2F handler
  emit(0xba); address('ready'); emit(0xb4,0x09,0xcd,0x21);
  // Release the inherited environment before retaining only PSP+hook.
  emit(0xa1); word(0x2c); emit(0x8e,0xc0,0xb4,0x49,0xcd,0x21);
  emit(0xba); word(Math.ceil(labels.get('resident_end') / 16));
  emit(0xb8); word(0x3100); emit(0xcd,0x21);
  label('failed');
  emit(0x0e,0x1f,0xba); address('unavailable'); emit(0xb4,0x09,0xcd,0x21);
  emit(0xb8); word(0x4c01); emit(0xcd,0x21);
  label('ready'); emit(...Buffer.from('D: SD folder ready.\r\n$','ascii'));
  label('unavailable'); emit(...Buffer.from('D: SD folder unavailable.\r\n$','ascii'));
  for (const fixup of fixups) {
    if (!labels.has(fixup.name)) throw new Error(`Unknown label ${fixup.name}`);
    const value = labels.get(fixup.name) - (fixup.relative ? 0x100 + fixup.offset + fixup.relative : 0);
    if (fixup.relative === 1 && (value < -128 || value > 127)) throw new Error('Short branch overflow');
    bytes[fixup.offset] = value & 255;
    if (fixup.relative !== 1) bytes[fixup.offset+1] = value >>> 8 & 255;
  }
  return Buffer.from(bytes);
}

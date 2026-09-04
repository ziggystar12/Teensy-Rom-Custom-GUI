import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

// One complete held-state transaction. The field positions reuse the proven
// bank-58 command-3 envelope, but the payload is explicitly NES-INPUT-V1:
// F8 buttons, F9 display bits, FA overflow count, FD protocol, FE sequence,
// FF XOR checksum; FC is the firmware acknowledgement.
export const NES_INPUT=Object.freeze({
  command:3,active:0x02c0,pending:0x02c8,ack:0xdffc,
  buttonsRegister:0xdff8,displayRegister:0xdff9,overflowRegister:0xdffa,
  protocolRegister:0xdffd,sequenceRegister:0xdffe,checksumRegister:0xdfff,
  protocol:0x81,sharp:1
});

// Generate the complete emitter with fixed low-RAM matrix addresses. Keeping
// the matrix outside emitted code also lets VICE inspect it directly.
export function emitNesController(e,p=NES_INPUT) {
  const matrix=0x02d8;
  const get=a=>e.abs(0xad,a,'read'),put=a=>e.abs(0x8d,a,'write');
  const set=(a,v)=>{e.emit(0xa9,v);put(a);};
  const jump=n=>e.abs(0x4c,n);
  const add=bit=>{get('nes_candidate');e.emit(0x09,bit);put('nes_candidate');};
  e.label('game_input_init');set(p.active,0);set(p.pending,0);set('nes_sequence',0);
  set('nes_queue_head',0);set('nes_queue_tail',0);set('nes_overflows',0);
  set('nes_last_buttons',0xff);set('nes_last_display',0xff);set('nes_sharp',1);set('nes_sharp_held',0);set(p.active,1);e.emit(0x60);
  e.label('sample_game_input');get(p.pending);e.branch(0xf0,'nes_input_dequeue');
  get(p.ack);e.abs(0xcd,'nes_sequence','read');e.branch(0xf0,'nes_input_accepted');jump('nes_input_send');
  e.label('nes_input_accepted');set(p.pending,0);e.label('nes_input_dequeue');
  get('nes_queue_head');e.abs(0xcd,'nes_queue_tail','read');e.branch(0xd0,'nes_input_available');e.emit(0x60);
  e.label('nes_input_available');e.emit(0xaa);e.abs(0xbd,'nes_queue_buttons','read');put('nes_send_buttons');
  e.abs(0xbd,'nes_queue_display','read');put('nes_send_display');e.emit(0xe8,0x8a,0x29,31);put('nes_queue_head');
  e.abs(0xee,'nes_sequence','write');get('nes_sequence');e.branch(0xd0,'nes_sequence_ready');e.abs(0xee,'nes_sequence','write');
  e.label('nes_sequence_ready');set(p.pending,1);e.label('nes_input_send');
  get('nes_send_buttons');put(p.buttonsRegister);get('nes_send_display');put(p.displayRegister);
  get('nes_overflows');put(p.overflowRegister);set(p.protocolRegister,p.protocol);get('nes_sequence');put(p.sequenceRegister);
  e.emit(0xa9,0xa5);for(const a of ['nes_send_buttons','nes_send_display','nes_overflows'])e.abs(0x4d,a,'read');
  e.emit(0x49,p.protocol);e.abs(0x4d,'nes_sequence','read');put(p.checksumRegister);set(0xdff4,p.command);e.emit(0x60);

  e.label('nes_capture_input');get(p.active);e.branch(0xd0,'nes_capture_scan');e.emit(0x60);
  e.label('nes_capture_scan');set(0xdc02,0);set(0xdc03,0);get(0xdc00);e.emit(0x49,255,0x29,31);put('nes_joy');
  get(0xdc01);put('nes_baseline');set(0xdc02,255);set('nes_mask',254);e.emit(0xa2,0);
  e.label('nes_matrix_row');get('nes_mask');put(0xdc00);get(0xdc01);e.emit(0x49,255);
  e.abs(0x2d,'nes_baseline','read');e.abs(0x9d,matrix,'write');e.abs(0x0e,'nes_mask','write');e.abs(0xee,'nes_mask','write');
  e.emit(0xe8,0xe0,8);e.branch(0xd0,'nes_matrix_row');set(0xdc00,0x40);set(0xdc02,0xc0);set('nes_candidate',0);
  const bit=(value,label,flag)=>{get('nes_joy');e.emit(0x29,value);e.branch(0xf0,label);add(flag);e.label(label);};
  bit(16,'nes_no_a',1);bit(1,'nes_no_up',16);bit(2,'nes_no_down',32);bit(4,'nes_no_left',64);bit(8,'nes_no_right',128);
  const key=(row,mask,label,flag)=>{get(matrix+row);e.emit(0x29,mask);e.branch(0xf0,label);add(flag);e.label(label);};
  key(7,16,'nes_no_b',2);key(0,2,'nes_no_start',8);
  get(matrix+1);e.emit(0x29,128);e.branch(0xd0,'nes_select');get(matrix+6);e.emit(0x29,16);e.branch(0xf0,'nes_no_select');
  e.label('nes_select');add(4);e.label('nes_no_select');
  get('nes_candidate');e.emit(0x29,0x30,0xc9,0x30);e.branch(0xd0,'nes_vertical_ready');get('nes_candidate');e.emit(0x29,0xcf);put('nes_candidate');
  e.label('nes_vertical_ready');get('nes_candidate');e.emit(0x29,0xc0,0xc9,0xc0);e.branch(0xd0,'nes_directions_ready');get('nes_candidate');e.emit(0x29,0x3f);put('nes_candidate');
  e.label('nes_directions_ready');

  // Ctrl+Commodore+F7 toggles Sharp Text once. F7 is row0 bit3; Ctrl and
  // Commodore are row7 bits2/5. Either Shift excludes the chord (F8 case).
  get(matrix);e.emit(0x29,8);e.branch(0xd0,'nes_f7_down');set('nes_sharp_held',0);jump('nes_hotkey_done');
  e.label('nes_f7_down');get(matrix+7);e.emit(0x29,36,0xc9,36);e.branch(0xd0,'nes_hotkey_done');
  get(matrix+1);e.emit(0x29,128);e.branch(0xd0,'nes_hotkey_done');get(matrix+6);e.emit(0x29,16);e.branch(0xd0,'nes_hotkey_done');
  get('nes_sharp_held');e.branch(0xd0,'nes_hotkey_done');get('nes_sharp');e.emit(0x49,1);put('nes_sharp');set('nes_sharp_held',1);
  e.label('nes_hotkey_done');

  get('nes_candidate');e.abs(0xcd,'nes_last_buttons','read');e.branch(0xd0,'nes_queue_changed');
  get('nes_sharp');e.abs(0xcd,'nes_last_display','read');e.branch(0xd0,'nes_queue_changed');e.emit(0x60);
  e.label('nes_queue_changed');get('nes_candidate');put('nes_last_buttons');get('nes_sharp');put('nes_last_display');
  get('nes_queue_tail');e.emit(0x18,0x69,1,0x29,31);put('nes_queue_next');e.abs(0xcd,'nes_queue_head','read');e.branch(0xd0,'nes_queue_room');
  e.abs(0xee,'nes_overflows','write');get('nes_queue_tail');e.emit(0x38,0xe9,1,0x29,31,0xaa);jump('nes_queue_store');
  e.label('nes_queue_room');e.abs(0xae,'nes_queue_tail','read');
  e.label('nes_queue_store');get('nes_candidate');e.abs(0x9d,'nes_queue_buttons','write');get('nes_sharp');e.abs(0x9d,'nes_queue_display','write');
  get('nes_queue_next');e.abs(0xcd,'nes_queue_head','read');e.branch(0xf0,'nes_queue_done');put('nes_queue_tail');
  e.label('nes_queue_done');e.emit(0x60);

  for(const label of ['nes_sequence','nes_send_buttons','nes_send_display','nes_overflows','nes_queue_head','nes_queue_tail','nes_queue_next',
    'nes_last_buttons','nes_last_display','nes_sharp','nes_sharp_held','nes_joy','nes_baseline','nes_mask','nes_candidate']){e.label(label);e.emit(0);}
  e.label('nes_queue_buttons');e.emit(...Array(32).fill(0));e.label('nes_queue_display');e.emit(...Array(32).fill(0));
}

export async function loadNesTerminal(agiRoot) {
  const filename=path.join(agiRoot,'host/mpe3-title-terminal.mjs');
  let source=fs.readFileSync(filename,'utf8');
  function replaceOnce(before,after){
    const index=source.indexOf(before);
    if(index<0||source.indexOf(before,index+before.length)>=0)throw new Error('Shared terminal changed: review NES overlay');
    source=source.slice(0,index)+after+source.slice(index+before.length);
  }
  replaceOnce("import { emitMpe4Keyboard, MPE4_INPUT } from './mpe4-keyboard.mjs';",
    `import { MPE4_INPUT } from '${pathToFileURL(path.join(agiRoot,'host/mpe4-keyboard.mjs')).href}';\n`+
    `import { emitNesController } from '${import.meta.url}';`);
  replaceOnce('if (gameplay) emitMpe4Keyboard(e, state.rasterTicks, { enable1351Mouse });',
    'if (gameplay) emitNesController(e);');
  replaceOnce('  e.abs(0xee, MPE3_TITLE_TERMINAL_STATE.rasterTicks, "write");',
    '  e.abs(0xee, MPE3_TITLE_TERMINAL_STATE.rasterTicks, "write");\n'+
    '  if (gameplay) { e.emit(0x8a,0x48,0x98,0x48); e.abs(0x20,"nes_capture_input"); e.emit(0x68,0xa8,0x68,0xaa); }');
  source=source.replace(/from '(\.\/[^']+)'/g,(_,relative)=>`from '${pathToFileURL(path.resolve(path.dirname(filename),relative)).href}'`);
  return import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
}

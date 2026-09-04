import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { buildDosdirCom } from './dosdir_com.mjs';

const args = process.argv.slice(2);
if (args.length && (args.length !== 2 || args[0] !== '--output'))
  throw new Error('Usage: node dos/tools/build_dosdir_com.mjs [--output PATH]');
const output = resolve(args[1] ?? 'build/dos-work/DOSDIR.COM');
const binary = buildDosdirCom();
mkdirSync(dirname(output), {recursive:true});
writeFileSync(output,binary);
console.log(`Built ${output}: ${binary.length} bytes; resident PSP+hook 272 bytes`);

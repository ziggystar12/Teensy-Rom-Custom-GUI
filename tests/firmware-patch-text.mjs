import assert from 'node:assert/strict';

// Reconstruct the exact function/file after a later patch without changing the
// generated firmware checkout or testing a stale historical implementation.
export function filePatch(patch, relative) {
  const text = patch.replace(/\r\n/g, '\n');
  const begin = text.indexOf(`diff --git a/${relative} b/${relative}\n`);
  assert.ok(begin >= 0, `Patch does not contain ${relative}`);
  const end = text.indexOf('\ndiff --git ', begin + 1);
  return text.slice(begin, end < 0 ? undefined : end);
}

export function patchAdditions(patch, relative) {
  return filePatch(patch, relative).split('\n')
    .filter(line => line.startsWith('+') && !line.startsWith('+++'))
    .map(line => line.slice(1)).join('\n');
}

export function applyFilePatch(source, patch, relative) {
  let updated = source.replace(/\r\n/g, '\n');
  const hunks = filePatch(patch, relative).split(/^@@[^\n]*\n/m).slice(1);
  assert.ok(hunks.length, `No patch hunks for ${relative}`);
  for (const hunk of hunks) {
    const lines = hunk.split('\n');
    const before = lines.filter(line => line.startsWith(' ') || line.startsWith('-'))
      .map(line => line.slice(1)).join('\n');
    const after = lines.filter(line => line.startsWith(' ') || line.startsWith('+'))
      .map(line => line.slice(1)).join('\n');
    const at = updated.indexOf(before);
    assert.ok(before && at >= 0, `Patch context missing for ${relative}`);
    assert.equal(updated.indexOf(before, at + 1), -1, `Ambiguous patch context for ${relative}`);
    updated = updated.slice(0, at) + after + updated.slice(at + before.length);
  }
  return updated;
}

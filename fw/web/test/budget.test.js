// Spec section 11: the whole web UI fits 80 KB gzip in the firmware image.
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const SRC = path.resolve(__dirname, '../src');
const FILES = ['index.html', 'app.js', 'render.js', 'style.css'];
const BUDGET = 80 * 1024;

test('gzip size budget', () => {
  let total = 0;
  for (const f of FILES) {
    const gz = zlib.gzipSync(fs.readFileSync(path.join(SRC, f)), { level: 9 }).length;
    total += gz;
  }
  // glyphs.json is served by /api/glyphs uncompressed but counts too
  total += fs.statSync(path.join(SRC, 'glyphs.json')).size;
  assert.ok(total <= BUDGET, `web UI ${total} bytes > ${BUDGET}`);
  console.log(`web UI: ${total} of ${BUDGET} bytes`);
});

test('pages reference only bundled files', () => {
  const html = fs.readFileSync(path.join(SRC, 'index.html'), 'utf8');
  const refs = [...html.matchAll(/(?:src|href)="\/([^"]+)"/g)].map((m) => m[1]);
  for (const r of refs) assert.ok(FILES.includes(r), `${r} is not compiled into the firmware`);
});

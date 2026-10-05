#!/usr/bin/env node
// Golden frames rendered by tools/sign-simulator/core.js.
//
//   node tools/glyphgen/golden.js [--check]
//
// Writes fw/tests/golden/sign_golden.json (for humans and Node tests) and
// fw/tests/golden/sign_golden_gen.h (for ztest). The C renderer must give
// the same frames and Mobitec bytes.
'use strict';
const fs = require('fs');
const path = require('path');
const core = require('../sign-simulator/core.js');
const { COND, DIRS } = require('./glyphgen.js');

const ROOT = path.resolve(__dirname, '../..');
const OUT_JSON = path.join(ROOT, 'fw/tests/golden/sign_golden.json');
const OUT_H = path.join(ROOT, 'fw/tests/golden/sign_golden_gen.h');
const { W, H } = core;

// ---- op based scenarios: each op is replayed by the C test ----
function apply(f, op) {
  switch (op.op) {
    case 'text': return core.text(f, op.font === 'big' ? core.BIG : core.SM, op.s, op.x, op.y);
    case 'blit': return core.blit(f, bitmap(op.kind, op.name), op.x, op.y);
    case 'bigtemp': return core.bigTemp(f, op.t, op.x, 0, op.plus);
    case 'invert': return core.invert(f, op.x0, op.y0, op.x1, op.y1);
    case 'line': return core.line(f, op.x0, op.y0, op.x1, op.y1);
    default: throw new Error(op.op);
  }
}

function bitmap(kind, name) {
  switch (kind) {
    case 'icon_l': return core.ICON[name];
    case 'icon_s': return core.ICON_S[name];
    case 'wind': return core.WIND[name];
    case 'trend': return core.TREND[name];
    case 'picto_l': return core.PICTO[name].L;
    case 'picto_s': return core.PICTO[name].S;
    default: throw new Error(kind);
  }
}

const opCases = [];
function opCase(name, ops) { opCases.push({ name, ops }); }

opCase('big_digits', [{ op: 'text', font: 'big', s: '0123456789', x: 0, y: 0 },
  { op: 'text', font: 'big', s: '+-.:° 7', x: 72, y: 0 }]);
const smChars = Object.keys(core.SM);
for (let i = 0, part = 0; i < smChars.length; part++) {
  const ops = [];
  for (const y of [0, 6]) {
    let s = '';
    while (i < smChars.length && core.textW(core.SM, s + smChars[i]) <= W) s += smChars[i++];
    ops.push({ op: 'text', font: 'small', s, x: 0, y });
  }
  opCase(`small_chars_${part}`, ops);
}
for (const t of [-35, -10, -9.5, -2.5, -0.4, 0, 0.5, 2.5, 9, 12, 35]) {
  opCase(`bigtemp_${String(t).replace('-', 'm').replace('.', '_')}`,
    [{ op: 'bigtemp', t, x: 0, plus: true }, { op: 'bigtemp', t, x: 50, plus: false }]);
}
opCase('icons_l', COND.map((c, i) => ({ op: 'blit', kind: 'icon_l', name: c, x: i * 12, y: 0 })));
opCase('icons_s_wind_trend', [
  ...COND.map((c, i) => ({ op: 'blit', kind: 'icon_s', name: c, x: i * 6, y: 0 })),
  ...DIRS.map((d, i) => ({ op: 'blit', kind: 'wind', name: d, x: i * 6, y: 6 })),
  ...['up', 'down', 'flat'].map((t, i) => ({ op: 'blit', kind: 'trend', name: t, x: 60 + i * 6, y: 0 })),
]);
const pictos = Object.keys(core.PICTO);
opCase('pictos_l_0', pictos.slice(0, 8).map((p, i) => ({ op: 'blit', kind: 'picto_l', name: p, x: i * 12, y: 0 })));
opCase('pictos_l_1', pictos.slice(8).map((p, i) => ({ op: 'blit', kind: 'picto_l', name: p, x: i * 12, y: 0 })));
opCase('pictos_s', pictos.map((p, i) => ({ op: 'blit', kind: 'picto_s', name: p, x: i * 6, y: 3 })));
opCase('clip_edges', [
  { op: 'blit', kind: 'icon_l', name: 'clear', x: -5, y: 0 },
  { op: 'blit', kind: 'icon_l', name: 'storm', x: 96, y: 2 },
  { op: 'text', font: 'big', s: '88', x: 40, y: -4 },
  { op: 'text', font: 'small', s: 'ДОМ', x: 70, y: 8 },
]);
opCase('invert_line', [
  { op: 'text', font: 'small', s: 'CO2 1240', x: 2, y: 0 },
  { op: 'invert', x0: 0, y0: 0, x1: 40, y1: 5 },
  { op: 'line', x0: 50, y0: 10, x1: 101, y1: 0 },
  { op: 'line', x0: 50, y0: 0, x1: 60, y1: 10 },
  { op: 'invert', x0: 95, y0: -3, x1: 120, y1: 20 },
]);

// ---- raw frames with checksum edge cases (FE and FF) ----
function rng(seed) { let s = seed >>> 0; return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296); }
function rawFrameWithChecksum(target) {
  const r = rng(target * 7919);
  for (;;) {
    const f = core.fb();
    for (let i = 0; i < f.length; i++) f[i] = r() < 0.3 ? 1 : 0;
    const m = core.mobitec(f);
    const n = m.length;
    const cs = m[n - 2] === 0x00 && m[n - 3] === 0xfe ? 0xfe : (m[n - 3] === 0xfe && m[n - 2] === 0x01 ? 0xff : m[n - 2]);
    if (cs === target) return f;
  }
}
const rawCases = [
  { name: 'checksum_fe', frame: rawFrameWithChecksum(0xfe) },
  { name: 'checksum_ff', frame: rawFrameWithChecksum(0xff) },
  { name: 'all_on', frame: core.fb().fill(1) },
  { name: 'all_off', frame: core.fb() },
];

// ---- slot screens (core.js widgets), compared with fw/lib/screens ----
function hourly(t0, amp, popAt) {
  return Array.from({ length: 24 }, (_, i) => ({
    h: (8 + i) % 24,
    t: Math.round(t0 + amp * Math.sin(i / 4)),
    pop: popAt.includes(i) ? 70 : (i === 0 ? 0 : 10),
  }));
}
const datasets = [
  { name: 'dry', d: { now: { t: 12, cond: 'pcloud', wind: 4, windDir: 'nw' }, hourly: hourly(12, 6, []),
    indoor: { t: 22.4, rh: 41, co2: 640, p: 748, ptrend: 0 } } },
  { name: 'rain', d: { now: { t: -2, cond: 'rain', wind: 11, windDir: 'se' }, hourly: hourly(-2, 4, [5, 6, 7, 8]),
    indoor: { t: 19.6, rh: 55, co2: 1240, p: 735, ptrend: -2 } } },
  { name: 'cold', d: { now: { t: -27, cond: 'clear', wind: 0, windDir: 'n' }, hourly: hourly(-27, 3, [2]),
    indoor: { t: 18, rh: 23, co2: 999, p: 771, ptrend: 3 } } },
];
const slotCases = [];
const keys = ['range', 'rain', 'wind', 'press', 'home', 'co2', 'graph'];
for (const ds of datasets) {
  for (let i = 0; i < keys.length; i++) {
    const left = keys[i], right = keys[(i + 3) % keys.length];
    const f = core.screenSlots(ds.d, left === 'graph' ? 'wind' : left, right);
    slotCases.push({ name: `${ds.name}_${left}_${right}`, data: ds.d, left: left === 'graph' ? 'wind' : left, right, frame: f });
  }
}

// ---- output ----
function frameBytes(f) {
  const b = new Array(Math.ceil(W * H / 8)).fill(0);
  for (let i = 0; i < W * H; i++) if (f[i]) b[i >> 3] |= 1 << (i & 7);
  return b;
}
function frameRows(f) {
  const rows = [];
  for (let y = 0; y < H; y++) rows.push(Array.from(f.slice(y * W, y * W + W)).map((v) => (v ? '#' : '.')).join(''));
  return rows;
}
const cArr = (a) => a.map((v) => '0x' + v.toString(16).padStart(2, '0')).join(',');
const cStr = (s) => JSON.stringify(s); // JSON escaping is valid C for our strings

function render() {
  const json = { ops: [], raw: [], slots: [] };
  const h = [];
  h.push('/* Generated by tools/glyphgen/golden.js from core.js. Do not edit. */');
  h.push('#ifndef SIGN_GOLDEN_GEN_H_\n#define SIGN_GOLDEN_GEN_H_\n#include <stdint.h>\n');
  h.push('enum golden_op_kind { G_TEXT_BIG, G_TEXT_SMALL, G_BLIT, G_BIGTEMP, G_INVERT, G_LINE };');
  h.push('struct golden_op { uint8_t kind; int16_t a, b, c, d; const char *s; const char *s2; };');
  h.push('struct golden_case { const char *name; const struct golden_op *ops; int n_ops;');
  h.push('\tuint8_t frame[141]; const uint8_t *mob; int mob_len; };\n');
  const cases = [];
  opCases.forEach((c, ci) => {
    const f = core.fb();
    c.ops.forEach((op) => apply(f, op));
    const mob = core.mobitec(f);
    json.ops.push({ name: c.name, ops: c.ops, frame: frameRows(f), mobitec: mob });
    const ops = c.ops.map((op) => {
      switch (op.op) {
        case 'text': return `{G_TEXT_${op.font.toUpperCase()}, ${op.x}, ${op.y}, 0, 0, ${cStr(op.s)}, 0}`;
        case 'blit': return `{G_BLIT, ${op.x}, ${op.y}, 0, 0, ${cStr(op.kind)}, ${cStr(op.name)}}`;
        case 'bigtemp': return `{G_BIGTEMP, ${Math.round(op.t * 10)}, ${op.x}, ${op.plus ? 1 : 0}, 0, 0, 0}`;
        case 'invert': return `{G_INVERT, ${op.x0}, ${op.y0}, ${op.x1}, ${op.y1}, 0, 0}`;
        case 'line': return `{G_LINE, ${op.x0}, ${op.y0}, ${op.x1}, ${op.y1}, 0, 0}`;
        default: throw new Error(op.op);
      }
    });
    h.push(`static const struct golden_op g_ops_${ci}[] = {\n\t${ops.join(',\n\t')}\n};`);
    h.push(`static const uint8_t g_mob_${ci}[] = {${cArr(mob)}};`);
    cases.push(`{${cStr(c.name)}, g_ops_${ci}, ${ops.length}, {${cArr(frameBytes(f))}}, g_mob_${ci}, ${mob.length}}`);
  });
  h.push(`static const struct golden_case golden_cases[] = {\n\t${cases.join(',\n\t')}\n};\n`);

  h.push('struct golden_raw { const char *name; uint8_t frame[141]; const uint8_t *mob; int mob_len; };');
  const raws = [];
  rawCases.forEach((c, i) => {
    const mob = core.mobitec(c.frame);
    json.raw.push({ name: c.name, frame: frameRows(c.frame), mobitec: mob });
    h.push(`static const uint8_t g_rawmob_${i}[] = {${cArr(mob)}};`);
    raws.push(`{${cStr(c.name)}, {${cArr(frameBytes(c.frame))}}, g_rawmob_${i}, ${mob.length}}`);
  });
  h.push(`static const struct golden_raw golden_raws[] = {\n\t${raws.join(',\n\t')}\n};\n`);

  h.push('struct golden_slots { const char *name; int16_t t, wind; const char *cond, *dir_from;');
  h.push('\tint16_t ht[24], hpop[24]; int16_t in_t_deci, in_rh, in_co2, in_p, in_ptrend;');
  h.push('\tconst char *left, *right; uint8_t frame[141]; };');
  const slots = [];
  slotCases.forEach((c) => {
    const d = c.data;
    json.slots.push({ name: c.name, left: c.left, right: c.right, data: d, frame: frameRows(c.frame) });
    slots.push(`{${cStr(c.name)}, ${d.now.t}, ${d.now.wind}, ${cStr(d.now.cond)}, ${cStr(d.now.windDir)},\n\t {${d.hourly.map((x) => x.t).join(',')}},\n\t {${d.hourly.map((x) => x.pop).join(',')}},\n\t ${Math.round(d.indoor.t * 10)}, ${d.indoor.rh}, ${d.indoor.co2}, ${d.indoor.p}, ${d.indoor.ptrend},\n\t ${cStr(c.left)}, ${cStr(c.right)}, {${cArr(frameBytes(c.frame))}}}`);
  });
  h.push(`static const struct golden_slots golden_slot_cases[] = {\n\t${slots.join(',\n\t')}\n};\n`);
  h.push('#endif\n');
  return [[OUT_JSON, JSON.stringify(json, null, 1) + '\n'], [OUT_H, h.join('\n')]];
}

function main() {
  const checkOnly = process.argv.includes('--check');
  let stale = false;
  for (const [file, content] of render()) {
    const old = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : null;
    if (old === content) continue;
    if (checkOnly) { console.error(`stale: ${path.relative(ROOT, file)}`); stale = true; continue; }
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, content);
    console.log(`wrote ${path.relative(ROOT, file)}`);
  }
  if (stale) { console.error('Run: node tools/glyphgen/golden.js'); process.exit(1); }
}
main();

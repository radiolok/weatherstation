// node --test fw/web/test/
// The browser renderer against the sign simulator (core.js) and the
// committed golden frames that the C renderer is checked against.
'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '../../..');
const R = require('../src/render.js');
const core = require(path.join(ROOT, 'tools/sign-simulator/core.js'));
const glyphs = JSON.parse(fs.readFileSync(path.join(ROOT, 'fw/web/src/glyphs.json'), 'utf8'));
const signGolden = require(path.join(ROOT, 'fw/tests/golden/sign_golden.json'));
const screensGolden = require(path.join(ROOT, 'fw/tests/golden/screens_golden.json'));
const factory = JSON.parse(fs.readFileSync(path.join(ROOT, 'fw/lib/screens/factory_screens.json'), 'utf8'));
const r = R.create(glyphs);

const rows = (f) => Array.from({ length: R.H }, (_, y) =>
  Array.from(f.slice(y * R.W, (y + 1) * R.W)).map((v) => (v ? '#' : '.')).join(''));

function widget(key, x, align) {
  switch (key) {
    case 'range': return [{ type: 'range', form: 'S2', x, w: 30, align }];
    case 'rain': return [{ type: 'rain', form: 'S2', x, w: 30, align, hours: 16 }];
    case 'wind': return [{ type: 'wind', form: 'S2', x, w: 30, align }];
    case 'press': return [{ type: 'pressure', form: 'S2', x, w: 30, align, trend: 0 }];
    case 'home': return [
      { type: 'number', form: 'S', var: 'in.t', x, y: 0, w: 30, align, decimals: 1, suffix: '°', picto: 'home' },
      { type: 'humidity', form: 'S', var: 'in.rh', x, y: 6, w: 30, align }];
    case 'co2': return [{ type: 'co2', form: 'S2', x, w: 30, align, invert: 1000 }];
    case 'graph': return [{ type: 'graph', form: 'L', x, w: 30, hours: 11 }];
    default: throw new Error(key);
  }
}

test('slot widgets draw like core.js', () => {
  for (const c of signGolden.slots) {
    const d = c.data;
    const st = core.dayStats(d);
    const tb = core.bigTempW(d.now.t);
    const cx = Math.round((R.W - (11 + 3 + tb)) / 2);
    const cfg = r.normalize({ schema: 1, screens: [{ id: 's', default: true, items: [
      ...widget(c.left, 0, 'left'), ...widget(c.right, 72, 'right'),
      { type: 'icon', x: cx }, { type: 'temp', x: cx + 14, w: tb, plus: true }] }] });
    const vars = {
      'out.t': d.now.t, 'out.cond': d.now.cond, 'out.wind': d.now.wind, 'out.wind_dir': d.now.windDir,
      'in.t': d.indoor.t, 'in.rh': d.indoor.rh, 'in.co2': d.indoor.co2, 'in.p': d.indoor.p,
      'in.p_trend': d.indoor.ptrend, 'fc.tmax': st.tmax, 'fc.tmin': st.tmin,
      'fc.rain_from': st.rainAt === null ? -1 : st.rainAt, 'fc.rain_to': st.rainEnd === null ? -1 : st.rainEnd,
      'fc.hours': d.hourly.slice(0, 12).map((h) => [h.t, h.pop]),
    };
    assert.deepStrictEqual(rows(r.renderScreen(cfg, 0, vars, 0)), c.frame, c.name);
  }
});

test('committed factory golden frames match render.js', () => {
  for (const g of screensGolden.frames) {
    const f = r.renderScreen(r.normalize(factory), g.screen, screensGolden.sets[g.set], g.sod);
    assert.deepStrictEqual(rows(f), g.frame, `${g.set} ${g.screen} ${g.sod}`);
  }
});

test('Mobitec encoder equals core.js', () => {
  for (const c of signGolden.raw) {
    const f = new Uint8Array(R.W * R.H);
    c.frame.forEach((row, y) => [...row].forEach((ch, x) => { f[y * R.W + x] = ch === '#' ? 1 : 0; }));
    assert.deepStrictEqual(r.mobitec(f), core.mobitec(f), c.name);
  }
});

test('default widths', () => {
  const it = (o) => r.normItem({}, o, 'top', null);
  assert.strictEqual(it({ type: 'temp' }).w, 24); // "-35°"
  assert.strictEqual(it({ type: 'icon' }).w, 11);
  assert.strictEqual(it({ type: 'icon', form: 'S' }).w, 5);
  assert.strictEqual(it({ type: 'clock' }).w, r.textW(glyphs.big, '88:88'));
});

test('unknown values draw dashes, rotators skip them', () => {
  const cfg = r.normalize({ schema: 1, screens: [{ id: 's', default: true, items: [
    { type: 'temp', x: 0 },
    { type: 'rotator', x: 40, w: 30, items: [{ type: 'range' }, { type: 'humidity', var: 'in.rh', y: 3 }] }] }] });
  const f = r.renderScreen(cfg, 0, { 'in.rh': 40 }, 0);
  const s = rows(f).join('\n');
  // "--" of the large font: two 5x2 bars on rows 4..5
  assert.strictEqual(rows(f)[4].slice(0, 11), '#####.#####');
  // the rotator shows humidity although it is the second item: range is unknown
  assert.ok(rows(f)[3].slice(40, 70).includes('#'), s);
});

test('hysteresis keeps the condition', () => {
  const cd = { any: false, list: [{ var: 'in.co2', op: '>', val: 1000, hyst: 100, state: false }] };
  assert.strictEqual(r.condEval(cd, { 'in.co2': 1001 }).ok, true);
  assert.strictEqual(r.condEval(cd, { 'in.co2': 950 }).ok, true);
  assert.strictEqual(r.condEval(cd, { 'in.co2': 899 }).ok, false);
  assert.strictEqual(r.condEval(cd, { 'in.co2': 950 }).ok, false);
});

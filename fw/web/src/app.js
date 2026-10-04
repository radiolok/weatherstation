// Weather station web UI. Plain ES2017, no build step: served gzip'ed from
// flash. The page talks to /api/* (fw/src/web/api.c) and draws with render.js,
// the browser twin of fw/lib/screens/render.c.
(function () {
  'use strict';

  const $ = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
  const el = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (k === 'class') e.className = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (v === true) e.setAttribute(k, '');
      else if (v !== false && v !== null && v !== undefined) e.setAttribute(k, v);
    }
    for (const k of kids.flat()) if (k !== null && k !== undefined) e.append(k.nodeType ? k : String(k));
    return e;
  };
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const p2 = (n) => String(n).padStart(2, '0');
  const hm = (m) => `${p2(Math.floor(m / 60))}:${p2(m % 60)}`;

  // ---------------------------------------------------------------- API
  async function api(path, opts) {
    const o = Object.assign({ headers: {} }, opts || {});
    if (o.json !== undefined) {
      o.body = JSON.stringify(o.json);
      o.headers['Content-Type'] = 'application/json';
      delete o.json;
    }
    const r = await fetch(path, o);
    let body = null;
    const text = await r.text();
    try { body = text ? JSON.parse(text) : null; } catch (e) { body = { error: text }; }
    if (!r.ok) {
      const err = new Error((body && (body.error || (body.errors && body.errors.map((x) => `${x.path}: ${x.msg}`).join('; ')))) || `HTTP ${r.status}`);
      err.status = r.status; err.body = body;
      throw err;
    }
    return body;
  }
  function msg(text, bad) {
    const m = $('#msg');
    m.textContent = text || '';
    m.className = bad ? 'bad' : 'ok';
  }

  // ---------------------------------------------------------------- state
  const S = {
    cfg: null,       // the screens file being edited
    saved: '',       // JSON of the last saved version
    catalog: null, glyphs: null, R: null,
    vars: {},        // name -> value (simulator / previews)
    varInfo: [],     // [{name, type, unit}]
    cur: 0,          // screen in the editor
    sel: -1,         // selected item
    undo: [], redo: [],
    sim: { sod: 12 * 60 },
    pic: { idx: -1 },
  };

  function setDirty() {
    const d = JSON.stringify(S.cfg) !== S.saved;
    $('#dirty').hidden = !d;
  }
  function snapshot() {
    S.undo.push(JSON.stringify(S.cfg));
    if (S.undo.length > 100) S.undo.shift();
    S.redo = [];
  }
  function changed(skipSnapshot) {
    setDirty();
    renderAll();
    validateSoon();
    return skipSnapshot;
  }
  function edit(fn) {
    snapshot();
    fn();
    changed();
  }

  // ---------------------------------------------------------------- dots
  const SCALE = 8;
  function css(name, fallback) {
    const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return v || fallback;
  }
  function drawFrame(cv, frame, opts) {
    const o = opts || {};
    const W = S.R.W, H = S.R.H;
    const sc = cv.width / W;
    const g = cv.getContext('2d');
    g.fillStyle = css('--sign-bg', '#111');
    g.fillRect(0, 0, cv.width, cv.height);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        g.fillStyle = frame[y * W + x] ? css('--dot-on', '#ffb000') : css('--dot-off', '#2a2a2a');
        g.beginPath();
        g.arc(x * sc + sc / 2, y * sc + sc / 2, sc * 0.38, 0, Math.PI * 2);
        g.fill();
      }
    }
    if (o.boxes) {
      for (const b of o.boxes) {
        g.strokeStyle = b.sel ? css('--accent', '#4af') : 'rgba(128,160,255,.35)';
        g.lineWidth = b.sel ? 2 : 1;
        g.setLineDash(b.sel ? [] : [3, 3]);
        g.strokeRect(b.x * sc + 0.5, b.y * sc + 0.5, b.w * sc - 1, b.h * sc - 1);
        if (b.bad) { g.strokeStyle = '#e44'; g.setLineDash([]); g.strokeRect(b.x * sc + 2, b.y * sc + 2, b.w * sc - 4, b.h * sc - 4); }
      }
      g.setLineDash([]);
    }
  }
  function renderOne(screenIdx, sod) {
    const n = S.R.normalize(S.cfg);
    return { n, frame: S.R.renderScreen(n, screenIdx, varsAt(sod === undefined ? S.sim.sod : sod), (sod === undefined ? S.sim.sod : sod) * 60) };
  }
  function varsAt(min) {
    const v = Object.assign({}, S.vars);
    v['time.hour'] = Math.floor(min / 60);
    v['time.min'] = min % 60;
    if (!v['time.dow']) v['time.dow'] = 1;
    return v;
  }

  // ---------------------------------------------------------------- tabs
  function showTab(name) {
    $$('#tabs [role=tab]').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.tab === name)));
    $$('.tab').forEach((s) => { s.hidden = s.id !== `tab-${name}`; });
    if (name === 'status') refreshStatus();
    if (name === 'settings') loadSettings();
    renderAll();
  }

  // ---------------------------------------------------------------- summaries
  function cmpText(c) {
    if (c.all || c.any) return `(${condText(c)})`;
    const v = Array.isArray(c.val) ? (c.op === 'between' ? `${c.val[0]}…${c.val[1]}` : c.val.join(', ')) : c.val;
    const h = c.hyst ? ` ±${c.hyst}` : '';
    return `${c.var} ${c.op === 'between' ? 'в' : c.op === 'in' ? 'из' : c.op} ${v}${h}`;
  }
  function condText(r) {
    const list = r.all || r.any || [];
    return list.map(cmpText).join(r.any ? ' или ' : ' и ');
  }
  function ruleText(sc) {
    if (sc.default) return 'экран по умолчанию';
    const r = sc.rule;
    if (!r) return 'без правила — показывается только вручную';
    const parts = [];
    const c = condText(r);
    if (c) parts.push(`когда ${c}`);
    if (r.time) {
      const d = r.time.days && r.time.days.length < 7 ? ` по дням ${r.time.days.join(',')}` : '';
      parts.push(`с ${r.time.from} до ${r.time.to}${d}`);
    }
    if ((r.mode || 'while') === 'insert') {
      const ins = r.insert || {};
      parts.push(`вставкой на ${ins.show || 30} с каждые ${ins.every || 10} мин`);
    }
    parts.push(`приоритет ${r.prio || 50}`);
    if (r.on_delay) parts.push(`задержка включения ${r.on_delay} с`);
    if (r.off_delay) parts.push(`задержка выключения ${r.off_delay} с`);
    parts.push(`минимум ${r.min_show === undefined ? 300 : r.min_show} с на экране`);
    return parts.join(', ');
  }

  // ---------------------------------------------------------------- screens list
  function renderList() {
    const ol = $('#screen-list');
    ol.textContent = '';
    S.cfg.screens.forEach((sc, i) => {
      const cv = el('canvas', { width: 408, height: 44, class: 'mini' });
      const li = el('li', { class: `card${i === S.cur ? ' current' : ''}${sc.enabled === false ? ' off' : ''}`, 'data-id': sc.id },
        el('div', { class: 'card-head' },
          el('strong', {}, sc.name || sc.id), ' ', el('code', {}, sc.id),
          sc.default ? el('span', { class: 'badge' }, 'по умолчанию') : null,
          el('span', { class: 'grow' }),
          el('label', {}, el('input', { type: 'checkbox', checked: sc.enabled !== false, onchange: (e) => edit(() => { sc.enabled = e.target.checked; }) }), ' включён'),
          el('button', { title: 'выше', disabled: i === 0, onclick: () => move(i, -1) }, '↑'),
          el('button', { title: 'ниже', disabled: i === S.cfg.screens.length - 1, onclick: () => move(i, 1) }, '↓'),
          el('button', { onclick: () => { S.cur = i; S.sel = -1; showTab('editor'); } }, 'Изменить'),
          el('button', { onclick: () => { S.cur = i; showTab('rule'); } }, 'Правило')),
        cv,
        el('p', { class: 'summary' }, ruleText(sc)));
      li.addEventListener('click', (e) => { if (e.target === li || e.target === cv) { S.cur = i; S.sel = -1; renderList(); } });
      ol.append(li);
      try { drawFrame(cv, renderOne(i).frame); } catch (e) { /* invalid screen: errors come from validation */ }
    });
  }
  function move(i, d) {
    edit(() => {
      const a = S.cfg.screens;
      [a[i], a[i + d]] = [a[i + d], a[i]];
      if (S.cur === i) S.cur = i + d; else if (S.cur === i + d) S.cur = i;
    });
  }
  // A new rule must already be valid: a time window the user then edits.
  function newRule() { return { time: { from: '07:00', to: '08:00' }, prio: 50 }; }
  function uniqueId(base) {
    let id = base, n = 2;
    while (S.cfg.screens.some((s) => s.id === id)) id = `${base}${n++}`;
    return id;
  }

  // ---------------------------------------------------------------- editor
  function screen() { return S.cfg.screens[S.cur]; }
  function typeInfo(t) { return S.catalog.types.find((x) => x.type === t); }

  function renderPalette() {
    const pal = $('#palette');
    if (pal.childElementCount) return;
    const groups = {};
    S.catalog.types.forEach((t) => { (groups[t.group] = groups[t.group] || []).push(t); });
    for (const [g, list] of Object.entries(groups)) {
      pal.append(el('h3', {}, g));
      for (const t of list) {
        const b = el('div', { class: 'pal-item', draggable: 'true', tabindex: '0', 'data-type': t.type, title: `Перетащите на холст или нажмите Enter (${t.type})` }, t.label);
        b.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/x-ws-type', t.type); e.dataTransfer.effectAllowed = 'copy'; });
        b.addEventListener('keydown', (e) => { if (e.key === 'Enter') addItem(t.type, 0, 0); });
        b.addEventListener('dblclick', () => addItem(t.type, 0, 0));
        pal.append(b);
      }
    }
  }
  function newItem(type, x, y) {
    const t = typeInfo(type);
    const it = { type, form: t.form, x, y: t.form === 'S' ? y : 0 };
    if (type === 'text') it.text = 'ТЕКСТ';
    if (type === 'picto') it.name = S.catalog.pictos[0];
    if (type === 'number') it.var = 'in.co2';
    if (type === 'rotator') { it.w = 30; it.items = [{ type: 'clock', form: 'S' }]; }
    return it;
  }
  function addItem(type, x, y) {
    edit(() => {
      const sc = screen();
      sc.items = sc.items || [];
      sc.items.push(newItem(type, Math.max(0, Math.min(S.R.W - 1, x)), Math.max(0, Math.min(6, y))));
      S.sel = sc.items.length - 1;
    });
  }
  function boxesOf(n) {
    const items = n.screens[S.cur].items;
    const boxes = items.map((it, i) => ({ x: it.x, y: it.y, w: it.w, h: it.h, sel: i === S.sel, bad: false }));
    for (let i = 0; i < items.length; i++) {
      for (let j = i + 1; j < items.length; j++) {
        const a = items[i], b = items[j];
        if (!a.when && !b.when && S.R.rectsOverlap(a, b)) { boxes[i].bad = boxes[j].bad = true; }
      }
      const b = boxes[i];
      if (b.x < 0 || b.y < 0 || b.x + b.w > S.R.W || b.y + b.h > S.R.H) b.bad = true;
    }
    return boxes;
  }
  function renderEditor() {
    const sc = screen();
    if (!sc) return;
    const sel = $('#ed-screen');
    sel.textContent = '';
    S.cfg.screens.forEach((s, i) => sel.append(el('option', { value: i, selected: i === S.cur }, s.name || s.id)));
    $('#ed-name').value = sc.name || '';
    $('#ed-id').value = sc.id;
    $('#ed-enabled').checked = sc.enabled !== false;
    renderPalette();
    const ruler = $('#ruler');
    if (!ruler.childElementCount) for (let x = 0; x < S.R.W; x += 10) ruler.append(el('span', { style: `left:${x * SCALE}px` }, x));
    let n;
    try {
      const r = renderOne(S.cur);
      n = r.n;
      drawFrame($('#ed-canvas'), r.frame, { boxes: boxesOf(n) });
    } catch (e) {
      $('#ed-errors').textContent = `Ошибка отрисовки: ${e.message}`;
    }
    renderProps(n);
  }
  function itemAt(x, y) {
    const n = S.R.normalize(S.cfg);
    const items = n.screens[S.cur].items;
    for (let i = items.length - 1; i >= 0; i--) {
      const it = items[i];
      if (x >= it.x && x < it.x + it.w && y >= it.y && y < it.y + it.h) return i;
    }
    return -1;
  }
  function canvasDot(e) {
    const cv = $('#ed-canvas');
    const r = cv.getBoundingClientRect();
    return { x: Math.floor((e.clientX - r.left) / r.width * S.R.W), y: Math.floor((e.clientY - r.top) / r.height * S.R.H) };
  }
  function setupCanvas() {
    const cv = $('#ed-canvas');
    let drag = null;
    cv.addEventListener('dragover', (e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'copy'; });
    cv.addEventListener('drop', (e) => {
      e.preventDefault();
      const t = e.dataTransfer.getData('text/x-ws-type');
      if (!t) return;
      const p = canvasDot(e);
      addItem(t, p.x, p.y);
    });
    cv.addEventListener('pointerdown', (e) => {
      const p = canvasDot(e);
      S.sel = itemAt(p.x, p.y);
      if (S.sel >= 0) {
        const it = screen().items[S.sel];
        const n = S.R.normalize(S.cfg).screens[S.cur].items[S.sel];
        drag = { dx: p.x - n.x, dy: p.y - n.y, moved: false, it };
        cv.setPointerCapture(e.pointerId);
      }
      cv.focus();
      renderEditor();
    });
    cv.addEventListener('pointermove', (e) => {
      if (!drag) return;
      const p = canvasDot(e);
      const nx = Math.max(0, Math.min(S.R.W - 1, p.x - drag.dx));
      const ny = Math.max(0, Math.min(S.R.H - 1, p.y - drag.dy));
      if (nx === (drag.it.x || 0) && ny === (drag.it.y || 0)) return;
      if (!drag.moved) { snapshot(); drag.moved = true; }
      drag.it.x = nx;
      if (drag.it.form === 'S' || drag.it.y !== undefined) drag.it.y = ny;
      changed();
    });
    const end = () => { drag = null; };
    cv.addEventListener('pointerup', end);
    cv.addEventListener('pointercancel', end);
    cv.addEventListener('keydown', (e) => {
      if (S.sel < 0) return;
      const it = screen().items[S.sel];
      const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
      if (d) {
        e.preventDefault();
        edit(() => { it.x = Math.max(0, (it.x || 0) + d[0]); if (d[1]) it.y = Math.max(0, (it.y || 0) + d[1]); });
      } else if (e.key === 'Delete' || e.key === 'Backspace') {
        e.preventDefault();
        edit(() => { screen().items.splice(S.sel, 1); S.sel = -1; });
      }
    });
  }

  const PARAM_UI = {
    plus: { label: 'Знак «+»', kind: 'bool' },
    tenths: { label: 'Десятые', kind: 'bool' },
    deg: { label: 'Знак градуса', kind: 'bool', def: true },
    align: { label: 'Выравнивание', kind: 'enum', values: ['left', 'center', 'right'], names: ['влево', 'по центру', 'вправо'] },
    decimals: { label: 'Знаков после запятой', kind: 'int', min: 0, max: 2 },
    digits: { label: 'Цифр (для ширины)', kind: 'int', min: 1, max: 6 },
    prefix: { label: 'Префикс', kind: 'str' },
    suffix: { label: 'Суффикс', kind: 'str' },
    picto: { label: 'Пиктограмма слева', kind: 'picto', optional: true },
    none: { label: 'Текст «нет осадков»', kind: 'str' },
    hours: { label: 'Горизонт, ч', kind: 'int', min: 1, max: 48 },
    skip: { label: 'Скрывать, если нет осадков', kind: 'bool' },
    dir: { label: 'Стрелка', kind: 'enum', values: ['to', 'from'], names: ['куда дует', 'откуда дует'] },
    trend: { label: 'Порог тренда, мм', kind: 'num' },
    invert: { label: 'Инверсия выше, ppm', kind: 'int', min: 0, max: 10000 },
    text: { label: 'Текст', kind: 'str' },
    name: { label: 'Пиктограмма', kind: 'picto' },
    dashed: { label: 'Пунктир', kind: 'bool' },
    period: { label: 'Период, с', kind: 'int', min: 5, max: 3600 },
    offset: { label: 'Сдвиг, с', kind: 'int', min: 0, max: 3600 },
  };
  function field(label, input) { return el('label', { class: 'prop' }, el('span', {}, label), input); }
  function propInput(it, key, ui, normVal) {
    const set = (v) => edit(() => { if (v === undefined || v === '') delete it[key]; else it[key] = v; });
    const cur = it[key] === undefined ? (ui.def !== undefined ? ui.def : normVal) : it[key];
    switch (ui.kind) {
      case 'bool': return el('input', { type: 'checkbox', checked: !!cur, onchange: (e) => set(e.target.checked) });
      case 'int': case 'num':
        return el('input', { type: 'number', step: ui.kind === 'num' ? '0.1' : '1', min: ui.min, max: ui.max, value: cur === undefined || cur === null ? '' : cur,
          onchange: (e) => set(e.target.value === '' ? undefined : Number(e.target.value)) });
      case 'enum': {
        const s = el('select', { onchange: (e) => set(e.target.value) });
        ui.values.forEach((v, i) => s.append(el('option', { value: v, selected: v === cur }, ui.names[i])));
        return s;
      }
      case 'picto': {
        const s = el('select', { onchange: (e) => set(e.target.value || undefined) });
        if (ui.optional) s.append(el('option', { value: '' }, '—'));
        S.catalog.pictos.concat((S.cfg.pictos || []).map((p) => p.name)).filter((v, i, a) => a.indexOf(v) === i)
          .forEach((p) => s.append(el('option', { value: p, selected: p === cur }, p)));
        return s;
      }
      default:
        return el('input', { value: cur || '', onchange: (e) => set(e.target.value) });
    }
  }
  function jsonField(label, it, key, hint) {
    const ta = el('textarea', { rows: 3, placeholder: hint },
      it[key] === undefined ? '' : JSON.stringify(it[key]));
    ta.addEventListener('change', () => {
      const t = ta.value.trim();
      try {
        const v = t ? JSON.parse(t) : undefined;
        edit(() => { if (v === undefined) delete it[key]; else it[key] = v; });
      } catch (e) {
        ta.classList.add('bad');
        msg(`${label}: неверный JSON (${e.message})`, true);
      }
    });
    return field(label, ta);
  }
  function renderProps(n) {
    const box = $('#props');
    box.textContent = '';
    const sc = screen();
    const altBar = $('#alt-bar');
    altBar.textContent = '';
    if (S.sel < 0 || !sc.items || !sc.items[S.sel]) {
      box.append(el('p', { class: 'hint' }, 'Выберите элемент на холсте.'));
      return;
    }
    const it = sc.items[S.sel];
    const t = typeInfo(it.type);
    const ni = n ? n.screens[S.cur].items[S.sel] : {};
    box.append(el('h3', {}, t.label));
    const form = el('select', { onchange: (e) => edit(() => { it.form = e.target.value; if (it.form !== 'S') delete it.y; }) });
    t.forms.forEach((f) => form.append(el('option', { value: f, selected: f === (it.form || t.form) }, f)));
    box.append(field('Форма', form));
    for (const k of ['x', 'y', 'w']) {
      box.append(field(k, el('input', { type: 'number', min: 0, max: k === 'y' ? 10 : 101, value: it[k] === undefined ? '' : it[k], placeholder: ni ? String(ni[k]) : '',
        onchange: (e) => edit(() => { if (e.target.value === '') delete it[k]; else it[k] = Number(e.target.value); }) })));
    }
    if (t.vars.length) {
      const s = el('select', { onchange: (e) => edit(() => { it.var = e.target.value; }) });
      t.vars.forEach((v) => s.append(el('option', { value: v, selected: v === (it.var || t.var) }, v)));
      box.append(field('Переменная', s));
    }
    for (const p of t.params) {
      if (p === 'var') continue;
      const ui = PARAM_UI[p];
      if (ui) box.append(field(ui.label, propInput(it, p, ui, ni ? ni[p] : undefined)));
    }
    if (it.type === 'rotator') box.append(jsonField('Элементы ротации', it, 'items', '[{"type":"clock","form":"S"}]'));
    box.append(jsonField('Показывать, если (when)', it, 'when', '{"all":[{"var":"fc.rain_in","op":"<","val":6}]}'));
    box.append(jsonField('Альтернативы', it, 'alts', '[{"type":"wind","when":{...}}]'));
    box.append(el('div', { class: 'row' },
      el('button', { onclick: () => edit(() => { sc.items.splice(S.sel + 1, 0, clone(it)); S.sel++; }) }, 'Копия'),
      el('button', { onclick: () => edit(() => { sc.items.splice(S.sel, 1); S.sel = -1; }) }, 'Удалить')));
    if (it.alts && it.alts.length) {
      altBar.append('Альтернативы: ', ...it.alts.map((a, i) => el('span', { class: 'chip' }, `${i + 1}. ${a.type}${a.when ? ' — ' + condText(a.when) : ''}`)));
    }
  }

  // ---------------------------------------------------------------- validation
  let vTimer = 0;
  function validateSoon() {
    clearTimeout(vTimer);
    vTimer = setTimeout(validate, 400);
  }
  async function validate() {
    const out = $('#ed-errors');
    try {
      await api('/api/screens/validate', { method: 'POST', json: S.cfg });
      out.textContent = '';
      out.className = 'errors';
      return true;
    } catch (e) {
      out.textContent = e.body && e.body.errors ? e.body.errors.map((x) => `${x.path}: ${x.msg}`).join('\n') : e.message;
      out.className = 'errors bad';
      return false;
    }
  }

  // ---------------------------------------------------------------- rule editor
  const OPS = ['=', '!=', '<', '<=', '>', '>=', 'between', 'in'];
  function parseVal(s, op) {
    const one = (x) => { x = x.trim(); if (x === 'true') return true; if (x === 'false') return false; const n = Number(x); return x !== '' && !isNaN(n) ? n : x; };
    if (op === 'between' || op === 'in') return s.split(op === 'between' ? /…|\.\.|,/ : ',').filter((x) => x.trim() !== '').map(one);
    return one(s);
  }
  function condRows(list, onChange) {
    const tb = el('tbody');
    list.forEach((c, i) => {
      if (c.all || c.any) {
        tb.append(el('tr', {}, el('td', { colspan: 4 }, el('code', {}, JSON.stringify(c))),
          el('td', {}, el('button', { onclick: () => onChange(() => list.splice(i, 1)) }, '×'))));
        return;
      }
      const vs = el('select', { onchange: (e) => onChange(() => { c.var = e.target.value; }) });
      S.varInfo.forEach((v) => vs.append(el('option', { value: v.name, selected: v.name === c.var }, v.name)));
      const os = el('select', { onchange: (e) => onChange(() => { c.op = e.target.value; c.val = parseVal(String(Array.isArray(c.val) ? c.val.join(',') : c.val), c.op); }) });
      OPS.forEach((o) => os.append(el('option', { value: o, selected: o === c.op }, o)));
      tb.append(el('tr', {},
        el('td', {}, vs), el('td', {}, os),
        el('td', {}, el('input', { value: Array.isArray(c.val) ? c.val.join(', ') : c.val, size: 10, onchange: (e) => onChange(() => { c.val = parseVal(e.target.value, c.op); }) })),
        el('td', {}, el('input', { type: 'number', step: '0.1', min: 0, value: c.hyst || '', placeholder: 'гистерезис', onchange: (e) => onChange(() => { if (e.target.value === '') delete c.hyst; else c.hyst = Number(e.target.value); }) })),
        el('td', {}, el('button', { title: 'удалить', onclick: () => onChange(() => list.splice(i, 1)) }, '×'))));
    });
    return el('table', { class: 'conds' }, el('thead', {}, el('tr', {}, el('th', {}, 'Переменная'), el('th', {}, 'Сравнение'), el('th', {}, 'Значение'), el('th', {}, 'Гистерезис'), el('th'))), tb);
  }
  function renderRule() {
    const sel = $('#rule-screen');
    sel.textContent = '';
    S.cfg.screens.forEach((s, i) => sel.append(el('option', { value: i, selected: i === S.cur }, s.name || s.id)));
    const sc = screen();
    const box = $('#rule-editor');
    box.textContent = '';
    $('#rule-summary').textContent = ruleText(sc);
    if (sc.default) { box.append(el('p', {}, 'Экран по умолчанию показывается, когда не подходит ни одно правило.')); return; }
    if (!sc.rule) {
      box.append(el('button', { onclick: () => edit(() => { sc.rule = newRule(); }) }, 'Добавить правило'));
      return;
    }
    const r = sc.rule;
    const any = !!r.any;
    const list = r.any || r.all || [];
    const tidy = () => { if (r.all && !r.all.length) delete r.all; if (r.any && !r.any.length) delete r.any; };
    const num = (key, label, def, min, max) => field(label, el('input', { type: 'number', min, max, value: r[key] === undefined ? def : r[key],
      onchange: (e) => edit(() => { r[key] = Number(e.target.value); }) }));
    const mode = el('select', { onchange: (e) => edit(() => { r.mode = e.target.value; if (r.mode === 'insert') r.insert = r.insert || { show: 30, every: 10 }; else delete r.insert; }) },
      el('option', { value: 'while', selected: (r.mode || 'while') === 'while' }, 'пока выполняется условие'),
      el('option', { value: 'insert', selected: r.mode === 'insert' }, 'вставка по расписанию'));
    const join = el('select', { onchange: (e) => edit(() => { const l = r.all || r.any || []; delete r.all; delete r.any; if (l.length) r[e.target.value] = l; }) },
      el('option', { value: 'all', selected: !any }, 'все условия (И)'),
      el('option', { value: 'any', selected: any }, 'любое условие (ИЛИ)'));
    box.append(el('div', { class: 'rule-grid' },
      field('Режим', mode), field('Объединение', join),
      num('prio', 'Приоритет 1–99', 50, 1, 99), num('on_delay', 'Задержка включения, с', 0, 0, 86400),
      num('off_delay', 'Задержка выключения, с', 0, 0, 86400), num('min_show', 'Минимальный показ, с', 300, 0, 86400)));
    if (r.mode === 'insert') {
      box.append(el('div', { class: 'rule-grid' },
        field('Показ, с', el('input', { type: 'number', min: 5, max: 3600, value: r.insert.show, onchange: (e) => edit(() => { r.insert.show = Number(e.target.value); }) })),
        field('Каждые, мин', el('input', { type: 'number', min: 1, max: 1440, value: r.insert.every, onchange: (e) => edit(() => { r.insert.every = Number(e.target.value); }) }))));
    }
    box.append(condRows(list, (fn) => edit(() => { fn(); tidy(); })));
    box.append(el('div', { class: 'row' },
      el('button', { onclick: () => edit(() => { (r.any || (r.all = r.all || [])).push({ var: 'out.t', op: '<', val: 0 }); }) }, 'Добавить условие'),
      el('button', { onclick: () => edit(() => { delete sc.rule; }) }, 'Удалить правило')));
    const t = r.time;
    const tbox = el('fieldset', {}, el('legend', {}, el('label', {}, el('input', { type: 'checkbox', checked: !!t,
      onchange: (e) => edit(() => { if (e.target.checked) r.time = { from: '07:00', to: '23:00' }; else delete r.time; }) }), ' только в интервал времени')));
    if (t) {
      tbox.append(field('с', el('input', { type: 'time', value: t.from, onchange: (e) => edit(() => { t.from = e.target.value; }) })),
        field('до', el('input', { type: 'time', value: t.to, onchange: (e) => edit(() => { t.to = e.target.value; }) })));
      const days = t.days || [1, 2, 3, 4, 5, 6, 7];
      ['Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб', 'Вс'].forEach((d, i) => tbox.append(el('label', { class: 'day' },
        el('input', { type: 'checkbox', checked: days.includes(i + 1), onchange: (e) => edit(() => {
          const s = new Set(t.days || [1, 2, 3, 4, 5, 6, 7]);
          if (e.target.checked) s.add(i + 1); else s.delete(i + 1);
          t.days = Array.from(s).sort();
          if (t.days.length === 7) delete t.days;
        }) }), d)));
    }
    box.append(tbox);
  }

  // ---------------------------------------------------------------- simulator
  function renderSimVars() {
    const box = $('#sim-vars');
    if (box.dataset.ready) return;
    box.dataset.ready = '1';
    for (const v of S.varInfo) {
      if (v.name.startsWith('time.')) continue;
      let input;
      const val = S.vars[v.name];
      const set = (x) => { if (x === null || x === '') delete S.vars[v.name]; else S.vars[v.name] = x; renderSim(); };
      if (v.type === 'bool') {
        input = el('select', { onchange: (e) => set(e.target.value === '' ? null : e.target.value === '1') },
          el('option', { value: '' }, '—'), el('option', { value: '1', selected: val === true }, 'да'), el('option', { value: '0', selected: val === false }, 'нет'));
      } else if (v.type === 'cond' || v.type === 'dir') {
        input = el('select', { onchange: (e) => set(e.target.value || null) }, el('option', { value: '' }, '—'));
        (v.type === 'cond' ? S.glyphs.cond : S.glyphs.dirs).forEach((c) => input.append(el('option', { value: c, selected: c === val }, c)));
      } else if (v.type === 'hours') {
        input = el('input', { value: val ? JSON.stringify(val) : '', placeholder: '[[t,pop],…]', onchange: (e) => { try { set(e.target.value ? JSON.parse(e.target.value) : null); } catch (x) { msg('fc.hours: неверный JSON', true); } } });
      } else if (v.type === 'num') {
        input = el('input', { type: 'number', step: '0.1', value: val === undefined || val === null ? '' : val, onchange: (e) => set(e.target.value === '' ? null : Number(e.target.value)) });
      } else {
        input = el('input', { value: val === undefined || val === null ? '' : val, onchange: (e) => { const t = e.target.value; set(t === '' ? null : (isNaN(Number(t)) ? t : Number(t))); } });
      }
      box.append(el('label', { class: 'var' }, el('span', {}, v.name, v.unit ? ` (${v.unit})` : ''), input));
    }
  }
  function settle(n, vars) {
    const e = S.R.createEngine(n);
    // conditions with delays: step long enough for on_delay/min_show to pass
    for (let t = 0; t <= 3600 * 3; t += 30) e.step(vars, t);
    return e;
  }
  function renderSim() {
    renderSimVars();
    const sod = S.sim.sod;
    $('#sim-time').value = sod;
    $('#sim-time-out').textContent = hm(sod);
    const n = S.R.normalize(S.cfg);
    const vars = varsAt(sod);
    const e = settle(n, vars);
    drawFrame($('#sim-canvas'), S.R.renderScreen(n, e.current, vars, sod * 60));
    $('#sim-reason').textContent = `Экран «${n.screens[e.current].name}»: ${e.reason}`;
  }
  function simDay() {
    const n = S.R.normalize(S.cfg);
    const e = S.R.createEngine(n);
    const rows = [];
    let prev = -1, prevFrame = null, flips = 0, changes = 0;
    for (let m = 0; m < 24 * 60; m++) {
      const vars = varsAt(m);
      e.step(vars, m * 60);
      const f = S.R.renderScreen(n, e.current, vars, m * 60);
      if (prevFrame) flips += S.R.diff(prevFrame, f).dots;
      prevFrame = f;
      if (e.current !== prev) {
        if (prev >= 0) changes++;
        rows.push(el('tr', {}, el('td', {}, hm(m)), el('td', {}, n.screens[e.current].name), el('td', {}, e.reason)));
        prev = e.current;
      }
    }
    const box = $('#sim-day');
    box.textContent = '';
    box.append(el('p', {}, `Смен экрана за сутки: ${changes}, перевёрнутых точек: ${flips}.`),
      el('table', { class: 'vars' }, el('thead', {}, el('tr', {}, el('th', {}, 'Время'), el('th', {}, 'Экран'), el('th', {}, 'Причина'))), el('tbody', {}, rows)));
  }

  // ---------------------------------------------------------------- pictograms
  function pictoRows(p) {
    return p.rows.map((h) => { const v = parseInt(h, 16); const r = []; for (let x = 0; x < p.size; x++) r.push((v >> (p.size - 1 - x)) & 1); return r; });
  }
  function pictoHex(bits, size) {
    return bits.map((r) => r.reduce((a, b, x) => a | (b << (size - 1 - x)), 0).toString(16).padStart(size > 8 ? 4 : 2, '0'));
  }
  function curPicto() { return (S.cfg.pictos || [])[S.pic.idx]; }
  function renderPictos() {
    const list = S.cfg.pictos || [];
    const sel = $('#pic-select');
    sel.textContent = '';
    if (S.pic.idx >= list.length) S.pic.idx = list.length - 1;
    if (S.pic.idx < 0 && list.length) S.pic.idx = 0;
    list.forEach((p, i) => sel.append(el('option', { value: i, selected: i === S.pic.idx }, `${p.name} (${p.size})`)));
    const p = curPicto();
    const cv = $('#pic-grid');
    const g = cv.getContext('2d');
    g.fillStyle = css('--sign-bg', '#111');
    g.fillRect(0, 0, cv.width, cv.height);
    $('#pic-name').value = p ? p.name : '';
    $('#pic-size').value = p ? String(p.size) : '11';
    if (!p) { $('#pic-used').textContent = 'Своих пиктограмм нет.'; return; }
    const bits = pictoRows(p);
    const c = cv.width / 11;
    bits.forEach((r, y) => r.forEach((b, x) => {
      g.fillStyle = b ? css('--dot-on', '#ffb000') : css('--dot-off', '#2a2a2a');
      g.fillRect(x * c + 1, y * c + 1, c - 2, c - 2);
    }));
    const used = S.cfg.screens.filter((s) => JSON.stringify(s.items || []).includes(`"${p.name}"`)).map((s) => s.name || s.id);
    $('#pic-used').textContent = used.length ? `Используется: ${used.join(', ')}` : 'Пока не используется';
    const n = S.R.normalize(S.cfg);
    const prev = S.R.fb();
    S.R.blit({ f: prev, clip: { x: 0, y: 0, w: S.R.W, h: S.R.H } }, S.R.picto(n, p.name, p.size === 11), 0, 0);
    drawFrame($('#pic-preview'), prev);
  }
  function picEdit(fn) {
    const p = curPicto();
    if (!p) return;
    edit(() => { const b = pictoRows(p); const nb = fn(b, p.size) || b; p.rows = pictoHex(nb, p.size); });
  }
  const PIC_OPS = {
    invert: (b) => b.map((r) => r.map((v) => 1 - v)),
    clear: (b) => b.map((r) => r.map(() => 0)),
    left: (b) => b.map((r) => r.slice(1).concat([0])),
    right: (b) => b.map((r) => [0].concat(r.slice(0, -1))),
    up: (b) => b.slice(1).concat([b[0].map(() => 0)]),
    down: (b) => [b[0].map(() => 0)].concat(b.slice(0, -1)),
    flip: (b) => b.map((r) => r.slice().reverse()),
    rotate: (b, n) => b.map((r, y) => r.map((v, x) => b[n - 1 - x][y])),
  };
  function setupPictos() {
    $('#pic-select').addEventListener('change', (e) => { S.pic.idx = Number(e.target.value); renderPictos(); });
    $('#pic-new').addEventListener('click', () => {
      const size = Number($('#pic-size').value);
      edit(() => {
        S.cfg.pictos = S.cfg.pictos || [];
        let name = 'my', k = 1;
        while (S.cfg.pictos.some((p) => p.name === name + k)) k++;
        S.cfg.pictos.push({ name: name + k, size, rows: pictoHex(Array.from({ length: size }, () => Array(size).fill(0)), size) });
        S.pic.idx = S.cfg.pictos.length - 1;
      });
    });
    $('#pic-copy').addEventListener('click', () => {
      const name = prompt(`Встроенная пиктограмма (${S.glyphs.pictos ? Object.keys(S.glyphs.pictos).join(', ') : ''}):`);
      const b = name && S.glyphs.pictos[name];
      if (!b) return;
      const size = Number($('#pic-size').value);
      const rows = (size === 11 ? b.L : b.S).map((r) => [...r].map((c) => (c === '#' ? 1 : 0)));
      edit(() => {
        S.cfg.pictos = S.cfg.pictos || [];
        S.cfg.pictos.push({ name: `${name}2`, size, rows: pictoHex(rows, size) });
        S.pic.idx = S.cfg.pictos.length - 1;
      });
    });
    $('#pic-del').addEventListener('click', () => { if (curPicto()) edit(() => { S.cfg.pictos.splice(S.pic.idx, 1); S.pic.idx--; }); });
    $('#pic-name').addEventListener('change', (e) => { const p = curPicto(); if (p) edit(() => { p.name = e.target.value; }); });
    $('#pic-size').addEventListener('change', (e) => {
      const p = curPicto();
      if (!p) return;
      const size = Number(e.target.value);
      if (size === p.size) return;
      edit(() => { const b = pictoRows(p); p.rows = pictoHex(Array.from({ length: size }, (_, y) => Array.from({ length: size }, (_, x) => (b[y] && b[y][x]) || 0)), size); p.size = size; });
    });
    $$('.pic-tools [data-op]').forEach((b) => b.addEventListener('click', () => picEdit(PIC_OPS[b.dataset.op])));
    const grid = $('#pic-grid');
    let paint = null;
    const at = (e) => { const r = grid.getBoundingClientRect(); return { x: Math.floor((e.clientX - r.left) / r.width * 11), y: Math.floor((e.clientY - r.top) / r.height * 11) }; };
    grid.addEventListener('pointerdown', (e) => {
      const p = curPicto();
      if (!p) return;
      const q = at(e);
      if (q.x >= p.size || q.y >= p.size) return;
      const b = pictoRows(p);
      paint = 1 - b[q.y][q.x];
      grid.setPointerCapture(e.pointerId);
      picEdit((bb) => { bb[q.y][q.x] = paint; });
    });
    grid.addEventListener('pointermove', (e) => {
      const p = curPicto();
      if (paint === null || !p) return;
      const q = at(e);
      if (q.x >= p.size || q.y >= p.size || q.x < 0 || q.y < 0) return;
      const b = pictoRows(p);
      if (b[q.y][q.x] === paint) return;
      b[q.y][q.x] = paint;
      p.rows = pictoHex(b, p.size);
      changed();
    });
    grid.addEventListener('pointerup', () => { paint = null; });
    $('#pic-import').addEventListener('change', async (e) => {
      const p = curPicto();
      const f = e.target.files[0];
      if (!p || !f) return;
      const img = new Image();
      img.src = URL.createObjectURL(f);
      await img.decode();
      const c = document.createElement('canvas');
      c.width = c.height = p.size;
      const g = c.getContext('2d');
      g.drawImage(img, 0, 0, p.size, p.size);
      const d = g.getImageData(0, 0, p.size, p.size).data;
      picEdit((b) => b.map((r, y) => r.map((_, x) => {
        const i = (y * p.size + x) * 4;
        return d[i + 3] > 127 && (d[i] + d[i + 1] + d[i + 2]) / 3 < 160 ? 1 : 0;
      })));
      e.target.value = '';
    });
  }

  // ---------------------------------------------------------------- settings
  async function loadSettings() {
    try {
      const s = await api('/api/settings');
      const f = $('#settings-form');
      for (const inp of $$('[name]', f)) {
        const k = inp.name;
        if (k.startsWith('geo.')) inp.value = s[k] === undefined ? '' : s[k] / 1e6;
        else if (inp.type === 'password') { inp.value = ''; inp.placeholder = s[`${k}_set`] ? 'задан — оставьте пустым, чтобы не менять' : 'не задан'; }
        else if (s[k] !== undefined) inp.value = s[k];
      }
    } catch (e) { msg(`Настройки: ${e.message}`, true); }
  }
  async function saveSettings(ev) {
    ev.preventDefault();
    const out = {};
    for (const inp of $$('#settings-form [name]')) {
      const k = inp.name;
      if (inp.type === 'password' && !inp.value) continue;
      if (k.startsWith('geo.')) { if (inp.value !== '') out[k] = Math.round(Number(inp.value) * 1e6); continue; }
      out[k] = inp.type === 'number' ? Number(inp.value) : inp.value;
    }
    try {
      await api('/api/settings', { method: 'POST', json: out });
      msg('Настройки сохранены');
      loadSettings();
    } catch (e) { msg(`Настройки не сохранены: ${e.message}`, true); }
  }
  async function scan() {
    msg('Поиск сетей…');
    try {
      const r = await api('/api/wifi/scan');
      const dl = $('#ssid-list');
      dl.textContent = '';
      r.networks.sort((a, b) => b.rssi - a.rssi).forEach((n) => dl.append(el('option', { value: n.ssid }, `${n.rssi} dBm${n.open ? ', открытая' : ''}`)));
      msg(`Найдено сетей: ${r.networks.length}`);
    } catch (e) { msg(`Поиск сетей: ${e.message}`, true); }
  }
  async function uploadOta(e) {
    const f = e.target.files[0];
    if (!f) return;
    const bar = $('#ota-progress');
    bar.hidden = false;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/upload');
    xhr.upload.onprogress = (ev) => { if (ev.lengthComputable) bar.value = Math.round(ev.loaded / ev.total * 100); };
    xhr.onload = () => msg(xhr.status === 200 ? 'Образ загружен, устройство перезагрузится' : `Обновление: ${xhr.responseText}`, xhr.status !== 200);
    xhr.onerror = () => msg('Обновление: ошибка связи', true);
    xhr.send(f);
  }

  // ---------------------------------------------------------------- status
  let stTimer = 0;
  async function refreshStatus() {
    clearTimeout(stTimer);
    if ($('#tab-status').hidden) return;
    try {
      const [st, ds, vs] = await Promise.all([api('/api/status'), api('/api/display/state'), api('/api/vars')]);
      const dl = $('#status-list');
      dl.textContent = '';
      const kv = (k, v) => dl.append(el('dt', {}, k), el('dd', {}, v === undefined || v === null ? '—' : String(v)));
      kv('Устройство', st.id); kv('Версия', st.version); kv('Время работы, с', st.uptime);
      kv('Сеть', `${st.net}${st.ip ? ' ' + st.ip : ''}${st.rssi ? `, ${st.rssi} dBm` : ''}`);
      kv('MQTT', st.mqtt ? 'подключён' : 'нет');
      kv('Время', st.time || 'не синхронизировано');
      kv('NTP', st.ntp_server); kv('Набор экранов', st.screens_source);
      kv('Подсветка', st.lamp ? 'вкл' : 'выкл'); kv('Обновление', st.ota);
      kv('Экран', `${ds.name} (${ds.screen}) — ${ds.reason}`);
      kv('Смен за сутки', ds.flips_24h);
      $('#lamp-toggle').dataset.on = st.lamp ? '1' : '0';
      const tb = $('#vars-table tbody');
      tb.textContent = '';
      for (const v of vs.vars) {
        tb.append(el('tr', {}, el('td', {}, v.name), el('td', {}, v.value === null ? '—' : (typeof v.value === 'object' ? JSON.stringify(v.value) : `${v.value}${v.unit ? ' ' + v.unit : ''}`)),
          el('td', {}, v.src), el('td', {}, v.age === null ? '—' : v.age)));
      }
    } catch (e) { msg(`Состояние: ${e.message}`, true); }
    stTimer = setTimeout(refreshStatus, 5000);
  }

  // ---------------------------------------------------------------- top-level actions
  function renderAll() {
    if (!S.cfg) return;
    const tab = ($$('#tabs [aria-selected=true]')[0] || {}).dataset;
    const t = tab ? tab.tab : 'screens';
    if (t === 'screens') renderList();
    else if (t === 'editor') renderEditor();
    else if (t === 'rule') renderRule();
    else if (t === 'sim') renderSim();
    else if (t === 'pictos') renderPictos();
  }
  async function save() {
    try {
      await api('/api/screens', { method: 'PUT', json: S.cfg });
      S.saved = JSON.stringify(S.cfg);
      setDirty();
      msg('Экраны сохранены и применены');
    } catch (e) { msg(`Не сохранено: ${e.message}`, true); }
  }
  async function reload(note) {
    const cfg = await api('/api/screens');
    S.cfg = cfg;
    S.saved = JSON.stringify(cfg);
    S.undo = []; S.redo = [];
    S.cur = Math.min(S.cur, cfg.screens.length - 1);
    S.sel = -1;
    setDirty();
    renderAll();
    if (note) msg(note);
  }
  function setupActions() {
    $$('#tabs [role=tab]').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
    $('#btn-save').addEventListener('click', save);
    $('#btn-new').addEventListener('click', () => edit(() => {
      S.cfg.screens.push({ id: uniqueId('screen'), name: 'Новый экран', enabled: true, rule: newRule(), items: [] });
      S.cur = S.cfg.screens.length - 1;
    }));
    $('#btn-dup').addEventListener('click', () => edit(() => {
      const c = clone(screen()); c.id = uniqueId(c.id); c.name = `${c.name || c.id} (копия)`; delete c.default;
      if (!c.rule) c.rule = newRule();
      S.cfg.screens.splice(S.cur + 1, 0, c); S.cur++;
    }));
    $('#btn-del').addEventListener('click', () => {
      const sc = screen();
      if (sc.default) { msg('Экран по умолчанию удалить нельзя — сначала назначьте другой', true); return; }
      if (confirm(`Удалить экран «${sc.name || sc.id}»?`)) edit(() => { S.cfg.screens.splice(S.cur, 1); S.cur = Math.max(0, S.cur - 1); });
    });
    $('#btn-default').addEventListener('click', () => edit(() => {
      S.cfg.screens.forEach((s, i) => { if (i === S.cur) { s.default = true; delete s.rule; } else delete s.default; });
    }));
    $('#btn-export').addEventListener('click', () => {
      const a = el('a', { href: URL.createObjectURL(new Blob([JSON.stringify(S.cfg, null, 1)], { type: 'application/json' })), download: 'screens.json' });
      document.body.append(a); a.click(); a.remove();
    });
    $('#file-import').addEventListener('change', async (e) => {
      const f = e.target.files[0];
      if (!f) return;
      try {
        const cfg = JSON.parse(await f.text());
        await api('/api/screens/validate', { method: 'POST', json: cfg });
        edit(() => { S.cfg = cfg; S.cur = 0; S.sel = -1; });
        msg('Файл загружен — нажмите «Сохранить», чтобы применить');
      } catch (x) { msg(`Импорт: ${x.message}`, true); }
      e.target.value = '';
    });
    $('#btn-rollback').addEventListener('click', async () => {
      try { await api('/api/screens/rollback', { method: 'POST' }); await reload('Возвращена предыдущая версия'); } catch (e) { msg(`Откат: ${e.message}`, true); }
    });
    $('#btn-factory').addEventListener('click', async () => {
      if (!confirm('Заменить все экраны заводским набором?')) return;
      try { await api('/api/screens/factory', { method: 'POST' }); await reload('Установлен заводской набор'); } catch (e) { msg(`Сброс: ${e.message}`, true); }
    });
    $('#ed-screen').addEventListener('change', (e) => { S.cur = Number(e.target.value); S.sel = -1; renderAll(); });
    $('#rule-screen').addEventListener('change', (e) => { S.cur = Number(e.target.value); renderAll(); });
    $('#ed-name').addEventListener('change', (e) => edit(() => { screen().name = e.target.value; }));
    $('#ed-id').addEventListener('change', (e) => edit(() => { screen().id = e.target.value; }));
    $('#ed-enabled').addEventListener('change', (e) => edit(() => { screen().enabled = e.target.checked; }));
    const undo = () => { if (!S.undo.length) return; S.redo.push(JSON.stringify(S.cfg)); S.cfg = JSON.parse(S.undo.pop()); changed(); };
    const redo = () => { if (!S.redo.length) return; S.undo.push(JSON.stringify(S.cfg)); S.cfg = JSON.parse(S.redo.pop()); changed(); };
    $('#btn-undo').addEventListener('click', undo);
    $('#btn-redo').addEventListener('click', redo);
    document.addEventListener('keydown', (e) => {
      if (!(e.ctrlKey || e.metaKey) || ['INPUT', 'TEXTAREA', 'SELECT'].includes(e.target.tagName)) return;
      if (e.key === 'z') { e.preventDefault(); undo(); }
      if (e.key === 'y' || (e.key === 'Z' && e.shiftKey)) { e.preventDefault(); redo(); }
    });
    $('#btn-preview').addEventListener('click', async () => {
      try {
        await api('/api/display/preview', { method: 'POST', json: { config: S.cfg, screen: screen().id, seconds: 60 } });
        msg('Экран показан на табло на 60 секунд');
      } catch (e) { msg(`Показ: ${e.message}`, true); }
    });
    $('#sim-time').addEventListener('input', (e) => { S.sim.sod = Number(e.target.value); renderSim(); });
    $('#btn-sim-day').addEventListener('click', simDay);
    $('#settings-form').addEventListener('submit', saveSettings);
    $('#btn-scan').addEventListener('click', scan);
    $('#ota-file').addEventListener('change', uploadOta);
    $('#lamp-toggle').addEventListener('click', async (e) => {
      try { await api('/api/lamp', { method: 'POST', json: { on: e.target.dataset.on !== '1' } }); refreshStatus(); } catch (x) { msg(`Подсветка: ${x.message}`, true); }
    });
    window.addEventListener('beforeunload', (e) => { if (S.cfg && JSON.stringify(S.cfg) !== S.saved) { e.preventDefault(); e.returnValue = ''; } });
  }

  async function init() {
    setupActions();
    setupCanvas();
    setupPictos();
    try {
      const [glyphs, catalog, vars] = await Promise.all([api('/api/glyphs'), api('/api/catalog'), api('/api/vars')]);
      S.glyphs = glyphs; S.catalog = catalog;
      S.R = window.WSRender.create(glyphs);
      S.varInfo = vars.vars.map((v) => ({ name: v.name, type: v.type, unit: v.unit }));
      for (const v of vars.vars) if (v.value !== null) S.vars[v.name] = v.value;
      const now = new Date();
      S.sim.sod = now.getHours() * 60 + now.getMinutes() - ((now.getHours() * 60 + now.getMinutes()) % 10);
      S.vars['time.dow'] = S.vars['time.dow'] || ((now.getDay() + 6) % 7) + 1;
      await reload();
      document.body.classList.add('ready');
    } catch (e) {
      msg(`Не удалось загрузить данные: ${e.message}`, true);
    }
  }
  window.WSApp = { state: S, api, ruleText, condText };
  document.addEventListener('DOMContentLoaded', init);
})();

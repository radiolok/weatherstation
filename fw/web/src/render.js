// Screen renderer for the browser: the same elements as fw/lib/screens/render.c.
// Golden frames (fw/web/tools/golden-screens.js) keep both implementations
// identical to the dot; the C side is checked by ztest, this side by node --test.
//
// Works in the browser (window.WSRender) and in Node (module.exports).
(function (root) {
  'use strict';

  const W = 102, H = 11;
  const TYPES = ['temp', 'icon', 'number', 'rain', 'wind', 'range', 'pressure', 'humidity',
    'co2', 'graph', 'clock', 'text', 'picto', 'sep', 'rotator'];
  const FORMS = {
    temp: ['L', 'S'], icon: ['L', 'S'], number: ['L', 'S'], rain: ['S2', 'L', 'S'],
    wind: ['S2', 'S'], range: ['S2'], pressure: ['S2', 'S'], humidity: ['S'], co2: ['L', 'S2'],
    graph: ['L'], clock: ['L', 'S'], text: ['S'], picto: ['L', 'S'], sep: ['L'], rotator: ['L'],
  };
  const VAR_DEFAULT = {
    temp: 'out.t', icon: 'out.cond', rain: 'fc.rain_from', wind: 'out.wind', range: 'fc.tmax',
    pressure: 'in.p', humidity: 'in.rh', co2: 'in.co2', graph: 'fc.hours', clock: 'time.hour',
  };
  // Variable types: num (tenths internally), bool, cond, dir, str, hours, any
  const VARS = {
    'out.t': 'num', 'out.cond': 'cond', 'out.wind': 'num', 'out.wind_dir': 'dir', 'out.rh': 'num',
    'out.p': 'num', 'fc.tmax': 'num', 'fc.tmin': 'num', 'fc.rain_from': 'num', 'fc.rain_to': 'num',
    'fc.rain_in': 'num', 'fc.snow': 'bool', 'fc.ice': 'bool', 'fc.storm': 'bool',
    'fc.next_name': 'str', 'fc.next_t': 'num', 'fc.next_cond': 'cond', 'fc.hours': 'hours',
    'fc.age': 'num', 'in.t': 'num', 'in.rh': 'num', 'in.p': 'num', 'in.p_trend': 'num',
    'in.co2': 'num', 'time.hour': 'num', 'time.min': 'num', 'time.dow': 'num', 'sun.up': 'bool',
    'sys.lamp': 'bool', 'sys.mqtt': 'bool', 'sys.wifi_rssi': 'num',
    'ext.1': 'any', 'ext.2': 'any', 'ext.3': 'any', 'ext.4': 'any', 'ext.5': 'any', 'ext.6': 'any',
    'ext.7': 'any', 'ext.8': 'any', 'obs.t': 'num', 'obs.cond': 'cond', 'obs.wind': 'num',
    'obs.wind_dir': 'dir', 'obs.p': 'num', 'obs.rh': 'num', 'obs.ice': 'bool', 'obs.age': 'num',
  };
  const ALIASES = { 'fc.next': 'fc.next_cond' };
  const OPPOSITE = { n: 's', ne: 'sw', e: 'w', se: 'nw', s: 'n', sw: 'ne', w: 'e', nw: 'se' };

  function create(glyphs) {
    const big = glyphs.big, small = glyphs.small;

    // ---- primitives (core.js semantics) with a clip rectangle ----
    function fb() { return new Uint8Array(W * H); }
    function canvas(f, clip) { return { f, clip: clip || { x: 0, y: 0, w: W, h: H } }; }
    function inClip(c, x, y) {
      return x >= c.clip.x && x < c.clip.x + c.clip.w && y >= c.clip.y && y < c.clip.y + c.clip.h &&
        x >= 0 && x < W && y >= 0 && y < H;
    }
    function px(c, x, y) { if (inClip(c, x, y)) c.f[y * W + x] = 1; }
    function blit(c, rows, x, y) {
      if (!rows) return;
      rows.forEach((row, dy) => { for (let dx = 0; dx < row.length; dx++) if (row[dx] === '#') px(c, x + dx, y + dy); });
    }
    function textW(font, s, gap = 1) {
      let w = 0;
      for (const ch of s) { const g = font[ch]; if (!g) continue; w += g[0].length + gap; }
      return Math.max(0, w - gap);
    }
    function text(c, font, s, x, y, gap = 1) {
      for (const ch of s) { const g = font[ch]; if (!g) continue; blit(c, g, x, y); x += g[0].length + gap; }
      return x - gap;
    }
    function invert(c, x0, y0, x1, y1) {
      for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) if (inClip(c, x, y)) c.f[y * W + x] ^= 1;
    }
    function line(c, x0, y0, x1, y1) {
      const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
      let e = dx + dy;
      for (;;) {
        px(c, x0, y0);
        if (x0 === x1 && y0 === y1) break;
        const e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
      }
    }
    // Math.round(deci / 10) without floating point surprises
    function roundDeci(d) { return Math.floor((d + 5) / 10); }
    function tempStr(d) { const v = roundDeci(d); return (v > 0 ? '+' : (v < 0 ? '-' : '')) + Math.abs(v); }
    function tenths(d) { const a = Math.abs(d); return (d < 0 ? '-' : '') + Math.floor(a / 10) + '.' + (a % 10); }
    const p2 = (h) => String(h).padStart(2, '0');

    // ---- configuration defaults (fw/lib/config/config.c) ----
    function heightOf(form) { return form === 'S' ? 5 : 11; }
    function picto(cfg, name, large) {
      const user = (cfg.pictos || []).find((p) => p.name === name && (p.size === 11) === large);
      if (user) {
        return user.rows.map((h) => {
          const v = parseInt(h, 16); let s = '';
          for (let x = 0; x < user.size; x++) s += (v >> (user.size - 1 - x)) & 1 ? '#' : '.';
          return s;
        });
      }
      const b = glyphs.pictos[name];
      return b ? (large ? b.L : b.S) : null;
    }
    function defaultW(cfg, it) {
      const large = it.form === 'L';
      const sw = (s) => textW(small, s), bw = (s) => textW(big, s);
      switch (it.type) {
        case 'temp': { const s = (it.tenths ? '-35.5' : '-35') + (it.deg ? '°' : ''); return large ? bw(s) : sw(s); }
        case 'icon': case 'picto': return large ? 11 : 5;
        case 'number': {
          let s = (it.prefix || '') + '-' + '8'.repeat(it.digits);
          if (it.decimals) s += '.' + '8'.repeat(it.decimals);
          s += it.suffix || '';
          return (large ? bw(s) : sw(s)) + (it.picto ? (large ? 11 : 5) + 2 : 0);
        }
        case 'rain':
          if (large) return 11 + 2 + bw('88-88');
          if (it.form === 'S') return Math.max(7 + sw('88Ч'), 7 + sw(it.none));
          return Math.max(7 + sw('88Ч'), 7 + sw(it.none), sw('ДО 88Ч'));
        case 'wind': return it.form === 'S' ? 7 + sw('88') : Math.max(7 + sw('88'), sw('М/С'));
        case 'range': return sw('↑-35');
        case 'pressure': return it.form === 'S' ? 7 + sw('888') : Math.max(7 + sw('888'), sw('ММ'));
        case 'humidity': return 7 + sw('100%');
        case 'co2': return large ? bw('8888') : sw('8888') + 2;
        case 'graph': return 30;
        case 'clock': return large ? bw('88:88') : sw('88:88');
        case 'text': return Math.max(1, sw(it.text || ''));
        case 'sep': return 1;
        case 'rotator': return Math.max(1, ...(it.items || []).map((ch) => defaultW(cfg, ch)));
        default: return 1;
      }
    }
    function secondVar(type, v) {
      switch (type) {
        case 'wind': return v === 'obs.wind' ? 'obs.wind_dir' : 'out.wind_dir';
        case 'range': return 'fc.tmin';
        case 'pressure': return v === 'in.p' ? 'in.p_trend' : null;
        case 'clock': return 'time.min';
        case 'rain': return 'fc.rain_to';
        default: return null;
      }
    }
    // Fills defaults like the compiler; returns a new object tree.
    function normItem(cfg, src, kind, parent) {
      const it = Object.assign({}, src);
      it.form = it.form || FORMS[it.type][0];
      if (it.type === 'rain') it.form = src.form || 'S2';
      it.var = ALIASES[it.var] || it.var || VAR_DEFAULT[it.type] || null;
      it.var2 = secondVar(it.type, it.var);
      it.plus = !!it.plus; it.tenths = !!it.tenths; it.deg = it.deg !== false;
      it.skip = it.skip === undefined ? it.type === 'rain' : !!it.skip;
      it.dashed = !!it.dashed;
      it.dirFrom = it.dir === 'from';
      it.decimals = it.decimals || 0; it.digits = it.digits || 4;
      it.align = it.align || 'left';
      if (it.type === 'co2') it.thr = (it.invert === undefined ? (it.form === 'L' ? 0 : 1000) : it.invert) * 10;
      if (it.type === 'pressure') it.thr = Math.round((it.trend === undefined ? 1 : it.trend) * 10);
      if (it.type === 'graph') it.hours = it.hours || 12;
      if (it.type === 'range' || it.type === 'rain') it.hours = it.hours || 16;
      if (it.type === 'rain') it.none = it.none === undefined ? 'НЕТ' : it.none;
      if (kind === 'alt') {
        Object.assign(it, { x: parent.x, y: parent.y, w: parent.w, h: parent.h });
      } else {
        it.x = src.x !== undefined ? src.x : (kind === 'child' ? parent.x : 0);
        it.y = src.y !== undefined ? src.y : (kind === 'child' ? parent.y : 0);
        it.h = it.type === 'rotator' ? (src.h || 11) : heightOf(it.form);
        if (it.type === 'rotator') {
          it.period = it.period || 60; it.offset = it.offset || 0;
          const explicit = src.w !== undefined;
          it.w = explicit ? src.w : 1;
          const pf = { x: it.x, y: it.y, w: explicit ? it.w : -1, h: it.h };
          it.items = (src.items || []).map((ch) => normItem(cfg, ch, 'child', pf));
          if (!explicit) it.w = defaultW(cfg, it);
        } else if (src.w !== undefined) {
          it.w = src.w;
        } else if (kind === 'child' && parent.w > 0) {
          it.w = parent.w - (it.x - parent.x);
        } else {
          it.w = defaultW(cfg, it);
        }
      }
      it.when = src.when ? normCond(src.when) : null;
      it.alts = kind !== 'alt' && src.alts ? src.alts.map((a) => normItem(cfg, a, 'alt', it)) : [];
      return it;
    }
    function normCmp(c) {
      if (c.all || c.any) return Object.assign(normCond(c), { group: true });
      return { var: ALIASES[c.var] || c.var, op: c.op, val: c.val, hyst: c.hyst || 0, state: false };
    }
    function normCond(c) {
      return { any: !!c.any, list: (c.all || c.any || []).map(normCmp) };
    }
    function normalize(cfg) {
      const out = { schema: 1, pictos: cfg.pictos || [], ext: cfg.ext || [], screens: [] };
      out.screens = (cfg.screens || []).map((s) => ({
        id: s.id, name: s.name || s.id, default: !!s.default, enabled: s.enabled !== false,
        rule: s.rule || null,
        items: (s.items || []).map((it) => normItem(out, it, 'top', null)),
      }));
      return out;
    }

    // ---- values: plain JS values in natural units, null/undefined = unknown ----
    function deci(x) { return Math.sign(x) * Math.floor(Math.abs(x) * 10 + 0.5 + 1e-9); }
    function value(vars, name) {
      if (!name) return null;
      const v = vars[name];
      if (v === undefined || v === null) return null;
      const t = VARS[name];
      if (t === 'num' || (t === 'any' && typeof v === 'number')) return { num: deci(v) };
      if (t === 'bool') return { num: v ? 1 : 0 };
      if (t === 'cond') { const i = glyphs.cond.indexOf(v); return i < 0 ? null : { num: i, s: v }; }
      if (t === 'dir') { const i = glyphs.dirs.indexOf(v); return i < 0 ? null : { num: i, s: v }; }
      if (t === 'hours') return v.length ? { hours: v } : null;
      return { str: String(v) };
    }
    function cmpEval(c, vars) {
      if (c.group) return condEval(c, vars).ok;
      const x = value(vars, c.var);
      if (!x || x.num === undefined) { c.state = false; return false; }
      const t = VARS[c.var];
      const conv = (v) => (t === 'bool' ? (v ? 1 : 0) : t === 'cond' ? glyphs.cond.indexOf(v)
        : t === 'dir' ? glyphs.dirs.indexOf(v) : deci(v));
      const v = x.num, h = deci(c.hyst || 0), on = c.state;
      let r = false;
      switch (c.op) {
        case '=': case '==': r = v === conv(c.val); break;
        case '!=': case '≠': r = v !== conv(c.val); break;
        case '>': r = on ? v > conv(c.val) - h : v > conv(c.val); break;
        case '>=': case '≥': r = on ? v >= conv(c.val) - h : v >= conv(c.val); break;
        case '<': r = on ? v < conv(c.val) + h : v < conv(c.val); break;
        case '<=': case '≤': r = on ? v <= conv(c.val) + h : v <= conv(c.val); break;
        case 'between': { const lo = conv(c.val[0]), hi = conv(c.val[1]);
          r = on ? v >= lo - h && v <= hi + h : v >= lo && v <= hi; break; }
        case 'in': r = c.val.map(conv).includes(v); break;
        default: r = false;
      }
      c.state = r;
      return r;
    }
    function condEval(cd, vars) {
      const res = cd.list.map((c) => cmpEval(c, vars));
      return { ok: cd.any ? res.some(Boolean) : res.every(Boolean), each: res };
    }

    // ---- elements ----
    function fontOf(it) { return it.form === 'L' ? big : small; }
    function alignX(it, tw) {
      if (it.align === 'right') return it.x + it.w - tw;
      if (it.align === 'center') return it.x + Math.floor((it.w - tw) / 2);
      return it.x;
    }
    function put(c, it, font, s, y) { const x = alignX(it, textW(font, s)); text(c, font, s, x, y); return x; }
    function putIcon(c, it, rows, font, s, y) {
      const bw = rows ? rows[0].length : 0;
      const x = alignX(it, bw + 2 + textW(font, s));
      blit(c, rows, x, y); text(c, font, s, x + bw + 2, y);
    }
    function state(it, vars) {
      switch (it.type) {
        case 'text': case 'picto': case 'sep': return 'ok';
        case 'rotator': return 'ok';
        case 'rain': { const v = value(vars, it.var); if (!v) return 'unknown'; return v.num < 0 ? 'empty' : 'ok'; }
        case 'range': case 'clock': return value(vars, it.var) && value(vars, it.var2) ? 'ok' : 'unknown';
        case 'graph': { const v = value(vars, it.var); return v && v.hours.length >= 2 ? 'ok' : 'unknown'; }
        default: return value(vars, it.var) ? 'ok' : 'unknown';
      }
    }
    function resolve(it, vars) {
      for (const a of it.alts) if (a.when && condEval(a.when, vars).ok) return a;
      if (it.when && !condEval(it.when, vars).ok) return null;
      return it;
    }
    function rotatorPick(rot, vars, sod) {
      const avail = [];
      rot.items.forEach((ch, i) => {
        const r = resolve(ch, vars);
        if (!r) return;
        const s = state(r, vars);
        if (s === 'unknown' || (s === 'empty' && r.skip)) return;
        avail.push(i);
      });
      if (!avail.length) return -1;
      const slot = Math.floor((sod - rot.offset) / rot.period);
      return avail[((slot % avail.length) + avail.length) % avail.length];
    }
    function graphY(t, mn, sp) { return Math.floor(7 - (t - mn) / sp * 7 + 0.5); }

    function draw(cfg, c, it, vars, sod) {
      if (it.type === 'rotator') {
        const k = rotatorPick(it, vars, sod);
        if (k < 0) return;
        const ch = it.items[k];
        const outer = c.clip;
        c.clip = { x: ch.x, y: ch.y, w: ch.w, h: ch.h };
        draw(cfg, c, resolve(ch, vars), vars, sod);
        c.clip = outer;
        return;
      }
      if (state(it, vars) === 'unknown') {
        const smallIcon = (it.type === 'icon' || it.type === 'picto') && it.form === 'S';
        put(c, it, fontOf(it), smallIcon ? '-' : '--', it.y);
        return;
      }
      const v = value(vars, it.var);
      switch (it.type) {
        case 'temp': {
          if (v.str !== undefined) { put(c, it, fontOf(it), v.str, it.y); break; }
          let num, neg, pos;
          if (it.tenths) { const a = Math.abs(v.num); num = Math.floor(a / 10) + '.' + (a % 10); neg = v.num < 0; pos = v.num > 0; }
          else { const r = roundDeci(v.num); num = String(Math.abs(r)); neg = r < 0; pos = r > 0; }
          const deg = it.deg ? '°' : '';
          let s = (neg ? '-' : (pos && it.plus ? '+' : '')) + num + deg;
          if (pos && it.plus && textW(fontOf(it), s) > it.w) s = num + deg;
          put(c, it, fontOf(it), s, it.y);
          break;
        }
        case 'icon': {
          const name = glyphs.cond[v.num];
          blit(c, it.form === 'L' ? glyphs.icon_l[name] : glyphs.icon_s[name], alignX(it, it.form === 'L' ? 11 : 5), it.y);
          break;
        }
        case 'number': {
          let num;
          if (v.str !== undefined) num = v.str;
          else if (it.decimals === 0) num = String(roundDeci(v.num));
          else { num = tenths(v.num); if (it.decimals === 2) num += '0'; }
          const s = (it.prefix || '') + num + (it.suffix || '');
          if (it.picto) putIcon(c, it, picto(cfg, it.picto, it.form === 'L'), fontOf(it), s, it.y);
          else put(c, it, fontOf(it), s, it.y);
          break;
        }
        case 'rain': {
          const to = value(vars, it.var2);
          const hasTo = to && to.num >= 0;
          const from = v.num;
          if (it.form === 'L') {
            if (from < 0) break;
            const s = hasTo ? p2(Math.trunc(from / 10)) + '-' + p2(Math.trunc(to.num / 10)) : p2(Math.trunc(from / 10));
            putIcon(c, it, glyphs.pictos.umbrella.L, big, s, it.y);
            break;
          }
          const umb = small['☂'];
          if (from < 0) {
            putIcon(c, it, umb, small, it.none, it.y);
            if (it.form === 'S2') put(c, it, small, it.hours + 'Ч', it.y + 6);
            break;
          }
          putIcon(c, it, umb, small, p2(Math.trunc(from / 10)) + 'Ч', it.y);
          if (it.form === 'S2' && hasTo) put(c, it, small, 'ДО ' + p2(Math.trunc(to.num / 10)) + 'Ч', it.y + 6);
          break;
        }
        case 'wind': {
          const s = String(roundDeci(v.num));
          const d = value(vars, it.var2);
          if (d) putIcon(c, it, glyphs.wind[it.dirFrom ? d.s : OPPOSITE[d.s]], small, s, it.y);
          else put(c, it, small, s, it.y);
          if (it.form === 'S2') put(c, it, small, 'М/С', it.y + 6);
          break;
        }
        case 'range': {
          const mn = value(vars, it.var2);
          put(c, it, small, '↑' + tempStr(v.num), it.y);
          put(c, it, small, '↓' + tempStr(mn.num), it.y + 6);
          break;
        }
        case 'pressure': {
          const tr = value(vars, it.var2);
          let t = 'flat';
          if (tr) { if (tr.num > it.thr) t = 'up'; else if (tr.num < -it.thr) t = 'down'; }
          putIcon(c, it, glyphs.trend[t], small, String(roundDeci(v.num)), it.y);
          if (it.form === 'S2') put(c, it, small, 'ММ', it.y + 6);
          break;
        }
        case 'humidity':
          putIcon(c, it, small['◊'], small, roundDeci(v.num) + '%', it.y);
          break;
        case 'co2': {
          const s = String(roundDeci(v.num));
          const inv = it.thr > 0 && v.num >= it.thr;
          if (it.form === 'L') {
            const x = put(c, it, big, s, it.y);
            if (inv) invert(c, x - 1, it.y, x + textW(big, s), it.y + 10);
            break;
          }
          put(c, it, small, 'CO2', it.y);
          const x = put(c, it, small, s, it.y + 6);
          if (inv) invert(c, x - 1, it.y + 5, x + textW(small, s), it.y + 10);
          break;
        }
        case 'graph': {
          const hs = v.hours.slice(0, it.hours);
          const n = hs.length;
          const ts = hs.map((h) => h[0]);
          const mx = Math.max(...ts), mn = Math.min(...ts), sp = Math.max(1, mx - mn);
          const step = Math.max(1, Math.floor(it.w / (n - 1)));
          if (mn < 0 && mx > 0) for (let i = 0; i < it.w; i += 2) px(c, it.x + i, it.y + graphY(0, mn, sp));
          for (let i = 0; i < n - 1; i++) {
            line(c, it.x + i * step, it.y + graphY(ts[i], mn, sp), it.x + (i + 1) * step, it.y + graphY(ts[i + 1], mn, sp));
          }
          hs.forEach((h, i) => { if (h[1] >= 50) for (let k = -1; k <= 1; k++) px(c, it.x + i * step + k, it.y + 10); });
          break;
        }
        case 'clock': {
          const m = value(vars, it.var2);
          put(c, it, fontOf(it), p2(Math.trunc(v.num / 10) % 100) + ':' + p2(Math.trunc(m.num / 10) % 100), it.y);
          break;
        }
        case 'text': put(c, it, small, it.text || '', it.y); break;
        case 'picto': {
          const rows = picto(cfg, it.name, it.form === 'L');
          blit(c, rows, alignX(it, rows ? rows[0].length : 0), it.y);
          break;
        }
        case 'sep':
          for (let y = 0; y < 11; y++) if (!it.dashed || y % 2 === 0) px(c, it.x, it.y + y);
          break;
        default: break;
      }
    }

    // Renders a screen of a normalized config; `sod` = second of the day.
    function renderScreen(ncfg, screen, vars, sod = 0) {
      const f = fb();
      const sc = typeof screen === 'string' ? ncfg.screens.find((s) => s.id === screen) : ncfg.screens[screen];
      if (!sc) return f;
      for (const it of sc.items) {
        const r = resolve(it, vars);
        if (!r) continue;
        draw(ncfg, canvas(f, { x: it.x, y: it.y, w: it.w, h: it.h }), r, vars, sod);
      }
      return f;
    }

    function mobitec(f, addr = 0x06) {
      const m = [0xff, addr, 0xa2];
      for (let band = 0; band < Math.ceil(H / 4); band++) {
        m.push(0xd2, 0x00, 0xd3, band * 4 + 4, 0xd4, 0x77);
        for (let x = 0; x < W; x++) {
          let n = 0;
          for (let l = 0; l < 4; l++) { const y = band * 4 + l; if (y < H && f[y * W + x]) n |= 1 << l; }
          m.push(0x20 + n);
        }
      }
      let cs = 0; for (let i = 1; i < m.length; i++) cs += m[i]; cs &= 0xff;
      m.push(cs); if (cs === 0xfe) m.push(0x00); else if (cs === 0xff) { m[m.length - 1] = 0xfe; m.push(0x01); }
      m.push(0xff);
      return m;
    }

    function rectsOverlap(a, b) {
      return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    }

    return { W, H, TYPES, FORMS, VARS, normalize, normItem, renderScreen, defaultW, textW,
      mobitec, rotatorPick, condEval, value, picto, rectsOverlap, heightOf, fb, blit, text };
  }

  const api = { create, W, H, TYPES, FORMS, VARS };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.WSRender = api;
})(typeof window !== 'undefined' ? window : globalThis);

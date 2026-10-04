// ===== Табло 102x11: шрифты, иконки, экраны, кодер Mobitec =====
const W = 102, H = 11;

// --- Крупные цифры 6x11 (толщина штриха 2) ---
const BIG = {
  '0': ['.####.','##..##','##..##','##..##','##..##','##..##','##..##','##..##','##..##','##..##','.####.'],
  '1': ['..##..','.###..','####..','..##..','..##..','..##..','..##..','..##..','..##..','..##..','######'],
  '2': ['.####.','##..##','....##','....##','...##.','..##..','.##...','##....','##....','##....','######'],
  '3': ['.####.','##..##','....##','....##','..###.','....##','....##','....##','....##','##..##','.####.'],
  '4': ['...##.','..###.','.####.','##.##.','##.##.','##.##.','######','...##.','...##.','...##.','...##.'],
  '5': ['######','##....','##....','##....','#####.','....##','....##','....##','....##','##..##','.####.'],
  '6': ['.####.','##..##','##....','##....','#####.','##..##','##..##','##..##','##..##','##..##','.####.'],
  '7': ['######','....##','....##','...##.','...##.','..##..','..##..','..##..','.##...','.##...','.##...'],
  '8': ['.####.','##..##','##..##','##..##','.####.','##..##','##..##','##..##','##..##','##..##','.####.'],
  '9': ['.####.','##..##','##..##','##..##','##..##','.#####','....##','....##','....##','##..##','.####.'],
  '-': ['.....','.....','.....','.....','#####','#####','.....','.....','.....','.....','.....'],
  '+': ['......','......','..##..','..##..','######','######','..##..','..##..','......','......','......'],
  '°': ['.##.','#..#','#..#','.##.','....','....','....','....','....','....','....'],
  '.': ['..','..','..','..','..','..','..','..','..','##','##'],
  ' ': ['..','..','..','..','..','..','..','..','..','..','..'],
  ':': ['..','..','..','##','##','..','..','##','##','..','..'],
};

// --- Мелкий шрифт 5 строк, переменная ширина ---
const SM = {
  '0':['###','#.#','#.#','#.#','###'], '1':['.#.','##.','.#.','.#.','###'],
  '2':['###','..#','###','#..','###'], '3':['###','..#','.##','..#','###'],
  '4':['#.#','#.#','###','..#','..#'], '5':['###','#..','###','..#','###'],
  '6':['###','#..','###','#.#','###'], '7':['###','..#','..#','.#.','.#.'],
  '8':['###','#.#','###','#.#','###'], '9':['###','#.#','###','..#','###'],
  '-':['...','...','###','...','...'], '+':['...','.#.','###','.#.','...'],
  '.':['.','.','.','.','#'], ':':['.','#','.','#','.'], ' ':['.','.','.','.','.'],
  '%':['#.#','..#','.#.','#..','#.#'], '/':['..#','..#','.#.','#..','#..'],
  '°':['##','##','..','..','..'],
  '↑':['.#.','###','#.#','.#.','.#.'], '↓':['.#.','.#.','#.#','###','.#.'],
  'А':['.#.','#.#','###','#.#','#.#'], 'В':['##.','#.#','##.','#.#','##.'],
  'Д':['.###.','.#.#.','.#.#.','#####','#...#'], 'Е':['###','#..','##.','#..','###'],
  'З':['##.','..#','.#.','..#','##.'], 'Н':['#.#','#.#','###','#.#','#.#'],
  'О':['.#.','#.#','#.#','#.#','.#.'], 'Р':['##.','#.#','##.','#..','#..'],
  'С':['.##','#..','#..','#..','.##'], 'Т':['###','.#.','.#.','.#.','.#.'],
  'У':['#.#','#.#','.##','..#','##.'], 'Ч':['#.#','#.#','.##','..#','..#'],
  'Ь':['#..','#..','##.','#.#','##.'], 'М':['#...#','##.##','#.#.#','#...#','#...#'],
  'К':['#.#','#.#','##.','#.#','#.#'], 'П':['###','#.#','#.#','#.#','#.#'],
  'Л':['.##','#.#','#.#','#.#','#.#'], 'Г':['###','#..','#..','#..','#..'],
  'C':['.##','#..','#..','#..','.##'], 'O':['.#.','#.#','#.#','#.#','.#.'],
  // пиктограммы 5x5
  '☂':['.###.','#####','..#..','..#..','.##..'],
  '≈':['.##..','#..#.','...#.','.##.#','#..#.'],
  '⌂':['..#..','.###.','#####','.#.#.','.###.'],
  '◊':['..#..','.###.','#####','#####','.###.'],   // капля
  '!':['#','#','#','.','#'],
};

// стрелки ветра 5x5: куда дует (по 8 направлениям)
const WIND = {
  s : ['..#..','..#..','#.#.#','.###.','..#..'],
  n : ['..#..','.###.','#.#.#','..#..','..#..'],
  e : ['..#..','...#.','#####','...#.','..#..'],
  w : ['..#..','.#...','#####','.#...','..#..'],
  ne: ['..###','...##','..#.#','.#...','#....'],
  nw: ['###..','##...','#.#..','...#.','....#'],
  se: ['#....','.#...','..#.#','...##','..###'],
  sw: ['....#','...#.','#.#..','##...','###..'],
};
// метеонаправление "откуда" -> стрелка "куда"
const FROM2TO = {n:'s', s:'n', e:'w', w:'e', ne:'sw', nw:'se', se:'nw', sw:'ne'};

// --- Иконки погоды 11x11 ---
const CLOUD = ['....###...','...#xxx##.','.##xxxxxx#','#xxxxxxxx#','#xxxxxxxx#','.########.'];
const SUN_S = ['...#...','.#...#.','..###..','#.###.#','..###..','.#...#.','...#...'];
const MOON_S= ['.###...','##.....','#......','#......','##.....','.###...'];
const SUN = ['.....#.....','.#...#...#.','..#.....#..','....###....','...#####...',
             '##.#####.##','...#####...','....###....','..#.....#..','.#...#...#.','.....#.....'];
const MOON = ['...####....','..##.......','.##........','.##........','##.........','##.........',
              '##.........','.##........','.##.....#..','..###.###..','...#####...'];

function iconBuild(parts){ // [[pattern, x, y]] — 'x' стирает
  const g = Array.from({length:11},()=>Array(11).fill(0));
  for (const [pat,ox,oy] of parts) pat.forEach((row,y)=>[...row].forEach((c,x)=>{
    const X=ox+x, Y=oy+y; if (X<0||X>10||Y<0||Y>10) return;
    if (c==='#') g[Y][X]=1; else if (c==='x') g[Y][X]=0; }));
  return g.map(r=>r.map(v=>v?'#':'.').join(''));
}
const ICON = {
  clear: SUN,
  night: MOON,
  pcloud: iconBuild([[SUN_S,4,0],[CLOUD,0,5]]),
  pcloud_n: iconBuild([[MOON_S,5,0],[CLOUD,0,5]]),
  cloudy: iconBuild([[CLOUD,0,3]]),
  rain: iconBuild([[CLOUD,0,0],[['..#...#...#','.#...#...#.','...........','...#...#...'],0,7]]),
  snow: iconBuild([[CLOUD,0,0],[['..#.....#..','.###...###.','..#.....#..','.....#.....'],0,7]]),
  storm: iconBuild([[CLOUD,0,0],[['.....#.....','....#......','...####....','.....#.....','....#......'],0,6]]),
};
const HOUSE = ['.....#.....','....###....','...##.##...','..##...##..','.##.....##.',
               '##.......##','.#.......#.','.#..###..#.','.#..#.#..#.','.#..#.#..#.','.#########.'];

// --- Кадровый буфер ---
function fb(){ return new Uint8Array(W*H); }
function px(f,x,y,v=1){ if(x>=0&&x<W&&y>=0&&y<H) f[y*W+x]=v; }
function blit(f,pat,x,y){ pat.forEach((row,dy)=>[...row].forEach((c,dx)=>{ if(c==='#') px(f,x+dx,y+dy); })); }
function textW(font,s,gap=1){ let w=0; for(const ch of s){ const g=font[ch]; if(!g) continue; w+=g[0].length+gap; } return Math.max(0,w-gap); }
function text(f,font,s,x,y,gap=1){ for(const ch of s){ const g=font[ch]; if(!g) continue; blit(f,g,x,y); x+=g[0].length+gap; } return x-gap; }
function invert(f,x0,y0,x1,y1){ for(let y=y0;y<=y1;y++) for(let x=x0;x<=x1;x++) if(x>=0&&x<W&&y>=0&&y<H) f[y*W+x]^=1; }
function line(f,x0,y0,x1,y1){ const dx=Math.abs(x1-x0), dy=-Math.abs(y1-y0), sx=x0<x1?1:-1, sy=y0<y1?1:-1; let e=dx+dy;
  for(;;){ px(f,x0,y0); if(x0===x1&&y0===y1) break; const e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} } }

const sgn = t => t>0?'+':(t<0?'-':'');
function tempStr(t){ return sgn(Math.round(t))+Math.abs(Math.round(t)); }

// крупная температура: "+12°", без "+" если не влезает
function bigTemp(f,t,x,y=0,withPlus=true){
  const v=Math.round(t); let s=(v<0?'-':(v>0&&withPlus?'+':''))+Math.abs(v)+'°';
  return text(f,BIG,s,x,y);
}
function bigTempW(t,withPlus=true){ const v=Math.round(t); return textW(BIG,(v<0?'-':(v>0&&withPlus?'+':''))+Math.abs(v)+'°'); }

// ---- Модель данных (пример) ----
// d = { now:{t, cond, wind, windDir}, hourly:[{h, t, pop, cond}] x24, indoor:{t, rh, co2, p} }
function dayStats(d, hours=16){
  const hs=d.hourly.slice(0,hours); const ts=hs.map(h=>h.t);
  const rain = hs.find(h=>h.pop>=50);
  return { tmax:Math.max(...ts), tmin:Math.min(...ts), rainAt: rain?rain.h:null,
           rainEnd: rain ? (hs.slice(hs.indexOf(rain)).find(h=>h.pop<50)||{h:null}).h : null };
}

// ===== Экраны =====
// A. Симметричный: слева дождь/ветер, в центре иконка+температура, справа макс/мин
function screenA(d){
  const f=fb(), st=dayStats(d);
  // левая колонка
  text(f,SM,'☂',0,0);
  text(f,SM, st.rainAt===null ? '-' : (String(st.rainAt).padStart(2,'0')+'Ч'),7,0);
  blit(f,WIND[FROM2TO[d.now.windDir]],0,6);
  text(f,SM,Math.round(d.now.wind)+'М/С',7,6);
  // центр
  const cw = 11+3+bigTempW(d.now.t);
  const cx = Math.round((W-cw)/2);
  blit(f,ICON[d.now.cond],cx,0);
  bigTemp(f,d.now.t,cx+14,0);
  // правая колонка (выравнивание по правому краю)
  const r1='↑'+tempStr(st.tmax), r2='↓'+tempStr(st.tmin);
  text(f,SM,r1,W-textW(SM,r1),0);
  text(f,SM,r2,W-textW(SM,r2),6);
  return f;
}

// B. Сейчас + график на 12 часов (температура линией, осадки снизу, пунктир нуля)
function screenB(d){
  const f=fb();
  blit(f,ICON[d.now.cond],0,0);
  const tx=13; bigTemp(f,d.now.t,tx,0,false);
  const hs=d.hourly.slice(0,12); const ts=hs.map(h=>h.t);
  const tmax=Math.max(...ts), tmin=Math.min(...ts); const span=Math.max(1,tmax-tmin);
  const gx=44, step=4, gTop=0, gBot=7; // график 45 px шириной, строки 0..7
  const Y = t => Math.round(gBot - (t-tmin)/span*(gBot-gTop));
  if (tmin<0 && tmax>0){ const zy=Y(0); for(let x=gx;x<=gx+44;x+=2) px(f,x,zy); }
  for(let i=0;i<hs.length-1;i++) line(f,gx+i*step,Y(ts[i]),gx+(i+1)*step,Y(ts[i+1]));
  hs.forEach((h,i)=>{ const x=gx+i*step; if(h.pop>=50){ for(let k=-1;k<=1;k++) px(f,x+k,10); } if(h.pop>=80){ for(let k=-1;k<=1;k++) px(f,x+k,9);} });
  // засечки каждые 3 часа под графиком (строка 8) — только где нет осадков
  for(let i=0;i<=11;i+=3) px(f,gx+i*step,8);
  const a=tempStr(tmax), b=tempStr(tmin);
  text(f,SM,a,W-textW(SM,a),0); text(f,SM,b,W-textW(SM,b),6);
  return f;
}

// C. Сейчас -> следующая часть суток
function screenC(d, label='НОЧЬ', next={t:-7, cond:'night'}){
  const f=fb();
  blit(f,ICON[d.now.cond],0,0);
  let x=bigTemp(f,d.now.t,14,0)+4;
  blit(f,['#....','.#...','..#..','.#...','#....'],x,3); x+=8;
  blit(f,ICON[next.cond],x,0); x+=14;
  x=bigTemp(f,next.t,x,0)+3;
  // подпись — вертикально по центру в оставшемся месте
  const lw=textW(SM,label); text(f,SM,label,Math.max(x,W-lw),3);
  return f;
}

// D. Дома: домик, температура с десятыми, влажность и CO2 (инверсия при тревоге)
function screenD(d){
  const f=fb(); const i=d.indoor;
  blit(f,HOUSE,0,0);
  const t=(Math.round(i.t*10)/10).toFixed(1);
  let x=text(f,BIG,t,14,0); text(f,BIG,'°',x+1,0);
  const r1='◊'+' '+Math.round(i.rh)+'%';
  text(f,SM,r1,W-textW(SM,r1),0);
  const r2='CO'+' '+i.co2;
  const rx=W-textW(SM,r2);
  text(f,SM,r2,rx,6);
  if(i.co2>=1000) invert(f,rx-2,5,W-1,10);
  return f;
}

// E. Компактный «всё сразу»: иконка, темп., справа 2 строки: «↑+4 ↓-3» и «☂15Ч ≈5»
function screenE(d){
  const f=fb(), st=dayStats(d);
  blit(f,ICON[d.now.cond],0,0);
  const x0=bigTemp(f,d.now.t,14,0)+5;
  const l1='↑'+tempStr(st.tmax)+' ↓'+tempStr(st.tmin);
  const l2a = st.rainAt===null ? 'БЕЗ ОСАДКОВ' : (String(st.rainAt).padStart(2,'0')+'-'+String(st.rainEnd??'').padStart(2,'0')+'Ч');
  text(f,SM,l1,x0,0);
  let x=x0; blit(f,SM['☂'],x,6); x+=7; x=text(f,SM,l2a,x,6)+4;
  if (st.rainAt===null) x=x0+7+textW(SM,l2a)+4;
  const ww=textW(SM,Math.round(d.now.wind)+'М/С')+7;
  if (x+ww<=W){ blit(f,WIND[FROM2TO[d.now.windDir]],x,6); text(f,SM,Math.round(d.now.wind)+'М/С',x+7,6); }
  return f;
}


// ===== Дополнения для прошивки (генератор tools/glyphgen) =====
// Иконки погоды 5x5 для мелкой формы S
const ICON_S = {
  clear:   ['..#..','.###.','#####','.###.','..#..'],
  night:   ['.###.','##...','#....','##...','.###.'],
  pcloud:  ['..#.#','...#.','.##..','#..#.','####.'],
  pcloud_n:['...##','..#..','.##..','#..#.','####.'],
  cloudy:  ['.....','.##..','#..##','#...#','#####'],
  rain:    ['.##..','#####','.....','#.#.#','.#.#.'],
  snow:    ['.##..','#####','.....','#.#.#','#.#.#'],
  storm:   ['.##..','#####','..#..','.#...','..#..'],
};
// Стрелки тренда давления 5x5 (как в виджете «Давление»)
const TREND = {
  up:   ['..#..','.###.','#.#.#','..#..','..#..'],
  down: ['..#..','..#..','#.#.#','.###.','..#..'],
  flat: ['.....','.....','#####','.....','.....'],
};
// Пиктограммы библиотеки конструктора: 11x11 и 5x5
const PICTO = {
  home: { L: HOUSE, S: SM['⌂'] },
  umbrella: { L: ['.....#.....','...#####...','..#######..','.#########.','###########',
                  '#.#..#..#.#','.....#.....','.....#.....','.....#.....','..#..#.....','...##......'],
              S: SM['☂'] },
  drop: { L: ['.....#.....','....#.#....','....#.#....','...#...#...','..#.....#..','.#.......#.',
              '.#.......#.','.#.......#.','..#.....#..','...#...#...','....###....'],
          S: SM['◊'] },
  snowflake: { L: ['.....#.....','.#...#...#.','..#..#..#..','...#.#.#...','....###....','###########',
                   '....###....','...#.#.#...','..#..#..#..','.#...#...#.','.....#.....'],
               S: ['#.#.#','.###.','#####','.###.','#.#.#'] },
  warn: { L: ['....###....','....###....','....###....','....###....','....###....','....###....',
              '.....#.....','...........','...........','....###....','....###....'],
          S: ['..#..','..#..','..#..','.....','..#..'] },
  rink: { L: ['...........','.###.......','.#.#.......','.#.#.......','.#.####....','.#.....##..',
              '.#.......#.','.#########.','...#...#...','###########','...........'],
          S: ['##...','##...','#####','.....','#####'] },
  window: { L: ['###########','#....#....#','#....#....#','#....#....#','#....#....#','###########',
                '#....#....#','#....#....#','#....#....#','#....#....#','###########'],
            S: ['#####','#.#.#','#####','#.#.#','#####'] },
  nolink: { L: ['...........','....###....','...#...##..','.##......#.','#.........#','.#########.',
                '...#...#...','....#.#....','.....#.....','....#.#....','...#...#...'],
            S: ['#...#','.#.#.','..#..','.#.#.','#...#'] },
  skid: { L: ['...#####...','..#.....#..','.#########.','.#.##.##.#.','.#########.','..##...##..',
              '...........','.##....##..','#..#..#..#.','....##....#','...........'],
          S: ['.###.','#####','.#.#.','.....','#.#.#'] },
};

// ===== Кодер Mobitec (по futaba.py: адрес 06, бит0 = верхний пиксель полосы) =====
function mobitec(f, addr=0x06){
  const m=[0xff,addr,0xa2];
  for(let band=0; band<Math.ceil(H/4); band++){
    m.push(0xd2,0x00,0xd3,band*4+4,0xd4,0x77);
    for(let x=0;x<W;x++){ let n=0; for(let l=0;l<4;l++){ const y=band*4+l; if(y<H && f[y*W+x]) n|=1<<l; } m.push(0x20+n); }
  }
  let cs=0; for(let i=1;i<m.length;i++) cs+=m[i]; cs&=0xff;
  m.push(cs); if(cs===0xfe) m.push(0x00); else if(cs===0xff){ m[m.length-1]=0xfe; m.push(0x01); }
  m.push(0xff); return m;
}


// ===== Режим «слоты»: центр постоянный, боковые виджеты меняются раз в минуту =====
const SLOT_W = 30, LEFT_X = 0, RIGHT_X = W - SLOT_W;   // 0..29 и 72..101, центр 31..70
// виджет рисует две строки (или график) в коробку 30x11; align: 'l' | 'r'
function put(f,s,x0,y,align){ const w=textW(SM,s); text(f,SM,s, align==='l'?x0:x0+SLOT_W-w, y); }
function putIcon(f,ic,s,x0,y,align){ const w=5+2+textW(SM,s); const x= align==='l'?x0:x0+SLOT_W-w; blit(f,ic,x,y); text(f,SM,s,x+7,y); }
const p2 = h => String(h).padStart(2,'0');
const WIDGETS = {
  range: { name:'Макс/мин', draw:(f,d,x,a)=>{ const st=dayStats(d); put(f,'↑'+tempStr(st.tmax),x,0,a); put(f,'↓'+tempStr(st.tmin),x,6,a); } },
  rain:  { name:'Осадки', draw:(f,d,x,a)=>{ const st=dayStats(d);
           if(st.rainAt===null){ putIcon(f,SM['☂'],'НЕТ',x,0,a); put(f,'16Ч',x,6,a); }
           else { putIcon(f,SM['☂'],p2(st.rainAt)+'Ч',x,0,a); put(f,'ДО '+p2(st.rainEnd??'')+'Ч',x,6,a); } } },
  wind:  { name:'Ветер', draw:(f,d,x,a)=>{ putIcon(f,WIND[FROM2TO[d.now.windDir]],String(Math.round(d.now.wind)),x,0,a); put(f,'М/С',x,6,a); } },
  press: { name:'Давление', draw:(f,d,x,a)=>{ const p=Math.round(d.indoor.p); const tr=d.indoor.ptrend; putIcon(f, tr>0?['..#..','.###.','#.#.#','..#..','..#..']:tr<0?['..#..','..#..','#.#.#','.###.','..#..']:['.....','.....','#####','.....','.....'], String(p),x,0,a); put(f,'ММ',x,6,a); } },
  home:  { name:'Дома', draw:(f,d,x,a)=>{ putIcon(f,SM['⌂'],(Math.round(d.indoor.t*10)/10).toFixed(1)+'°',x,0,a); putIcon(f,SM['◊'],Math.round(d.indoor.rh)+'%',x,6,a); } },
  co2:   { name:'CO2', draw:(f,d,x,a)=>{ put(f,'CO2',x,0,a); const s=String(d.indoor.co2); put(f,s,x,6,a);
           if(d.indoor.co2>=1000){ const w=textW(SM,s); const sx=a==='l'?x:x+SLOT_W-w; invert(f,sx-1,5,sx+w,10); } } },
  graph: { name:'График 12 ч', draw:(f,d,x)=>{ const hs=d.hourly.slice(0,11), ts=hs.map(h=>h.t);
           const mx=Math.max(...ts), mn=Math.min(...ts), sp=Math.max(1,mx-mn), Y=t=>Math.round(7-(t-mn)/sp*7);
           if(mn<0&&mx>0){ for(let i=0;i<SLOT_W;i+=2) px(f,x+i,Y(0)); }
           for(let i=0;i<hs.length-1;i++) line(f,x+i*3,Y(ts[i]),x+(i+1)*3,Y(ts[i+1]));
           hs.forEach((h,i)=>{ if(h.pop>=50) for(let k=-1;k<=1;k++) px(f,x+i*3+k,10); }); } },
};
function screenSlots(d, leftKey, rightKey){
  const f=fb();
  const cw=11+3+bigTempW(d.now.t), cx=Math.round((W-cw)/2);
  blit(f,ICON[d.now.cond],cx,0); bigTemp(f,d.now.t,cx+14,0);
  if(leftKey)  WIDGETS[leftKey].draw(f,d,LEFT_X,'l');
  if(rightKey) WIDGETS[rightKey].draw(f,d,RIGHT_X,'r');
  return f;
}
function diff(a,b){ let dots=0; const cols=new Set(); for(let i=0;i<a.length;i++) if(a[i]!==b[i]){ dots++; cols.add(i%W); } return {dots, cols:cols.size}; }

if (typeof module!=='undefined') module.exports={W,H,ICON,screenA,screenB,screenC,screenD,screenE,mobitec,screenSlots,WIDGETS,diff,
  BIG,SM,WIND,FROM2TO,HOUSE,ICON_S,TREND,PICTO,fb,px,blit,text,textW,invert,line,bigTemp,bigTempW,tempStr,put,putIcon,SLOT_W,dayStats};

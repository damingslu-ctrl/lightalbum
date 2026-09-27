/* 灯箱高清查看器 运行时回归测试
 * 加载 index.html 的真实脚本（非重实现），用可控 DOM 桩：
 *  - #lbStage 的 getBoundingClientRect 固定为 800x500
 *  - #lbImgEl 可设 naturalWidth/Height，并捕获 style.transform
 * 目的：验证「双重缩小」已消除（lbFit 成为唯一尺寸控制）、
 *       以及 transform-origin:center 下的光标锚点缩放公式正确（锚点不漂移）。
 */
const fs = require('fs');
const N = process.cwd();
const FILE = process.argv[2] || 'index.html';
const h = fs.readFileSync(N + '/' + FILE, 'utf8');
const js = h.match(/<script>([\s\S]*)<\/script>/)[1];

const STAGE = { top: 0, left: 0, width: 800, height: 500, right: 800, bottom: 500 };

function makeEl(tag) {
  const el = {
    tagName: (tag || 'div').toUpperCase(), dataset: {}, children: [], _html: '',
    naturalWidth: 0, naturalHeight: 0, onload: null, onerror: null, src: '',
    style: { _v: {}, setProperty(k, v) { this._v[k] = String(v); }, getPropertyValue(k) { return this._v[k] || ''; } },
    classList: { _s: new Set(), add(...c) { c.forEach(x => this._s.add(x)); }, remove(...c) { c.forEach(x => this._s.delete(x)); },
      toggle(c, f) { if (f === undefined) f = !this._s.has(c); f ? this._s.add(c) : this._s.delete(c); return f; }, contains(c) { return this._s.has(c); } },
    setAttribute() {}, getAttribute() { return null; }, removeAttribute() {},
    // 记录监听器，便于用例直接触发某个按钮的 click（跨作用域入口回归用）
    addEventListener(t, fn) { (this._ls = this._ls || {})[t] = fn; },
    removeEventListener() {}, focus() {}, blur() {}, click() {}, remove() {},
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; }, removeChild() {},
    insertBefore(c) { this.children.push(c); c.parentNode = this; return c; },
    querySelector() { return makeEl(); }, querySelectorAll() { return []; },
    getBoundingClientRect() { return { top: 0, left: 0, width: 0, height: 0, right: 0, bottom: 0 }; },
    scrollIntoView() {}, closest() { return null; }, contains() { return false; }, setAttributeNS() {}
  };
  Object.defineProperty(el, 'innerHTML', { get() { return this._html; }, set(v) { this._html = String(v); } });
  Object.defineProperty(el, 'textContent', { get() { return this._txt || ''; }, set(v) { this._txt = String(v); } });
  Object.defineProperty(el, 'hidden', { get() { return !!this._hidden; }, set(v) { this._hidden = !!v; } });
  return el;
}
const cache = new Map();
function byId(id) {
  if (!cache.has(id)) {
    const e = makeEl();
    if (id === 'lbStage') e.getBoundingClientRect = () => STAGE;
    cache.set(id, e);
  }
  return cache.get(id);
}
const doc = {
  getElementById: byId, querySelector() { return makeEl(); }, querySelectorAll() { return []; },
  addEventListener() {}, removeEventListener() {}, createElement: t => makeEl(t),
  body: makeEl('body'), documentElement: makeEl('html'), activeElement: null, hidden: false
};
const win = {
  matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }),
  addEventListener() {}, removeEventListener() {}, innerWidth: 1400, innerHeight: 900, scrollY: 0,
  requestAnimationFrame: () => 0, cancelAnimationFrame() {}, IntersectionObserver: undefined,
  location: { search: '', hash: '', href: 'file:///index.html', protocol: 'file:' },
  navigator: { geolocation: null, userAgent: 'node' }, __native: undefined, __nativeAbs: undefined
};
const store = {};
const LS = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); }, removeItem: k => { delete store[k]; } };
function ImageCtor() { this.src = ''; }

let api;
try {
  api = new Function('window', 'document', 'requestAnimationFrame', 'cancelAnimationFrame', 'localStorage',
    'navigator', 'location', 'IntersectionObserver', 'setTimeout', 'addEventListener', 'Image', 'fetch',
    js + '\n;return { openLightbox, lbFit, lbZoom, lbApplyView, filteredPhotos, loadData, PHOTOS, state, normalizePhoto, setupRetouchLab,' +
    'get lbView(){return lbView}, get lbImgEl(){return document.getElementById("lbImgEl")},' +
    'get lbStage(){return document.getElementById("lbStage")} };'
  )(win, doc, win.requestAnimationFrame, win.cancelAnimationFrame, LS, win.navigator, win.location,
    undefined, (f) => 0, () => {}, ImageCtor, () => Promise.reject());
} catch (e) { console.log('LOAD_FAIL ' + e.message); process.exit(1); }

// 网页版无示例数据（SAMPLE_PHOTOS=[]）：直接经 normalizePhoto 塞入最小合法作品，绕过 localStorage 路径
api.PHOTOS.length = 0;
api.PHOTOS.push(api.normalizePhoto({ id: 1, t: '测试作品', s: '黄金时刻', y: 2024, o: 'landscape',
  r: 'china', cam: 'X', lens: 'Y', focal: 24, ap: 'f/8', sh: '1/100', iso: 100, d: '2024-01-01', tag: '测试',
  img: 'data:image/png;base64,', orig: 'data:image/png;base64,', src: '', rel: '' }));
api.PHOTOS.push(api.normalizePhoto({ id: 2, t: '第二幅', s: '蓝调时刻', y: 2023, o: 'portrait',
  r: 'norway', cam: 'X', lens: 'Y', focal: 50, ap: 'f/4', sh: '1/200', iso: 200, d: '2023-06-01', tag: 't2',
  img: 'data:image/png;base64,', orig: 'data:image/png;base64,', src: '', rel: '' }));
console.log('PHOTOS=' + api.PHOTOS.length);

function parseTransform(t) {
  const m = /translate\(([-\d.]+)px,\s*([-\d.]+)px\)\s*scale\(([-\d.]+)\)/.exec(t || '');
  return m ? { tx: +m[1], ty: +m[2], scale: +m[3] } : null;
}
function setNat(w, hh) { const el = api.lbImgEl; el.naturalWidth = w; el.naturalHeight = hh; el.onload && el.onload(); }

let pass = 0, fail = 0;
function assert(name, cond, extra) { if (cond) { pass++; console.log('  PASS ' + name); } else { fail++; console.log('  FAIL ' + name + (extra ? '  ' + extra : '')); } }

/* ---- 用例 1：大图（4000x3000）在 800x500 舞台，lbFit 应只缩放不双重缩小 ---- */
console.log('[1] 大图 lbFit');
api.openLightbox(api.PHOTOS[0].id);
setNat(4000, 3000);
let tr = parseTransform(api.lbImgEl.style.transform);
let expectScale = Math.min(800 / 4000, 500 / 3000);  // = 0.1667（被 1 上限截断，仍是 0.1667）
assert('lbFit 缩放值=0.1667', Math.abs(tr.scale - expectScale) < 1e-3, 'got ' + tr.scale);
assert('lbFit 居中 tx=0', Math.abs(tr.tx) < 1e-6, 'got ' + tr.tx);
assert('lbFit 居中 ty=0', Math.abs(tr.ty) < 1e-6, 'got ' + tr.ty);
// 关键：用该 scale 渲染出的宽 = 4000*0.1667 = 666px（落在舞台内且接近满宽，而非被 CSS 再压一次成 ~111px）
let renderedW = 4000 * tr.scale;
assert('渲染宽度≈666（未被双重缩小）', Math.abs(renderedW - 666.7) < 1, 'got ' + renderedW.toFixed(1));

/* ---- 用例 2：小图（200x150）在 800x500 舞台，lbFit 不应放大于 1（避免模糊） ---- */
console.log('[2] 小图 lbFit（不放大）');
api.openLightbox(api.PHOTOS[1] ? api.PHOTOS[1].id : api.PHOTOS[0].id);
setNat(200, 150);
tr = parseTransform(api.lbImgEl.style.transform);
assert('lbFit 小图 scale 上限=1', Math.abs(tr.scale - 1) < 1e-6, 'got ' + tr.scale);

/* ---- 用例 3：光标锚点缩放——放大到 2x 后，光标下的同一点仍落在光标处 ---- */
console.log('[3] 光标锚点缩放（center origin）');
api.openLightbox(api.PHOTOS[0].id);
setNat(4000, 3000);                      // fit 后 lbView={scale:0.1667,tx:0,ty:0}
const cursor = { x: 600, y: 400 };       // 舞台内某点
// 放大前该光标对应的图片局部坐标 cp（相对图片中心）
const s0 = api.lbView.scale, tx0 = api.lbView.tx, ty0 = api.lbView.ty;
const cx = 800 / 2, cy = 500 / 2;
const cpx = (cursor.x - cx - tx0) / s0, cpy = (cursor.y - cy - ty0) / s0;
api.lbZoom(2, cursor.x, cursor.y);
tr = parseTransform(api.lbImgEl.style.transform);
// 放大后该局部点应仍映射到同一屏幕坐标
const sx = cx + api.lbView.tx + api.lbView.scale * cpx;
const sy = cy + api.lbView.ty + api.lbView.scale * cpy;
assert('放大2x后光标锚点x不变', Math.abs(sx - cursor.x) < 1e-6, 'sx=' + sx);
assert('放大2x后光标锚点y不变', Math.abs(sy - cursor.y) < 1e-6, 'sy=' + sy);
assert('放大2x后 scale=0.3333', Math.abs(api.lbView.scale - 0.3333) < 1e-3, 'got ' + api.lbView.scale);

/* ---- 用例 4：中心缩放应保持居中（tx,ty 不变） ---- */
console.log('[4] 中心缩放保持居中');
api.openLightbox(api.PHOTOS[0].id);
setNat(4000, 3000);
api.lbZoom(3);                            // 无光标 → 默认舞台中心
tr = parseTransform(api.lbImgEl.style.transform);
assert('中心放大 tx=0', Math.abs(tr.tx) < 1e-6, 'got ' + tr.tx);
assert('中心放大 ty=0', Math.abs(tr.ty) < 1e-6, 'got ' + tr.ty);

/* ---- 用例 5：缩放下限保护（不会变负/零） ---- */
console.log('[5] 缩放下限');
api.openLightbox(api.PHOTOS[0].id);
setNat(4000, 3000);
for (let i = 0; i < 20; i++) api.lbZoom(0.1);
assert('连续缩小后 scale>=0.2', api.lbView.scale >= 0.2 - 1e-9, 'got ' + api.lbView.scale);

/* ---- 用例 6：修图动态进度条 ---- */
console.log('[6] 修图动态进度条');
try { api.setupRetouchLab(); } catch (e) { console.log('  (setupRetouchLab: ' + e.message + ')'); }
const progEl = doc.getElementById('rlProg');
const progFill = doc.getElementById('rlProgFill');
const progTxt = doc.getElementById('rlProgTxt');
assert('HTML 里进度条默认 hidden', /id="rlProg" hidden/.test(h), 'no hidden attr in markup');
if (typeof win.__onRetouchProgress === 'function') {
  win.__onRetouchProgress({ done: 3, total: 10, name: 'a.jpg' });
  assert('收到进度后显示', progEl.hidden === false);
  assert('宽度=30%', progFill.style.width === '30%', 'got ' + progFill.style.width);
  assert('文案含 3 / 10', /3 \/ 10/.test(String(progTxt.textContent)), progTxt.textContent);
  win.__onRetouchProgress({ done: 10, total: 10, name: 'b.jpg' });
  assert('完成时宽度=100%', progFill.style.width === '100%', 'got ' + progFill.style.width);
} else {
  assert('__onRetouchProgress 已定义', false, 'undefined');
}

/* ---- 用例 7：内嵌编辑器的跨作用域入口 ----
   回归背景：编辑器的 openEditor 定义在独立 IIFE 里，灯箱的「✏️ 内嵌编辑」按钮在
   **另一个 IIFE** 里。曾经直接写 `openEditor()` → ReferenceError → 点了「没反应」。
   现在必须走 window.__openEditor。本用例真实触发灯箱按钮的 click 处理器。 */
console.log('[7] 内嵌编辑跨作用域入口');
assert('window.__openEditor 已暴露', typeof win.__openEditor === 'function', typeof win.__openEditor);
// 造出「本机 + 有路径」的条件，让处理器真的走到 openEditor 里（而不是被门控提前 return）
win.__native = { call() {}, on() {} };
api.PHOTOS[0].path = 'C:\\fake\\a.jpg';
api.openLightbox(api.PHOTOS[0].id);
const edBtn = doc.getElementById('lbEdit');
let clickErr = null;
try { if (edBtn._ls && edBtn._ls.click) edBtn._ls.click(); else clickErr = new Error('未注册 click 监听'); }
catch (e) { clickErr = e; }
assert('点击灯箱「内嵌编辑」不抛错', !clickErr, clickErr && clickErr.message);
// 退化路径：组件缺失时给出**可见**提示（toast），而不是静默无反应
assert('缺 fabric 时写入可见提示', /组件未加载|无可用图像源|本机路径/.test(String(doc.getElementById('edToast').textContent || doc.getElementById('srcMsg').textContent || '')),
  String(doc.getElementById('edToast').textContent || ''));
delete win.__native;

console.log('VIEWER_TEST pass=' + pass + ' fail=' + fail);
process.on('unhandledRejection', () => {});
setTimeout(() => process.exit(fail ? 1 : 0), 30);

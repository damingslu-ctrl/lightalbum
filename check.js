const fs = require('fs');
const N = process.cwd();
const h = fs.readFileSync(N + '/index.html', 'utf8');

/* ---------- 0. 让「打印过 *_FAIL」真的算失败 ----------
   历史问题：脚本末尾写死 `process.exit(0)`，于是所有 `XXX_FAIL` 只打印不影响退出码，
   挂在 run_all_tests 里等于「永远绿」。这里统一拦截：只要打印过失败标记就 FATAL，
   最终以非零退出。 */
let FATAL = 0;
const _rawLog = console.log;
console.log = function (...a) {
  const s = a.map(String).join(' ');
  if (/(^|\s)[A-Z][A-Z0-9_]*_FAIL\b/.test(s) || s.includes('=FAIL') || s.includes('TAIL_ERROR')) FATAL++;
  _rawLog.apply(console, a);
};

/* ---------- 1. 语法 ---------- */
const js = h.match(/<script>([\s\S]*)<\/script>/)[1];
try { new Function(js); console.log('SYNTAX_OK len=' + js.length); }
catch (e) { console.log('SYNTAX_FAIL ' + e.message); process.exit(1); }

/* ---------- 2. ID 存在性 ---------- */
// 动态创建的 id（JS 里 createElement 后赋 id）不在 HTML 静态标记里，属正常
const DYN_IDS = new Set(['galMore', 'edToast']);   // JS 动态创建（edToast = 编辑器提示浮层）
const ids = [...new Set([...js.matchAll(/getElementById\('([^']+)'\)/g)].map(x => x[1]))];
const miss = ids.filter(i => !DYN_IDS.has(i) && h.indexOf('id="' + i + '"') < 0);
console.log('IDS=' + ids.length + ' MISSING=' + (miss.length ? miss.join(',') : 'none'));

/* ---------- 3. 关键函数 ---------- */
const fns = ['photoXY', 'applyMapZoom', 'renderPhotoMarkers', 'renderMap', 'selectRegion', 'selectProvince',
  'mapTo', 'mapBack', 'buildProvinces', 'renderProvLabels', 'renderMapCrumb', 'renderRegionList', 'provCityTree',
  'provCounts', 'regionBBox', 'provBBox', 'fitZoom', 'invZ', 'escHtml', 'rippleAt', 'showProvTip', 'hideProvTip',
  'highlightProvArc', 'markProvSelection', 'buildArcs', 'highlightArcs', 'clearArcs', 'placeMarks', 'mapTick',
  'startMap', 'provName', 'provincesOf', 'provByName', 'ringsBBox', 'arcXY', 'arcD', 'curZ', 'driftTx', 'normalizePhoto'];
const fmiss = fns.filter(f => js.indexOf('function ' + f + '(') < 0);
console.log('FN_MISSING=' + (fmiss.join(',') || 'none'));

/* ---------- 3b. 跨作用域桥接自洽 ----------
   本页所有 JS 都在同一个 <script> 里，但有多个 IIFE。IIFE 私有函数被别的 IIFE
   直接调用会 ReferenceError（点了按钮「没反应」且控制台才看得到）。约定：跨作用域
   一律走 window.__xxx 桥。这里校验「用到的 window.__xxx 必须在页内被赋值」，
   外部注入的（native_bridge.js 提供）列入白名单。 */
const EXT_BRIDGE = new Set(['__native', '__nativeAbs', '__libPort', '__SMART_PAGE__']);
const usedB = new Set([...js.matchAll(/window\.(__[A-Za-z0-9_$]+)/g)].map(x => x[1]));
const hasB = new Set([...js.matchAll(/window\.(__[A-Za-z0-9_$]+)\s*=/g)].map(x => x[1]));
const bmiss = [...usedB].filter(n => !hasB.has(n) && !EXT_BRIDGE.has(n));
console.log('BRIDGE_MISSING=' + (bmiss.join(',') || 'none'));

/* ---------- 3c. 内联 fabric 完整性（历史事故守护） ----------
   事故复盘：编辑器 CSS 曾被插入到 **fabric 源码自己字符串里的第一个
   `]]></style>`** 处 —— 后果 ①fabric 的 JS 字符串跨了真实换行 → SyntaxError →
   `fabric` 未定义（症状：点「内嵌编辑」提示「编辑器组件未加载」）；②那段 CSS 落在
   <script> 文本里，从未当作 CSS 生效。注意：主 <script> 的正则**抓不到** fabric 块
   （它在另一个 <script id="fabric-lib"> 里），所以必须单独校验。 */
const fbTag = '<script id="fabric-lib">';
const fbI = h.indexOf(fbTag);
if (fbI < 0) {
  console.log('FABRIC_BLOCK=FAIL (缺少 <script id="fabric-lib">)');
} else {
  const fbBody = h.slice(fbI + fbTag.length, h.indexOf('</script>', fbI));
  let parseErr = '';
  try { new Function(fbBody); } catch (e) { parseErr = e.message; }
  console.log('FABRIC_SYNTAX=' + (parseErr ? 'FAIL ' + parseErr : 'OK'));
  // 真跑一遍：在 vm 里执行，必须真的挂出 fabric.Canvas（这才叫「能用」）
  let runErr = '', defines = false;
  try {
    const vm = require('vm');
    const ctx = {}; vm.createContext(ctx);
    vm.runInContext(fbBody, ctx);
    defines = !!(ctx.fabric && typeof ctx.fabric.Canvas === 'function');
  } catch (e) { runErr = e.message; }
  console.log('FABRIC_LOADS=' + (defines ? 'OK' : 'FAIL ' + runErr));
  // 与 pristine vendor 逐字比对（甩掉内联时加的头注释与 sourceMappingURL）
  const vPath = N + '/client/resources/vendor/fabric.min.js';
  let match = 'no-vendor';
  if (fs.existsSync(vPath)) {
    const norm = s => s.replace(/^\s*\/\*![\s\S]*?\*\//, '')
                       .replace(/\/\/# sourceMappingURL=\S*\s*$/, '').trim();
    match = String(norm(fbBody) === norm(fs.readFileSync(vPath, 'utf8')));
  }
  console.log('FABRIC_MATCH_VENDOR=' + match);
}
// 编辑器 CSS 必须待在真正的 <style> 里（第一个 <script> 之前），否则等于没写
const edPos = h.indexOf('.ed{position:relative');
const firstScript = h.indexOf('<script');
console.log('ED_CSS_IN_STYLE=' + ((edPos > 0 && edPos < firstScript) ? 'OK' : 'FAIL'));

/* ---------- 4. 零外链 ---------- */
const ext = [...h.matchAll(/(?:src|href)="(https?:)?\/\//g)].map(m => m[0]);
console.log('EXTERNAL_REFS=' + (ext.length ? ext.join(' ') : 'none'));

/* ---------- 5. 文件体量 ---------- */
console.log('FILE_KB=' + Math.round(h.length / 1024));

/* ---------- 6. DOM stub 冒烟：走真实渲染路径 ---------- */
function makeEl(tag) {
  const el = {
    tagName: (tag || 'div').toUpperCase(), dataset: {}, children: [], _html: '',
    // style 必须支持 setProperty：下钻时会把 --z 写进 #mapZoom 供描边按 1/Z 补偿
    style: { _v: {}, setProperty(k, v) { this._v[k] = String(v); this[k] = String(v); },
      getPropertyValue(k) { return this._v[k] || ''; }, removeProperty(k) { delete this._v[k]; delete this[k]; } },
    classList: { _s: new Set(), add(...c) { c.forEach(x => this._s.add(x)); }, remove(...c) { c.forEach(x => this._s.delete(x)); },
      toggle(c, f) { if (f === undefined) f = !this._s.has(c); f ? this._s.add(c) : this._s.delete(c); return f; }, contains(c) { return this._s.has(c); } },
    setAttribute() {}, getAttribute() { return null; }, removeAttribute() {},
    addEventListener() {}, removeEventListener() {}, focus() {}, blur() {}, click() {},
    remove() {   // 图库「加载更多」哨兵会调用；桩里必须实现，否则报 not a function
      if (this.parentNode && this.parentNode.children) {
        const i = this.parentNode.children.indexOf(this);
        if (i >= 0) this.parentNode.children.splice(i, 1);
      }
      this.parentNode = null;
    },
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; }, removeChild() {},
    insertBefore(c, ref) {
      const i = ref ? this.children.indexOf(ref) : -1;
      if (i >= 0) this.children.splice(i, 0, c); else this.children.push(c);
      c.parentNode = this; return c;
    },
    querySelector() { return makeEl(); }, querySelectorAll() { return []; },
    getBoundingClientRect() { return { top: 0, left: 0, width: 1000, height: 500, right: 1000, bottom: 500 }; },
    scrollIntoView() {}, closest() { return null; }, contains() { return false; }
  };
  Object.defineProperty(el, 'innerHTML', { get() { return this._html; }, set(v) { this._html = String(v); } });
  Object.defineProperty(el, 'textContent', { get() { return this._txt || ''; }, set(v) { this._txt = String(v); } });
  return el;
}
const cache = new Map();
function byId(id) { if (!cache.has(id)) cache.set(id, makeEl()); return cache.get(id); }
const doc = {
  getElementById: byId,
  querySelector() { return makeEl(); },
  querySelectorAll() { return []; },
  addEventListener() {}, removeEventListener() {},
  createElement: t => makeEl(t),
  body: makeEl('body'),
  documentElement: makeEl('html'),
  activeElement: null,
  hidden: false
};
const win = {
  matchMedia: () => ({ matches: false, addEventListener() {}, removeEventListener() {} }),
  addEventListener() {}, removeEventListener() {},
  innerWidth: 1400, innerHeight: 900, scrollY: 0,
  requestAnimationFrame: () => 0, cancelAnimationFrame() {},
  location: { search: '', hash: '', href: 'file:///index.html', protocol: 'file:' },
  navigator: { geolocation: null, userAgent: 'node' },
  IntersectionObserver: undefined
};
const store = {};
const LS = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); }, removeItem: k => { delete store[k]; } };

let api;
try {
  api = new Function('window', 'document', 'requestAnimationFrame', 'cancelAnimationFrame',
    'localStorage', 'navigator', 'location', 'IntersectionObserver', 'setTimeout', 'addEventListener',
    js + '\n;return { state, refreshAll, loadData, mapTo, mapBack, selectRegion, selectProvince, renderMap, ' +
    'applyMapZoom, buildProvinces, renderProvLabels, renderMapCrumb, provCityTree, provCounts, provincesOf, ' +
    'regionBBox, provBBox, fitZoom, escHtml, provName, get PROVINCES(){return PROVINCES}, get PHOTOS(){return PHOTOS}, ' +
    'get REGIONS(){return REGIONS}, get PROV_COUNT(){return Object.keys(PROVINCES).length} };'
  )(win, doc, win.requestAnimationFrame, win.cancelAnimationFrame, LS, win.navigator, win.location, undefined, (f) => 0, () => {});
} catch (e) { console.log('SMOKE_FAIL ' + e.message); process.exit(1); }

api.loadData();
console.log('PROV_COUNT=' + api.PROV_COUNT + ' PHOTOS=' + api.PHOTOS.length + ' REGIONS=' + api.REGIONS.length);

/* --- 渲染世界级 --- */
try { api.refreshAll(); console.log('RENDER_LV0_OK'); } catch (e) { console.log('RENDER_LV0_FAIL ' + e.message); }
/* --- 下钻到中国 --- */
try {
  api.selectRegion('china');
  console.log('LV1 china level=' + api.state.mapLevel + ' Z=' + (api.state._Z || 1).toFixed(2) +
    ' provs=' + api.provincesOf('china').length);
  const bb = api.regionBBox('china');
  console.log('  regionBBox cx=' + bb.cx.toFixed(1) + ' cy=' + bb.cy.toFixed(1) + ' w=' + bb.w.toFixed(1) + ' h=' + bb.h.toFixed(1));
} catch (e) { console.log('LV1_FAIL ' + e.message); }

/* --- 下钻到湖南省 --- */
const HU = api.provincesOf('china').find(p => /Hunan/i.test(p[0]))[0];
try {
  api.selectProvince(HU);
  const pb = api.provBBox('china', HU);
  console.log('LV2 ' + HU + ' level=' + api.state.mapLevel + ' Z=' + (api.state._Z || 1).toFixed(2) +
    ' 中文=' + api.provName(HU) + ' bbox_w=' + (pb ? pb.w.toFixed(2) : 'null'));
} catch (e) { console.log('LV2_FAIL ' + e.message); }

/* --- 逐级退回 --- */
try {
  api.mapBack(); console.log('BACK1 level=' + api.state.mapLevel + ' prov=' + api.state.prov);
  api.mapBack(); console.log('BACK2 level=' + api.state.mapLevel + ' region=' + api.state.region);
} catch (e) { console.log('BACK_FAIL ' + e.message); }

/* --- 各省 bbox 与缩放健全性 --- */
try {
  let bad = 0, checked = 0;
  for (const rid of ['iceland', 'norway', 'morocco', 'namibia', 'china', 'japan', 'newzealand', 'usa', 'peru', 'bolivia']) {
    const list = api.provincesOf(rid);
    for (const p of list) {
      const bb = api.provBBox(rid, p[0]);
      checked++;
      if (!bb || !isFinite(bb.cx) || !isFinite(bb.cy) || bb.w < 0) { bad++; if (bad < 4) console.log('   BAD bbox ' + rid + '/' + p[0]); continue; }
      const Z = api.fitZoom(bb, 1.75, 1, 70);
      if (!isFinite(Z) || Z <= 0) { bad++; if (bad < 6) console.log('   BAD Z ' + rid + '/' + p[0] + ' -> ' + Z); }
    }
  }
  console.log('PROV_BBOX checked=' + checked + ' bad=' + bad);
} catch (e) { console.log('BBOX_FAIL ' + e.message); }

/* --- 逐省下钻一遍，确保不崩 --- */
try {
  let n = 0;
  for (const rid of ['iceland', 'norway', 'usa', 'japan', 'china']) {
    api.selectRegion(rid);
    const cnt = api.provCounts(rid);
    const keys = Object.keys(cnt);
    for (const pv of keys) { api.selectProvince(pv); n++; }
  }
  api.mapTo(0);
  console.log('DRILL_ALL_OK 下钻次数=' + n + ' 末态 level=' + api.state.mapLevel);
} catch (e) { console.log('DRILL_FAIL ' + e.message); }

/* --- 转义安全 --- */
console.log('ESC=' + api.escHtml('<img src=x onerror="1">'));

/* --- 数据完整性：照片 pv 必须能在 PROVINCES 里找到；中文名必须齐备 --- */
try {
  const P = api.PROVINCES;
  const all = new Set();
  Object.values(P).forEach(l => l.forEach(p => all.add(p[0])));
  const badPv = api.PHOTOS.filter(p => !p.pv || !(P[p.r] || []).some(q => q[0] === p.pv));
  const noZh = [...all].filter(n => api.provName(n) === n);
  console.log('INTEGRITY 省份总数=' + all.size + ' 照片pv缺失=' + badPv.length + ' 无中文名=' + noZh.length +
    (noZh.length ? ' → ' + noZh.join(' | ') : ''));
  if (badPv.length) badPv.forEach(p => console.log('   BAD pv: #' + p.id + ' ' + p.r + '/' + p.pv));
} catch (e) { console.log('INTEGRITY_FAIL ' + e.message); }

/* --- 空数据保护 --- */
try {
  const bak = api.PHOTOS.slice();
  api.PHOTOS.length = 0;
  api.state.region = 'china'; api.state.mapLevel = 2; api.state.prov = 'Hunan';
  api.refreshAll();
  api.mapBack(); api.mapBack();
  console.log('EMPTY_OK level=' + api.state.mapLevel);
  api.PHOTOS.push(...bak);
  api.refreshAll();
} catch (e) { console.log('EMPTY_FAIL ' + e.message); }

console.log('SMOKE_DONE');

// 页面里的本地库探测是异步的（libQueue 链），在 Node 桩环境下没有 fetch/网络，
// 收尾阶段可能留下未处理的 rejection —— 断言已经全部跑完，这里静默即可，
// 否则 Node 会在退出时抛错、把「全绿」的结果盖掉。
process.on('unhandledRejection', () => {});
process.on('uncaughtException', (e) => {
  console.log('TAIL_ERROR ' + (e && e.message));
});
setTimeout(() => process.exit(FATAL ? 1 : 0), 30);

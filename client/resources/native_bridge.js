/*!
 * 光影集 · 本地客户端桥接层
 *
 * 由 PySide6 以 DocumentCreation 时机注入，因此保证在页面自身脚本之前就绪。
 * 注入后网页侧获得：
 *   window.__native            —— 调用原生能力（文件夹对话框 / 扫描 / 打开原图 / 导入导出）
 *   window.__onNative(action)  —— 接收原生回推的结果
 *
 * 通信方向
 *   JS → Py：new Image().src = 'lightalbum://<action>?p=<urlencoded json>'
 *             （子资源请求，由 Python 侧 QWebEngineUrlSchemeHandler 接住并回 1×1 GIF）
 *   Py → JS：page.runJavaScript('window.__onNative(action, payload)')
 *
 * ⚠ 绝不能用 location.assign / location.href 发指令。
 *   那是主框架导航：一旦被 acceptNavigationRequest 拒绝，Chromium 会把当前文档
 *   换成 about:blank#blocked；而本脚本在 DocumentCreation 注入，新文档里会再发一次，
 *   于是变成无限循环、页面永久空白（实测踩过，排查代价很高）。
 *   子资源请求不触碰文档，天然安全。
 *
 * 这个文件只做「桥接 + 客户端专属 UI」，不改动 index.html 本身的任何逻辑 ——
 * 网页版保持单文件零外链、可独立部署，客户端只是额外获得原生能力。
 */
(function () {
  'use strict';

  var SCHEME = 'lightalbum';
  var handlers = {};
  var pending = {};                 // action -> 是否已收到过（用于幂等提示）

  // 让页面知道自己跑在客户端里（网页版独立打开时该标记不存在，行为完全不变）
  window.__IS_CLIENT__ = true;

  /* ---------------------------------------------------- JS → 原生 */
  var seq = 0;

  function call(action, payload) {
    var p;
    try {
      p = encodeURIComponent(JSON.stringify(payload || {}));
    } catch (e) {
      p = '%7B%7D';
    }
    // action 放 query 而不是 authority：
    // URL 规范化会把 authority（`lightalbum://Action`）**整体转成小写**，
    // 于是 confirmClearDemo 变成 confirmcleardemo，Python 侧 act_<action> 找不到，
    // 信标被静默丢弃（setTitle 也栽在这上面）。query 参数不会被改大小写。
    var url = SCHEME + '://x/?a=' + encodeURIComponent(action) + '&n=' + (++seq) + '&p=' + p;
    // 页面若经本地 HTTP 加载（本地库模式），lightalbum:// 这种跨 scheme 子资源
    // 会被 Chromium 直接拦截（实测：信标全部静默失效）。所以改走本地服务的
    // /api/native 通道，失败再回退信标（file:// 加载时信标仍然可用）。
    if (window.__libPort && typeof fetch === 'function') {
      try {
        fetch('http://127.0.0.1:' + window.__libPort + '/api/native', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: action, payload: payload || {} })
        }).catch(function () { beacon(url, action); });
        return;
      } catch (e) { /* 落到信标 */ }
    }
    beacon(url, action);
  }

  function beacon(url, action) {
    try {
      // 子资源信标：不会改变文档，也不会被导航拦截影响。
      // 带自增 n= 避免命中缓存导致原生收不到请求。
      var im = new Image();
      im.onload = im.onerror = function () { im.onload = im.onerror = null; };
      im.src = url;
    } catch (e) {
      console.error('[native] call failed', action, e);
    }
  }

  /* ---------------------------------------------------- 原生 → JS */
  window.__onNative = function (action, payload) {
    var fn = handlers[action];
    if (!fn) return;
    try {
      fn(payload || {});
    } catch (e) {
      console.error('[native] handler error', action, e);
    }
  };

  function on(action, fn) { handlers[action] = fn; }

  window.__native = {
    call: call,
    on: on,
    ready: true
  };

  /* ---------------------------------------------------- 页面内小工具 */
  function el(id) { return document.getElementById(id); }
  function msg(t) {
    if (typeof srcMsg === 'function') srcMsg(t);
    else console.log('[native] ' + t);
  }
  function openDrawer() {
    var d = el('srcDrawer'), b = el('btnSources');
    if (!d) return;
    if (typeof renderSources === 'function') renderSources();
    d.classList.add('show');
    document.body.classList.add('drawer-open');
    if (b) b.focus();
  }

  /* ---------------------------------------------------- 扫描结果落库
     约定：客户端扫描出来的作品统一挂在同一个来源下（src='native'）。
     再次扫描时先清掉同来源的旧数据，避免出现"删了文件但作品还在"的幽灵数据。

     首轮扫描还会问一次「要不要清掉示例数据」：网页版自带 24 幅示例作品，
     真实照片一旦导入，示例数据混在图库/统计/地图里会污染所有数字，
     所以让用户明确选一次，而不是替他决定。 */
  var NATIVE_SRC = 'native';
  var _pendingScan = null;      // 等用户回答「是否清示例数据」时暂存的扫描结果
  var _demoAsked = false;

  function nonNativeCount() {
    if (typeof PHOTOS === 'undefined') return 0;
    var n = 0;
    for (var i = 0; i < PHOTOS.length; i++) {
      if (PHOTOS[i] && PHOTOS[i].src !== NATIVE_SRC) n++;
    }
    return n;
  }

  function dropNonNativePhotos() {
    if (typeof PHOTOS === 'undefined') return 0;
    var n = 0;
    for (var i = PHOTOS.length - 1; i >= 0; i--) {
      if (PHOTOS[i] && PHOTOS[i].src !== NATIVE_SRC) { PHOTOS.splice(i, 1); n++; }
    }
    return n;
  }

  function dropNativePhotos() {
    if (typeof PHOTOS === 'undefined') return;
    for (var i = PHOTOS.length - 1; i >= 0; i--) {
      if (PHOTOS[i] && PHOTOS[i].src === NATIVE_SRC) PHOTOS.splice(i, 1);
    }
  }

  function nextId() {
    var m = 0;
    if (typeof PHOTOS !== 'undefined') {
      for (var i = 0; i < PHOTOS.length; i++) {
        if (PHOTOS[i] && PHOTOS[i].id > m) m = PHOTOS[i].id;
      }
    }
    return m + 1;
  }

  function applyScanResult(p) {
    var items = p.photos || [];
    if (typeof PHOTOS === 'undefined') { msg('页面尚未就绪'); return; }
    dropNativePhotos();
    var added = 0;
    for (var i = 0; i < items.length; i++) {
      var raw = items[i];
      var abs = raw._abs;
      delete raw._abs;                       // 绝对路径不入库存（换机器就失效）
      raw.id = nextId();
      raw.src = NATIVE_SRC;
      raw.rel = raw.rel || '';
      if (typeof normalizePhoto === 'function') raw = normalizePhoto(raw);
      PHOTOS.push(raw);
      added++;
      if (typeof window.__nativeAbs === 'undefined') window.__nativeAbs = {};
      if (abs) window.__nativeAbs[raw.rel] = abs;   // 仅本次会话有效，供"打开原图"
    }
    if (typeof savePhotos === 'function') savePhotos();
    if (typeof refreshAll === 'function') refreshAll();
    if (typeof renderSources === 'function') renderSources();
    var withGps = items.filter(function (x) { return x.la || x.lo; }).length;
    var withExif = items.filter(function (x) { return x.cam; }).length;
    msg('扫描完成：导入 ' + added + ' 幅'
      + '（含 EXIF ' + withExif + ' 幅，含 GPS ' + withGps + ' 幅）');
    pending.lastScan = { root: p.root, count: added };
  }

  on('version', function (p) { pending.version = p; });

  on('folderPicked', function (p) {
    if (!p.path) return;
    var f = el('srcRoot');
    if (f) f.value = p.path;
    msg('已选择文件夹，开始扫描：' + p.path);
    call('scan', { root: p.path, thumbs: true });
  });

  on('scanResult', function (p) {
    var demo = nonNativeCount();
    if (!_demoAsked && demo > 0 && (p.photos || []).length) {
      _demoAsked = true;
      _pendingScan = p;
      call('confirmClearDemo', { demo: demo, scanned: (p.photos || []).length });
      return;                                  // 等用户回答后再落库
    }
    applyScanResult(p);
  });

  on('clearDemoDecision', function (p) {
    var stashed = _pendingScan;
    _pendingScan = null;
    if (p && p.clear) {
      var n = dropNonNativePhotos();
      msg('已清空 ' + n + ' 幅示例作品');
    }
    if (stashed) applyScanResult(stashed);
  });


  on('scanError', function (p) { msg('扫描失败：' + (p.msg || '未知原因')); });

  /* ---- 本地库变化：由客户端在写库/导入完成后推送，页面据此立即重载画廊 ----
     早年扫描只进内存 PHOTOS，本地库模式下 loadLibraryPhotos 会覆盖内存，
     导致「导入的照片在图库里不显示、重启即丢」。现在客户端会把扫描/导入
     写进 DB 并推这个事件，画廊即时刷新（watchLibrarySync 3 秒内也会兜底）。 */
  on('libraryChanged', function (p) {
    if (window.__libPort && typeof loadLibraryPhotos === 'function') {
      loadLibraryPhotos();
    }
  });
  on('importDone', function (p) {
    msg('已导入 ' + (p.count || 0) + ' 张到自管目录（' + (p.dir || '') + '），不再依赖原盘/iCloud');
  });
  on('importError', function (p) { msg('导入失败：' + (p.msg || '未知原因')); });

  /* ---- 外部编辑器（ADR-0013）：回推挂钩 ----
     产物回灌复用既有 importDone / libraryChanged 通道，
     画廊会自动刷新，这里只补状态提示。 */
  on('extEditLaunched', function (p) {
    msg('已在外部编辑器打开工作副本。成片导出到 output 目录后，'
      + '客户端每 10 秒检查一次并自动回灌入库。');
  });
  on('extEditEditorSet', function (p) {
    msg('外部编辑器已设置：' + (p.editor || ''));
  });
  on('extEditError', function (p) { msg('外部编辑器：' + (p.msg || '未知错误')); });

  /* ---- 版本组（ADR-0013）：外部编辑/无头渲染产物 ↔ 原图 ----
     数据源：Python 推 versionsMap（versions.json 全量）。abs 匹配规则：
     页面照片的 p.path || __nativeAbs[p.rel]（统一小写正斜杠后比对），
     本地库/网页两模式通用。 */
  function normKey(x) {
    return String(x || '').replace(/\\/g, '/').toLowerCase();
  }
  function absOf(p) {
    return ((window.__nativeAbs || {})[p.rel] || p.path ||
            (p.img || '').replace(/^file:\/\//, ''));
  }
  window.__onVersionsMap = null;
  on('versionsMap', function (p) {
    window.__versionMap = (p && p.products) || {};
    var lb = el('lightbox');
    if (lb && lb.classList.contains('show')) updateVersionStrip();
  });
  on('headlessProgress', function (p) {
    msg('无头渲染 ' + (p.done || 0) + ' / ' + (p.total || 0) + '：' + (p.name || ''));
  });
  on('headlessDone', function (p) {
    msg('无头渲染完成：成功 ' + (p.ok || 0) + ' 张，失败 ' + (p.fail || 0)
      + ' 张。成片回灌入库中…');
  });
  on('headlessError', function (p) { msg('无头渲染：' + (p.msg || '未知错误')); });

  /* ---- 灯箱版本条：原图 / 外部编辑 / 渲染 chips，点击即切换 ---- */
  function updateVersionStrip() {
    var host = el('lbVersions');
    if (!host) return;
    var map = window.__versionMap || {};
    if (typeof lbPhotoId === 'undefined' || lbPhotoId == null ||
        typeof PHOTOS === 'undefined') { host.style.display = 'none'; return; }
    var cur = PHOTOS.filter(function (x) { return x.id === lbPhotoId; })[0];
    if (!cur) { host.style.display = 'none'; return; }
    var curAbs = normKey(absOf(cur));
    var link = map[curAbs] || null;
    var origAbs = link ? normKey(link.orig) : curAbs;
    var chips = [];
    if (origAbs !== curAbs) {                 // 当前是产物 → 带原图 chip
      var origPh = PHOTOS.filter(function (x) {
        return normKey(absOf(x)) === origAbs; })[0];
      chips.push({ p: origPh, label: '原图' });
    }
    Object.keys(map).forEach(function (k) {   // 同一原图下的其他产物
      if (normKey((map[k] || {}).orig) !== origAbs) return;
      if (k === curAbs) return;
      var prod = PHOTOS.filter(function (x) {
        return normKey(absOf(x)) === k; })[0];
      if (!prod) return;
      var v = map[k];
      var tag = v.kind === 'develop' ? '渲染' : '外部编辑';
      var when = '';
      if (v.ts) {
        var d = new Date(v.ts * 1000);
        when = ' ' + ('0' + (d.getMonth() + 1)).slice(-2) + '-' +
          ('0' + d.getDate()).slice(-2) + ' ' +
          ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
      }
      chips.push({ p: prod, label: tag + when });
    });
    if (!chips.length) { host.style.display = 'none'; return; }
    host.style.display = '';
    host.textContent = '';
    var t = document.createElement('span');
    t.className = 'hint';
    t.textContent = '版本：';
    host.appendChild(t);
    chips.forEach(function (c) {
      var b = document.createElement('button');
      b.type = 'button';
      b.className = 'lb-act';
      b.style.margin = '2px 6px 2px 0';
      b.textContent = c.label;
      b.addEventListener('click', function () {
        if (c.p && typeof openLightbox === 'function') openLightbox(c.p.id);
      });
      host.appendChild(b);
    });
  }

  var _lbObs = null;
  function watchLightbox() {
    var lb = el('lightbox');
    if (!lb || _lbObs || typeof MutationObserver === 'undefined') return;
    _lbObs = new MutationObserver(function () {
      if (lb.classList.contains('show')) updateVersionStrip();
    });
    _lbObs.observe(lb, { attributes: true, attributeFilter: ['class'] });
  }
  on('saveDone', function (p) { msg('已导出到：' + p.path); });

  /* ---- 大批量扫描结果：分批接收，攒齐后走同一条落库路径 ----
     几千条结果一次性塞给页面会把主线程卡死（JSON 解析 + normalizePhoto +
     渲染全在一条消息里），Python 侧按 SCAN_CHUNK 分批发，这里攒齐再处理，
     demo 确认等原有逻辑完全不受影响。 */
  var _chunkBuf = null, _chunkTotal = 0;
  on('scanResultChunk', function (p) {
    if (!_chunkBuf) { _chunkBuf = []; _chunkTotal = p.total || 0; }
    _chunkBuf = _chunkBuf.concat(p.items || []);
    msg('正在接收扫描结果 ' + _chunkBuf.length + ' / ' + _chunkTotal + ' …');
    if (p.last) {
      var all = _chunkBuf, root = p.root;
      _chunkBuf = null; _chunkTotal = 0;
      applyScanResult({ photos: all, root: root });
    }
  });

  /* ---- 智能修图：进度 / 完成 / 失败 ---- */
  on('retouchProgress', function (p) {
    msg('智能修图 ' + p.done + ' / ' + p.total + '：' + (p.name || ''));
    relay('__onRetouchProgress', p);        // 页面动态进度条（relay 会重试直到钩子就绪）
  });
  on('retouchResult', function (p) {
    var n = (p.ok || []).length, e = (p.err || []).length;
    if (!n && !e) { msg('智能修图：没有可处理的文件'); return; }
    // 修图实验室（页面 section）有自己的渲染器 —— 单张结果直接交给它
    if (typeof window.__onRetouchResult === 'function') {
      try { window.__onRetouchResult(p); } catch (err) { console.error(err); }
      if (n) return;                        // 实验室已展示，消息条只报失败
    }
    var s = (p.ok || [])[0];
    var detail = s ? ('示例：' + s.out.split(/[\\/]/).pop()
      + '（亮度 ' + s.lum_before + ' → ' + s.lum_after
      + '，降噪档 ' + s.denoise + '）') : '';
    msg('智能修图完成：成功 ' + n + ' 张' + (e ? '，失败 ' + e + ' 张' : '')
      + '。输出在原片同目录的 *_retouched.jpg。' + detail);
    if (e) (p.err || []).slice(0, 3).forEach(function (x) {
      console.warn('[native] retouch failed', x[0], x[1]);
    });
  });
  on('retouchError', function (p) { msg('智能修图失败：' + (p.msg || '未知错误')); });

  /* ---- 云相册接入：渲染探测结果，一键交给既有扫描管线 ----
     iCloud / 一刻相册都没有开放 API，接入的是它们的 Windows 同步目录；
     目录找到后直接 call('scan', {root})，缓存优先/并行/draft 全部自动生效。 */
  /* ---- AI 决策 / 执行：把原生事件转给页面钩子 ----
     页面侧定义了 window.__onAiStatus / __onAiEditResult，
     这里负责转发（漏了这一步时，Python 明明发了事件，
     页面徽章却一直停在默认值 —— 实测踩过，靠截图才看出来）。 */
  function relay(hook, payload, tries) {
    // 为什么要重试：原生事件可能早于「页面脚本执行到钩子定义」就到了
    // （loadFinished 对空白页也会触发一次，那次推送落在新文档解析过程中）。
    // 只推一次会静默丢掉 —— 实测踩过：libPort 已设置，但页面 state 一直是 0。
    tries = tries || 0;
    var f = window[hook];
    if (typeof f === 'function') {
      try { f(payload); } catch (e) { console.error('[native] ' + hook, e); }
      return;
    }
    if (tries < 25) setTimeout(function () { relay(hook, payload, tries + 1); }, 250);
  }
  on('aiStatus', function (p) { relay('__onAiStatus', p); });
  on('aiSuggestResult', function (p) { relay('__onAiSuggest', p); });
  on('aiEditResult', function (p) { relay('__onAiEditResult', p); });
  on('aiEditError', function (p) {
    relay('__onAiEditResult', null);
    msg('AI 修图失败：' + ((p && p.msg) || '未知原因'));
  });
  // P3 去背景（BiRefNet）：结果转给页面 __onMattingResult 钩子
  on('mattingResult', function (p) { relay('__onMattingResult', p); });
  // 本地库服务端口：页面据此切到「本地库模式」（数据来自 SQLite 索引，
  // 不再每次重扫云相册；网页版也可以直接连这个地址读同一份数据）
  on('libInfo', function (p) {
    if (p && p.port) {
      window.__libPort = p.port;
      relay('__onLibInfo', p);
    }
  });

  /* ---- 云相册：启动自动导入 ----
     探测结果里若带 auto=true，自动挑照片最多的那个同步目录启动扫描。
     策略（开关 + 6 小时窗口）在这里再判一次，执行方把关。 */
  var CLOUD_AUTO_KEY = 'wb_photo_autoCloud';
  var CLOUD_LAST_KEY = 'wb_photo_lastCloudScan';
  var CLOUD_FRESH_MS = 6 * 3600 * 1000;
  function autoImportAllowed() {
    try {
      if (localStorage.getItem(CLOUD_AUTO_KEY) === '0') return false;
      var last = parseInt(localStorage.getItem(CLOUD_LAST_KEY) || '0', 10) || 0;
      return (Date.now() - last) > CLOUD_FRESH_MS;
    } catch (e) {
      return false;                            // 存储不可用时不自动导入，宁可保守
    }
  }
  function markCloudScanned() {
    try { localStorage.setItem(CLOUD_LAST_KEY, String(Date.now())); } catch (e) {}
  }

  on('cloudAlbumsError', function (p) { msg('云相册探测失败：' + (p.msg || '')); });
  on('cloudAlbums', function (p) {
    var host = el('cloudList');
    if (!host) return;
    host.textContent = '';
    (p.albums || []).forEach(function (a) {
      if (!a.found) {
        var miss = document.createElement('div');
        miss.className = 'hint';
        miss.style.margin = '6px 0';
        var b1 = document.createElement('b');
        b1.textContent = a.name + '：未检测到本地同步目录。';
        miss.appendChild(b1);
        miss.appendChild(document.createTextNode(a.hint || ''));
        host.appendChild(miss);
        return;
      }
      var row = document.createElement('div');
      row.style.cssText = 'display:flex;gap:9px;align-items:center;margin:8px 0;flex-wrap:wrap';
      var info = document.createElement('div');
      info.style.cssText = 'flex:1 1 200px;min-width:0';
      var nm = document.createElement('b');
      nm.textContent = a.name;
      info.appendChild(nm);
      var meta = document.createElement('div');
      meta.className = 'hint';
      meta.style.wordBreak = 'break-all';
      meta.textContent = a.path + ' · ' + a.photo_count + ' 张';
      info.appendChild(meta);
      if (a.warn) {
        var wn = document.createElement('div');
        wn.className = 'hint';
        wn.style.cssText = 'color:#c0392b;margin-top:4px';
        wn.textContent = a.warn;
        info.appendChild(wn);
      }
      var btn = document.createElement('button');
      btn.className = 'btn primary';
      btn.textContent = '扫描导入';
      btn.addEventListener('click', function () {
        msg('开始扫描云相册同步目录：' + a.path);
        call('scan', { root: a.path, thumbs: true });
      });
      row.appendChild(info);
      row.appendChild(btn);
      host.appendChild(row);
    });

    // 启动自动导入：挑照片最多的同步目录直接扫（扫描本身在线程池里，不卡界面）
    // 这里要**自己复查一次策略**，不能只信回推里的 auto 标记 ——
    // auto 是页面请求时算出来的，回推途中状态可能已变（实测：连续两次回推
    // auto=true 会重复导入）。执行方负责把关。
    if (p && p.auto && autoImportAllowed()) {
      var best = (p.albums || []).filter(function (a) { return a.found; })
                                 .sort(function (a, b) { return b.photo_count - a.photo_count; })[0];
      if (best) {
        msg('检测到云相册『' + best.name + '』（' + best.photo_count
            + ' 张），正在自动导入…');
        markCloudScanned();
        call('scan', { root: best.path, thumbs: true });
      }
    }
  });

  on('fileOpened', function (p) {
    try {
      var d = JSON.parse(p.content);
      if (!d || !Array.isArray(d.photos)) { msg('导入失败：文件里没有 photos 数组'); return; }
      if (typeof PHOTOS === 'undefined') return;
      PHOTOS.length = 0;
      for (var i = 0; i < d.photos.length; i++) {
        var o = d.photos[i];
        if (typeof normalizePhoto === 'function') o = normalizePhoto(o);
        PHOTOS.push(o);
      }
      if (Array.isArray(d.favs) && typeof FAVS !== 'undefined') {
        FAVS.clear();
        d.favs.forEach(function (n) { FAVS.add(Number(n)); });
      }
      if (d.sources && typeof SOURCES !== 'undefined') {
        SOURCES.length = 0;
        d.sources.forEach(function (s) { SOURCES.push(s); });
        if (typeof saveSources === 'function') saveSources();
      }
      if (typeof savePhotos === 'function') savePhotos();
      if (typeof saveFavs === 'function') saveFavs();
      if (typeof refreshAll === 'function') refreshAll();
      msg('已导入 ' + PHOTOS.length + ' 幅作品');
    } catch (e) {
      msg('导入失败：' + e.message);
    }
  });

  /* ---------------------------------------------------- 客户端专属 UI
     在「数据工具条」里插入一个只在客户端出现的按钮。
     网页版独立打开时 __native 不存在，这个按钮不会出现，不会破坏零外链约束。 */
  function buildUI() {
    var bar = el('btnSources');
    if (!bar) { setTimeout(buildUI, 300); return; }
    if (el('btnNativeScan')) return;

    var b = document.createElement('button');
    b.id = 'btnNativeScan';
    b.className = 'btn primary';
    b.type = 'button';
    b.textContent = '扫描本地文件夹';
    b.title = '递归扫描任意照片目录，读取真实 EXIF 与 GPS（仅本地客户端可用）';
    b.addEventListener('click', function () { call('pickFolder', {}); });
    bar.parentNode.insertBefore(b, bar);

    // 「导入到自管目录」：把选中的照片复制进 app 自己的目录，
    // 彻底不依赖 iCloud / 原盘挂载（与「扫描」按原路径引用互补）。
    var bi = document.createElement('button');
    bi.id = 'btnImportManaged';
    bi.className = 'btn';
    bi.type = 'button';
    bi.textContent = '导入到自管目录';
    bi.title = '把选中的照片复制到本机自管目录（~/.lightalbum/imports），即使源盘/iCloud 不挂载也能查看';
    bi.addEventListener('click', function () { call('importToLibrary', {}); });
    bar.parentNode.insertBefore(bi, bar);

    // 打开原图：给灯箱加一个入口
    var fav = el('lbFav');
    if (fav && !el('lbOpenOriginal')) {
      var o = document.createElement('button');
      o.id = 'lbOpenOriginal';
      o.className = 'lb-fav';
      o.type = 'button';
      o.textContent = '在资源管理器中打开';
      o.addEventListener('click', function () {
        var id = (typeof lbPhotoId === 'undefined') ? null : lbPhotoId;
        if (id == null) return;
        var p = PHOTOS.filter(function (x) { return x.id === id; })[0];
        if (!p) return;
        // 优先级：会话内记录的绝对路径 → DB 里的 path（本地库模式扫描/导入的照片都带）
        // → img 里的 file:// 绝对路径（file:// 降级模式）。
        var abs = (window.__nativeAbs || {})[p.rel] || p.path ||
                  (p.img || '').replace(/^file:\/\/\//, '');
        if (abs) call('openExternal', { path: abs.replace(/\//g, '\\') });
      });
      fav.parentNode.appendChild(o);
    }

    // 外部编辑器（ADR-0013）：把本机原图交给 darktable/ART/PhotoDemon 等
    // 独立暗房；客户端轮询其 output 目录，产物自动回灌入库。本文件只在
    // 客户端注入，网页版不会出现此按钮，零外链约束不受影响。
    if (fav && !el('lbExtEdit')) {
      var xe = document.createElement('button');
      xe.id = 'lbExtEdit';
      xe.className = 'lb-fav';
      xe.type = 'button';
      xe.textContent = '\ud83c\udfa8 外部编辑器';
      xe.addEventListener('click', function () {
        var id = (typeof lbPhotoId === 'undefined') ? null : lbPhotoId;
        if (id == null) return;
        var p = PHOTOS.filter(function (x) { return x.id === id; })[0];
        if (!p) return;
        // 与「在资源管理器中打开」同优先级：会话绝对路径 → DB path → file://
        var abs = (window.__nativeAbs || {})[p.rel] || p.path ||
                  (p.img || '').replace(/^file:\/\//, '');
        if (!abs) { msg('找不到本机原图路径，无法交给外部编辑器'); return; }
        call('extEdit', { path: abs.replace(/\//g, '\\') });
      });
      fav.parentNode.appendChild(xe);
    }

    // 无头批量渲染（darktable-cli）：本目录同套显影 → 成片回灌为「渲染」版本
    if (fav && !el('lbHeadless')) {
      var hb = document.createElement('button');
      hb.id = 'lbHeadless';
      hb.className = 'lb-act';
      hb.type = 'button';
      hb.textContent = 'darktable 渲染本组';
      hb.addEventListener('click', function () {
        var id = (typeof lbPhotoId === 'undefined') ? null : lbPhotoId;
        if (id == null) return;
        var p = PHOTOS.filter(function (x) { return x.id === id; })[0];
        if (!p) return;
        var abs = absOf(p);
        if (!abs) { msg('找不到本机路径，无法渲染'); return; }
        call('headlessRender', { path: abs.replace(/\//g, '\\') });
      });
      fav.parentNode.appendChild(hb);
    }

    // 版本条容器（灯箱动作区末尾，内容由 updateVersionStrip 填充）
    if (fav && !el('lbVersions')) {
      var vs = document.createElement('div');
      vs.id = 'lbVersions';
      vs.style.cssText = 'margin-top:6px;display:none;flex-wrap:wrap;align-items:center';
      fav.parentNode.appendChild(vs);
    }
    watchLightbox();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { setTimeout(buildUI, 60); });
  } else {
    setTimeout(buildUI, 60);
  }

  // 客户端里把标题同步到窗口标题栏（只发一次，避免任何形式的重复触发）
  var _titleSent = false;
  function _syncTitle() {
    if (_titleSent) return;
    var t = (document.title || '').replace(/ · .*$/, '').trim();
    if (!t) { setTimeout(_syncTitle, 300); return; }   // 文档还没解析完，稍后再试
    _titleSent = true;
    call('setTitle', { title: t });
  }
  setTimeout(_syncTitle, 400);
})();

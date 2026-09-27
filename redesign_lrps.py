# -*- coding: utf-8 -*-
"""1.7.0 · LR/PS 集大成布局 + 活力灵动/科技感增强。
零外链 · 单主脚本 · 双主题 · 不改渲染 DAG 纯度 · 不动 reduced-motion 铁律。
所有 old/new 经 norm() 统一成 CRLF 再匹配（二进制读，保留 CRLF）。
"""
import io, sys

PATH = 'index.html'
NL = '\r\n'

def norm(s):
    return s.replace('\r\n', '\n').replace('\n', '\r\n')

data = open(PATH, 'rb').read().decode('utf-8')

def rep(old, new, label):
    global data
    o, n = norm(old), norm(new)
    c = data.count(o)
    assert c == 1, '%s: count=%d (expected 1)' % (label, c)
    data = data.replace(o, n)
    print('OK  ', label)
    return data

# ---------------------------------------------------------------------------
# Rep 1 · 顶栏加两个切换按钮（检视面板 / 胶片条），插在主题按钮前
# ---------------------------------------------------------------------------
data = rep(
'''    <button class="theme-btn" id="themeBtn" aria-label="切换深色模式" title="切换深色 / 浅色">◐</button>''',
'''    <button class="top-toggle" id="inspectToggle" aria-label="检视面板" title="检视面板" aria-pressed="false"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="16" rx="2"/><line x1="14" y1="4" x2="14" y2="20"/><line x1="3" y1="9" x2="14" y2="9"/></svg></button>
    <button class="top-toggle" id="filmToggle" aria-label="胶片条" title="胶片条" aria-pressed="true"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="5" width="18" height="14" rx="2"/><path d="M3 10h18M3 14h18M7 5v14M12 5v14M17 5v14"/></svg></button>
    <button class="theme-btn" id="themeBtn" aria-label="切换深色模式" title="切换深色 / 浅色">◐</button>''',
'1-topbar-toggles')

# ---------------------------------------------------------------------------
# Rep 2 · 在 #shell 内（main-col 关闭后、shell 关闭前）插入检视面板 + 胶片条
# ---------------------------------------------------------------------------
data = rep(
'''</footer>
</div>
</div>

<!-- 个人资料编辑 -->''',
'''</footer>
</div>
</div>

<!-- LR/PS 集大成：右侧检视面板 + 底部胶片条（固定定位，独立于页面滚动） -->
<aside id="inspect" aria-label="作品检视面板">
  <div class="insp-head"><span class="insp-title"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="16" rx="2"/><line x1="14" y1="4" x2="14" y2="20"/></svg>检视面板</span><button class="insp-close" id="inspectClose" aria-label="关闭检视面板">×</button></div>
  <div class="insp-body" id="inspectBody"></div>
</aside>
<div id="filmstrip" aria-label="胶片条">
  <div class="film-head"><span class="film-title">胶片条</span><span class="film-sub" id="filmSub"></span></div>
  <div class="film-rail" id="filmstripThumbs"></div>
</div>

<!-- 个人资料编辑 -->''',
'2-panels')

# ---------------------------------------------------------------------------
# Rep 3 · 在第一个 </style>（头部样式块）前插入所有新样式
# ---------------------------------------------------------------------------
i = data.find('</style>')
fidx = data.find('<script id="fabric-lib">')
assert i > 0 and i < fidx, 'first </style> not the head one (i=%d)' % i

CSS = r'''/* ===== LR/PS 集大成 · 活力灵动 + 科技感（1.7.0） ===== */
:root{
  --accent2:#37e0ff;                                          /* 科技青：与翠绿组成双色渐变 */
  --tech:linear-gradient(135deg,#3ddc97 0%,#37e0ff 100%);
  --glow:0 0 18px rgba(61,220,151,.35);
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace;
}
body{isolation:isolate;}                                       /* 为 ::before 点阵建立层叠上下文，确保它落在背景之上、内容之下 */
/* 深色：底层点阵科技纹理（固定铺底，不随滚动） */
body.dark::before{
  content:"";position:fixed;inset:0;z-index:-1;pointer-events:none;
  background-image:radial-gradient(circle at center, rgba(255,255,255,.05) 1px, transparent 1.4px);
  background-size:24px 24px;
  -webkit-mask-image:radial-gradient(circle at 50% 28%, #000 0%, rgba(0,0,0,.12) 82%);
          mask-image:radial-gradient(circle at 50% 28%, #000 0%, rgba(0,0,0,.12) 82%);
}
/* 品牌与分区标题：双色渐变文字（科技 / 活力） */
body.dark .brand{background:var(--tech);-webkit-background-clip:text;background-clip:text;color:transparent;}
body.dark .brand .dot{background:#3ddc97;box-shadow:0 0 10px rgba(61,220,151,.6);}
body.dark .sec-title{background:var(--tech);-webkit-background-clip:text;background-clip:text;color:transparent;}
/* 侧栏激活项：翠绿辉光 */
body.dark #nav a.active{
  box-shadow:inset 0 0 0 1px rgba(61,220,151,.40), 0 0 16px rgba(61,220,151,.20);
  background:linear-gradient(90deg, rgba(61,220,151,.16), rgba(61,220,151,.02));
}
/* KPI 数字：等宽 + 辉光，强化科技读数感 */
body.dark .kpi .v{font-family:var(--mono);font-variant-numeric:tabular-nums;letter-spacing:.5px;text-shadow:0 0 18px rgba(61,220,151,.25);}
/* 卡片悬停辉光（深色） */
body.dark .card:hover{border-color:rgba(61,220,151,.30);box-shadow:0 14px 44px rgba(0,0,0,.55),0 0 26px rgba(61,220,151,.08);}
/* 入场动效更「活」：轻微缩放 + 更利落的缓动（尊重既有 nth-child 错峰延迟） */
.reveal .card,.reveal .kpi{opacity:0;transform:translateY(16px) scale(.985);}
.reveal.in .card,.reveal.in .kpi{opacity:1;transform:none;transition:opacity .42s cubic-bezier(.2,.7,.3,1),transform .42s cubic-bezier(.2,.7,.3,1);}
/* ===== 右侧检视面板（PS 右栏类比） ===== */
#inspect{position:fixed;top:0;right:0;height:100vh;width:332px;z-index:80;display:flex;flex-direction:column;
  background:rgba(16,20,18,.80);backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);
  border-left:1px solid rgba(255,255,255,.09);box-shadow:-18px 0 50px rgba(0,0,0,.45);
  transform:translateX(102%);transition:transform var(--dur-3) var(--ease-out);}
body.insp-open #inspect{transform:none;}
body.insp-open .main-col{padding-right:332px;}
.insp-head{display:flex;align-items:center;justify-content:space-between;padding:14px 16px;border-bottom:1px solid rgba(255,255,255,.08);}
.insp-title{display:flex;align-items:center;gap:8px;font-weight:600;font-size:15px;color:var(--ink);}
.insp-title svg{width:18px;height:18px;color:#3ddc97;}
.insp-close{width:30px;height:30px;border:none;border-radius:8px;background:rgba(255,255,255,.06);color:var(--muted);font-size:18px;cursor:pointer;line-height:1;transition:all var(--dur-2);}
.insp-close:hover{background:rgba(255,255,255,.12);color:var(--ink);}
.insp-body{flex:1;overflow:auto;padding:16px;display:flex;flex-direction:column;gap:14px;}
.insp-card{background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.08);border-radius:14px;padding:14px;}
.insp-card h4{margin:0 0 10px;font-size:12px;letter-spacing:.6px;color:#3ddc97;text-transform:uppercase;}
.insp-title-main{font-size:18px;font-weight:700;color:var(--ink);margin:0 0 4px;line-height:1.3;}
.insp-meta{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;font-size:13px;color:var(--muted);}
.insp-meta b{color:var(--ink);font-weight:600;font-family:var(--mono);}
.insp-chips{display:flex;flex-wrap:wrap;gap:8px;}
.insp-chip{font-size:12px;padding:6px 12px;border-radius:999px;border:1px solid rgba(61,220,151,.35);
  background:rgba(61,220,151,.10);color:#7df0c0;cursor:pointer;transition:all var(--dur-2);}
.insp-chip:hover{background:rgba(61,220,151,.20);box-shadow:var(--glow);}
.insp-hint{font-size:13px;color:var(--muted);line-height:1.6;text-align:center;padding:24px 8px;}
.insp-stat{display:flex;justify-content:space-between;font-size:13px;padding:7px 0;border-bottom:1px dashed rgba(255,255,255,.07);}
.insp-stat:last-child{border-bottom:none;}
.insp-stat b{font-family:var(--mono);color:var(--ink);}
/* ===== 底部胶片条（LR 标志性底栏） ===== */
#filmstrip{position:fixed;left:0;right:0;bottom:0;z-index:75;padding:10px 16px 12px;
  background:linear-gradient(0deg, rgba(11,14,12,.94), rgba(11,14,12,.60));
  backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);
  border-top:1px solid rgba(255,255,255,.08);transform:translateY(110%);transition:transform var(--dur-3) var(--ease-out);}
body.film-open #filmstrip{transform:none;}
body.film-open{padding-bottom:122px;}
.film-head{display:flex;align-items:baseline;gap:10px;margin-bottom:8px;}
.film-title{font-size:13px;font-weight:600;color:#3ddc97;letter-spacing:.5px;}
.film-sub{font-size:12px;color:var(--muted);}
.film-rail{display:flex;gap:10px;overflow-x:auto;padding-bottom:4px;scrollbar-width:thin;}
.film-rail::-webkit-scrollbar{height:6px;}
.film-rail::-webkit-scrollbar-thumb{background:rgba(255,255,255,.18);border-radius:3px;}
.ft{flex:0 0 auto;width:96px;height:64px;border-radius:10px;overflow:hidden;cursor:pointer;position:relative;
  border:2px solid transparent;background:#11161a;transition:transform var(--dur-2),border-color var(--dur-2),box-shadow var(--dur-2);}
.ft:hover{transform:translateY(-3px);}
.ft.on{border-color:#3ddc97;box-shadow:0 0 16px rgba(61,220,151,.45);}
.ft .ph{width:100%;height:100%;display:block;object-fit:cover;}
.ft .ft-fav{position:absolute;right:5px;bottom:5px;font-size:12px;color:#ff6b8a;text-shadow:0 1px 3px rgba(0,0,0,.6);}
/* ===== 顶栏切换按钮 ===== */
.top-toggle{width:38px;height:38px;border-radius:10px;border:1px solid var(--glass-line);background:var(--glass);
  color:var(--muted);display:inline-flex;align-items:center;justify-content:center;cursor:pointer;transition:all var(--dur-2);}
.top-toggle svg{width:19px;height:19px;}
.top-toggle:hover{color:var(--gold);border-color:rgba(61,220,151,.4);}
.top-toggle[aria-pressed="true"]{color:#3ddc97;border-color:rgba(61,220,151,.5);box-shadow:var(--glow);background:rgba(61,220,151,.10);}
/* 胶片条缩略图入场（活力灵动；尊重 reduced-motion） */
@media (prefers-reduced-motion: no-preference){
  #filmstrip .ft{animation:filmIn .42s both cubic-bezier(.2,.7,.3,1);}
  #filmstrip .ft:nth-child(1){animation-delay:.02s}
  #filmstrip .ft:nth-child(2){animation-delay:.06s}
  #filmstrip .ft:nth-child(3){animation-delay:.10s}
  #filmstrip .ft:nth-child(4){animation-delay:.14s}
  #filmstrip .ft:nth-child(5){animation-delay:.18s}
  #filmstrip .ft:nth-child(n+6){animation-delay:.22s}
}
@keyframes filmIn{from{opacity:0;transform:translateY(12px) scale(.96)}to{opacity:1;transform:none}}
/* ===== 响应式：窄屏把检视面板改成浮层、隐藏胶片条 ===== */
@media (max-width:900px){
  #inspect{width:min(88vw,360px);}
  body.insp-open .main-col{padding-right:0;}
  #filmstrip{display:none;}
  body.film-open{padding-bottom:0;}
}
@media (max-width:600px){
  .top-toggle{width:34px;height:34px;}
}
'''
data = data[:i] + norm(CSS) + NL + data[i:]
print('OK  3-css-insert')

# ---------------------------------------------------------------------------
# Rep 4 · refreshAll：挂载两个新渲染函数（纯读，DAG 成员）
# ---------------------------------------------------------------------------
data = rep(
'''function refreshAll(){
  renderKpiStrip();   // 顶部 KPI 指标条（纯读渲染）''',
'''function refreshAll(){
  renderKpiStrip();   // 顶部 KPI 指标条（纯读渲染）
  renderInspect();    // 右侧检视面板（纯读渲染）
  renderFilmstrip();  // 底部胶片条（纯读渲染）''',
'4-refreshAll')

# ---------------------------------------------------------------------------
# Rep 5 · 在 setupNav 前插入检视/胶片条的全部逻辑
# ---------------------------------------------------------------------------
JS = r'''/* ===== LR/PS 集大成：检视面板 / 胶片条（1.7.0） ===== */
let _selPhoto = null;                                   // 当前检视的作品 id
function thumbHtml(p, cls){
  const isFav = FAVS.has(p.id);
  const bg = gradFor(p.t + p.r);
  const img = p.img
    ? '<img class="ph" src="'+escHtml(p.img)+'" alt="" loading="lazy" decoding="async">'
    : '<div class="ph" style="background:'+bg+'"></div>';
  return '<div class="'+cls+'" data-id="'+p.id+'" tabindex="0" role="button" aria-label="'+escHtml(p.t)+'">'+img+(isFav?'<span class="ft-fav">♥</span>':'')+'</div>';
}
function renderFilmstrip(){
  const rail = document.getElementById('filmstripThumbs'); if(!rail) return;
  const sub = document.getElementById('filmSub');
  let list = PHOTOS.filter(p=>FAVS.has(p.id));
  if(list.length < 8) list = list.concat(PHOTOS.filter(p=>!FAVS.has(p.id))).slice(0, 24);
  if(sub) sub.textContent = list.length ? (FAVS.size ? '收藏 '+FAVS.size+' · ' : '') + list.length + ' 幅' : '';
  if(!list.length){ rail.innerHTML = '<div class="insp-hint">暂无作品或收藏，连接本地库后这里会出现胶片条。</div>'; return; }
  rail.innerHTML = list.map(p=>thumbHtml(p,'ft')).join('');
  rail.querySelectorAll('.ft').forEach(el=>{
    const id = +el.dataset.id;
    el.addEventListener('click',()=>selectPhoto(id, true));
    el.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); selectPhoto(id, true); } });
  });
  rail.querySelectorAll('.ft').forEach(el=>el.classList.toggle('on', +el.dataset.id === _selPhoto));
}
function inspectSummary(){
  const total = PHOTOS.length, favN = FAVS.size;
  const provs = new Set(PHOTOS.map(p=>p.pv).filter(Boolean)).size;
  const yrs = yearsList().length;
  const rows =
      '<div class="insp-stat"><span>作品总数</span><b>'+total+'</b></div>'+
      '<div class="insp-stat"><span>已收藏</span><b>'+favN+'</b></div>'+
      '<div class="insp-stat"><span>足迹省份</span><b>'+provs+'</b></div>'+
      '<div class="insp-stat"><span>拍摄年份</span><b>'+yrs+'</b></div>';
  return '<div class="insp-card"><h4>库概览</h4>'+rows+'</div>'+
         '<div class="insp-hint">点击底部<b>胶片条</b>中的任意作品，即可在此查看元数据、参数与地图定位。</div>';
}
function inspectShow(p){
  const body = document.getElementById('inspectBody'); if(!body) return;
  if(!p){ body.innerHTML = inspectSummary(); return; }
  const isFav = FAVS.has(p.id);
  const meta = [
    ['地区', regionName(p.r)], ['省份', p.pv ? provName(p.pv) : '—'], ['年份', p.y],
    ['构图', p.o==='landscape'?'横构图':'竖构图'], ['器材', p.cam || '—'], ['镜头', p.lens || '—'],
    ['焦距', p.focal ? p.focal+'mm' : '—'], ['光圈', p.ap || '—'], ['快门', p.sh || '—'],
    ['ISO', p.iso != null ? p.iso : '—'], ['日期', p.d || '—']
  ].map(m=>'<div><span>'+m[0]+'</span><b>'+escHtml(String(m[1]))+'</b></div>').join('');
  body.innerHTML =
    '<div class="insp-card"><h4>当前作品</h4>'+
    '<div class="insp-title-main">'+escHtml(p.t)+'</div>'+
    '<div class="insp-meta">'+meta+'</div></div>'+
    '<div class="insp-card"><h4>操作</h4><div class="insp-chips">'+
      '<button class="insp-chip" data-act="fav">'+(isFav?'取消收藏 ♥':'收藏 ♡')+'</button>'+
      '<button class="insp-chip" data-act="map">在地图查看</button>'+
      '<button class="insp-chip" data-act="open">打开大图</button>'+
    '</div></div>'+
    '<div class="insp-card"><h4>AI 建议</h4><div class="insp-hint" style="padding:8px 4px;text-align:left">连接本地库并启用修图引擎后，这里会给出适配本图的预设建议。</div></div>';
  body.querySelectorAll('.insp-chip').forEach(b=>{
    b.addEventListener('click',()=>{
      const a = b.dataset.act;
      if(a==='fav'){ toggleFav(p.id); _selPhoto = p.id; inspectShow(p); }
      else if(a==='map'){ const link = document.querySelector('#nav a[data-sec="map"]'); if(link) link.click(); }
      else if(a==='open'){ openLightbox(p.id); }
    });
  });
}
function renderInspect(){ inspectShow(_selPhoto != null ? PHOTOS.find(p=>p.id===_selPhoto) : null); }
function selectPhoto(id, openPanel){
  const p = PHOTOS.find(x=>x.id===id); if(!p) return;
  _selPhoto = id;
  inspectShow(p);
  const rail = document.getElementById('filmstripThumbs');
  if(rail) rail.querySelectorAll('.ft').forEach(el=>el.classList.toggle('on', +el.dataset.id===id));
  if(openPanel) document.body.classList.add('insp-open');
  const t = document.getElementById('inspectToggle'); if(t) t.setAttribute('aria-pressed','true');
}
function toggleInspect(force){
  const on = force===undefined ? !document.body.classList.contains('insp-open') : force;
  document.body.classList.toggle('insp-open', on);
  const t = document.getElementById('inspectToggle'); if(t) t.setAttribute('aria-pressed', on?'true':'false');
}
function toggleFilm(force){
  const on = force===undefined ? !document.body.classList.contains('film-open') : force;
  document.body.classList.toggle('film-open', on);
  const t = document.getElementById('filmToggle'); if(t) t.setAttribute('aria-pressed', on?'true':'false');
}
function setupStudio(){
  const it = document.getElementById('inspectToggle'); if(it) it.addEventListener('click',()=>toggleInspect());
  const ft = document.getElementById('filmToggle'); if(ft) ft.addEventListener('click',()=>toggleFilm());
  const ic = document.getElementById('inspectClose'); if(ic) ic.addEventListener('click',()=>toggleInspect(false));
  // 默认开启胶片条（LR 标志性底栏）；检视面板默认收起
  document.body.classList.add('film-open');
  const tf = document.getElementById('filmToggle'); if(tf) tf.setAttribute('aria-pressed','true');
}

'''
data = rep('function setupNav(){', norm(JS) + NL + 'function setupNav(){', '5-studio-logic')

# ---------------------------------------------------------------------------
# Rep 6 · 画廊卡片点击：同步检视面板（若已展开）
# ---------------------------------------------------------------------------
data = rep(
'''    el.addEventListener('click',(e)=>{
      if(e.target.closest('.fav')) return;              // 点星标不打开大图
      if(e.target.closest('.ph-fallback')) return;      // 加载失败兜底区：交给重试按钮
      openLightbox(+el.dataset.id);
    });''',
'''    el.addEventListener('click',(e)=>{
      if(e.target.closest('.fav')) return;              // 点星标不打开大图
      if(e.target.closest('.ph-fallback')) return;      // 加载失败兜底区：交给重试按钮
      const _id=+el.dataset.id;
      selectPhoto(_id);                                 // 检视面板（若已展开）同步该作品
      openLightbox(_id);
    });''',
'6-gallery-select')

# ---------------------------------------------------------------------------
# Rep 7 · 初始化钩子：setupStudio()
# ---------------------------------------------------------------------------
data = rep(
'''  setupNav();
  setupTopSearch();
  setupCarousel();''',
'''  setupNav();
  setupTopSearch();
  setupStudio();
  setupCarousel();''',
'7-init')

# ---------------------------------------------------------------------------
# 收尾校验
# ---------------------------------------------------------------------------
for must in ['id="inspect"', 'id="filmstrip"', 'id="inspectToggle"', 'id="filmToggle"',
             'id="inspectBody"', 'id="inspectClose"', 'id="filmstripThumbs"', 'id="filmSub"',
             'function setupStudio', 'function renderInspect', 'function renderFilmstrip',
             'function selectPhoto', 'body.insp-open #inspect', 'body.film-open #filmstrip']:
    assert must in data, 'missing '+must
ext = len(__import__('re').findall(r'(?:src|href)="(https?:)?//', data))
assert ext == 0, 'external refs = %d' % ext

open(PATH, 'wb').write(data.encode('utf-8'))
print('WROTE index.html  (%d KB)' % (len(data)//1024))
print('ALL REPS DONE')

# -*- coding: utf-8 -*-
"""1.6.0 仪表盘骨架重构：左侧栏 + 顶栏(标题/全局搜索) + KPI 指标条。
全部替换带 count 断言；多行 pattern 用 CRLF。保住全部既有 id（check.js 162 个）。"""
P = 'index.html'
NL = '\r\n'
s = open(P, 'rb').read().decode('utf-8')
orig = len(s)


def rep(old, new, count=1):
    global s
    c = s.count(old)
    assert c == count, f"ABORT count={c} expected={count}: {old[:80]!r}"
    s = s.replace(old, new)


def blk(t):
    return t.replace('\n', NL)


# ── A. 顶栏整块 → shell + 侧栏 + 主列 + 新顶栏 ─────────────────────────
SVG = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">'
OLD_TOP = blk('''<!-- 顶栏 -->
<header class="topbar">
  <div class="topbar-inner">
    <div class="brand"><span class="dot"></span>光影集</div>
    <nav class="nav" id="nav">
      <a href="#home" data-sec="home" class="active">首页</a>
      <a href="#map" data-sec="map">足迹</a>
      <a href="#series" data-sec="series">系列</a>
      <a href="#gallery" data-sec="gallery">图库</a>
      <a href="#retouch" data-sec="retouch">修图</a>
      <a href="#log" data-sec="log">日志</a>
      <a href="#about" data-sec="about">关于</a>
    </nav>
    <button class="theme-btn" id="themeBtn" aria-label="切换深色模式" title="切换深色 / 浅色">◐</button>
    <div class="sync-pill"><span class="led"></span><span id="syncLabel">本地模式</span></div>
  </div>
</header>''')
NEW_TOP = blk('''<!-- 仪表盘骨架：左侧栏 + 主列（顶栏 / 内容 / 页脚） -->
<div id="shell">
<aside id="sidebar">
  <div class="brand"><span class="dot"></span>光影集</div>
  <nav class="nav" id="nav">
    <a href="#home" data-sec="home" class="active">''' + SVG + '''<path d="M4 11l8-7 8 7"/><path d="M6 10v9h12v-9"/></svg>首页</a>
    <a href="#map" data-sec="map">''' + SVG + '''<path d="M12 21s-6-5.2-6-10a6 6 0 1 1 12 0c0 4.8-6 10-6 10z"/><circle cx="12" cy="11" r="2.2"/></svg>足迹</a>
    <a href="#series" data-sec="series">''' + SVG + '''<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/></svg>系列</a>
    <a href="#gallery" data-sec="gallery">''' + SVG + '''<rect x="4" y="4" width="7" height="7" rx="1.5"/><rect x="13" y="4" width="7" height="7" rx="1.5"/><rect x="4" y="13" width="7" height="7" rx="1.5"/><rect x="13" y="13" width="7" height="7" rx="1.5"/></svg>图库</a>
    <a href="#retouch" data-sec="retouch">''' + SVG + '''<path d="M5 6h14M5 12h14M5 18h14"/><circle cx="9" cy="6" r="1.6"/><circle cx="15" cy="12" r="1.6"/><circle cx="8" cy="18" r="1.6"/></svg>修图</a>
    <a href="#log" data-sec="log">''' + SVG + '''<path d="M4 20v-9M10 20V5M16 20v-6"/><path d="M3 20h18"/></svg>日志</a>
    <a href="#about" data-sec="about">''' + SVG + '''<circle cx="12" cy="8" r="3.5"/><path d="M5 20c1.2-3.4 3.9-5 7-5s5.8 1.6 7 5"/></svg>关于</a>
  </nav>
  <div class="sb-foot">
    <div class="sync-pill"><span class="led"></span><span id="syncLabel">本地模式</span></div>
  </div>
</aside>
<div class="main-col">

<!-- 顶栏 -->
<header class="topbar">
  <div class="topbar-inner">
    <div class="tb-head"><strong id="tbTitle">首页</strong><span class="tb-sub">光影集 · 摄影作品集</span></div>
    <div class="tb-search">
      ''' + SVG + '''<circle cx="11" cy="11" r="6.5"/><path d="M16 16l4.5 4.5"/></svg>
      <input id="tbSearch" type="search" placeholder="搜索作品、地点、器材…" aria-label="搜索作品">
    </div>
    <button class="theme-btn" id="themeBtn" aria-label="切换深色模式" title="切换深色 / 浅色">◐</button>
  </div>
</header>''')
rep(OLD_TOP, NEW_TOP)

# ── B. footer 收尾闭合 main-col / shell ───────────────────────────────
rep('</footer>', '</footer>' + NL + '</div>' + NL + '</div>')

# ── C. KPI 指标条插入 main 顶部 ───────────────────────────────────────
rep('<main id="main">' + NL + NL + '<!-- 首页轮播 -->',
    '<main id="main">' + NL + NL + '<!-- 仪表盘：KPI 指标条 -->' + NL +
    '<div class="kpi-strip" id="kpiStrip" aria-label="作品概览"></div>' + NL + NL +
    '<!-- 首页轮播 -->')

# ── D. CSS：骨架样式（插到 head 主样式块末尾）──────────────────────────
CSS = blk('''  /* ---------- 仪表盘骨架：左侧栏 + 顶栏 + KPI 指标条 ---------- */
  #shell{display:flex;align-items:stretch;min-height:100vh}
  #sidebar{position:sticky;top:0;height:100vh;flex:0 0 232px;display:flex;flex-direction:column;gap:18px;
    padding:22px 14px 18px;background:var(--glass);border-right:1px solid var(--glass-line);
    backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);z-index:60}
  #sidebar .nav{display:flex;flex-direction:column;gap:4px;margin:4px 0 0;flex:1;overflow-y:auto}
  #sidebar .nav a{display:flex;align-items:center;gap:11px;padding:10px 13px;border-radius:12px;font-size:14px;color:var(--muted);white-space:nowrap}
  #sidebar .nav a svg{width:18px;height:18px;flex:0 0 auto;opacity:.9}
  .sb-foot{display:flex;flex-direction:column;gap:8px;padding:0 6px}
  .sb-foot .sync-pill{justify-content:center}
  .main-col{flex:1;min-width:0;display:flex;flex-direction:column}
  .tb-head{display:flex;align-items:baseline;gap:10px;min-width:0}
  .tb-head strong{font-size:17px;letter-spacing:.5px;white-space:nowrap}
  .tb-sub{font-size:12.5px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .tb-search{flex:1;max-width:460px;margin:0 auto;display:flex;align-items:center;gap:8px;
    padding:8px 14px;border-radius:999px;border:1px solid var(--line);background:var(--card)}
  .tb-search svg{width:15px;height:15px;color:var(--muted);flex:0 0 auto}
  .tb-search input{flex:1;min-width:0;border:none;outline:none;background:transparent;color:var(--ink);font-size:13.5px;font-family:inherit}
  .tb-search input::placeholder{color:var(--muted)}
  /* KPI 指标条（参考稿：label + 大数字 + 副行 + 图标徽章） */
  .kpi-strip{max-width:1180px;margin:0 auto;padding:30px 20px 0;display:grid;grid-template-columns:repeat(4,1fr);gap:18px}
  .kpi-strip .kpi{text-align:left;padding:16px 18px;display:flex;flex-direction:column;gap:2px}
  .kpi-strip .k-top{display:flex;align-items:center;justify-content:space-between;gap:8px}
  .kpi-strip .k-top .l{margin:0}
  .kpi-strip .k-ic{width:32px;height:32px;border-radius:10px;display:flex;align-items:center;justify-content:center;flex:0 0 auto;
    background:rgba(193,154,75,.1);color:var(--gold);border:1px solid rgba(193,154,75,.22)}
  body.dark .kpi-strip .k-ic{background:rgba(61,220,151,.12);border-color:rgba(61,220,151,.28)}
  .kpi-strip .v{margin-top:6px}
  .kpi-strip .s{font-size:12px;color:var(--muted)}
  @media(max-width:1080px){#sidebar{flex-basis:200px}}
  @media(max-width:768px){
    #sidebar{display:none}
    .tb-search{display:none}
    .tb-sub{display:none}
    .kpi-strip{grid-template-columns:repeat(2,1fr);padding-top:18px}
  }
''')
i = s.find('</style>')
assert i > 0 and s.count('</style>', 0, i) == 0
s = s[:i] + CSS + s[i:]

# ── E. JS：renderKpiStrip / syncTbTitle / setupTopSearch ──────────────
JS = blk('''/* 仪表盘：顶部 KPI 指标条（纯读渲染，refreshAll DAG 成员） */
function renderKpiStrip(){
  const el=document.getElementById('kpiStrip'); if(!el) return;
  const total=PHOTOS.length, favN=FAVS.size;
  const provs=new Set(PHOTOS.map(p=>p.pv).filter(Boolean)).size;
  const yrs=yearsList().length;
  const pct= total? Math.round(favN/total*100) : 0;
  el.innerHTML=[
    {v:total,l:'作品总数',s:'跨越 '+yrs+' 个年份',ic:'<path d="M4 8h3l2-3h6l2 3h3v11H4z"/><circle cx="12" cy="13" r="3.5"/>'},
    {v:favN,l:'已收藏',s:'占全部作品 '+pct+'%',ic:'<path d="M12 20s-7-4.6-9.3-8.8C1 8.4 3 5.5 6 5.5c2 0 3.4 1.2 4 2.4.6-1.2 2-2.4 4-2.4 3 0 5 2.9 3.3 5.7C15 15.4 12 20 12 20z"/>'},
    {v:provs,l:'足迹省份',s: total? '覆盖 '+new Set(PHOTOS.map(p=>p.r).filter(Boolean)).size+' 个地区':'从照片 EXIF 推断',ic:'<path d="M12 21s-6-5.2-6-10a6 6 0 1 1 12 0c0 4.8-6 10-6 10z"/><circle cx="12" cy="11" r="2.2"/>'},
    {v:yrs,l:'拍摄年份',s:'持续记录中',ic:'<rect x="4" y="5" width="16" height="16" rx="2"/><path d="M8 3v4M16 3v4M4 11h16"/>'}
  ].map(k=>`<div class="kpi"><div class="k-top"><span class="l">${k.l}</span><span class="k-ic"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">${k.ic}</svg></span></div><div class="v">${k.v}</div><div class="s">${k.s}</div></div>`).join('');
}

/* 顶栏标题跟随当前分区（侧栏导航点击 / 滚动高亮时同步） */
function syncTbTitle(){
  const tt=document.getElementById('tbTitle'); if(!tt) return;
  const a=document.querySelector('#nav a.active');
  if(a) tt.textContent=a.textContent.trim();
}

/* 顶栏全局搜索：输入即切到图库并同步其搜索框 */
function setupTopSearch(){
  const ts=document.getElementById('tbSearch'); if(!ts) return;
  ts.addEventListener('input',()=>{
    const sb=document.getElementById('searchBox');
    if(sb){ sb.value=ts.value; sb.dispatchEvent(new Event('input')); }
    if(ts.value.trim()){ const link=document.querySelector('#nav a[data-sec="gallery"]'); if(link) link.click(); }
  });
  ts.addEventListener('keydown',e=>{
    if(e.key==='Escape'){ ts.value=''; const sb=document.getElementById('searchBox'); if(sb){ sb.value=''; sb.dispatchEvent(new Event('input')); } }
  });
}

''')
rep('function setupNav(){', JS + 'function setupNav(){')

# setupNav 点击高亮后同步标题
rep(blk('''  links.forEach(a=>a.addEventListener('click',()=>{
    const sec=a.dataset.sec;
    links.forEach(x=>x.classList.toggle('active', x.dataset.sec===sec));
  }));'''),
    blk('''  links.forEach(a=>a.addEventListener('click',()=>{
    const sec=a.dataset.sec;
    links.forEach(x=>x.classList.toggle('active', x.dataset.sec===sec));
    syncTbTitle();
  }));'''))

# 滚动高亮后同步标题
rep(blk('''    entries.forEach(en=>{
      if(en.isIntersecting){
        const id=en.target.id;
        links.forEach(x=>x.classList.toggle('active', x.dataset.sec===id));
      }
    });'''),
    blk('''    entries.forEach(en=>{
      if(en.isIntersecting){
        const id=en.target.id;
        links.forEach(x=>x.classList.toggle('active', x.dataset.sec===id));
        syncTbTitle();
      }
    });'''))

# 初始化调用
rep('  setupNav();', '  setupNav();' + NL + '  setupTopSearch();')

# refreshAll 链首插入 KPI 条渲染
rep('function refreshAll(){' + NL + '  renderCarousel();',
    'function refreshAll(){' + NL + '  renderKpiStrip();   // 顶部 KPI 指标条（纯读渲染）' + NL + '  renderCarousel();')

# 打印时隐藏侧栏
rep('.topbar,.mobile-nav,.to-top,.data-bar,.lb,.map-legend,#mapZoomOut,.skip-link,.hero-btn,.dots,.drawer{display:none!important}',
    '#sidebar,.topbar,.mobile-nav,.to-top,.data-bar,.lb,.map-legend,#mapZoomOut,.skip-link,.hero-btn,.dots,.drawer{display:none!important}')

open(P, 'wb').write(s.encode('utf-8'))
print(f"OK {orig} -> {len(s)} chars, all assertions passed")

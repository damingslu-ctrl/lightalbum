# -*- coding: utf-8 -*-
"""1.7.4 图库浮层 + 系列优化：
① 分组画廊「查看全部」改为浮层展开（点照片 → 灯箱看信息），不再页内铺开
② 系列分类扩充（+夜阑灯火/雪国幻境/雾漫仙境，含 hint 规则与 band 映射）
③ 系列卡只展示有作品的分类；卡片底色改用该系列真实作品（渐变仅作兜底）
④ 系列饼图/图例过滤零计数；分组头计数改为实显数
铁律：CRLF、count==1 断言、零外链、单主 script、双主题、reduced-motion 兜底。
"""
import re

P = 'index.html'
raw = open(P, 'rb').read()
data = raw.decode('utf-8').replace('\r\n', '\n')

def rep(old, new, label):
    global data
    n = data.count(old)
    assert n == 1, 'REP FAIL [%s]: count=%d' % (label, n)
    data = data.replace(old, new)
    print('ok  %s' % label)

# ---------- 1. 系列区文案 ----------
rep('''    <div class="sec-sub">按光线与天象整理的四个长期系列，点击卡片可筛选图库中的对应作品。</div>''',
'''    <div class="sec-sub">按光线与天象整理的长期系列 —— 只展示有作品的分类，卡片封面即该系列真实作品。点击卡片筛选图库，点分组「查看全部」以浮层浏览整组。</div>''',
    'series sec-sub')

# ---------- 2. SERIES 扩充（全部复用既有色板） ----------
rep('''  '日月交辉': {color:'#c76b3a', desc:'日月同框、曙暮光与天体交辉的奇观。'}
};''',
'''  '日月交辉': {color:'#c76b3a', desc:'日月同框、曙暮光与天体交辉的奇观。'},
  '夜阑灯火': {color:'#3f6098', desc:'夜幕落下后的城市灯火与霓虹，人间烟火的另一面。'},
  '雪国幻境': {color:'#37e0ff', desc:'冰雪覆盖的纯白世界，万籁俱寂的冷冽之美。'},
  '雾漫仙境': {color:'#3ddc97', desc:'云雾缭绕的山水林间，虚实交界的氤氲氛围。'}
};''',
    'SERIES +3')

# ---------- 3. SERIES_HINT 新规则（更具体的放前面） ----------
rep('''const SERIES_HINT = [
  [/极光|aurora|银河|milky|星轨|star ?trail|夜空|night|星辰|星野/i, '星汉灿烂'],''',
'''const SERIES_HINT = [
  [/雪|snow|冰川|冰湖|雾凇|ice|winter/i, '雪国幻境'],
  [/雾|mist|fog|云海|cloud ?sea/i, '雾漫仙境'],
  [/夜市|灯火|霓虹|neon|夜幕|city ?night|街拍/i, '夜阑灯火'],
  [/极光|aurora|银河|milky|星轨|star ?trail|夜空|night|星辰|星野/i, '星汉灿烂'],''',
    'SERIES_HINT +3')

# ---------- 4. SERIES_BAND 映射 ----------
rep('''const SERIES_BAND = {'星汉灿烂':'astro', '蓝调时刻':'blue', '黄金时刻':'golden', '日月交辉':null};''',
'''const SERIES_BAND = {'星汉灿烂':'astro', '蓝调时刻':'blue', '黄金时刻':'golden', '日月交辉':null,
                     '夜阑灯火':null, '雪国幻境':null, '雾漫仙境':null};''',
    'SERIES_BAND')

# ---------- 5. renderSeries：空系列不展示 + 作品封面 ----------
rep('''function renderSeries(){
  const grid = document.getElementById('seriesGrid');
  grid.innerHTML = Object.keys(SERIES).map(k=>{
    const meta = SERIES[k];
    const cnt = PHOTOS.filter(p=>p.s===k).length;
    const active = state.series===k?'active':'';
    return `<div class="series-card ${active}" data-s="${k}" style="border-color:${meta.color}55"
        tabindex="0" role="button" aria-pressed="${state.series===k}" aria-label="系列 ${k}，${cnt} 幅作品，点击筛选图库">
      <div class="sc-ph" style="background:linear-gradient(135deg,${meta.color},#15140f)"></div>
      <div class="sc-mask"></div>
      <div class="sc-t">
        <div class="nm">${k}</div>
        <div class="ds">${meta.desc}</div>
        <div class="cnt">${cnt} 幅作品</div>
        <div class="go">→ 查看作品</div>
      </div>
    </div>`;
  }).join('');''',
'''function renderSeries(){
  const grid = document.getElementById('seriesGrid');
  const cards = Object.keys(SERIES).map(k=>{
    const meta = SERIES[k];
    const items = PHOTOS.filter(p=>p.s===k);
    const cnt = items.length;
    if(!cnt) return '';                      // 没有作品的系列不展示（需求）
    const rep = items.find(p=>p.img);        // 卡片底色用该系列真实作品（渐变只作兜底）
    const active = state.series===k?'active':'';
    return `<div class="series-card ${active}" data-s="${k}" style="border-color:${meta.color}55"
        tabindex="0" role="button" aria-pressed="${state.series===k}" aria-label="系列 ${k}，${cnt} 幅作品，点击筛选图库">
      <div class="sc-ph" style="background:linear-gradient(135deg,${meta.color},#15140f)">${rep?`<img class="sc-img" src="${rep.img}" alt="" loading="lazy" decoding="async" onerror="this.remove()">`:''}</div>
      <div class="sc-mask"></div>
      <div class="sc-t">
        <div class="nm">${k}</div>
        <div class="ds">${meta.desc}</div>
        <div class="cnt">${cnt} 幅作品</div>
        <div class="go">→ 查看作品</div>
      </div>
    </div>`;
  }).join('');
  grid.innerHTML = cards || '<div class="rl-empty">还没有系列作品 —— 导入作品后按光线与天象自动归类。</div>';''',
    'renderSeries rework')

# ---------- 6. 系列饼图/图例过滤零计数 ----------
rep('''  const sData=Object.keys(SERIES).map(k=>({c:SERIES[k].color,v:PHOTOS.filter(p=>p.s===k).length,k}));
  document.getElementById('chartSeries').innerHTML = svgPie(sData,{s:150});''',
'''  const sData=Object.keys(SERIES).map(k=>({c:SERIES[k].color,v:PHOTOS.filter(p=>p.s===k).length,k})).filter(d=>d.v>0);
  document.getElementById('chartSeries').innerHTML = sData.length ? svgPie(sData,{s:150}) : '<div class="hint" style="padding:36px 0;text-align:center">暂无作品</div>';''',
    'series pie filter')

# ---------- 7. 分组头「时段数」改为实显数 ----------
rep('''  html += `<div class="gsec-h gsec-top"><span class="gsec-t">按光影时段</span>`
        + `<span class="gsec-n">${Object.keys(SERIES).length} 个时段</span></div>`;''',
'''  const shownSeries = Object.keys(SERIES).filter(k=>list.some(p=>p.s===k));
  html += `<div class="gsec-h gsec-top"><span class="gsec-t">按光影时段</span>`
        + `<span class="gsec-n">${shownSeries.length} 个时段</span></div>`;''',
    'grouped header count')

# ---------- 8. 「查看全部」→ 浮层 ----------
rep('''  grid.querySelectorAll('.gsec-more[data-kind]').forEach(b=>{
    b.addEventListener('click', ()=>{
      if(b.dataset.kind==='series'){ state.series=b.dataset.key; state.cat=null; }
      else { state.cat=b.dataset.key; state.series=null; }
      refreshAll();
      const g=document.getElementById('galleryGrid');
      if(g && g.scrollIntoView) g.scrollIntoView({behavior:'smooth', block:'start'});
    });
  });''',
'''  grid.querySelectorAll('.gsec-more[data-kind]').forEach(b=>{
    b.addEventListener('click', ()=>openGroupOverlay(b.dataset.kind, b.dataset.key||''));
  });''',
    'gsec-more -> overlay')

# ---------- 9. 图集浮层 JS（挂在分组画廊与隐私库之间） ----------
rep('''  });
}

/* ---------- 隐私库 ---------- */''',
'''  });
}

/* ---------- 图集浮层：分组「查看全部」以浮层展开（不再页内铺开） ---------- */
function gcolItemsOf(kind, key){
  if(kind==='series') return PHOTOS.filter(p=>p.s===key);
  if(kind==='cat') return PHOTOS.filter(p=>catKey(p)===key);
  return [];
}
function openGroupOverlay(kind, key){
  const ov=document.getElementById('gcolOv'); if(!ov) return;
  const grid=document.getElementById('gcolGrid'); if(!grid) return;
  let title='作品集', items=[];
  if(kind==='series'){ items=gcolItemsOf('series',key); title=key; }
  else if(kind==='cat'){ items=gcolItemsOf('cat',key); title=key; }
  else if(kind==='vault'){
    title='🔒 隐私库';
    items=(VAULT.items||[]).map(it=>({vid:it.id, name:it.name||'隐私照片'}));
  }
  if(!items.length) return;
  document.getElementById('gcolTitle').textContent=title;
  document.getElementById('gcolCount').textContent=items.length+' 幅';
  if(kind==='vault'){
    grid.innerHTML=items.map(it=>
      `<div class="g-item" data-vid="${it.vid}" tabindex="0" role="button" aria-label="隐私照片 ${String(it.name).replace(/</g,'&lt;')}">`
      +`<img class="ph" src="/api/vault/thumb/${it.vid}" alt="" loading="lazy" style="width:100%;display:block">`
      +`<div class="gi-cap"><div class="gi-t">${String(it.name).replace(/</g,'&lt;')}</div></div></div>`
    ).join('');
  } else {
    grid.innerHTML=items.map(galleryCardHtml).join('');
    bindGalleryItems([...grid.querySelectorAll('.g-item:not([data-vid])')]);
  }
  grid.classList.add('no-anim');
  ov.hidden=false;
}
function closeGroupOverlay(){
  const ov=document.getElementById('gcolOv'); if(!ov || ov.hidden) return;
  ov.hidden=true;
  const grid=document.getElementById('gcolGrid'); if(grid) grid.innerHTML='';
}

/* ---------- 隐私库 ---------- */''',
    'overlay JS')

# ---------- 10. 全局键盘：浮层 Escape（灯箱优先级更高，早退在前） ----------
rep('''    const tag=e.target.tagName;
    if(tag==='INPUT'||tag==='TEXTAREA'||tag==='SELECT') return;''',
'''    const gcolOv=document.getElementById('gcolOv');
    if(gcolOv && !gcolOv.hidden){
      if(e.key==='Escape') closeGroupOverlay();
      return;                                            // 图集浮层打开时屏蔽其余快捷键
    }
    const tag=e.target.tagName;
    if(tag==='INPUT'||tag==='TEXTAREA'||tag==='SELECT') return;''',
    'keydown overlay guard')

# ---------- 11. 浮层 HTML（body 末尾） ----------
rep('''<!-- 内嵌编辑台已迁入 #retouch 左列（.rl-main），此处不再有弹出模态 -->''',
'''<!-- 内嵌编辑台已迁入 #retouch 左列（.rl-main），此处不再有弹出模态 -->

<!-- 图集浮层：分组「查看全部」的悬浮浏览层（z-index 90，低于灯箱 100 —— 点照片在浮层之上开灯箱看信息） -->
<div class="gcol" id="gcolOv" hidden>
  <div class="gcol-mask" id="gcolMask"></div>
  <div class="gcol-panel" role="dialog" aria-modal="true" aria-label="作品集合浏览">
    <div class="gcol-head">
      <div class="gcol-tt"><strong id="gcolTitle">作品集</strong><span class="gcol-n" id="gcolCount">0 幅</span></div>
      <button class="btn sm" id="gcolClose" type="button">关闭</button>
    </div>
    <div class="gcol-body"><div class="gallery" id="gcolGrid"></div></div>
  </div>
</div>''',
    'overlay HTML')

# ---------- 12. 浮层 CSS（head 样式块末尾 = 第一个 </style> 前） ----------
overlay_css = '''  /* ---------- 图集浮层：分组「查看全部」浮层展开 ---------- */
  .gcol{position:fixed;inset:0;z-index:90;display:flex;align-items:center;justify-content:center;padding:22px}
  .gcol[hidden]{display:none}
  .gcol-mask{position:absolute;inset:0;background:rgba(18,16,12,.66);backdrop-filter:blur(6px)}
  .gcol-panel{position:relative;z-index:1;width:min(1240px,96vw);max-height:90vh;display:flex;flex-direction:column;
    background:var(--glass);border:1px solid var(--glass-line);border-radius:18px;overflow:hidden;
    box-shadow:var(--shadow-lg);backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);
    animation:gcolIn var(--dur-3) var(--ease-out)}
  @keyframes gcolIn{from{opacity:0;transform:translateY(16px) scale(.985)}to{opacity:1;transform:none}}
  .gcol-head{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:13px 18px;border-bottom:1px solid var(--line)}
  .gcol-tt{display:flex;align-items:baseline;gap:10px;min-width:0}
  .gcol-tt strong{font-size:17px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .gcol-n{font-size:12.5px;color:var(--muted);white-space:nowrap}
  .gcol-body{overflow-y:auto;padding:16px 18px;overscroll-behavior:contain}
  /* 系列卡：作品封面（渐变兜底之上） */
  .series-card .sc-img{position:absolute;inset:0;width:100%;height:100%;object-fit:cover}
'''
i = data.find('</style>')
j = data.find('<script id="fabric-lib">')
assert 0 < i < j, 'first </style> position bad: %d / %d' % (i, j)
data = data[:i] + overlay_css + data[i:]
print('ok  overlay CSS')

# ---------- 绑定浮层关闭（放 overlay JS 之后、隐私库之前再补一段绑定） ----------
# 放在 #gcolOv 存在的时机之外也可以：脚本体直接执行时 DOM 已就绪（主 script 在 body 尾）。
rep('''function closeGroupOverlay(){
  const ov=document.getElementById('gcolOv'); if(!ov || ov.hidden) return;
  ov.hidden=true;
  const grid=document.getElementById('gcolGrid'); if(grid) grid.innerHTML='';
}''',
'''function closeGroupOverlay(){
  const ov=document.getElementById('gcolOv'); if(!ov || ov.hidden) return;
  ov.hidden=true;
  const grid=document.getElementById('gcolGrid'); if(grid) grid.innerHTML='';
}
(function(){
  const ov=document.getElementById('gcolOv'); if(!ov) return;
  const c=document.getElementById('gcolClose'); if(c) c.addEventListener('click', closeGroupOverlay);
  const m=document.getElementById('gcolMask'); if(m) m.addEventListener('click', closeGroupOverlay);
})();''',
    'overlay close bindings')

# ---------- 写回 ----------
out = data.replace('\n', '\r\n').encode('utf-8')
open(P, 'wb').write(out)

# ---------- 终检 ----------
final = out.decode('utf-8')
for k in ['gcolOv','gcolMask','gcolTitle','gcolCount','gcolGrid','gcolClose']:
    assert final.count('id="%s"' % k) == 1, k
assert final.count('夜阑灯火') >= 3 and final.count('雪国幻境') >= 3 and final.count('雾漫仙境') >= 3
assert final.count('.series-card .sc-img') == 1
assert final.count('openGroupOverlay') >= 2 and final.count('closeGroupOverlay') >= 4
assert final.count('z-index:90') == 1
ext = re.findall(r'(?:src|href)="(?:https?:)?//', final)
assert not ext, ext
print('ALL REPS DONE  size=%dKB' % (len(out)//1024))

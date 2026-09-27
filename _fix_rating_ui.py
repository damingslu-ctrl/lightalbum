# -*- coding: utf-8 -*-
"""1.9.13 页面侧：星级评分 UI（显示 / 点击评分 / 筛选 / 排序）。"""
import io
import sys

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html'
s = io.open(P, encoding='utf-8').read()

ED = [
    # 1) 排序：加「星级：高 → 低」
    ('<option value="aperture">光圈：大 → 小</option>',
     '<option value="aperture">光圈：大 → 小</option>\n'
     '        <option value="rating">星级：高 → 低</option>'),

    # 2) sortPhotos 支持 rating
    ("  else if(mode==='aperture') a.sort((x,y)=>apertNum(x)-apertNum(y));",
     "  else if(mode==='aperture') a.sort((x,y)=>apertNum(x)-apertNum(y));\n"
     "  else if(mode==='rating') a.sort((x,y)=>(Number(y.rating)||0)-(Number(x.rating)||0));"),

    # 3) filteredPhotos 支持最低星级
    ("    if(state.fav && !FAVS.has(p.id)) return false;",
     "    if(state.fav && !FAVS.has(p.id)) return false;\n"
     "    // 星级：minRating = 最低星级（1-5）；0 / 未设 = 不过滤\n"
     "    if(state.minRating && (Number(p.rating)||0) < state.minRating) return false;"),

    # 4) 筛选条：加 ≥1★ … ≥5★ 筹码
    ("""  html += `<button class="chip ${state.fav?'on':''}" data-f="1" aria-pressed="${state.fav}">★ 仅看收藏 <span style="opacity:.6">${FAVS.size}</span></button>`;""",
     """  html += `<button class="chip ${state.fav?'on':''}" data-f="1" aria-pressed="${state.fav}">★ 仅看收藏 <span style="opacity:.6">${FAVS.size}</span></button>`;
  // 星级筛选：点中再点一次即取消（minRating 归 0）
  html += [1,2,3,4,5].map(n=>`<button class="chip ${state.minRating===n?'on':''}" data-r="${n}" aria-pressed="${state.minRating===n}">≥${n}★ <span style="opacity:.6">${rateCnt(n)}</span></button>`).join('');"""),

    # 5) 星级筹码点击
    ("    b.addEventListener('click',()=>{ state.fav=!state.fav; refreshAll(); });",
     "    b.addEventListener('click',()=>{ state.fav=!state.fav; refreshAll(); });\n"
     "    b.addEventListener('click',()=>{ state.minRating = (state.minRating===+b.dataset.r) ? 0 : +b.dataset.r; refreshAll(); });"),

    # 6) syncFilterUI 同步星级筹码高亮
    ("  document.querySelectorAll('#orientRow .chip[data-o]').forEach(b=>b.classList.toggle('on', b.dataset.o===state.orient));",
     "  document.querySelectorAll('#orientRow .chip[data-o]').forEach(b=>b.classList.toggle('on', b.dataset.o===state.orient));\n"
     "  document.querySelectorAll('#orientRow .chip[data-r]').forEach(b=>b.classList.toggle('on', +b.dataset.r===state.minRating));"),

    # 7) 卡片上的星星
    ("""      <div class="ov"><div class="tt">${p.t}</div></div>""",
     """      <div class="ov"><div class="tt">${p.t}</div>${starHtml(p)}</div>"""),

    # 8) 点星星不要开灯箱
    ("      if(e.target.closest('.fav')) return;              // 点星标不打开大图",
     "      if(e.target.closest('.fav')) return;              // 点星标不打开大图\n"
     "      if(e.target.closest('.stars')) return;            // 点星级条不打开大图"),

    # 9) 绑定星星点击
    ("""      openLightbox(_id);
    });""",
     """      openLightbox(_id);
    });
    el.querySelectorAll('.stars .st').forEach(b=>{
      b.addEventListener('click',(e)=>{ e.stopPropagation(); setRating(+el.dataset.id, +b.dataset.n); });
    });"""),

    # 10) CSS
    ("  .g-item .ph{width:100%;display:block;position:relative;overflow:hidden}",
     "  .g-item .ph{width:100%;display:block;position:relative;overflow:hidden}\n"
     "  /* 星级（1.9.13）：卡片左下角，点击直接评分 */\n"
     "  .g-item .stars{position:absolute;left:8px;bottom:8px;display:flex;gap:1px;z-index:2}\n"
     "  .g-item .stars .st{border:none;background:none;padding:2px;line-height:1;font-size:14px;\n"
     "    cursor:pointer;color:rgba(255,255,255,.7);text-shadow:0 1px 3px rgba(0,0,0,.6)}\n"
     "  .g-item .stars .st.on{color:#ffcf5c}\n"
     "  .g-item .stars .st:hover{transform:scale(1.15)}"),
]

for i, (o, n) in enumerate(ED, 1):
    c = s.count(o)
    if c != 1:
        print('FAIL edit %d count=%d' % (i, c))
        sys.exit(1)
    s = s.replace(o, n)
    print('  ok edit %d' % i)

# 11) 新增 starHtml / rateCnt / setRating（放在 galleryCardHtml 之前）
anchor = 'function galleryCardHtml(p){'
assert s.count(anchor) == 1
helpers = '''// ---- 星级（1.9.13）----
function starHtml(p){
  const r = Number(p.rating)||0;
  let s = '<div class="stars" data-id="'+p.id+'">';
  for(let i=1;i<=5;i++) s += '<button class="st'+(i<=r?' on':'')+'" data-n="'+i+'" aria-label="评 '+i+' 星">'+(i<=r?'\\u2605':'\\u2606')+'</button>';
  return s + '</div>';
}
function rateCnt(n){
  return PHOTOS.filter(p=>(Number(p.rating)||0) >= n).length;
}
async function setRating(id, n){
  const p = PHOTOS.find(x=>x.id===id);
  if(!p) return;
  const cur = Number(p.rating)||0;
  const next = (cur===n) ? 0 : n;          // 再点同一颗 = 取消评级
  try{
    const r = await fetch('/api/rating', {method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({path: p.path, rating: next})});
    const j = await r.json();
    if(j && j.ok !== false){ p.rating = next; refreshAll(); }
  }catch(e){ /* 离线/无服务时静默：不让评分交互抛错打断浏览 */ }
}

'''
s = s.replace(anchor, helpers + anchor)

io.open(P, 'w', encoding='utf-8').write(s)
print('WROTE index.html (%d bytes)' % len(s))
for probe in ["function starHtml", "function rateCnt", "async function setRating",
              'value="rating"', "state.minRating", ".stars .st", "≥1★"]:
    print(('  OK   ' if probe in s else '  MISS '), probe)

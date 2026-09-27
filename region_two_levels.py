# -*- coding: utf-8 -*-
"""1.7.6 拍摄地两级树：
- 一级：国家（主体点击=下钻地图+筛选；箭头=展开/收起该国「省市」子树，状态存 state._rlOpen）
- 二级：省市 —— 省行（点击下钻到该省）+ 缩进城市行（点击浮层浏览该市全部作品）
- 旧「下钻后才出现的省份子列表」(renderProvList) 由树取代并删除
- openGroupOverlay 扩展 kind='city'（带 rid 上下文过滤）
铁律：CRLF、count==1 断言、零外链、reduced-motion 由全局兜底。
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

# ---------- 1. renderRegionList → 两级树 ----------
rep('''// 右侧「拍摄地」列表（国家一级）
function renderRegionList(counts){
  const rl = document.getElementById('regionList'); if(!rl) return;
  const total = PHOTOS.length;
  const used = usedRegions();
  const sorted = used.sort((a,b)=>(counts[b.id]||0)-(counts[a.id]||0));
  rl.innerHTML = '<div class="rl-head"><span>拍摄地</span><span class="n">'+used.length+' 地 · '+total+
      ' 幅</span><button class="clr '+(state.region?'show':'')+'" id="rlClear" type="button">清除筛选</button></div>'+
    sorted.map(r=>{
      const c=counts[r.id]||0;
      const sel = state.region===r.id?'sel':'';
      return '<div class="rl-item '+sel+'" data-id="'+r.id+'" tabindex="0" role="button" aria-pressed="'+(state.region===r.id)+'" '+
        'aria-label="'+escHtml(r.name)+'，'+c+' 幅作品，点击下钻"><span>'+escHtml(r.name)+'</span><span class="ct">'+c+' 幅</span></div>';
    }).join('');
  rl.querySelectorAll('.rl-item').forEach(el=>{
    const R=REGIONS.find(x=>x.id===el.dataset.id);
    el.addEventListener('click',()=>selectRegion(el.dataset.id));
    el.addEventListener('keydown',e=>{
      if(e.key==='Enter'||e.key===' '){ e.preventDefault(); selectRegion(el.dataset.id); }
    });
    el.addEventListener('mouseenter',()=>{ highlightArcs(R.id); showPreview(R,false); });
    el.addEventListener('mouseleave',()=>{ if(state.region!==R.id){ clearArcs(); hidePreview(); } });
  });
  const clrBtn=document.getElementById('rlClear');
  if(clrBtn) clrBtn.addEventListener('click',(e)=>{ e.stopPropagation(); if(state.region) mapTo(0); });
}''',
'''// 右侧「拍摄地」两级树：一级国家（点击下钻 / 箭头展开省市），二级省市（省行下钻、市行浮层）
function renderRegionList(counts){
  state._rlOpen = state._rlOpen || {};
  state._rlCounts = counts;                     // 折叠/展开只重绘本列表时复用最新计数
  const rl = document.getElementById('regionList'); if(!rl) return;
  const total = PHOTOS.length;
  const used = usedRegions();
  const sorted = used.sort((a,b)=>(counts[b.id]||0)-(counts[a.id]||0));
  rl.innerHTML = '<div class="rl-head"><span>拍摄地</span><span class="n">'+used.length+' 地 · '+total+
      ' 幅</span><button class="clr '+(state.region?'show':'')+'" id="rlClear" type="button">清除筛选</button></div>'+
    sorted.map(r=>{
      const c=counts[r.id]||0;
      const sel = state.region===r.id?'sel':'';
      const tree = provCityTree(r.id);
      const open = !!state._rlOpen[r.id];
      const tw = tree.length
        ? '<span class="tw" data-tw="'+r.id+'" role="button" tabindex="0" aria-expanded="'+open+'" aria-label="展开或收起 '+escHtml(r.name)+' 的省市">'+
          '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"/></svg></span>'
        : '';
      const sub = (open && tree.length) ? renderProvCitySub(r.id, tree) : '';
      return '<div class="rl-item '+sel+(open?' exp':'')+'" data-id="'+r.id+'" tabindex="0" role="button" aria-pressed="'+(state.region===r.id)+'" '+
        'aria-label="'+escHtml(r.name)+'，'+c+' 幅作品，点击下钻"><span>'+escHtml(r.name)+'</span><span class="ct">'+c+' 幅</span>'+tw+'</div>'+sub;
    }).join('');
  rl.querySelectorAll('.rl-item').forEach(el=>{
    const R=REGIONS.find(x=>x.id===el.dataset.id);
    el.addEventListener('click',()=>selectRegion(el.dataset.id));
    el.addEventListener('keydown',e=>{
      if(e.key==='Enter'||e.key===' '){ e.preventDefault(); selectRegion(el.dataset.id); }
    });
    el.addEventListener('mouseenter',()=>{ highlightArcs(R.id); showPreview(R,false); });
    el.addEventListener('mouseleave',()=>{ if(state.region!==R.id){ clearArcs(); hidePreview(); } });
  });
  rl.querySelectorAll('.tw').forEach(el=>{
    el.addEventListener('click',e=>{
      e.stopPropagation();
      const rid=el.dataset.tw;
      state._rlOpen[rid]=!state._rlOpen[rid];
      renderRegionList(state._rlCounts||{});    // 只重绘列表，不动地图
    });
    el.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); el.click(); } });
  });
  bindProvCityRows(rl);
  const clrBtn=document.getElementById('rlClear');
  if(clrBtn) clrBtn.addEventListener('click',(e)=>{ e.stopPropagation(); if(state.region) mapTo(0); });
}''',
    'renderRegionList tree')

# ---------- 2. renderProvList → 省市子树 builder ----------
rep('''// 右侧列表追加「省份」子列表（下钻到国家后出现）
function renderProvList(){
  const rl = document.getElementById('regionList'); if(!rl) return;
  if((state.mapLevel||0)<1 || !state.region) return;
  const list=provincesOf(state.region), cnt=provCounts(state.region);
  const rows=list.filter(p=>cnt[p[0]]).sort((a,b)=>cnt[b[0]]-cnt[a[0]]);
  if(!rows.length) return;
  const box=document.createElement('div');
  box.className='rl-sub';
  box.innerHTML='<div class="rl-head"><span>'+escHtml(regionName(state.region))+' · 省份</span><span class="n">'+rows.length+
      ' 省 · '+rows.reduce((s,p)=>s+cnt[p[0]],0)+' 幅</span></div>'+
    rows.map((p,i)=>'<div class="rl-item '+(state.prov===p[0]?'sel':'')+'" data-pv="'+escHtml(p[0])+'" tabindex="0" role="button" '+
      'style="--i:'+Math.min(i,12)+'" aria-pressed="'+(state.prov===p[0])+'"><span>'+escHtml(provName(p[0]))+'</span><span class="ct">'+cnt[p[0]]+' 幅</span></div>').join('');
  rl.appendChild(box);
  box.querySelectorAll('.rl-item').forEach(el=>{
    el.addEventListener('click',()=>selectProvince(el.dataset.pv));
    el.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); selectProvince(el.dataset.pv); } });
  });
}''',
'''// 某国的「省市」树数据：按省(pv)分组，省内按市(ct)计数；无省名的归「未细分」（只列有作品的）
function provCityTree(rid){
  const byProv = {};
  (PHOTOS||[]).forEach(p=>{
    if(p.r!==rid) return;
    const k = p.pv || (p.ct ? '未细分' : '');
    if(!k) return;
    const g = byProv[k] = byProv[k] || {n:0, cities:{}};
    g.n++;
    if(p.ct) g.cities[p.ct]=(g.cities[p.ct]||0)+1;
  });
  return Object.keys(byProv).map(k=>({
      pv:k, n:byProv[k].n,
      cities:Object.keys(byProv[k].cities).map(c=>({ct:c,n:byProv[k].cities[c]})).sort((a,b)=>b.n-a.n)
    })).sort((a,b)=>b.n-a.n);
}
function renderProvCitySub(rid, tree){
  const total = tree.reduce((s,t)=>s+t.n,0);
  return '<div class="rl-sub" data-rid="'+rid+'">'+
    '<div class="rl-head"><span>'+escHtml(regionName(rid))+' · 省市</span><span class="n">'+tree.length+' 组 · '+total+' 幅</span></div>'+
    tree.map(t=>{
      const pvLabel = t.pv==='未细分' ? '未细分' : (provName(t.pv)||t.pv);
      const isSel = state.prov===t.pv && state.region===rid;
      const cities = t.cities.map(ci=>
        '<div class="rl-city" data-ct="'+escHtml(ci.ct)+'" data-rid="'+rid+'" tabindex="0" role="button" '+
        'aria-label="城市 '+escHtml(ci.ct)+'，'+ci.n+' 幅作品，点击浏览"><i></i><span>'+escHtml(ci.ct)+'</span><span class="ct">'+ci.n+' 幅</span></div>'
      ).join('');
      return '<div class="rl-item '+(isSel?'sel':'')+'" data-pv="'+escHtml(t.pv)+'" data-rid="'+rid+'" tabindex="0" role="button" aria-pressed="'+isSel+'" '+
        'aria-label="省份 '+escHtml(pvLabel)+'，'+t.n+' 幅作品，点击下钻"><span>'+escHtml(pvLabel)+'</span><span class="ct">'+t.n+' 幅</span></div>'+cities;
    }).join('')+
  '</div>';
}
function bindProvCityRows(rl){
  // 省行：未下钻该国时先下钻国家，再下钻到省（selectProvince 自带同省再点收回的 toggle）
  rl.querySelectorAll('.rl-sub .rl-item[data-pv]').forEach(el=>{
    el.addEventListener('click',()=>{
      const rid=el.dataset.rid, pv=el.dataset.pv;
      state._rlOpen = state._rlOpen || {};
      state._rlOpen[rid]=true;
      if(state.region!==rid) selectRegion(rid);
      selectProvince(pv);
    });
    el.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); el.click(); } });
  });
  // 市行：浮层浏览该市全部作品（不开新筛选状态）
  rl.querySelectorAll('.rl-city[data-ct]').forEach(el=>{
    el.addEventListener('click',()=>openGroupOverlay('city', el.dataset.ct, el.dataset.rid));
    el.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); el.click(); } });
  });
}''',
    'prov-city tree builders')

# ---------- 3. renderMap 去掉旧子列表调用 ----------
rep('''  renderRegionList(counts);
  renderProvList();
  renderPhotoMarkers();''',
'''  renderRegionList(counts);
  renderPhotoMarkers();''',
    'remove renderProvList call')

# ---------- 4. openGroupOverlay：签名加 ctx + city 分支 ----------
rep('''function openGroupOverlay(kind, key){
  const ov=document.getElementById('gcolOv'); if(!ov) return;
  const grid=document.getElementById('gcolGrid'); if(!grid) return;
  let title='作品集', items=[];
  if(kind==='series'){ items=gcolItemsOf('series',key); title=key; }
  else if(kind==='cat'){ items=gcolItemsOf('cat',key); title=key; }''',
'''function openGroupOverlay(kind, key, ctx){
  const ov=document.getElementById('gcolOv'); if(!ov) return;
  const grid=document.getElementById('gcolGrid'); if(!grid) return;
  let title='作品集', items=[];
  if(kind==='series'){ items=gcolItemsOf('series',key); title=key; }
  else if(kind==='cat'){ items=gcolItemsOf('cat',key); title=key; }
  else if(kind==='city'){ items=PHOTOS.filter(p=>p.ct===key && (!ctx || p.r===ctx)); title=key; }''',
    'overlay city kind')

# ---------- 5. selectRegion 下钻自动展开该国子树 ----------
rep('''    state.region=id; state.prov=null; state.mapLevel=1; state.mapZoom=id;
    // 下钻时清空其余筛选，避免多条件叠加后出现「空结果」的困惑
    state.series=null; state.year='all'; state.orient='all'; state.fav=false; state.q='';''',
'''    state.region=id; state.prov=null; state.mapLevel=1; state.mapZoom=id;
    // 下钻时清空其余筛选，避免多条件叠加后出现「空结果」的困惑
    state.series=null; state.year='all'; state.orient='all'; state.fav=false; state.q='';
    state._rlOpen = state._rlOpen || {}; state._rlOpen[id]=true;   // 下钻即展开该国「省市」子树''',
    'selectRegion auto-expand')

# ---------- 6. CSS：折叠箭头 + 城市行 ----------
rep('''  @keyframes rlSubItem{from{opacity:0;transform:translateX(-8px)}to{opacity:1;transform:none}}''',
'''  @keyframes rlSubItem{from{opacity:0;transform:translateX(-8px)}to{opacity:1;transform:none}}
  /* 拍摄地两级树：一级国家折叠箭头 + 二级缩进城市行 */
  .region-list .rl-item .tw{flex:0 0 auto;width:18px;height:18px;display:inline-flex;align-items:center;justify-content:center;
    margin-left:8px;border-radius:5px;color:var(--muted);transition:transform var(--dur-2) var(--ease-std)}
  .region-list .rl-item .tw svg{width:11px;height:11px}
  .region-list .rl-item .tw:hover{color:var(--gold)}
  .region-list .rl-item.exp .tw{transform:rotate(90deg)}
  .region-list .rl-sub{margin-top:4px;margin-bottom:10px}
  .region-list .rl-city{display:flex;justify-content:space-between;align-items:center;gap:8px;
    padding:6px 12px 6px 28px;border-radius:9px;margin-bottom:4px;
    font-size:12.5px;color:var(--muted);cursor:pointer;transition:var(--dur-1);
    animation:rlSubItem .34s cubic-bezier(.22,.61,.36,1) both}
  .region-list .rl-city:hover{color:var(--ink);background:var(--card);border-color:var(--gold)}
  .region-list .rl-city i{flex:0 0 4px;width:4px;height:4px;border-radius:50%;background:var(--gold);opacity:.6}
  .region-list .rl-city .ct{font-size:12px;font-variant-numeric:tabular-nums}
  body.dark .region-list .rl-city:hover{background:rgba(255,255,255,.06)}''',
    'tree CSS')

# ---------- 写回 ----------
out = data.replace('\n', '\r\n').encode('utf-8')
open(P, 'wb').write(out)

# ---------- 终检 ----------
final = out.decode('utf-8')
assert final.count('provCityTree') == 3          # 定义 + 调用 + 注释
assert final.count('renderProvCitySub') == 2     # 定义 + 调用
assert final.count('bindProvCityRows') == 2      # 定义 + 调用
assert final.count('renderProvList') == 0        # 旧函数彻底移除
assert final.count("kind==='city'") == 1
assert final.count('.rl-city') >= 5
ext = re.findall(r'(?:src|href)="(?:https?:)?//', final)
assert not ext, ext
print('ALL REPS DONE  size=%dKB' % (len(out)//1024))

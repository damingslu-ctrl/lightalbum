# -*- coding: utf-8 -*-
"""1.9.29 前端：把选片(Culling)/AI美图(Enhance)两个隐藏模块接成真实可用 UI。

全部 rep() 带 count==1 断言（规避大文件 Edit 静默不落盘）。
插入点：
  A) 桌面导航：图库后加「智能选片」，修图后加「AI 美图」
  B) 手机导航：同理（用 2 空格缩进区分）
  C) 两个 <section> 插在修图段与摄影日志之间
  D) CSS 插在第一个 </style> 之前（head 样式块）
  E) IntersectionObserver 的 secs 数组补 cull/enhance
  F) DOMContentLoaded 里调用 bindCull()/bindEnhance()
  G) 主 <script> 内新增 JS 函数块（在 DOMContentLoaded 注册之前）
"""
import io

H = r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html'


def rep(old, new):
    with io.open(H, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'anchor 出现 %d 次（应=1）:\n%r' % (c, old[:90])
    s = s.replace(old, new, 1)
    with io.open(H, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK (old %d -> new %d)' % (len(old), len(new)))


# ---------- A) 桌面导航（4 空格缩进区分） ----------
rep('图库</a>\n    <a href="#retouch"',
    '图库</a>\n    <a href="#cull" data-sec="cull"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5h16l-6 7v6l-4 2v-8z"/></svg>智能选片</a>\n    <a href="#retouch"')
rep('修图</a>\n    <a href="#log"',
    '修图</a>\n    <a href="#enhance" data-sec="enhance"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M15 4l1 2 2 1-2 1-1 2-1-2-2-1 2-1z"/><path d="M4 20l8-8M11 13l3 3"/></svg>AI 美图</a>\n    <a href="#log"')

# ---------- B) 手机导航（2 空格缩进区分） ----------
rep('图库</a>\n  <a href="#retouch"',
    '图库</a>\n  <a href="#cull" data-sec="cull">\n    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 5h16l-6 7v6l-4 2v-8z"/></svg>智能选片</a>\n  <a href="#retouch"')
rep('修图</a>\n  <a href="#log"',
    '修图</a>\n  <a href="#enhance" data-sec="enhance">\n    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M15 4l1 2 2 1-2 1-1 2-1-2-2-1 2-1z"/><path d="M4 20l8-8M11 13l3 3"/></svg>AI 美图</a>\n  <a href="#log"')

# ---------- C) 两个 section ----------
sections = '''
<!-- 智能选片 · Culling -->
<section id="cull">
  <div class="sec-head">
    <div class="sec-title">智能选片</div>
    <div class="sec-sub">从海量照片里本地筛出值得留下的好照片：精确 MD5 去重 + 感知哈希相似去重，剔除模糊 / 严重过曝欠曝 / 低分辨率，并按综合评分挑出 Top N。全部本地计算，零云端。</div>
  </div>
  <div class="card cull-card">
    <div class="cull-opts">
      <label class="cull-field"><span>范围</span>
        <select id="cullScope"><option value="all">整个图库</option><option value="selection">当前选择集</option></select>
      </label>
      <label class="cull-field"><span>去重强度</span>
        <select id="cullDedup">
          <option value="关闭">关闭</option><option value="极严">极严</option><option value="严格">严格</option>
          <option value="标准" selected>标准</option><option value="宽松">宽松</option><option value="极宽">极宽</option>
        </select>
      </label>
      <label class="cull-field"><span>模糊阈值</span>
        <input id="cullBlur" type="number" value="80" min="0" step="5">
      </label>
      <label class="cull-field"><span>最少百万像素</span>
        <input id="cullMp" type="number" value="0.5" min="0" step="0.1">
      </label>
      <label class="cull-field"><span>精选张数</span>
        <input id="cullTop" type="number" value="10" min="1" max="200" step="1">
      </label>
    </div>
    <div class="cull-acts">
      <button class="btn primary" id="cullRun">开始选片</button>
      <button class="btn" id="cullExport" disabled>导出整理…</button>
      <span class="cull-msg" id="cullMsg" role="status" aria-live="polite"></span>
    </div>
    <div class="cull-summary" id="cullSummary" hidden>
      <div class="cull-cols">
        <div class="cull-col"><h4>精选 Top <span id="cullTopN">0</span></h4><div class="cull-grid" id="cullTopGrid"></div></div>
        <div class="cull-col"><h4>去重组 <span id="cullDupN">0</span></h4><div class="cull-dups" id="cullDups"></div></div>
      </div>
    </div>
  </div>
</section>

<!-- AI 美图 · Enhance -->
<section id="enhance">
  <div class="sec-head">
    <div class="sec-title">AI 美图</div>
    <div class="sec-sub">先由库内 AI 分类判断风景 / 人像，再路由到对应本地模型做智能调色增强（3DLUT）。全部本地推理，零云端。未分类的照片不猜测、不处理。</div>
  </div>
  <div class="card enh-card">
    <div class="enh-top">
      <span class="enh-status off" id="enhStatus">AI 美图服务：检测中…</span>
      <button class="btn" id="enhPick">选择照片</button>
    </div>
    <div class="enh-body">
      <div class="enh-pane"><div class="enh-label">原图</div><div class="enh-img" id="enhBefore"></div></div>
      <div class="enh-pane"><div class="enh-label">增强后</div><div class="enh-img" id="enhAfter"></div></div>
    </div>
    <div class="enh-ctrls">
      <label>照片：<span id="enhName">未选择</span></label>
      <label>强度 <input type="range" id="enhStrength" min="0" max="1" step="0.05" value="1"><span id="enhStrengthVal">1.00</span></label>
      <button class="btn primary" id="enhRun">增强</button>
      <button class="btn" id="enhOpen" style="display:none">在资源管理器打开</button>
      <span class="cull-msg" id="enhMsg" role="status" aria-live="polite"></span>
    </div>
  </div>
</section>

'''
sec_anchor = '''</section>

<!-- 摄影日志 -->'''
rep(sec_anchor, sections + '\n<!-- 摄影日志 -->')

# ---------- D) CSS ----------
css = """
/* ===== 1.9.29 选片 / AI美图 模块 ===== */
.cull-card,.enh-card{max-width:1180px;margin:0 auto 28px}
.cull-opts{display:flex;flex-wrap:wrap;gap:14px 22px;align-items:flex-end;margin-bottom:16px}
.cull-field{display:flex;flex-direction:column;gap:6px;font-size:13px;color:var(--muted,#8a93a3)}
.cull-field select,.cull-field input{background:var(--panel,#0f1622);color:inherit;border:1px solid var(--line,#243044);
  border-radius:8px;padding:7px 10px;font-size:14px;min-width:120px}
.cull-acts{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:14px}
.cull-msg{font-size:13px;color:var(--muted,#8a93a3)}
.cull-summary[hidden]{display:none}
.cull-cols{display:grid;grid-template-columns:1fr 1fr;gap:22px}
@media(max-width:768px){.cull-cols{grid-template-columns:1fr}}
.cull-col h4{font-size:14px;margin:0 0 10px;color:var(--ink,#e8edf4)}
.cull-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(96px,1fr));gap:10px}
.cull-item{background:var(--panel,#0f1622);border:1px solid var(--line,#243044);border-radius:10px;overflow:hidden;font-size:12px}
.cc-img{aspect-ratio:1/1;background:#0a0f17;display:flex;align-items:center;justify-content:center}
.cc-img img{width:100%;height:100%;object-fit:cover;display:block}
.cc-name{padding:5px 7px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--ink,#e8edf4)}
.cc-grade{display:inline-block;margin:0 7px 5px;font-weight:700;color:var(--sun,#e8a33d)}
.cc-reason{display:block;padding:0 7px 6px;color:var(--muted,#8a93a3);font-size:11px}
.cull-dups{display:flex;flex-direction:column;gap:12px}
.dup-grp{background:var(--panel,#0f1622);border:1px solid var(--line,#243044);border-radius:10px;padding:10px;display:flex;gap:12px}
.dup-sim{display:flex;gap:8px;flex-wrap:wrap;flex:1}
.dup-sim .cull-item{width:84px}
.enh-top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}
.enh-status{font-size:12px;padding:4px 10px;border-radius:999px;border:1px solid var(--line,#243044)}
.enh-status.on{color:#5fd08a;border-color:#2c6b46;background:rgba(95,208,138,.08)}
.enh-status.off{color:#e0a24a;border-color:#6b532c;background:rgba(224,162,74,.08)}
.enh-body{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:14px}
@media(max-width:768px){.enh-body{grid-template-columns:1fr}}
.enh-pane .enh-label{font-size:12px;color:var(--muted,#8a93a3);margin-bottom:6px}
.enh-img{aspect-ratio:3/2;background:#0a0f17;border:1px solid var(--line,#243044);border-radius:10px;overflow:hidden;display:flex;align-items:center;justify-content:center}
.enh-img img{width:100%;height:100%;object-fit:contain;display:block}
.enh-ctrls{display:flex;align-items:center;gap:14px;flex-wrap:wrap;font-size:13px;color:var(--ink,#e8edf4)}
.enh-ctrls input[type=range]{vertical-align:middle}
"""
css_anchor = '''</style>
</head>'''
rep(css_anchor, css + '</style>\n</head>')

# ---------- E) IntersectionObserver secs ----------
rep("""  const secs=['home','map','series','gallery','log','about'];""",
    """  const secs=['home','map','series','gallery','cull','retouch','enhance','log','about'];""")

# ---------- F) DOMContentLoaded 绑定 ----------
rep("""  setupRetouchLab();""",
    """  setupRetouchLab();
  bindCull();
  bindEnhance();""")

# ---------- G) JS 函数块（插在 DOMContentLoaded 注册之前） ----------
js_anchor = """document.addEventListener('DOMContentLoaded',()=>{"""
js_block = """/* =========================================================
   智能选片（Culling）+ AI 美图（Enhance）
   两个模块原本藏在引擎层（culling.py / enhance.py），这里接成真实可用 UI。
   ========================================================= */
let _cullResult = null;
function _libBase(){ return (window.__libPort ? 'http://127.0.0.1:' + window.__libPort : ''); }
function cullThumb(url){ return url ? (url.indexOf('http')===0 ? url : _libBase()+url) : ''; }

function bindCull(){
  const run = document.getElementById('cullRun');
  if(!run || run.dataset.bound) return; run.dataset.bound='1';
  run.addEventListener('click', async ()=>{
    const scope = document.getElementById('cullScope').value;
    const opts = {
      dedup_level: document.getElementById('cullDedup').value,
      blur_threshold: parseFloat(document.getElementById('cullBlur').value)||80,
      min_megapixels: parseFloat(document.getElementById('cullMp').value)||0.5,
      top_n: parseInt(document.getElementById('cullTop').value)||10,
    };
    const msg = document.getElementById('cullMsg');
    msg.textContent = '正在本地分析 ' + (scope==='selection'?'选择集':'整个图库') + '，请稍候…';
    document.getElementById('cullExport').disabled = true;
    try{
      const d = await _apiPost('/api/cull', {scope:scope, options:opts});
      if(!d || !d.ok){ msg.textContent='选片失败：'+((d&&d.detail)||'未知错误'); return; }
      _cullResult = d; renderCull(d);
      msg.textContent = '完成：共 '+d.total+' 张，保留 '+d.kept+' 张，剔除 '+d.rejected+' 张'
        + (d.dup_groups.length? '（含 '+d.dup_groups.length+' 组重复）':'');
      document.getElementById('cullExport').disabled = false;
    }catch(e){ msg.textContent='选片出错：'+e; }
  });
  const ex = document.getElementById('cullExport');
  ex.addEventListener('click', ()=>{
    if(!_cullResult) return;
    if(window.__native && window.__native.call) window.__native.call('pickExport', {});
  });
  if(window.__native && window.__native.on) window.__native.on('exportPicked', function(p){
    if(!_cullResult || !p.path) return;
    const msg = document.getElementById('cullMsg');
    msg.textContent = '正在导出到 '+p.path+' …';
    _apiPost('/api/cull/export', {output_dir:p.path, top_n:_cullResult.top.length})
      .then(function(r){
        if(r && r.ok){ msg.textContent='已导出到：'+r.output_dir;
          if(window.__native && window.__native.call) window.__native.call('revealFile', {path:r.output_dir}); }
        else msg.textContent='导出失败：'+((r&&r.detail)||'未知');
      }).catch(function(e){ msg.textContent='导出出错：'+e; });
  });
}
function renderCull(d){
  const sum = document.getElementById('cullSummary'); if(sum) sum.hidden=false;
  const tn = document.getElementById('cullTopN'); if(tn) tn.textContent=d.top.length;
  const dn = document.getElementById('cullDupN'); if(dn) dn.textContent=d.dup_groups.length;
  const tg = document.getElementById('cullTopGrid');
  if(tg) tg.innerHTML = d.top.length ? d.top.map(cullCardHtml).join('') : '<p class="hint">没有入选照片。</p>';
  const dg = document.getElementById('cullDups');
  if(dg) dg.innerHTML = d.dup_groups.length ? d.dup_groups.map(function(g){
    const sim = g.similar.map(function(s){
      return cullCardHtml({name: s.name, thumb: s.thumb, reason: s.reason});
    }).join('');
    return '<div class="dup-grp"><div class="dup-keep">'
      + cullCardHtml({name: g.kept_name, thumb: g.kept_thumb, reason:'保留（最清晰）'})
      + '</div><div class="dup-sim">'+sim+'</div></div>';
  }).join('') : '<p class="hint">没有发现重复照片。</p>';
}
function cullCardHtml(t){
  const u = cullThumb(t.thumb);
  const reason = t.reason ? '<span class="cc-reason">'+escHtml(t.reason)+'</span>' : '';
  const grade = t.grade ? '<span class="cc-grade">'+escHtml(String(t.grade))+'</span>' : '';
  return '<div class="cull-item"><div class="cc-img">'+(u?'<img src="'+u+'" alt="" loading="lazy">':'')+'</div>'
    + '<div class="cc-name">'+escHtml(t.name||'')+'</div>'+grade+reason+'</div>';
}

/* ---- AI 美图 ---- */
let _enhPhoto = null, _enhBusy = false, _enhOut = '';
function bindEnhance(){
  const pick = document.getElementById('enhPick');
  if(!pick || pick.dataset.bound) return; pick.dataset.bound='1';
  pick.addEventListener('click', ()=>{
    if(state.selMode && state.sel && state.sel.length){
      const id = state.sel[0];
      const p = PHOTOS.filter(function(x){return x.id===id;})[0];
      if(p) return setEnhPhoto(p);
    }
    if(PHOTOS.length) return setEnhPhoto(PHOTOS[0]);
    document.getElementById('enhMsg').textContent='图库还没有照片。';
  });
  const sv = document.getElementById('enhStrength');
  const svv = document.getElementById('enhStrengthVal');
  if(sv && svv) sv.addEventListener('input', ()=>{ svv.textContent = parseFloat(sv.value).toFixed(2); });
  const run = document.getElementById('enhRun');
  run.addEventListener('click', async ()=>{
    if(!_enhPhoto){ document.getElementById('enhMsg').textContent='请先选择一张照片。'; return; }
    if(_enhBusy) return; _enhBusy = true;
    const msg = document.getElementById('enhMsg');
    const strength = parseFloat(document.getElementById('enhStrength').value)||1;
    msg.textContent='正在调用本地 AI 美图服务…';
    try{
      const d = await _apiPost('/api/enhance', {path:_enhPhoto.path, cat:_enhPhoto.cat, strength:strength});
      if(!d || !d.ok){
        if(d && d.skipped) msg.textContent='跳过：'+d.reason;
        else if(d && d.available===false) msg.textContent='AI 美图服务未启动：'+(d.reason||'');
        else msg.textContent='增强失败：'+((d&&d.detail)||(d&&d.reason)||'未知');
        _enhBusy=false; return;
      }
      _enhOut = d.output || '';
      const prev = d.preview_url ? (_libBase()+d.preview_url) : '';
      showEnhResult(prev);
      const open = document.getElementById('enhOpen');
      if(open) open.style.display = _enhOut ? '' : 'none';
      msg.textContent='增强完成（路由：'+(d.route||'')+'）'+(prev?'，已生成对比预览':'');
    }catch(e){ msg.textContent='增强出错：'+e; }
    _enhBusy=false;
  });
  const open = document.getElementById('enhOpen');
  if(open) open.addEventListener('click', ()=>{
    if(_enhOut && window.__native && window.__native.call) window.__native.call('revealFile', {path:_enhOut});
  });
  refreshEnhanceStatus();
}
function setEnhPhoto(p){
  _enhPhoto = {path: p.path, name: (p.t||p.rel||'照片'), cat: p.cat||'', thumb: p.img||''};
  document.getElementById('enhName').textContent = _enhPhoto.name;
  const pv = document.getElementById('enhBefore');
  if(pv){ const u = cullThumb(p.img); pv.innerHTML = u?'<img src="'+u+'" alt="">':''; }
  document.getElementById('enhAfter').innerHTML='';
  document.getElementById('enhOpen').style.display='none';
  document.getElementById('enhMsg').textContent='已选择，点击「增强」开始。';
}
function showEnhResult(prevUrl){
  const after = document.getElementById('enhAfter');
  if(after) after.innerHTML = prevUrl ? '<img src="'+prevUrl+'" alt="">' : '(无预览)';
}
function refreshEnhanceStatus(){
  _apiGet('/api/enhance/status').then(function(s){
    const el = document.getElementById('enhStatus'); if(!el) return;
    if(s && s.available){ el.textContent='AI 美图服务：在线'; el.className='enh-status on'; }
    else { el.textContent='AI 美图服务：离线（需启动 enhance_server :'+(s?s.port:'')+'）'; el.className='enh-status off'; }
  }).catch(function(){ const el=document.getElementById('enhStatus'); if(el){el.textContent='AI 美图服务：未知'; el.className='enh-status off';} });
}

""" + js_anchor
rep(js_anchor, js_block)

print('patch_index_cull_enhance: ALL DONE')

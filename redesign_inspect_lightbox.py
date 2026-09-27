# -*- coding: utf-8 -*-
"""1.9.22 · 检视面板默认展开 + 灯箱图片主导 + 弹出动效升级

三件事（用户最新需求）：
  1. 默认打开检视面板（#inspect 启动即展开，localStorage 记忆偏好）
  2. 图库点开照片：信息（标题/地区/EXIF）塞进检视面板，灯箱只展示图片
     —— 灯箱改为「图片主导」：去掉侧栏文字，保留悬浮操作条（fav/修图/编辑…）
  3. 优化灯箱弹出动效：背景淡入 + 卡片 scale-pop 弹起，尊重 prefers-reduced-motion

范式：所有改动落在根 index.html，逐处 rep 断言 count==1，随后 sync_client.py
把根页复制进 client/resources 并断言两份零差异。
"""
import io, sys

ROOT = 'index.html'
src = open(ROOT, encoding='utf-8').read()

reps = []

def rep(old, new, n=1):
    global src
    c = src.count(old)
    assert c == n, 'count==%d 期望 %d，实际 %d\n---OLD---\n%s' % (n, n, c, old[:200])
    src = src.replace(old, new, 1)

# ---------------------------------------------------------------- 1. 灯箱弹出动效升级
reps.append((
'  .lb{position:fixed;inset:0;z-index:100;background:rgba(18,16,12,.86);display:none;align-items:center;justify-content:center;padding:24px}\n'
'  .lb.show{display:flex;animation:lbIn var(--dur-3) var(--ease-out)}\n'
'  @keyframes lbIn{from{opacity:0}to{opacity:1}}',
'  .lb{position:fixed;inset:0;z-index:100;background:rgba(18,16,12,.86);display:none;align-items:center;justify-content:center;padding:24px}\n'
'  .lb.show{display:flex;animation:lbIn var(--dur-3) ease-out}\n'
'  .lb.show .lb-box{animation:lbPop var(--dur-3) cubic-bezier(.2,.8,.25,1) both}\n'
'  @keyframes lbIn{from{opacity:0}to{opacity:1}}\n'
'  @keyframes lbPop{from{opacity:0;transform:scale(.92) translateY(10px)}to{opacity:1;transform:none}}\n'
'  @media(prefers-reduced-motion:reduce){ .lb.show,.lb.show .lb-box{animation:none} }'
))

# 灯箱盒子：定位锚点 + 更大图区（去掉侧栏后可放宽）
reps.append((
'  .lb-box{background:var(--card);border-radius:18px;max-width:1080px;width:100%;max-height:92vh;overflow:auto;display:flex;flex-wrap:wrap}',
'  .lb-box{position:relative;background:var(--card);border-radius:18px;max-width:min(1280px,94vw);width:100%;max-height:94vh;overflow:hidden;display:flex;flex-wrap:wrap}'
))
reps.append((
'  .lb-img{flex:1;min-width:280px;min-height:300px;border-radius:18px 0 0 18px}',
'  .lb-img{flex:1;min-width:280px;min-height:300px;border-radius:18px}'
))

# 2. 灯箱「图片主导」：信息文字隐藏，操作条改悬浮胶囊
reps.append((
'  .lb-info{padding:26px;flex:1;min-width:280px}\n'
'  .lb-info h3{font-size:22px}\n'
'  .lb-info .meta{color:var(--muted);font-size:13px;margin-top:4px}\n'
'  .lb-info .exif{margin-top:18px;border-top:1px solid var(--line);padding-top:14px}\n'
'  .lb-info .exif .row{display:flex;justify-content:space-between;padding:7px 0;border-bottom:1px dashed var(--line);font-size:14px}\n'
'  .lb-info .exif .row b{color:var(--ink);font-weight:600}\n'
'  .lb-info .exif .row span{color:var(--muted);font-variant-numeric:tabular-nums}',
'  /* 1.9.22：灯箱转「图片主导」——元数据文字移入检视面板，灯箱仅保留图片 + 悬浮操作条 */\n'
'  .lb-info{position:absolute;left:50%;bottom:18px;transform:translateX(-50%);z-index:4;\n'
'    display:flex;gap:10px;flex-wrap:wrap;justify-content:center;max-width:92%;\n'
'    padding:10px 14px;background:rgba(0,0,0,.46);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);\n'
'    border:1px solid rgba(255,255,255,.16);border-radius:999px}\n'
'  .lb-info h3,.lb-info .meta,.lb-info .exif,.lb-info .lb-miss{display:none}\n'
'  .lb-info .lb-actions{margin-top:0;justify-content:center}'
))

# 3. openLightbox：停止把标题/地区/EXIF 文字塞进灯箱（改由检视面板承载），保留操作按钮可见性判定
reps.append((
"  document.getElementById('lbTitle').textContent=p.t;\n"
"  const lbMissEl=document.getElementById('lbMiss'); if(lbMissEl) lbMissEl.hidden=!p._missing;\n"
"  document.getElementById('lbMeta').textContent=`${regionName(p.r)} · ${p.y}${p.s?(' · '+p.s):''} · ${p.o==='landscape'?'横构图':'竖构图'}`;\n"
"  document.getElementById('lbExif').innerHTML=`\n"
"    <div class=\"row\"><b>机身</b><span>${p.cam}</span></div>\n"
"    <div class=\"row\"><b>镜头</b><span>${p.lens}</span></div>\n"
"    <div class=\"row\"><b>焦距</b><span>${p.focal? p.focal+' mm' : '—'}</span></div>\n"
"    <div class=\"row\"><b>光圈</b><span>${p.ap}</span></div>\n"
"    <div class=\"row\"><b>快门</b><span>${p.sh}</span></div>\n"
"    <div class=\"row\"><b>ISO</b><span>${p.iso? p.iso : '—'}</span></div>\n"
"    <div class=\"row\"><b>拍摄日期</b><span>${p.d}</span></div>\n"
"    <div class=\"row\"><b>标签</b><span>${p.tag}</span></div>`;\n"
"  const fav=document.getElementById('lbFav');",
"  // 1.9.22：灯箱改为图片主导——标题/地区/EXIF 等信息改用检视面板呈现，灯箱只渲染图片与悬浮操作条。\n"
"  const fav=document.getElementById('lbFav');"
))

# 4. 图库点击：信息塞进检视面板并展开，灯箱只展示图片
reps.append((
"      selectPhoto(_id);                                 // 检视面板（若已展开）同步该作品\n"
"      openLightbox(_id);",
"      selectPhoto(_id, true);                           // 信息塞进检视面板并展开\n"
"      openLightbox(_id);                                // 灯箱只展示图片"
))

# 5. setupStudio：检视面板默认展开 + 记忆偏好
reps.append((
"function setupStudio(){\n"
"  const it = document.getElementById('inspectToggle'); if(it) it.addEventListener('click',()=>toggleInspect());\n"
"  const ft = document.getElementById('filmToggle'); if(ft) ft.addEventListener('click',()=>toggleFilm());\n"
"  const ic = document.getElementById('inspectClose'); if(ic) ic.addEventListener('click',()=>toggleInspect(false));\n"
"  // 1.9.11：胶片条默认收起（不再默认占用底部 122px）；检视面板同样默认收起。\n"
"  document.body.classList.remove('film-open');\n"
"  const tf = document.getElementById('filmToggle'); if(tf) tf.setAttribute('aria-pressed','false');\n"
"}",
"function setupStudio(){\n"
"  const it = document.getElementById('inspectToggle'); if(it) it.addEventListener('click',()=>toggleInspect());\n"
"  const ft = document.getElementById('filmToggle'); if(ft) ft.addEventListener('click',()=>toggleFilm());\n"
"  const ic = document.getElementById('inspectClose'); if(ic) ic.addEventListener('click',()=>toggleInspect(false));\n"
"  // 1.9.11：胶片条默认收起（不再默认占用底部 122px）。\n"
"  document.body.classList.remove('film-open');\n"
"  const tf = document.getElementById('filmToggle'); if(tf) tf.setAttribute('aria-pressed','false');\n"
"  // 1.9.22：检视面板默认展开（需求：默认打开检视面板）；以 localStorage 记忆用户偏好，默认开。\n"
"  let _insp = true;\n"
"  try{ const _v = localStorage.getItem('wb_insp_open'); if(_v!==null) _insp = (_v==='1'); }catch(_e){}\n"
"  document.body.classList.toggle('insp-open', _insp);\n"
"  const _itg = document.getElementById('inspectToggle'); if(_itg) _itg.setAttribute('aria-pressed', _insp?'true':'false');\n"
"}"
))

# 6. toggleInspect：持久化偏好
reps.append((
"function toggleInspect(force){\n"
"  const on = force===undefined ? !document.body.classList.contains('insp-open') : force;\n"
"  document.body.classList.toggle('insp-open', on);\n"
"  const t = document.getElementById('inspectToggle'); if(t) t.setAttribute('aria-pressed', on?'true':'false');\n"
"}",
"function toggleInspect(force){\n"
"  const on = force===undefined ? !document.body.classList.contains('insp-open') : force;\n"
"  document.body.classList.toggle('insp-open', on);\n"
"  const t = document.getElementById('inspectToggle'); if(t) t.setAttribute('aria-pressed', on?'true':'false');\n"
"  try{ localStorage.setItem('wb_insp_open', on?'1':'0'); }catch(_e){}\n"
"}"
))

for old, new in reps:
    rep(old, new)

open(ROOT, 'w', encoding='utf-8').write(src)
print('patched root index.html, bytes=%d' % len(src))
print('reps applied =', len(reps))

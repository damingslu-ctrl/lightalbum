# -*- coding: utf-8 -*-
"""1.9.23 · 极致「纯图片」灯箱：操作胶囊整体移入检视面板

用户拍板：灯箱里那条悬浮操作胶囊也整个移走，灯箱只留图片与看图控件。

实现：
  1. CSS：`.lb-info` 由「悬浮胶囊」改为 `display:none`——灯箱 DOM 里的
     fav/vault/reveal/retouch/edit/frame 按钮保留，仅作「处理器宿主」
     （处理器都在灯箱按钮上注册，fmOpen 等读取 lbPhotoId）。
  2. `inspectShow`：按 openLightbox 同款可见性条件生成操作 chips
     （vault=libMode；reveal/retouch/edit=native+abs+__native.call；frame=native+abs）。
  3. 点击 chips：先同步 `lbPhotoId = p.id`，再程序化 `.click()` 对应隐藏按钮
     ——处理器单一事实源在灯箱按钮上，检视面板零逻辑重复。

范式：rep 断言 count==1 → sync_client.py 双页零差异。
"""
import sys

ROOT = 'index.html'
src = open(ROOT, encoding='utf-8').read()

def rep(old, new, n=1):
    global src
    c = src.count(old)
    assert c == n, 'count==%d 期望 %d，实际 %d\n---OLD---\n%s' % (n, n, c, old[:200])
    src = src.replace(old, new, 1)

# ---- 1. CSS：悬浮胶囊 → 整体隐藏（按钮留作处理器宿主） ----
rep(
'  /* 1.9.22：灯箱转「图片主导」——元数据文字移入检视面板，灯箱仅保留图片 + 悬浮操作条 */\n'
'  .lb-info{position:absolute;left:50%;bottom:18px;transform:translateX(-50%);z-index:4;\n'
'    display:flex;gap:10px;flex-wrap:wrap;justify-content:center;max-width:92%;\n'
'    padding:10px 14px;background:rgba(0,0,0,.46);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);\n'
'    border:1px solid rgba(255,255,255,.16);border-radius:999px}\n'
'  .lb-info h3,.lb-info .meta,.lb-info .exif,.lb-info .lb-miss{display:none}\n'
'  .lb-info .lb-actions{margin-top:0;justify-content:center}',
'  /* 1.9.23：极致「纯图片」灯箱——操作条整体移入检视面板；灯箱 DOM 保留按钮仅作处理器宿主（隐藏） */\n'
'  .lb-info{display:none}'
)

# ---- 2. inspectShow：操作卡加动态 chips ----
rep(
"    '<div class=\"insp-card\"><h4>操作</h4><div class=\"insp-chips\">'+\n"
"      '<button class=\"insp-chip\" data-act=\"fav\">'+(isFav?'取消收藏 ♥':'收藏 ♡')+'</button>'+\n"
"      '<button class=\"insp-chip\" data-act=\"map\">在地图查看</button>'+\n"
"      '<button class=\"insp-chip\" data-act=\"open\">打开大图</button>'+\n"
"    '</div></div>'+",
"    '<div class=\"insp-card\"><h4>操作</h4><div class=\"insp-chips\">'+\n"
"      '<button class=\"insp-chip\" data-act=\"fav\">'+(isFav?'取消收藏 ♥':'收藏 ♡')+'</button>'+\n"
"      '<button class=\"insp-chip\" data-act=\"map\">在地图查看</button>'+\n"
"      '<button class=\"insp-chip\" data-act=\"open\">打开大图</button>'+\n"
"      _inspActionChips(p) +\n"
"    '</div></div>'+"
)

# ---- 3. inspectShow：点击处理转发到灯箱处理器宿主 ----
rep(
"      if(a==='fav'){ toggleFav(p.id); _selPhoto = p.id; inspectShow(p); }\n"
"      else if(a==='map'){ const link = document.querySelector('#nav a[data-sec=\"map\"]'); if(link) link.click(); }\n"
"      else if(a==='open'){ openLightbox(p.id); }",
"      if(a==='fav'){ toggleFav(p.id); _selPhoto = p.id; inspectShow(p); }\n"
"      else if(a==='map'){ const link = document.querySelector('#nav a[data-sec=\"map\"]'); if(link) link.click(); }\n"
"      else if(a==='open'){ openLightbox(p.id); }\n"
"      else { _inspRunAction(p, a); }   // 1.9.23：灯箱操作整体移交检视面板"
)

# ---- 4. 新增两个辅助函数（插在 inspectShow 之前） ----
rep(
"function inspectShow(p){\n"
"  const body = document.getElementById('inspectBody'); if(!body) return;\n"
"  if(!p){ body.innerHTML = inspectSummary(); return; }",
"/* 1.9.23：灯箱操作条整体移入检视面板。\n"
"   灯箱 DOM 里的操作按钮保留作「处理器宿主」（.lb-info 已 display:none），\n"
"   这里按 openLightbox 同款可见性条件生成 chips；点击时同步 lbPhotoId 后\n"
"   程序化触发隐藏按钮，复用原有处理器（修图/编辑/相框/隐私库/资源管理器），零逻辑重复。 */\n"
"function _inspActionChips(p){\n"
"  const abs = p.path || (window.__nativeAbs && window.__nativeAbs[p.rel]) || '';\n"
"  const nat = !!(window.__native && window.__native.call);\n"
"  const isNative = p.src === 'native';\n"
"  let chips = '';\n"
"  if(state.libMode) chips += '<button class=\"insp-chip\" data-act=\"vault\">🔒 移入隐私库</button>';\n"
"  if(isNative && abs && nat){\n"
"    chips += '<button class=\"insp-chip\" data-act=\"reveal\">📂 在资源管理器中打开</button>';\n"
"    chips += '<button class=\"insp-chip\" data-act=\"retouch\">🖌 跳转修图</button>';\n"
"    chips += '<button class=\"insp-chip\" data-act=\"edit\">✏️ 内嵌编辑</button>';\n"
"  }\n"
"  if(isNative && abs) chips += '<button class=\"insp-chip\" data-act=\"frame\">🖼 相框水印</button>';\n"
"  return chips;\n"
"}\n"
"function _inspRunAction(p, act){\n"
"  const map = { vault:'lbVault', reveal:'lbReveal', retouch:'lbRetouch', edit:'lbEdit', frame:'lbFrame' };\n"
"  const btn = map[act] ? document.getElementById(map[act]) : null;\n"
"  if(!btn) return;\n"
"  lbPhotoId = p.id;            // 处理器全部读取 lbPhotoId，先同步再到按钮上触发\n"
"  btn.click();\n"
"}\n"
"function inspectShow(p){\n"
"  const body = document.getElementById('inspectBody'); if(!body) return;\n"
"  if(!p){ body.innerHTML = inspectSummary(); return; }"
)

open(ROOT, 'w', encoding='utf-8').write(src)
print('patched root index.html ok, reps=4')

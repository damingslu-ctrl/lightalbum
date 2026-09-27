# -*- coding: utf-8 -*-
# 1.9.39 修复 OCR review 两个 medium：
#   M1 签名过弱（同数量原地修正检测不到）-> photosFp() O(n) 轻量指纹，纳入
#      renderMapGuarded / renderStatsGuarded / #rlPick 三处
#   M2 aiDecide 死载荷字段 -> 两端注释锚定（index.html 发送处 + app.py 消费处）
import io, sys

ROOT = r"D:\Test\workbuddy\2026-09-18-09-00-13"

# ---------- index.html ----------
P1 = ROOT + r"\index.html"
with io.open(P1, "r", encoding="utf-8") as f:
    data = f.read()

repls = []

# M1a: 守卫块加 photosFp，签名替换
repls.append((
"function renderMapGuarded(){\n"
"  const m=document.getElementById('map');\n"
"  const sig=(state.mapLevel||0)+'|'+(state.region||'')+'|'+(state.prov||'')+'|'+((PHOTOS||[]).length)+'|'+(m?m.clientWidth:0);\n"
"  if(state._mapSig===sig) return;     // 输入未变且已渲染过 -> 跳过整张 SVG 世界重建\n"
"  state._mapSig=sig;\n"
"  renderMap();\n"
"}\n"
"function renderStatsGuarded(){\n"
"  const sig=((PHOTOS||[]).length)+'|'+((PHOTOS||[]).filter(p=>p.rating).length);\n"
"  if(state._statsSig===sig) return;   // 数据未变 -> 跳过图表重绘\n"
"  state._statsSig=sig;\n"
"  renderStats();\n"
"}",
"function photosFp(){   // OCR 1.9.39 M1：O(n) 轻量指纹，同数量原地修正（重扫/EXIF回填/分类/评分/改名）也能被察觉\n"
"  let fp=0; const a=PHOTOS||[];\n"
"  for(let i=0;i<a.length;i++){\n"
"    const p=a[i];\n"
"    fp=(fp*31 + (p.id|0) + (p.r?7:0) + ((p.lo||p.la)?13:0) + (p.cat?3:0) + (p.rating||0) + (p.t?1:0))|0;\n"
"  }\n"
"  return fp;\n"
"}\n"
"function renderMapGuarded(){\n"
"  const m=document.getElementById('map');\n"
"  const sig=(state.mapLevel||0)+'|'+(state.region||'')+'|'+(state.prov||'')+'|'+photosFp()+'|'+(m?m.clientWidth:0);\n"
"  if(state._mapSig===sig) return;     // 输入未变且已渲染过 -> 跳过整张 SVG 世界重建\n"
"  state._mapSig=sig;\n"
"  renderMap();\n"
"}\n"
"function renderStatsGuarded(){\n"
"  const sig=photosFp();\n"
"  if(state._statsSig===sig) return;   // 数据未变 -> 跳过图表重绘\n"
"  state._statsSig=sig;\n"
"  renderStats();\n"
"}",
"M1a 地图/图表签名改用 photosFp"))

# M1b: #rlPick 重建条件纳入指纹
repls.append((
"  const pickEl = document.getElementById('rlPick');\n"
"  if(pickEl && pickEl.options){\n"
"    if(pickEl.options.length !== PHOTOS.length){\n"
"      pickEl.innerHTML = PHOTOS.map(function(p){\n"
"        return '<option value=\"'+p.id+'\">'+escHtml((p.t||'未命名')+' · '+(p.d||''))+'</option>';\n"
"      }).join('');\n"
"    }",
"  const pickEl = document.getElementById('rlPick');\n"
"  if(pickEl && pickEl.options){\n"
"    if(pickEl.options.length !== PHOTOS.length || state._pickFp !== photosFp()){\n"
"      state._pickFp = photosFp();\n"
"      pickEl.innerHTML = PHOTOS.map(function(p){\n"
"        return '<option value=\"'+p.id+'\">'+escHtml((p.t||'未命名')+' · '+(p.d||''))+'</option>';\n"
"      }).join('');\n"
"    }",
"M1b rlPick 重建条件纳入指纹"))

# M2a: aiDecide 发送处注释锚定
repls.append((
"    if(window.__native.call) window.__native.call('aiEdit', { path: abs, goal: goal, preset: 'auto', aiDecide: true, config: aiConfig() });",
"    // aiDecide 字段为语义标记：原生 act_aiEdit 不消费它，「AI 全权」由 preset='auto' 表达（1.9.39 审查锚定）\n"
"    if(window.__native.call) window.__native.call('aiEdit', { path: abs, goal: goal, preset: 'auto', aiDecide: true, config: aiConfig() });",
"M2a aiDecide 页面侧锚定"))

for old, new, label in repls:
    c = data.count(old)
    if c != 1:
        print("FAIL [%s] count=%d (expected 1)" % (label, c)); sys.exit(1)
    data = data.replace(old, new, 1)
    print("OK   [%s]" % label)

with io.open(P1, "w", encoding="utf-8") as f:
    f.write(data)
print("index.html WROTE %d bytes" % len(data))

# ---------- app.py ----------
P2 = ROOT + r"\client\lightalbum\app.py"
with io.open(P2, "r", encoding="utf-8") as f:
    ap = f.read()

old = ("        path = (p or {}).get('path', '')\n"
       "        goal = (p or {}).get('goal', '') or ''\n"
       "        preset = (p or {}).get('preset', 'auto') or 'auto'\n")
new = ("        # aiDecide 字段由页面发送但此处不消费：「AI 全权」语义由 preset='auto' 表达（1.9.39 审查锚定）\n"
       "        path = (p or {}).get('path', '')\n"
       "        goal = (p or {}).get('goal', '') or ''\n"
       "        preset = (p or {}).get('preset', 'auto') or 'auto'\n")
c = ap.count(old)
if c != 1:
    print("FAIL [M2b app.py] count=%d" % c); sys.exit(1)
ap = ap.replace(old, new, 1)
print("OK   [M2b aiDecide 原生侧锚定]")
with io.open(P2, "w", encoding="utf-8") as f:
    f.write(ap)

# ---------- 复核 ----------
checks = {
    "photosFp 定义": data.count("function photosFp()"),
    "photosFp 用于地图签名": data.count("'|'+photosFp()+'|'+(m?m.clientWidth:0)"),
    "photosFp 用于图表签名": data.count("const sig=photosFp();"),
    "rlPick 指纹条件": data.count("state._pickFp !== photosFp()"),
    "aiDecide 页面锚定": data.count("1.9.39 审查锚定"),
}
for k, v in checks.items():
    print(("OK  " if v >= 1 else "FAIL ") + "%-30s %d" % (k, v))
print("app.py 锚定:", ap.count("1.9.39 审查锚定"))
sys.exit(0)

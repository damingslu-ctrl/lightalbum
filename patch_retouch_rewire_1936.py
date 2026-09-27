# -*- coding: utf-8 -*-
"""1.9.36 智能修图实验室重新接线（page-only JS/HTML）。
安全范式：读全量 -> 每处 old 断言 count==1 -> 替换 -> 写回。
改动清单：
  A) DOM：锁定卡片加「切换作品」下拉 #rlPick
  B) #rlAi 语义重写：忽略手动预设，让决策引擎按目标词全权修图（区别于 rlRun 的「用我选的预设」）
  C) 在 setupRetouchLab 内补 #rlEdit（重载画布）与 #rlPick 的内部接线（不再跨 IIFE）
  D) renderRetouchLab 同步 #rlPick 下拉（仅作品数变化时重建 option）
  E) 删除编辑器 IIFE 里重复的 #rlEdit 跨作用域绑定
"""
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

repls = []

# ---- A) DOM：锁定卡片加 #rlPick ----
repls.append((
"        <span class=\"rl-lock\" id=\"rlLock\" title=\"当前锁定作品：修图 / 编辑台只针对这一张；从画廊「🖌 跳转修图」可换图\">\n"
"          🔒 锁定：<span id=\"rlLockName\">未锁定</span>\n"
"        </span>\n"
"      </div>",
"        <span class=\"rl-lock\" id=\"rlLock\" title=\"当前锁定作品：修图 / 编辑台只针对这一张；从画廊「🖌 跳转修图」可换图\">\n"
"          🔒 锁定：<span id=\"rlLockName\">未锁定</span>\n"
"        </span>\n"
"        <select id=\"rlPick\" aria-label=\"切换锁定作品\" title=\"切换修图目标作品（实验室内部切换，无需离开）\"></select>\n"
"      </div>",
"#A DOM 加 #rlPick 下拉"))

# ---- B) #rlAi 语义重写 ----
repls.append((
"  // 「AI 一键修图」按钮原先没有绑定（死按钮）—— 直接复用同一个执行链路\n"
"  const runAi = document.getElementById('rlAi');\n"
"  if(runAi) runAi.addEventListener('click', function(){ run.click(); });",
"  // 「AI 一键修图」：忽略手动预设，让决策引擎按目标词全权选预设+算子\n"
"  // （区别于 rlRun 的「用我选的预设」—— 二者形成「手动预设 vs AI 接管」两条清晰路径）\n"
"  const runAi = document.getElementById('rlAi');\n"
"  if(runAi) runAi.addEventListener('click', function(){\n"
"    const p = rlCurPhoto();\n"
"    if(!p) return;\n"
"    if(!(window.__native && window.__native.call)){\n"
"      const m = document.getElementById('rlMsg'); if(m) m.textContent = '执行修图需要本地客户端（原生 Pillow 管线）；摄影建议已生成，见下方。';\n"
"      return;\n"
"    }\n"
"    const abs = p.path || (window.__nativeAbs && window.__nativeAbs[p.rel]);\n"
"    if(!abs){\n"
"      const m = document.getElementById('rlMsg'); if(m) m.textContent = '这幅作品没有已知的本地原图路径 —— 只有「客户端扫描导入 / 本地库」的作品可以直接修图。';\n"
"      return;\n"
"    }\n"
"    const goal = (document.getElementById('rlGoal')||{}).value.trim();\n"
"    const m = document.getElementById('rlMsg'); if(m) m.textContent = '正在由 AI 全权修图（目标：' + (goal || '自动') + '）… 已转入后台线程处理，界面不卡顿';\n"
"    progShow(true, '正在由 AI 全权修图…');\n"
"    if(window.__native.call) window.__native.call('aiEdit', { path: abs, goal: goal, preset: 'auto', aiDecide: true, config: aiConfig() });\n"
"  });",
"#B #rlAi 重写为 AI 全权接管"))

# ---- C) 在 setupRetouchLab 内补 #rlEdit 与 #rlPick 接线 ----
repls.append((
"    if(window.__native.call){\n"
"      window.__native.call('aiEdit', { path: abs, goal: goal, preset: preset, config: aiConfig() });\n"
"    }\n"
"  });",
"    if(window.__native.call){\n"
"      window.__native.call('aiEdit', { path: abs, goal: goal, preset: preset, config: aiConfig() });\n"
"    }\n"
"  });\n"
"  // 重载画布：重新就位当前锁定作品的编辑台（openEditor(false) 复用编辑器闭包，经 window.__openEditor 跨作用域）\n"
"  const editBtn = document.getElementById('rlEdit');\n"
"  if(editBtn) editBtn.addEventListener('click', function(){\n"
"    if(typeof window.__openEditor === 'function') window.__openEditor(false);\n"
"  });\n"
"  // 实验室内部「切换作品」下拉：选谁就锁定谁并重渲染 + 重新要建议，无需离开实验室\n"
"  const pickEl = document.getElementById('rlPick');\n"
"  if(pickEl) pickEl.addEventListener('change', function(){\n"
"    rlSelId = pickEl.value || null;\n"
"    renderRetouchLab();\n"
"    if(window.__rlRequestSuggest) window.__rlRequestSuggest();\n"
"  });",
"#C setupRetouchLab 内补 rlEdit/rlPick 接线"))

# ---- D) renderRetouchLab 同步 #rlPick ----
repls.append((
"  updateLockLabel();\n"
"  renderLabAll();\n"
"  if(window.__edSync) window.__edSync();   // 编辑台随锁定作品自动就位（客户端环境）\n"
"}",
"  updateLockLabel();\n"
"  // 同步「切换作品」下拉：仅在作品数变化时重建 option，避免每次 refresh 重建\n"
"  const pickEl = document.getElementById('rlPick');\n"
"  if(pickEl && pickEl.options){\n"
"    if(pickEl.options.length !== PHOTOS.length){\n"
"      pickEl.innerHTML = PHOTOS.map(function(p){\n"
"        return '<option value=\"'+p.id+'\">'+escHtml((p.t||'未命名')+' · '+(p.d||''))+'</option>';\n"
"      }).join('');\n"
"    }\n"
"    pickEl.value = rlSelId || (PHOTOS[0] && PHOTOS[0].id) || '';\n"
"  }\n"
"  renderLabAll();\n"
"  if(window.__edSync) window.__edSync();   // 编辑台随锁定作品自动就位（客户端环境）\n"
"}",
"#D renderRetouchLab 同步 #rlPick"))

# ---- E) 删除编辑器 IIFE 里重复的 #rlEdit 跨作用域绑定 ----
repls.append((
"    if((b=$('rlEdit'))) b.addEventListener('click', function(){ openEditor(false); });   // 重载画布",
"    // 「重载画布」(#rlEdit) 接线已并入 setupRetouchLab（经 window.__openEditor(false) 跨作用域），此处不再重复绑定",
"#E 删除编辑器 IIFE 重复绑定"))

# ---- 执行 + 断言 ----
for old, new, label in repls:
    c = data.count(old)
    if c != 1:
        print("FAIL [%s] count=%d (expected 1)" % (label, c))
        sys.exit(1)
    data = data.replace(old, new, 1)
    print("OK   [%s]" % label)

with io.open(PATH, "w", encoding="utf-8") as f:
    f.write(data)

# ---- 复核关键不变量 ----
checks = {
    "rlPick DOM": data.count('id="rlPick"'),
    "rlAi 死按钮残留(run.click 复制品)": data.count("runAi.addEventListener('click', function(){ run.click(); })"),
    "rlAi aiDecide 新语义": data.count("preset: 'auto', aiDecide: true"),
    "setupRetouchLab 内 rlEdit 接线": data.count("editBtn.addEventListener('click'"),
    "renderRetouchLab pickEl.options": data.count("if(pickEl && pickEl.options)"),
    "编辑器IIFE 旧绑定残留": data.count("b.addEventListener('click', function(){ openEditor(false); })"),
}
for k, v in checks.items():
    print(("OK  " if (k.startswith("rlAi 死按钮") and v==0) or (k=="编辑器IIFE 旧绑定残留" and v==0) or (k not in ("rlAi 死按钮","编辑器IIFE 旧绑定残留")) else "CHK ") + "%-50s %d" % (k, v))

print("WROTE %d bytes" % len(data))

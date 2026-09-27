# -*- coding: utf-8 -*-
import io

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-27.md"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

entry = r"""

## 智能修图实验室重新接线【1.9.36，已交付】
- 触发：用户「智能修图实验室的逻辑全部重新捋一遍，将每个功能按钮跳转，功能切换全部重新接线」。
- AskUserQuestion 两项决策：
  1. #rlAi（原「AI 一键修图」）此前是 #rlRun 的死复制品（点了等于再点一次 start）。重接后 = 让 AI 全权决定（忽略手动预设，aiDecide:true）。
  2. 实验室内部无法切换「锁定作品」（只由画廊/灯箱跳转设定）→ 加一个内部「切换作品」下拉 #rlPick，选谁锁定谁、无需离开实验室。
- 审计结论：原接线正确的保留（#rlRun→aiEdit 用我选的预设；#rlMatte→matting；#rlScore→score；#rlAiCfg→配置面板；#rlPreset→bindPresetDesc；#rlSuggest 芯片→套用预设；#lbRetouch/#lbEdit/#btnRetouch 路径；cfg 输入框→save+probeAi）。
- 3 处真问题已修（脚本 patch_retouch_rewire_1936.py，5 处 rep/计数断言，全绿）：
  A) 锁定卡片 DOM 加 #rlPick 下拉（aria-label 切换锁定作品）。
  B) #rlAi 语义重写：解析 rlCurPhoto()→判 native→取 abs 路径→读 #rlGoal→
     window.__native.call('aiEdit', { path, goal, preset:'auto', aiDecide:true, config: aiConfig() })；
     与 #rlRun 形成「手动预设 vs AI 接管」两条清晰路径。
  C) setupRetouchLab 内补 #rlEdit（重载画布，经 window.__openEditor(false) 跨作用域）与 #rlPick change 接线
     （rlSelId=pickEl.value → renderRetouchLab() → window.__rlRequestSuggest()）。
  D) renderRetouchLab 同步 #rlPick（仅作品数变化时重建 option，避免每次 refresh 重建；pickEl.value 跟随 rlSelId）。
  E) 删除编辑器 IIFE 里重复的 #rlEdit 跨作用域绑定（b=$('rlEdit')...），改由 setupRetouchLab 统一接线；
     注意残留 grep 命中是另一处 #edReopen 的绑定（$('rlEdit') 已 0 命中），属正常保留。
- 不变量复核：rlPick DOM=1；old run.click 副本=0；aiDecide 新语义=1；setupRetouchLab 内 rlEdit 接线=1；
  renderRetouchLab pickEl.options=1；$('rlEdit') 全文件 0 命中。
- 验证链全绿：node check.js SYNTAX_OK+EXTERNAL_REFS=none+SMOKE_DONE；node test_viewer.js 19/0；
  sync_client.py IDENTICAL（root==client/resources）。
- 构建：1.9.35→1.9.36；BUILD_OK → client/dist/LightAlbum-1.9.36-win64/LightAlbum/LightAlbum.exe
  （workpath w0927091829）。权威核验：包内 _internal/resources/index.html 含 id="rlPick"(1)、
  preset: 'auto', aiDecide: true(1)、旧 run.click 副本(0)，全部符合预期。
"""

assert data.endswith("\n") or data == "", "file should end with newline for clean append"
with io.open(PATH, "a", encoding="utf-8") as f:
    f.write(entry)
print("APPENDED 1.9.36 entry, total bytes=%d" % (len(data) + len(entry)))

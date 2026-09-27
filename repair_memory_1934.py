# -*- coding: utf-8 -*-
"""修复 2026-09-27.md：截断到 1.9.34 标记之前，重贴干净的 1.9.34 条目。
（之前的 -c 脚本因 backtick 触发 bash 命令替换而损坏了文件内容。）"""
import io

P = r"D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-27.md"
s = io.open(P, encoding="utf-8").read()

marker = "## UI 整理：降噪"
idx = s.find(marker)
assert idx != -1, "marker not found"
prefix = s[:idx].rstrip() + "\n"

entry = (
    "## UI 整理：降噪 + 层级分组 + 一致性【1.9.34，已交付】\n"
    "- 触发：承接 1.9.33 重新整理一下 UI 排版；AskUserQuestion 用户勾选全部四块（整体视觉降噪 / 画廊与卡片 / 侧栏与导航 / 响应式档位）→ 做一次有边界的整体整理。\n"
    "- 铁律遵守：纯 CSS/HTML 改动，不碰 JS 逻辑、不动 .drawer-panel/.pvl/.pm 变换、不动 .main-col 滚动容器；单主 script / 双主题 / reduced-motion 只压时长不写 transform:none 全部维持。\n"
    "- 改动（脚本 patch_ui_1934.py，13 处 rep/计数断言，全绿）：\n"
    "  1. 整体降噪（#162）：移除全部装饰性常驻循环动画（面积小但属视觉噪声，且属一直闪的残余观感来源）：.btn.primary 的 glowBreath、.brand .dot 的 dotBreath、.kpi 的 glowBreath、.sync-pill 的 glowBreath、地图 .home-mark .hm-halo 的 pulse、.pin .pulse 的 pulse；并删除因此变死的 keyframes glowBreath/dotBreath/ambientDrift/meshFloat/pulse（1.9.33 已停 body::before/::after 引用，本轮彻底清死代码）。\n"
    "  2. 保留功能性高亮：.nav a.active 的 navGlow（当前页指示）、.g-item.sel 的 selPulse（选片反馈）、.sync-pill .led 的 ledPulse（同步状态）、.rl-prog 的 rlIndet（加载条）—— 都是状态反馈不是装饰，一律保留。reduced-motion 清单收敛为仅 .g-item.sel。\n"
    "  3. 画廊卡片 hover 收敛（#163）：box-shadow 金辉 2rem/24% → 1rem/16%，抬升 -4px → -2px，降噪同时保留轻反馈。\n"
    "  4. 侧栏导航分组（#164）：插入两条 #sidebar .nav-sep 分隔线，把 8 项分为 [首页/足迹/图库] · [智能选片/修图/AI美图] · [日志/关于] 三组，信息架构更清晰。\n"
    "  5. 响应式顶栏（#165）：.topbar-inner 加 flex-wrap:wrap（其余断点 1080/900/768 是刻意设计，不动）。\n"
    "- 验证链全绿：node check.js SYNTAX_OK + EXTERNAL_REFS=none + SMOKE_DONE；node test_viewer.js 19/0；sync_client.py IDENTICAL（root==client/resources）；装饰性动画 token（glowBreath/dotBreath/ambientDrift/meshFloat/@keyframes pulse 等）全 0 命中。\n"
    "- 构建：1.9.33→1.9.34；BUILD_OK → client/dist/LightAlbum-1.9.34-win64/LightAlbum/LightAlbum.exe。\n"
    "- 说明：本轮是降噪/一致性整理，未改任何交互逻辑与布局断点语义；一直闪在 1.9.33 已根除（全屏动画层），本轮进一步清掉残余的小元素装饰脉冲，观感更安静。\n"
)

io.open(P, "w", encoding="utf-8").write(prefix + entry)
print("REPAIRED len=%d marker_count=%d" % (len(prefix + entry), (prefix + entry).count(marker)))

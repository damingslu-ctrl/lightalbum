# -*- coding: utf-8 -*-
"""1.9.34 UI 整理：降噪 + 层级分组 + 一致性（page-only CSS/HTML）。
安全范式：读全量 -> 每处 old 断言 count==1 -> 替换 -> 写回 -> 复核。
"""
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"

with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

repls = []

# ---- #162 整体视觉降噪：移除装饰性常驻动画（保留功能性高亮） ----
# 1) 主按钮呼吸辉光 + 品牌点呼吸 + 两个 keyframes（仅装饰）
repls.append((
"  /* 主行动按钮：呼吸辉光（循环，reduced-motion 关停） */\n"
"  .btn.primary{animation:glowBreath 3.4s ease-in-out infinite}\n"
"  @keyframes glowBreath{\n"
"    0%,100%{box-shadow:0 0 0 color-mix(in srgb,var(--gold) 0%,transparent)}\n"
"    50%{box-shadow:0 0 18px color-mix(in srgb,var(--gold) 35%,transparent)}}\n"
"  /* 品牌点：环境呼吸 */\n"
"  .brand .dot{animation:dotBreath 3.6s ease-in-out infinite}\n"
"  @keyframes dotBreath{\n"
"    0%,100%{box-shadow:0 0 0 0 color-mix(in srgb,var(--gold) 0%,transparent)}\n"
"    50%{box-shadow:0 0 12px 2px color-mix(in srgb,var(--gold) 45%,transparent)}}\n"
"  /* 选中卡脉冲光环（MCP 选择桥的可见反馈） */",
"  /* 选中卡脉冲光环（MCP 选择桥的可见反馈） */",
"#162a 移除 btn.primary/brand .dot 装饰呼吸辉光+keyframes"))

# 2) 已死的 ambientDrift keyframes（1.9.33 已停 body::after 引用）
repls.append((
"  body::after{content:'';position:fixed;inset:0;pointer-events:none;z-index:0;\n"
"    background:radial-gradient(46% 34% at 72% 8%,color-mix(in srgb,var(--gold) 6%,transparent),transparent 72%);\n"
"}\n"
"  @keyframes ambientDrift{\n"
"    from{opacity:.45;transform:translate3d(0,0,0)}\n"
"    to{opacity:1;transform:translate3d(-2.5%,1.5%,0)}}",
"  body::after{content:'';position:fixed;inset:0;pointer-events:none;z-index:0;\n"
"    background:radial-gradient(46% 34% at 72% 8%,color-mix(in srgb,var(--gold) 6%,transparent),transparent 72%);\n"
"}",
"#162b 移除死 ambientDrift keyframes"))

# 3) 已死的 meshFloat keyframes（1.9.33 已停 body::before 引用）
repls.append((
"  filter:blur(48px) saturate(135%);\n"
"}\n"
"@keyframes meshFloat{\n"
"  0%{transform:translate3d(0,0,0) scale(1)}\n"
"  50%{transform:translate3d(3%,-2.5%,0) scale(1.08)}\n"
"  100%{transform:translate3d(-2.5%,3%,0) scale(1.04)}}\n"
"body.dark::before{",
"  filter:blur(48px) saturate(135%);\n"
"}\n"
"body.dark::before{",
"#162c 移除死 meshFloat keyframes"))

# 4) KPI / 同步药丸装饰辉光（保留 .nav a.active 功能性 navGlow）
repls.append((
".kpi{animation:glowBreath 5s ease-in-out infinite}\n"
".nav a.active{animation:navGlow 3.8s ease-in-out infinite}\n"
".sync-pill{animation:glowBreath 4.4s ease-in-out infinite}",
".nav a.active{animation:navGlow 3.8s ease-in-out infinite}",
"#162d 移除 kpi/sync-pill 装饰辉光，保留 navGlow"))

# 5) reduced-motion 清单同步（仅留仍在循环的 .g-item.sel）
repls.append((
"  @media (prefers-reduced-motion: reduce){\n"
"    .btn.primary,.brand .dot,.g-item.sel,body::after{animation:none}\n"
"  }",
"  @media (prefers-reduced-motion: reduce){\n"
"    .g-item.sel{animation:none}\n"
"  }",
"#162e reduced-motion 清单收敛"))

# 6) 地图定位点 hm-halo 脉冲（装饰）
repls.append((
"  .home-mark .hm-halo{fill:#f2c04c;opacity:.32;transform-box:fill-box;transform-origin:center;animation:pulse 2.8s ease-out infinite}",
"  .home-mark .hm-halo{fill:#f2c04c;opacity:.32;transform-box:fill-box;transform-origin:center}",
"#162f 地图 hm-halo 去脉冲"))

# 7) 地图 pin.pulse 脉冲（装饰）
repls.append((
"  .pin .pulse{fill:var(--gold);transform-origin:center;transform-box:fill-box;animation:pulse 2.6s ease-out infinite}",
"  .pin .pulse{fill:var(--gold);transform-origin:center;transform-box:fill-box}",
"#162g 地图 pin.pulse 去脉冲"))

# 8) 已死 pulse keyframes（仅被上面两处引用，现已无引用）
repls.append((
"  @keyframes pulse{0%{transform:scale(1);opacity:.5}70%{opacity:0}100%{transform:scale(3.4);opacity:0}}\n",
"",
"#162h 移除死 pulse keyframes"))

# ---- #163 画廊卡片 hover 收敛 ----
repls.append((
"  .g-item:hover{box-shadow:var(--shadow-lg),0 0 2rem color-mix(in srgb,var(--gold) 24%,transparent);border-color:color-mix(in srgb,var(--gold) 38%,var(--line));transform:translateY(-4px)}",
"  .g-item:hover{box-shadow:var(--shadow-lg),0 0 1rem color-mix(in srgb,var(--gold) 16%,transparent);border-color:color-mix(in srgb,var(--gold) 30%,var(--line));transform:translateY(-2px)}",
"#163 画廊卡片 hover 收敛"))

# ---- #164 侧栏导航分组 ----
# CSS：导航分隔线
repls.append((
"  #sidebar .nav a svg{width:18px;height:18px;flex:0 0 auto;opacity:.9}",
"  #sidebar .nav a svg{width:18px;height:18px;flex:0 0 auto;opacity:.9}\n"
"  #sidebar .nav-sep{height:1px;margin:6px 10px;background:var(--line);opacity:.7}",
"#164a 导航分隔线 CSS"))
# 在「智能选片」前插入分隔（分组：[首页/足迹/图库] · [选片/修图/AI美图] · [日志/关于]）
repls.append((
"    <a href=\"#cull\" data-sec=\"cull\">",
"    <span class=\"nav-sep\"></span>\n"
"    <a href=\"#cull\" data-sec=\"cull\">",
"#164b 选片分组前分隔"))
repls.append((
"    <a href=\"#log\" data-sec=\"log\">",
"    <span class=\"nav-sep\"></span>\n"
"    <a href=\"#log\" data-sec=\"log\">",
"#164c 日志分组前分隔"))

# ---- #165 响应式顶栏 ----
repls.append((
"  .topbar-inner{max-width:1180px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;gap:16px}",
"  .topbar-inner{max-width:1180px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;gap:16px;flex-wrap:wrap}",
"#165 顶栏 flex-wrap"))

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

# ---- 复核关键残留应为 0 ----
for token in ["glowBreath", "dotBreath", "ambientDrift", "meshFloat", "@keyframes pulse",
              ".btn.primary{animation", ".brand .dot{animation", ".kpi{animation", ".sync-pill{animation",
              "animation:pulse 2.8s", "animation:pulse 2.6s"]:
    n = data.count(token)
    print(("CLEAN" if n == 0 else "LEAK ") + " %-26s %d" % (token, n))

print("WROTE %d bytes" % len(data))

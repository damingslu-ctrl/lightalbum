#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""1.9.25 视觉升级：飞书 vibecoding 资源复刻（零外链、内联）。
复用既有 glowBreath/dotBreath/selPulse/ambientDrift 体系，在其上更激进地叠层：
  1) body::before 流动网格渐变（mesh-gradient，GPU 合成 transform 漂移）
  2) KPI / 激活导航 / 同步药丸 呼吸辉光（navGlow 保留各自静态底影）
  3) 内联 SVG 图标悬浮金辉 + 激活态发光
  4) 卡片 / 系列卡悬浮金辉环 + 微缩放（.drawer/.pvl/.pm 不触碰）
  5) .reveal 子元素错峰入场（riseIn backwards 填充，不破坏 hover transform）
约束：双主题、reduced-motion 仅停新增循环装饰、单主 script、零外链。
范式：二进制读 → rep(anchor,count==1) → 写回； anchor 在第一个 </style> 前。
"""
import io, sys

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"

BLOCK = r"""
/* ========== 1.9.25 · 飞书 vibecoding 资源复刻（零外链·内联） ==========
   灵感：libraries.dev/image（动态辉光）/ colorflow mesh-gradient /
   details.so（错峰转场）/ HoloCard（玻璃虹彩）。全部双主题、
   reduced-motion 仅停循环装饰，不写 transform:none 破坏布局。 */
/* --- 流动网格渐变背景（mesh-gradient，GPU 合成 transform 漂移） --- */
body::before{
  content:'';position:fixed;inset:-25%;z-index:-1;pointer-events:none;
  background:
    radial-gradient(38% 36% at 18% 22%, color-mix(in srgb,var(--gold) 22%,transparent), transparent 60%),
    radial-gradient(40% 38% at 82% 18%, color-mix(in srgb,var(--blue) 18%,transparent), transparent 60%),
    radial-gradient(44% 42% at 75% 85%, color-mix(in srgb,var(--sun) 16%,transparent), transparent 62%),
    radial-gradient(40% 40% at 22% 82%, color-mix(in srgb,var(--indigo) 16%,transparent), transparent 60%);
  filter:blur(48px) saturate(135%);
  animation:meshFloat 28s ease-in-out infinite alternate;
  will-change:transform;
}
@keyframes meshFloat{
  0%{transform:translate3d(0,0,0) scale(1)}
  50%{transform:translate3d(3%,-2.5%,0) scale(1.08)}
  100%{transform:translate3d(-2.5%,3%,0) scale(1.04)}}
body.dark::before{
  background:
    radial-gradient(40% 38% at 16% 20%, color-mix(in srgb,var(--gold) 20%,transparent), transparent 60%),
    radial-gradient(42% 40% at 84% 18%, color-mix(in srgb,var(--blue) 26%,transparent), transparent 60%),
    radial-gradient(46% 44% at 74% 86%, color-mix(in srgb,var(--sun) 18%,transparent), transparent 62%),
    radial-gradient(42% 42% at 24% 82%, color-mix(in srgb,var(--indigo) 24%,transparent), transparent 60%);
}
/* --- 强化呼吸辉光：KPI / 激活导航 / 同步药丸（保留各自静态底影） --- */
@keyframes navGlow{
  0%,100%{box-shadow:var(--shadow),0 0 0 color-mix(in srgb,var(--gold) 0%,transparent)}
  50%{box-shadow:var(--shadow),0 0 16px color-mix(in srgb,var(--gold) 40%,transparent)}}
.kpi{animation:glowBreath 5s ease-in-out infinite}
.nav a.active{animation:navGlow 3.8s ease-in-out infinite}
.sync-pill{animation:glowBreath 4.4s ease-in-out infinite}
/* --- 图标精修：线描图标悬浮金辉 + 激活态发光（零外链内联 SVG） --- */
.nav a svg{transition:transform var(--dur-1) var(--ease-out),filter var(--dur-1) var(--ease-std)}
.nav a:hover svg{transform:translateY(-1px);filter:drop-shadow(0 0 6px color-mix(in srgb,var(--gold) 60%,transparent))}
.nav a.active svg{color:var(--gold);filter:drop-shadow(0 0 8px color-mix(in srgb,var(--gold) 78%,transparent))}
.top-toggle svg{transition:transform var(--dur-1) var(--ease-out),filter var(--dur-1)}
.top-toggle:hover svg{filter:drop-shadow(0 0 7px color-mix(in srgb,var(--gold) 55%,transparent))}
/* --- 卡片悬浮强化：金辉环 + 轻微抬升/缩放（.drawer/.pvl/.pm 不触碰） --- */
.card:hover{box-shadow:var(--shadow-hover),0 0 0 1px color-mix(in srgb,var(--gold) 35%,transparent),0 12px 30px color-mix(in srgb,var(--gold) 14%,transparent)}
.series-card:hover{transform:translateY(-4px) scale(1.02);box-shadow:var(--shadow-lg),0 0 22px color-mix(in srgb,var(--gold) 28%,transparent)}
.btn:hover{box-shadow:0 6px 20px rgba(32,32,30,.18),0 0 14px color-mix(in srgb,var(--gold) 30%,transparent)}
.btn.primary:hover{box-shadow:0 8px 26px rgba(32,32,30,.26),0 0 22px color-mix(in srgb,var(--gold) 45%,transparent)}
/* --- 错峰入场转场（details.so 风格：子元素递增延迟淡起，backwards 不破坏 hover） --- */
@keyframes riseIn{from{opacity:0;transform:translateY(18px)}to{opacity:1;transform:none}}
.reveal.in .series-grid > *,
.reveal.in .kpis > *,
.reveal.in .stat-grid > *{animation:riseIn var(--dur-3) var(--ease-out) backwards}
.reveal.in .gallery .g-item{animation:riseIn var(--dur-3) var(--ease-out) backwards}
.reveal.in .gallery .g-item:nth-child(1){animation-delay:0ms}
.reveal.in .gallery .g-item:nth-child(2){animation-delay:45ms}
.reveal.in .gallery .g-item:nth-child(3){animation-delay:90ms}
.reveal.in .gallery .g-item:nth-child(4){animation-delay:135ms}
.reveal.in .gallery .g-item:nth-child(5){animation-delay:180ms}
.reveal.in .gallery .g-item:nth-child(6){animation-delay:225ms}
.reveal.in .gallery .g-item:nth-child(7){animation-delay:270ms}
.reveal.in .gallery .g-item:nth-child(8){animation-delay:315ms}
.reveal.in .gallery .g-item:nth-child(n+9){animation-delay:360ms}
/* reduced-motion：停掉本轮新增的循环/入场装饰（不破坏布局） */
@media (prefers-reduced-motion: reduce){
  body::before,.kpi,.nav a.active,.sync-pill,
  .reveal.in .series-grid > *,.reveal.in .kpis > *,.reveal.in .stat-grid > *,
  .reveal.in .gallery .g-item{animation:none}
}
"""

def rep(data: str, old: str, new: str):
    c = data.count(old)
    assert c == 1, f"anchor count={c} (expected 1)"
    return data.replace(old, new, 1)

with io.open(SRC, "r", encoding="utf-8") as f:
    html = f.read()

anchor = "\n</style>\n</head>"
new_html = rep(html, anchor, "\n" + BLOCK + anchor)

with io.open(SRC, "w", encoding="utf-8") as f:
    f.write(new_html)

# 自检：关键新类/关键帧是否落盘，且无重复 anchor
assert new_html.count("meshFloat") == 1
assert new_html.count("navGlow") == 1
assert new_html.count("riseIn") == 1
assert new_html.count("@keyframes meshFloat") == 1
assert new_html.count("body::before{") == 1
print("UI_UPGRADE_1925_OK bytes=%d" % len(new_html.encode("utf-8")))

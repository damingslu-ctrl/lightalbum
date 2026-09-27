# -*- coding: utf-8 -*-
"""1.5.8 玻璃拟态改造：深色=墨黑+祖母绿+磨砂卡片（参考稿），浅色=暖调玻璃。
全部替换带 count 断言；块插入点 = 文档第一个真实 </style>（head 主样式块，
fabric 字符串里的 ']]></style>' 位于其后，不会误命中）。"""
import sys

P = 'index.html'
NL = '\r\n'
s = open(P, 'rb').read().decode('utf-8')
orig_len = len(s)


def rep(old, new, count=1):
    global s
    c = s.count(old)
    assert c == count, f"ABORT: count={c} expected={count} for: {old[:90]!r}"
    s = s.replace(old, new)


# ── R1: :root 增加玻璃 token（浅色=暖调玻璃）──────────────────────────
rep('    --shadow:0 6px 24px rgba(40,36,30,.08);' + NL + '    --r:16px;',
    '    --shadow:0 6px 24px rgba(40,36,30,.08);' + NL +
    '    --glass:rgba(255,255,255,.72); --glass-line:#e7e3dc;  /* 玻璃拟态卡片 */' + NL +
    '    --glass-blur:blur(16px) saturate(150%);' + NL +
    '    --r:16px;')

# ── R2: 浅色 body 底 → 柔和暖色光晕（给玻璃卡片做背景层次）────────────
rep('    background:var(--bg); color:var(--ink); line-height:1.6;',
    '    background:' + NL +
    '      radial-gradient(1100px 700px at 88% -10%,rgba(193,154,75,.09),transparent 60%),' + NL +
    '      radial-gradient(900px 620px at -8% 28%,rgba(63,96,152,.05),transparent 55%),' + NL +
    '      var(--bg);' + NL +
    '    color:var(--ink); line-height:1.6;')

# ── R3: .card → 磨砂玻璃（含不支持 backdrop-filter 的兜底）─────────────
rep('  .card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow)}',
    '  .card{background:var(--glass);border:1px solid var(--glass-line);border-radius:var(--r);box-shadow:var(--shadow);' + NL +
    '    backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur)}' + NL +
    '  @supports not (backdrop-filter:blur(1px)){.card{background:var(--card)}}')

# ── R4: 卡片 hover 抬升（reveal 作用域内需更高特异度）──────────────────
rep('  .card:hover{box-shadow:0 10px 36px rgba(40,36,30,.12)}',
    '  .card:hover{box-shadow:0 12px 40px rgba(40,36,30,.14)}' + NL +
    '  .reveal.in .card:hover{transform:translateY(-2px)}')

# ── R5: KPI 卡 → 玻璃 + 右上角光晕 + 大数字（参考稿 KPI 观感）──────────
rep('  .kpi{padding:16px;border-radius:14px;background:var(--card);border:1px solid var(--line);text-align:center}' + NL +
    '  .kpi .v{font-size:26px;font-weight:700;color:var(--gold);font-variant-numeric:tabular-nums}',
    '  .kpi{position:relative;overflow:hidden;padding:18px 20px;border-radius:16px;background:var(--glass);border:1px solid var(--glass-line);' + NL +
    '    backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);text-align:center}' + NL +
    '  .kpi::after{content:"";position:absolute;inset:0;border-radius:inherit;pointer-events:none;' + NL +
    '    background:radial-gradient(170px 100px at 86% -10%,rgba(193,154,75,.16),transparent 70%)}' + NL +
    '  .kpi .v{font-size:30px;font-weight:700;color:var(--gold);font-variant-numeric:tabular-nums;letter-spacing:.5px}')

# ── R6: 深色 token → 玻璃拟态仪表盘（--gold 重映射为祖母绿 = 全站点缀变色）──
rep('  body.dark{--bg:#15140f;--card:#211e19;--ink:#f1eee8;--muted:#a59e94;--line:#36322b;--shadow:0 6px 24px rgba(0,0,0,.5)}',
    '  /* —— 深色 = 玻璃拟态仪表盘：墨黑底 + 祖母绿点缀 + 磨砂卡片（参考稿）—— */' + NL +
    '  body.dark{--bg:#0b0e0c;--card:#181d1a;--ink:#f2f5f3;--muted:#9fa8a3;--line:rgba(255,255,255,.09);' + NL +
    '    --gold:#3ddc97;' + NL +
    '    --glass:rgba(255,255,255,.055);--glass-line:rgba(255,255,255,.1);--glass-blur:blur(18px) saturate(140%);' + NL +
    '    --shadow:0 10px 34px rgba(0,0,0,.5);' + NL +
    '    background:' + NL +
    '      radial-gradient(1000px 640px at 14% -6%,rgba(61,220,151,.13),transparent 62%),' + NL +
    '      radial-gradient(860px 560px at 92% 2%,rgba(45,212,191,.09),transparent 58%),' + NL +
    '      radial-gradient(760px 520px at 50% 112%,rgba(61,220,151,.07),transparent 62%),' + NL +
    '      linear-gradient(180deg,#0c0f0d,#080a09);' + NL +
    '    background-attachment:fixed}')

# ── R7: 深色顶栏 + 导航（active = 祖母绿玻璃胶囊 + 辉光）────────────────
rep('  body.dark header.topbar{background:rgba(21,20,15,.85)}' + NL +
    '  body.dark .nav a:hover,body.dark .nav a.active{background:var(--card)}',
    '  body.dark header.topbar{background:rgba(8,10,9,.66);border-bottom-color:rgba(255,255,255,.08)}' + NL +
    '  body.dark .nav a:hover{background:rgba(255,255,255,.06)}' + NL +
    '  body.dark .nav a.active{background:rgba(61,220,151,.13);color:#eafff5;' + NL +
    '    box-shadow:inset 0 0 0 1px rgba(61,220,151,.35),0 4px 20px rgba(61,220,151,.15)}')

# ── R8: 深色移动端底栏 → 玻璃 ─────────────────────────────────────────
rep('  body.dark .mobile-nav{background:rgba(21,20,15,.94)}',
    '  body.dark .mobile-nav{background:rgba(8,10,9,.92);backdrop-filter:blur(14px)}')

# ── R9: 深色 data-bar 玻璃化（.kpi 移出该行，走 --glass）───────────────
rep('  body.dark .data-bar,body.dark .kpi,body.dark .chip{background:var(--card)}',
    '  body.dark .data-bar,body.dark .chip{background:var(--card)}' + NL +
    '  body.dark .data-bar{background:var(--glass);border-color:var(--glass-line);backdrop-filter:var(--glass-blur)}')

# ── R10: 深色图库卡 → 玻璃 ───────────────────────────────────────────
rep('  body.dark .g-item{background:#2a2620}',
    '  body.dark .g-item{background:rgba(255,255,255,.05);border-color:rgba(255,255,255,.1)}')

# ── R11: 深色主按钮 → 祖母绿实心 + hover 辉光（参考稿「新增任务」钮）────
rep('  body.dark .btn.primary{background:var(--ink);color:var(--bg);border-color:var(--ink)}',
    '  body.dark .btn.primary{background:var(--gold);color:#04241a;border-color:var(--gold)}' + NL +
    '  body.dark .btn.primary:hover{box-shadow:0 4px 22px rgba(61,220,151,.3)}')

# ── R12: 深色 chip.on → 祖母绿玻璃胶囊 ────────────────────────────────
rep('  body.dark .chip.on{background:var(--ink);color:var(--bg);border-color:var(--ink)}',
    '  body.dark .chip.on{background:rgba(61,220,151,.15);color:#d9fff0;border-color:rgba(61,220,151,.4)}')

# ── R13/R14/R15: 深色地图面板暖棕选中态 → 祖母绿系 ─────────────────────
rep('  body.dark .region-list .rl-item.sel{background:#3b2f20;border-color:var(--sun)}',
    '  body.dark .region-list .rl-item.sel{background:rgba(61,220,151,.12);border-color:rgba(61,220,151,.55)}')
rep('  body.dark .rl-head .clr{background:#3b2f20;border-color:#5c4a2a;color:#e9d59a}' + NL +
    '  body.dark .rl-head .clr:hover{background:#4a3b26}',
    '  body.dark .rl-head .clr{background:rgba(61,220,151,.1);border-color:rgba(61,220,151,.35);color:#a9ecc9}' + NL +
    '  body.dark .rl-head .clr:hover{background:rgba(61,220,151,.2)}')
rep('  body.dark .map-hint{background:#2a2417;border-color:#4a3f22;color:#d9c690}' + NL +
    '  body.dark .map-hint b{color:#e9d59a}',
    '  body.dark .map-hint{background:rgba(61,220,151,.08);border-color:rgba(61,220,151,.25);color:#c9f2dd}' + NL +
    '  body.dark .map-hint b{color:#eafff5}')

# ── R16: 深色 KPI 数值 → 祖母绿（原 amber）────────────────────────────
rep('  body.dark .kpi .v{color:var(--amber)}',
    '  body.dark .kpi .v{color:var(--gold)}')

# ── R17: 深色 theme-color meta 对齐新底色 ─────────────────────────────
rep('<meta name="theme-color" content="#15140f" media="(prefers-color-scheme: dark)">',
    '<meta name="theme-color" content="#0a0c0b" media="(prefers-color-scheme: dark)">')

# ── R18: 减弱动效 → 同时清零 delay（避免 both+delay 先藏后跳）──────────
rep('      animation-duration:.01ms!important;animation-iteration-count:1!important;',
    '      animation-duration:.01ms!important;animation-iteration-count:1!important;animation-delay:0ms!important;')

# ── R19: 新增块 → 级联进场（挂在既有 .reveal 体系上）+ 深色 hover 辉光 ──
BLOCK = (
    '  /* ---------- 玻璃拟态级联进场（挂在既有 .reveal 分区体系上）----------' + NL +
    '     卡片随所在 section 的 .in 上浮，transition-delay 按 nth-child 级联，' + NL +
    '     形成参考稿的 stagger 观感；减弱动效由 .reveal 兜底块 + 全局时长压缩接管。 */' + NL +
    '  .reveal .card,.reveal .kpi{opacity:0;transform:translateY(16px)}' + NL +
    '  .reveal.in .card,.reveal.in .kpi{opacity:1;transform:none;' + NL +
    '    transition:opacity .36s var(--ease-out),transform .36s var(--ease-out)}' + NL +
    '  .reveal.in .card:nth-child(2),.reveal.in .kpi:nth-child(2){transition-delay:60ms}' + NL +
    '  .reveal.in .card:nth-child(3),.reveal.in .kpi:nth-child(3){transition-delay:120ms}' + NL +
    '  .reveal.in .card:nth-child(4),.reveal.in .kpi:nth-child(4){transition-delay:180ms}' + NL +
    '  .reveal.in .card:nth-child(5),.reveal.in .kpi:nth-child(5){transition-delay:240ms}' + NL +
    '  .reveal.in .card:nth-child(6),.reveal.in .kpi:nth-child(6){transition-delay:300ms}' + NL +
    '  .reveal.in .card:nth-child(n+7),.reveal.in .kpi:nth-child(n+7){transition-delay:360ms}' + NL +
    '  body.dark .card:hover{border-color:rgba(61,220,151,.3)}' + NL +
    '  body.dark .reveal.in .card:hover{box-shadow:0 14px 44px rgba(0,0,0,.55),0 0 28px rgba(61,220,151,.08)}' + NL +
    '  /* 打印 / 移动端兜底 */' + NL +
    '  @media print{.reveal .card,.reveal .kpi{opacity:1!important;transform:none!important}}' + NL +
    '  @media(max-width:768px){' + NL +
    '    :root{--glass-blur:blur(10px) saturate(140%)}' + NL +
    '    body.dark{--glass-blur:blur(12px) saturate(140%)}}' + NL +
    '  body.dark ::-webkit-scrollbar{width:10px;height:10px}' + NL +
    '  body.dark ::-webkit-scrollbar-thumb{background:rgba(255,255,255,.14);border-radius:8px}' + NL +
    '  body.dark ::-webkit-scrollbar-track{background:transparent}' + NL)
i = s.find('</style>')
assert i > 0 and s.count('</style>', 0, i) == 0, 'first </style> not head one'
s = s[:i] + BLOCK + s[i:]

open(P, 'wb').write(s.encode('utf-8'))
print(f"OK {orig_len} -> {len(s)} bytes, {orig_len and 'all assertions passed'}")

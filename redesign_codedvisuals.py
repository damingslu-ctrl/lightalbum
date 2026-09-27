# -*- coding: utf-8 -*-
r"""1.9.19 · CodedVisuals 视觉语言升级（暖炭 + 陶土橘）。

参考 https://codedvisuals.com/visuals 的实测 token（OKLCH→hex 换算）：
  bg #262624 / card #2c2c2b / sidebar #1f1e1d / fg #f1f1ef / muted #b7b5a9
  primary(签名色) #d97757 / border #3e3e38 / destructive #ef4444 / radius 1rem
  Inter 字体、玻璃 sticky 头部、卡片 hover 0 0 2rem primary 光晕。

范式（J 节）：二进制安全读 → rep(old,new) 逐条断言唯一 → 写回。
顺序：先「具体块替换」（用原始文本做锚），再「全局色号替换」（扫尾）。
"""
import io

FILES = [r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html',
         r'D:\Test\workbuddy\2026-09-18-09-00-13\client\resources\index.html']

# (old, new, 说明)
REPS = [
    # ---- 亮色 token：暖纸白 + 陶土橘 ----
    ('--bg:#f7f6f3; --card:#ffffff; --ink:#20201e;',
     '--bg:#faf9f5; --card:#ffffff; --ink:#1c1c1a;',
     'light bg/ink -> CV 暖纸白'),
    ('--line:#e7e3dc; --gold:#c19a4b;',
     '--line:#e7e3dc; --gold:#d97757;',
     'light 签名色 -> 陶土橘'),
    ('--sun:#c76b3a;', '--sun:#ef4444;', 'danger -> CV destructive 红'),
    ('font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;',
     'font-family:Inter,"Inter Variable",-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;',
     '字体栈头部加 Inter（本地存在则生效，零外链）'),
    ('radial-gradient(1100px 700px at 88% -10%,rgba(193,154,75,.09),transparent 60%)',
     'radial-gradient(1100px 700px at 88% -10%,rgba(217,119,87,.08),transparent 60%)',
     'light 顶部环境光 -> 陶土橘'),
    ('background:rgba(247,246,243,.85)',
     'background:rgba(250,249,245,.85)',
     'light topbar 底色跟随 bg'),
    # ---- 暗色 token：暖炭系（CV .dark 实测值） ----
    ('''body.dark{--bg:#0b0e0c;--card:#181d1a;--ink:#f2f5f3;--muted:#9fa8a3;--line:rgba(255,255,255,.09);
    --gold:#3ddc97;
    --glass:rgba(255,255,255,.055);--glass-line:rgba(255,255,255,.1);--glass-blur:blur(18px) saturate(140%);
    --shadow:0 10px 34px rgba(0,0,0,.5);
    --shadow-sm:0 1px 2px rgba(0,0,0,.4);
    --shadow-lg:0 18px 50px rgba(0,0,0,.6);
    --shadow-hover:0 12px 32px rgba(0,0,0,.5),0 0 22px rgba(61,220,151,.06);''',
     '''body.dark{--bg:#262624;--card:#2c2c2b;--ink:#f1f1ef;--muted:#b7b5a9;--line:#3e3e38;
    --gold:#d97757;
    --glass:rgba(255,255,255,.045);--glass-line:rgba(255,255,255,.09);--glass-blur:blur(18px) saturate(140%);
    --shadow:0 10px 34px rgba(0,0,0,.5);
    --shadow-sm:0 1px 2px rgba(0,0,0,.4);
    --shadow-lg:0 18px 50px rgba(0,0,0,.6);
    --shadow-hover:0 12px 32px rgba(0,0,0,.5),0 0 22px rgba(217,119,87,.10);''',
     'dark token -> CV 暖炭 + 陶土橘'),
    ('''      radial-gradient(1000px 640px at 14% -6%,rgba(61,220,151,.13),transparent 62%),
      radial-gradient(860px 560px at 92% 2%,rgba(45,212,191,.09),transparent 58%),
      radial-gradient(760px 520px at 50% 112%,rgba(61,220,151,.07),transparent 62%),
      linear-gradient(180deg,#0c0f0d,#080a09);''',
     '''      radial-gradient(1000px 640px at 14% -6%,rgba(217,119,87,.10),transparent 62%),
      radial-gradient(860px 560px at 92% 2%,rgba(217,164,65,.06),transparent 58%),
      radial-gradient(760px 520px at 50% 112%,rgba(217,119,87,.06),transparent 62%),
      linear-gradient(180deg,#282826,#1e1e1c);''',
     'dark 环境光 -> 陶土橘+琥珀，底 -> 暖炭'),
    ('body.dark header.topbar{background:rgba(8,10,9,.66);border-bottom-color:rgba(255,255,255,.08)}',
     'body.dark header.topbar{background:rgba(32,32,30,.78);border-bottom-color:rgba(255,255,255,.10)}',
     'dark topbar -> CV bg/80 玻璃'),
    ('body.dark .mobile-nav{background:rgba(8,10,9,.92);backdrop-filter:blur(14px)}',
     'body.dark .mobile-nav{background:rgba(31,30,29,.94);backdrop-filter:blur(14px)}',
     'dark mobile-nav -> sidebar 色'),
    ('body.dark .g-item{background:rgba(255,255,255,.05);border-color:rgba(255,255,255,.1)}',
     'body.dark .g-item{background:var(--card);border-color:var(--line)}',
     'dark 卡片 -> CV 实底 card'),
    # ---- CV 签名交互：卡片光晕 / 选中光晕 ----
    ('.g-item:hover{box-shadow:var(--shadow-lg);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}',
     '.g-item:hover{box-shadow:var(--shadow-lg),0 0 2rem color-mix(in srgb,var(--gold) 18%,transparent);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}',
     'hover 加 CV 0 0 2rem primary 光晕'),
    ('.g-item{break-inside:avoid;margin-bottom:26px;border-radius:14px;',
     '.g-item{break-inside:avoid;margin-bottom:26px;border-radius:16px;',
     '卡片圆角 -> CV 1rem'),
    ('.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px}',
     '.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px;box-shadow:0 0 1.6rem color-mix(in srgb,var(--gold) 32%,transparent)}',
     '选中态（MCP 选择桥）加光晕'),
    # ---- --sun 职责拆分：选中类改 gold，danger 留 sun ----
    ('.pin.sel .core{fill:var(--sun)}', '.pin.sel .core{fill:var(--gold)}', '地图 pin 选中'),
    ('.region-list .rl-item.sel{border-color:var(--sun);box-shadow:0 4px 16px rgba(199,107,58,.18);background:#fff7f1}',
     '.region-list .rl-item.sel{border-color:var(--gold);box-shadow:0 4px 16px rgba(217,119,87,.18);background:#fdf1ea}',
     '区域列表选中'),
    ('font-size:12.5px;color:var(--sun);', 'font-size:12.5px;color:var(--gold);', 'gsec-more 链接'),
    ('.series-card.active{border-color:var(--sun);box-shadow:0 10px 30px rgba(199,107,58,.22)}',
     '.series-card.active{border-color:var(--gold);box-shadow:0 10px 30px rgba(217,119,87,.22)}',
     '系列卡激活'),
    ('.insight b{color:var(--sun)}', '.insight b{color:var(--gold)}', '洞察强调'),
    ('color:var(--sun);cursor:pointer;border:1px solid var(--sun);',
     'color:var(--gold);cursor:pointer;border:1px solid var(--gold);', 'rl 清除按钮'),
    ('background:linear-gradient(90deg,var(--gold),var(--sun));',
     'background:linear-gradient(90deg,var(--gold),var(--amber));',
     '进度条渐变 -> 陶土橘→琥珀'),
    ('style="border-color:var(--sun);color:var(--sun)"',
     'style="border-color:var(--gold);color:var(--gold)"', 'JS 内联确认按钮'),
    # ---- 科技渐变：翠绿/青 -> 陶土橘/琥珀 ----
    ('--accent2:#37e0ff;                                          /* 科技青：与翠绿组成双色渐变 */',
     '--accent2:#d9a441;                                          /* 琥珀：与陶土橘组成双色渐变 */',
     'aux 强调色'),
    ('--tech:linear-gradient(135deg,#3ddc97 0%,#37e0ff 100%);',
     '--tech:linear-gradient(135deg,#d97757 0%,#d9a441 100%);', '双色渐变'),
]

# 全局扫尾（在具体替换之后跑；old 剩余数 >0 即全换）
GLOBAL_SWAPS = [
    ('61,220,151', '217,119,87', '祖母绿 rgba -> 陶土橘 rgba'),
    ('#3ddc97', '#d97757', '祖母绿 hex -> 陶土橘 hex'),
    ('#eafff5', '#ffece3', '绿白文字 -> 暖白'),
    ('#c9f2dd', '#f2cdbd', '浅绿文字 -> 浅陶'),
    ('#d9fff0', '#ffe4d6', '浅绿文字 -> 浅陶'),
    ('#a9ecc9', '#f0bfa8', '绿文字 -> 陶土浅调'),
    ('#7df0c0', '#f2b39b', '绿 chip 文字 -> 陶土浅调'),
    ('#d8ffe9', '#ffe3d4', '绿白文字 -> 暖白'),
]

# 新增 CSS（插在第一个 </style> 前，head 块内、fabric 之前）
NEW_CSS = '''  /* ========== 1.9.19 · CodedVisuals 视觉语言：暖炭 + 陶土橘 ========== */
  /* 聚焦环（CV ring 语义）：键盘导航可见性， 不打扰鼠标点击 */
  a:focus-visible,button:focus-visible,[tabindex]:focus-visible,
  .btn:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible{
    outline:2px solid color-mix(in srgb,var(--gold) 55%,transparent);outline-offset:2px}
'''


def rep(s, old, new, note):
    n = s.count(old)
    assert n == 1, f'[FAIL] rep 非唯一({n}): {note}\n  old={old[:80]!r}'
    print(f'  ok(1) {note}')
    return s.replace(old, new)


def gswap(s, old, new, note, atleast=1):
    n = s.count(old)
    assert n >= atleast, f'[FAIL] 全局替换未命中({n}): {note}'
    print(f'  ok(x{n}) {note}')
    return s.replace(old, new)


for fp in FILES:
    print('==', fp)
    with io.open(fp, encoding='utf-8') as f:
        s = f.read()
    for old, new, note in REPS:
        s = rep(s, old, new, note)
    for old, new, note in GLOBAL_SWAPS:
        s = gswap(s, old, new, note)
    # 新 CSS：插在第一个 </style> 前
    first = s.find('</style>')
    assert first > 0 and s.find('<script id="fabric-lib">') > first, '插入点校验失败'
    s = s[:first] + NEW_CSS + s[first:]
    with io.open(fp, 'w', encoding='utf-8') as f:
        f.write(s)
    print('  patched, size =', len(s))

print('DONE')

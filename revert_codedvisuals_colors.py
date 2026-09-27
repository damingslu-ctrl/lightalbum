# -*- coding: utf-8 -*-
r"""撤销 redesign_codedvisuals 的全部「颜色」改动（用户要求：只留动效）。

与 redesign_codedvisuals.py 逐条互逆；先具体替换（新文本做锚），再全局扫尾。
保留（动效/可用性，非配色）：:focus-visible 聚焦环。
"""
import io

FILES = [r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html',
         r'D:\Test\workbuddy\2026-09-18-09-00-13\client\resources\index.html']

REPS = [
    # ---- 亮色 token 还原 ----
    ('--bg:#faf9f5; --card:#ffffff; --ink:#1c1c1a;',
     '--bg:#f7f6f3; --card:#ffffff; --ink:#20201e;', 'light bg/ink 还原'),
    ('--line:#e7e3dc; --gold:#d97757;',
     '--line:#e7e3dc; --gold:#c19a4b;', 'light 签名色还原'),
    ('--sun:#ef4444;', '--sun:#c76b3a;', 'danger 色还原'),
    ('font-family:Inter,"Inter Variable",-apple-system,',
     'font-family:-apple-system,', '字体栈还原'),
    ('radial-gradient(1100px 700px at 88% -10%,rgba(217,119,87,.08),transparent 60%)',
     'radial-gradient(1100px 700px at 88% -10%,rgba(193,154,75,.09),transparent 60%)',
     'light 环境光还原'),
    ('background:rgba(250,249,245,.85)',
     'background:rgba(247,246,243,.85)', 'light topbar 还原'),
    # ---- 暗色 token 还原 ----
    ('''body.dark{--bg:#262624;--card:#2c2c2b;--ink:#f1f1ef;--muted:#b7b5a9;--line:#3e3e38;
    --gold:#d97757;
    --glass:rgba(255,255,255,.045);--glass-line:rgba(255,255,255,.09);--glass-blur:blur(18px) saturate(140%);
    --shadow:0 10px 34px rgba(0,0,0,.5);
    --shadow-sm:0 1px 2px rgba(0,0,0,.4);
    --shadow-lg:0 18px 50px rgba(0,0,0,.6);
    --shadow-hover:0 12px 32px rgba(0,0,0,.5),0 0 22px rgba(217,119,87,.10);''',
     '''body.dark{--bg:#0b0e0c;--card:#181d1a;--ink:#f2f5f3;--muted:#9fa8a3;--line:rgba(255,255,255,.09);
    --gold:#3ddc97;
    --glass:rgba(255,255,255,.055);--glass-line:rgba(255,255,255,.1);--glass-blur:blur(18px) saturate(140%);
    --shadow:0 10px 34px rgba(0,0,0,.5);
    --shadow-sm:0 1px 2px rgba(0,0,0,.4);
    --shadow-lg:0 18px 50px rgba(0,0,0,.6);
    --shadow-hover:0 12px 32px rgba(0,0,0,.5),0 0 22px rgba(61,220,151,.06);''',
     'dark token 还原'),
    ('''      radial-gradient(1000px 640px at 14% -6%,rgba(217,119,87,.10),transparent 62%),
      radial-gradient(860px 560px at 92% 2%,rgba(217,164,65,.06),transparent 58%),
      radial-gradient(760px 520px at 50% 112%,rgba(217,119,87,.06),transparent 62%),
      linear-gradient(180deg,#282826,#1e1e1c);''',
     '''      radial-gradient(1000px 640px at 14% -6%,rgba(61,220,151,.13),transparent 62%),
      radial-gradient(860px 560px at 92% 2%,rgba(45,212,191,.09),transparent 58%),
      radial-gradient(760px 520px at 50% 112%,rgba(61,220,151,.07),transparent 62%),
      linear-gradient(180deg,#0c0f0d,#080a09);''',
     'dark 环境光还原'),
    ('body.dark header.topbar{background:rgba(32,32,30,.78);border-bottom-color:rgba(255,255,255,.10)}',
     'body.dark header.topbar{background:rgba(8,10,9,.66);border-bottom-color:rgba(255,255,255,.08)}',
     'dark topbar 还原'),
    ('body.dark .mobile-nav{background:rgba(31,30,29,.94);backdrop-filter:blur(14px)}',
     'body.dark .mobile-nav{background:rgba(8,10,9,.92);backdrop-filter:blur(14px)}',
     'dark mobile-nav 还原'),
    ('body.dark .g-item{background:var(--card);border-color:var(--line)}',
     'body.dark .g-item{background:rgba(255,255,255,.05);border-color:rgba(255,255,255,.1)}',
     'dark 卡片还原'),
    # ---- 交互还原（动效部分随后单独加） ----
    ('.g-item:hover{box-shadow:var(--shadow-lg),0 0 2rem color-mix(in srgb,var(--gold) 18%,transparent);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}',
     '.g-item:hover{box-shadow:var(--shadow-lg);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}',
     'hover 还原（光晕后补）'),
    ('.g-item{break-inside:avoid;margin-bottom:26px;border-radius:16px;',
     '.g-item{break-inside:avoid;margin-bottom:26px;border-radius:14px;',
     '卡片圆角还原'),
    ('.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px;box-shadow:0 0 1.6rem color-mix(in srgb,var(--gold) 32%,transparent)}',
     '.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px}',
     '选中态还原（光晕后补）'),
    ('.pin.sel .core{fill:var(--gold)}', '.pin.sel .core{fill:var(--sun)}', 'pin 选中还原'),
    ('.region-list .rl-item.sel{border-color:var(--gold);box-shadow:0 4px 16px rgba(217,119,87,.18);background:#fdf1ea}',
     '.region-list .rl-item.sel{border-color:var(--sun);box-shadow:0 4px 16px rgba(199,107,58,.18);background:#fff7f1}',
     '区域选中还原'),
    ('font-size:12.5px;color:var(--gold);', 'font-size:12.5px;color:var(--sun);', 'gsec-more 还原'),
    ('.series-card.active{border-color:var(--gold);box-shadow:0 10px 30px rgba(217,119,87,.22)}',
     '.series-card.active{border-color:var(--sun);box-shadow:0 10px 30px rgba(199,107,58,.22)}',
     '系列卡还原'),
    ('.insight b{color:var(--gold)}', '.insight b{color:var(--sun)}', '洞察还原'),
    ('color:var(--gold);cursor:pointer;border:1px solid var(--gold);',
     'color:var(--sun);cursor:pointer;border:1px solid var(--sun);', 'rl 清除还原'),
    ('background:linear-gradient(90deg,var(--gold),var(--amber));',
     'background:linear-gradient(90deg,var(--gold),var(--sun));',
     '进度条渐变还原'),
    ('style="border-color:var(--gold);color:var(--gold)"',
     'style="border-color:var(--sun);color:var(--sun)"', 'JS 确认钮还原'),
    ('--accent2:#d9a441;                                          /* 琥珀：与陶土橘组成双色渐变 */',
     '--accent2:#37e0ff;                                          /* 科技青：与翠绿组成双色渐变 */',
     'aux 还原'),
    ('--tech:linear-gradient(135deg,#d97757 0%,#d9a441 100%);',
     '--tech:linear-gradient(135deg,#3ddc97 0%,#37e0ff 100%);', '双色渐变还原'),
    # ---- favicon / 图表 ----
    ('%23d97757', '%23c19a4b', 'favicon 还原（特例：按计数断言）'),
    ('.adv.tip i{background:#d97757}', '.adv.tip i{background:#c19a4b}', 'tip 图标还原'),
    ("d.color||'#d97757'", "d.color||'#c19a4b'", '柱状图默认色还原'),
    ("color:'#d97757'", "color:'#c19a4b'", '光圈柱色还原'),
    ("color:'#d9a441'", "color:'#c76b3a'", '焦距柱色还原'),
    ("['#d97757','#3f6098','#4b3f7a','#d9a441','#5a8f6b','#b06a8a']",
     "['#c19a4b','#3f6098','#4b3f7a','#c76b3a','#5a8f6b','#b06a8a']", '调色板还原'),
]

# 特例计数（favicon x2 用 count 断言替代唯一性）
SPECIAL_COUNT = {'%23d97757': 2}

GLOBAL_SWAPS = [
    ('217,119,87', '61,220,151', 36, '陶土橘 rgba -> 祖母绿 rgba'),
    ('#d97757', '#3ddc97', 6, '陶土橘 hex -> 祖母绿 hex'),
    ('#ffece3', '#eafff5', 2, '暖白 -> 绿白'),
    ('#f2cdbd', '#c9f2dd', 1, '浅陶 -> 浅绿'),
    ('#ffe4d6', '#d9fff0', 1, '浅陶 -> 浅绿'),
    ('#f0bfa8', '#a9ecc9', 1, '陶浅调 -> 绿'),
    ('#f2b39b', '#7df0c0', 1, '陶浅调 -> 绿'),
    ('#ffe3d4', '#d8ffe9', 1, '暖白 -> 绿白'),
]

POST_CHECKS = [
    ('#d97757', 0), ('217,119,87', 0), ('217,164,65', 0), ('ef4444', 0),
    ('#d9a441', 1),   # 仅存 --amber 定义
    ('#ffece3', 0), ('#f2b39b', 0),
]


def rep(s, old, new, note):
    n = s.count(old)
    want = SPECIAL_COUNT.get(old, 1)
    assert n == want, f'[FAIL] {note}: count={n} expect={want}'
    print(f'  ok(x{n}) {note}')
    return s.replace(old, new)


for fp in FILES:
    print('==', fp)
    s = io.open(fp, encoding='utf-8').read()
    for old, new, note in REPS:
        s = rep(s, old, new, note)
    for old, new, want, note in GLOBAL_SWAPS:
        n = s.count(old)
        assert n == want, f'[FAIL] 全局 {note}: count={n} expect={want}'
        s = s.replace(old, new)
        print(f'  ok(x{n}) {note}')
    for tok, want in POST_CHECKS:
        n = s.count(tok)
        assert n == want, f'[FAIL] 后置校验 {tok}: {n} != {want}'
    print('  post-checks OK')
    io.open(fp, 'w', encoding='utf-8').write(s)

print('REVERT DONE')

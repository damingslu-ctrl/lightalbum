# -*- coding: utf-8 -*-
r"""1.9.20 · CodedVisuals 动效语言落地（用户明确：不克制）。

CV 动效词汇 → LightAlbum 映射：
  hover 光晕+抬升   → .g-item（画廊卡）
  选中脉冲光环      → .g-item.sel（MCP 选择桥可见反馈）
  呼吸辉光          → .btn.primary（主行动）/ .brand .dot（品牌点）
  环境光漂移        → body::after 固定覆层（极低透明度，GPU 合成）
  胶片条 hover 辉光 → .ft（延续其自带 translateY(-3px) 范式）
  按压反馈          → .btn:active

铁律遵守：循环动画全部 @media(prefers-reduced-motion: reduce) 下 animation:none
（= 既有 line961/1194 范式；「只压时长勿 transform:none」针对的是交互动效，
循环装饰动画按既有先例直接停）；不碰 .drawer-panel/.pvl/.pm/#lbImgEl 的 transform；
时长走既有 token（--dur-*）或显式秒（filmIn 先例）。
"""
import io

FILES = [r'D:\Test\workbuddy\2026-09-18-09-00-13\index.html',
         r'D:\Test\workbuddy\2026-09-18-09-00-13\client\resources\index.html']

REPS = [
    # 1) 画廊卡：transition 增加 transform 通道
    ('.g-item{transition:box-shadow var(--dur-2) var(--ease-std),border-color var(--dur-2) var(--ease-std)}',
     '.g-item{transition:box-shadow var(--dur-2) var(--ease-std),border-color var(--dur-2) var(--ease-std),transform var(--dur-2) var(--ease-out)}',
     'g-item transition + transform'),
    # 2) hover：光晕 + 抬升（CV 0 0 2rem primary/30 签名）
    ('.g-item:hover{box-shadow:var(--shadow-lg);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}',
     '.g-item:hover{box-shadow:var(--shadow-lg),0 0 2rem color-mix(in srgb,var(--gold) 24%,transparent);border-color:color-mix(in srgb,var(--gold) 38%,var(--line));transform:translateY(-4px)}',
     'g-item hover 光晕+抬升'),
    # 3) 选中卡：脉冲光环（keyframes 在新块中定义）
    ('.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px}',
     '.g-item.sel{outline:3px solid var(--gold);outline-offset:-3px;animation:selPulse 2.4s ease-in-out infinite}',
     '选中卡脉冲光环'),
]

NEW_CSS = '''  /* ========== 1.9.20 · CodedVisuals 动效语言 ========== */
  /* 主行动按钮：呼吸辉光（循环，reduced-motion 关停） */
  .btn.primary{animation:glowBreath 3.4s ease-in-out infinite}
  @keyframes glowBreath{
    0%,100%{box-shadow:0 0 0 color-mix(in srgb,var(--gold) 0%,transparent)}
    50%{box-shadow:0 0 18px color-mix(in srgb,var(--gold) 35%,transparent)}}
  /* 品牌点：环境呼吸 */
  .brand .dot{animation:dotBreath 3.6s ease-in-out infinite}
  @keyframes dotBreath{
    0%,100%{box-shadow:0 0 0 0 color-mix(in srgb,var(--gold) 0%,transparent)}
    50%{box-shadow:0 0 12px 2px color-mix(in srgb,var(--gold) 45%,transparent)}}
  /* 选中卡脉冲光环（MCP 选择桥的可见反馈） */
  @keyframes selPulse{
    0%,100%{box-shadow:0 0 1.2rem color-mix(in srgb,var(--gold) 22%,transparent)}
    50%{box-shadow:0 0 2rem color-mix(in srgb,var(--gold) 45%,transparent)}}
  /* 胶片条 hover：辉光（.ft 自带 translateY(-3px) 抬升，此处补光） */
  .ft:hover{box-shadow:0 0 14px color-mix(in srgb,var(--gold) 30%,transparent)}
  /* 按压反馈：.btn 已有 transition:var(--dur-1)，transform 自动纳入 */
  .btn:active{transform:translateY(1px)}
  /* 环境光漂移：固定覆层，极低透明度，pointer-events:none，GPU 合成友好 */
  body::after{content:'';position:fixed;inset:0;pointer-events:none;z-index:0;
    background:radial-gradient(46% 34% at 72% 8%,color-mix(in srgb,var(--gold) 6%,transparent),transparent 72%);
    animation:ambientDrift 10s ease-in-out infinite alternate}
  @keyframes ambientDrift{
    from{opacity:.45;transform:translate3d(0,0,0)}
    to{opacity:1;transform:translate3d(-2.5%,1.5%,0)}}
  /* reduced-motion：循环装饰动画直接停（既有 line961/1194 先例），
     交互类（hover 抬升/按压）保留但时长已被全局压缩机制覆盖 */
  @media (prefers-reduced-motion: reduce){
    .btn.primary,.brand .dot,.g-item.sel,body::after{animation:none}
  }
'''


def rep(s, old, new, note):
    n = s.count(old)
    assert n == 1, f'[FAIL] rep 非唯一({n}): {note}'
    print(f'  ok(1) {note}')
    return s.replace(old, new)


for fp in FILES:
    print('==', fp)
    s = io.open(fp, encoding='utf-8').read()
    for old, new, note in REPS:
        s = rep(s, old, new, note)
    first = s.find('</style>')
    assert first > 0 and s.find('<script id="fabric-lib">') > first, '插入点校验失败'
    s = s[:first] + NEW_CSS + s[first:]
    io.open(fp, 'w', encoding='utf-8').write(s)
    print('  patched, size =', len(s))

print('MOTION DONE')

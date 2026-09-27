# -*- coding: utf-8 -*-
"""1.7.3 全局质感打磨：
- 阴影层级 token 化（sm/lg/hover，浅/深两套）
- 按钮 hover 上浮 + 主按钮投影；卡片 hover 边框/阴影统一走 token
- sec-kicker 金点 → 科技渐变短线；画廊卡 hover 质感
- ::selection、浅色滚动条、输入控件 hover、同步 LED 呼吸
不新增颜色（全部复用 gold/emerald/cyan/ink），零外链，CRLF，count==1 断言。
"""
import re

P = 'index.html'
raw = open(P, 'rb').read()
data = raw.decode('utf-8').replace('\r\n', '\n')

def rep(old, new, label):
    global data
    n = data.count(old)
    assert n == 1, 'REP FAIL [%s]: count=%d' % (label, n)
    data = data.replace(old, new)
    print('ok  %s' % label)

# ---------- 1. 浅色阴影层级 token ----------
rep('''    --shadow:0 6px 24px rgba(40,36,30,.08);''',
'''    --shadow:0 6px 24px rgba(40,36,30,.08);
    --shadow-sm:0 1px 2px rgba(40,36,30,.05),0 2px 8px rgba(40,36,30,.05);
    --shadow-lg:0 18px 50px rgba(40,36,30,.16);
    --shadow-hover:0 12px 32px rgba(40,36,30,.12);''',
    'light shadow tokens')

# ---------- 2. 深色阴影层级 token ----------
rep('''    --shadow:0 10px 34px rgba(0,0,0,.5);''',
'''    --shadow:0 10px 34px rgba(0,0,0,.5);
    --shadow-sm:0 1px 2px rgba(0,0,0,.4);
    --shadow-lg:0 18px 50px rgba(0,0,0,.6);
    --shadow-hover:0 12px 32px rgba(0,0,0,.5),0 0 22px rgba(61,220,151,.06);''',
    'dark shadow tokens')

# ---------- 3. 按钮 hover 上浮 ----------
rep('''  .btn:hover{border-color:var(--gold)}''',
'''  .btn:hover{border-color:var(--gold);transform:translateY(-1px);box-shadow:var(--shadow-sm)}''',
    'btn hover lift')

# ---------- 4. 主按钮投影 ----------
rep('''  .btn.primary{background:var(--ink);color:#fff;border-color:var(--ink)}''',
'''  .btn.primary{background:var(--ink);color:#fff;border-color:var(--ink)}
  .btn.primary:hover{box-shadow:0 6px 20px rgba(32,32,30,.24)}''',
    'primary hover shadow')

# ---------- 5. 全局提质块升级：kicker 渐变短线 + card hover token 化 ----------
rep('''  .sec-kicker::before{content:"";position:absolute;left:-15px;top:50%;transform:translateY(-50%);
    width:8px;height:8px;border-radius:2px;background:var(--gold);opacity:.55}''',
'''  .sec-kicker::before{content:"";position:absolute;left:-36px;top:50%;transform:translateY(-50%);
    width:26px;height:2px;border-radius:2px;background:var(--tech);opacity:.9}''',
    'sec-kicker tech tick')

rep('''  .card{transition:box-shadow var(--dur-2),transform var(--dur-2)}
  .card:hover{box-shadow:0 12px 40px rgba(40,36,30,.14)}''',
'''  .card{transition:box-shadow var(--dur-2) var(--ease-std),transform var(--dur-2) var(--ease-std),border-color var(--dur-2) var(--ease-std)}
  .card:hover{box-shadow:var(--shadow-hover);border-color:color-mix(in srgb,var(--gold) 42%,var(--glass-line))}''',
    'card hover tokens')

# ---------- 6. ::selection + 浅色滚动条 ----------
rep('''  img{max-width:100%;display:block}''',
'''  img{max-width:100%;display:block}

  /* ---------- 全局质感：文本选区 / 浅色滚动条 ---------- */
  ::selection{background:color-mix(in srgb,var(--gold) 28%,transparent)}
  ::-webkit-scrollbar{width:10px;height:10px}
  ::-webkit-scrollbar-thumb{background:rgba(40,36,30,.18);border-radius:8px;
    border:2px solid transparent;background-clip:padding-box}
  ::-webkit-scrollbar-thumb:hover{background:rgba(40,36,30,.30);border:2px solid transparent;background-clip:padding-box}
  ::-webkit-scrollbar-track{background:transparent}''',
    'selection + light scrollbar')

# ---------- 7. 输入控件 hover ----------
rep('''  select:focus-visible,.btn:focus-visible,input:focus-visible,textarea:focus-visible{
    outline:2px solid var(--gold);outline-offset:2px}''',
'''  select:focus-visible,.btn:focus-visible,input:focus-visible,textarea:focus-visible{
    outline:2px solid var(--gold);outline-offset:2px}
  input[type=text]:hover:not(:focus),select:hover:not(:focus){
    border-color:color-mix(in srgb,var(--gold) 32%,var(--line))}''',
    'input hover')

# ---------- 8. 画廊卡 hover 质感 ----------
rep('''  .g-item:hover .ov{opacity:1}''',
'''  .g-item{transition:box-shadow var(--dur-2) var(--ease-std),border-color var(--dur-2) var(--ease-std)}
  .g-item:hover{box-shadow:var(--shadow-lg);border-color:color-mix(in srgb,var(--gold) 38%,var(--line))}
  .g-item:hover .ov{opacity:1}''',
    'g-item hover')

# ---------- 9. 空态精致化 ----------
rep('''  .rl-empty{padding:22px;border:1px dashed var(--line);border-radius:12px;
    color:var(--muted);font-size:14px;text-align:center}''',
'''  .rl-empty{padding:26px 22px;border:1px dashed color-mix(in srgb,var(--gold) 30%,var(--line));border-radius:12px;
    color:var(--muted);font-size:14px;text-align:center;
    background:color-mix(in srgb,var(--gold) 4%,transparent)}''',
    'rl-empty')

# ---------- 10. 同步 LED 呼吸 ----------
rep('''  .sync-pill .led{width:7px;height:7px;border-radius:50%;background:#3a9d5d}''',
'''  .sync-pill .led{width:7px;height:7px;border-radius:50%;background:#3a9d5d;animation:ledPulse 2.6s ease-in-out infinite}
  @keyframes ledPulse{0%,100%{box-shadow:0 0 0 0 rgba(58,157,93,.4)}55%{box-shadow:0 0 0 4px rgba(58,157,93,0)}}''',
    'sync led pulse')

# ---------- 写回 ----------
out = data.replace('\n', '\r\n').encode('utf-8')
open(P, 'wb').write(out)

# ---------- 终检 ----------
final = out.decode('utf-8')
for needle in ['--shadow-sm:', '--shadow-lg:', '--shadow-hover:',
               'ledPulse', 'kicker tech', '::selection']:
    pass
assert final.count('--shadow-sm:') == 1 and final.count('--shadow-lg:') == 1
assert final.count('--shadow-hover:') == 1
assert final.count('ledPulse') == 2
assert final.count('::selection') == 1
assert final.count('transform:translateY(-1px);box-shadow:var(--shadow-sm)') == 1
assert final.count('color-mix(in srgb,var(--gold) 42%,var(--glass-line))') == 1
ext = re.findall(r'(?:src|href)="(?:https?:)?//', final)
assert not ext, ext
print('ALL REPS DONE  size=%dKB' % (len(out)//1024))

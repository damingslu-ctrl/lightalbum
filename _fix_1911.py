# -*- coding: utf-8 -*-
"""1.9.11：① 胶片条默认收起 ② 默认深色模式。"""
import io
import sys

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
HTML = ROOT + r'\index.html'
VP = ROOT + r'\client\lightalbum\version.py'

s = io.open(HTML, encoding='utf-8').read()

edits = [
    # ① 胶片条默认收起
    (
        "  // 默认开启胶片条（LR 标志性底栏）；检视面板默认收起\n"
        "  document.body.classList.add('film-open');\n"
        "  const tf = document.getElementById('filmToggle'); if(tf) tf.setAttribute('aria-pressed','true');",
        "  // 1.9.11：胶片条默认收起（不再默认占用底部 122px）；检视面板同样默认收起。\n"
        "  document.body.classList.remove('film-open');\n"
        "  const tf = document.getElementById('filmToggle'); if(tf) tf.setAttribute('aria-pressed','false');",
    ),
    # ② 默认深色模式
    (
        "function initTheme(){\n"
        "  const stored=localStorage.getItem(LS_KEY+'theme');\n"
        "  const sysDark = !!(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);\n"
        "  if(stored==='dark' || (!stored && sysDark)) document.body.classList.add('dark');\n"
        "  syncThemeBtn();\n"
        "  if(window.matchMedia){\n"
        "    const mq=window.matchMedia('(prefers-color-scheme: dark)');\n"
        "    const onSys=ev=>{ if(!localStorage.getItem(LS_KEY+'theme')){ document.body.classList.toggle('dark',ev.matches); syncThemeBtn(); } };\n"
        "    if(mq.addEventListener) mq.addEventListener('change',onSys);\n"
        "  }\n"
        "}",
        "function initTheme(){\n"
        "  const stored=localStorage.getItem(LS_KEY+'theme');\n"
        "  // 1.9.11：默认深色模式。用户显式选择优先（'dark' / 'light'）；\n"
        "  // 从未选择过时固定深色 —— 因此也不再跟随系统明暗（否则系统会把它拽回浅色）。\n"
        "  const dark = stored ? stored==='dark' : true;\n"
        "  document.body.classList.toggle('dark', dark);\n"
        "  syncThemeBtn();\n"
        "}",
    ),
]

for i, (old, new) in enumerate(edits, 1):
    c = s.count(old)
    if c != 1:
        print('FAIL edit %d: count=%d' % (i, c))
        sys.exit(1)
    s = s.replace(old, new)
    print('  ok edit %d' % i)

io.open(HTML, 'w', encoding='utf-8').write(s)
print('WROTE index.html (%d bytes)' % len(s))

v = io.open(VP, encoding='utf-8').read()
assert v.count("APP_VERSION = '1.9.10'") == 1
io.open(VP, 'w', encoding='utf-8').write(v.replace("APP_VERSION = '1.9.10'", "APP_VERSION = '1.9.11'"))
print("version -> 1.9.11")

for probe in ["1.9.11：胶片条默认收起", "1.9.11：默认深色模式"]:
    print(('FOUND ' if probe in s else 'MISSING ') + probe)

# -*- coding: utf-8 -*-
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"
with io.open(PATH, "r", encoding="utf-8") as f:
    s = f.read()

edits = [
    # A: lock html/body to viewport, no window scroll
    (
        "  html{scroll-behavior:smooth}\n  body{",
        "  html{scroll-behavior:smooth}\n"
        "  /* 固定外壳（1.9.9）：html/body 锁定视口、不再滚动；滚动只发生在 .main-col 内部 */\n"
        "  html,body{height:100%;overflow:hidden}\n"
        "  body{",
    ),
    # B: #shell fixed height + hidden overflow
    (
        "  #shell{display:flex;align-items:stretch;min-height:100vh}",
        "  #shell{display:flex;align-items:stretch;height:100vh;overflow:hidden}",
    ),
    # C: .main-col becomes the scroll container
    (
        "  .main-col{flex:1;min-width:0;display:flex;flex-direction:column}",
        "  .main-col{flex:1;min-width:0;display:flex;flex-direction:column;height:100vh;overflow-y:auto;overscroll-behavior:contain}",
    ),
    # D: mobile: move the 64px clearance from body to .main-col (now the scroller)
    (
        "    .mobile-nav a.active{color:var(--ink)}\n    body{padding-bottom:64px}\n",
        "    .mobile-nav a.active{color:var(--ink)}\n    .main-col{padding-bottom:64px}\n",
    ),
    # E: retarget to-top window-scroll JS to the .main-col scroll container
    (
        "  const toTop=document.getElementById('toTop');\n"
        "  let scrollTicking=false;\n"
        "  window.addEventListener('scroll',()=>{\n"
        "    if(scrollTicking) return;\n"
        "    scrollTicking=true;\n"
        "    requestAnimationFrame(()=>{ toTop.classList.toggle('show', window.scrollY>400); scrollTicking=false; });\n"
        "  },{passive:true});\n"
        "  toTop.addEventListener('click',()=>window.scrollTo({top:0,behavior:'smooth'}));",
        "  const toTop=document.getElementById('toTop');\n"
        "  let scrollTicking=false;\n"
        "  /* 固定外壳（1.9.9）：滚动容器是 .main-col 而非 window；回退到 window 兼容极端情况 */\n"
        "  const _scrollBox=document.querySelector('.main-col')||window;\n"
        "  _scrollBox.addEventListener('scroll',()=>{\n"
        "    if(scrollTicking) return;\n"
        "    scrollTicking=true;\n"
        "    requestAnimationFrame(()=>{ toTop.classList.toggle('show', _scrollBox.scrollTop>400); scrollTicking=false; });\n"
        "  },{passive:true});\n"
        "  toTop.addEventListener('click',()=>_scrollBox.scrollTo({top:0,behavior:'smooth'}));",
    ),
]

for i, (old, new) in enumerate(edits, 1):
    c = s.count(old)
    if c != 1:
        print("FAIL edit %d: expected count 1, got %d" % (i, c))
        sys.exit(1)
    s = s.replace(old, new)
    print("ok edit %d" % i)

with io.open(PATH, "w", encoding="utf-8") as f:
    f.write(s)
print("WROTE index.html, new length=%d" % len(s))

# sanity prints
import re
for probe in ["html,body{height:100%;overflow:hidden}", "#shell{display:flex;align-items:stretch;height:100vh;overflow:hidden}",
              ".main-col{flex:1;min-width:0;display:flex;flex-direction:column;height:100vh;overflow-y:auto",
              ".main-col{padding-bottom:64px}", "_scrollBox"]:
    print(("FOUND " if probe in s else "MISSING ") + probe)

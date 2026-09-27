#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""1.9.26 · Hero 视差/光斑 + 抽屉光边（红线：不动 .drawer-panel transform）
三处 rep，anchor 各 count==1：
  A) CSS 块插在第一个 </style> 前（anchor='\n</style>\n</head>'）
  B) markup：#home .hero-scrim 后插入 <div class="hero-orbs">
  C) JS：主脚本收尾前插入视差 IIFE（anchor='\n</script>\n</body>'）
约束：零外链、双主题、reduced-motion 仅停循环/一次性装饰、单主 script。
"""
import io

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"

CSS = r"""
/* ========== 1.9.26 · Hero 视差/光斑 + 抽屉光边（红线：不动 .drawer-panel transform） ========== */
/* --- Hero 光斑：漂浮辉光球，screen 混合只在照片上「打光」，不抢文案（z 介于 scrim 与 copy 之间） --- */
.hero-orbs{position:absolute;inset:0;z-index:5;pointer-events:none;overflow:hidden;mix-blend-mode:screen}
.hero-orbs i{position:absolute;border-radius:50%;filter:blur(28px);opacity:.55;mix-blend-mode:screen;will-change:transform}
.hero-orbs i:nth-child(1){width:340px;height:340px;left:6%;top:14%;background:radial-gradient(circle,color-mix(in srgb,var(--gold) 85%,transparent),transparent 70%);animation:orbA 15s ease-in-out infinite alternate}
.hero-orbs i:nth-child(2){width:280px;height:280px;right:10%;top:26%;background:radial-gradient(circle,color-mix(in srgb,var(--blue) 75%,transparent),transparent 70%);animation:orbB 19s ease-in-out infinite alternate}
.hero-orbs i:nth-child(3){width:210px;height:210px;left:44%;bottom:6%;background:radial-gradient(circle,color-mix(in srgb,var(--sun) 75%,transparent),transparent 70%);animation:orbC 17s ease-in-out infinite alternate}
@keyframes orbA{0%{transform:translate(0,0) scale(1)}100%{transform:translate(46px,-34px) scale(1.12)}}
@keyframes orbB{0%{transform:translate(0,0) scale(1)}100%{transform:translate(-54px,22px) scale(1.1)}}
@keyframes orbC{0%{transform:translate(0,0) scale(1)}100%{transform:translate(34px,30px) scale(1.14)}}
/* --- 抽屉弹簧感：仅供光边/辉光，绝不碰 .drawer-panel 的 transform 位移（记忆 J 红线） --- */
.drawer.show .drawer-panel{
  box-shadow:-18px 0 64px color-mix(in srgb,var(--gold) 20%,transparent),
            -3px 0 0 color-mix(in srgb,var(--gold) 38%,transparent);
  animation:drawerGlow 3.4s ease-in-out infinite;
}
@keyframes drawerGlow{
  0%,100%{box-shadow:-18px 0 64px color-mix(in srgb,var(--gold) 16%,transparent),-3px 0 0 color-mix(in srgb,var(--gold) 34%,transparent)}
  50%{box-shadow:-18px 0 64px color-mix(in srgb,var(--gold) 32%,transparent),-3px 0 0 color-mix(in srgb,var(--gold) 62%,transparent)}}
/* 开场一道光沿边缘扫过（纯 opacity，不改位移） */
.drawer.show .drawer-panel::after{
  content:'';position:absolute;top:0;bottom:0;left:0;width:2px;pointer-events:none;
  background:linear-gradient(180deg,transparent,color-mix(in srgb,var(--gold) 95%,transparent),transparent);
  opacity:0;animation:edgeSweep 1.1s var(--ease-out) .04s 1 both}
@keyframes edgeSweep{0%{opacity:0}28%{opacity:1}100%{opacity:0}}
body.dark .drawer.show .drawer-panel{
  box-shadow:-18px 0 64px color-mix(in srgb,var(--gold) 28%,transparent),-3px 0 0 color-mix(in srgb,var(--gold) 48%,transparent)}
/* reduced-motion：停掉本轮新增循环/一次性装饰（不破坏布局） */
@media (prefers-reduced-motion: reduce){
  .hero-orbs i,.drawer.show .drawer-panel{animation:none}
  .drawer.show .drawer-panel::after{animation:none;opacity:0}
}
"""

ORBS_MARKUP = '    <div class="hero-orbs" aria-hidden="true"><i></i><i></i><i></i></div>'

JS = r"""
/* 1.9.26 Hero 视差：.main-col 滚动驱动，仅前 1.3 屏；reduced-motion 跳过。
   动 .band(缩放 Ken-Burns) 与 .hero-copy(上浮)，绝不碰 .drawer-panel。 */
(function(){
  try{
    var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    if(reduce) return;
    var scroller = document.querySelector('.main-col');
    var band = document.querySelector('#home .band');
    var copy = document.querySelector('#home .hero-copy');
    if(!scroller) return;
    var ticking=false, lim=0;
    function apply(){
      ticking=false;
      var y = scroller.scrollTop || 0;
      lim = lim || (window.innerHeight * 1.3);
      var k = y > lim ? lim : y;
      if(band) band.style.transform = 'scale(' + (1 + k*0.00006).toFixed(4) + ')';
      if(copy) copy.style.transform = 'translate3d(0,' + (-k*0.04).toFixed(1) + 'px,0)';
    }
    function onScroll(){ if(!ticking){ ticking=true; requestAnimationFrame(apply); } }
    scroller.addEventListener('scroll', onScroll, {passive:true});
    apply();
  }catch(e){}
})();
"""


def rep(data, old, new):
    c = data.count(old)
    assert c == 1, "anchor count=%d (expected 1) for: %.40r" % (c, old)
    return data.replace(old, new, 1)


with io.open(SRC, "r", encoding="utf-8") as f:
    html = f.read()

# A) CSS
html = rep(html, "\n</style>\n</head>", "\n" + CSS + "\n</style>\n</head>")
# B) markup
anchor_b = '    <div class="hero-scrim" aria-hidden="true"></div>'
html = rep(html, anchor_b, anchor_b + "\n" + ORBS_MARKUP)
# C) JS (main script tail) —— 主脚本 </script> 后紧跟注释，不是 </body>
anchor_c = "\n</script>\n\n<!-- 内嵌编辑台已迁入 #retouch 左列（.rl-main），此处不再有弹出模态 -->\n"
html = rep(html, anchor_c, "\n" + JS + anchor_c)

with io.open(SRC, "w", encoding="utf-8") as f:
    f.write(html)

# 自检
assert html.count("@keyframes orbA") == 1
assert html.count("@keyframes drawerGlow") == 1
assert html.count("@keyframes edgeSweep") == 1
assert html.count('class="hero-orbs"') == 1
assert html.count("initHeroParallax") == 0  # 命名未用，避免混淆
assert html.count("requestAnimationFrame(apply)") == 1
print("UI_UPGRADE_1926_OK bytes=%d" % len(html.encode("utf-8")))

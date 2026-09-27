# -*- coding: utf-8 -*-
"""
1.8.0 改造：
  ① 侧边栏 → 磨砂玻璃（半透明白底 + 24px 模糊 + 1px 亮白描边，底下渐变/照片透上来，栏与页面产生真实前后层次）。
  ② 首页首图 → 反向图带：两行图，一行向左一行向右（速度差=深度）；中间一组叠着的 3 张图，鼠标靠近即张开。
改写范式（沿用 CRLF）：二进制读 -> 去 \r\n -> rep(精确/正则，全带断言) -> 写回 \r\n。
"""
import io, re, sys

PATH = 'index.html'

def rep(data, old, new, label, n=1):
    c = data.count(old)
    if c != n:
        raise SystemExit(f"[ASSERT] {label}: expected {n} occurrence(s), found {c}")
    return data.replace(old, new)

def rep_re(data, pat, new, label):
    m = re.findall(pat, data)
    if len(m) != 1:
        raise SystemExit(f"[ASSERT] {label}: expected 1 regex match, found {len(m)}")
    return re.sub(pat, new, data, count=1)

with io.open(PATH, 'rb') as f:
    raw = f.read()
data = raw.decode('utf-8').replace('\r\n', '\n')

# ============ ① 侧边栏：磨砂玻璃 ============
OLD_SB = (
"  #sidebar{position:sticky;top:0;height:100vh;flex:0 0 232px;display:flex;flex-direction:column;gap:18px;\n"
"    padding:22px 14px 18px;background:var(--glass);border-right:1px solid var(--glass-line);\n"
"    backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur);z-index:60}"
)
NEW_SB = (
"  /* 磨砂玻璃侧边栏：半透明白底 + 24px 模糊 + 1px 亮白描边，底下渐变/照片隐约透上来，栏与页面产生真实前后层次 */\n"
"  #sidebar{position:sticky;top:0;height:100vh;flex:0 0 232px;display:flex;flex-direction:column;gap:18px;\n"
"    padding:22px 14px 18px;background:rgba(255,255,255,.5);border:1px solid rgba(255,255,255,.72);\n"
"    backdrop-filter:blur(24px) saturate(180%);-webkit-backdrop-filter:blur(24px) saturate(180%);z-index:60;\n"
"    box-shadow:0 0 0 1px rgba(255,255,255,.25), 0 18px 50px rgba(40,34,24,.18)}\n"
"  body.dark #sidebar{background:rgba(18,22,20,.42);border:1px solid rgba(255,255,255,.16);\n"
"    backdrop-filter:blur(24px) saturate(160%);-webkit-backdrop-filter:blur(24px) saturate(160%);\n"
"    box-shadow:0 0 0 1px rgba(255,255,255,.06), 0 18px 50px rgba(0,0,0,.45)}"
)
data = rep(data, OLD_SB, NEW_SB, 'sidebar-glass', 1)

# ============ ② 首页轮播 CSS -> 反向图带 CSS ============
OLD_HERO_CSS = (
"  /* ---------- 首页轮播 ---------- */\n"
"  .hero{position:relative;border-radius:24px;overflow:hidden;box-shadow:var(--shadow);background:#15140f}\n"
"  .slides{position:relative;height:480px}\n"
"  .slide{position:absolute;inset:0;opacity:0;transition:opacity var(--dur-hero) ease;display:flex;align-items:flex-end}\n"
"  .slide.show{opacity:1}\n"
"  .slide .ph{position:absolute;inset:0}\n"
"  .slide .cap{position:relative;z-index:2;padding:34px 38px;color:#fff;width:100%;\n"
"    background:linear-gradient(transparent,rgba(0,0,0,.62))}\n"
"  .slide .cap .t{font-size:26px;font-weight:700;letter-spacing:1px}\n"
"  .slide .cap .m{font-size:14px;opacity:.85;margin-top:4px}\n"
"  .slide .cap .tag{display:inline-block;margin-top:10px;font-size:12px;padding:4px 12px;border-radius:999px;\n"
"    background:rgba(255,255,255,.18);border:1px solid rgba(255,255,255,.3)}\n"
"  .hero-btn{position:absolute;top:50%;transform:translateY(-50%);z-index:3;width:46px;height:46px;border-radius:50%;\n"
"    background:rgba(255,255,255,.16);color:#fff;border:1px solid rgba(255,255,255,.35);font-size:20px;\n"
"    display:flex;align-items:center;justify-content:center;backdrop-filter:blur(4px)}\n"
"  .hero-btn:hover{background:rgba(255,255,255,.3)}\n"
"  .hero-btn.prev{left:16px}.hero-btn.next{right:16px}\n"
"  .dots{position:absolute;bottom:18px;left:50%;transform:translateX(-50%);z-index:3;display:flex;gap:8px}\n"
"  .dots button{width:9px;height:9px;border-radius:50%;background:rgba(255,255,255,.4)}\n"
"  .dots button.on{background:#fff;width:22px;border-radius:6px}"
)
NEW_HERO_CSS = (
"  /* ---------- 首页 · 反向图带 ---------- */\n"
"  .hero.hero-band{height:480px;background:linear-gradient(135deg,#0e1320,#1a2436 55%,#0e1320);border-radius:24px;overflow:hidden;box-shadow:var(--shadow)}\n"
"  body.dark .hero.hero-band{background:linear-gradient(135deg,#0b0f17,#161f30 55%,#0b0f17)}\n"
"  .band{position:absolute;inset:0}\n"
"  .band-row{position:absolute;left:-3%;right:-3%;top:0;height:62%;overflow:hidden;display:flex;align-items:center;\n"
"    -webkit-mask-image:linear-gradient(90deg,transparent,#000 7%,#000 93%,transparent);\n"
"            mask-image:linear-gradient(90deg,transparent,#000 7%,#000 93%,transparent)}\n"
"  .band-row-b{bottom:0;top:auto;height:62%}\n"
"  .band-track{display:flex;gap:16px;padding:0 8px;width:max-content;will-change:transform}\n"
"  .band-row-a .band-track{animation:bandScroll 40s linear infinite}\n"
"  .band-row-b .band-track{animation:bandScroll 56s linear infinite reverse}\n"
"  .band-card{width:212px;height:140px;flex:0 0 auto;border-radius:14px;background:#222 center/cover no-repeat;\n"
"    box-shadow:0 10px 30px rgba(0,0,0,.35);border:1px solid rgba(255,255,255,.16)}\n"
"  .band-row-b .band-card{height:120px;width:188px;opacity:.86}\n"
"  @keyframes bandScroll{from{transform:translateX(0)}to{transform:translateX(-50%)}}\n"
"  /* 中间叠堆：默认轻微错位叠放，悬停张开 */\n"
"  .band-center{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;pointer-events:none;z-index:6}\n"
"  .band-stack{position:relative;width:248px;height:286px;pointer-events:auto;cursor:pointer}\n"
"  .band-stack .bc{position:absolute;inset:0;border-radius:18px;background:#2a2f3a center/cover no-repeat;\n"
"    border:1px solid rgba(255,255,255,.5);box-shadow:0 18px 50px rgba(0,0,0,.5);\n"
"    transition:transform .55s cubic-bezier(.2,.7,.3,1),box-shadow .4s;transform-origin:center}\n"
"  .band-stack .bc-1{transform:translate(-12px,-16px) rotate(-6deg);z-index:1}\n"
"  .band-stack .bc-2{transform:translate(0,0) rotate(0deg);z-index:2}\n"
"  .band-stack .bc-3{transform:translate(12px,16px) rotate(6deg);z-index:3}\n"
"  .band-stack:hover .bc-1{transform:translate(-132px,-30px) rotate(-13deg)}\n"
"  .band-stack:hover .bc-2{transform:translate(0,-6px) rotate(0deg) scale(1.05)}\n"
"  .band-stack:hover .bc-3{transform:translate(132px,30px) rotate(13deg)}\n"
"  .band-stack:hover .bc{box-shadow:0 26px 64px rgba(0,0,0,.6)}\n"
"  .band-cap{position:absolute;left:22px;top:18px;z-index:7;font-size:13px;letter-spacing:3px;color:rgba(255,255,255,.85);\n"
"    text-shadow:0 2px 10px rgba(0,0,0,.55);pointer-events:none}"
)
data = rep(data, OLD_HERO_CSS, NEW_HERO_CSS, 'hero-band-css', 1)

# ============ hero-progress -> 图带悬停暂停 ============
OLD_HP = (
"  .hero-progress{position:absolute;left:0;right:0;bottom:0;height:3px;z-index:3;background:rgba(255,255,255,.15)}\n"
"  .hero-progress i{display:block;height:100%;transform-origin:left center;background:#fff;opacity:.85;animation:heroProg 5s linear infinite}\n"
"  .hero:hover .hero-progress i{animation-play-state:paused}"
)
NEW_HP = (
"  /* 反向图带：悬停暂停滚动（图会回应你） */\n"
"  .band.paused .band-track{animation-play-state:paused}"
)
data = rep(data, OLD_HP, NEW_HP, 'hero-progress', 1)

# ============ hero-count -> 图带品牌字 ============
OLD_HC = (
"  .hero-count{position:absolute;top:16px;right:16px;z-index:3;background:rgba(0,0,0,.42);color:#fff;\n"
"    font-size:13px;padding:4px 11px;border-radius:999px;font-variant-numeric:tabular-nums;backdrop-filter:blur(4px)}"
)
NEW_HC = (
"  .band-cap{position:absolute;left:22px;top:18px;z-index:7;font-size:13px;letter-spacing:3px;color:rgba(255,255,255,.85);\n"
"    text-shadow:0 2px 10px rgba(0,0,0,.55);pointer-events:none}"
)
# 上方已注入 .band-cap；此行改为空（避免重复定义）
data = rep(data, OLD_HC, NEW_HC, 'hero-count', 1)

# ============ reduced-motion：.hero-progress i -> .band-track ============
OLD_RM = "  @media(prefers-reduced-motion:reduce){ .g-item{animation:none} .bar,.slice{animation:none} .hero-progress i{animation:none}"
NEW_RM = "  @media(prefers-reduced-motion:reduce){ .g-item{animation:none} .bar,.slice{animation:none} .band-track{animation:none}"
data = rep(data, OLD_RM, NEW_RM, 'reduced-motion', 1)

# ============ ② HTML：hero 轮播 -> 反向图带 ============
OLD_HTML = (
"<!-- 首页轮播 -->\n"
"<section id=\"home\">\n"
"  <div class=\"hero\">\n"
"    <div class=\"slides\" id=\"slides\"></div>\n"
"    <button class=\"hero-btn prev\" id=\"heroPrev\" aria-label=\"上一张\">‹</button>\n"
"    <button class=\"hero-btn next\" id=\"heroNext\" aria-label=\"下一张\">›</button>\n"
"    <div class=\"dots\" id=\"heroDots\"></div>\n"
"    <div class=\"hero-progress\"><i></i></div>\n"
"    <div class=\"hero-count\" id=\"heroCount\">1 / 5</div>\n"
"  </div>\n"
"</section>"
)
NEW_HTML = (
"<!-- 首页 · 反向图带 -->\n"
"<section id=\"home\">\n"
"  <div class=\"hero hero-band\">\n"
"    <div class=\"band-cap\">光影集</div>\n"
"    <div class=\"band\" id=\"band\">\n"
"      <div class=\"band-row band-row-a\"><div class=\"band-track\" id=\"bandRowA\"></div></div>\n"
"      <div class=\"band-row band-row-b\"><div class=\"band-track\" id=\"bandRowB\"></div></div>\n"
"      <div class=\"band-center\">\n"
"        <div class=\"band-stack\" id=\"bandStack\">\n"
"          <div class=\"bc bc-1\"></div>\n"
"          <div class=\"bc bc-2\"></div>\n"
"          <div class=\"bc bc-3\"></div>\n"
"        </div>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"</section>"
)
data = rep(data, OLD_HTML, NEW_HTML, 'hero-html', 1)

# ============ ② JS：renderCarousel(轮播) -> renderHeroBand(图带) ============
NEW_RENDER = (
"function renderHeroBand(){\n"
"  const rowA = document.getElementById('bandRowA');\n"
"  const rowB = document.getElementById('bandRowB');\n"
"  const stack = document.getElementById('bandStack');\n"
"  if(!rowA) return;\n"
"  const picks = sortPhotos(PHOTOS,'new');\n"
"  const N = picks.length;\n"
"  const total = Math.max(8, N);\n"
"  const seq = [];\n"
"  for(let i=0;i<total;i++) seq.push(N ? picks[i%N] : null);\n"
"  state._bandData = seq.slice();\n"
"  const card = (p,i)=>{\n"
"    const bg = p && p.img ? `url('${p.img}')` : (p ? gradFor(p.t+p.r) : 'linear-gradient(135deg,#3f6098,#4b3f7a)');\n"
"    const title = p ? escHtml(p.t) : '光影集';\n"
"    return `<div class=\"band-card\" data-i=\"${i}\" title=\"${title}\" style=\"background:${bg} center/cover no-repeat\"></div>`;\n"
"  };\n"
"  const rowHtml = seq.map((p,i)=>card(p,i)).join('');\n"
"  rowA.innerHTML = rowHtml + rowHtml;                 // 双份实现无缝循环\n"
"  const seqB = seq.slice().reverse();\n"
"  const rowHtmlB = seqB.map((p,i)=>card(p,i)).join('');\n"
"  if(rowB) rowB.innerHTML = rowHtmlB + rowHtmlB;\n"
"  // 中间叠堆：取前 3 张（无图回退渐变）\n"
"  const top3 = picks.slice(0,3);\n"
"  const fb = ['linear-gradient(135deg,#3f6098,#4b3f7a)','linear-gradient(135deg,#4b3f7a,#7a3f5a)','linear-gradient(135deg,#2b6f8f,#3a6098)'];\n"
"  for(let k=0;k<3;k++){\n"
"    const el = stack && stack.querySelector('.bc-'+(k+1));\n"
"    if(!el) continue;\n"
"    const p = top3[k] || null;\n"
"    const idx = (p && N) ? picks.indexOf(p) : (N ? (k%N) : 0);\n"
"    el.dataset.i = (idx>=0 ? idx : 0);\n"
"    const bg = p && p.img ? `url('${p.img}')` : (p ? gradFor(p.t+p.r) : fb[k]);\n"
"    el.style.background = bg + ' center/cover no-repeat';\n"
"  }\n"
"}"
)
data = rep_re(data, r'function renderCarousel\(\)\{[\s\S]*?\n\}', NEW_RENDER, 'renderCarousel->band')

# ============ goCarousel 三连 -> 空操作桩（保留键盘处理器调用安全） ============
NEW_GOCAR = (
"// 首页图带取代了旧轮播；保留 goCarousel 空钩子以免破坏全局键盘处理器（左右键不再驱动轮播）\n"
"function goCarousel(){ /* no-op: hero is now a reverse image band */ }"
)
data = rep_re(data, r'function updateHeroCounter\(\)\{[\s\S]*?\n\}', NEW_GOCAR, 'updateHeroCounter..goCarousel')

# ============ setupCarousel -> setupHeroBand ============
OLD_SETUP = (
"function setupCarousel(){\n"
"  document.getElementById('heroPrev').addEventListener('click',()=>goCarousel(state.carousel-1));\n"
"  document.getElementById('heroNext').addEventListener('click',()=>goCarousel(state.carousel+1));\n"
"  const hero=document.querySelector('.hero');\n"
"  hero.addEventListener('mouseenter',stopCarousel);\n"
"  hero.addEventListener('mouseleave',startCarousel);\n"
"  // 触屏滑动\n"
"  const sl=document.getElementById('slides'); let sx=0,active=false;\n"
"  sl.addEventListener('pointerdown',e=>{active=true;sx=e.clientX;});\n"
"  sl.addEventListener('pointerup',e=>{ if(!active)return; active=false; const dx=e.clientX-sx; if(Math.abs(dx)>40) goCarousel(state.carousel+(dx<0?1:-1)); });\n"
"  sl.addEventListener('pointerleave',()=>active=false);\n"
"  startCarousel();\n"
"}"
)
NEW_SETUP = (
"function setupHeroBand(){\n"
"  const hero=document.querySelector('.hero-band');\n"
"  const band=document.getElementById('band');\n"
"  if(hero && band){\n"
"    // 鼠标靠近整块图带 → 暂停滚动（图会回应你）\n"
"    hero.addEventListener('mouseenter',()=>band.classList.add('paused'));\n"
"    hero.addEventListener('mouseleave',()=>band.classList.remove('paused'));\n"
"    // 点击图卡：有作品开灯箱，否则滚动到图库\n"
"    band.addEventListener('click',(e)=>{\n"
"      const c=e.target.closest('.band-card'); if(!c) return;\n"
"      const i=+c.dataset.i; const p=(state._bandData||[])[i];\n"
"      if(p && p.id!=null){ selectPhoto(p.id, true); }\n"
"      else { const g=document.getElementById('gallery'); if(g) g.scrollIntoView({behavior:'smooth'}); }\n"
"    });\n"
"  }\n"
"}"
)
data = rep(data, OLD_SETUP, NEW_SETUP, 'setupCarousel->band', 1)

# ============ 调用点改名 ============
data = rep(data, "  renderCarousel();", "  renderHeroBand();", 'call-render', 1)
data = rep(data, "  setupCarousel();", "  setupHeroBand();", 'call-setup', 1)

# ============ 验证：应存在 / 应消失 ============
must_have = ["hero-band", "bandRowA", "bandRowB", "bandStack", "renderHeroBand", "setupHeroBand",
             "band-stack", "bandScroll", ".band.paused .band-track"]
must_gone = ["id=\"slides\"", "id=\"heroPrev\"", "id=\"heroNext\"", "id=\"heroDots\"", "id=\"heroCount\"",
             "getElementById('heroPrev')", "getElementById('heroNext')", "getElementById('slides')",
             "getElementById('heroDots')", "getElementById('heroCount')"]
for s in must_have:
    if s not in data:
        raise SystemExit(f"[VERIFY] missing expected marker: {s}")
for s in must_gone:
    if s in data:
        raise SystemExit(f"[VERIFY] should be removed but still present: {s}")

out = data.replace('\n', '\r\n').encode('utf-8')
with io.open(PATH, 'wb') as f:
    f.write(out)

print("redesign_hero_band: all edits applied; markers verified.")

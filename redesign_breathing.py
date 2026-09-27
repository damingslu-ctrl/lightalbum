# -*- coding: utf-8 -*-
"""
redesign_breathing.py  —  v1.8.1
① 移除「按光影时段分类」的 SERIES 体系（常量 / 自动归类 / 渲染 / #series 区块 / 两个 nav 链接 /
   统计饼图 / renderGroupedGallery 的「按光影时段」分组 / gcolItemsOf·openGroupOverlay 的 series 分支 /
   灯箱与月相逻辑对 p.s 的依赖），保留 p.s 为空字段不报错。
② 首页照片样式新颖氛围改造（hero 调色板色雾气光晕；行距/遮罩放宽；中心叠堆略放大）。
③ 全客户端「呼吸感」：section 纵向 padding、sec-head/title/sub、gallery 列间距与 g-item 下边距、nav 间距。

CRLF 安全：读 → 去 CRLF → 全部 rep（count==1 断言）→ 写回 CRLF。
"""
import io, re, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"

def load():
    with io.open(PATH, "r", encoding="utf-8") as f:
        return f.read()

def save(s):
    with io.open(PATH, "w", encoding="utf-8", newline="") as f:
        f.write(s)

data = load()
lf = data.replace("\r\n", "\n")

fails = []
def rep_exact(old, new, label):
    global lf
    n = lf.count(old)
    if n != 1:
        fails.append(f"[exact] {label}: expected 1, found {n}")
        print(f"  !! FAIL {label}: count={n}")
        return
    lf = lf.replace(old, new, 1)
    print(f"  ok  {label}")

def rep_re(pat, new, label):
    global lf
    m = re.search(pat, lf, re.S)
    if not m:
        fails.append(f"[re] {label}: no match")
        print(f"  !! FAIL {label}: no match")
        return
    lf = lf[:m.start()] + new + lf[m.end():]
    print(f"  ok  {label}")

print("== A. 移除光影时段 SERIES 分类体系 ==")

# A1 常量
rep_exact(
"const SERIES = {\n"
"  '蓝调时刻': {color:'#3f6098', desc:'日出前与日落后的幽蓝时分，冷暖交界处的静谧。'},\n"
"  '黄金时刻': {color:'#d9a441', desc:'日出后与日落前一小时，温暖金光铺满大地。'},\n"
"  '星汉灿烂': {color:'#4b3f7a', desc:'无月晴夜，银河横贯天际的璀璨。'},\n"
"  '日月交辉': {color:'#c76b3a', desc:'日月同框、曙暮光与天体交辉的奇观。'},\n"
"  '夜阑灯火': {color:'#3f6098', desc:'夜幕落下后的城市灯火与霓虹，人间烟火的另一面。'},\n"
"  '雪国幻境': {color:'#37e0ff', desc:'冰雪覆盖的纯白世界，万籁俱寂的冷冽之美。'},\n"
"  '雾漫仙境': {color:'#3ddc97', desc:'云雾缭绕的山水林间，虚实交界的氤氲氛围。'}\n"
"};",
"const SERIES = {};  // 光影时段系列分类已移除（v1.8.1）；全部作品统一以年份图库呈现",
"SERIES const -> {}")

# A2 normalizePhoto
rep_exact(
"  const seriesKeys = Object.keys(SERIES);\n  const _lo = isFinite(parseFloat(o.lo)) ? parseFloat(o.lo) : null;",
"  const _lo = isFinite(parseFloat(o.lo)) ? parseFloat(o.lo) : null;",
"remove seriesKeys line")
rep_exact(
"    s:    seriesKeys.indexOf(o.s)>=0 ? o.s : seriesKeys[0],",
"    s:    safeStr(o.s, ''),",
"normalizePhoto s: default ''")

# A3 SERIES_OPTS + photoToProps 系列字段
rep_exact(
"var SERIES_OPTS = ['蓝调时刻','黄金时刻','星汉灿烂','日月交辉'];\n",
"",
"remove SERIES_OPTS")
rep_exact(
"    '系列':   { select: (SERIES_OPTS.indexOf(p.s)>=0 ? p.s : '蓝调时刻') },\n",
"",
"remove photoToProps 系列 select")
rep_exact(
"      s: g('系列','蓝调时刻'),\n",
"",
"remove photoToProps reverse s")

# A4 SERIES_BAND + seriesConsistency（含注释，整块移除）
rep_re(
r"/\* ---------- 系列与光线的一致性 ----------.*?\n\}\n",
"",
"remove SERIES_BAND + seriesConsistency")

# A5 renderSeries 函数（含后接 renderYears 作锚）
rep_re(
r"function renderSeries\(\)\{.*?\n\}\nfunction renderYears\(\)\{",
"function renderYears(){",
"remove renderSeries()")
# A6 renderSeries() 调用
rep_exact(
"  renderSeries();\n",
"",
"remove renderSeries() call")

# A7 #series 区块
rep_re(
r"<!-- 系列作品 -->.*?</section>\n",
"",
"remove #series section")

# A8 两个 nav 链接
rep_exact(
'    <a href="#series" data-sec="series"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/></svg>系列</a>\n',
"",
"remove topnav 系列 link")
rep_exact(
'  <a href="#series" data-sec="series">\n'
'    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/></svg>系列</a>\n',
"",
"remove mobilenav 系列 link")

# A9 renderGroupedGallery ②「按光影时段」分组
rep_re(
r"  // ② 按光影时段（既有的时序系列）.*?  \}\);\n",
"",
"remove grouped 按光影时段 block")

# A10 gcolItemsOf series 分支
rep_exact(
"  if(kind==='series') return PHOTOS.filter(p=>p.s===key);\n",
"",
"remove gcolItemsOf series branch")
# A11 openGroupOverlay series 分支
rep_exact(
"  if(kind==='series'){ items=gcolItemsOf('series',key); title=key; }\n",
"",
"remove openGroupOverlay series branch")

# A12 统计饼图 JS + HTML
rep_re(
r"  // 系列分布\n  const sData=Object\.keys\(SERIES\)\.map.*?\.join\(''\);\n",
"  // 系列时段分布图已随光影时段分类移除（v1.8.1）\n",
"remove stats chartSeries JS")
rep_exact(
'  <div class="card stat-card" style="margin-top:18px">\n'
'    <h4>系列时段分布</h4><div class="hint">不同系列对应的光线窗口</div>\n'
'    <div id="chartSeries" style="display:flex;gap:22px;flex-wrap:wrap;align-items:center"></div>\n'
'    <div class="legend" id="seriesLegend"></div>\n'
'  </div>\n',
"",
"remove stats chartSeries HTML")

# A13 灯箱 meta 去掉空 p.s
rep_exact(
'${regionName(p.r)} · ${p.y} · ${p.s} · ${p.o===\'landscape\'?\'横构图\':\'竖构图\'}',
'${regionName(p.r)} · ${p.y}${p.s?(\' · \'+p.s):\'\'} · ${p.o===\'landscape\'?\'横构图\':\'竖构图\'}',
"lightbox meta drop empty p.s")
# A14 月相/银河不再依赖 p.s
rep_exact(
"  if(t.moon && (p.s === '星汉灿烂' || (t.band === 'night'))){",
"  if(t.moon && t.band === 'night'){",
"moon logic drop p.s")
# A15 洞察文案去光影时段引用
rep_exact(
"    `足迹横跨 <b>${countries}</b> 个地区，平均每年约 <b>${perYr}</b> 幅。星野与蓝调，是你反复回到的光线窗口。`;",
"    `足迹横跨 <b>${countries}</b> 个地区，平均每年约 <b>${perYr}</b> 幅。风光与夜色，是你反复回到的母题。`;",
"insight text drop series")

# A16 SERIES_HINT + guessSeries + 用法
rep_re(
r"const SERIES_HINT = \[.*?\n\];\n",
"const SERIES_HINT = [];  // 光影时段自动归类已移除（v1.8.1）\n",
"remove SERIES_HINT")
rep_re(
r"function guessSeries\(full\)\{.*?\n\}\n",
"function guessSeries(full){ return ''; }  // 光影时段自动归类已移除（v1.8.1）\n",
"neutralize guessSeries")
rep_exact(
"    s: ov.s || guessSeries(full) || Object.keys(SERIES)[0],",
"    s: ov.s ? String(ov.s) : '',",
"import s: drop guessSeries")

print("== B. 首页照片样式：新颖氛围改造 ==")
# B-hero 雾气光晕（调色板色，非滥用装饰渐变）
rep_exact(
"  .hero.hero-band{height:480px;background:linear-gradient(135deg,#0e1320,#1a2436 55%,#0e1320);border-radius:24px;overflow:hidden;box-shadow:var(--shadow)}\n"
"  body.dark .hero.hero-band{background:linear-gradient(135deg,#0b0f17,#161f30 55%,#0b0f17)}",
"  .hero.hero-band{height:clamp(480px,52vh,560px);border-radius:28px;overflow:hidden;box-shadow:var(--shadow);\n"
"    background:\n"
"      radial-gradient(120% 90% at 16% -8%,rgba(226,166,58,.20),transparent 52%),\n"
"      radial-gradient(90% 80% at 102% 108%,rgba(61,220,151,.16),transparent 60%),\n"
"      radial-gradient(70% 70% at 70% 12%,rgba(79,154,168,.12),transparent 64%),\n"
"      linear-gradient(135deg,#0e1320,#1a2436 55%,#0e1320)}\n"
"  body.dark .hero.hero-band{background:\n"
"      radial-gradient(120% 90% at 16% -8%,rgba(226,166,58,.14),transparent 52%),\n"
"      radial-gradient(90% 80% at 102% 108%,rgba(61,220,151,.12),transparent 60%),\n"
"      linear-gradient(135deg,#0b0f17,#161f30 55%,#0b0f17)}",
"hero atmospheric glow")
# B-mask 放宽
rep_exact(
"    -webkit-mask-image:linear-gradient(90deg,transparent,#000 7%,#000 93%,transparent);\n"
"            mask-image:linear-gradient(90deg,transparent,#000 7%,#000 93%,transparent)}",
"    -webkit-mask-image:linear-gradient(90deg,transparent,#000 12%,#000 88%,transparent);\n"
"            mask-image:linear-gradient(90deg,transparent,#000 12%,#000 88%,transparent)}",
"band mask wider fade")
# B-track gap
rep_exact(
"  .band-track{display:flex;gap:16px;padding:0 8px;width:max-content;will-change:transform}",
"  .band-track{display:flex;gap:22px;padding:0 10px;width:max-content;will-change:transform}",
"band track gap")
# B-card radius + shadow
rep_exact(
"  .band-card{width:212px;height:140px;flex:0 0 auto;border-radius:14px;background:#222 center/cover no-repeat;\n"
"    box-shadow:0 10px 30px rgba(0,0,0,.35);border:1px solid rgba(255,255,255,.16)}",
"  .band-card{width:212px;height:140px;flex:0 0 auto;border-radius:16px;background:#222 center/cover no-repeat;\n"
"    box-shadow:0 14px 40px rgba(0,0,0,.38);border:1px solid rgba(255,255,255,.18)}",
"band card radius/shadow")
# B-stack 略放大增强存在感
rep_exact(
"  .band-stack{position:relative;width:248px;height:286px;pointer-events:auto;cursor:pointer}",
"  .band-stack{position:relative;width:262px;height:300px;pointer-events:auto;cursor:pointer}",
"band stack bigger")

print("== C. 全客户端呼吸感 ==")
# C-section padding
rep_exact(
"  section{max-width:1180px;margin:0 auto;padding:54px 20px}",
"  section{max-width:1240px;margin:0 auto;padding:clamp(72px,8vw,112px) 24px}",
"section padding")
# C-sec-head
rep_exact(
"  .sec-head{margin-bottom:26px}",
"  .sec-head{margin-bottom:44px}",
"sec-head margin")
rep_exact(
"  .sec-kicker{font-size:13px;letter-spacing:3px;color:var(--gold);text-transform:uppercase;font-weight:600}",
"  .sec-kicker{font-size:13px;letter-spacing:4px;color:var(--gold);text-transform:uppercase;font-weight:600;margin-bottom:2px}",
"sec-kicker tracking")
rep_exact(
"  .sec-title{font-size:28px;font-weight:700;margin-top:6px;letter-spacing:.5px}",
"  .sec-title{font-size:clamp(28px,4vw,38px);font-weight:700;margin-top:8px;letter-spacing:-.5px;line-height:1.12}",
"sec-title scale + tight tracking")
rep_exact(
"  .sec-sub{color:var(--muted);margin-top:8px;max-width:680px}",
"  .sec-sub{color:var(--muted);margin-top:12px;max-width:680px;font-size:15.5px;line-height:1.7}",
"sec-sub airy")
# C-gallery
rep_exact(
"  .gallery{columns:4 220px;column-gap:14px}",
"  .gallery{columns:4 240px;column-gap:26px}",
"gallery gap")
rep_exact(
"  .g-item{break-inside:avoid;margin-bottom:14px;border-radius:14px;overflow:hidden;position:relative;",
"  .g-item{break-inside:avoid;margin-bottom:26px;border-radius:14px;overflow:hidden;position:relative;",
"g-item margin")
# C-nav
rep_exact(
"  .nav{display:flex;gap:6px;margin-left:auto;flex-wrap:wrap}",
"  .nav{display:flex;gap:10px;margin-left:auto;flex-wrap:wrap}",
"nav gap")
rep_exact(
"    padding:8px 14px;border-radius:999px;font-size:14px;color:var(--muted);",
"    padding:9px 15px;border-radius:999px;font-size:14px;color:var(--muted);",
"nav a padding")

print("== D. 危险 token 扫描 ==")
danger = ["renderSeries", "seriesGrid", "SERIES_BAND", "seriesConsistency",
          "getElementById('chartSeries')", "getElementById('seriesLegend')",
          "id=\"series\"", "href=\"#series\"", "data-sec=\"series\"",
          "Object.keys(SERIES)", "'系列'"]
leftover = {}
for d in danger:
    c = lf.count(d)
    if c:
        leftover[d] = c
if leftover:
    print("  仍存在危险 token：", leftover)
    fails.append("danger tokens remain: " + str(leftover))
else:
    print("  全部危险 token 已清除 ✓")

if fails:
    print("\n❌ 失败条目：")
    for f in fails:
        print("   -", f)
    sys.exit(1)

# 写回 CRLF
save(lf.replace("\n", "\r\n"))
print("\n✅ 全部编辑成功，已写回 CRLF。")

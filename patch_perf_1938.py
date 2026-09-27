# -*- coding: utf-8 -*-
# 1.9.38 性能：地图/图表按视口可见性重渲染 + 签名守卫（解决图库/足迹卡顿）
# 根因：单页长滚动，refreshAll 末尾的 scheduleHeavyRenders 无论当前在哪个区段，
#       都整体重建 SVG 世界地图（233 省+国界+标记 ~数十 ms）与图表；
#       用户在图库时后台仍反复重绘，抢占主线程导致全局掉帧。
# 修法：用 .main-col 滚动容器的 IntersectionObserver 判定 #map/#log 是否接近视野，
#       只有接近视野才构建地图/图表；两者再加「输入未变则跳过重建」的签名守卫。
# 安全范式：读全量 -> old 断言 count==1 -> 替换 -> 写回 -> 复核不变量
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

repls = []

repls.append((
"function scheduleHeavyRenders(){\n"
"  if(_heavyTimer){ clearTimeout(_heavyTimer); _heavyTimer = null; }\n"
"  const run = () => { _heavyTimer = null; renderMap(); renderStats(); };\n"
"  if((state.mapLevel || 0) > 0 || state.region){ run(); return; }\n"
"  _heavyTimer = setTimeout(run, 40);   // 用定时器而非 requestIdleCallback：内嵌浏览器空闲调度不可靠会让地图迟迟不渲染\n"
"}",
"let _mapNear=false, _logNear=false, _secObsReady=false;\n"
"function _setupSectionObservers(){\n"
"  if(_secObsReady) return; _secObsReady=true;\n"
"  if(typeof IntersectionObserver==='undefined') return;\n"
"  const root=document.querySelector('.main-col')||null;\n"
"  const opts=root?{root:root,rootMargin:'300px',threshold:0.01}:{rootMargin:'300px',threshold:0.01};\n"
"  const obs=new IntersectionObserver(es=>{\n"
"    es.forEach(e=>{\n"
"      if(e.target.id==='map'){ _mapNear=e.isIntersecting; if(_mapNear) scheduleHeavyRenders(); }\n"
"      else if(e.target.id==='log'){ _logNear=e.isIntersecting; if(_logNear) scheduleHeavyRenders(); }\n"
"    });\n"
"  },opts);\n"
"  const m=document.getElementById('map'), l=document.getElementById('log');\n"
"  if(m) obs.observe(m);\n"
"  if(l) obs.observe(l);\n"
"}\n"
"function scheduleHeavyRenders(){\n"
"  _setupSectionObservers();\n"
"  if(_heavyTimer){ clearTimeout(_heavyTimer); _heavyTimer=null; }\n"
"  const forceMap=(state.mapLevel>0)||!!state.region;   // 下钻态必须渲染（即使视野判定未近）\n"
"  if(!_mapNear && !_logNear && !forceMap) return;       // 两块都不在视野 -> 跳过整体重绘（足迹/图库卡顿主因）\n"
"  const run=()=>{\n"
"    _heavyTimer=null;\n"
"    if(_mapNear||forceMap) renderMapGuarded();\n"
"    if(_logNear) renderStatsGuarded();\n"
"  };\n"
"  if(forceMap){ run(); return; }\n"
"  _heavyTimer=setTimeout(run,40);   // 用定时器而非 requestIdleCallback：内嵌浏览器空闲调度不可靠会让地图迟迟不渲染\n"
"}\n"
"function renderMapGuarded(){\n"
"  const m=document.getElementById('map');\n"
"  const sig=(state.mapLevel||0)+'|'+(state.region||'')+'|'+(state.prov||'')+'|'+((PHOTOS||[]).length)+'|'+(m?m.clientWidth:0);\n"
"  if(state._mapSig===sig) return;     // 输入未变且已渲染过 -> 跳过整张 SVG 世界重建\n"
"  state._mapSig=sig;\n"
"  renderMap();\n"
"}\n"
"function renderStatsGuarded(){\n"
"  const sig=((PHOTOS||[]).length)+'|'+((PHOTOS||[]).filter(p=>p.rating).length);\n"
"  if(state._statsSig===sig) return;   // 数据未变 -> 跳过图表重绘\n"
"  state._statsSig=sig;\n"
"  renderStats();\n"
"}",
"#A scheduleHeavyRenders 视口门控 + 签名守卫"))

for old, new, label in repls:
    c = data.count(old)
    if c != 1:
        print("FAIL [%s] count=%d (expected 1)" % (label, c))
        sys.exit(1)
    data = data.replace(old, new, 1)
    print("OK   [%s]" % label)

with io.open(PATH, "w", encoding="utf-8") as f:
    f.write(data)

# ---- 复核不变量 ----
checks = {
    "renderMapGuarded 定义": data.count("function renderMapGuarded()"),
    "renderStatsGuarded 定义": data.count("function renderStatsGuarded()"),
    "_setupSectionObservers 定义": data.count("function _setupSectionObservers()"),
    "_mapNear 声明": data.count("let _mapNear=false"),
    "旧直接 renderMap(); renderStats(); 已删": data.count("renderMap(); renderStats();"),
    "#map 接近视野触发渲染": data.count("if(e.target.id==='map'){ _mapNear=e.isIntersecting"),
    "签名守卫 state._mapSig": data.count("if(state._mapSig===sig) return;"),
    "签名守卫 state._statsSig": data.count("if(state._statsSig===sig) return;"),
}
allok = True
for k, v in checks.items():
    ok = (v >= 1)
    if not ok: allok = False
    print(("OK  " if ok else "CHK ") + "%-48s %d" % (k, v))
print("WROTE %d bytes" % len(data))
sys.exit(0 if allok else 1)

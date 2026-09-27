# -*- coding: utf-8 -*-
# 修复「地图不显示 / 照片位置不识别」：
#  1) 陆地配色在深色地图底上不可见 -> 提亮 .cty 陆地色（两主题 + 下钻层级）
#  2) renderMap 先画国界再读照片，且对 PHOTOS 做空守卫（照片数据异常也不致地图空白）
#  3) scheduleHeavyRenders 用确定性 setTimeout 取代 requestIdleCallback（内嵌浏览器空闲调度不可靠）
#  4) renderPhotoMarkers 在世界层级也渲染紧凑光点（按真实经纬度，有 GPS 即精确落点）
# 全程 CRLF、零外链；改动集中在地图函数与 CSS，不影响客户端 3 处专属增强。
PATH = 'index.html'
raw = open(PATH, 'rb').read()
text = raw.decode('utf-8').replace('\r\n', '\n')

def rep(o, n, label):
    global text
    c = text.count(o)
    if c != 1:
        raise SystemExit('[%s] 期望命中 1 次，实际 %d 次' % (label, c))
    text = text.replace(o, n)
    print('OK  ', label)

# —— 1) 陆地可见性（核心「地图不显示」修复）——
rep('.cty{fill:#1c2733;stroke:#33465a;',
    '.cty{fill:#2f4658;stroke:#5b86a8;',
    'cty base 陆地提亮')
rep('body.dark .cty{fill:#151f29;stroke:#2b3d50}',
    'body.dark .cty{fill:#2f4658;stroke:#5b86a8}',
    'cty dark 陆地提亮')
rep('#mapZoom[data-lv="1"] .cty{fill:#141d27;stroke:#2b3c4d}',
    '#mapZoom[data-lv="1"] .cty{fill:#1d2e3d;stroke:#3a536b}',
    'cty lv1 陆地提亮')
rep('#mapZoom[data-lv="2"] .cty{fill:#121a22;stroke:#263645}',
    '#mapZoom[data-lv="2"] .cty{fill:#182734;stroke:#32485c}',
    'cty lv2 陆地提亮')

# —— 世界层级照片光点样式 ——
rep('.pm.dim{opacity:.16}',
    '.pm.dim{opacity:.16}\n'
    '.pmw{cursor:pointer;opacity:.95}\n'
    '.pmw-dot{fill:#ffcf6b;stroke:#fff;stroke-width:1;filter:drop-shadow(0 1px 2px rgba(0,0,0,.55));transition:r var(--dur-1)}\n'
    '.pmw:hover .pmw-dot,.pmw:focus-visible .pmw-dot{r:4.8;fill:#ffb43a}',
    'pmw 光点样式')

# —— 2) renderMap：先画国界 + PHOTOS 空守卫 ——
rep("  applyMapZoom();   // 内部会调用 setPhase() 把相位写进 #worldWrap\n"
    "  const lvl=state.mapLevel||0;\n"
    "\n"
    "  const counts = {};\n"
    "  PHOTOS.forEach(p=>counts[p.r]=(counts[p.r]||0)+1);\n"
    "\n"
    "  buildWorld();",
    "  applyMapZoom();   // 内部会调用 setPhase() 把相位写进 #worldWrap\n"
    "  buildWorld();                       // 先画国界：即使照片数据异常，地图也保证可见\n"
    "  const lvl=state.mapLevel||0;\n"
    "\n"
    "  const counts = {};\n"
    "  (PHOTOS||[]).forEach(p=>counts[p.r]=(counts[p.r]||0)+1);",
    'renderMap 重排 buildWorld')

# —— 3) scheduleHeavyRenders：确定性定时器 ——
rep("  if((state.mapLevel || 0) > 0 || state.region){ run(); return; }\n"
    "  if(window.requestIdleCallback) requestIdleCallback(run, { timeout: 600 });\n"
    "  else _heavyTimer = setTimeout(run, 80);",
    "  if((state.mapLevel || 0) > 0 || state.region){ run(); return; }\n"
    "  _heavyTimer = setTimeout(run, 40);   // 用定时器而非 requestIdleCallback：内嵌浏览器空闲调度不可靠会让地图迟迟不渲染",
    'scheduleHeavyRenders 定时器')

# —— 4a) renderPhotoMarkers：世界层级纳入列表 + 空守卫 ——
rep("  const layer=document.getElementById('photoLayer');\n"
    "  const lvl=state.mapLevel||0;\n"
    "  if(lvl===0 || !state.region){ layer.innerHTML=''; return; }\n"
    "  const list = (lvl>=2 && state.prov)\n"
    "    ? PHOTOS.filter(p=>p.r===state.region && p.pv===state.prov)\n"
    "    : PHOTOS.filter(p=>p.r===state.region);\n"
    "  const k=invZ().toFixed(4);",
    "  const layer=document.getElementById('photoLayer');\n"
    "  const lvl=state.mapLevel||0;\n"
    "  const world = (lvl===0 || !state.region);\n"
    "  const list = world ? PHOTOS\n"
    "    : (lvl>=2 && state.prov)\n"
    "      ? PHOTOS.filter(p=>p.r===state.region && p.pv===state.prov)\n"
    "      : PHOTOS.filter(p=>p.r===state.region);\n"
    "  if(!list || !list.length){ layer.innerHTML=''; return; }\n"
    "  const k=invZ().toFixed(4);",
    'renderPhotoMarkers 列表/守卫')

# —— 4b) 循环内：世界层级改画紧凑光点（不加载缩略图，避免上百张同时解码）——
OLD_LOOP = """  for(let i=0;i<list.length;i++){
    const p=list[i], xy=photoXY(p), hue=photoHue(p), fill='hsl('+hue+',58%,58%)';
    const inner = p.img
      ? '<image class="pm-th" href="'+escHtml(p.img)+'" x="-6" y="-6" width="12" height="12" preserveAspectRatio="xMidYMid slice"/>'
      : '<rect class="pm-th" x="-6" y="-6" width="12" height="12" rx="3" fill="'+fill+'"/>';
    html+='<g class="pm" data-id="'+p.id+'" data-x="'+xy[0].toFixed(2)+'" data-y="'+xy[1].toFixed(2)+'" data-k="'+k+'" '+
      'tabindex="0" role="button" aria-label="'+escHtml(p.t)+'\uff08'+escHtml(p.ct||regionName(p.r))+'\uff09">'+
      '<circle cx="0" cy="0" r="11" fill="transparent"/>'+inner+'</g>';"""

NEW_LOOP = """  for(let i=0;i<list.length;i++){
    const p=list[i], xy=photoXY(p);
    if(world){
      // 世界层级：每幅作品一个紧凑光点，按真实经纬度散布；有 GPS 即精确落点（不加载缩略图，避免上百张同时解码）
      html+='<g class="pm pmw" data-id="'+p.id+'" data-x="'+xy[0].toFixed(2)+'" data-y="'+xy[1].toFixed(2)+'" data-k="1" '+
        'tabindex="0" role="button" aria-label="'+escHtml(p.t)+'\uff08'+escHtml(p.ct||regionName(p.r))+'\uff09">'+
        '<circle class="pmw-dot" cx="0" cy="0" r="3.4"/></g>';
      continue;
    }
    const hue=photoHue(p), fill='hsl('+hue+',58%,58%)';
    const inner = p.img
      ? '<image class="pm-th" href="'+escHtml(p.img)+'" x="-6" y="-6" width="12" height="12" preserveAspectRatio="xMidYMid slice"/>'
      : '<rect class="pm-th" x="-6" y="-6" width="12" height="12" rx="3" fill="'+fill+'"/>';
    html+='<g class="pm" data-id="'+p.id+'" data-x="'+xy[0].toFixed(2)+'" data-y="'+xy[1].toFixed(2)+'" data-k="'+k+'" '+
      'tabindex="0" role="button" aria-label="'+escHtml(p.t)+'\uff08'+escHtml(p.ct||regionName(p.r))+'\uff09">'+
      '<circle cx="0" cy="0" r="11" fill="transparent"/>'+inner+'</g>';"""
rep(OLD_LOOP, NEW_LOOP, 'renderPhotoMarkers 世界光点分支')

# 写回（恢复 CRLF）
out = text.replace('\n', '\r\n')
open(PATH, 'wb').write(out.encode('utf-8'))
print('WROTE', PATH, len(out), 'bytes')

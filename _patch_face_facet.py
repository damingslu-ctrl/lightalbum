# -*- coding: utf-8 -*-
"""E 能力（人物识别）前端收尾：在 index.html 增加「人物」facet 行 + 识别按钮/进度条。
按项目铁律：大文件必须用 Python 脚本补丁（Edit 会静默不落盘）；两处 index.html 必须逐字节一致。
"""
import io, sys, subprocess

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
FILES = [
    ROOT + r'\index.html',
    ROOT + r'\client\resources\index.html',
]

# ---------- 1. 新 CSS（插在第一个 </style> 之前，位于 fabric-lib 之前，满足守卫） ----------
CSS_ANCHOR = (
    "  @media(max-width:768px){ .fx-label{width:auto;text-align:left} "
    ".fx-group{flex-direction:column;align-items:flex-start;gap:4px} }\n"
    "</style>"
)
CSS_NEW = (
    "  @media(max-width:768px){ .fx-label{width:auto;text-align:left} "
    ".fx-group{flex-direction:column;align-items:flex-start;gap:4px} }\n"
    "  /* 人物识别（v1.9.17）*/\n"
    "  .fx-scan{margin-left:8px}\n"
    "  .fx-scan[disabled]{opacity:.5;cursor:default}\n"
    "  .fx-scan-state{font-size:12px;color:var(--muted);margin-left:4px}\n"
    "  .fx-scan-state.running{color:var(--gold)}\n"
    "  .fx-scan-state.ok{color:var(--ok,#3aa76d)}\n"
    "  .fx-scan-state.err{color:var(--danger,#d9534f)}\n"
    "  .fx-progress{display:none;align-items:center;gap:10px;margin-top:8px;padding:8px 12px;border:1px solid var(--line);border-radius:var(--r);background:var(--card)}\n"
    "  .fx-progress-text{font-size:12px;color:var(--muted);white-space:nowrap}\n"
    "  .fx-progress-track{flex:1;height:6px;border-radius:999px;background:var(--line);overflow:hidden}\n"
    "  .fx-progress-fill{height:100%;width:0;background:linear-gradient(90deg,var(--gold),var(--accent,#5b8def));transition:width var(--dur-2) var(--ease-std)}\n"
    "  .fx-group-person .fx-label{color:var(--gold)}\n"
    "  body.dark .fx-scan-state.ok{color:#5fcf8e}\n"
    "  body.dark .fx-scan-state.err{color:#ff7b72}\n"
    "</style>"
)

# ---------- 2. state.fx 增加 person ----------
FX_STATE_OLD = "fx:{ cam:[], lens:[], ap:[], iso:[], focal:[] },"
FX_STATE_NEW = "fx:{ cam:[], lens:[], ap:[], iso:[], focal:[], person:[] },"

# ---------- 3. HTML：fx-bar 增加识别按钮 + 状态 + 进度条 ----------
FXBAR_OLD = (
    '      <button class="btn sm fx-clear" id="fxClear" type="button" style="display:none">清空筛选</button>\n'
    '    </div>'
)
FXBAR_NEW = (
    '      <button class="btn sm fx-clear" id="fxClear" type="button" style="display:none">清空筛选</button>\n'
    '      <button class="btn sm fx-scan" id="fxScan" type="button" style="display:none">识别人物</button>\n'
    '      <span class="fx-scan-state" id="fxScanState"></span>\n'
    '    </div>\n'
    '    <div class="fx-progress" id="fxProgress" style="display:none">\n'
    '      <span class="fx-progress-text" id="fxProgressText"></span>\n'
    '      <div class="fx-progress-track"><div class="fx-progress-fill" id="fxProgressFill"></div></div>\n'
    '      <button class="btn xs fx-abort" id="fxAbort" type="button">停止</button>\n'
    '    </div>'
)

# ---------- 4. HTML：fx-body 增加人物 group ----------
FXBODY_OLD = (
    '      <div class="fx-group"><div class="fx-label">焊距</div><div class="fx-row" id="fxFocal"></div></div>\n'
    '    </div>'
)
FXBODY_NEW = (
    '      <div class="fx-group"><div class="fx-label">焊距</div><div class="fx-row" id="fxFocal"></div></div>\n'
    '      <div class="fx-group fx-group-person"><div class="fx-label">人物</div><div class="fx-row" id="fxPerson"></div></div>\n'
    '    </div>'
)

# ---------- 5. filteredPhotos：增加 person 过滤（组内 OR） ----------
FILT_OLD = "    if(state.fx.focal.length && !state.fx.focal.includes(focalBucketOf(p.focal))) return false;"
FILT_NEW = (
    "    if(state.fx.focal.length && !state.fx.focal.includes(focalBucketOf(p.focal))) return false;\n"
    "    // 人物筛选（多选自组内 OR；p.person 为逗号分隔的「人物N」串）\n"
    "    if(state.fx.person.length){\n"
    "      const pnames=(p.person||'').split(',').map(s=>s.trim()).filter(Boolean);\n"
    "      if(!state.fx.person.some(n=>pnames.includes(n))) return false;\n"
    "    }"
)

# ---------- 6a. renderFacets：focal 行后补人物行 ----------
RF_FOCAL_OLD = "  _fxRow('fxFocal', focVals, state.fx.focal);\n  // 折叠 / 计数 / 清空"
RF_FOCAL_NEW = (
    "  _fxRow('fxFocal', focVals, state.fx.focal);\n"
    "  // 人物（数据来自 /api/persons，异步回填）\n"
    "  refreshPersonFacet();\n"
    "  // 折叠 / 计数 / 清空"
)

# ---------- 6b. renderFacets：nActive 增加 person ----------
NA_OLD = "  const nActive = state.year.length + state.fx.cam.length + state.fx.lens.length + state.fx.ap.length + state.fx.iso.length + state.fx.focal.length;"
NA_NEW = "  const nActive = state.year.length + state.fx.cam.length + state.fx.lens.length + state.fx.ap.length + state.fx.iso.length + state.fx.focal.length + state.fx.person.length;"

# ---------- 6c. renderFacets：清空 + 末端接 wireFaceScan/loadPersons ----------
RF_END_OLD = (
    "    if(clr){ clr.addEventListener('click',()=>{ state.year=[]; state.fx.cam=[]; state.fx.lens=[]; state.fx.ap=[]; state.fx.iso=[]; state.fx.focal=[]; refreshAll(); }); }\n"
    "  }\n"
    "}\n"
    "function renderOrients(){"
)
RF_END_NEW = (
    "    if(clr){ clr.addEventListener('click',()=>{ state.year=[]; state.fx.cam=[]; state.fx.lens=[]; state.fx.ap=[]; state.fx.iso=[]; state.fx.focal=[]; state.fx.person=[]; refreshAll(); }); }\n"
    "    wireFaceScan();\n"
    "  }\n"
    "  loadPersons();\n"
    "}\n"
    "function renderOrients(){"
)

# ---------- 7. 新增 JS 函数块（插在 renderFacets 之前） ----------
RF_ANCHOR = (
    "// EXIF 器材 / 参数筛选条（v1.9.16）：年份 + 机身 + 镜头 + 光圈 + ISO + 焦距\n"
    "function renderFacets(){"
)
RF_BLOCK = (
    "// ---- 人物识别（v1.9.17）：筛选 facet + 识别按钮/进度 ----\n"
    "let _personStats = [];\n"
    "let _facePolling = 0;\n"
    "function refreshPersonFacet(){ _fxRow('fxPerson', _personStats, state.fx.person); }\n"
    "async function loadPersons(){\n"
    "  try{\n"
    "    const r = await fetch('/api/persons'); if(!r.ok) return;\n"
    "    const d = await r.json();\n"
    "    const stats = d.stats || {};\n"
    "    _personStats = Object.entries(stats).map(([k,c])=>({k,c})).sort((a,b)=>b.c-a.c);\n"
    "    refreshPersonFacet();\n"
    "    const face = d.face || {};\n"
    "    const scanBtn = document.getElementById('fxScan');\n"
    "    if(scanBtn){ scanBtn.style.display = (face.ok && face.loaded) ? '' : 'none'; }\n"
    "    setFaceScanState(d.status || {});\n"
    "  }catch(e){ /* 服务未就绪：静默 */ }\n"
    "}\n"
    "function setFaceScanState(st){\n"
    "  const el = document.getElementById('fxScanState');\n"
    "  if(!el) return;\n"
    "  if(st.running){ el.textContent = '识别中 '+(st.done||0)+'/'+(st.total||0); el.className='fx-scan-state running'; }\n"
    "  else if(st.last_error){ el.textContent = '上次失败：'+(st.last_error||''); el.className='fx-scan-state err'; }\n"
    "  else if(st.last_time){ el.textContent = '已识别 '+(st.persons||0)+' 人 / '+(st.faces||0)+' 张脸'; el.className='fx-scan-state ok'; }\n"
    "  else { el.textContent=''; el.className='fx-scan-state'; }\n"
    "}\n"
    "async function startFaceScan(){\n"
    "  const btn=document.getElementById('fxScan'); if(btn) btn.disabled=true;\n"
    "  try{\n"
    "    const r=await fetch('/api/face/scan',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});\n"
    "    if(r.status===409){ if(btn) btn.disabled=false; toast('已有识别任务在进行'); return; }\n"
    "    if(!r.ok){ if(btn) btn.disabled=false; toast('识别启动失败'); return; }\n"
    "  }catch(e){ if(btn) btn.disabled=false; toast('人脸服务不可用'); return; }\n"
    "  pollFaceScan();\n"
    "}\n"
    "function pollFaceScan(){\n"
    "  if(_facePolling) clearInterval(_facePolling);\n"
    "  const box=document.getElementById('fxProgress');\n"
    "  const txt=document.getElementById('fxProgressText');\n"
    "  const fill=document.getElementById('fxProgressFill');\n"
    "  const btn=document.getElementById('fxScan');\n"
    "  if(box) box.style.display='';\n"
    "  _facePolling=setInterval(async ()=>{\n"
    "    try{\n"
    "      const r=await fetch('/api/face/status'); if(!r.ok) return;\n"
    "      const st=await r.json();\n"
    "      setFaceScanState(st);\n"
    "      if(txt) txt.textContent = '识别中 '+(st.done||0)+'/'+(st.total||0);\n"
    "      if(fill && st.total){ fill.style.width = Math.round(100*(st.done||0)/(st.total||1))+'%'; }\n"
    "      if(!st.running){\n"
    "        if(_facePolling){ clearInterval(_facePolling); _facePolling=0; }\n"
    "        if(box) setTimeout(()=>{ box.style.display='none'; }, 800);\n"
    "        if(btn) btn.disabled=false;\n"
    "        if(st.ok!==false && !st.last_error){\n"
    "          try{ const pr=await fetch('/api/persons'); const pd=await pr.json(); const stats=pd.stats||{}; _personStats=Object.entries(stats).map(([k,c])=>({k,c})).sort((a,b)=>b.c-a.c); refreshPersonFacet(); }catch(e){}\n"
    "          refreshAll();\n"
    "        }\n"
    "      }\n"
    "    }catch(e){ /* 轮询中断：保留进度条，下次重连 */ }\n"
    "  }, 600);\n"
    "}\n"
    "function wireFaceScan(){\n"
    "  const btn=document.getElementById('fxScan');\n"
    "  if(btn) btn.addEventListener('click', startFaceScan);\n"
    "  const abort=document.getElementById('fxAbort');\n"
    "  if(abort) abort.addEventListener('click', async ()=>{ try{ await fetch('/api/face/abort',{method:'POST'}); }catch(e){} });\n"
    "}\n"
    "\n"
    "// EXIF 器材 / 参数筛选条（v1.9.16）：年份 + 机身 + 镜头 + 光圈 + ISO + 焦距\n"
    "function renderFacets(){"
)

PATCHES = [
    ("CSS", CSS_ANCHOR, CSS_NEW),
    ("fx_state", FX_STATE_OLD, FX_STATE_NEW),
    ("fxbar", FXBAR_OLD, FXBAR_NEW),
    ("fxbody", FXBODY_OLD, FXBODY_NEW),
    ("filt", FILT_OLD, FILT_NEW),
    ("rf_focal", RF_FOCAL_OLD, RF_FOCAL_NEW),
    ("nactive", NA_OLD, NA_NEW),
    ("rf_end", RF_END_OLD, RF_END_NEW),
    ("rf_block", RF_ANCHOR, RF_BLOCK),
]

def patch_one(path):
    with io.open(path, 'r', encoding='utf-8') as f:
        data = f.read()
    for name, old, new in PATCHES:
        c = data.count(old)
        if c != 1:
            raise SystemExit("FAIL [%s] %s: count=%d (expect 1)" % (path, name, c))
        data = data.replace(old, new, 1)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(data)
    print("patched:", path)

for fp in FILES:
    patch_one(fp)

# 同步两处：显式断言逐字节一致（双页同步铁律）
import filecmp
if not filecmp.cmp(FILES[0], FILES[1], shallow=False):
    raise SystemExit("FAIL: 两处 index.html 不一致")
print("OK: 两处 index.html 逐字节一致")
print("DONE")

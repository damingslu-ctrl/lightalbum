# -*- coding: utf-8 -*-
"""E 前端补丁 2：扫描完成后重新拉取 PHOTOS（带上新 person 字段）再刷新。
两处 index.html 同步，断言 count==1 与逐字节一致。
"""
import io, filecmp

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
FILES = [ROOT + r'\index.html', ROOT + r'\client\resources\index.html']

OLD = (
    "        if(st.ok!==false && !st.last_error){\n"
    "          try{ const pr=await fetch('/api/persons'); const pd=await pr.json(); const stats=pd.stats||{}; _personStats=Object.entries(stats).map(([k,c])=>({k,c})).sort((a,b)=>b.c-a.c); refreshPersonFacet(); }catch(e){}\n"
    "          refreshAll();\n"
    "        }"
)
NEW = (
    "        if(st.ok!==false && !st.last_error){\n"
    "          // 识别写回的是服务端库；先重新拉取 PHOTOS（带上新 person 字段）再刷新\n"
    "          try{ await loadLibraryPhotos(); }catch(e){}\n"
    "          try{ const pr=await fetch('/api/persons'); const pd=await pr.json(); const stats=pd.stats||{}; _personStats=Object.entries(stats).map(([k,c])=>({k,c})).sort((a,b)=>b.c-a.c); refreshPersonFacet(); }catch(e){}\n"
    "          refreshAll();\n"
    "        }"
)

for fp in FILES:
    with io.open(fp, 'r', encoding='utf-8') as f:
        data = f.read()
    c = data.count(OLD)
    if c != 1:
        raise SystemExit("FAIL %s: count=%d" % (fp, c))
    data = data.replace(OLD, NEW, 1)
    with io.open(fp, 'w', encoding='utf-8') as f:
        f.write(data)
    print("patched:", fp)

if not filecmp.cmp(FILES[0], FILES[1], shallow=False):
    raise SystemExit("FAIL: 两处不一致")
print("OK 一致")
print("DONE")

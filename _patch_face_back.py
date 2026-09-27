# -*- coding: utf-8 -*-
"""E 后端收尾：
  1) server.py /api/persons 的 stats 由「列表」转成「{name:count} 字典」，
     以匹配前端 loadPersons 的 Object.entries(stats) 用法。
  2) run_all_tests.py 注册 'face' 套件（step_face + STEPS）。
均用 Python 补丁（count==1 断言），避免 Edit 静默不落盘。
"""
import io

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'

SERVER = ROOT + r'\client\lightalbum\core\server.py'
RUN = ROOT + r'\run_all_tests.py'

# ---------- 1) server.py ----------
S_OLD = (
    "        elif path == '/api/persons':\n"
    "            self._send(200, {'stats': self.lib.person_stats(),\n"
    "                             'face': self.face.probe(),\n"
    "                             'status': dict(self.face.status)})\n"
)
S_NEW = (
    "        elif path == '/api/persons':\n"
    "            # person_stats() 返回 [{'person','count'},...] 列表；前端用 Object.entries\n"
    "            # 需要 {name: count} 字典，这里转换一次（保持 person_stats 自身签名不变）。\n"
    "            ps = {r['person']: r['count'] for r in self.lib.person_stats()}\n"
    "            self._send(200, {'stats': ps,\n"
    "                             'face': self.face.probe(),\n"
    "                             'status': dict(self.face.status)})\n"
)

# ---------- 2) run_all_tests.py ----------
R1_OLD = (
    "    return True, 'pass=%d fail=%d' % (p, f)\n"
    "\n"
    "\n"
    "def step_culling():"
)
R1_NEW = (
    "    return True, 'pass=%d fail=%d' % (p, f)\n"
    "\n"
    "\n"
    "def step_face():\n"
    "    rc, out = _run([VENV_PY, 'test_face.py'], CLIENT)\n"
    "    pr = _parse_pair(out, 'FACE_TEST')\n"
    "    if pr is None:\n"
    "        return False, '未捕获 FACE_TEST 汇总行（exit=%d）' % rc\n"
    "    p, f = pr\n"
    "    if rc != 0 or f != 0:\n"
    "        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)\n"
    "    return True, 'pass=%d fail=%d' % (p, f)\n"
    "\n"
    "\n"
    "def step_culling():"
)
R2_OLD = "    'facets':   ('EXIF 器材筛选条', step_facets),"
R2_NEW = (
    "    'facets':   ('EXIF 器材筛选条', step_facets),\n"
    "    'face':     ('人物识别（聚类/库列/前端钩子）', step_face),"
)

PATCHES = [('server', SERVER, S_OLD, S_NEW),
           ('run_r1', RUN, R1_OLD, R1_NEW),
           ('run_r2', RUN, R2_OLD, R2_NEW)]

for name, path, old, new in PATCHES:
    with io.open(path, 'r', encoding='utf-8') as f:
        data = f.read()
    c = data.count(old)
    if c != 1:
        raise SystemExit("FAIL [%s] %s: count=%d" % (name, path, c))
    data = data.replace(old, new, 1)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(data)
    print("patched:", name)

print("DONE")

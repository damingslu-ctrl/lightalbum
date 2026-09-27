# -*- coding: utf-8 -*-
"""真机验证：darktable-cli 批量渲染全流程 + scanner 跳过规则（一次性脚本）。"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, r'D:\Test\workbuddy\2026-09-18-09-00-13\client')

from PIL import Image                                              # noqa: E402
from lightalbum.core.engines.headless_engine import HeadlessRenderEngine  # noqa: E402
from lightalbum.core.engines.develop_engine import _RAW_EXTS       # noqa: E402
from lightalbum.core.scanner import scan_dir                       # noqa: E402

tmp = tempfile.mkdtemp(prefix='dt_batch_')

# ---- 造 3 张「原片」（模拟 act_headlessRender 的同目录收集）----
names = ['IMG_001.jpg', 'IMG_002.jpg', 'IMG_003.jpg']
for i, n in enumerate(names):
    Image.new('RGB', (400, 400), (90 + i * 40, 90, 60)).save(
        os.path.join(tmp, n), 'JPEG', quality=92)

# 模拟用户在 darktable 里编辑过 IMG_001 → 侧车存在
with open(os.path.join(tmp, 'IMG_001.jpg.xmp'), 'w', encoding='ascii') as fh:
    fh.write('<x:xmpmeta xmlns:x="adobe:ns:meta/"/>\n')

# ---- 引擎探测（应自动发现 D:\darktable\bin\darktable-cli.exe）----
eng = HeadlessRenderEngine()
eng.init()
probe = eng.task_probe()
print('PROBE darktable:', probe['darktable'])
assert probe['darktable']['available'], 'darktable-cli 未被自动探测到'

# ---- 模拟 act_headlessRender 的收集与渲染循环 ----
folder = tmp
sidecar = os.path.join(folder, 'IMG_001.jpg.xmp')
sidecar = sidecar if os.path.isfile(sidecar) else None
exts = {'.jpg', '.jpeg', '.png', '.tif', '.tiff'} | set(_RAW_EXTS)
files = []
src_one = os.path.join(folder, 'IMG_001.jpg')
for name in sorted(os.listdir(folder)):
    fp = os.path.join(folder, name)
    if not os.path.isfile(fp):
        continue
    if os.path.splitext(name)[1].lower() not in exts:
        continue
    if os.path.abspath(fp) == os.path.abspath(src_one):
        continue
    files.append(fp)
print('COLLECT', len(files), 'files; sidecar:', bool(sidecar))

out_dir = os.path.join(folder, '_develop')
os.makedirs(out_dir, exist_ok=True)
oks, errs = [], []
for f in files:
    stem, _e = os.path.splitext(os.path.basename(f))
    out = os.path.join(out_dir, stem + '-develop.jpg')
    try:
        res = eng.run('render', (f, out), {'sidecar': sidecar})
        oks.append((f, out))
        print('  OK', os.path.basename(f), '->', res['size'], 'bytes')
    except Exception as e:  # noqa: BLE001
        errs.append('%s: %s' % (os.path.basename(f), e))
        print('  ERR', os.path.basename(f), e)

print('BATCH_RESULT ok=%d fail=%d' % (len(oks), len(errs)))

# ---- scanner 跳过验证：_develop/_external_edit 不应被扫进来 ----
os.makedirs(os.path.join(tmp, '_external_edit', 'x', 'output'), exist_ok=True)
Image.new('RGB', (32, 32), (10, 10, 10)).save(
    os.path.join(tmp, '_external_edit', 'x', 'output', 'x-edit.jpg'), 'JPEG')
found = [p for root, _s, fs in os.walk(tmp) for p in
         [os.path.join(root, f) for f in fs if f.lower().endswith('.jpg')]]
scanned = scan_dir(tmp, thumbs=False) if 'thumbs' in scan_dir.__code__.co_varnames \
    else scan_dir(tmp)
rels = [r.get('rel') for r in (scanned[0] if isinstance(scanned, tuple)
                              else scanned)]
bad = [r for r in rels if r and ('_develop' in r or '_external_edit' in r)]
print('SCANNER rels=%r' % rels[:12])
print('SCANNER_SKIP_OK' if not bad else 'SCANNER_LEAK %r' % bad)

shutil.rmtree(tmp, ignore_errors=True)

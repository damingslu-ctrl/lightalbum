# -*- coding: utf-8 -*-
"""真机验证 v2：darktable-cli 批量渲染 + scanner 跳过（一次性脚本）。"""
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

names = ['IMG_001.jpg', 'IMG_002.jpg', 'IMG_003.jpg']
for i, n in enumerate(names):
    Image.new('RGB', (400, 400), (90 + i * 40, 90, 60)).save(
        os.path.join(tmp, n), 'JPEG', quality=92)
with open(os.path.join(tmp, 'IMG_001.jpg.xmp'), 'w', encoding='ascii') as fh:
    fh.write('<x:xmpmeta xmlns:x="adobe:ns:meta/"/>\n')

eng = HeadlessRenderEngine()
eng.init()
probe = eng.task_probe()
print('PROBE darktable:', probe['darktable'])
assert probe['darktable']['available'], 'darktable-cli 未被自动探测到'

sidecar = os.path.join(tmp, 'IMG_001.jpg.xmp')
sidecar = sidecar if os.path.isfile(sidecar) else None
exts = {'.jpg', '.jpeg', '.png', '.tif', '.tiff'} | set(_RAW_EXTS)
src_one = os.path.join(tmp, 'IMG_001.jpg')
files = []
for name in sorted(os.listdir(tmp)):
    fp = os.path.join(tmp, name)
    if os.path.isfile(fp) and os.path.splitext(name)[1].lower() in exts \
            and os.path.abspath(fp) != os.path.abspath(src_one):
        files.append(fp)
print('COLLECT', len(files), 'files; sidecar:', bool(sidecar))

out_dir = os.path.join(tmp, '_develop')
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

# scanner 跳过验证：产物目录不该被扫成照片
os.makedirs(os.path.join(tmp, '_external_edit', 'x', 'output'), exist_ok=True)
Image.new('RGB', (32, 32), (10, 10, 10)).save(
    os.path.join(tmp, '_external_edit', 'x', 'output', 'x-edit.jpg'), 'JPEG')
scanned = scan_dir(tmp)
blob = repr(scanned)
leak = ('_develop' in blob) or ('_external_edit' in blob)
ok_cnt = str(len(oks))
print('SCANNER:', 'LEAK!' if leak else 'SKIP_OK',
      '| 扫到条目含 IMG_003develop =', ('develop' in blob.lower()))
shutil.rmtree(tmp, ignore_errors=True)
print('SMOKE_DONE ok=%s' % ok_cnt)

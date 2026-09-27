# -*- coding: utf-8 -*-
"""测量冷启动基线：带 LIGHTALBUM_TIMING=1 起真实客户端（offscreen），
跑够时间后杀掉，读回 %TEMP%/lightalbum_startup.txt 的里程碑耗时。

用法：client/venv/Scripts/python.exe measure_startup.py [秒数]
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
CLIENT = os.path.join(ROOT, 'client')
WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0

tf = os.path.join(tempfile.gettempdir(), 'lightalbum_startup.txt')
if os.path.exists(tf):
    os.remove(tf)

env = dict(os.environ)
env['LIGHTALBUM_TIMING'] = '1'
env['QT_QPA_PLATFORM'] = 'offscreen'

p = subprocess.Popen([sys.executable, 'run.py'], cwd=CLIENT, env=env,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
try:
    p.wait(timeout=WAIT)
    print('EXITED early rc=%s' % p.returncode)
except subprocess.TimeoutExpired:
    p.kill()
    try:
        p.wait(timeout=5)
    except Exception:
        pass
    print('KILLED after %.0fs (预期：app.exec() 常驻)' % WAIT)

time.sleep(0.5)
if not os.path.exists(tf):
    print('NO_TIMING_FILE', tf)
    sys.exit(1)

rows = []
for line in open(tf, encoding='utf-8'):
    line = line.strip()
    if not line:
        continue
    name, ms = line.split('\t')
    rows.append((name, float(ms)))

print('\n=== 启动里程碑（ms，相对 script_start）===')
prev = 0.0
for name, ms in rows:
    print('  %-22s %8.0f ms   (+%.0f)' % (name, ms, ms - prev))
    prev = ms
total = rows[-1][1] if rows else 0
print('  %-22s %8.0f ms' % ('TOTAL(到最后里程碑)', total))
print('MEASURE_DONE')

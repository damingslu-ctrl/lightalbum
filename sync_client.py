# -*- coding: utf-8 -*-
"""根页 -> 客户端页 同步（1.8.5 起：两份彻底一致）。

历史背景
--------
以前客户端页 = 根页 + 3 处「本地库增强」，由本脚本在复制后注入：
  A 进本地库模式先清空内存 PHOTOS
  B 端口变了就断开重连
  C __onLibInfo 不预设 libPort
后果是客户端与 web 版行为不一致（web 版会闪现旧 localStorage 照片、换端口不重连）。
这 3 处本身是修 bug 的逻辑，不该只给客户端。现已全部上移进根页 index.html，
于是本脚本退化为「纯复制 + 断言零差异」，从机制上杜绝两者再次漂移。

用法：python sync_client.py
"""
import shutil
import difflib

ROOT = 'index.html'
CLIENT = 'client/resources/index.html'

shutil.copyfile(ROOT, CLIENT)

a = open(ROOT, encoding='utf-8').read().splitlines()
b = open(CLIENT, encoding='utf-8').read().splitlines()
real = [l for l in difflib.unified_diff(a, b, 'ROOT', 'CLIENT', lineterm='')
        if l[:1] in ('+', '-')]
print('copied root -> client; remaining +/- diff lines:', len(real))
for l in real[:40]:
    print(l)
assert not real, '根页与客户端页仍有差异，禁止交付（两份必须完全一致）'
print('IDENTICAL root == client/resources/index.html')

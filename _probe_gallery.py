# -*- coding: utf-8 -*-
"""无头实测：1.9.9 固定外壳后，「全部作品」画廊到底发生了什么。

对照设计：同时查 #home（对照组）。若 #home 也没拿到 .in，说明是 offscreen 下
IntersectionObserver 回调被饿死（环境干扰），而非真实回归。
"""
import os
import sys
import time

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ['LIGHTALBUM_NO_DESKTOP'] = '1'
os.environ['LIGHTALBUM_SKIP_CLOUD'] = '1'
os.environ['LIGHTALBUM_LIB_PORT'] = '8799'

CLIENT = r'D:\Test\workbuddy\2026-09-18-09-00-13\client'
sys.path.insert(0, CLIENT)

from PySide6.QtCore import QUrl, QTimer            # noqa: E402
from PySide6.QtWidgets import QApplication          # noqa: E402
from PySide6.QtWebEngineWidgets import QWebEngineView  # noqa: E402
from lightalbum.core.server import LibraryServer     # noqa: E402

RES = os.path.join(CLIENT, 'resources')

app = QApplication(sys.argv)

srv = LibraryServer(port=8799, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
print('SERVER port =', port)

view = QWebEngineView()
view.resize(720, 940)          # 复刻客户端窗口尺寸（<768px → 手机版布局）
result = {'html': None}

view.loadFinished.connect(lambda ok: print('loadFinished ok=%s' % ok))

PROBE = r"""
(function(){
  var g   = document.getElementById('gallery');
  var h   = document.getElementById('home');
  var grid= document.getElementById('galleryGrid');
  var col = document.querySelector('.main-col');
  var gal = document.querySelector('.gallery');
  var cs  = function(e){ return e ? getComputedStyle(e) : null; };
  var items = document.querySelectorAll('.g-item');
  return JSON.stringify({
    homeClass:   h ? h.className : null,
    galClass:    g ? g.className : null,
    galOpacity:  g ? cs(g).opacity : null,
    galOffsetH:  g ? g.offsetHeight : null,
    galViewportPct: g ? +(g.offsetHeight ? (940/g.offsetHeight*100).toFixed(2) : 0) : null,
    gridChildren: grid ? grid.children.length : null,
    galColCount: gal ? cs(gal).columnCount : null,
    galBoxH:     gal ? gal.offsetHeight : null,
    mainColClientH: col ? col.clientHeight : null,
    mainColScrollH: col ? col.scrollHeight : null,
    items: items.length,
    item0Opacity: items.length ? cs(items[0]).opacity : null,
    imgsTotal:  document.querySelectorAll('.ph-img').length,
    imgsLoaded: document.querySelectorAll('.ph-img.loaded').length,
    docScrollH: document.documentElement.scrollHeight,
    bodyScrollH: document.body.scrollHeight
  });
})()
"""


def probe(tag):
    def cb(v):
        result['html'] = v
        print('--- %s ---' % tag)
        print(v)
    view.page().runJavaScript(PROBE, cb)


def finish():
    app.quit()


view.load(QUrl('http://127.0.0.1:%d/' % port))

# 等页面 + 异步照片拉取，再探两次（第二次滚到画廊再看）
QTimer.singleShot(6000, lambda: probe('T+6s 初始'))
QTimer.singleShot(9000, lambda: view.page().runJavaScript(
    "document.getElementById('gallery').scrollIntoView(); 'scrolled'"))
QTimer.singleShot(12000, lambda: probe('T+12s 滚到画廊后'))
QTimer.singleShot(14000, finish)

app.exec()

# -*- coding: utf-8 -*-
"""探针 v3 · A/B 对照：同一页面内，用 JS 撤回 1.9.9 固定外壳 CSS，看画廊是否恢复。

两步都强制把所有 .reveal 加上 .in（消除 offscreen 下 IntersectionObserver 饿死
导致的 opacity:0 干扰），只比较「DOM 里到底有没有 .g-item」。
"""
import os
import sys

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ['LIGHTALBUM_NO_DESKTOP'] = '1'
os.environ['LIGHTALBUM_SKIP_CLOUD'] = '1'

CLIENT = r'D:\Test\workbuddy\2026-09-18-09-00-13\client'
sys.path.insert(0, CLIENT)

from PySide6.QtCore import QUrl, QTimer            # noqa: E402
from PySide6.QtWidgets import QApplication          # noqa: E402
from PySide6.QtWebEngineWidgets import QWebEngineView  # noqa: E402
from lightalbum.core.server import LibraryServer     # noqa: E402

RES = os.path.join(CLIENT, 'resources')
app = QApplication(sys.argv)
srv = LibraryServer(port=8802, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
view = QWebEngineView()
view.resize(720, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

PIN = """
(function(){
  var s=document.createElement('style'); s.id='pin';
  s.textContent='html,body{height:940px!important}#shell{height:940px!important}'
    +'.main-col{height:940px!important}';
  document.head.appendChild(s); return 'pinned';
})()
"""

# 撤回 1.9.9 固定外壳：模拟改动前的布局
REVERT = """
(function(){
  var s=document.createElement('style'); s.id='revert';
  s.textContent='html,body{height:auto!important;overflow:visible!important}'
    +'#shell{height:auto!important;min-height:100vh!important;overflow:visible!important}'
    +'.main-col{height:auto!important;overflow:visible!important}';
  document.head.appendChild(s); return 'reverted';
})()
"""

FORCE_IN = """
(function(){
  document.querySelectorAll('.reveal').forEach(function(e){e.classList.add('in');});
  if(typeof refreshAll==='function'){ try{ refreshAll(); }catch(e){} }
  return 'in+refreshAll';
})()
"""

PROBE = r"""
(function(){
  var grid=document.getElementById('galleryGrid');
  var gal=document.querySelector('.gallery');
  var cs=function(e){return e?getComputedStyle(e):null;};
  var items=document.querySelectorAll('.g-item');
  var col=document.querySelector('.main-col');
  return JSON.stringify({
    PHOTOS_len:(typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:-1,
    items:items.length,
    gridChildren:grid?grid.children.length:null,
    galColCount:gal?cs(gal).columnCount:null,
    galBoxH:gal?gal.offsetHeight:null,
    mainColClientH:col?col.clientHeight:null,
    mainColScrollH:col?col.scrollHeight:null,
    imgsTotal:document.querySelectorAll('.ph-img').length,
    imgsLoaded:document.querySelectorAll('.ph-img.loaded').length
  });
})()
"""


def runjs(js, tag=None):
    def cb(v):
        if tag:
            print('  [%s] -> %s' % (tag, v))
    view.page().runJavaScript(js, cb)


def show(tag):
    def cb(v):
        print('--- %s ---' % tag)
        print('   ', v)
    view.page().runJavaScript(PROBE, cb)


# A：当前 1.9.9 CSS
QTimer.singleShot(6000, lambda: runjs(PIN, 'pin'))
QTimer.singleShot(7000, lambda: runjs(FORCE_IN, 'forceIn'))
QTimer.singleShot(8500, lambda: show('A · 当前 1.9.9 固定外壳'))

# B：撤回固定外壳（模拟改动前）
QTimer.singleShot(9500, lambda: runjs(REVERT, 'revert'))
QTimer.singleShot(10500, lambda: runjs(FORCE_IN, 'forceIn2'))
QTimer.singleShot(12000, lambda: show('B · 撤回固定外壳（改动前布局）'))

QTimer.singleShot(14000, app.quit)
app.exec()

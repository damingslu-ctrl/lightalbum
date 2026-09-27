# -*- coding: utf-8 -*-
"""探针 v2：区分「没照片数据」还是「固定外壳导致渲染/可见性坏掉」。

offscreen 下 100vh 解析为 0（v1 实测 mainColClientH=64=0+64padding），
所以这里用 JS 显式钉死 940px，模拟真实客户端视口，绕开 vh 缺陷。
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
srv = LibraryServer(port=8801, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
print('SERVER port =', port)

view = QWebEngineView()
view.resize(720, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

# 钉死真实视口高度，抵消 offscreen 的 vh=0 缺陷
PIN = """
(function(){
  var s=document.createElement('style');
  s.textContent='html,body{height:940px!important}'
    +'#shell{height:940px!important}'
    +'.main-col{height:940px!important}';
  document.head.appendChild(s);
  return 'pinned';
})()
"""

PROBE = r"""
(function(){
  var g=document.getElementById('gallery'), h=document.getElementById('home');
  var grid=document.getElementById('galleryGrid');
  var col=document.querySelector('.main-col');
  var gal=document.querySelector('.gallery');
  var cs=function(e){return e?getComputedStyle(e):null;};
  var items=document.querySelectorAll('.g-item');
  var PH=(typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:-1;
  return JSON.stringify({
    PHOTOS_len: PH,
    homeClass: h?h.className:null,
    galClass: g?g.className:null,
    galOpacity: g?cs(g).opacity:null,
    gridChildren: grid?grid.children.length:null,
    galColCount: gal?cs(gal).columnCount:null,
    mainColClientH: col?col.clientHeight:null,
    mainColScrollH: col?col.scrollHeight:null,
    mainColScrollTop: col?col.scrollTop:null,
    items: items.length,
    item0Opacity: items.length?cs(items[0]).opacity:null,
    imgsTotal: document.querySelectorAll('.ph-img').length,
    imgsLoaded: document.querySelectorAll('.ph-img.loaded').length
  });
})()
"""


def show(tag):
    def cb(v):
        print('--- %s ---' % tag)
        print(v)
    view.page().runJavaScript(PROBE, cb)


QTimer.singleShot(6000, lambda: view.page().runJavaScript(PIN))
QTimer.singleShot(7000, lambda: show('T+7s 钉死940后'))
QTimer.singleShot(9000, lambda: view.page().runJavaScript(
    "document.getElementById('gallery').scrollIntoView();'ok'"))
QTimer.singleShot(11000, lambda: show('T+11s 滚到画廊'))
QTimer.singleShot(13000, lambda: view.page().runJavaScript(
    "var c=document.querySelector('.main-col');c.scrollTop=c.scrollHeight;'ok'"))
QTimer.singleShot(15000, lambda: show('T+15s 滚到底（触发哨兵）'))
QTimer.singleShot(17000, app.quit)

app.exec()

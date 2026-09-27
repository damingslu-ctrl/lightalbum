# -*- coding: utf-8 -*-
"""1.9.10 修复验证：画廊是否渲染出照片 + 图带节点数/时长是否修复。"""
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
srv = LibraryServer(port=8803, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
view = QWebEngineView()
view.resize(720, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

PROBE = r"""
(function(){
  var cs=function(e){return e?getComputedStyle(e):null;};
  var band=document.getElementById('bandRowA');
  var grid=document.getElementById('galleryGrid');
  return JSON.stringify({
    PHOTOS_len:(typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:-1,
    libHandshake: window.__libHandshake===true,
    galleryItems: document.querySelectorAll('.g-item').length,
    gridChildren: grid?grid.children.length:null,
    bandCards: band?band.children.length:null,
    bandAnimDur: band?cs(band).animationDuration:null,
    countText: (document.getElementById('galleryCount')||{}).textContent||null
  });
})()
"""

QTimer.singleShot(7000, lambda: view.page().runJavaScript(PROBE, lambda v: print('RESULT', v)))
QTimer.singleShot(10000, app.quit)
app.exec()

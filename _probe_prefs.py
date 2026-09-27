# -*- coding: utf-8 -*-
"""1.9.11 实测：默认深色 + 胶片条默认收起。"""
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
srv = LibraryServer(port=8805, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
view = QWebEngineView()
view.resize(1440, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

PROBE = r"""
(function(){
  var b=document.body;
  var fs=document.getElementById('filmstrip');
  var cs=fs?getComputedStyle(fs):null;
  var tf=document.getElementById('filmToggle');
  return JSON.stringify({
    bodyClass: b.className,
    hasDark: b.classList.contains('dark'),
    hasFilmOpen: b.classList.contains('film-open'),
    filmTransform: cs?cs.transform:null,
    filmTop: fs?Math.round(fs.getBoundingClientRect().top):null,
    viewportH: window.innerHeight,
    filmDisplay: cs?cs.display:null,
    filmTogglePressed: tf?tf.getAttribute('aria-pressed'):null,
    themeBtnText: (document.getElementById('themeBtn')||{}).textContent||null,
    storedTheme: localStorage.getItem((typeof LS_KEY!=='undefined'?LS_KEY:'')+'theme')
  });
})()
"""

QTimer.singleShot(7000, lambda: view.page().runJavaScript(PROBE, lambda v: print('RESULT', v)))
QTimer.singleShot(10000, app.quit)
app.exec()

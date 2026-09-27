# -*- coding: utf-8 -*-
"""编辑器双重初始化修复 E2E 探针：连发两次 openEditor（复现 __edSync 竞态）。"""
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

# 1x1 GIF：fromURL 真实走异步解码，足以复现「ed 未回填窗口」
GIF = ('data:image/gif;base64,'
       'R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7')

SETUP = r"""
(function(){
  window.__native = { call: function(){} };
  rlCurPhoto = function(){ return { path:'C:/fake.jpg', rel:'fake.jpg',
                                    orig:'%s', img:'%s' }; };
  __openEditor(true);   // 第一次：new Canvas + fromURL 异步在途
  __openEditor(true);   // 第二次：落在 ed 未回填窗口（修复前必抛）
  return 'fired';
})()
""" % (GIF, GIF)

CHECK = r"""
(function(){
  try{
    var t = document.getElementById('edToast');
    return JSON.stringify({
      canvasContainers: document.querySelectorAll('.canvas-container').length,
      edEmptyHidden: (document.getElementById('edEmpty')||{}).hidden === true,
      toastHasInitFail: !!(t && t.textContent && t.textContent.indexOf('初始化失败') >= 0),
      toastText: t ? (t.textContent||'').slice(0,80) : null
    });
  }catch(e){ return JSON.stringify({checkErr: String(e && e.message || e)}); }
})()
"""

RESULT = {}


def phase1(v):
    print('PHASE1', v)
    # 第三次调用：此时 ed 已就绪或已修复重建，再连发两轮逼近真实刷新场景
    view.page().runJavaScript(
        "(function(){__openEditor(true);__openEditor(true);return 'fired2';})()",
        phase2)


def phase2(v):
    print('PHASE2', v)
    view.page().runJavaScript("String(1+1)", phase3)


def phase3(v):
    print('PHASE3 minimal:', repr(v))
    QTimer.singleShot(1500, lambda: view.page().runJavaScript(CHECK, done))


def done(v):
    print('RESULT', repr(v))
    RESULT.update({'r': v})
    app.quit()


QTimer.singleShot(7000, lambda: view.page().runJavaScript(SETUP, phase1))
QTimer.singleShot(20000, app.quit)
app.exec()

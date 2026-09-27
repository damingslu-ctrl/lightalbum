# -*- coding: utf-8 -*-
"""页面侧性能探针：真实 QtWebEngine(offscreen) 加载 index.html，对准**真实库**，
测 refreshAll() 均值耗时 / 画廊项数 / DOM 规模，作为优化前后对比。

用法：client/venv/Scripts/python.exe probe_page_perf.py [db_path]
注意：autosync=False，只读，不改库。
"""
import json
import os
import sys

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ['LIGHTALBUM_NO_DESKTOP'] = '1'
os.environ['LIGHTALBUM_SKIP_CLOUD'] = '1'

CLIENT = r'D:\Test\workbuddy\2026-09-18-09-00-13\client'
sys.path.insert(0, CLIENT)

from PySide6.QtCore import QUrl, QTimer                    # noqa: E402
from PySide6.QtWidgets import QApplication                 # noqa: E402
from PySide6.QtWebEngineWidgets import QWebEngineView      # noqa: E402
from lightalbum.core.server import LibraryServer           # noqa: E402

DB = sys.argv[1] if len(sys.argv) > 1 else r'D:\lightalbum\library.db'
RES = os.path.join(CLIENT, 'resources')

app = QApplication(sys.argv)
PORT = 8804
srv = LibraryServer(port=PORT, autosync=False, db_path=DB,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
view = QWebEngineView()
view.resize(1440, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

# 纯 QWebEngineView 没有客户端 native_bridge 注入端口，
# 这里手动模拟 bridge：把库端口交给页面并触发本地库探测。
BOOTSTRAP = (
    '(function(){'
    '  try{'
    '    if(typeof state!=="undefined"){ state.libPort=%d; }'
    '    if(typeof tryLocalLibrary==="function"){ tryLocalLibrary(%d); }'
    '  }catch(e){ window.__bootstrapErr=String(e); }'
    '  return "ok";'
    '})()' % (PORT, PORT)
)

WAIT_JS = r"""(function(){
  return JSON.stringify({
    photos: (typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:-1,
    handshake: window.__libHandshake===true
  });
})()"""

PROBE = r"""
(function(){
  var out = {};
  out.photos = (typeof PHOTOS!=='undefined' && PHOTOS) ? PHOTOS.length : -1;
  out.galleryItems = document.querySelectorAll('.g-item').length;
  out.domNodes = document.getElementsByTagName('*').length;
  out.filmItems = document.querySelectorAll('#filmstrip .ft').length;
  if (typeof refreshAll === 'function') {
    refreshAll();                       // 预热
    var t0 = performance.now();
    for (var i=0;i<5;i++) refreshAll();
    out.refreshAllMs = +((performance.now()-t0)/5).toFixed(2);
  } else { out.refreshAllMs = -1; }
  var g = document.getElementById('galleryGrid');
  out.gridChildren = g ? g.children.length : null;
  var f = document.getElementById('filmstrip');
  out.filmChildren = f ? f.children.length : null;
  return JSON.stringify(out);
})()
"""

state = {'tries': 0, 'done': False}


def bootstrap():
    view.page().runJavaScript(BOOTSTRAP, lambda v: poll_ready())


def poll_ready():
    state['tries'] += 1
    view.page().runJavaScript(WAIT_JS, lambda v: on_ready(v))


def on_ready(v):
    try:
        d = json.loads(v)
    except Exception:
        d = {}
    if (d.get('photos', -1) > 0 or state['tries'] >= 60):
        view.page().runJavaScript(PROBE, finish)
        return
    QTimer.singleShot(500, poll_ready)


def finish(v):
    try:
        state['data'] = json.loads(v)
    except Exception as e:
        state['data'] = {'raw': str(v), 'err': str(e)}
    state['done'] = True
    app.quit()


QTimer.singleShot(4000, bootstrap)
QTimer.singleShot(60000, app.quit)      # 兜底
app.exec()

d = state.get('data', {})
print('PAGE_PERF db=%s' % DB)
print('  photos=%s galleryItems=%s domNodes=%s gridChildren=%s filmChildren=%s'
      % (d.get('photos'), d.get('galleryItems'), d.get('domNodes'),
         d.get('gridChildren'), d.get('filmChildren')))
print('  refreshAllMs=%s' % d.get('refreshAllMs'))
print('PROBE_PAGE_PERF_DONE')

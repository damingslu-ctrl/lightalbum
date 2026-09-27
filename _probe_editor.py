# -*- coding: utf-8 -*-
"""编辑器初始化失败专项探针：无头复现 `new fabric.Canvas('edCanvas')` 的真实报错。"""
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
srv = LibraryServer(port=8804, autosync=False, db_path=None,
                    static_dir=RES, host='127.0.0.1', token='')
port = srv.start()
view = QWebEngineView()
view.resize(1440, 940)
view.load(QUrl('http://127.0.0.1:%d/' % port))

PROBE = r"""
(function(){
  var out={fabric:typeof fabric};
  try{
    var el=document.getElementById('edCanvas');
    out.canvasExists=!!el;
    out.canvasTag=el?el.tagName:null;
    out.canvasInDom=el?!!el.closest('#edWrap, body'):null;
    if(typeof fabric!=='undefined'){
      var fc=new fabric.Canvas('edCanvas',{selection:false,preserveObjectStacking:true});
      out.ok=true;
      try{
        var fc2=new fabric.Canvas('edCanvas',{selection:false,preserveObjectStacking:true});
        out.double='ok(不抛)'; fc2.dispose();
      }catch(e2){
        out.double='THROW'; out.err2=(e2&&e2.message)?e2.message:String(e2);
      }
      fc.dispose();
      try{
        var fc3=new fabric.Canvas('edCanvas',{selection:false,preserveObjectStacking:true});
        out.reinit='ok'; fc3.dispose();
      }catch(e3){
        out.reinit='THROW'; out.err3=(e3&&e3.message)?e3.message:String(e3);
      }
    }
  }catch(e){
    out.ok=false;
    out.err=(e&&e.message)?e.message:String(e);
    out.stack=(e&&e.stack)?String(e.stack).split('\n').slice(0,3).join(' | '):'';
  }
  return JSON.stringify(out);
})()
"""

def done(v):
    print('RESULT', v)
    app.quit()

QTimer.singleShot(7000, lambda: view.page().runJavaScript(PROBE, done))
QTimer.singleShot(15000, app.quit)
app.exec()

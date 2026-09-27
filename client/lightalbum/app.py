# -*- coding: utf-8 -*-
"""
光影集 · 本地客户端

架构
----
原生外壳（Qt Widgets）+ 内嵌 WebEngine 展示网页版作品集。

    网页 (index.html)                   Python 原生层
    ──────────────────                  ──────────────
    __native.call(action, payload)  →   lightalbum://action?p=<json>
                                        NativeSchemeHandler.requestStarted()
                                        Bridge.handle()
                                          ├ 原生文件对话框
                                          ├ 目录扫描（QThread）
                                          ├ EXIF 提取（QThread）
                                          └ 缩略图生成
    window.__onNative(payload)      ←   page.runJavaScript()

传输方式的关键取舍（踩过大坑）
------------------------------
JS → Python 走**子资源请求**（`new Image().src = 'lightalbum://…'`），由
QWebEngineUrlSchemeHandler 接住并回一个 1×1 GIF；**绝不能用 `location.assign`
去发指令**：那是主框架导航，一旦被 acceptNavigationRequest 拒绝，Chromium 会把
当前文档替换成 `about:blank#blocked`；而桥接脚本在 DocumentCreation 注入，新文档
里又会再发一次 → 死循环刷屏、页面永久空白。

为什么不用 QWebChannel：PySide6 的 wheel 里没有内置 qwebchannel.js，
从 Qt 源码里扒一份再打包会增加一层脆弱的构建依赖。自定义 URL scheme +
runJavaScript 这对组合零外部文件、零版本耦合，且天然是异步的。

为什么扫描/EXIF 走 QThread：上千张照片的 EXIF 读取是秒级操作，
放在主线程会把窗口卡成无响应。
"""
from __future__ import annotations

import base64
import io
import json
import os
import shutil
import sys
import threading
import time
import urllib.parse


from PySide6.QtCore import (
    Qt, QObject, QThread, Signal, QUrl, QTimer, QBuffer, QIODevice,
    QSettings, QStandardPaths, QSize,
)
from PySide6.QtGui import (
    QAction, QIcon, QKeySequence, QDesktopServices, QPixmap, QPainter, QColor,
)
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QFileDialog, QMessageBox, QProgressBar, QLabel,
    QSystemTrayIcon, QMenu, QDialog, QDialogButtonBox, QFormLayout, QLineEdit,
    QSpinBox, QCheckBox, QComboBox,
)
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWebEngineCore import (
    QWebEngineUrlScheme, QWebEngineUrlSchemeHandler, QWebEngineScript,
    QWebEnginePage,
)
from PySide6.QtWebEngineWidgets import QWebEngineView

# 桌面集成（托盘 / 拖拽 / 几何记忆 / 任务栏进度 / 设置）
from .desktop import DesktopIntegration, log, TaskbarProgress

# 版本与命名（单一事实源；构建脚本与本文件共用同一份）
from .version import (
    APP_NAME, APP_NAME_EN, APP_VERSION, ORG_NAME,
    ENGINE_NAME, ENGINE_NAME_CN, ENGINE_VERSION, ENGINE_TAGLINE,
    engine_display,
)

# 核心模块（PIL / piexif / numpy…）与 Pillow 一律**懒加载**：
# 启动时不导入，首次真正用到（扫描 / 修图 / 探测）才 import。
# 实测这一项能省掉启动期约 300ms 的纯导入开销。
E = S = T = CA = R = AI = None


def _cores(*need):
    """按需导入核心模块。用法：E, S, T = _cores('E','S','T')；不传参默认三者。"""
    global E, S, T, CA, R, AI
    if not need:
        need = ('E', 'S', 'T')          # 早先漏了这个默认值，导致 _cores() 返回空元组、
                                        # 解包抛异常，还被并行循环的 except 吞掉 →
                                        # 「扫描成功但 0 张」（实测踩过）
    if E is None:
        from .core import exif as _E, scanner as _S, thumbs as _T
        E, S, T = _E, _S, _T
    if CA is None and any(n in ('CA', 'R', 'AI') for n in need):
        from .core import cloud_albums as _CA, retouch as _R, ai as _AI
        CA, R, AI = _CA, _R, _AI
    return tuple(globals()[n] for n in need)

# APP_NAME / 版本 / 引擎命名由 .version 提供（单一事实源，见上方 import）
SCHEME = b'lightalbum'

# ---- 启动计时（仅在设了 LIGHTALBUM_TIMING=1 时启用，写文件供外部测量）----
_TIMING = {'t0': time.perf_counter(), 'on': bool(os.environ.get('LIGHTALBUM_TIMING'))}


def _mark(name):
    """记录启动里程碑。设 LIGHTALBUM_TIMING=1 时写 %TEMP%/lightalbum_startup.txt。

    为什么写文件而不是 print：打包后是 --windowed，没有控制台。
    """
    if not _TIMING['on']:
        return
    try:
        path = os.path.join(os.environ.get('TEMP') or '/tmp', 'lightalbum_startup.txt')
        with open(path, 'a', encoding='utf-8') as f:
            f.write('%s\t%.0f\n' % (name, (time.perf_counter() - _TIMING['t0']) * 1000))
    except Exception:
        pass


_mark('script_start')
_mark('qt_ready')


# 资源目录：源码运行时在 ../resources；PyInstaller 打包后资源随包分发。
# 不同 PyInstaller 布局下 _MEIPASS 指向不同：
#   * onefile 解压到临时目录，_MEIPASS 即资源根；
#   * onedir（新 _internal 双文件夹布局）下 _MEIPASS 指向 _internal，
#     资源在 _internal/resources；
#   * 老版 onedir 资源直接在可执行文件同级 resources。
# 这里用「存在即采用」的回退链，兼容三种情况，避免页面/桥接脚本找不到而白屏。
if getattr(sys, 'frozen', False):
    exe_dir = os.path.dirname(os.path.abspath(sys.executable))
    meipass = getattr(sys, '_MEIPASS', exe_dir)
    candidates = [
        os.path.join(meipass, 'resources'),
        os.path.join(exe_dir, 'resources'),
        os.path.join(exe_dir, '_internal', 'resources'),
    ]
    RES = next((c for c in candidates if os.path.isdir(c)), candidates[0])
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
    RES = os.path.abspath(os.path.join(HERE, '..', 'resources'))
PAGE = os.path.join(RES, 'index.html')
BRIDGE_JS = os.path.join(RES, 'native_bridge.js')


# ------------------------------------------------------------ 工作线程
SCAN_CHUNK = 300          # 一次性把上万条结果塞给页面会卡 UI，分批传输


class ScanWorker(QObject):
    """目录扫描 + 逐张 EXIF + 缩略图。线程池并行，全程不碰主线程。

    大批量导入的三级提速（相对最早的串行版）：
    1. 缓存优先 —— 缩略图命中缓存时连文件都不打开（重扫近乎免费）；
    2. 单次读取 —— EXIF 与缩略图共用一次磁盘读 + 一次 PIL 打开；
       缩略图用 draft() 让 JPEG 只解码 1/8 分辨率；
    3. 线程池 —— Pillow/piexif 的 C 段会放 GIL，多核并行有效。
    """
    progress = Signal(int, int, str)      # 当前 / 总数 / 状态文案
    finished = Signal(list)               # 完整照片列表（网页版字段契约）
    failed = Signal(str)

    def __init__(self, root, want_thumbs=True, workers=None):
        super().__init__()
        self.root = root
        self.want_thumbs = want_thumbs
        self._abort = False
        self._workers = workers or min(8, max(2, (os.cpu_count() or 4)))

    def abort(self):
        self._abort = True

    def run(self):
        try:
            S, = _cores('S')
            files = S.scan_dir(self.root)
            total = len(files)
            if not total:
                self.finished.emit([])
                return
            out = [None] * total
            idx = {rel: i for i, rel in enumerate(files)}
            done = 0
            errors = []

            def _collect(rel, fu=None):
                """收一张图的结果；失败要计数并留证，不能静默吞掉。

                早先这里写的是 `except Exception: pass`，把「所有照片都处理失败」
                伪装成了「扫描成功但 0 张」（实测踩过，非常难查）。
                """
                try:
                    item = self._one(rel) if fu is None else fu.result()
                except Exception as e:
                    errors.append('%s: %s' % (rel, e))
                    return
                out[idx[rel]] = item

            if self._workers <= 1 or total < 8:
                # 小目录/单核：串行即可，省掉线程池开销
                for i, rel in enumerate(files):
                    if self._abort:
                        break
                    _collect(rel)
                    done += 1
                    if i % 10 == 0 or i == total - 1:
                        self.progress.emit(i + 1, total, os.path.basename(rel))
            else:
                from concurrent.futures import ThreadPoolExecutor, as_completed
                with ThreadPoolExecutor(max_workers=self._workers) as pool:
                    futs = {pool.submit(self._one, rel): rel for rel in files}
                    for fu in as_completed(futs):
                        if self._abort:
                            for f in futs:
                                f.cancel()
                            break
                        rel = futs[fu]
                        _collect(rel, fu)
                        done += 1
                        if done % 20 == 0 or done == total:
                            self.progress.emit(done, total, os.path.basename(rel))
            items = [x for x in out if x]
            # 一张都没成功、却有错误 → 当作失败上报，别让用户以为「这个文件夹是空的」
            if not items and errors:
                self.failed.emit('全部 %d 张处理失败，例如：%s' % (len(errors), errors[0]))
                return
            self.finished.emit(items)
        except Exception as e:
            self.failed.emit(str(e))

    def _one(self, rel):
        """处理单张：读一次盘、开一次图，EXIF 与缩略图共享。"""
        E, S, T = _cores()
        abs_p = os.path.join(self.root, rel.replace('/', os.sep))
        g = S.guess_from_path(rel)

        # 缓存优先：缩略图命中就完全不碰图片本体（连 EXIF 都只需头部，
        # 但 piexif 会整读 —— 真正免费的只有缩略图这一段）
        img_url = ''
        need_decode = self.want_thumbs
        if need_decode:
            hit = T.cached_thumb_path(abs_p)
            if hit:
                img_url = T.to_file_url(hit)
                need_decode = False

        data = None
        im = None
        if need_decode:
            try:
                with open(abs_p, 'rb') as f:
                    data = f.read()
                from PIL import Image          # 懒加载：只在真的要看图时才导入
                im = Image.open(io.BytesIO(data))
            except Exception:
                data = None
                im = None

        try:
            ex = E.read_exif(abs_p, data=data, _im=im)
        except Exception:
            ex = {}

        # 优先级：EXIF GPS > 路径推导（有 GPS 就用真实坐标）
        la, lo = ex.get('la'), ex.get('lo')
        if la is None or lo is None:
            la, lo = None, None

        d = ex.get('d') or g['d']
        tm = ex.get('tm') or ''
        item = {
            't': g['t'],
            's': g['s'] or '',
            'y': int(d[:4]) if d[:4].isdigit() else 0,
            'o': 'portrait' if (ex.get('orient') or 0) in (5, 6, 7, 8) else 'landscape',
            'r': g['r'], 'pv': g['pv'], 'ct': g['ct'],
            'lo': lo,
            'la': la,
            'cam': ex.get('cam', ''), 'lens': ex.get('lens', ''),
            'focal': ex.get('focal', 0) or 0,
            'ap': ex.get('ap') or '',
            'sh': ex.get('sh') or '',
            'iso': ex.get('iso') or 0,
            'd': d, 'tm': tm,
            'tag': '',
            'alt': ex.get('alt'), 'evc': ex.get('evc', 0.0),
            'wb': ex.get('wb', ''), 'meter': ex.get('meter', ''),
            'dim': ex.get('dim', ''),
            'src': 'native', 'rel': rel,
            'img': img_url, '_abs': abs_p,
        }
        if need_decode and self.want_thumbs:
            try:
                tp = T.thumb_from_open(im, abs_p)
                if tp:
                    item['img'] = T.to_file_url(tp)
            except Exception:
                pass
        if im is not None:
            try:
                im.close()
            except Exception:
                pass
        return item


class CloudWorker(QObject):
    """云相册目录探测（要遍历几千个文件做统计，必须离开主线程）。

    早先版本在 Bridge 里同步调用，启动自动探测时会卡住界面 0.5-1s。
    """
    done = Signal(list)
    failed = Signal(str)

    def __init__(self, extra):
        super().__init__()
        self.extra = extra or {}

    def run(self):
        try:
            CA, = _cores('CA')
            self.done.emit(CA.detect(self.extra))
        except Exception as e:
            self.failed.emit(str(e))


class RetouchWorker(QObject):
    """批量智能修图，全程在子线程（大图逐张处理是秒级操作）。"""
    progress = Signal(int, int, str)      # 当前 / 总数 / 文件名
    finished = Signal(list, list)         # (成功摘要列表, 失败[(path, err)])
    failed = Signal(str)

    def __init__(self, paths, preset):
        super().__init__()
        self.paths = paths
        self.preset = preset
        self._abort = False

    def abort(self):
        self._abort = True

    def run(self):
        try:
            total = len(self.paths)
            oks, errs = [], []
            for i, p in enumerate(self.paths):
                if self._abort:
                    break
                try:
                    # 用项目自己的 EXIF 读取器喂给修图引擎 → 降噪强度由真实 ISO/快门决定
                    E, R = _cores('E', 'R')
                    ex = E.read_exif(p)
                    exif = {'iso': ex.get('iso') or 0, 'shutter': ex.get('shutter') or 0}
                    out, s = R.retouch_file(p, self.preset, exif)
                    oks.append(s)
                except Exception as e:
                    errs.append((p, str(e)))
                self.progress.emit(i + 1, total, os.path.basename(p))
            self.finished.emit(oks, errs)
        except Exception as e:
            self.failed.emit(str(e))


class CallWorker(QObject):
    """通用子线程作业：执行 fn()，成功/失败回推信号。

    为什么需要：AI 决策层（Jev / 本地 LLM）要外呼 + 探测，执行层（Pixel
    Cake）要上传并处理大图。这些都可能**秒级到分钟级**（本地 LLM 冷加载
    实测 >110s）。以前它们直接在 GUI 线程里跑，一卡就是「正在智能修图」不动，
    整个客户端假死。现在统统丢到子线程，界面照常响应。
    """
    done = Signal(object)
    failed = Signal(str)

    def __init__(self, fn):
        super().__init__()
        self._fn = fn

    def run(self):
        try:
            self.done.emit(self._fn())
        except Exception as e:                       # noqa: BLE001
            self.failed.emit(str(e))


class ThreadSlot(QObject):
    """把普通回调托管成「GUI 线程亲和」的槽。

    Qt 的 AutoConnection 依据**接收者 QObject 的线程归属**决定直连还是排队。
    普通闭包不是 QObject，信号到达时可能在子线程里被直接调用 —— 那样
    `win.status(...)` 之类的界面操作就变成跨线程访问。包一层本类（在 GUI
    线程创建 → 归属 GUI 线程）后，跨线程回推会被 Qt 自动排到主线程执行。
    """
    def __init__(self, ok, err):
        super().__init__()
        self._ok = ok
        self._err = err

    def on_ok(self, res):
        if self._ok:
            self._ok(res)

    def on_err(self, msg):
        if self._err:
            self._err(msg)


# ------------------------------------------------------------ 桥接
class Bridge(QObject):
    """JS → Python 的入口。所有原生能力都从这里分发。"""
    result = Signal(str, object)          # (action, payload) 回推给网页
    # 页面走 http 加载后，lightalbum:// 信标会被 Chromium 拦截（跨 scheme 子资源），
    # 所以原生动作改走本地库服务：POST /api/native → 这里排队回到 GUI 线程执行。
    actionIn = Signal(str, object)

    def __init__(self, window):
        super().__init__()
        self.win = window
        self.thread = None
        self.worker = None
        self._cloud_thread = None
        self._cloud_worker = None
        self._jobs = []                         # 在跑的一次性后台作业（防 GC）
        self.actionIn.connect(self.handle)      # 队列连接：跨线程安全
        self._poll_interval = 10.0              # 外部产物轮询间隔（E2E 测试可调小）

    # ---- 由 BridgePage 调用
    def handle(self, action, payload):
        fn = getattr(self, 'act_' + action, None)
        if fn is None:
            self.win.status('未知的原生指令: %s' % action)
            return
        try:
            fn(payload or {})
        except Exception as e:
            self.win.status('原生指令出错: %s' % e)
            QMessageBox.warning(self.win, APP_NAME, '操作失败：\n%s' % e)

    def _run_async(self, fn, on_ok, on_err):
        """在子线程执行 fn()，完成后把结果交回 **GUI 线程** 的 on_ok / on_err。

        AI 决策/执行这类动作耗时不可控（本地 LLM 冷加载可 >110s、Pixel Cake
        处理大图可达分钟级），必须离开 GUI 线程，否则界面直接假死。
        """
        th = QThread(self.win)
        w = CallWorker(fn)
        slot = ThreadSlot(on_ok, on_err)            # 在 GUI 线程创建 → 归属 GUI
        w.moveToThread(th)
        th.started.connect(w.run)
        w.done.connect(slot.on_ok)                  # 跨线程 → Qt 自动排队到 GUI
        w.failed.connect(slot.on_err)
        w.done.connect(th.quit)
        w.failed.connect(th.quit)
        # 先按「身份」摘引用（此刻 C++ 对象还活着），再让它自己析构。
        # ★勿用 isRunning() 去清列表：QThread 被 deleteLater 后 Python 包装仍在，
        #   但对已析构的 C++ 对象调任何方法都抛 RuntimeError（本项被回归测试抓到）。
        th.finished.connect(self._drop_finished_job)
        th.finished.connect(w.deleteLater)
        th.finished.connect(th.deleteLater)
        th.finished.connect(slot.deleteLater)
        self._jobs.append((th, w, slot))            # 持有引用防 GC
        th.start()

    def _drop_finished_job(self):
        """线程结束后按 sender() 身份移出 _jobs（只比 Python 身份，不碰 C++）。"""
        th = self.sender()
        self._jobs = [j for j in self._jobs if j[0] is not th]

    # ---- 具体动作
    def act_pickFolder(self, p):
        d = QFileDialog.getExistingDirectory(
            self.win, '选择照片文件夹', self.win.last_dir or os.path.expanduser('~'))
        if d:
            self.win.last_dir = d
            self.emit('folderPicked', {'path': d})

    def act_pickExport(self, p):
        """选片导出目录选择（与 pickFolder 区分，避免触发扫描流程）。"""
        d = QFileDialog.getExistingDirectory(
            self.win, '选择选片导出目录',
            self.win.last_dir or os.path.expanduser('~'))
        if d:
            self.win.last_dir = d
            self.emit('exportPicked', {'path': d})

    def act_scan(self, p):
        root = p.get('root', '')
        if not root or not os.path.isdir(root):
            self.emit('scanError', {'msg': '目录不存在：%s' % root})
            return
        self.win.last_dir = root
        self._run_scan(root, p.get('thumbs', True))

    def act_abort(self, p):
        if self.worker:
            self.worker.abort()

    def act_importToLibrary(self, p):
        """把用户挑选的照片复制进 app 自管的「导入库」目录，再入库。

        与「扫描本地文件夹」（按原路径引用、依赖源盘/iCloud 挂载）不同，
        这里是**把原件复制进 ~/.lightalbum/imports/**，之后即使源盘拔掉、
        iCloud 没挂载，照片也完整保存在本机，图库照常显示。
        """
        files, _ = QFileDialog.getOpenFileNames(
            self.win, '选择要导入的照片',
            self.win.last_dir or os.path.expanduser('~'),
            '图片文件 (*.jpg *.jpeg *.png *.heic *.heif *.tif *.tiff '
            '*.webp *.gif *.bmp);;所有文件 (*.*)')
        if not files:
            return
        self._run_import(files)

    # ---- 外部编辑器跳转（ADR-0013）：工作副本 → 独立暗房 → 产物回灌 ----
    def _extedit_engine(self):
        """优先取 Engine-Hub 里的 extedit 引擎；Hub 未挂载时退回独立实例。"""
        hub = getattr(self.win, 'hub', None)
        if hub is not None:
            eng = hub.get('extedit')
            if eng is not None:
                return eng
        if getattr(self, '_ext_engine_fallback', None) is None:
            from .core.engines.external_editor_engine import ExternalEditorEngine
            self._ext_engine_fallback = ExternalEditorEngine()
        return self._ext_engine_fallback

    def act_extEdit(self, p):
        """把本机原图交给外部编辑器（darktable/ART/PhotoDemon…）。

        首次使用弹对话框选编辑器并记住（QSettings extEditor）；
        启动后开后台轮询：output 目录出现新产物 → 自动回灌自管目录。
        """
        path = (p or {}).get('path', '')
        if not path or not os.path.isfile(path):
            self.emit('extEditError', {'msg': '找不到本机文件：%s' % path})
            return
        eng = self._extedit_engine()
        try:
            if not eng.task_editor().get('configured'):
                saved = str(QSettings('lightalbum', 'LightAlbum')
                            .value('extEditor', ''))
                if saved and os.path.isfile(saved):
                    eng.task_set_editor(saved)
                else:
                    exe, _f = QFileDialog.getOpenFileName(
                        self.win, '选择外部编辑器（darktable / ART / PhotoDemon…）',
                        '', '程序 (*.exe);;所有文件 (*.*)')
                    if not exe:
                        self.win.status('未选择外部编辑器，已取消')
                        return
                    eng.task_set_editor(exe)
                    QSettings('lightalbum', 'LightAlbum').setValue('extEditor', exe)
                    self.emit('extEditEditorSet', {'editor': exe})
        except Exception as e:
            self.emit('extEditError', {'msg': '外部编辑器配置失败：%s' % e})
            return

        since = time.time()          # 产物 mtime 基线（此刻之后写出才算新产物）
        self.win.status('正在交给外部编辑器…')

        def job():
            hub = getattr(self.win, 'hub', None)
            if hub is not None and hub.get('extedit') is not None:
                jh = hub.submit('extedit', 'launch', path)   # 走统一调度（ADR-0012）
                jh.wait(300)
                if jh.error is not None:
                    raise jh.error
                return jh.result
            return self._extedit_engine().run('launch', (path,), {})

        def ok(res):
            self.emit('extEditLaunched', res)
            self._start_extedit_poller(res.get('out_dir', ''), since, path)

        def err(msg_):
            self.emit('extEditError', {'msg': str(msg_)})

        self._run_async(job, ok, err)

    def _start_extedit_poller(self, out_dir, since, orig):
        """每 10 秒扫描 output 目录（上限 6 小时），发现产物自动回灌入库。

        轮询在纯后台线程；入库动作经 actionIn 队列连接跳回 GUI 线程，
        与既有 _run_async / ThreadSlot 的线程纪律一致。
        """
        if not out_dir:
            return
        eng = self._extedit_engine()

        def poll():
            deadline = time.time() + 6 * 3600
            while time.time() < deadline:
                time.sleep(self._poll_interval)
                try:
                    found = eng.run('scan', (out_dir, since), {})
                except Exception:
                    return
                if found:
                    self.actionIn.emit('extEditImport', {'items': [
                        {'path': f['path'], 'orig': orig, 'kind': 'extedit'}
                        for f in found]})
                    return
        th = threading.Thread(target=poll, name='extedit-poll', daemon=True)
        self._ext_pollers = getattr(self, '_ext_pollers', [])
        self._ext_pollers.append(th)   # 持引用防 GC
        th.start()

    def act_extEditImport(self, p):
        """外部产物回灌：复制进自管目录并写库，登记版本关联后推 versionsMap。"""
        items = [(i.get('path'), i.get('orig') or '',
                  i.get('kind') or 'extedit')
                 for i in (p or {}).get('items', [])]
        items = [(f, o, k) for (f, o, k) in items if f and os.path.isfile(f)]
        if not items:
            return
        self.win.status('外部产物 %d 张，正在回灌入库…' % len(items))
        self._run_import([f for (f, _o, _k) in items], variants=items)


    # ---- 无头批量渲染（ADR-0013：darktable-cli/art-cli 同套显影出整组）----
    def _headless_engine(self):
        """优先取 Engine-Hub 里的 headless 引擎；Hub 未挂载时退回独立实例。"""
        hub = getattr(self.win, 'hub', None)
        if hub is not None:
            eng = hub.get('headless')
            if eng is not None:
                return eng
        if getattr(self, '_headless_fallback', None) is None:
            from .core.engines.headless_engine import HeadlessRenderEngine
            self._headless_fallback = HeadlessRenderEngine()
        return self._headless_fallback

    def act_headlessRender(self, p):
        """对当前照片同目录的整组照片做无头渲染。

        参数源：若当前照片存在同名 .xmp 侧车（用户在 darktable 里编辑过，
        默认自动写），就用它当显影参数；否则用 darktable 默认渲染。
        产物落到 <同目录>/_develop/（scanner 已跳过该目录），渲染完成后
        自动回灌入库并登记为「渲染」版本。
        """
        src = (p or {}).get('path', '')
        if not src or not os.path.isfile(src):
            self.emit('headlessError', {'msg': '找不到本机文件：%s' % src})
            return
        eng = self._headless_engine()
        if not eng.task_probe().get('darktable', {}).get('available'):
            exe, _f = QFileDialog.getOpenFileName(
                self.win, '选择 darktable-cli.exe（默认在 D:\\darktable\\bin\\）',
                '', '程序 (*.exe);;所有文件 (*.*)')
            if not exe:
                self.emit('headlessError', {'msg': '未配置 darktable-cli，已取消'})
                return
            eng.task_set_binary('darktable', exe)

        folder = os.path.dirname(src)
        sidecar = None
        sc = src + '.xmp'                      # darktable 编辑后自动写的侧车
        if os.path.isfile(sc):
            sidecar = sc
        files = eng.run('collect', (folder, src), {'cap': 100})
        if not files:
            self.emit('headlessError', {'msg': '同目录没有其他可渲染的照片'})
            return
        if QMessageBox.question(
                self.win, APP_NAME,
                '将渲染同目录 %d 张（源目录：\n%s\n）\n参数：%s\n输出：%s\\_develop\\\n\n开始？'
                % (len(files), folder,
                   '同名 .xmp 侧车' if sidecar else 'darktable 默认渲染',
                   folder)) != QMessageBox.Yes:
            return
        out_dir = os.path.join(folder, '_develop')
        os.makedirs(out_dir, exist_ok=True)
        self.win.status('无头渲染 0/%d…' % len(files))

        def job():
            hub = getattr(self.win, 'hub', None)
            use_hub = hub is not None and hub.get('headless') is not None
            oks, errs = [], []
            for i, f in enumerate(files):
                stem, _e = os.path.splitext(os.path.basename(f))
                out = os.path.join(out_dir, stem + '-develop.jpg')
                try:
                    if use_hub:
                        jh = hub.submit('headless', 'render', f, out,
                                        sidecar=sidecar)   # 统一调度（ADR-0012）
                        jh.wait(600)
                        if jh.error is not None:
                            raise jh.error
                    else:
                        eng.run('render', (f, out), {'sidecar': sidecar})
                    oks.append((f, out))
                except Exception as e:
                    errs.append('%s: %s' % (os.path.basename(f), e))
                self.emit('headlessProgress', {'done': i + 1,
                                               'total': len(files),
                                               'name': os.path.basename(f)})
            return {'oks': oks, 'errs': errs}

        def ok(res):
            self.emit('headlessDone', {'ok': len(res['oks']),
                                       'fail': len(res['errs'])})
            if res['oks']:                     # 成片回灌为「渲染」版本
                items = [{'path': out, 'orig': f, 'kind': 'develop'}
                         for (f, out) in res['oks']]
                self.actionIn.emit('extEditImport', {'items': items})
            if res['errs']:
                self.emit('headlessError', {'msg': '失败 %d 张：%s'
                                            % (len(res['errs']), res['errs'][0])})

        def err(msg_):
            self.emit('headlessError', {'msg': str(msg_)})

        self._run_async(job, ok, err)

    # ---- AI 决策（Jev）+ 执行（Pixel Cake），两者可独立降级
    def act_aiProbe(self, p):
        """探测 jev_local / Pixel Cake 是否在线，以及本机 GPU。

        探测含多次带超时的外呼（jev 3s + cake 3s + LLM 2.5s）+ `nvidia-smi` 5s，
        最坏合计十余秒；它在页面加载时就触发，同步跑会把开屏卡住 → 放子线程。
        """
        cfg = (p or {}).get('config') or {}

        def job():
            AI, = _cores('AI')
            return AI.status(cfg)

        def ok(res):
            self.emit('aiStatus', res)

        def err(msg):
            self.emit('aiStatus', {'error': msg})

        self._run_async(job, ok, err)

    def act_aiEdit(self, p):
        """一键智能修图：目标 → 决策 → 执行。全程本地（有回环地址守卫）。

        重活（决策层外呼 + 执行层大图处理）放子线程：本地 LLM 冷加载实测可
        >110s，旧版直接在 GUI 线程里跑，界面会**冻在「正在智能修图」不动**。
        """
        # aiDecide 字段由页面发送但此处不消费：「AI 全权」语义由 preset='auto' 表达（1.9.39 审查锚定）
        path = (p or {}).get('path', '')
        goal = (p or {}).get('goal', '') or ''
        preset = (p or {}).get('preset', 'auto') or 'auto'
        cfg = (p or {}).get('config') or {}
        if not path or not os.path.isfile(path):
            self.emit('aiEditError', {'msg': '文件不存在：%s' % path})
            return
        self.win.status('智能修图中…（%s）' % preset)

        def job():
            E, AI = _cores('E', 'AI')
            ex = E.read_exif(path)
            exif = {'iso': ex.get('iso'), 'shutter': ex.get('shutter'),
                    'ap': ex.get('ap'), 'focal': ex.get('focal')}
            return AI.smart_edit(path, goal, exif, cfg, preset=preset)

        def ok(res):
            self._attach_file_urls((res or {}).get('summary'))
            self.emit('aiEditResult', res)
            self.win.status('智能修图完成（%s · %s）'
                            % (res.get('brain_name', '?'), res.get('executor_name', '?')))

        def err(msg):
            self.emit('aiEditError', {'msg': msg})
            self.win.status('智能修图失败：%s' % msg)

        self._run_async(job, ok, err)

    def act_aiSuggest(self, p):
        """本地 AI 预设建议：按画面特征推荐预设（**仅建议、不改图**，全程本地）。"""
        path = (p or {}).get('path', '')
        cfg = (p or {}).get('config') or {}
        if not path or not os.path.isfile(path):
            self.emit('aiSuggestResult', {'error': '文件不存在：%s' % path,
                                          'suggestions': []})
            return

        def job():
            E, AI = _cores('E', 'AI')
            ex = E.read_exif(path)
            exif = {'iso': ex.get('iso'), 'shutter': ex.get('shutter'),
                    'ap': ex.get('ap'), 'focal': ex.get('focal')}
            return AI.suggest_presets(path, exif, cfg)

        def ok(res):
            self.emit('aiSuggestResult', res)

        def err(msg):
            self.emit('aiSuggestResult', {'error': msg, 'suggestions': []})

        self._run_async(job, ok, err)                # 可能含 Pixel Cake 上传，勿占 GUI

    def act_matting(self, p):
        """BiRefNet 去背景 / 抠图（P3）。需 Pixel Cake + BiRefNet 在线。"""
        path = (p or {}).get('path', '')
        cfg = (p or {}).get('config') or {}
        rgba = bool((p or {}).get('rgba', False))
        if not path or not os.path.isfile(path):
            self.emit('mattingResult', {'error': '文件不存在：%s' % path})
            return

        def job():
            AI, = _cores('AI')
            return AI.matting(path, cfg, return_rgba=rgba)

        def ok(res):
            self.emit('mattingResult', res)
            self.win.status('去背景完成（%s）' % os.path.basename(res.get('out', '')))

        def err(msg):
            self.emit('mattingResult', {'error': msg})

        self.win.status('去背景中…（BiRefNet 首次加载较慢）')
        self._run_async(job, ok, err)                # BiRefNet 加载 ~16s，勿占 GUI

    def act_score(self, p):
        """pyiqa 画质/美学评分（P4）。需 Pixel Cake + pyiqa 在线（musiq 等无参考指标）。"""
        path = (p or {}).get('path', '')
        cfg = (p or {}).get('config') or {}
        if not path or not os.path.isfile(path):
            self.emit('scoreResult', {'error': '文件不存在：%s' % path})
            return

        def job():
            AI, = _cores('AI')
            return AI.score(path, cfg)

        def ok(res):
            self.emit('scoreResult', res)
            self.win.status('画质评分完成')

        def err(msg):
            self.emit('scoreResult', {'error': msg})

        self.win.status('画质评分中…')
        self._run_async(job, ok, err)                # 需上传 + pyiqa 推理，勿占 GUI

    def act_saveImage(self, p):
        """把内嵌编辑器产出的图片（base64 PNG/JPEG）静默写到磁盘。

        p.path 目标路径；p.data base64 字符串（可带 data: 前缀）。
        """
        import base64
        path = (p or {}).get('path', '')
        data = (p or {}).get('data', '') or ''
        if not path or not data:
            self.emit('saveImageResult', {'error': '缺少路径或数据'})
            return

        def job():
            d = data
            if ',' in d[:64] and d[:5] == 'data:':
                d = d.split(',', 1)[1]
            with open(path, 'wb') as f:
                f.write(base64.b64decode(d))
            return path

        def ok(fp):
            self.emit('saveImageResult', {'path': fp})
            self.win.status('已保存：%s' % fp)

        def err(msg):
            self.emit('saveImageResult', {'error': msg})

        # 编辑器产物可达几十 MB base64：解码 + 写盘放子线程（同 act_* 的既定做法）
        self._run_async(job, ok, err)

    def act_inpaint(self, p):
        """AI 消除：按蒙版抹掉区域并智能补全（Pixel Cake · LaMa）。"""
        path = (p or {}).get('path', '')
        mask = (p or {}).get('mask', '')
        cfg = (p or {}).get('config') or {}
        prompt = (p or {}).get('prompt', '')
        if not path or not mask:
            self.emit('inpaintResult', {'error': '缺少原图或蒙版'})
            return

        def job():
            AI, = _cores('AI')
            return AI.inpaint(path, mask, cfg, prompt)

        def ok(res):
            self.emit('inpaintResult', res)
            self.win.status('AI 消除完成')

        def err(msg):
            self.emit('inpaintResult', {'error': msg})

        self.win.status('AI 消除中…')
        self._run_async(job, ok, err)                # 上传 + LaMa 推理，勿占 GUI

    def act_cloudAlbums(self, p):
        """探测 iCloud / 一刻相册的本地同步目录（子线程，不阻塞界面）。

        p.extra：可选，{provider_id: [目录, ...]} —— 用户手动指定的补充目录
        （如一刻相册自选的备份路径），只归属对应 provider，与真实候选择优。
        p.auto：可选，探测结果里带上标记，页面据此决定是否自动导入。
        """
        extra = (p.get('extra') or {}) if p else {}
        auto = bool((p or {}).get('auto'))
        if self._cloud_thread and self._cloud_thread.isRunning():
            return                                  # 已有探测在跑，别叠
        self._cloud_thread = QThread(self.win)
        self._cloud_worker = CloudWorker(extra)
        self._cloud_worker.moveToThread(self._cloud_thread)
        self._cloud_thread.started.connect(self._cloud_worker.run)
        self._cloud_worker.done.connect(
            lambda albums: self.emit('cloudAlbums', {'albums': albums, 'auto': auto}))
        self._cloud_worker.failed.connect(
            lambda m: self.emit('cloudAlbumsError', {'msg': m}))
        self._cloud_worker.done.connect(self._cloud_thread.quit)
        self._cloud_worker.failed.connect(self._cloud_thread.quit)
        self._cloud_thread.start()

    def _attach_file_urls(self, summary):
        """给修图摘要补上「本机 HTTP 取图 URL」。

        为什么必须：页面在 http://127.0.0.1:<port>/ 下运行，Chromium 禁止 http
        页面加载 file:// 子资源 —— 直接用 file:// 对比图会全黑。改走 /api/file/<token>。
        """
        try:
            srv = getattr(self.win, 'lib_server', None)
            if not srv or not isinstance(summary, dict):
                return summary
            if summary.get('in') and not summary.get('in_url'):
                summary['in_url'] = srv.file_url(summary['in'])
            if summary.get('out') and not summary.get('out_url'):
                summary['out_url'] = srv.file_url(summary['out'])
        except Exception:
            pass
        return summary

    def act_retouch(self, p):
        """智能修图：paths 为绝对路径列表，preset ∈ soft/auto/strong。"""
        paths = [x for x in (p.get('paths') or []) if x and os.path.isfile(x)]
        if not paths:
            self.emit('retouchError', {'msg': '没有可修图的有效文件路径'})
            return
        self._run_retouch(paths, p.get('preset', 'auto'))

    def act_openExternal(self, p):
        path = p.get('path', '')
        if path and os.path.isfile(path):
            QDesktopServices.openUrl(QUrl.fromLocalFile(path))

    def act_revealFile(self, p):
        """在资源管理器中打开文件所在文件夹（Windows 下选中该文件）。"""
        path = p.get('path', '')
        if not path or not os.path.exists(path):
            return
        folder = os.path.dirname(path) if os.path.isfile(path) else path
        if sys.platform.startswith('win'):
            # explorer /select, 让文件在资源管理器里被高亮选中
            try:
                os.startfile(folder) if not os.path.isfile(path) else os.startfile(path)
                return
            except Exception:
                pass
        QDesktopServices.openUrl(QUrl.fromLocalFile(folder))

    def act_saveFile(self, p):
        """原生另存为（导出 JSON）。"""
        path, _ = QFileDialog.getSaveFileName(
            self.win, '导出作品数据', p.get('default', '光影集-作品数据.json'),
            'JSON 文件 (*.json)')
        if not path:
            return
        content = p.get('content', '') or ''

        def job():
            with open(path, 'w', encoding='utf-8') as f:
                f.write(content)
            return path

        def ok(fp):
            self.emit('saveDone', {'path': fp})
            self.win.status('已导出：%s' % fp)

        def err(msg):
            QMessageBox.warning(self.win, APP_NAME, '导出失败：\n%s' % msg)

        self._run_async(job, ok, err)     # 对话框留在 GUI 线程；写盘（可能几十 MB）放子线程

    def act_openFile(self, p):
        """原生打开（导入 JSON）。"""
        path, _ = QFileDialog.getOpenFileName(
            self.win, '导入作品数据', self.win.last_dir or '', 'JSON 文件 (*.json)')
        if not path:
            return

        def job():
            with open(path, 'r', encoding='utf-8') as f:
                return f.read()

        def ok(content):
            self.emit('fileOpened', {'path': path, 'content': content})
            self.win.status('已读取：%s' % path)

        def err(msg):
            QMessageBox.warning(self.win, APP_NAME, '读取失败：\n%s' % msg)

        self._run_async(job, ok, err)     # 大 JSON 读盘别占 GUI 线程

    def act_setTitle(self, p):
        self.win.setWindowTitle('%s — %s' % (APP_NAME, p.get('title', '')))

    def act_confirmClearDemo(self, p):
        """首轮真实扫描时，问一次要不要把网页自带的示例作品清掉。

        为什么要问：示例数据一旦和真实照片混在一起，图库计数、EXIF 统计、
        足迹地图全都会被污染，用户会以为软件算错了。但也不能替他删，
        所以问一次；选「否」就两边共存，后续不再追问。
        """
        demo = int(p.get('demo') or 0)
        scanned = int(p.get('scanned') or 0)
        box = QMessageBox(self.win)
        box.setWindowTitle(APP_NAME)
        box.setIcon(QMessageBox.Question)
        box.setText('检测到 %d 幅示例作品，本次扫描到 %d 幅真实照片。' % (demo, scanned))
        box.setInformativeText(
            '示例作品是网页版的占位数据。如果保留，它会和你的真实照片混在一起，'
            '影响图库计数、EXIF 统计与足迹地图。\n\n是否清空示例作品，只保留扫描结果？')
        yes = box.addButton('清空示例作品', QMessageBox.AcceptRole)
        box.addButton('保留，两者共存', QMessageBox.RejectRole)
        box.setDefaultButton(yes)
        box.exec()
        self.emit('clearDemoDecision', {'clear': box.clickedButton() is yes})

    def act_quit(self, p):
        self.win.close()

    def act_version(self, p):
        self.emit('version', {
            'name': APP_NAME, 'version': APP_VERSION, 'py': sys.version.split()[0],
            'native': True,
            'engine': ENGINE_NAME, 'engineCn': ENGINE_NAME_CN,
            'engineVersion': ENGINE_VERSION,
            'capabilities': ['scan', 'exif', 'gps', 'thumbs', 'openExternal',
                             'saveFile', 'openFile', 'pickFolder',
                             'importToLibrary'],
        })

    # ---- 内部
    def _run_scan(self, root, want_thumbs):
        if self.thread and self.thread.isRunning():
            self.worker.abort()
            self.thread.quit()
            self.thread.wait(2000)
        self.thread = QThread(self.win)
        self.worker = ScanWorker(root, want_thumbs)
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.progress.connect(self.win.on_scan_progress)
        self.worker.finished.connect(self._scan_done)
        self.worker.failed.connect(self._scan_failed)
        self.worker.finished.connect(self.thread.quit)
        self.worker.failed.connect(self.thread.quit)
        self.thread.start()
        self.win.status('正在扫描：%s' % root)

    def _scan_done(self, items):
        self.win.on_scan_done(len(items))
        root = getattr(self.worker, 'root', '')
        # 关键修复：把扫描结果同时写入本地库（DB 是唯一事实来源）。
        # 早年只推进内存 PHOTOS + localStorage，本地库模式下画廊读 DB，
        # 会覆盖内存 PHOTOS —— 扫描结果在后台同步刷新的瞬间被抹掉、重启后
        # 彻底丢失，表现为「图库不显示导入的图片」。写库后 watchLibrarySync
        # 会自动把新照片并进来。
        # 入库是逐张 os.stat + 缩略图缓存查找（上千张=上万次 syscall），旧实现直接
        # 压在 GUI 线程上 → 扫描一结束界面就僵住。现在入库放子线程，
        # 且**入库完成后才推 scanResult**，保持「先落库、后刷页」的原有顺序。
        self._persist_scan(root, items)

    def _emit_scan_result(self, root, items):
        """把扫描结果推给页面。小批量一次推；大批量分块推 —— 一次 runJavaScript
        塞几 MB JSON 会把页面主线程卡死。"""
        if len(items) <= SCAN_CHUNK:
            self.emit('scanResult', {'photos': items, 'root': root})
            return
        total = len(items)
        for i in range(0, total, SCAN_CHUNK):
            part = items[i:i + SCAN_CHUNK]
            self.emit('scanResultChunk', {
                'items': part, 'total': total,
                'last': (i + SCAN_CHUNK) >= total, 'root': root,
            })

    def _persist_scan(self, root, items):
        """把扫描出来的照片落到本地库 DB（与扫描结果入库互不相通的老问题根因）。

        只在本机库服务可用时生效；file:// 降级模式下没有服务，
        仍走原来的内存 PHOTOS + localStorage，行为不变。
        写完后推 libraryChanged，让画廊立即刷新（watchLibrarySync 3 秒内也会兜底）。
        """
        srv = getattr(self.win, 'lib_server', None)
        if not srv or not root or not items:
            self._emit_scan_result(root, items)      # 无本地库（降级模式）：直接推结果
            return

        def job():
            srv.sync.add_root(root)
            return srv.lib.import_photos(root, items, src='native')

        def ok(n):
            self.win.push_result('libraryChanged', {'root': root, 'count': n})
            self._emit_scan_result(root, items)

        def err(msg):
            self.win.status('扫描入库失败：%s' % msg)
            self._emit_scan_result(root, items)      # 入库失败也得让页面看到扫描结果

        self._run_async(job, ok, err)

    def _scan_failed(self, msg):
        self.win.status('扫描失败')
        self.emit('scanError', {'msg': msg})

    # ---- 导入到 app 自管目录（彻底不依赖 iCloud / 源盘挂载）
    def _run_import(self, files, variants=None):
        srv = getattr(self.win, 'lib_server', None)
        if not srv:
            self.win.status('本地库服务不可用，无法导入')
            return
        if files:
            self.win.last_dir = os.path.dirname(files[0])
        self.win.status('正在导入到自管目录：%d 张…' % len(files))
        threading.Thread(target=self._import_worker, args=(files, variants),
                        name='import-to-lib', daemon=True).start()

    def _unique_name(self, folder, name):
        """在 folder 下生成不冲突的文件名（同名追加 _2/_3…）。"""
        base, ext = os.path.splitext(name)
        cand = name
        k = 2
        while os.path.exists(os.path.join(folder, cand)):
            cand = '%s_%d%s' % (base, k, ext)
            k += 1
        return os.path.join(folder, cand)

    def _import_worker(self, files, variants=None):
        """复制原件 → 入库 → 推刷新。全程后台线程，不阻塞界面。"""
        try:
            from .core import library as LIB
            imports_dir = LIB.IMPORTS_DIR
            os.makedirs(imports_dir, exist_ok=True)
            copied = []
            recorded = []            # (dst, orig, kind) 待登记的版本关联
            try:
                from .core.versions import norm as _vnorm, products as _vproducts, \
                    record as _vrecord
            except Exception:        # versions 不可用时退化为普通导入
                _vnorm = _vproducts = _vrecord = None
            for f in files:
                if not os.path.isfile(f):
                    continue
                dst = self._unique_name(imports_dir, os.path.basename(f))
                try:
                    shutil.copy2(f, dst)
                    copied.append(dst)
                    if _vnorm is not None:
                        for (vf, vorig, vkind) in (variants or []):
                            if _vnorm(vf) == _vnorm(f):
                                recorded.append((dst, vorig, vkind))
                                break
                except Exception as e:
                    self.emit('importError', {'msg': '%s: %s' % (f, e)})
            if copied:
                srv = self.win.lib_server
                srv.sync.add_root(imports_dir)
                # 同步自管目录：缩略图经缓存优先生成，原件已在本机，绝不触发云盘按需下载
                srv.sync.sync(imports_dir, src='import')
                self.emit('importDone', {'count': len(copied), 'dir': imports_dir})
                self.win.push_result('libraryChanged',
                                     {'root': imports_dir, 'count': len(copied)})
                if recorded and _vrecord is not None:
                    try:
                        import time as _t
                        for (dst, vorig, vkind) in recorded:
                            _vrecord(dst, vorig, vkind, _t.time())
                        self.emit('versionsMap', {'products': _vproducts()})
                    except Exception:
                        pass
            else:
                self.emit('importError', {'msg': '没有成功复制任何文件'})
        except Exception as e:
            self.emit('importError', {'msg': str(e)})

    # ---- 智能修图（与扫描同一套 QThread 模式）
    def _run_retouch(self, paths, preset):
        if self.thread and self.thread.isRunning():
            self.worker.abort()
            self.thread.quit()
            self.thread.wait(2000)
        self.thread = QThread(self.win)
        self.worker = RetouchWorker(paths, preset)
        self.worker.moveToThread(self.thread)
        self.thread.started.connect(self.worker.run)
        self.worker.progress.connect(
            lambda d, t, n: self.emit('retouchProgress', {'done': d, 'total': t, 'name': n}))
        # 原生状态栏同步显示进度（页面里的动态进度条由 retouchProgress 驱动）
        self.worker.progress.connect(
            lambda d, t, n: self.win.status('智能修图中 %d / %d：%s' % (d, t, n)))
        self.worker.finished.connect(self._retouch_done)
        self.worker.failed.connect(self._retouch_failed)
        self.worker.finished.connect(self.thread.quit)
        self.worker.failed.connect(self.thread.quit)
        self.thread.start()
        self.win.status('智能修图进行中：%d 张（%s）' % (len(paths), preset))

    def _retouch_done(self, oks, errs):
        for _s in oks:
            self._attach_file_urls(_s)
        self.win.status('智能修图完成：%d 成功 / %d 失败' % (len(oks), len(errs)))
        self.emit('retouchResult', {'ok': oks, 'err': errs,
                                    'preset': self.worker.preset})

    def _retouch_failed(self, msg):
        self.win.status('智能修图失败')
        self.emit('retouchError', {'msg': msg})

    def emit(self, action, payload):
        self.result.emit(action, payload)


# ------------------------------------------------------------ 页面
class NativeSchemeHandler(QWebEngineUrlSchemeHandler):
    """接住网页发来的 lightalbum:// 子资源请求，转成原生调用。

    网页侧用 `new Image().src = 'lightalbum://action?p=<json>'` 发指令；
    这里解析后派发给 Bridge，并回一个 1×1 透明 GIF 让请求「成功结束」——
    避免浏览器控制台出现一堆资源加载失败。

    请求是子资源，不触碰主框架导航，因此不会出现文档被替换成 about:blank 的问题。
    """

    _PIXEL = base64.b64decode(
        'R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7')   # 1×1 透明 GIF

    def __init__(self, bridge, parent=None):
        super().__init__(parent)
        self.bridge = bridge
        self._keep = []          # 持有 QBuffer 引用，reply 期间不能被 GC

    def requestStarted(self, job):
        url = job.requestUrl()
        q = urllib.parse.parse_qs(url.query())
        # action 优先取 query 里的 a=：authority 会被 URL 规范化成小写，
        # 走 host 会把 confirmClearDemo 变成 confirmcleardemo（踩过）。
        action = (q.get('a') or [url.path().lstrip('/') or url.host()])[0]
        raw = (q.get('p') or ['{}'])[0]
        try:
            payload = json.loads(raw)
        except Exception:
            payload = {}

        buf = QBuffer()
        buf.setData(self._PIXEL)
        buf.open(QIODevice.ReadOnly)
        self._keep.append(buf)
        try:
            job.destroyed.connect(lambda *_: self._drop(buf))
        except Exception:
            pass
        job.reply(b'image/gif', buf)

        # 延后派发：不在请求回调里直接弹模态对话框（QFileDialog 会重入事件循环）
        QTimer.singleShot(0, lambda a=action, p=payload: self.bridge.handle(a, p))

    def _drop(self, buf):
        try:
            self._keep.remove(buf)
        except ValueError:
            pass


class BridgePage(QWebEnginePage):
    """兜底：万一有代码把主框架导航到 lightalbum://，直接拒绝，不让它破坏文档。"""

    def __init__(self, bridge, parent=None):
        super().__init__(parent)
        self.bridge = bridge

    def acceptNavigationRequest(self, url, navtype, isMainFrame):
        # 正常的 JS→原生 通道是子资源请求（由 NativeSchemeHandler 接），不走导航。
        # 这里只做兜底：真有代码把主框架导航到 lightalbum:// 时直接拒绝，
        # 否则 scheme handler 的 1×1 GIF 会成为新文档、把页面顶掉。
        if url.scheme().lower() == 'lightalbum':
            return False
        return super().acceptNavigationRequest(url, navtype, isMainFrame)


# ------------------------------------------------------------ 主窗口
class MainWindow(QMainWindow, DesktopIntegration):
    def __init__(self):
        super().__init__()
        self.last_dir = ''
        self.setWindowTitle(APP_NAME)
        # 桌面版尺寸：与网页版在宽窗口下一致（左侧栏 + 顶栏 + 全屏影像 hero）。
        # 页面是同一份，布局由视口宽度决定：>768px 走桌面版（含左侧栏），<=768px 切成手机版。
        self.resize(1440, 940)

        self.bridge = Bridge(self)
        self.view = QWebEngineView(self)
        page = BridgePage(self.bridge, self.view)
        self.view.setPage(page)
        self.bridge.result.connect(self.push_result)
        self.pushIn.connect(self._push_result_now)      # 跨线程推送排队

        # 安装自定义 scheme handler：承载网页发来的子资源「信标」请求
        self.scheme_handler = NativeSchemeHandler(self.bridge, self)
        page.profile().installUrlSchemeHandler(SCHEME, self.scheme_handler)

        # 注入桥接脚本：在任何页面脚本之前就绪
        self._inject_bridge_js(page)
        # 本地库服务必须在加载页面前起来：页面要通过同源的 /api/* 拿数据，
        # 而 file:// 页面是 opaque origin，Chromium 禁止它 fetch 本地 http（实测）。
        self.lib_server = None
        self.hub = None               # ADR-0012 engine hub (mounted in _start_library)
        self._lib_info = None
        self._classifier_proc = None      # 由本客户端拉起的 CLIP 服务子进程
        self._cake_proc = None            # 由本客户端拉起的 Pixel Cake 修图服务子进程
        self._face_proc = None            # 由本客户端拉起的人脸服务子进程
        _mark('lib_before')
        self._start_library()
        _mark('lib_done')
        # 本地 CLIP 分类服务随客户端自启（端口 8766，外部 venv 进程）。
        # 失败静默降级，不影响主程序。
        self._start_classifier()
        # 本地 Pixel Cake 修图服务随客户端自启（端口 8765）。
        # 这样「智能修图」才真正用上本地重模型；失败静默降级到本地 Pillow 管线。
        self._start_cake()
        # 本地人脸服务随客户端自启（端口 8767，外部 venv 进程）。
        # 提供「人物识别」能力；失败静默降级，UI 隐藏入口。
        self._start_face()
        _mark('ai_spawned')
        # 页面加载完成后再补推一次库信息（首次推送时页面可能还在解析）
        self.view.loadFinished.connect(self._on_page_loaded)
        self.setCentralWidget(self.view)

        self._build_menu()
        self._build_status()
        _mark('ui_built')

        # 页面地址：优先用本地库服务提供（同源，页面才能 fetch API）；
        # 服务起不来就退回 file://（此时页面走 localStorage，功能降级但可用）。
        page_url = QUrl.fromLocalFile(PAGE)
        if self.lib_server:
            page_url = QUrl('http://127.0.0.1:%d/' % self.lib_server.port)
        else:
            self.status('本地库服务不可用，改用 file:// 加载页面')
        if not os.environ.get('LIGHTALBUM_NO_DESKTOP'):
            self.desktop_init()      # 托盘 / 拖拽 / 几何记忆 / 任务栏进度（可用 LIGHTALBUM_NO_DESKTOP=1 跳过）
        _mark('desktop_done')
        self.view.setUrl(page_url)
        _mark('url_set')
        QTimer.singleShot(700, lambda: self.status('就绪'))

    def _push_lib_info(self, info):
        """推送本地库信息。页面可能还没加载完 —— 先存下来，
        等 loadFinished 后再补推一次，否则事件会丢在空白页里。"""
        self._lib_info = info
        self.push_result('libInfo', info)

    def _on_page_loaded(self, ok):
        _mark('page_loaded')
        if ok and getattr(self, '_lib_info', None):
            QTimer.singleShot(400, lambda: self.push_result('libInfo', self._lib_info))
        if ok:                                 # 版本组映射（灯箱版本条数据源）
            QTimer.singleShot(600, self._push_versions)

    def _push_versions(self, *_a):
        try:
            from .core import versions as VER
            self.push_result('versionsMap', {'products': VER.products()})
        except Exception:
            pass

    # ---- 本地库服务（客户端 = 数据服务）
    def _start_library(self):
        try:
            from .core.server import LibraryServer
            port = int(os.environ.get('LIGHTALBUM_LIB_PORT') or 8787)
            db = os.environ.get('LIGHTALBUM_DB') or None
            lan = str(QSettings('lightalbum', 'LightAlbum').value('lanAccess', 'false'))
            host = '0.0.0.0' if lan == 'true' else '127.0.0.1'
            token = str(QSettings('lightalbum', 'LightAlbum').value('lanToken', ''))
            if lan == 'true' and not token:
                import secrets
                token = secrets.token_hex(8)
                QSettings('lightalbum', 'LightAlbum').setValue('lanToken', token)
            self.lib_server = LibraryServer(port=port, autosync=True, db_path=db,
                                            static_dir=RES, host=host, token=token)
            # 页面走 http 后信标会被拦，原生动作统一由本地服务转发回桥接
            self.lib_server.native_handler = (
                lambda action, payload: self.bridge.actionIn.emit(action, payload))
            port = self.lib_server.start()
            # ADR-0012: mount unified engine hub。
            # 1.9.32 性能：build_default_hub 要 import 各引擎（重模块），同步构建会
            # 直接算进启动耗时；挪到后台线程，主线程不等。引擎只在用户真正发起任务
            # 时才用到，未就绪/构建失败仍按原逻辑降级为 None，不改变任何语义。
            def _build_hub():
                try:
                    from .core.engines.registry import build_default_hub
                    _hub = build_default_hub(self.lib_server.lib)
                    QTimer.singleShot(0, lambda: setattr(self, 'hub', _hub))
                except Exception as _hub_e:  # noqa: BLE001
                    print('[hub] init failed (degraded): %s' % _hub_e)
                _mark('hub_done')
            threading.Thread(target=_build_hub, name='hub-build', daemon=True).start()
            self.status('本地库服务已启动：http://127.0.0.1:%d' % port)
            if os.environ.get('LIGHTALBUM_SKIP_CLOUD'):
                self._push_lib_info({'port': port,
                                     'photos': self.lib_server.lib.count()})
                return
            threading.Thread(target=self._register_roots, args=(port,),
                             daemon=True).start()
        except Exception as e:
            self.lib_server = None
            self.hub = None
            self.status('本地库服务启动失败：%s' % e)

    # ---- AI 分类服务自启（随客户端启动，端口 8766，外部 venv 进程）
    def _start_classifier(self):
        """随客户端自启本地 CLIP 分类服务。

        仅当 `LIGHTALBUM_CLASSIFIER_AUTOSTART != '0'` 时生效（默认开）。
        在后台线程拉起，避免首屏因等子进程就绪而卡顿；失败静默降级。
        """
        if os.environ.get('LIGHTALBUM_CLASSIFIER_AUTOSTART', '1') == '0':
            return
        threading.Thread(target=self._launch_classifier, name='cls-launch',
                         daemon=True).start()

    def _launch_classifier(self):
        try:
            from .core.classifier_service import launch
            res = launch(autostart=True)
            if res.get('launched'):
                self._classifier_proc = res.get('proc')
                self._classifier_pid = res.get('pid')
                QTimer.singleShot(0, lambda: self.status('AI 分类服务已启动'))
            elif res.get('adopted'):
                self._classifier_proc = res.get('proc')
                self._classifier_pid = res.get('pid')
                QTimer.singleShot(0, lambda: self.status('AI 分类服务（接管残留进程）'))
            elif res.get('already_running'):
                QTimer.singleShot(0, lambda: self.status('AI 分类服务已在运行'))
            elif res.get('error'):
                log('classifier autostart: %s' % res['error'])
        except Exception as e:  # noqa: BLE001
            log('classifier autostart failed: %s' % e)

    def _start_cake(self):
        """随客户端自启本地 Pixel Cake 修图服务（端口 8765）。

        仅当 `LIGHTALBUM_CAKE_AUTOSTART != '0'` 时生效（默认开）。
        后台线程拉起，避免首屏因等 torch 加载而卡顿；失败静默降级。
        """
        if os.environ.get('LIGHTALBUM_CAKE_AUTOSTART', '1') == '0':
            return
        threading.Thread(target=self._launch_cake, name='cake-launch',
                         daemon=True).start()

    def _launch_cake(self):
        try:
            from .core.cake_service import launch
            res = launch(autostart=True)
            if res.get('launched'):
                self._cake_proc = res.get('proc')
                self._cake_pid = res.get('pid')
                QTimer.singleShot(0, lambda: self.status('修图服务已启动（Pixel Cake）'))
            elif res.get('adopted'):
                self._cake_proc = res.get('proc')
                self._cake_pid = res.get('pid')
                QTimer.singleShot(0, lambda: self.status('修图服务（接管残留进程）'))
            elif res.get('already_running'):
                QTimer.singleShot(0, lambda: self.status('修图服务已在运行'))
            elif res.get('error'):
                log('cake autostart: %s' % res['error'])
        except Exception as e:  # noqa: BLE001
            log('cake autostart failed: %s' % e)

    # ---- 人脸服务自启（随客户端启动，端口 8767，外部 venv 进程）
    def _start_face(self):
        """随客户端自启本地人脸服务。

        仅当 `LIGHTALBUM_FACE_AUTOSTART != '0'` 时生效（默认开）。
        后台线程拉起，失败静默降级。
        """
        if os.environ.get('LIGHTALBUM_FACE_AUTOSTART', '1') == '0':
            return
        threading.Thread(target=self._launch_face, name='face-launch',
                         daemon=True).start()

    def _launch_face(self):
        try:
            from .core.face_service import launch
            res = launch(autostart=True)
            if res.get('launched') or res.get('adopted') or res.get('already_running'):
                self._face_proc = res.get('proc')
                if res.get('launched'):
                    QTimer.singleShot(0, lambda: self.status('人脸服务已启动'))
                elif res.get('adopted'):
                    QTimer.singleShot(0, lambda: self.status('人脸服务（接管残留进程）'))
                else:
                    QTimer.singleShot(0, lambda: self.status('人脸服务已在运行'))
            elif res.get('error'):
                log('face autostart: %s' % res['error'])
        except Exception as e:  # noqa: BLE001
            log('face autostart failed: %s' % e)

    def _register_roots(self, port):
        """把云相册同步目录登记为自动同步源；已在库里的目录直接复用。

        这一步替代了早先「页面每次启动重扫云相册」的做法：
        现在只在后台按 mtime/size 做增量比对，新照片自动入库。
        """
        try:
            from .core import cloud_albums as CA
            albums = CA.detect()
            for a in albums:
                if a.get('found') and a.get('path'):
                    self.lib_server.sync.add_root(a['path'])
            # 首次启动：库里还是空的就同步一轮（之后靠后台定时增量）
            if self.lib_server.lib.count() == 0:
                self.lib_server.sync.sync_all()
            self._push_lib_info({
                'port': self.lib_server.port,
                'photos': self.lib_server.lib.count(),
                'roots': self.lib_server.sync.roots(),
            })
        except Exception as e:
            self.push_result('libInfo', {'port': port, 'error': str(e)})

    # ---- 桥接脚本注入
    def _inject_bridge_js(self, page):
        try:
            with open(BRIDGE_JS, 'r', encoding='utf-8') as f:
                src = f.read()
        except Exception as e:
            self.status('桥接脚本缺失：%s' % e)
            return
        s = QWebEngineScript()
        s.setName('lightalbum-bridge')
        s.setSourceCode(src)
        s.setInjectionPoint(QWebEngineScript.InjectionPoint.DocumentCreation)
        s.setWorldId(QWebEngineScript.ScriptWorldId.MainWorld)
        s.setRunsOnSubFrames(False)
        page.scripts().insert(s)

    pushIn = Signal(str, object)

    def push_result(self, action, payload):
        """Python → JS：把原生结果推回网页。

        ⚠️ runJavaScript 只能在 GUI 线程调用。后台线程（云相册探测、
        同步完成回调）也会推结果 —— 早年直接调 runJavaScript 导致
        QtWebEngine 崩溃（0x80000003 断点异常，启动即闪退）。
        所以这里统一走信号排队回 GUI 线程。
        """
        import threading as _t
        if _t.current_thread() is _t.main_thread():
            self._push_result_now(action, payload)
        else:
            self.pushIn.emit(action, payload)

    def _push_result_now(self, action, payload):
        js = 'window.__onNative && window.__onNative(%s, %s);' % (
            json.dumps(action), json.dumps(payload, ensure_ascii=False))
        self.view.page().runJavaScript(js)

    # ---- 菜单
    def _build_menu(self):
        m = self.menuBar()
        fm = m.addMenu('文件(&F)')
        self._add(fm, '扫描文件夹…', 'Ctrl+O', lambda: self.bridge.handle('pickFolder', {}))
        self._add(fm, '导入到自管目录…', 'Ctrl+Shift+I', lambda: self.bridge.handle('importToLibrary', {}))
        self._add(fm, '导入 JSON…', 'Ctrl+I', lambda: self.bridge.handle('openFile', {}))
        self._add(fm, '导出 JSON…', 'Ctrl+S', lambda: self.bridge.handle('saveFile', {'default': '光影集-作品数据.json'}))

        self._add(fm, '同步新照片', 'F5', lambda: self.sync_now(notify=True))
        fm.addSeparator()
        self._recent_menu = fm.addMenu('最近导入')
        self.refresh_recent()
        fm.addSeparator()
        self._add(fm, '设置…', 'Ctrl+,', self.open_settings)
        self._add(fm, '退出', 'Ctrl+Q', self.quit_app)
        self._refresh_recent_hook = self.refresh_recent

        vm = m.addMenu('视图(&V)')
        self._add(vm, '重新加载页面', 'Ctrl+R', self.view.reload)
        self._add(vm, '放大', 'Ctrl+=', lambda: self._zoom(1.2))
        self._add(vm, '缩小', 'Ctrl+-', lambda: self._zoom(1 / 1.2))
        self._add(vm, '实际大小', 'Ctrl+0', lambda: self.view.setZoomFactor(1.0))

        hm = m.addMenu('帮助(&H)')
        self._add(hm, '关于', '', self._about)

    def _add(self, menu, text, shortcut, fn):
        a = QAction(text, self)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        a.triggered.connect(fn)
        menu.addAction(a)
        return a

    def _zoom(self, k):
        self.view.setZoomFactor(max(0.4, min(3.0, self.view.zoomFactor() * k)))

    def _about(self):
        QMessageBox.information(self, '关于 %s' % APP_NAME,
                                '%s · 本地客户端 %s\n\n'
                                '原生外壳 + 网页渲染，作品数据全部保存在本机。\n'
                                '支持：真实文件夹扫描、真实 EXIF 提取、缩略图缓存、智能修图。\n\n'
                                '修图引擎：%s\n%s\nPython %s'
                                % (APP_NAME, APP_VERSION, engine_display(),
                                   ENGINE_TAGLINE, sys.version.split()[0]))

    # ---- 状态栏
    def _build_status(self):
        self.statusBar().showMessage('启动中…')
        self.pb = QProgressBar(self)
        self.pb.setMaximumWidth(220)
        self.pb.setVisible(False)
        self.statusBar().addPermanentWidget(self.pb)
        self.lbl = QLabel('')
        self.statusBar().addPermanentWidget(self.lbl)

    def status(self, s):
        self.statusBar().showMessage(s)

    def on_scan_progress(self, cur, total, name):
        self.pb.setVisible(True)
        self.pb.setMaximum(total)
        self.pb.setValue(cur)
        self.lbl.setText('%d / %d' % (cur, total))
        self.status('处理：%s' % name)

    def on_scan_done(self, n):
        self.pb.setVisible(False)
        self.lbl.setText('')
        self.status('扫描完成，共 %d 幅作品' % n)

    def _teardown_all(self):
        """真正退出时收尾（关闭策略由 DesktopIntegration.closeEvent 决定）。"""
        try:
            if self.bridge.worker:
                self.bridge.worker.abort()
            if self.bridge.thread and self.bridge.thread.isRunning():
                self.bridge.thread.quit()
                self.bridge.thread.wait(1500)
            for t in (getattr(self.bridge, '_cloud_thread', None),):
                if t and t.isRunning():
                    t.quit()
                    t.wait(800)
            if self.lib_server:                 # 关掉本地库服务与后台同步线程
                self.lib_server.stop()
            # 终止由本客户端拉起 / 认领的 CLIP 分类子进程（外部开的实例不动）
            cp = getattr(self, '_classifier_proc', None)
            if cp is not None:
                try:
                    from .core.classifier_service import stop
                    stop(cp, timeout=3.0)
                except Exception:
                    pass
            # 终止由本客户端拉起 / 认领的 Pixel Cake 修图子进程（外部开的实例不动）
            kp = getattr(self, '_cake_proc', None)
            if kp is not None:
                try:
                    from .core.cake_service import stop
                    stop(kp, timeout=3.0)
                except Exception:
                    pass
            # 终止由本客户端拉起 / 认领的人脸子进程（外部开的实例不动）
            fp = getattr(self, '_face_proc', None)
            if fp is not None:
                try:
                    from .core.face_service import stop
                    stop(fp, timeout=3.0)
                except Exception:
                    pass
        except Exception:
            pass


# ------------------------------------------------------------ 入口
def setup_scheme():
    """注册自定义 scheme。必须在 QApplication 创建之前调用。"""
    sch = QWebEngineUrlScheme(SCHEME)
    sch.setSyntax(QWebEngineUrlScheme.Syntax.Path)
    # setDefaultPort 只吃 int（SpecialPort 枚举不接受），Path 语法下端口本就被忽略，不设。
    sch.setFlags(QWebEngineUrlScheme.Flag.SecureScheme |
                 QWebEngineUrlScheme.Flag.LocalScheme |
                 QWebEngineUrlScheme.Flag.LocalAccessAllowed)
    QWebEngineUrlScheme.registerScheme(sch)


def _notify_existing():
    """已有实例在运行？通知它显示窗口并等待回执。

    收到 'ok' 才算旧实例真的活着（并已唤起窗口）；拿不到回执说明旧实例
    已卡死，返回 False 让本次自建实例，避免「第二次打不开」。
    """
    try:
        sock = QLocalSocket()
        sock.connectToServer('lightalbum-single')
        if sock.waitForConnected(600):
            sock.write(b'show')
            sock.flush()
            sock.waitForBytesWritten(600)
            ok = False
            # 回执可能先于此处就已缓冲；waitForReadyRead 对「已缓冲」的数据
            # 可能不再返回 True，必须先查 bytesAvailable（否则误判旧实例已死、
            # 反而拉起第二个实例）。
            if sock.bytesAvailable() or sock.waitForReadyRead(1500):
                ok = bytes(sock.readAll()).strip().startswith(b'ok')
            sock.disconnectFromServer()
            return ok
    except Exception:
        pass
    return False


def main():
    setup_scheme()
    settings = QSettings('lightalbum', 'LightAlbum')

    QApplication.setApplicationName(APP_NAME)
    QApplication.setApplicationVersion(APP_VERSION)
    QApplication.setOrganizationName(ORG_NAME)
    QApplication.setAttribute(Qt.AA_ShareOpenGLContexts, True)

    app = QApplication(sys.argv)
    _mark('qapp_ready')

    # 单实例：已在运行时，唤起旧窗口后退出（正式软件的标配行为）
    if _notify_existing():
        _mark('single_instance_exit')
        log('another instance is running; exit')
        return

    # 开场动画：在 MainWindow 同步构建（本地库/分类/修图子进程）与随后
    # QWebEngine 渲染首屏这段最卡的窗口期，盖住空白与卡顿。
    # 必须在 QApplication 之后、MainWindow 之前创建，且用 processEvents
    # 先画一帧，否则会闪一下空窗口。
    from .splash import SplashScreen, LightTransition
    # 纯动效开场：只有品牌字标，不放任何说明文案（用户要求）
    splash = SplashScreen(app_name=APP_NAME)
    splash.show()
    app.processEvents()

    w = MainWindow()
    _mark('window_built')
    # 1.9.31：**这里不显示客户端** —— 整个开场期间它都待在后台（未显示），
    # 屏幕上只有开场动画（440x300 的居中浮层）。等动画收拢成线、要从线上把
    # 客户端展开时，才在 _reveal() 里把它从后台拉起（show + raise + activate），
    # 随即被 LightTransition 的整屏暗幕盖住，再从中线展开 —— 全程看不到
    # 「客户端先出现、动画再盖上去」的反向顺序。
    _mark('window_bg_hidden')
    # 1.9.10：开场动画要等「内容真的就绪」再淡出。loadFinished 只代表 HTML 解析完，
    # 此刻照片还没从本地库取回、画廊仍是空的，直接淡出会露出空画廊（用户反馈）。
    # 判定：本地库握手完成 且（库为空 或 画廊已渲染出卡片）；15s 硬兜底绝不挡界面。
    _splash_state = {'done': False}

    def _reveal():
        # 1.9.31：此刻才把客户端从后台拉起 —— 此前它一直未显示，屏幕上只有开场动画。
        # seam 用 splash 收拢出的那条线的屏幕纵坐标，保证「从那条线展开」严丝合缝。
        w.show()
        w.raise_()
        w.activateWindow()
        app.processEvents()
        LightTransition(w, seam=_splash_state.get('seam')).start()

    def _finish_splash():
        if _splash_state['done']:
            return
        _splash_state['done'] = True
        try:
            _ready_timer.stop()
        except Exception:
            pass
        # 1.9.28：握手就绪（__libHandshake + 画廊卡片已渲染）后再收拢成线，
        # 结束即从中线展开客户端；15s 硬兜底同样走这条路，绝不挡界面。
        # 1.9.31：先记下 splash 收拢成线时的屏幕纵坐标（此刻 splash 还活着），
        # 供 LightTransition 把 seam 对到同一条线上。
        try:
            _splash_state['seam'] = splash.line_global_y()
        except Exception:
            _splash_state['seam'] = None
        splash.begin_exit(_reveal)

    READY_JS = (
        "(function(){"
        "if(window.__libHandshake!==true) return 0;"
        "var n=(typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:0;"
        "if(n===0) return 1;"
        "return document.querySelectorAll('.g-item').length>0?1:0;"
        "})()"
    )

    def _check_ready():
        if _splash_state['done']:
            return
        try:
            w.view.page().runJavaScript(
                READY_JS, lambda v: _finish_splash() if v == 1 else None)
        except Exception:
            _finish_splash()

    _ready_timer = QTimer()
    _ready_timer.setInterval(300)
    _ready_timer.timeout.connect(_check_ready)
    _ready_timer.start()
    QTimer.singleShot(15000, _finish_splash)
    # 页面加载里程碑（libInfo 补推已在 MainWindow 内部接线）
    return app.exec()


if __name__ == '__main__':
    sys.exit(main())

# -*- coding: utf-8 -*-
r"""桌面集成（Windows 正式软件该有的行为）—— 以 mixin 形式挂到 MainWindow。

包含：
*   **单实例**：第二次启动唤起已有窗口（QLocalServer/QLocalSocket）
*   **窗口几何记忆**：位置与大小持久化到 QSettings
*   **拖拽导入**：把文件夹/照片拖进窗口即增量入库
*   **系统托盘**：关闭最小化到托盘、托盘菜单（显示/同步/设置/退出）、气泡通知
*   **任务栏进度**：ctypes 调 ITaskbarList3（PySide6 6.x 没有 QtWinExtras）
*   **原生设置对话框**：同步间隔 / 局域网访问 / 关闭行为 / 库文件与端口
*   **最近导入**：文件菜单里列出已登记的根目录，点击即增量同步
*   **日志**：~/.lightalbum/lightalbum.log

之所以做成 mixin 而不是把代码堆进 app.py：这些能力彼此独立，
放在单独文件里既好读也好测（可以在无 GUI 的场景下单独验证纯逻辑部分）。
"""
from __future__ import annotations

import os
import threading
import time

from PySide6.QtCore import QSettings, Qt, QTimer
from PySide6.QtGui import QAction, QColor, QIcon, QKeySequence, QPixmap
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QLineEdit,
    QMenu, QMessageBox, QSpinBox, QSystemTrayIcon, QWidget,
)

from .core.home import data_home

LOG_PATH = os.path.join(data_home(), 'lightalbum.log')
IPC_NAME = 'lightalbum-single'


def log(msg):
    """写一行日志；失败静默 —— 日志绝不能反过来影响主流程。"""
    try:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        if os.path.exists(LOG_PATH) and os.path.getsize(LOG_PATH) > 1_000_000:
            with open(LOG_PATH, 'w', encoding='utf-8'):
                pass                       # 超过 1MB 直接截断，不搞轮转
        with open(LOG_PATH, 'a', encoding='utf-8') as f:
            f.write('%s\t%s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), msg))
    except Exception:
        pass


class TaskbarProgress:
    """Windows 任务栏进度条（ITaskbarList3 的 ctypes 封装）。

    PySide6 6.x 没有 QtWinExtras，只能自己调 COM。任何一步失败都静默降级，
    绝不让进度条把主流程拖垮。
    """

    TBPF_NOPROGRESS = 0x0
    TBPF_INDETERMINATE = 0x1
    TBPF_NORMAL = 0x2
    TBPF_ERROR = 0x4
    TBPF_PAUSED = 0x8

    def __init__(self, hwnd=0):
        self.hwnd = int(hwnd or 0)
        self.ptr = None
        if os.name != 'nt' or not self.hwnd:
            return
        try:
            import ctypes
            from ctypes import wintypes

            class GUID(ctypes.Structure):
                _fields_ = [('d1', wintypes.DWORD), ('d2', wintypes.WORD),
                            ('d3', wintypes.WORD), ('d4', ctypes.c_byte * 8)]

            clsid = GUID(0x56FDF344, 0xFD6D, 0x11d0,
                         (ctypes.c_byte * 8)(*[0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90]))
            iid = GUID(0xEA1AFB91, 0x9E28, 0x4B86,
                       (ctypes.c_byte * 8)(*[0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF]))
            ctypes.windll.ole32.CoInitialize(None)
            ptr = ctypes.c_void_p()
            rc = ctypes.windll.ole32.CoCreateInstance(
                ctypes.byref(clsid), None, 1, ctypes.byref(iid), ctypes.byref(ptr))
            if rc == 0 and ptr.value:
                self.ptr = ptr
        except Exception:
            self.ptr = None

    def _fn(self, index, *argtypes):
        import ctypes
        vtable = ctypes.cast(self.ptr, ctypes.POINTER(ctypes.c_void_p))[0]
        addr = ctypes.cast(vtable, ctypes.POINTER(ctypes.c_void_p))[index]
        return ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, *argtypes)(addr)

    def set_state(self, state):
        if not self.ptr:
            return
        try:
            import ctypes
            self._fn(3, ctypes.c_void_p, ctypes.c_int)(
                self.ptr, ctypes.c_void_p(self.hwnd), int(state))
        except Exception:
            pass

    def set_value(self, done, total):
        if not self.ptr:
            return
        try:
            import ctypes
            self._fn(4, ctypes.c_void_p, ctypes.c_ulonglong, ctypes.c_ulonglong)(
                self.ptr, ctypes.c_void_p(self.hwnd), int(done), int(total))
        except Exception:
            pass

    def clear(self):
        self.set_state(self.TBPF_NOPROGRESS)


class DesktopIntegration:
    """混入 MainWindow 的桌面能力。宿主需提供：self.bridge / self.view /
    self.lib_server / self.status() / self._teardown_all()。"""

    # ---------------------------------------------------------- 安装
    def desktop_init(self):
        self.settings = QSettings('lightalbum', 'LightAlbum')
        self._quitting = False
        self._was_syncing = False
        self._restore_geometry()
        self.setAcceptDrops(True)
        self._build_tray()
        self.taskbar = TaskbarProgress(self.winId())
        self._sync_timer = QTimer(self)
        self._sync_timer.timeout.connect(self.poll_sync)
        self._sync_timer.start(2000)
        self._ipc = None
        try:
            self._start_single_instance_server()
        except Exception as e:
            log('single-instance server failed: %s' % e)
        log('desktop init done')

    # ---------------------------------------------------------- 单实例
    @staticmethod
    def notify_existing_instance(ipc_name=IPC_NAME):
        """已有实例在跑？通知它显示窗口并等待回执。

        必须收到对方 'ok' 才算「旧实例活着」——若对方已卡死（GUI 线程卡住、
        无法处理连接），这里拿不到回执就返回 False，让本次启动自建实例，
        避免被僵尸实例永久顶住（「关掉后台后打不开第二次」）。
        """
        try:
            sock = QLocalSocket()
            sock.connectToServer(ipc_name)
            if not sock.waitForConnected(600):
                return False
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
            return False

    def _start_single_instance_server(self, ipc_name=IPC_NAME):
        QLocalServer.removeServer(ipc_name)
        self._ipc = QLocalServer(self)
        # 先接好信号再 listen：避免 listen 与 connect 之间有连接进来被漏掉
        self._ipc.newConnection.connect(self._on_ipc)
        if not self._ipc.listen(ipc_name):
            log('single-instance listen failed: %s' % self._ipc.errorString())

    def _on_ipc(self):
        """有新连接就立刻唤起窗口，并回执 'ok'。

        关键：不能只依赖 conn.readyRead —— 客户端连上后马上 write('show')，
        数据往往在 readyRead 信号接好之前就已进入缓冲，Qt 不会补发该信号，
        于是窗口永远不弹（「第二次打不开」的根因）。这里直接唤起。
        """
        try:
            while self._ipc.hasPendingConnections():
                conn = self._ipc.nextPendingConnection()
                if not conn:
                    break
                conn.disconnected.connect(conn.deleteLater)
                try:
                    conn.readAll()          # 丢弃请求内容（内容无所谓）
                    conn.write(b'ok')       # 回执：新实例据此判断「旧实例活着」
                    conn.flush()
                except Exception:
                    pass
            self.show_from_tray()
        except Exception as e:
            log('ipc handle failed: %s' % e)

    # ---------------------------------------------------------- 几何
    def _restore_geometry(self):
        geo = self.settings.value('geometry')
        if geo is not None:
            try:
                self.restoreGeometry(geo)
            except Exception:
                pass

    def _save_geometry(self):
        try:
            self.settings.setValue('geometry', self.saveGeometry())
            self.settings.sync()
        except Exception:
            pass

    # ---------------------------------------------------------- 托盘
    def _build_tray(self):
        self.tray = None
        try:
            if not QSystemTrayIcon.isSystemTrayAvailable():
                return
            icon = self.windowIcon()
            if icon.isNull():
                pm = QPixmap(32, 32)
                pm.fill(QColor(24, 35, 47))
                icon = QIcon(pm)
            self.tray = QSystemTrayIcon(icon, self)
            self.tray.setToolTip(self.windowTitle() or '光影集')
            menu = QMenu()
            menu.addAction('显示主窗口', self.show_from_tray)
            menu.addAction('立即同步新照片', lambda: self.sync_now(notify=True))
            menu.addSeparator()
            menu.addAction('设置…', self.open_settings)
            menu.addAction('退出', self.quit_app)
            self.tray.setContextMenu(menu)
            self.tray.activated.connect(self._on_tray_activated)
            self.tray.show()
        except Exception as e:
            log('tray failed: %s' % e)
            self.tray = None

    def _on_tray_activated(self, reason):
        if reason in (QSystemTrayIcon.ActivationReason.DoubleClick,
                      QSystemTrayIcon.ActivationReason.Trigger):
            self.show_from_tray()

    def show_from_tray(self):
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def quit_app(self):
        self._quitting = True
        # 窗口若已隐藏（托盘态），Qt 的 close() 不派发 closeEvent，不能只靠它收尾
        self._shutdown()
        self.close()
        self._final_exit()

    def notify(self, title, body):
        try:
            if self.tray:
                self.tray.showMessage(title, body,
                                      QSystemTrayIcon.MessageIcon.Information, 4000)
        except Exception:
            pass
        log('%s: %s' % (title, body))

    # ---------------------------------------------------------- 拖拽导入
    def dragEnterEvent(self, ev):
        try:
            if ev.mimeData().hasUrls():
                ev.acceptProposedAction()
                return
        except Exception:
            pass
        super().dragEnterEvent(ev)

    def dropEvent(self, ev):
        paths = []
        try:
            paths = [u.toLocalFile() for u in ev.mimeData().urls() if u.isLocalFile()]
        except Exception:
            pass
        self.import_paths(paths)
        ev.acceptProposedAction()

    def import_paths(self, paths):
        """目录直接同步；文件则同步其所在目录（增量，代价很小）。"""
        dirs = []
        for p in paths or []:
            if not p:
                continue
            if os.path.isdir(p):
                dirs.append(p)
            elif os.path.isfile(p):
                d = os.path.dirname(p)
                if d and d not in dirs:
                    dirs.append(d)
        if not dirs:
            self.status('拖入的内容里没有可导入的目录或照片')
            return []
        if not getattr(self, 'lib_server', None):
            self.status('本地库服务未启动，无法导入')
            return []
        for d in dirs:
            self.lib_server.sync.add_root(d)
        self.status('正在导入 %d 个目录…' % len(dirs))
        log('import: %s' % '; '.join(dirs[:5]))
        threading.Thread(target=self._sync_thread, args=(dirs, True),
                         daemon=True).start()
        self.refresh_recent()
        return dirs

    def _sync_thread(self, dirs, notify=False):
        lib = getattr(self, 'lib_server', None)
        if not lib:
            return
        ok = True
        try:
            for d in dirs:
                lib.sync.sync(d)
        except Exception as e:
            ok = False
            log('sync failed: %s' % e)
            if notify:
                self.notify('导入失败', str(e)[:140])
        if notify and ok:
            self.notify('导入完成', '本地库现有 %d 幅作品' % lib.lib.count())
        try:
            # 线程安全推送（内部会排队回 GUI 线程；不能直接 runJavaScript）
            self.push_result('libSynced', {})
        except Exception:
            pass

    def sync_now(self, notify=False):
        lib = getattr(self, 'lib_server', None)
        if not lib:
            self.status('本地库服务未启动')
            return
        self.status('正在增量同步…')
        threading.Thread(target=lambda: self._sync_thread(lib.sync.roots(), notify),
                         daemon=True).start()

    # ---------------------------------------------------------- 状态轮询
    def poll_sync(self):
        try:
            lib = getattr(self, 'lib_server', None)
            if not lib:
                return
            st = lib.sync.status
            running = bool(st.get('running'))
            if running:
                total = st.get('total') or 0
                done = st.get('done') or 0
                if total:
                    self.taskbar.set_state(TaskbarProgress.TBPF_NORMAL)
                    self.taskbar.set_value(done, total)
                else:
                    self.taskbar.set_state(TaskbarProgress.TBPF_INDETERMINATE)
                self.setWindowTitle('%s — 同步中 %d/%d' % (self._app_name(), done, total))
            elif self._was_syncing:
                self.taskbar.clear()
                self.setWindowTitle(self._app_name())
                self.status('同步完成：%d 幅' % lib.lib.count())
            self._was_syncing = running
        except Exception:
            pass

    def _app_name(self):
        from .app import APP_NAME
        return APP_NAME

    # ---------------------------------------------------------- 设置
    def open_settings(self):
        lib = getattr(self, 'lib_server', None)
        if not lib:
            QMessageBox.warning(self, self._app_name(), '本地库服务未启动，设置不可用。')
            return
        dlg = QDialog(self)
        dlg.setWindowTitle('设置')
        form = QFormLayout(dlg)

        interval = QSpinBox(dlg)
        interval.setRange(1, 240)
        interval.setSuffix(' 分钟')
        interval.setValue(int(max(1, round(lib.sync.interval / 60.0))))
        form.addRow('后台同步间隔', interval)

        lan = QCheckBox('手机 / 网页版可通过局域网连接本机资料库', dlg)
        lan.setChecked(str(self.settings.value('lanAccess', 'false')) == 'true')
        form.addRow('局域网访问', lan)

        closed = QComboBox(dlg)
        closed.addItems(['最小化到托盘（后台继续同步）', '直接退出'])
        closed.setCurrentIndex(0 if str(self.settings.value('closeToTray', 'true')) == 'true' else 1)
        form.addRow('关闭窗口时', closed)

        dbline = QLineEdit(lib.lib.db_path, dlg)
        dbline.setReadOnly(True)
        form.addRow('资料库文件', dbline)

        portline = QLineEdit(str(lib.port), dlg)
        portline.setReadOnly(True)
        form.addRow('本机服务端口', portline)

        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel, dlg)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        form.addRow(bb)

        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        lib.sync.interval = float(max(60, interval.value() * 60))
        self.settings.setValue('lanAccess', 'true' if lan.isChecked() else 'false')
        self.settings.setValue('closeToTray', 'true' if closed.currentIndex() == 0 else 'false')
        self.settings.sync()
        log('settings: interval=%ss lan=%s' % (lib.sync.interval, lan.isChecked()))
        self.status('设置已保存（局域网访问改动需重启客户端生效）')

    # ---------------------------------------------------------- 最近导入
    def refresh_recent(self):
        menu = getattr(self, '_recent_menu', None)
        if menu is None:
            return
        try:
            menu.clear()
            roots = self.lib_server.sync.roots() if getattr(self, 'lib_server', None) else []
            if not roots:
                a = menu.addAction('（还没有导入过目录）')
                a.setEnabled(False)
                return
            for r in roots[:12]:
                name = os.path.basename(r.rstrip('\\/')) or r
                act = QAction('%s  —  %s' % (name, r), self)
                act.triggered.connect(lambda _=False, p=r: self.import_paths([p]))
                menu.addAction(act)
        except Exception:
            pass

    # ---------------------------------------------------------- 关闭
    def closeEvent(self, ev):
        to_tray = self.tray and str(self.settings.value('closeToTray', 'true')) == 'true'
        if to_tray and not self._quitting:
            ev.ignore()
            self.hide()
            if str(self.settings.value('trayHinted', 'false')) != 'true':
                self.settings.setValue('trayHinted', 'true')
                self.notify('仍在后台运行', '关闭窗口不会退出，照片同步继续；托盘图标可重新打开。')
            return
        self._shutdown()
        # 关键：不能用 super().closeEvent —— MainWindow(QMainWindow, DesktopIntegration)
        # 的 MRO 中 DesktopIntegration 之后只剩 object，super() 会抛
        # AttributeError: 'super' object has no attribute 'closeEvent'，
        # 于是窗口关不掉、进程退不出（「后台不能退出」的根因）。显式调 Qt 基类实现。
        QWidget.closeEvent(self, ev)
        self._final_exit()

    def _shutdown(self):
        """真正退出的收尾（幂等）：几何 / 定时器 / 任务栏 / 服务与子进程。

        必须独立于 closeEvent：Qt 对**已隐藏**的窗口调用 close() 不会派发 closeEvent，
        托盘态下退出若只依赖 closeEvent，本地服务与子进程就会漏停。
        """
        if getattr(self, '_torn_down', False):
            return
        self._torn_down = True
        try:
            self._save_geometry()
        except Exception:
            pass
        try:
            self._sync_timer.stop()
        except Exception:
            pass
        try:
            if getattr(self, 'taskbar', None):
                self.taskbar.clear()
        except Exception:
            pass
        try:
            self._teardown_all()
        except Exception:
            pass

    def _final_exit(self):
        """兜底确保进程真的退出（并保证收尾已执行）。

        窗口若已隐藏（最小化到托盘），Qt 的 quitOnLastWindowClosed 只认「可见窗口」，
        关闭隐藏窗口不会触发退出 —— 这里显式 quit，保证一定能退出；顺手藏掉托盘图标。
        """
        self._shutdown()
        try:
            if getattr(self, 'tray', None):
                self.tray.hide()
        except Exception:
            pass
        try:
            from PySide6.QtWidgets import QApplication
            app = QApplication.instance()
            if app is not None:
                app.quit()
        except Exception:
            pass

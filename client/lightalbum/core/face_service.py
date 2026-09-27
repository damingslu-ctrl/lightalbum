# -*- coding: utf-8 -*-
"""随客户端自启的本地人脸服务拉起器。

把「手动起 face_server.py」自动化：客户端启动时，自动用**独立的 venv**
进程拉起 `D:\\lightalbum_ai\\face_server.py`（端口 8767）。

为什么是外部进程
----------------
人脸模型（ArcFace ~174MB）跑在 `D:\\lightalbum_ai\\venv_face` 里，
与客户端 venv 隔离：客户端 venv 会被 PyInstaller 整体打包，塞进去会让
exe 变大且打包环境里没有匹配的 onnxruntime/opencv。沿用既有的本地 AI 思路
（classifier / Pixel Cake）：模型跑在外面，客户端只通过 HTTP 调它。

降级原则（绝不因人脸服务影响主程序）
--------------------------------------
* `LIGHTALBUM_FACE_AUTOSTART=0` → 完全不拉起，行为等同以前。
* 找不到 AI 目录 / venv / 脚本 → 静默跳过，UI 上「识别人物」入口不出现。
* 端口 8767 已被健康占用 → 不重复拉起，避免端口冲突。
* 客户端真正退出时，只终止由自己这次拉起的进程；绝不杀别人开的实例。
* 崩溃残留的孤儿子进程：靠 pid 文件认领，下次启动由当前客户端负责清理。

路径 / 端口可经环境变量覆盖：
    LIGHTALBUM_AI_DIR              默认 D:\\lightalbum_ai
    LIGHTALBUM_FACE_PORT           默认 8767
    LIGHTALBUM_FACE_AUTOSTART      默认 1（=0 关闭自启）
"""
from __future__ import annotations

import json
import logging
import os
import signal
import subprocess
import threading
import time
import urllib.error
import urllib.request

log = logging.getLogger('face-service')

DEFAULT_AI_DIR = r'D:\lightalbum_ai'
DEFAULT_PORT = 8767
PROBE_TIMEOUT = 1.5          # 探测端口是否已健康占用
LAUNCH_GRACE = 1.2           # 子进程拉起后，确认它没立刻崩的时间
STOP_TIMEOUT = 8.0           # 退出时终止子进程的最长等待


def _env(name, default):
    v = os.environ.get(name)
    return v.strip() if v else default


def ai_dir():
    return _env('LIGHTALBUM_AI_DIR', DEFAULT_AI_DIR)


def find_paths(base=None):
    """算出 脚本 / python 三件套的绝对路径。缺哪个对应字段为 None。"""
    base = base or ai_dir()
    script = os.path.join(base, 'face_server.py')
    python = os.path.join(base, 'venv_face', 'Scripts', 'python.exe')
    return {
        'base': base,
        'script': script if os.path.isfile(script) else None,
        'python': python if os.path.isfile(python) else None,
    }


def _url(port):
    return 'http://127.0.0.1:%d' % port


def port_occupied(url, timeout=PROBE_TIMEOUT):
    """端口上是否已有服务在响应 /health。返回 (alive, info)。"""
    try:
        with urllib.request.urlopen(url + '/health', timeout=timeout) as r:
            info = json.loads(r.read().decode('utf-8'))
        return True, info
    except Exception:  # noqa: BLE001
        return False, None


def _pid_alive(pid):
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False
    except Exception:  # noqa: BLE001
        return False


class _AdoptedProc:
    """认领（而非 spawn）的进程：只提供 terminate/kill/poll 接口。"""

    def __init__(self, pid):
        self.pid = pid
        self._returncode = None

    def poll(self):
        if self._returncode is not None:
            return self._returncode
        if _pid_alive(self.pid):
            return None
        self._returncode = 0
        return 0

    def terminate(self):
        if self.poll() is not None:
            return
        try:
            os.kill(self.pid, signal.SIGTERM)
        except Exception:  # noqa: BLE001
            pass

    def kill(self):
        self.terminate()


def _pid_path(base):
    return os.path.join(base, '_logs', 'face.pid')


def _write_pid(base, pid, port):
    try:
        d = os.path.join(base, '_logs')
        os.makedirs(d, exist_ok=True)
        with open(_pid_path(base), 'w', encoding='utf-8') as f:
            json.dump({'pid': pid, 'port': port, 'at': time.time()}, f)
    except Exception:  # noqa: BLE001
        pass


def _read_pid(base):
    try:
        p = _pid_path(base)
        if os.path.isfile(p):
            with open(p, 'r', encoding='utf-8') as f:
                return json.load(f)
    except Exception:  # noqa: BLE001
        pass
    return {}


def _adopt_if_orphan(url, base):
    data = _read_pid(base)
    pid = int(data.get('pid', 0))
    if not (pid and _pid_alive(pid)):
        return None
    alive, _ = port_occupied(url)
    if alive:
        return _AdoptedProc(pid)
    return None


def launch(autostart=True):
    """尝试拉起人脸服务。返回 dict 描述结果（含 proc / pid，供退出时清理）。"""
    res = {'launched': False, 'already_running': False, 'adopted': False,
           'error': None, 'url': None, 'pid': None, 'proc': None}
    if not autostart:
        return res

    port = int(_env('LIGHTALBUM_FACE_PORT', str(DEFAULT_PORT)))
    url = _url(port)
    res['url'] = url

    alive, info = port_occupied(url)
    if alive:
        paths = find_paths()
        adopted = _adopt_if_orphan(url, paths['base'])
        if adopted is not None:
            res['adopted'] = True
            res['proc'] = adopted
            res['pid'] = adopted.pid
            res['info'] = info
            log.info('face orphan adopted pid=%s on %s', adopted.pid, url)
            return res
        res['already_running'] = True
        res['info'] = info
        log.info('face already running (external) on %s', url)
        return res

    paths = find_paths()
    if not (paths['script'] and paths['python']):
        res['error'] = '人脸服务组件缺失（脚本或 venv 不存在）'
        log.info('face autostart skipped: %s', res['error'])
        return res

    log_dir = os.path.join(paths['base'], '_logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'face.log')
    try:
        out = open(log_path, 'ab', buffering=0)
    except Exception:  # noqa: BLE001
        out = None

    si = None
    flags = 0
    if hasattr(subprocess, 'STARTUPINFO'):
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = subprocess.SW_HIDE
    if hasattr(subprocess, 'CREATE_NEW_PROCESS_GROUP'):
        flags |= subprocess.CREATE_NEW_PROCESS_GROUP

    try:
        proc = subprocess.Popen(
            [paths['python'], paths['script'], '--port', str(port)],
            env=dict(os.environ), stdout=out, stderr=out, stdin=subprocess.DEVNULL,
            startupinfo=si, creationflags=flags, close_fds=True)
    except Exception as e:  # noqa: BLE001
        res['error'] = '%s: %s' % (type(e).__name__, e)
        log.info('face launch failed: %s', res['error'])
        if out:
            try:
                out.close()
            except Exception:  # noqa: BLE001
                pass
        return res

    time.sleep(LAUNCH_GRACE)
    if proc.poll() is not None:
        res['error'] = ('人脸服务进程启动后立即退出（退出码 %s），'
                        '详见 %s' % (proc.returncode, log_path))
        log.info(res['error'])
        return res

    res['launched'] = True
    res['proc'] = proc
    res['pid'] = proc.pid
    _write_pid(paths['base'], proc.pid, port)
    log.info('face launched pid=%s url=%s', proc.pid, url)
    return res


def stop(proc, timeout=STOP_TIMEOUT):
    """终止由本客户端拉起 / 认领的人脸进程（仅当我们自己持有 proc 时才调）。"""
    if proc is None:
        return
    try:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=timeout)
            except Exception:  # noqa: BLE001
                try:
                    proc.kill()
                except Exception:  # noqa: BLE001
                    pass
    except Exception as e:  # noqa: BLE001
        log.info('face stop failed: %s' % e)

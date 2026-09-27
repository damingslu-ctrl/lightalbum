# -*- coding: utf-8 -*-
r"""随客户端自启的本地 Pixel Cake 修图服务拉起器（端口 8765）。

与 classifier_service 同构：客户端启动时在后台线程把本地 Pixel Cake 服务拉起来，
让「智能修图」真正用上本地重模型（LaMa / SAM2 / mediapipe），而不是退回本地 Pillow 管线。

为什么是「拉起外部进程」而不是内嵌
----------------------------------
Pixel Cake 依赖 torch(+CUDA) / transformers / SAM2，整体数 GB，且需要独立 venv
（`D:\lightalbum_ai\venv_cake`）。绝不塞进客户端 exe。

降级原则（绝不因修图服务影响主程序）
--------------------------------------
* `LIGHTALBUM_CAKE_AUTOSTART=0` → 完全不拉起。
* 找不到 venv / backend → 静默跳过，UI 上「执行」徽章显示本地 Pillow 管线即可。
* 端口 8765 已被健康占用 → 不重复拉起（可能用户自己开的）。
* 客户端**真正退出**时，只终止由自己这次拉起的进程；不杀别人开的实例。
* 崩溃残留的孤儿子进程：靠 pid 文件认领，下次启动由当前客户端负责清理。

模型与缓存
----------
重模型（LaMa ~450MB / SAM2 0.4–2.4GB）**首次用到该功能时**由 Pixel Cake 自行下载；
这里把 HF_HOME / TORCH_HOME 一并指到 D 盘（`LIGHTALBUM_AI_DIR`），避免权重落到 C 盘。
若模型已缓存且想彻底离线，可设 `LIGHTALBUM_CAKE_OFFLINE=1`。

路径 / 端口可经环境变量覆盖：
    LIGHTALBUM_AI_DIR            默认 D:\lightalbum_ai
    LIGHTALBUM_CAKE_PORT         默认 8765
    LIGHTALBUM_CAKE_AUTOSTART    默认 1（=0 关闭自启）
"""
from __future__ import annotations

import json
import logging
import os
import signal
import subprocess
import time
import urllib.request

log = logging.getLogger('cake-service')

DEFAULT_AI_DIR = r'D:\lightalbum_ai'
DEFAULT_PORT = 8765
PROBE_TIMEOUT = 1.5
LAUNCH_GRACE = 2.0           # uvicorn + torch import 稍慢，给足确认不崩的时间
STOP_TIMEOUT = 10.0


def _env(name, default):
    v = os.environ.get(name)
    return v.strip() if v else default


def ai_dir():
    return _env('LIGHTALBUM_AI_DIR', DEFAULT_AI_DIR)


def find_paths(base=None):
    """算出 venv / backend 的绝对路径。缺哪个对应字段为 None。"""
    base = base or ai_dir()
    python = os.path.join(base, 'venv_cake', 'Scripts', 'python.exe')
    backend = os.path.join(base, 'pixel-cake', 'backend')
    main_py = os.path.join(backend, 'main.py')
    return {
        'base': base,
        'python': python if os.path.isfile(python) else None,
        'backend': backend if os.path.isfile(main_py) else None,
        'main': main_py if os.path.isfile(main_py) else None,
    }


def _url(port):
    return 'http://127.0.0.1:%d' % port


def port_occupied(url, timeout=PROBE_TIMEOUT):
    """端口上是否已有 Pixel Cake 在响应 /health。返回 (alive, info)。"""
    try:
        with urllib.request.urlopen(url + '/health', timeout=timeout) as r:
            info = json.loads(r.read().decode('utf-8'))
        return True, info
    except Exception:
        return False, None


def _pid_alive(pid):
    if not pid:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False
    except Exception:
        return False


class _AdoptedProc:
    """认领（而非 spawn）的进程：只提供 terminate/kill/poll，供退出时清理。"""

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
        except Exception:
            pass

    def kill(self):
        self.terminate()


def _pid_path(base):
    return os.path.join(base, '_logs', 'pixelcake.pid')


def _write_pid(base, pid, port):
    try:
        d = os.path.join(base, '_logs')
        os.makedirs(d, exist_ok=True)
        with open(_pid_path(base), 'w', encoding='utf-8') as f:
            json.dump({'pid': pid, 'port': port, 'at': time.time()}, f)
    except Exception:
        pass


def _read_pid(base):
    try:
        p = _pid_path(base)
        if os.path.isfile(p):
            with open(p, 'r', encoding='utf-8') as f:
                return json.load(f)
    except Exception:
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


def child_env(paths, port):
    """给 Pixel Cake 子进程准备环境：模型缓存指到 D 盘，可选离线。"""
    env = dict(os.environ)
    base = paths.get('base') or ai_dir()
    hf = os.path.join(base, 'hf_cache')
    torch_home = os.path.join(base, 'torch_cache')
    env['HF_HOME'] = hf
    env['HF_HUB_CACHE'] = hf
    env['HF_HUB_DISABLE_SYMLINKS_WARNING'] = '1'
    env['TORCH_HOME'] = torch_home
    env['PYTHONUNBUFFERED'] = '1'
    env['CAKE_PORT'] = str(port)
    # 重模型是「首次用时才下」——默认**不**离线，否则首下会失败；确需离线再显式打开
    if os.environ.get('LIGHTALBUM_CAKE_OFFLINE') == '1':
        env['HF_HUB_OFFLINE'] = '1'
        env['TRANSFORMERS_OFFLINE'] = '1'
    return env


def launch(autostart=True):
    """尝试拉起 Pixel Cake。返回 dict（含 proc / pid，供退出时清理）。静默降级，不抛。"""
    res = {'launched': False, 'already_running': False, 'adopted': False,
           'error': None, 'url': None, 'pid': None, 'proc': None}
    if not autostart:
        return res

    port = int(_env('LIGHTALBUM_CAKE_PORT', str(DEFAULT_PORT)))
    url = _url(port)
    res['url'] = url

    # 1) 端口已被健康占用 → 认领上一轮残留的孤儿子进程
    alive, info = port_occupied(url)
    if alive:
        paths = find_paths()
        adopted = _adopt_if_orphan(url, paths['base'])
        if adopted is not None:
            res['adopted'] = True
            res['proc'] = adopted
            res['pid'] = adopted.pid
            res['info'] = info
            log.info('pixelcake orphan adopted pid=%s on %s', adopted.pid, url)
            return res
        res['already_running'] = True
        res['info'] = info
        log.info('pixelcake already running (external) on %s', url)
        return res

    # 2) 组件路径；缺失则降级
    paths = find_paths()
    if not (paths['python'] and paths['backend']):
        res['error'] = 'Pixel Cake 组件缺失（venv_cake 或 backend 不存在）'
        log.info('cake autostart skipped: %s', res['error'])
        return res

    env = child_env(paths, port)
    log_dir = os.path.join(paths['base'], '_logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'pixelcake.log')
    try:
        out = open(log_path, 'ab', buffering=0)
    except Exception:
        out = None

    si = None
    flags = 0
    if hasattr(subprocess, 'STARTUPINFO'):
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = subprocess.SW_HIDE
    if hasattr(subprocess, 'CREATE_NEW_PROCESS_GROUP'):
        flags |= subprocess.CREATE_NEW_PROCESS_GROUP

    # 必须 cwd=backend 再起 uvicorn（README 实测：--app-dir 会报 Could not import "main"）
    try:
        proc = subprocess.Popen(
            [paths['python'], '-m', 'uvicorn', 'main:app',
             '--host', '127.0.0.1', '--port', str(port)],
            cwd=paths['backend'], env=env, stdout=out, stderr=out,
            stdin=subprocess.DEVNULL, startupinfo=si, creationflags=flags,
            close_fds=True)
    except Exception as e:  # noqa: BLE001
        res['error'] = '%s: %s' % (type(e).__name__, e)
        log.info('cake launch failed: %s', res['error'])
        if out:
            try:
                out.close()
            except Exception:
                pass
        return res

    time.sleep(LAUNCH_GRACE)
    if proc.poll() is not None:
        res['error'] = ('Pixel Cake 进程启动后立即退出（退出码 %s），详见 %s'
                        % (proc.returncode, log_path))
        log.info(res['error'])
        return res

    res['launched'] = True
    res['proc'] = proc
    res['pid'] = proc.pid
    _write_pid(paths['base'], proc.pid, port)
    log.info('pixelcake launched pid=%s url=%s', proc.pid, url)
    return res


def stop(proc, timeout=STOP_TIMEOUT):
    """终止由本客户端拉起 / 认领的 Pixel Cake 进程。"""
    if proc is None:
        return
    try:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=timeout)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
    except Exception as e:  # noqa: BLE001
        log.info('pixelcake stop failed: %s' % e)

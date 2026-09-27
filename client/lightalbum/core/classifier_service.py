# -*- coding: utf-8 -*-
"""随客户端自启的本地 CLIP 分类服务拉起器。

把「手动双击 start_classifier.bat」自动化：客户端（LightAlbum.exe）启动时，
自动用**独立的 venv** 进程拉起 `D:\\lightalbum_ai\\classifier_server.py`（端口 8766）。

为什么是「拉起外部进程」而不是内嵌
----------------------------------
torch + CLIP 约 2GB+，且依赖 CUDA。客户端 venv 会被 PyInstaller 整体打进 exe，
塞进去会让 exe 变成几个 GB、且打包环境里也没有匹配的 CUDA 运行时。
所以沿用既有的本地 AI 思路（Pixel Cake / Jev）：模型跑在外面的
`D:\\lightalbum_ai\\venv_cake` 里，客户端只通过 HTTP 调它（见 classifier.py）。

降级原则（绝不因分类服务影响主程序）
--------------------------------------
* `LIGHTALBUM_CLASSIFIER_AUTOSTART=0` → 完全不拉起，行为等同以前。
* 找不到 AI 目录 / venv / 脚本 → 静默跳过，UI 上「AI 分类」入口不出现。
* 端口 8766 已被（我们或其他程序）健康占用 → 不重复拉起，避免端口冲突。
* 客户端**真正退出**时，只终止由自己这次拉起的进程；绝不杀别人开的实例。
* 崩溃残留的孤儿子进程：靠 pid 文件认领，下次启动由当前客户端负责清理。

路径 / 端口可经环境变量覆盖：
    LIGHTALBUM_AI_DIR              默认 D:\\lightalbum_ai
    LIGHTALBUM_CLASSIFIER_PORT     默认 8766
    LIGHTALBUM_CLASSIFIER_AUTOSTART 默认 1（=0 关闭自启）
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

log = logging.getLogger('classifier-service')

DEFAULT_AI_DIR = r'D:\lightalbum_ai'
DEFAULT_PORT = 8766
PROBE_TIMEOUT = 1.5          # 探测端口是否已健康占用
LAUNCH_GRACE = 1.2           # 子进程拉起后，确认它没立刻崩的时间
STOP_TIMEOUT = 8.0           # 退出时终止子进程的最长等待


def _env(name, default):
    v = os.environ.get(name)
    return v.strip() if v else default


def ai_dir():
    return _env('LIGHTALBUM_AI_DIR', DEFAULT_AI_DIR)


def find_paths(base=None):
    """算出 脚本 / python / hf_cache 三件套的绝对路径。缺哪个对应字段为 None。"""
    base = base or ai_dir()
    script = os.path.join(base, 'classifier_server.py')
    python = os.path.join(base, 'venv_cake', 'Scripts', 'python.exe')
    hf_cache = os.path.join(base, 'hf_cache')
    return {
        'base': base,
        'script': script if os.path.isfile(script) else None,
        'python': python if os.path.isfile(python) else None,
        'hf_cache': hf_cache if os.path.isdir(hf_cache) else None,
    }


def _url(port):
    return 'http://127.0.0.1:%d' % port


def port_occupied(url, timeout=PROBE_TIMEOUT):
    """端口上是否已有服务在响应 /health。返回 (alive, info)。"""
    try:
        with urllib.request.urlopen(url + '/health', timeout=timeout) as r:
            info = json.loads(r.read().decode('utf-8'))
        return True, info
    except Exception:
        return False, None


def _pid_alive(pid):
    """跨平台判断进程是否存活（不发送信号，仅探测）。"""
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
    """认领（而非 spawn）的进程：只提供 terminate/kill/poll 接口，供退出时清理。"""

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
    return os.path.join(base, '_logs', 'classifier.pid')


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
    """若 pid 文件指向的进程还活着、且端口健康，认领它（由本客户端负责退出清理）。"""
    data = _read_pid(base)
    pid = int(data.get('pid', 0))
    if not (pid and _pid_alive(pid)):
        return None
    alive, _ = port_occupied(url)
    if alive:
        return _AdoptedProc(pid)
    return None


def _hf_cache_has_models(hf):
    """hf_cache 里是否已有模型快照 —— 决定要不要强制离线。"""
    try:
        if not hf or not os.path.isdir(hf):
            return False
        for name in os.listdir(hf):
            if name.startswith('models--') and os.path.isdir(os.path.join(hf, name)):
                return True
    except Exception:
        pass
    return False


def child_env(paths, port):
    """分类子进程环境：HF_HOME 指向 D 盘缓存；模型已缓存则强制离线。

    为什么要离线：模型下到本地后，若不禁止联网，transformers 每次启动仍会向
    HuggingFace 做一次在线校验（用户不希望「模型在本地却还联网」）。模型齐全时
    直接离线即可；确需临时联网可设 LIGHTALBUM_CLASSIFIER_ONLINE=1。
    """
    env = dict(os.environ)
    hf = paths.get('hf_cache') or os.path.join(paths.get('base') or ai_dir(), 'hf_cache')
    env['HF_HOME'] = hf
    env['HF_HUB_CACHE'] = hf
    env['HF_HUB_DISABLE_SYMLINKS_WARNING'] = '1'
    env['CLASSIFIER_PORT'] = str(port)
    if _hf_cache_has_models(hf) and os.environ.get('LIGHTALBUM_CLASSIFIER_ONLINE') != '1':
        env['HF_HUB_OFFLINE'] = '1'
        env['TRANSFORMERS_OFFLINE'] = '1'
    return env


def launch(autostart=True):
    """尝试拉起分类服务。返回 dict 描述结果（含 proc / pid，供退出时清理）。

    静默降级：任何异常都兜住，绝不抛出影响主程序。
    """
    res = {'launched': False, 'already_running': False, 'adopted': False,
           'error': None, 'url': None, 'pid': None, 'proc': None}
    if not autostart:
        return res

    port = int(_env('LIGHTALBUM_CLASSIFIER_PORT', str(DEFAULT_PORT)))
    url = _url(port)
    res['url'] = url

    # 1) 端口已被健康占用 → 看看是不是我们上一轮残留的孤儿子进程，是则认领
    alive, info = port_occupied(url)
    if alive:
        paths = find_paths()
        adopted = _adopt_if_orphan(url, paths['base'])
        if adopted is not None:
            res['adopted'] = True
            res['proc'] = adopted
            res['pid'] = adopted.pid
            res['info'] = info
            log.info('classifier orphan adopted pid=%s on %s', adopted.pid, url)
            return res
        res['already_running'] = True
        res['info'] = info
        log.info('classifier already running (external) on %s', url)
        return res

    # 2) 组装组件路径；缺失则降级
    paths = find_paths()
    if not (paths['script'] and paths['python']):
        res['error'] = '分类服务组件缺失（脚本或 venv 不存在）'
        log.info('classifier autostart skipped: %s', res['error'])
        return res

    # 3) 子进程环境：模型缓存指到 D 盘；已缓存则强制离线（见 child_env）
    env = child_env(paths, port)

    log_dir = os.path.join(paths['base'], '_logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'classifier.log')
    try:
        out = open(log_path, 'ab', buffering=0)
    except Exception:
        out = None

    si = None
    flags = 0
    if hasattr(subprocess, 'STARTUPINFO'):
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = subprocess.SW_HIDE            # 不弹控制台窗口
    # 独立进程组：客户端即使从控制台被 Ctrl+C，也不连带杀掉分类服务
    if hasattr(subprocess, 'CREATE_NEW_PROCESS_GROUP'):
        flags |= subprocess.CREATE_NEW_PROCESS_GROUP

    try:
        proc = subprocess.Popen(
            [paths['python'], paths['script'], '--port', str(port)],
            env=env, stdout=out, stderr=out, stdin=subprocess.DEVNULL,
            startupinfo=si, creationflags=flags, close_fds=True)
    except Exception as e:  # noqa: BLE001
        res['error'] = '%s: %s' % (type(e).__name__, e)
        log.info('classifier launch failed: %s', res['error'])
        if out:
            try:
                out.close()
            except Exception:
                pass
        return res

    # 4) 确认子进程没立刻死（uvicorn 起来需要一点时间，这里只验证进程活着）
    time.sleep(LAUNCH_GRACE)
    if proc.poll() is not None:
        res['error'] = ('分类服务进程启动后立即退出（退出码 %s），'
                        '详见 %s' % (proc.returncode, log_path))
        log.info(res['error'])
        return res

    res['launched'] = True
    res['proc'] = proc
    res['pid'] = proc.pid
    _write_pid(paths['base'], proc.pid, port)
    log.info('classifier launched pid=%s url=%s', proc.pid, url)
    return res


def stop(proc, timeout=STOP_TIMEOUT):
    """终止由本客户端拉起 / 认领的分类进程（仅当我们自己持有 proc 时才调）。"""
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
        log.info('classifier stop failed: %s' % e)

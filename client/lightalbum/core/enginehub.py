# -*- coding: utf-8 -*-
"""光影集 · 引擎化与统一调度（Engine-Hub，ADR-0012）。

设计约束（见 ADR-0012 §3）：
  - 本模块**零 Qt 顶层依赖**：仅用 stdlib（concurrent.futures / enum /
    threading / time / uuid）。Qt 仅在 app 侧通过 `EngineHub.set_executor()`
    注入 QThread 执行器，保证回归可在无 GUI 环境下实跑。
  - 引擎层用「适配层（adapter）」包住既有公共 API，不改动被包模块的内部逻辑。
  - VRAM 分时预算是**协作式**约束：重模型引擎须自觉 request/release，Hub 不杀进程。

契约：
  Engine 子类声明 name/kind/vram_weight/max_jobs/priority + 生命周期
  (init/start/stop) + 可观测 (health/metrics) + task_<name> 能力方法；
  Hub.submit(engine, task, *a, **kw) 返回 JobHandle。
"""
from __future__ import annotations

import concurrent.futures as _cf
import enum
import threading
import time
import uuid
from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, List, Optional


# ---------------------------------------------------------------- 状态枚举
class EngineState(enum.Enum):
    UNINIT = 'uninit'
    READY = 'ready'
    BUSY = 'busy'
    STOPPED = 'stopped'
    ERROR = 'error'


class JobState(enum.Enum):
    QUEUED = 'queued'
    RUNNING = 'running'
    DONE = 'done'
    FAILED = 'failed'
    CANCELLED = 'cancelled'


# ---------------------------------------------------------------- 作业句柄
class JobHandle:
    """一次引擎作业的句柄：状态 / 进度 / 结果 / 取消 / 回调。线程安全。"""

    def __init__(self, job_id: str, engine_name: str, task: str,
                 meta: Optional[dict] = None):
        self.id = job_id
        self.engine = engine_name
        self.task = task
        self.meta = meta or {}
        self.state = JobState.QUEUED
        self.progress = 0.0
        self.result: Any = None
        self.error: Any = None
        self._lock = threading.RLock()
        self._done_evt = threading.Event()
        self._progress_cbs: List[Callable[['JobHandle'], None]] = []
        self._done_cbs: List[Callable[['JobHandle'], None]] = []
        self._cancelled = False
        self._fut = None  # 由 Hub 填充（供未来强制取消扩展）

    # -- 进度（由 Worker 调用）--
    def set_progress(self, value: float) -> None:
        with self._lock:
            self.progress = max(0.0, min(1.0, float(value)))
        for cb in list(self._progress_cbs):
            try:
                cb(self)
            except Exception:
                pass

    # -- 完成（由 Worker 调用）--
    def _finish(self, ok: bool, result: Any = None, error: Any = None) -> None:
        with self._lock:
            self.result = result
            self.error = error
            if ok:
                self.state = JobState.DONE
                self.progress = 1.0
            else:
                self.state = JobState.FAILED
        self._done_evt.set()
        for cb in list(self._done_cbs):
            try:
                cb(self)
            except Exception:
                pass

    # -- 取消 --
    def cancel(self) -> bool:
        with self._lock:
            if self.state in (JobState.DONE, JobState.FAILED, JobState.CANCELLED):
                return False
            self._cancelled = True
            self.state = JobState.CANCELLED
        self._done_evt.set()
        for cb in list(self._done_cbs):
            try:
                cb(self)
            except Exception:
                pass
        return True

    def cancelled(self) -> bool:
        with self._lock:
            return self._cancelled

    # -- 回调登记 --
    def on_progress(self, cb: Callable[['JobHandle'], None]) -> 'JobHandle':
        self._progress_cbs.append(cb)
        return self

    def on_done(self, cb: Callable[['JobHandle'], None]) -> 'JobHandle':
        self._done_cbs.append(cb)
        return self

    # -- 同步等待 --
    def wait(self, timeout: Optional[float] = None) -> bool:
        self._done_evt.wait(timeout)
        with self._lock:
            return self.state in (JobState.DONE, JobState.FAILED, JobState.CANCELLED)


# ---------------------------------------------------------------- 引擎基类
class Engine(ABC):
    """所有引擎的契约基类。子类覆盖 task_<name> 方法实现具体能力。"""

    name: str = 'engine'
    kind: str = 'native'          # 'native' | 'view'
    vram_weight: float = 0.0      # 0..1，>0 表示占用显存预算（协作式）
    max_jobs: int = 1             # 该引擎并发作业上限
    priority: int = 0             # 越高越优先（预算不足时排队序）

    def __init__(self) -> None:
        self._state = EngineState.UNINIT

    # ---- 生命周期 ----
    def init(self) -> None:
        """惰性初始化（建连接 / 加载等）。默认无操作。"""
        self._state = EngineState.READY

    def start(self) -> None:
        if self._state in (EngineState.UNINIT, EngineState.STOPPED):
            self._state = EngineState.READY

    def stop(self) -> None:
        self._state = EngineState.STOPPED

    @property
    def state(self) -> EngineState:
        return self._state

    # ---- 可观测 ----
    def health(self) -> Dict[str, Any]:
        return {'name': self.name, 'kind': self.kind,
                'state': self._state.value, 'vram_weight': self.vram_weight}

    def metrics(self) -> Dict[str, Any]:
        return {'name': self.name, 'jobs': 0}

    def tasks(self) -> List[str]:
        return [m[5:] for m in dir(self)
                if m.startswith('task_') and callable(getattr(self, m, None))]

    # ---- 派发 ----
    def run(self, task: str, args: tuple, kwargs: dict) -> Any:
        method = getattr(self, 'task_' + task, None)
        if method is None or not callable(method):
            raise NotImplementedError(
                'engine %s 不支持任务 %s' % (self.name, task))
        return method(*args, **kwargs)


class ViewEngine(Engine):
    """视图引擎标记基类（kind='view'）。

    地图 / 画廊 / 灯箱等纯渲染与帧预算治理，状态在 web 页 JS 侧，
    经 `lightalbum://` 桥接注册到同一 Hub（详见 ADR-0012 §2/§5 后续）。
    Python 侧仅作注册、health 与 metrics 桥接，不实现 task_*。
    """

    kind = 'view'

    def health(self) -> Dict[str, Any]:
        return {'name': self.name, 'kind': 'view',
                'state': self._state.value,
                'note': 'registered via lightalbum:// bridge'}


# ---------------------------------------------------------------- 调度器
class EngineHub:
    """统一引擎调度器：注册表 + 并发预算 + 6GB 显存分时优先级 + 聚合 health/metrics。"""

    def __init__(self, vram_budget: float = 1.0, max_workers: int = 8) -> None:
        self._engines: Dict[str, Engine] = {}
        self._order: List[str] = []
        self.vram_budget = float(vram_budget)
        self._vram_used = 0.0
        self._vram_lock = threading.Lock()
        self._lock = threading.RLock()
        self._active: Dict[str, int] = {}           # engine_name -> 进行中作业数
        self._running: Dict[str, JobHandle] = {}    # job_id -> handle
        self._executor = _cf.ThreadPoolExecutor(max_workers=max_workers)
        self._owns_executor = True
        self._started = False

    # ---- 注册表 ----
    def register(self, engine: Engine) -> 'EngineHub':
        with self._lock:
            if engine.name in self._engines:
                try:
                    self._engines[engine.name].stop()
                except Exception:
                    pass
            self._engines[engine.name] = engine
            if engine.name not in self._order:
                self._order.append(engine.name)
        try:
            engine.init()
        except Exception:
            engine._state = EngineState.ERROR
        return self

    def unregister(self, name: str) -> None:
        with self._lock:
            eng = self._engines.pop(name, None)
            if name in self._order:
                self._order.remove(name)
        if eng is not None:
            try:
                eng.stop()
            except Exception:
                pass

    def get(self, name: str) -> Optional[Engine]:
        with self._lock:
            return self._engines.get(name)

    def engines(self) -> List[str]:
        with self._lock:
            return list(self._order)

    # ---- 可插拔执行器（Qt 侧换 QThread 执行器）----
    def set_executor(self, executor: Any, owns: bool = True) -> None:
        with self._lock:
            if self._owns_executor and self._executor is not None:
                try:
                    self._executor.shutdown(wait=False)
                except Exception:
                    pass
            self._executor = executor
            self._owns_executor = owns

    # ---- 6GB 显存分时预算（协作式）----
    def request_vram(self, weight: float) -> bool:
        if weight <= 0:
            return True
        with self._vram_lock:
            if self._vram_used + weight <= self.vram_budget + 1e-9:
                self._vram_used += weight
                return True
            return False

    def release_vram(self, weight: float) -> None:
        if weight <= 0:
            return
        with self._vram_lock:
            self._vram_used = max(0.0, self._vram_used - weight)

    # ---- 容量判定 / 占用 / 释放（VRAM + 每引擎并发）----
    def _capacity_ok(self, eng: Engine, weight: float) -> bool:
        if weight > 0 and self._vram_used + weight > self.vram_budget + 1e-9:
            return False
        if self._active.get(eng.name, 0) >= eng.max_jobs:
            return False
        return True

    def _acquire(self, eng: Engine, weight: float) -> None:
        if weight > 0:
            self._vram_used += weight
        self._active[eng.name] = self._active.get(eng.name, 0) + 1

    def _release(self, eng: Engine, weight: float) -> None:
        if weight > 0:
            with self._vram_lock:
                self._vram_used = max(0.0, self._vram_used - weight)
        self._active[eng.name] = max(0, self._active.get(eng.name, 0) - 1)

    # ---- 提交作业 ----
    def submit(self, name: str, task: str, *args: Any,
               priority: Optional[int] = None, **kwargs: Any) -> JobHandle:
        eng = self.get(name)
        if eng is None:
            raise KeyError('未注册的引擎: %s' % name)
        handle = JobHandle(
            job_id=uuid.uuid4().hex[:12], engine_name=name, task=task,
            meta={'priority': priority if priority is not None else eng.priority})
        self._running[handle.id] = handle
        weight = eng.vram_weight
        if self._capacity_ok(eng, weight):
            self._acquire(eng, weight)
            self._dispatch(eng, handle, weight, args, kwargs)
        else:
            handle.state = JobState.QUEUED
            threading.Thread(
                target=self._wait_capacity,
                args=(eng, handle, weight, args, kwargs),
                name='hub-wait-%s' % name, daemon=True).start()
        return handle

    def _wait_capacity(self, eng: Engine, handle: JobHandle,
                       weight: float, args: tuple, kwargs: dict) -> None:
        while not self._capacity_ok(eng, weight):
            if handle.cancelled():
                self._running.pop(handle.id, None)   # 防泄漏：取消即摘除
                return
            time.sleep(0.1)
        if handle.cancelled():
            self._running.pop(handle.id, None)
            return
        self._acquire(eng, weight)
        self._dispatch(eng, handle, weight, args, kwargs)

    def _dispatch(self, eng: Engine, handle: JobHandle,
                  weight: float, args: tuple, kwargs: dict) -> None:
        handle.state = JobState.RUNNING
        try:
            fut = self._executor.submit(
                self._worker, eng, handle, weight, args, kwargs)
            handle._fut = fut
        except Exception as e:  # 提交即失败（如执行器已关）
            self._release(eng, weight)
            self._running.pop(handle.id, None)
            handle._finish(False, error=e)

    def _worker(self, eng: Engine, handle: JobHandle,
                weight: float, args: tuple, kwargs: dict) -> None:
        try:
            result = eng.run(handle.task, args, kwargs)
            handle._finish(True, result=result)
        except Exception as e:
            handle._finish(False, error=e)
        finally:
            self._release(eng, weight)
            self._running.pop(handle.id, None)

    # ---- 聚合 ----
    def health(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {}
        with self._lock:
            names = list(self._order)
        for n in names:
            eng = self._engines.get(n)
            try:
                out[n] = eng.health() if eng else {'state': 'missing'}
            except Exception as e:  # noqa: BLE001
                out[n] = {'state': 'error', 'error': str(e)}
        out['_vram_used'] = round(self._vram_used, 3)
        out['_vram_budget'] = self.vram_budget
        return out

    def metrics(self) -> Dict[str, Any]:
        out: Dict[str, Any] = {}
        with self._lock:
            names = list(self._order)
        for n in names:
            eng = self._engines.get(n)
            try:
                out[n] = eng.metrics() if eng else {}
            except Exception:
                out[n] = {}
        out['_running_jobs'] = len(self._running)
        out['_active'] = dict(self._active)
        return out

    # ---- 启停 ----
    def start_all(self) -> None:
        with self._lock:
            names = list(self._order)
        for n in names:
            eng = self._engines.get(n)
            if eng is None:
                continue
            try:
                if eng._state in (EngineState.UNINIT, EngineState.STOPPED):
                    eng.start()
            except Exception:
                eng._state = EngineState.ERROR
        self._started = True

    def stop_all(self) -> None:
        with self._lock:
            names = list(self._order)
        for n in names:
            eng = self._engines.get(n)
            if eng is None:
                continue
            try:
                eng.stop()
            except Exception:
                pass
        self._started = False


__all__ = [
    'EngineState', 'JobState', 'JobHandle', 'Engine', 'ViewEngine', 'EngineHub',
]

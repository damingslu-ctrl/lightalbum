# -*- coding: utf-8 -*-
r"""本地库服务：把客户端的作品库通过 HTTP 暴露给页面（含网页版）。

设计要点
────────
*   **只绑回环地址**（127.0.0.1）—— 与「智能修图只走本地」的约定一致，
    不对局域网暴露。
*   **CORS 全开**：网页版在普通浏览器里打开时也能读到本地库
    （这正是「客户端 = 数据服务，网页版直接调用」的实现方式）。
*   **缩略图经 HTTP 提供**：浏览器出于安全策略拿不到 file:// 图片，
    走 http://127.0.0.1:8787/api/thumb/xxx.jpg 才能显示。
*   **开启时增量同步一次**：启动后等首屏 IO 空闲再做一次比对，把磁盘上
    新增的照片入库；之后**不再周期轮询**，新增照片由用户手动导入
    （拖拽进窗口 / 「立即同步」按钮）触发。

端点
    GET  /api/health              服务状态 + 库内照片数
    GET  /api/photos?limit&offset 全部照片（页面字段契约，img 为缩略图 URL）
    GET  /api/thumb/<key>         缩略图 JPEG
    GET  /api/sources             来源列表（root + 张数）
    GET  /api/status              同步进度
    POST /api/sync                {root, src} 立即增量同步一个目录
    POST /api/sync-all            同步全部已登记目录
    POST /api/roots               {roots:[...]} 登记要自动同步的目录
    POST /api/rating              {path, rating} 设置星级（0-5，0=取消）
    POST /api/frame               {path, preset, opts, mode} 相框/EXIF 水印预览与导出
"""
from __future__ import annotations

import hashlib
import json
import mimetypes
import os
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .library import Library

DEFAULT_PORT = 8787
AUTO_SYNC_INTERVAL = 300.0          # 后台自动增量同步间隔（秒）


class SyncManager:
    """负责「登记了哪些目录」+ 后台增量同步 + 进度状态。"""

    def __init__(self, lib, interval=AUTO_SYNC_INTERVAL):
        self.lib = lib
        self.interval = interval
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread = None
        self.status = {'running': False, 'root': '', 'done': 0, 'total': 0,
                       'last_result': None, 'last_time': 0, 'last_error': ''}

    # ---- 登记
    def roots(self):
        try:
            return json.loads(self.lib.meta_get('roots', '[]')) or []
        except Exception:
            return []

    def set_roots(self, roots):
        keep = []
        for r in roots or []:
            if r and os.path.isdir(r) and r not in keep:
                keep.append(os.path.abspath(r))
        self.lib.meta_set('roots', json.dumps(keep))
        return keep

    def add_root(self, root):
        rs = self.roots()
        if root and os.path.isdir(root) and root not in rs:
            rs.append(os.path.abspath(root))
            self.set_roots(rs)
        return self.roots()

    # ---- 同步
    def _progress(self, done, total, name):
        with self._lock:
            self.status['done'] = done
            self.status['total'] = total

    def sync(self, root, src='native', want_thumbs=True):
        with self._lock:
            self.status.update(running=True, root=root, done=0, total=0, last_error='')
        try:
            res = self.lib.sync_root(root, src=src, want_thumbs=want_thumbs,
                                     progress=self._progress)
            # 仅当有实际新增/更新才推进 last_time —— 否则页面 watchLibrarySync
            # 会在「每轮无变化也跑一次」的自动同步后整库重拉，造成周期性卡顿。
            changed = bool((res or {}).get('added') or (res or {}).get('updated'))
            with self._lock:
                if changed:
                    self.status['last_time'] = time.time()
                self.status['running'] = False
                self.status['last_result'] = res
            return res
        except Exception as e:
            with self._lock:
                self.status.update(running=False, last_error=str(e))
            raise

    def sync_all(self, want_thumbs=True):
        out = {}
        for r in self.roots():
            if not os.path.isdir(r):
                continue
            try:
                out[r] = self.sync(r, want_thumbs=want_thumbs)
            except Exception as e:
                out[r] = {'error': str(e)}
        return out

    # ---- 后台循环
    def start_background(self):
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, name='lib-autosync',
                                        daemon=True)
        self._thread.start()

    def stop_background(self):
        self._stop.set()

    def _loop(self):
        # 仅「开启时拉一次」：启动后等一会儿（别和界面首屏抢 IO），做一次
        # 增量同步把磁盘上新增的照片入库；之后**不再周期轮询**——
        # 新增照片由用户手动导入（拖拽进窗口 / 「立即同步」）触发，
        # 不再每 5 分钟无谓地扫全盘、也不再驱动页面整库重拉。
        if self._stop.wait(20):
            return
        try:
            if not self.status.get('running'):
                self.sync_all()
        except Exception:
            pass
        # 一次性：不进入 while 循环，关闭周期性自动同步。


class _Handler(BaseHTTPRequestHandler):
    server_version = 'LightAlbum/1.0'
    lib = None
    sync = None
    files = None

    # ---- 工具
    def _send(self, code, body, ctype='application/json; charset=utf-8', extra=None):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        elif isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Access-Control-Allow-Methods', 'GET,POST,OPTIONS')
        self.send_header('Cache-Control', 'no-store')
        for k, v in (extra or {}).items():
            self.send_header(k, str(v))
        self.end_headers()
        try:
            self.wfile.write(body)
        except Exception:
            pass

    def log_message(self, *a):        # 静音，避免刷屏
        pass

    _CHUNK = 256 * 1024

    def _send_file(self, fp, ctype, cache_control='no-store'):
        """流式返回磁盘文件，带 ETag / Last-Modified，命中 If-None-Match 回 304。

        为什么不复用 _send：
          ① 大图（10–30MB 原图）一次性 f.read() 进内存，响应慢且吃内存；
          ② 图片端点若也写 no-store，浏览器无法缓存 —— 画廊每次重渲染（筛选/
             搜索/排序/滚动）或反复开同一张灯箱，都要重新下载整张图。
        缩略图文件名 = sha1(mtime|size)（内容寻址），可安全长缓存 + immutable。
        """
        try:
            st = os.stat(fp)
        except Exception:
            self._send(404, {'detail': 'not found'})
            return
        etag = '"%x-%x"' % (int(st.st_mtime), int(st.st_size))
        last_mod = self.date_time_string(int(st.st_mtime))
        inm = self.headers.get('If-None-Match') or ''
        ims = self.headers.get('If-Modified-Since') or ''
        hit_inm = bool(inm) and any(t.strip() == etag for t in inm.split(','))
        hit_ims = (not inm) and ims and ims == last_mod
        if hit_inm or hit_ims:
            self.send_response(304)
            self.send_header('ETag', etag)
            self.send_header('Cache-Control', cache_control)
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            return
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(st.st_size))
        self.send_header('ETag', etag)
        self.send_header('Last-Modified', last_mod)
        self.send_header('Cache-Control', cache_control)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        try:
            with open(fp, 'rb') as f:
                while True:
                    chunk = f.read(self._CHUNK)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        except Exception:
            pass

    def token_ok(self, remote_host, query):
        """访问控制：回环地址永远放行；其他（局域网）必须带正确 token。

        默认只绑 127.0.0.1，开「局域网访问」时才绑 0.0.0.0 —— 那时就必须
        有 token，否则同网段任何人都能读到整库（照片是隐私）。
        """
        if not self.token:
            return True
        rh = (remote_host or '').split('%')[0]
        if rh in ('127.0.0.1', 'localhost', '::1', ''):
            return True
        given = (query.get('t') or [''])[0] or (self.headers.get('X-LightAlbum-Token') or '')
        return given == self.token

    def _serve_static(self, name, ctype):
        """从资源目录提供静态文件（只允许白名单名，防穿越）。"""
        if '/' in name or '\\' in name or '..' in name:
            self._send(404, {'detail': 'Not found'})
            return
        base = getattr(self, 'static_dir', '') or ''
        fp = os.path.join(base, name) if base else ''
        if not fp or not os.path.isfile(fp):
            self._send(404, {'detail': 'Not found'})
            return
        try:
            with open(fp, 'rb') as f:
                data = f.read()
            self._send(200, data, ctype)
        except Exception:
            self._send(500, {'detail': 'read error'})

    def do_OPTIONS(self):
        self._send(204, b'')

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        path, q = u.path, urllib.parse.parse_qs(u.query)
        if not self.token_ok(self.client_address[0] if self.client_address else '', q):
            self._send(401, {'detail': '需要访问令牌'})
            return
        # ---- 静态页面：让客户端用 http:// 加载页面本身
        # 为什么必须这么做：file:// 页面的 origin 是 opaque，
        # Chromium 直接禁止它 fetch http://127.0.0.1（实测 "Failed to fetch"）。
        # 页面走 http://127.0.0.1:<port>/ 就与 API 同源，且网页版也能复用。
        if path in ('/', '/index.html'):
            self._serve_static('index.html', 'text/html; charset=utf-8')
            return
        if path == '/native_bridge.js':
            self._serve_static('native_bridge.js',
                               'application/javascript; charset=utf-8')
            return
        if path == '/api/health':
            self._send(200, {'status': 'ok', 'photos': self.lib.count(),
                             'roots': len(self.sync.roots()),
                             'version': '1.0'})
        elif path == '/api/photos':
            limit = int((q.get('limit') or ['0'])[0] or 0)
            offset = int((q.get('offset') or ['0'])[0] or 0)
            photos = self.lib.photos(limit=limit or None, offset=offset)
            self._send(200, {'photos': photos, 'total': self.lib.count(),
                             'offset': offset})
        elif path == '/api/selection':
            self._send(200, {'paths': list(self._sel)})
        elif path == '/api/photo_exif':
            p = (q.get('path') or [''])[0] or ''
            row = self.lib.photo_by_path(p) if p else None
            if row:
                self._send(200, {'ok': True, 'path': p, 'exif': row})
            else:
                self._send(404, {'ok': False, 'detail': 'not found'})
        elif path.startswith('/api/thumb/'):
            key = urllib.parse.unquote(path[len('/api/thumb/'):])
            fp = self.lib.thumb_file(key)
            if not fp:
                self._send(404, {'detail': 'not found'})
                return
            # 内容寻址（文件名含 sha1(mtime|size)）→ 可长缓存，浏览器直接命中不回头
            self._send_file(fp, 'image/jpeg',
                            'public, max-age=31536000, immutable')
        elif path.startswith('/api/original/'):
            # 原图（HD）：按 thumb key 反查本库登记的源文件，流式返回
            key = urllib.parse.unquote(path[len('/api/original/'):])
            fp = self.lib.original_file_by_id(key)
            if not fp:
                self._send(404, {'detail': 'not found'})
                return
            ctype = mimetypes.guess_type(fp)[0] or 'application/octet-stream'
            # no-cache（不是 no-store）：允许浏览器存下来，但每次带 ETag 复验 ——
            # 未改动就 304 空体，省掉整张原图（10–30MB）的传输与内存读入。
            self._send_file(fp, ctype, 'no-cache')
        elif path.startswith('/api/file/'):
            # 只提供客户端**主动登记**过的文件（如修图产物）：token 反查，
            # 杜绝「按路径读任意文件」——本服务带 CORS *，不能开这个口子。
            key = urllib.parse.unquote(path[len('/api/file/'):])
            fp = (self.files or {}).get(key)
            if not fp or not os.path.isfile(fp):
                self._send(404, {'detail': 'not found'})
                return
            ctype = mimetypes.guess_type(fp)[0] or 'application/octet-stream'
            # 修图产物可能被同名覆盖 → 用 no-cache + ETag 复验，未变即 304
            self._send_file(fp, ctype, 'no-cache')
        elif path == '/api/sources':
            self._send(200, {'sources': self.lib.sources(),
                             'roots': self.sync.roots()})
        elif path == '/api/status':
            self._send(200, dict(self.sync.status))
        # ---- AI 分类
        elif path == '/api/categories':
            # 每类多少张 + 分类服务是否可用（服务没起时 UI 要隐藏入口）
            self._send(200, {'stats': self.lib.category_stats(),
                             'classifier': self.classify.probe()})
        elif path == '/api/classify/status':
            self._send(200, dict(self.classify.status))
        # ---- 人物识别
        elif path == '/api/persons':
            # person_stats() 返回 [{'person','count'},...] 列表；前端用 Object.entries
            # 需要 {name: count} 字典，这里转换一次（保持 person_stats 自身签名不变）。
            ps = {r['person']: r['count'] for r in self.lib.person_stats()}
            self._send(200, {'stats': ps,
                             'face': self.face.probe(),
                             'status': dict(self.face.status)})
        elif path == '/api/face/status':
            self._send(200, dict(self.face.status))
        # ---- 隐私库
        elif path == '/api/enhance/status':
            # 探测本机 AI 美图服务（enhance_server :8768）是否在线，供前端显示状态
            from . import enhance as E
            import urllib.request as _ur_req
            _ok = False
            _info = None
            try:
                with _ur_req.urlopen('http://127.0.0.1:%d/health' % E.ENHANCE_PORT,
                                     timeout=2) as _r:
                    _info = json.loads(_r.read().decode('utf-8'))
                    _ok = True
            except Exception:
                _info = None
            self._send(200, {'available': _ok, 'port': E.ENHANCE_PORT,
                             'service': _info})
        elif path == '/api/vault/status':
            v = self.vault
            self._send(200, {
                'configured': v.has_password(),
                'unlocked': v.unlocked(),
                'count': len(v.items()),
                'seconds_left': v.seconds_left(),
            })
        elif path == '/api/vault/list':
            v = self.vault
            if not v.unlocked():
                self._send(403, {'detail': '隐私库已锁定'})
                return
            v.touch()
            self._send(200, {'items': v.items()})
        elif path.startswith('/api/vault/thumb/'):
            # 解锁后才返回缩略图；未解锁一律 403（关键：绝不泄漏任何内容）
            v = self.vault
            if not v.unlocked():
                self._send(403, {'detail': '隐私库已锁定'})
                return
            try:
                item_id = int(path[len('/api/vault/thumb/'):])
            except ValueError:
                self._send(400, {'detail': 'bad id'})
                return
            try:
                data = v.thumb_bytes(item_id)
                v.touch()
            except Exception as e:  # noqa: BLE001
                self._send(500, {'detail': str(e)})
                return
            if not data:
                self._send(404, {'detail': 'no thumb'})
                return
            self._send(200, data, 'image/jpeg')
        else:
            self._send(404, {'detail': 'Not found'})

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        try:
            n = int(self.headers.get('Content-Length') or 0)
            payload = json.loads(self.rfile.read(n).decode('utf-8')) if n else {}
        except Exception:
            payload = {}
        u2 = urllib.parse.urlparse(self.path)
        q2 = urllib.parse.parse_qs(u2.query)
        if not self.token_ok(self.client_address[0] if self.client_address else '', q2):
            self._send(401, {'detail': '需要访问令牌'})
            return
        if u.path == '/api/native':
            # 页面走 http 后，lightalbum:// 信标会被 Chromium 拦截，
            # 所以原生动作改由这里转发给客户端（回调里再排队回 GUI 线程）。
            handler = getattr(self, 'native_handler', None)
            if not handler:
                self._send(503, {'detail': '客户端未就绪'})
                return
            action = payload.get('action') or ''
            if not action:
                self._send(400, {'detail': '缺少 action'})
                return
            try:
                handler(action, payload.get('payload') or {})
                self._send(200, {'ok': True, 'action': action})
            except Exception as e:
                self._send(500, {'detail': str(e)})
        elif u.path == '/api/frame':
            # 相框/EXIF 水印：{path, preset, opts, mode:'preview'|'apply'}
            from . import frame as frame_mod
            fp = payload.get('path') or ''
            if not os.path.isfile(fp):
                self._send(400, {'ok': False, 'detail': '文件不存在'})
                return
            preset = payload.get('preset') or 'bar-light'
            opts = payload.get('opts') or {}
            mode = payload.get('mode') or 'preview'
            try:
                if mode == 'apply':
                    out = frame_mod.apply_frame(fp, preset, dict(opts))
                    root = os.path.dirname(out)
                    threading.Thread(target=lambda: self._safe_sync(root),
                                     daemon=True).start()
                    self._send(200, {'ok': True, 'out': out})
                else:
                    b64 = frame_mod.preview_frame(fp, preset, dict(opts),
                                                  max_edge=int(opts.get('previewEdge') or 900))
                    self._send(200, {'ok': True, 'img': b64})
            except Exception as e:  # noqa: BLE001 - 渲染失败要回给页面而不是挂掉
                self._send(500, {'ok': False, 'detail': str(e)})
        elif u.path == '/api/rating':
            # 星级评分：{path, rating}（0-5，0 = 取消评级）
            try:
                r = int(payload.get('rating') or 0)
            except (TypeError, ValueError):
                self._send(400, {'ok': False, 'detail': 'rating 必须是 0-5 的整数'})
                return
            ok = self.lib.set_rating(payload.get('path') or '', r)
            self._send(200, {'ok': bool(ok),
                             'rating': max(0, min(5, r)),
                             'stats': self.lib.rating_stats()})
        elif u.path == '/api/sync':
            root = payload.get('root') or ''
            if not os.path.isdir(root):
                self._send(400, {'detail': '目录不存在'})
                return
            self.sync.add_root(root)
            threading.Thread(target=lambda: self._safe_sync(root),
                             daemon=True).start()
            self._send(202, {'started': True, 'root': root})
        elif u.path == '/api/sync-all':
            threading.Thread(target=self._safe_sync_all, daemon=True).start()
            self._send(202, {'started': True})
        elif u.path == '/api/roots':
            kept = self.sync.set_roots(payload.get('roots') or [])
            self._send(200, {'roots': kept})
        # ---- AI 分类
        elif u.path == '/api/classify':
            res = self.classify.start(limit=payload.get('limit'))
            code = 202 if res.get('ok') else 409
            self._send(code, res)
        elif u.path == '/api/classify/abort':
            self._send(200, self.classify.abort())
        # ---- 人物识别
        elif u.path == '/api/face/scan':
            res = self.face.start(limit=payload.get('limit'))
            code = 202 if res.get('ok') else 409
            self._send(code, res)
        elif u.path == '/api/face/abort':
            self._send(200, self.face.abort())
        elif u.path == '/api/select':
            # 双向选择桥：MCP 设置 / 客户端自己设置都走这里（写入共享选择集）
            paths = payload.get('paths') or []
            if not isinstance(paths, list):
                paths = [str(paths)]
            self._sel = [str(x) for x in paths]
            self._send(200, {'ok': True, 'count': len(self._sel)})
        # ---- 选片（Culling，能力吸收自 ai-photo-manager，MIT）
        elif u.path == '/api/cull':
            from . import culling as C
            scope = payload.get('scope') or 'all'
            opts = dict(payload.get('options') or {})
            try:
                opts['top_n'] = int(opts.get('top_n') or 10)
            except Exception:
                opts['top_n'] = 10
            photos = self.lib.photos()
            by_path = {ph['path']: ph for ph in photos if ph.get('path')}
            if scope == 'selection':
                want = set(self._sel or [])
                paths = [p for p in want if p in by_path and os.path.isfile(p)]
            elif isinstance(payload.get('paths'), list) and payload['paths']:
                paths = [p for p in payload['paths'] if p and os.path.isfile(p)]
            else:
                paths = [p['path'] for p in photos
                         if p.get('path') and os.path.isfile(p['path'])]
            if not paths:
                self._send(400, {'ok': False, 'detail': '没有可分析的照片'})
                return
            try:
                candidates, rejected = C.filter_photos(paths, opts, None)
            except Exception as e:  # noqa: BLE001
                self._send(500, {'ok': False, 'detail': '选片失败：%s' % e})
                return
            dup_groups = C.collect_dedup_groups(rejected)
            top = C.select_top(candidates, n=opts['top_n'])
            self._last_cull = (candidates, rejected)   # 供 /api/cull/export 复用

            def _thumb(rec):
                ph = by_path.get(rec.get('path') or '')
                return (ph or {}).get('img') or ''

            def _info(rec):
                ph = by_path.get(rec.get('path') or '')
                return {
                    'path': rec.get('path') or '',
                    'name': os.path.basename(rec.get('path') or ''),
                    'rel': (ph or {}).get('rel') or '',
                    'cat': (ph or {}).get('cat') or '',
                    'thumb': _thumb(rec),
                    'reason': rec.get('reject_reason'),
                    'blur': rec.get('blur'),
                    'is_blurry': rec.get('is_blurry'),
                    'total_score': rec.get('total_score'),
                    'grade': (rec.get('score') or {}).get('grade'),
                }

            resp = {
                'ok': True, 'scope': scope, 'total': len(paths),
                'kept': len(candidates), 'rejected': len(rejected),
                'dup_groups': [
                    {'kept': g['kept'],
                     'kept_name': os.path.basename(g['kept']),
                     'kept_thumb': _thumb({'path': g['kept']}),
                     'reason': g['reason'],
                     'similar': [_info({'path': s, 'reject_reason': g['reason']})
                                 for s in g['similar']]}
                    for g in dup_groups
                ],
                'top': [_info(r) for r in top],
                'rejected_detail': [_info(r) for r in rejected[:300]],
                'options': opts,
            }
            self._send(200, resp)
        elif u.path == '/api/cull/export':
            from . import culling as C
            last = getattr(self, '_last_cull', None)
            if not last:
                self._send(400, {'ok': False, 'detail': '请先运行选片'})
                return
            candidates, rejected = last
            try:
                top_n = int(payload.get('top_n') or 10)
            except Exception:
                top_n = 10
            top = C.select_top(candidates, n=top_n)
            top_paths = {r['path'] for r in top}
            rest = [r for r in candidates if r['path'] not in top_paths]
            out_dir = payload.get('output_dir') or ''
            if not out_dir:
                base = (candidates[0]['path'] if candidates
                        else os.path.expanduser('~'))
                base = os.path.dirname(base)
                out_dir = os.path.join(base, '选片整理-%s'
                                       % time.strftime('%Y%m%d-%H%M%S'))
            out_dir = os.path.abspath(out_dir)
            try:
                C.export_organized(top, rest, rejected, out_dir, archive_all=True)
                self._send(200, {'ok': True, 'output_dir': out_dir,
                                 'count': len(top) + len(rest)})
            except Exception as e:  # noqa: BLE001
                self._send(500, {'ok': False, 'detail': '导出失败：%s' % e})
        # ---- AI 美图（Enhance，吸收自 AfterFrame / 本地 3DLUT 路由）
        elif u.path == '/api/enhance':
            from . import enhance as E
            import urllib.request as _ur
            import urllib.error as _ue
            fp = payload.get('path') or ''
            if not fp or not os.path.isfile(fp):
                self._send(400, {'ok': False, 'detail': '文件不存在'})
                return
            route = payload.get('route') or None
            cat = payload.get('cat') or None
            r = route or E.route_for(cat)
            if not r:
                self._send(200, {'ok': False, 'skipped': True,
                                 'reason': '照片未分类（cat 缺失或「其他」），'
                                           '按数据诚实铁律不猜测路由'})
                return
            # 像素级单色检测：真·黑白照片即使被分类为人像，也改走 landscape 色调增强
            # （避免 portrait 上色器给黑白照染色），结果再转回灰度保持黑白。
            mono = False
            try:
                from PIL import Image
                import numpy as _npy
                _im = Image.open(fp).convert('RGB')
                _arr = _npy.asarray(_im, dtype=_npy.float32)
                mono = E.is_monochrome(_arr)
            except Exception:
                mono = False
            if mono and r == 'portrait':
                r = 'landscape'
            try:
                strength = float(payload.get('strength') or E.DEFAULT_STRENGTH)
            except Exception:
                strength = E.DEFAULT_STRENGTH
            url = 'http://127.0.0.1:%d/enhance' % E.ENHANCE_PORT
            body = json.dumps({'path': fp, 'route': r, 'strength': strength,
                               'variant': 'paired'}).encode('utf-8')
            try:
                req = _ur.Request(url, data=body,
                                  headers={'Content-Type': 'application/json'},
                                  method='POST')
                with _ur.urlopen(req, timeout=180) as rr:
                    srv = json.loads(rr.read().decode('utf-8'))
                srv.setdefault('route', r)
                _err = srv.get('error') or srv.get('detail')
                if not _err:
                    srv['ok'] = True
                    out = srv.get('output') or srv.get('out_path') or ''
                    if mono and out and os.path.isfile(out):
                        try:
                            from PIL import Image as _PIL
                            _g = _PIL.open(out).convert('L').convert('RGB')
                            _gpath = out.rsplit('.', 1)[0] + '.mono.png'
                            _g.save(_gpath)
                            out = _gpath
                        except Exception:
                            pass
                    if out and os.path.isfile(out):
                        srv['output'] = out
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)
                srv['mono'] = bool(mono)
                self._send(200, srv)
            except _ue.HTTPError as he:
                try:
                    bd = json.loads(he.read().decode('utf-8'))
                except Exception:
                    bd = {'detail': str(he)}
                self._send(he.code, {'ok': False, 'detail': bd})
            except Exception as e:  # noqa: BLE001
                self._send(200, {'ok': False, 'available': False,
                                 'reason': 'AI 美图服务（enhance_server :%d）未启动或不可达：%s'
                                          % (E.ENHANCE_PORT, e)})
        # ---- 隐私库
        elif u.path == '/api/vault/setup':
            pw = payload.get('password') or ''
            try:
                self.vault.set_password(pw)
                self._send(200, {'ok': True})
            except Exception as e:  # noqa: BLE001 - 口令太短等业务错误要回给用户
                self._send(400, {'detail': str(e)})
        elif u.path == '/api/vault/unlock':
            try:
                good = self.vault.unlock(payload.get('password') or '')
            except Exception as e:  # noqa: BLE001
                self._send(400, {'detail': str(e)})
                return
            if good:
                self._send(200, {'ok': True})
            else:
                self._send(403, {'ok': False, 'detail': '口令不正确'})
        elif u.path == '/api/vault/lock':
            self.vault.lock()
            self._send(200, {'ok': True})
        elif u.path == '/api/vault/add':
            # 页面只知道 rel（拿不到绝对路径），所以允许传 rel 由服务端反查
            p = payload.get('path') or ''
            if not p:
                p = self.lib.path_by_rel(payload.get('rel') or '',
                                         payload.get('src'))
            if not p or not os.path.isfile(p):
                self._send(400, {'detail': '文件不存在：%s'
                                 % (p or payload.get('rel') or '(空)')})
                return
            try:
                res = self.vault.add(p, name=payload.get('name'))
                self._send(200, res)
            except Exception as e:  # noqa: BLE001
                self._send(400, {'detail': str(e)})
        elif u.path == '/api/vault/remove':
            try:
                res = self.vault.remove(int(payload.get('id') or 0),
                                        payload.get('dest'))
                self._send(200, res)
            except Exception as e:  # noqa: BLE001
                self._send(400, {'detail': str(e)})
        else:
            self._send(404, {'detail': 'Not found'})

    def _safe_sync(self, root):
        try:
            self.sync.sync(root)
        except Exception:
            pass

    def _safe_sync_all(self):
        try:
            self.sync.sync_all()
        except Exception:
            pass


class ClassifyManager:
    """AI 分类：后台线程跑 CLIP 推理 + 进度状态。

    classifier 模块在 _run() 里**延迟导入** —— 顶层 import 会把 lazily-loaded 的
    core 模块提前拉起来，破坏既有「启动毫秒级」的优化。
    """

    def __init__(self, lib, url=None):
        self.lib = lib
        self.url = url or os.environ.get('LIGHTALBUM_CLASSIFIER_URL') or None
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread = None
        self.status = {'running': False, 'done': 0, 'total': 0,
                       'last_result': None, 'last_time': 0, 'last_error': ''}

    # ---- 探测（供 UI 决定要不要显示入口）
    def probe(self):
        from . import classifier as C
        return C.probe(self.url) if self.url else C.probe()

    # ---- 后台分类
    def _progress(self, done, total, name):
        with self._lock:
            self.status['done'] = done
            self.status['total'] = total

    def start(self, limit=None):
        if self.status['running']:
            return {'ok': False, 'error': '分类任务正在运行'}
        self._stop.clear()
        self.status.update({'running': True, 'done': 0, 'total': 0,
                            'last_error': ''})

        def _run():
            from . import classifier as C
            try:
                kw = {'lib': self.lib, 'progress': self._progress,
                      'cancel': self._stop.is_set}
                if self.url:
                    kw['url'] = self.url
                if limit:
                    kw['limit'] = int(limit)
                res = C.classify_library(**kw)
                with self._lock:
                    self.status['last_result'] = res
                    self.status['last_time'] = time.time()
                    if not res.get('ok'):
                        self.status['last_error'] = res.get('error') or '分类失败'
            except Exception as e:  # noqa: BLE001
                with self._lock:
                    self.status['last_error'] = '%s: %s' % (type(e).__name__, e)
            finally:
                with self._lock:
                    self.status['running'] = False

        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()
        return {'ok': True}

    def abort(self):
        self._stop.set()
        return {'ok': True}


class FaceManager:
    """人物识别：后台线程跑本地人脸服务推理 + 聚类 + 进度状态。

    与 ClassifyManager 同构；face 模块在 _run() 里**延迟导入**。
    """

    def __init__(self, lib, url=None):
        self.lib = lib
        self.url = url or os.environ.get('LIGHTALBUM_FACE_URL') or None
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread = None
        self.status = {'running': False, 'done': 0, 'total': 0,
                       'faces': 0, 'persons': 0, 'last_result': None,
                       'last_time': 0, 'last_error': ''}

    def probe(self):
        from . import face as F
        return F.probe(self.url) if self.url else F.probe()

    def _progress(self, done, total, name):
        with self._lock:
            self.status['done'] = done
            self.status['total'] = total

    def start(self, limit=None):
        if self.status['running']:
            return {'ok': False, 'error': '人物识别任务正在运行'}
        self._stop.clear()
        self.status.update({'running': True, 'done': 0, 'total': 0,
                            'faces': 0, 'persons': 0, 'last_error': ''})

        def _run():
            from . import face as F
            try:
                kw = {'lib': self.lib, 'progress': self._progress,
                      'cancel': self._stop.is_set}
                if self.url:
                    kw['url'] = self.url
                if limit:
                    kw['limit'] = int(limit)
                res = F.face_scan_library(**kw)
                with self._lock:
                    self.status['last_result'] = res
                    self.status['last_time'] = time.time()
                    if res.get('ok'):
                        self.status['faces'] = res.get('faces', 0)
                        self.status['persons'] = res.get('persons', 0)
                    else:
                        self.status['last_error'] = res.get('error') or '识别失败'
            except Exception as e:  # noqa: BLE001
                with self._lock:
                    self.status['last_error'] = '%s: %s' % (type(e).__name__, e)
            finally:
                with self._lock:
                    self.status['running'] = False

        self._thread = threading.Thread(target=_run, daemon=True)
        self._thread.start()
        return {'ok': True}

    def abort(self):
        self._stop.set()
        return {'ok': True}


class LibraryServer:
    """本地库 HTTP 服务（守护线程里跑，随客户端启动/退出）。"""

    def __init__(self, lib=None, port=DEFAULT_PORT, autosync=True, db_path=None,
                 static_dir=None, host='127.0.0.1', token=''):
        self.lib = lib or Library(db_path)
        self.sync = SyncManager(self.lib)
        self.classify = ClassifyManager(self.lib)
        self.face = FaceManager(self.lib)
        self._sel = []   # 双向选择桥：MCP / 客户端共享的「当前选择」paths 列表
        self._vault = None          # 延迟创建：import cryptography 有启动成本
        self.port = int(port)
        self.autosync = autosync
        self.static_dir = static_dir or ''
        self.host = host or '127.0.0.1'
        self.token = token or ''
        self.native_handler = None      # 由客户端注入：(action, payload) -> None
        self._files = {}                # token -> 绝对路径（客户端主动放行）
        self._files_lock = threading.Lock()
        self._last_cull = None          # 上次选片结果（供 /api/cull/export 复用，跨请求共享）
        self.httpd = None
        self._thread = None

    def vault(self):
        """延迟创建隐私库 —— cryptography 不是每次启动都用得上，别拖慢启动。"""
        if self._vault is None:
            from .vault import Vault
            self._vault = Vault(self.lib)
        return self._vault

    def register_file(self, path):
        """登记一个允许经 HTTP 取回的本机文件，返回不透明 token。

        本服务带 `Access-Control-Allow-Origin: *`，任何网页都能打这个回环端口，
        因此绝不能提供「按路径取任意文件」的接口。这里只放行客户端**主动登记**
        过的文件（修图产物等），token 也不可反推路径。
        """
        try:
            ap = os.path.abspath(path)
            key = hashlib.sha1(ap.encode('utf-8')).hexdigest()[:20]
            with self._files_lock:
                self._files[key] = ap
            return key
        except Exception:
            return ''

    def file_url(self, path):
        """返回页面可直接用的**相对** URL（按页面同源解析）；失败返回 ''。"""
        try:
            if not path or not os.path.isfile(path):
                return ''
            key = self.register_file(path)
            if not key:
                return ''
            return '/api/file/%s?v=%d' % (key, int(os.path.getmtime(path)))
        except Exception:
            return ''

    def start(self):
        server = self

        class H(_Handler):
            lib = self.lib
            sync = self.sync
            classify = self.classify
            face = self.face
            static_dir = self.static_dir
            token = self.token
            files = self._files
            register_file = server.register_file   # 供 /api/enhance 登记产物预览

            # 双向选择桥：_sel 的事实源在 LibraryServer 上，handler 用 property
            # 转发。不能写 `_sel = self._sel`（类属性快照）——POST 里的
            # `self._sel = [...]` 只会落在临时 handler 实例上，跨请求即丢。
            @property
            def _sel(self):
                return server._sel

            @_sel.setter
            def _sel(self, v):
                server._sel = list(v)

            @property
            def native_handler(self):
                return server.native_handler

            @property
            def vault(self):
                return server.vault()

            @property
            def _last_cull(self):
                return server._last_cull
            @_last_cull.setter
            def _last_cull(self, v):
                server._last_cull = v

        handler = H
        # 端口被占就往后顺延试几个（避免和别的程序冲突导致起不来）
        last_err = None
        for p in range(self.port, self.port + 8):
            try:
                self.httpd = ThreadingHTTPServer((self.host, p), handler)
                self.port = p
                break
            except OSError as e:
                last_err = e
        if self.httpd is None:
            raise RuntimeError('本地库服务端口被占用：%s' % last_err)
        self._thread = threading.Thread(target=self.httpd.serve_forever,
                                        name='lib-http', daemon=True)
        self._thread.start()
        if self.autosync:
            self.sync.start_background()
        return self.port

    def stop(self):
        try:
            self.sync.stop_background()
        except Exception:
            pass
        if self.httpd:
            try:
                self.httpd.shutdown()
                self.httpd.server_close()
            except Exception:
                pass

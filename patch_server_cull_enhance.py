# -*- coding: utf-8 -*-
"""1.9.29 服务端：把藏在引擎层的选片(Culling)/AI美图(Enhance)能力接成 HTTP 端点。

三处插入（均带 count==1 断言，遵循项目 rep() 范式，规避大文件 Edit 静默不落盘）：
  1) do_GET   ：新增 /api/enhance/status（探测 enhance_server :8768 是否在线）
  2) do_POST  ：在「# ---- 隐私库」之前插入 /api/cull、/api/cull/export、/api/enhance
"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\core\server.py'


def rep(path, old, new):
    with io.open(path, 'r', encoding='utf-8') as f:
        s = f.read()
    c = s.count(old)
    assert c == 1, 'anchor 出现 %d 次，应为 1:\n%r' % (c, old[:80])
    s = s.replace(old, new, 1)
    with io.open(path, 'w', encoding='utf-8') as f:
        f.write(s)
    print('OK replace (%d chars -> %d chars)' % (len(old), len(new)))


# ---------- 1) do_GET：/api/enhance/status（插在隐私库 GET 之前） ----------
get_anchor = """        elif path == '/api/vault/status':"""
get_block = """        elif path == '/api/enhance/status':
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
        elif path == '/api/vault/status':"""
rep(P, get_anchor, get_block)

# ---------- 2) do_POST：选片 + AI美图（插在「# ---- 隐私库」之前） ----------
post_anchor = """        # ---- 隐私库
        elif u.path == '/api/vault/setup':"""
post_block = """        # ---- 选片（Culling，能力吸收自 ai-photo-manager，MIT）
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
                if srv.get('ok'):
                    out = srv.get('output') or ''
                    if out and os.path.isfile(out):
                        srv['preview_url'] = '/api/file/%s' % self.register_file(out)
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
        elif u.path == '/api/vault/setup':"""
rep(P, post_anchor, post_block)

print('patch_server_cull_enhance: ALL DONE')

# -*- coding: utf-8 -*-
r"""本地作品库（SQLite 持久索引 + 增量同步）。

为什么要有它
────────────
早先的做法是「每次启动重新扫描云相册 → 把几千条 JSON 塞进页面 →
页面存进 localStorage」。三个硬伤：

1. 每次都要重扫（几千张的目录遍历 + EXIF 判断）；
2. 浏览器 localStorage 只有 5-10MB，6000+ 条必然超限；
3. 网页版（浏览器里打开）拿不到本地文件，也无法用 file:// 显示缩略图。

现在改成：**SQLite 作为唯一事实来源**，客户端启动后
- 只在「文件 mtime/size 变了」时才处理那一张（增量，秒级）；
- 缩略图经本地 HTTP 服务提供，浏览器也能显示；
- 网页版只要连上本地服务就能读到整库（本地客户端 = 数据服务）。

表结构
    photos(path PK, root, rel, src, size, mtime, thumb, exif_json, missing, seen_at)
    meta(key PK, value)
"""
from __future__ import annotations

import json
import os
import sqlite3
import threading
import time

from .home import data_home

DB_DIR = data_home()
DB_PATH = os.path.join(DB_DIR, 'library.db')
# 客户端自管的「导入库」目录：用户把照片「复制/导入到 app 自管目录」时，
# 原件就落在这里，彻底不依赖 iCloud / 原盘挂载（即使源盘拔掉，照片也还在）。
IMPORTS_DIR = os.path.join(DB_DIR, 'imports')
# 隐私库目录：加密后的密文与加密缩略图都放在这里，磁盘上不留明文。
VAULT_DIR = os.path.join(DB_DIR, 'vault')

# iCloud「优化存储」下，未下载原件的本地副本读图会触发按需下载、
# 把整个同步卡死（实测卡在 16% 就是这个）。靠云端特征识别占位，
# 而不是粗暴按文件大小（真实小照片也会被误杀）。
PLACEHOLDER_BYTES = 24 * 1024


def _is_cloud_placeholder(path):
    """识别云盘「未下载原件」占位文件，避免读图触发按需下载卡死。

    两种信号，命中任一即视为占位：
      1) iCloud 旧式 sidecar：同目录存在 ``name.ext.icloud``；
      2) Windows Cloud Files 的 recall 属性（dwFileAttributes 上的
         FILE_ATTRIBUTE_RECALL_ON_OPEN / _DATA_ACCESS）——这类文件
         报的是「云端全尺寸」，本地根本没数据，一读就阻塞。
    都不是则当作正常文件返回 False。
    """
    # 1) 副文件标记
    try:
        if os.path.exists(path + '.icloud'):
            return True
    except Exception:
        pass
    # 2) Windows recall 属性
    try:
        import ctypes
        from ctypes import wintypes
        INVALID = 0xFFFFFFFF
        attrs = ctypes.windll.kernel32.GetFileAttributesW(path)
        if attrs != INVALID:
            RECALL_ON_OPEN = 0x00040000
            RECALL_ON_DATA = 0x00400000
            if attrs & (RECALL_ON_OPEN | RECALL_ON_DATA):
                return True
    except Exception:
        pass
    return False

SCHEMA = """
CREATE TABLE IF NOT EXISTS photos (
    path      TEXT PRIMARY KEY,
    root      TEXT NOT NULL,
    rel       TEXT NOT NULL,
    src       TEXT NOT NULL DEFAULT 'native',
    size      INTEGER NOT NULL DEFAULT 0,
    mtime     REAL    NOT NULL DEFAULT 0,
    thumb     TEXT,
    exif_json TEXT,
    missing   INTEGER NOT NULL DEFAULT 0,
    seen_at   REAL    NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_photos_root ON photos(root);
CREATE INDEX IF NOT EXISTS idx_photos_src  ON photos(src);
-- 性能索引：
--   thumb —— /api/original/<thumb> 反查原图（每次开灯箱都会走），原来全表扫
--   rel   —— path_by_rel 按相对路径查（隐私库移出/重定位）
--   (root,missing) —— 剪枝/计数（WHERE root=? AND missing=0），原来只能靠单列 root 索引
-- SCHEMA 在每次开库时都会 executescript，故对**已存在**的旧库同样生效（幂等）。
CREATE INDEX IF NOT EXISTS idx_photos_thumb ON photos(thumb);
CREATE INDEX IF NOT EXISTS idx_photos_rel ON photos(rel);
CREATE INDEX IF NOT EXISTS idx_photos_root_missing ON photos(root, missing);
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);
-- 隐私库：明文库 photos 里的行会被移到这里，只存密文路径
CREATE TABLE IF NOT EXISTS vault (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    path       TEXT UNIQUE NOT NULL,   -- 入加密库前的原始路径（用于「移出」还原）
    name       TEXT NOT NULL,          -- 展示用文件名
    enc        TEXT NOT NULL,          -- 密文文件名（位于 VAULT_DIR）
    thumb      TEXT,                   -- 加密缩略图文件名（解锁后列表用，避免解密原图）
    size       INTEGER NOT NULL DEFAULT 0,
    mtime      REAL    NOT NULL DEFAULT 0,
    created_at REAL    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_vault_created ON vault(created_at);
"""
# 后加的列：CREATE TABLE IF NOT EXISTS 不会给已存在的表补列，必须显式 ALTER。
# (列名, SQL 类型)
EXTRA_COLUMNS = [
    ('category',  'TEXT'),    # AI 分类结果标签（NULL = 未分类）
    ('cat_score', 'REAL'),    # 置信度
    ('cat_at',    'REAL'),    # 分类时间戳
    ('rating',    'INTEGER'), # 星级 0-5（0 = 未评级）；导入时取 Lightroom XMP 的 xmp:Rating
    ('person',    'TEXT'),    # 人物识别结果（NULL = 未识别）；多人逗号分隔，如 '人物1,人物2'
]


class Library:
    """线程安全的本地库。所有写操作走同一把锁（同步任务在后台线程跑）。"""

    def __init__(self, db_path=None):
        self.db_path = db_path or DB_PATH
        os.makedirs(os.path.dirname(self.db_path), exist_ok=True)
        self._lock = threading.RLock()
        self._workers = max(2, min(8, os.cpu_count() or 4))
        self._conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        with self._lock:
            self._conn.executescript(SCHEMA)
            try:
                self._conn.execute('PRAGMA journal_mode=WAL')
            except Exception:
                pass
            self._migrate()
            self._conn.commit()

    def _migrate(self):
        """给已存在的旧库补列。幂等，可反复调用。RLock 支持上层持锁时重入。"""
        with self._lock:
            cols = {r['name'] for r in
                    self._conn.execute('PRAGMA table_info(photos)').fetchall()}
            for name, typ in EXTRA_COLUMNS:
                if name not in cols:
                    try:
                        self._conn.execute(
                            'ALTER TABLE photos ADD COLUMN %s %s' % (name, typ))
                    except Exception as e:  # noqa: BLE001 - 并发多实例时忽略
                        print('[migrate] skip %s: %s' % (name, e))
            # category 索引必须等列存在后再建（否则新库 CREATE INDEX 会因缺列报错）
            self._conn.execute(
                'CREATE INDEX IF NOT EXISTS idx_photos_cat ON photos(category)')
            self._conn.execute(
                'CREATE INDEX IF NOT EXISTS idx_photos_rating ON photos(rating)')
            # 清理上次进程崩溃残留的隐私库占位行（path 为空 = 从未 commit）
            try:
                self._conn.execute("DELETE FROM vault WHERE path=''")
            except Exception:
                pass

    # ---------------------------------------------------------- 基础
    def close(self):
        with self._lock:
            try:
                self._conn.close()
            except Exception:
                pass

    def meta_get(self, key, default=None):
        with self._lock:
            row = self._conn.execute('SELECT value FROM meta WHERE key=?', (key,)).fetchone()
        return row['value'] if row else default

    def meta_set(self, key, value):
        with self._lock:
            self._conn.execute('INSERT INTO meta(key,value) VALUES(?,?) '
                               'ON CONFLICT(key) DO UPDATE SET value=excluded.value',
                               (key, str(value)))
            self._conn.commit()

    def count(self):
        with self._lock:
            return self._conn.execute(
                'SELECT COUNT(*) c FROM photos WHERE missing=0').fetchone()['c']

    # ---------------------------------------------------------- 增量同步
    def known_index(self, root):
        """返回该 root 下已知文件的 {path: (size, mtime)}，用于比对。"""
        with self._lock:
            rows = self._conn.execute(
                'SELECT path,size,mtime FROM photos WHERE root=?', (root,)).fetchall()
        return {r['path']: (r['size'], r['mtime']) for r in rows}

    def sync_root(self, root, src='native', want_thumbs=True, progress=None,
                  prune=True):
        """增量同步一个目录。

        规则：**size 与 mtime 都没变就完全跳过**（不读 EXIF、不做缩略图）。
        新增/变化的照片才处理；消失的标记 missing=1（不删记录，
        移动硬盘没挂上时不至于把库清空）。prune=False 时不做消失检测。

        返回 {added, updated, unchanged, missing, total, seconds}
        """
        from . import exif as E, scanner as S, thumbs as T

        t0 = time.perf_counter()
        root = os.path.abspath(root)
        files = S.scan_dir(root)
        known = self.known_index(root)
        added = updated = unchanged = 0
        seen = set()
        total = len(files)
        todo = []                # 需要「读图处理」的 (abs_p, rel, size, mtime, is_update)
        placeholders = 0
        errors = []
        for rel in files:
            abs_p = os.path.join(root, rel.replace('/', os.sep))
            seen.add(abs_p)
            try:
                st = os.stat(abs_p)
                size, mtime = int(st.st_size), float(st.st_mtime)
            except Exception:
                continue
            prev = known.get(abs_p)
            if prev and prev[0] == size and abs(abs(prev[1] - mtime) < 1e-6):
                unchanged += 1
                continue
            # 云端占位文件：读它会触发按需下载、把同步卡死（实测卡在 16%
            # 就是它）。靠云端特征识别（iCloud sidecar / Windows recall
            # 属性），零误杀正常小照片；仅在 iCloud 路径下才用大小兜底。
            is_icloud = 'icloud' in root.lower()
            if _is_cloud_placeholder(abs_p) or (is_icloud and size < PLACEHOLDER_BYTES):
                placeholders += 1
                continue
            todo.append((abs_p, rel, size, mtime, prev is not None))

        def process_one(item):
            abs_p, rel, size, mtime, _is_update = item
            g = S.guess_from_path(rel)
            im = None
            ex = {}
            try:
                from PIL import Image
                im = Image.open(abs_p)           # 惰性打开：只读头部，不解码像素
                ex = E.read_exif(abs_p, _im=im)
            except Exception:
                ex = {}
            thumb_name = ''
            if want_thumbs:
                try:
                    # 缓存优先：命中则完全不解码图片（重扫上千张已是缓存的
                    # 照片时，这一步是纯 stat + sha1，毫秒级）。未命中才生成。
                    hit = T.cached_thumb_path(abs_p)
                    if hit:
                        thumb_name = os.path.basename(hit)
                    else:
                        tp = (T.thumb_from_open(im, abs_p) if im is not None
                              else T.thumb_path(abs_p))
                        if tp:
                            thumb_name = os.path.basename(tp)
                except Exception:
                    pass
            if im is not None:
                try:
                    im.close()
                except Exception:
                    pass
            d = ex.get('d') or g['d']
            return {
                'path': abs_p, 'root': root, 'rel': rel, 'src': src,
                'size': size, 'mtime': mtime, 'thumb': thumb_name,
                'exif': {
                    't': g['t'], 's': g['s'] or '',
                    'y': int(d[:4]) if d[:4].isdigit() else 0,
                    'o': 'portrait' if (ex.get('orient') or 0) in (5, 6, 7, 8) else 'landscape',
                    'r': g['r'], 'pv': g['pv'], 'ct': g['ct'],
                    'lo': ex.get('lo'),
                    'la': ex.get('la'),
                    'cam': ex.get('cam', ''), 'lens': ex.get('lens', ''),
                    'focal': ex.get('focal', 0) or 0,
                    'ap': ex.get('ap') or '',
                    'sh': ex.get('sh') or '',
                    'iso': ex.get('iso') or 0,
                    'd': d, 'tm': ex.get('tm') or '',
                    'tag': '', 'alt': ex.get('alt'), 'evc': ex.get('evc', 0.0),
                    'wb': ex.get('wb', ''), 'meter': ex.get('meter', ''),
                    'dim': ex.get('dim', ''),
                },
            }

        def _bump(done):
            if progress and (done % 50 == 0 or done == total):
                progress(done, total, '')

        # 攒批写库：原来每张照片一次 _upsert_many → 一次 commit（上千张=上千次
        # fsync 级事务，同步墙钟时间被 commit 主导）。改为攒够 UPSERT_BATCH 再提交。
        pending = []
        UPSERT_BATCH = 200

        def flush(force=False):
            if pending and (force or len(pending) >= UPSERT_BATCH):
                self._upsert_many(pending)
                del pending[:]

        done_n = unchanged + placeholders
        if todo and self._workers > 1:
            from concurrent.futures import ThreadPoolExecutor, as_completed
            BATCH = self._workers * 2
            GLOBAL_TMO = 25.0      # 整批完成上限：超时=有任务卡在云端阻塞读
            i = 0
            while i < len(todo):
                batch = todo[i:i + BATCH]
                # 每批用独立线程池：上一批若卡在云端占位读，卡死的 worker
                # 留在旧池里，下一批用全新线程池继续，绝不被拖垮。
                try:
                    with ThreadPoolExecutor(max_workers=self._workers) as pool:
                        futs = {pool.submit(process_one, it): it
                                for it in batch}
                        try:
                            for fu in as_completed(futs, timeout=GLOBAL_TMO):
                                it = futs[fu]
                                try:
                                    photo = fu.result()   # 已完成，立即返回
                                except Exception as e:
                                    errors.append('%s: %s' % (it[1], e))
                                    photo = None
                                if photo:
                                    pending.append(photo)
                                    flush()
                                    if it[4]:
                                        updated += 1
                                    else:
                                        added += 1
                                done_n += 1
                                _bump(done_n)
                        except TimeoutError:
                            # 本批有任务卡在云端占位读（阻塞或极慢失败），
                            # 放弃未完成的，换全新线程池继续下一批。
                            pending = [futs[f] for f in futs if not f.done()]
                            for it in pending:
                                errors.append('%s: 疑似云端占位,跳过' % it[1])
                                placeholders += 1
                            done_n += len(pending)
                            _bump(done_n)
                except Exception as e:
                    errors.append('batch: %s' % e)
                i += BATCH
        else:
            for it in todo:
                try:
                    photo = process_one(it)
                except Exception as e:
                    errors.append('%s: %s' % (it[0], e))
                    photo = None
                if photo:
                    pending.append(photo)
                    flush()
                    if it[4]:
                        updated += 1
                    else:
                        added += 1
                done_n = unchanged + placeholders + added + updated
                _bump(done_n)

        flush(force=True)          # 收尾：把不足一批的尾部写掉（含上面出错中断的情况）

        missing = 0
        if prune:
            with self._lock:
                rows = self._conn.execute(
                    'SELECT path FROM photos WHERE root=? AND missing=0', (root,)).fetchall()
                gone = [r['path'] for r in rows if r['path'] not in seen]
                if gone:
                    self._conn.executemany(
                        'UPDATE photos SET missing=1 WHERE path=?',
                        [(p,) for p in gone])
                    self._conn.commit()
                    missing = len(gone)
                # 重新出现的（这次扫到的）才复位 —— 早先写的是
                # `UPDATE ... WHERE root=?` 无条件复位整个 root，
                # 把刚标好的 missing 又抹平了（实测踩过）。
                if seen:
                    self._conn.executemany(
                        'UPDATE photos SET missing=0 WHERE path=?',
                        [(p,) for p in seen])
                    self._conn.commit()
        self.meta_set('last_sync:' + root, time.time())
        return {'added': added, 'updated': updated, 'unchanged': unchanged,
                'missing': missing, 'placeholders': placeholders,
                'errors': errors[:5], 'error_count': len(errors),
                'total': total,
                'seconds': round(time.perf_counter() - t0, 2)}

    def _upsert_many(self, photos):
        with self._lock:
            now = time.time()
            self._conn.executemany(
                'INSERT INTO photos(path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at,rating)'
                ' VALUES(?,?,?,?,?,?,?,?,0,?,?)'
                ' ON CONFLICT(path) DO UPDATE SET size=excluded.size, mtime=excluded.mtime,'
                ' thumb=excluded.thumb, exif_json=excluded.exif_json, rel=excluded.rel,'
                ' src=excluded.src, missing=0, seen_at=excluded.seen_at,'
                # 重扫/增量同步不得覆盖用户在应用内打过的星：只有当前为 0（未评级）
                # 时才吸收导入值（XMP）。否则一次重扫就会把评分清空。
                ' rating=CASE WHEN COALESCE(photos.rating,0)=0 THEN excluded.rating'
                ' ELSE photos.rating END',
                [(p['path'], p['root'], p['rel'], p['src'], p['size'], p['mtime'],
                  p['thumb'], json.dumps(p['exif'], ensure_ascii=False), now,
                  int((p.get('exif') or {}).get('rating') or 0))
                 for p in photos])
            self._conn.commit()

    # ---------------------------------------------------------- 查询
    def photos(self, include_missing=False, limit=None, offset=0):
        """按页面字段契约返回照片列表（img 指向本地 HTTP 缩略图）。"""
        q = ('SELECT path,rel,src,thumb,exif_json,missing,category,cat_score,rating,person '
             'FROM photos')
        if not include_missing:
            q += ' WHERE missing=0'
        q += ' ORDER BY path'
        if limit:
            q += ' LIMIT %d OFFSET %d' % (int(limit), int(offset))
        with self._lock:
            rows = self._conn.execute(q).fetchall()
        out = []
        for r in rows:
            try:
                ex = json.loads(r['exif_json'] or '{}')
            except Exception:
                ex = {}
            ex['id'] = abs(hash(r['path'])) % 100000000
            ex['rel'] = r['rel']
            ex['src'] = r['src']
            # AI 分类：cat 供前端分组用；无结果时为空串
            ex['cat'] = r['category'] or ''
            ex['catScore'] = r['cat_score'] or 0.0
            ex['rating'] = int(r['rating'] or 0)
            ex['person'] = r['person'] or ''
            ex['path'] = r['path']          # 本地绝对路径（仅回环服务暴露，供修图用）
            ex['img'] = ('/api/thumb/' + r['thumb']) if r['thumb'] else ''
            # 原图（HD）走本地 HTTP 原图端点；灯箱/查看器用它加载高清，缩略图仅作即时占位
            ex['orig'] = ('/api/original/' + r['thumb']) if r['thumb'] else ''
            if r['missing']:
                ex['_missing'] = True
            out.append(ex)
        return out

    def import_photos(self, root, items, src='native'):
        """把扫描结果（ScanWorker 产出的页面字段契约 dict）直接入库。

        这是「修复扫描/导入的照片在图库里不显示、重启即丢」的关键：
        早年扫描只把照片推进内存 PHOTOS + localStorage，而本地库模式
        下画廊读的是 DB（loadLibraryPhotos 会覆盖内存 PHOTOS），
        所以扫描结果在后台同步刷新的瞬间就被抹掉、重启后更是彻底消失。

        这里复用 ScanWorker 已经生成好的缩略图缓存（cached_thumb_path
        命中即返回，不重复读图/解码），只补一次 stat 拿 size/mtime，
        把照片以「DB 为唯一事实来源」的方式持久化。

        返回实际入库的张数。
        """
        from . import thumbs as T

        root = os.path.abspath(root)
        records = []
        seen = set()
        for it in items or []:
            abs_p = it.get('_abs') or it.get('path')
            if not abs_p:
                continue
            abs_p = os.path.abspath(abs_p)
            seen.add(abs_p)
            try:
                st = os.stat(abs_p)
                size, mtime = int(st.st_size), float(st.st_mtime)
            except Exception:
                continue
            thumb = ''
            try:
                hit = T.cached_thumb_path(abs_p)
                if hit:
                    thumb = os.path.basename(hit)
            except Exception:
                pass
            ex = {k: it.get(k) for k in (
                't', 's', 'y', 'o', 'r', 'pv', 'ct', 'lo', 'la', 'cam', 'lens',
                'focal', 'ap', 'sh', 'iso', 'd', 'tm', 'tag', 'alt', 'evc',
                'wb', 'meter', 'dim', 'rating')}
            # 星级可能挂在扫描项顶层，也可能只在 exif 子字典里（两种调用形态都兼容）
            if not ex.get('rating'):
                ex['rating'] = int((it.get('exif') or {}).get('rating') or 0)
            if ex.get('o') is None:
                ex['o'] = 'landscape'
            records.append({
                'path': abs_p, 'root': root, 'rel': it.get('rel', ''),
                'src': src, 'size': size, 'mtime': mtime,
                'thumb': thumb, 'exif': ex,
            })
        if records:
            self._upsert_many(records)
        # 剪枝：本次扫描里已经不存在的文件标 missing（删了源文件但作品还在的幽灵数据）
        with self._lock:
            rows = self._conn.execute(
                'SELECT path FROM photos WHERE root=? AND missing=0', (root,)).fetchall()
            gone = [r['path'] for r in rows if r['path'] not in seen]
            if gone:
                self._conn.executemany(
                    'UPDATE photos SET missing=1 WHERE path=?',
                    [(p,) for p in gone])
                self._conn.commit()
        return len(records)

    def thumb_file(self, key):
        """把缩略图缓存文件名映射到磁盘路径（限定在缓存目录内，防目录穿越）。"""
        from .thumbs import cache_dir
        if not key or '/' in key or '\\' in key or '..' in key:
            return ''
        p = os.path.join(cache_dir(), key)
        return p if os.path.isfile(p) else ''

    def original_file_by_id(self, key):
        """按缩略图 key 反查原图在磁盘上的绝对路径（限定为本库已登记文件，防目录穿越）。"""
        if not key or '/' in key or '\\' in key or '..' in key:
            return ''
        with self._lock:
            row = self._conn.execute(
                'SELECT path FROM photos WHERE thumb=?', (key,)).fetchone()
        if not row:
            return ''
        p = row['path']
        return p if os.path.isfile(p) else ''

    def sources(self):
        """按 root 汇总（给 UI 显示「库里有哪些来源、各多少张」）。"""
        with self._lock:
            rows = self._conn.execute(
                'SELECT root, src, COUNT(*) c, SUM(missing) m FROM photos'
                ' GROUP BY root, src ORDER BY c DESC').fetchall()
        return [{'root': r['root'], 'src': r['src'], 'count': r['c'],
                 'missing': r['m'] or 0} for r in rows]

    def drop_root(self, root):
        with self._lock:
            self._conn.execute('DELETE FROM photos WHERE root=?', (root,))
            self._conn.commit()

    # ---------------------------------------------------------- AI 分类
    def photos_missing_category(self, limit=None):
        """返回 [(path, thumb)] —— 尚未分类且未缺失的照片。"""
        q = ('SELECT path, thumb FROM photos '
             'WHERE missing=0 AND category IS NULL ORDER BY path')
        if limit:
            q += ' LIMIT %d' % int(limit)
        with self._lock:
            return [(r['path'], r['thumb'])
                    for r in self._conn.execute(q).fetchall()]

    def set_categories(self, items):
        """批量写回分类结果。items: [(path, label, score), ...]"""
        if not items:
            return 0
        now = time.time()
        with self._lock:
            self._conn.executemany(
                'UPDATE photos SET category=?, cat_score=?, cat_at=? '
                'WHERE path=?',
                [(lbl, float(score), now, p) for p, lbl, score in items])
            self._conn.commit()
        return len(items)

    def set_rating(self, path, rating):
        """设置星级（0-5，0 = 取消评级）。返回 True/False。"""
        try:
            r = int(rating)
        except (TypeError, ValueError):
            return False
        r = max(0, min(5, r))
        with self._lock:
            cur = self._conn.execute('UPDATE photos SET rating=? WHERE path=?', (r, path))
            self._conn.commit()
            return cur.rowcount > 0

    def rating_stats(self):
        """各星级计数 {0..5: n}，供筛选条显示。"""
        with self._lock:
            rows = self._conn.execute(
                'SELECT COALESCE(rating,0) r, COUNT(*) c FROM photos'
                ' WHERE missing=0 GROUP BY r').fetchall()
        out = {i: 0 for i in range(6)}
        for x in rows:
            try:
                out[int(x['r'])] = int(x['c'])
            except Exception:
                pass
        return out

    def category_stats(self):
        """每类各多少张（cat 为空串代表「未分类」）。"""
        with self._lock:
            rows = self._conn.execute(
                'SELECT COALESCE(category,"") cat, COUNT(*) c FROM photos'
                ' WHERE missing=0 GROUP BY cat ORDER BY c DESC').fetchall()
        return [{'cat': r['cat'], 'count': r['c']} for r in rows]

    def clear_categories(self):
        """清空全部分类结果（换模型 / 想重跑时用）。"""
        with self._lock:
            self._conn.execute(
                'UPDATE photos SET category=NULL, cat_score=NULL, cat_at=NULL')
            self._conn.commit()

    # ---------------------------------------------------------- 人物识别
    def photos_missing_person(self, limit=None):
        """返回 [(path, thumb)] —— 尚未跑过人物识别（person 为空）且未缺失的照片。"""
        q = ('SELECT path, thumb FROM photos '
             'WHERE missing=0 AND person IS NULL ORDER BY path')
        if limit:
            q += ' LIMIT %d' % int(limit)
        with self._lock:
            return [(r['path'], r['thumb'])
                    for r in self._conn.execute(q).fetchall()]

    def set_persons(self, items):
        """批量写回人物识别结果。items: [(path, person_str), ...]"""
        if not items:
            return 0
        with self._lock:
            self._conn.executemany(
                'UPDATE photos SET person=? WHERE path=?',
                [(pstr, p) for p, pstr in items])
            self._conn.commit()
        return len(items)

    def person_stats(self):
        """每个「人物」标签各多少张（逗号分隔的多人照片会拆分计数）。

        返回 [{'person': '人物1', 'count': n}, ...] 按数量降序；空库/未识别返回 []。
        """
        with self._lock:
            rows = self._conn.execute(
                "SELECT person FROM photos WHERE missing=0 AND person IS NOT NULL"
            ).fetchall()
        from collections import Counter
        c = Counter()
        for r in rows:
            for name in (r['person'] or '').split(','):
                name = name.strip()
                if name:
                    c[name] += 1
        return [{'person': k, 'count': v} for k, v in
                sorted(c.items(), key=lambda kv: -kv[1])]

    def clear_persons(self):
        """清空全部人物识别结果（换模型 / 想重跑时用）。"""
        with self._lock:
            self._conn.execute('UPDATE photos SET person=NULL')
            self._conn.commit()

    def photo_by_path(self, path):
        """按绝对路径取单张照片的页面字段契约 dict（与 photos() 同形状），不存在返回 None。

        供 MCP 服务端 /api/photo_exif 用：只查一行，避免为单张 exif 拉全库。
        """
        with self._lock:
            row = self._conn.execute(
                'SELECT exif_json, thumb, person, missing FROM photos WHERE path=?',
                (path,)).fetchone()
        if not row:
            return None
        try:
            ex = json.loads(row['exif_json'] or '{}')
        except Exception:
            ex = {}
        ex['person'] = row['person'] or ''
        ex['img'] = ('/api/thumb/' + row['thumb']) if row['thumb'] else ''
        ex['path'] = path
        if row['missing']:
            ex['_missing'] = True
        return ex

    # ---------------------------------------------------------- 隐私库
    def vault_reserve_id(self):
        """占一个 id。密文文件名依赖 id，所以必须先占位（path 先留空）。"""
        with self._lock:
            cur = self._conn.execute(
                "INSERT INTO vault(path,name,enc,thumb,created_at)"
                " VALUES('','','','',0)")
            self._conn.commit()
            return cur.lastrowid

    def vault_commit(self, item_id, path, name, enc, thumb, size, mtime):
        with self._lock:
            self._conn.execute(
                'UPDATE vault SET path=?,name=?,enc=?,thumb=?,size=?,mtime=?,'
                'created_at=? WHERE id=?',
                (path, name, enc, thumb, size, mtime, time.time(), item_id))
            self._conn.commit()

    def list_vault(self):
        with self._lock:
            rows = self._conn.execute(
                'SELECT id,path,name,enc,thumb,size,mtime,created_at FROM vault'
                " WHERE path<>'' ORDER BY created_at DESC").fetchall()
        return [dict(r) for r in rows]

    def vault_get(self, item_id):
        with self._lock:
            r = self._conn.execute(
                'SELECT id,path,name,enc,thumb,size,mtime,created_at FROM vault'
                ' WHERE id=?', (item_id,)).fetchone()
        return dict(r) if r else None

    def vault_delete(self, item_id):
        with self._lock:
            self._conn.execute('DELETE FROM vault WHERE id=?', (item_id,))
            self._conn.commit()

    def drop_photo(self, path):
        """从明文库移除（照片被加密进隐私库后调用）。"""
        with self._lock:
            self._conn.execute('DELETE FROM photos WHERE path=?', (path,))
            self._conn.commit()

    def path_by_rel(self, rel, src=None):
        """按相对路径找回原始绝对路径。

        页面从 /api/photos 只拿到 rel（拿不到绝对路径），所以「移入隐私库」
        要由服务端按 rel 反查。
        """
        if not rel:
            return ''
        with self._lock:
            if src:
                r = self._conn.execute(
                    'SELECT path FROM photos WHERE rel=? AND src=? LIMIT 1',
                    (rel, src)).fetchone()
            else:
                r = self._conn.execute(
                    'SELECT path FROM photos WHERE rel=? LIMIT 1',
                    (rel,)).fetchone()
        return r['path'] if r else ''

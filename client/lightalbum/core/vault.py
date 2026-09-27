# -*- coding: utf-8 -*-
"""隐私库（强加密隔离）。

安全模型
--------
* 口令 → PBKDF2-HMAC-SHA256（480k 次迭代，per-vault 随机 salt）→ 32 字节密钥 → Fernet。
  Fernet = AES-128-CBC + HMAC-SHA256，自带完整性校验，密文被篡改会解密失败。
* **磁盘上不存密钥**，也不存口令本身。只存 salt + 一段「校验串」：
  用该密钥加密一段已知常量，能解开就说明口令正确。
* 密钥只在解锁期间驻留内存，超时自动锁定；进程退出即消失。
* **忘记口令 = 无法恢复**（这是强加密的代价，UI 必须明确提示）。

最重要的安全底线
----------------
「移入隐私库」时会先加密 → 再解密回来 → 比对 SHA256 与原文件一致，
**确认无误后才删除明文**。宁可失败也不留断点：拿不到原文件哈希、
或往返校验不一致，一律保留明文并报错。
"""
from __future__ import annotations

import base64
import hashlib
import os
import secrets
import threading
import time

from cryptography.fernet import Fernet, InvalidToken
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

from .library import VAULT_DIR

PBKDF2_ITERATIONS = 480_000
SALT_BYTES = 16
VERIFIER_PLAIN = b'lightalbum-vault-verifier-v1'
# 自动锁定：默认 30 分钟无操作
AUTO_LOCK_SECONDS = 30 * 60
# 缩略图长边（隐私库内部自产自管）
THUMB_PX = 512


def _b64(b: bytes) -> str:
    return base64.urlsafe_b64encode(b).decode('ascii')


def _unb64(s: str) -> bytes:
    return base64.urlsafe_b64decode(s.encode('ascii'))


def derive_key(password: str, salt: bytes) -> bytes:
    """口令 + salt → Fernet 密钥（URL-safe base64）。"""
    kdf = PBKDF2HMAC(algorithm=hashes.SHA256(), length=32,
                     salt=salt, iterations=PBKDF2_ITERATIONS)
    return base64.urlsafe_b64encode(kdf.derive(password.encode('utf-8')))


def sha256_file(path: str, chunk=1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(chunk), b''):
            h.update(blk)
    return h.hexdigest()


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class VaultError(Exception):
    """隐私库操作异常（会原样展示给用户）。"""


class Vault:
    """隐私库门面。所有方法线程安全。"""

    def __init__(self, lib, vault_dir=None):
        self.lib = lib
        self.dir = vault_dir or VAULT_DIR
        os.makedirs(self.dir, exist_ok=True)
        self._lock = threading.RLock()
        self._fernet = None
        self._unlocked_at = 0.0

    # ---------------------------------------------------- 口令管理
    def has_password(self) -> bool:
        return bool(self.lib.meta_get('vault_salt')) and \
               bool(self.lib.meta_get('vault_verifier'))

    def set_password(self, password: str):
        if not password or len(password) < 4:
            raise VaultError('口令至少 4 位')
        salt = secrets.token_bytes(SALT_BYTES)
        f = Fernet(derive_key(password, salt))
        verifier = f.encrypt(VERIFIER_PLAIN)
        self.lib.meta_set('vault_salt', _b64(salt))
        self.lib.meta_set('vault_verifier', _b64(verifier))
        # 设新口令即视为已解锁（首次使用要马上能往里放东西）
        with self._lock:
            self._fernet = f
            self._unlocked_at = time.time()

    def verify_password(self, password: str) -> bool:
        b64 = self.lib.meta_get('vault_salt')
        ver = self.lib.meta_get('vault_verifier')
        if not b64 or not ver:
            return False
        try:
            f = Fernet(derive_key(password, _unb64(b64)))
            return f.decrypt(_unb64(ver)) == VERIFIER_PLAIN
        except (InvalidToken, Exception):  # noqa: BLE001
            return False

    def unlock(self, password: str) -> bool:
        b64 = self.lib.meta_get('vault_salt')
        ver = self.lib.meta_get('vault_verifier')
        if not b64 or not ver:
            raise VaultError('尚未设置隐私库口令')
        try:
            f = Fernet(derive_key(password, _unb64(b64)))
            f.decrypt(_unb64(ver))          # 错口令会抛 InvalidToken
        except InvalidToken:
            return False
        with self._lock:
            self._fernet = f
            self._unlocked_at = time.time()
        return True

    def change_password(self, old_pw: str, new_pw: str):
        """换口令：所有密文用新密钥重新加密（必须已解锁或提供旧口令）。"""
        if not self.unlock(old_pw):
            raise VaultError('原口令不正确')
        if not new_pw or len(new_pw) < 4:
            raise VaultError('新口令至少 4 位')

        old_f = self._fernet
        rows = self.lib.list_vault()
        # 先用旧密钥把所有明文解出来（内存中）
        plain = {}
        for r in rows:
            enc = os.path.join(self.dir, r['enc'])
            with open(enc, 'rb') as fh:
                plain[r['id']] = old_f.decrypt(fh.read())

        # 换新 salt/密钥并重加密
        salt = secrets.token_bytes(SALT_BYTES)
        new_f = Fernet(derive_key(new_pw, salt))
        for _id, data in plain.items():
            with open(self._secret_path(_id), 'wb') as fh:
                fh.write(new_f.encrypt(data))

        self.lib.meta_set('vault_salt', _b64(salt))
        self.lib.meta_set('vault_verifier', _b64(new_f.encrypt(VERIFIER_PLAIN)))
        with self._lock:
            self._fernet = new_f
            self._unlocked_at = time.time()

    # ---------------------------------------------------- 会话状态
    def lock(self):
        with self._lock:
            self._fernet = None
            self._unlocked_at = 0.0

    def unlocked(self) -> bool:
        with self._lock:
            if self._fernet is None:
                return False
            if time.time() - self._unlocked_at > AUTO_LOCK_SECONDS:
                self._fernet = None
                return False
            return True

    def touch(self):
        """有操作时顺延会话（避免用户浏览时中途掉锁）。"""
        with self._lock:
            if self._fernet is not None:
                self._unlocked_at = time.time()

    def seconds_left(self) -> int:
        if not self.unlocked():
            return 0
        return max(0, int(AUTO_LOCK_SECONDS - (time.time() - self._unlocked_at)))

    # ---------------------------------------------------- 内部
    def _require(self):
        f = self._fernet if self.unlocked() else None
        if f is None:
            raise VaultError('隐私库已锁定，请先解锁')
        return f

    def _secret_path(self, item_id):
        return os.path.join(self.dir, 'v%06d.enc' % int(item_id))

    def _thumb_path(self, item_id):
        return os.path.join(self.dir, 'v%06d_t.enc' % int(item_id))

    def _make_thumb_bytes(self, file_bytes: bytes, ext: str):
        """从原始字节生成 JPEG 缩略图字节（不落磁盘、不留明文）。"""
        try:
            from io import BytesIO
            from PIL import Image
            im = Image.open(BytesIO(file_bytes))
            im.load()
            if getattr(im, 'n_frames', 1) > 1:
                im.seek(0)
            im = im.convert('RGB')
            im.thumbnail((THUMB_PX, THUMB_PX))
            buf = BytesIO()
            im.save(buf, 'JPEG', quality=82)
            return buf.getvalue()
        except Exception:
            return b''

    # ---------------------------------------------------- 入库 / 出库
    def add(self, path: str, name: str = None, root: str = '', rel: str = '',
            src: str = 'native', thumb_hint: str = ''):
        """把一张照片加密进隐私库，成功后删除明文。

        顺序（宁可失败也不丢数据）：
          1) 算原文件 SHA256；
          2) 读原文件 → 加密 → 同时也加密一张缩略图；
          3) **解密回来比对哈希**，不一致立即放弃（保留明文）；
          4) 写 DB；
          5) 最后才删明文 + 从明文库除名。
        """
        f = self._require()
        if not os.path.isfile(path):
            raise VaultError('文件不存在：%s' % path)

        orig_hash = sha256_file(path)
        with open(path, 'rb') as fh:
            raw = fh.read()
        token = f.encrypt(raw)

        # 先占位拿到 id（密文文件名依赖 id）
        item_id = self.lib.vault_reserve_id()
        secret = self._secret_path(item_id)
        thumb_secret = self._thumb_path(item_id)
        thumb_token = b''
        tb = self._make_thumb_bytes(raw, os.path.splitext(path)[1])
        if tb:
            thumb_token = f.encrypt(tb)

        try:
            # 往返校验：必须能用同一把钥匙解出完全一致的字节
            if f.decrypt(token) != raw:
                raise VaultError('加密自校验失败，已保留原文件')
            if thumb_token and f.decrypt(thumb_token) != tb:
                raise VaultError('缩略图加密自校验失败，已保留原文件')

            with open(secret, 'wb') as fh:
                fh.write(token)
            if thumb_token:
                with open(thumb_secret, 'wb') as fh:
                    fh.write(thumb_token)
        except Exception:
            # 任何一步失败都要清掉残留密文，且绝不删明文
            for p in (secret, thumb_secret):
                if os.path.isfile(p):
                    try:
                        os.remove(p)
                    except Exception:
                        pass
            raise

        # 落库
        self.lib.vault_commit(
            item_id, path, name or os.path.basename(path),
            os.path.basename(secret),
            os.path.basename(thumb_secret) if os.path.isfile(thumb_secret) else '',
            len(raw), os.path.getmtime(path))

        # 只有当完整性确认无误后，才允许删除明文
        self.lib.drop_photo(path)
        try:
            os.remove(path)
        except Exception as e:  # noqa: BLE001
            # 删不掉不能算失败（库里已有密文），但要如实告知
            return {'ok': True, 'id': item_id, 'hash': orig_hash,
                    'warning': '明文删除失败，请手动删除：%s (%s)'
                               % (path, e)}
        return {'ok': True, 'id': item_id, 'hash': orig_hash}

    def remove(self, item_id: int, dest_dir: str = None):
        """取出隐私照片：解密写回明文，并从隐私库删除密文。"""
        f = self._require()
        row = self.lib.vault_get(item_id)
        if not row:
            raise VaultError('隐私库里没有这张照片')

        secret = os.path.join(self.dir, row['enc'])
        if not os.path.isfile(secret):
            raise VaultError('密文缺失：%s' % row['enc'])
        with open(secret, 'rb') as fh:
            raw = f.decrypt(fh.read())

        dest_dir = dest_dir or os.path.dirname(row['path']) or os.path.expanduser('~')
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, row['name'])
        n = 1
        stem, ext = os.path.splitext(row['name'])
        while os.path.exists(dest):                    # 不覆盖已有文件
            dest = os.path.join(dest_dir, '%s_%d%s' % (stem, n, ext))
            n += 1

        with open(dest, 'wb') as fh:
            fh.write(raw)
        # 写回后再确认一次，确保不是空壳
        if sha256_file(dest) != sha256_bytes(raw):
            raise VaultError('解密写回校验失败，已中止')

        self.lib.vault_delete(item_id)
        for p in (secret, self._thumb_path(item_id)):
            if os.path.isfile(p):
                try:
                    os.remove(p)
                except Exception:
                    pass
        return {'ok': True, 'path': dest, 'size': len(raw)}

    # ---------------------------------------------------- 读取（需解锁）
    def items(self):
        """元信息列表（不含任何明文/缩略图内容）。"""
        return self.lib.list_vault()

    def thumb_bytes(self, item_id: int) -> bytes:
        """解锁后返回缩略图明文 JPEG 字节；没有加密缩略图时返回空。"""
        f = self._require()
        row = self.lib.vault_get(item_id)
        if not row or not row['thumb']:
            return b''
        p = os.path.join(self.dir, row['thumb'])
        if not os.path.isfile(p):
            return b''
        try:
            with open(p, 'rb') as fh:
                return f.decrypt(fh.read())
        except InvalidToken:
            raise VaultError('密文校验失败（口令可能已更换）')

    def file_bytes(self, item_id: int) -> bytes:
        f = self._require()
        row = self.lib.vault_get(item_id)
        if not row:
            raise VaultError('隐私库里没有这张照片')
        p = os.path.join(self.dir, row['enc'])
        try:
            with open(p, 'rb') as fh:
                return f.decrypt(fh.read())
        except InvalidToken:
            raise VaultError('密文校验失败（口令可能已更换）')

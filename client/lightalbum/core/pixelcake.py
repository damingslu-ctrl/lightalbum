# -*- coding: utf-8 -*-
r"""执行层：Pixel Cake（本地 AI 修图服务）+ 本地 Pillow 管线兜底。

Pixel Cake 真实接口（已核对 backend/main.py，默认 0.0.0.0:8765）：
    GET  /health                 → {status, gpu:{available,name}, models_loaded:{...}}
    POST /upload                 (multipart file) → {image_id, width, height, path}
    POST /enhance                (JSON: image_id + 16 参数 + filter + skin_smooth) → JPEG 流
    POST /relight                (form: image_id, brightness, warmth)
    POST /auto-segment           (form: image_id, mode=person|sky|skin|teeth|ground|all) → 掩膜 PNG
    POST /inpaint                (JSON: image_id, mask_id, prompt, ...) → JPEG
    POST /color-match            (form image_id + file reference) → JPEG
    POST /face-slim|/hair-smooth (form: image_id, strength)
    POST /sky/replace            (JSON: image_id, sky_type, blend_strength)
    GET  /download/{result_id}
结果图一律以 StreamingResponse 返回 JPEG，结果 id 在 X-Result-Id 头里。

为什么还要本地兜底：Pixel Cake 需要单独部署 + 首次自动下载模型
（LaMa ~450MB、SAM2 400MB–2.4GB）。服务不在时，本地管线按同一份
EditPlan 执行它能覆盖的那部分参数，保证功能不中断。
"""
from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from uuid import uuid4

from . import retouch as R
from .brain import assert_local
from .editplan import EditPlan

DEFAULT_CAKE_URL = 'http://127.0.0.1:8765'

# 本地管线能覆盖的参数（其余仅 Pixel Cake 支持）
LOCAL_SUPPORTED = ('brightness', 'contrast', 'saturation', 'warmth',
                   'sharpness', 'denoise')


def probe_cake(base_url=DEFAULT_CAKE_URL, timeout=3.0):
    """探测 Pixel Cake 是否可用。返回 (ok, 详情)。"""
    try:
        info = _get(base_url.rstrip('/') + '/health', timeout=timeout)
        if info.get('status') != 'ok':
            return False, 'Pixel Cake 响应异常'
        gpu = info.get('gpu') or {}
        loaded = info.get('models_loaded') or {}
        return True, 'Pixel Cake 在线（GPU: %s）' % (
            gpu.get('name') or ('可用' if gpu.get('available') else '仅 CPU'))
    except urllib.error.HTTPError as e:
        return False, 'Pixel Cake HTTP %s' % e.code
    except Exception as e:
        return False, 'Pixel Cake 未启动（%s）' % type(e).__name__


def probe_cake_caps(base_url=DEFAULT_CAKE_URL, timeout=3.0):
    """读取 /health 的 models_loaded，返回 {matting: bool, score: bool}。

    注意：仅反映**已加载**状态（BiRefNet/pyiqa 首次调用才下载权重）。
    未加载≠未安装；按钮可按此隐藏，点击若 503 再提示需安装依赖。
    """
    try:
        info = _get(base_url.rstrip('/') + '/health', timeout=timeout)
        loaded = info.get('models_loaded') or {}
        return {'matting': bool(loaded.get('matting')),
                'score': bool(loaded.get('score'))}
    except Exception:
        return {'matting': False, 'score': False}


def _get(url, timeout=5.0):
    with urllib.request.urlopen(urllib.request.Request(url, method='GET'),
                                timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8'))


def _post_json(url, payload, timeout=120.0):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode('utf-8'),
        headers={'Content-Type': 'application/json'}, method='POST')
    return urllib.request.urlopen(req, timeout=timeout)


def _multipart(fields, files):
    b = uuid4().hex
    body = b''
    for k, v in (fields or {}).items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n'
                 % (b, k, v)).encode('utf-8')
    for k, (fname, data, ctype) in (files or {}).items():
        body += ('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\n'
                 'Content-Type: %s\r\n\r\n' % (b, k, fname, ctype)).encode('utf-8')
        body += data + b'\r\n'
    body += ('--%s--\r\n' % b).encode('utf-8')
    return b, body


def _post_form(url, fields, files=None, timeout=120.0):
    b, body = _multipart(fields, files)
    req = urllib.request.Request(
        url, data=body,
        headers={'Content-Type': 'multipart/form-data; boundary=%s' % b},
        method='POST')
    return urllib.request.urlopen(req, timeout=timeout)


class PixelCakeClient:
    """Pixel Cake 的 HTTP 客户端（只做「上传 → 调接口 → 取回图片」）。"""

    name = 'pixelcake'

    def __init__(self, base_url=DEFAULT_CAKE_URL, timeout=180.0):
        assert_local(base_url, 'Pixel Cake 服务')
        self.base_url = (base_url or DEFAULT_CAKE_URL).rstrip('/')
        self.timeout = timeout

    def available(self):
        return probe_cake(self.base_url)[0]

    def upload(self, path):
        with open(path, 'rb') as f:
            data = f.read()
        ctype = 'image/jpeg'
        low = path.lower()
        if low.endswith('.png'):
            ctype = 'image/png'
        elif low.endswith('.webp'):
            ctype = 'image/webp'
        return self.upload_bytes(data, os.path.basename(path), ctype)

    def upload_bytes(self, data, filename='upload.jpg', ctype='image/jpeg'):
        r = _post_form(self.base_url + '/upload', {},
                       {'file': (filename, data, ctype)}, timeout=self.timeout)
        return json.loads(r.read().decode('utf-8'))

    def enhance(self, image_id, params, filter_name=None, skin_smooth=False,
                filter_intensity=1.0):
        payload = {'image_id': image_id}
        payload.update({k: float(v) for k, v in (params or {}).items()
                        if isinstance(v, (int, float))})
        payload['filter'] = filter_name
        payload['filter_intensity'] = float(filter_intensity)
        payload['skin_smooth'] = bool(skin_smooth)
        r = _post_json(self.base_url + '/enhance', payload, timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def auto_mask(self, image_id, mode='person'):
        r = _post_form(self.base_url + '/auto-segment',
                       {'image_id': image_id, 'mode': mode}, timeout=self.timeout)
        return r.read(), r.headers.get('X-Mask-Id')

    def inpaint(self, image_id, mask_id, prompt='', **kw):
        payload = {'image_id': image_id, 'mask_id': mask_id, 'prompt': prompt}
        payload.update(kw)
        r = _post_json(self.base_url + '/inpaint', payload, timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def relight(self, image_id, brightness=0.3, warmth=0.1):
        r = _post_form(self.base_url + '/relight',
                       {'image_id': image_id, 'brightness': str(brightness),
                        'warmth': str(warmth)}, timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def sky(self, image_id, sky_type='blue', blend=0.7):
        r = _post_json(self.base_url + '/sky/replace',
                       {'image_id': image_id, 'sky_type': sky_type,
                        'blend_strength': float(blend)}, timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def face_slim(self, image_id, strength=0.3):
        r = _post_form(self.base_url + '/face-slim',
                       {'image_id': image_id, 'strength': str(strength)},
                       timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def hair_smooth(self, image_id, strength=0.5):
        r = _post_form(self.base_url + '/hair-smooth',
                       {'image_id': image_id, 'strength': str(strength)},
                       timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def color_match(self, image_id, ref_path):
        with open(ref_path, 'rb') as f:
            data = f.read()
        r = _post_form(self.base_url + '/color-match', {'image_id': image_id},
                       {'reference': (os.path.basename(ref_path), data, 'image/jpeg')},
                       timeout=self.timeout)
        return r.read(), r.headers.get('X-Result-Id')

    def matting(self, image_id, return_rgba=False):
        """BiRefNet 抠图。返回 (PNG bytes, mask_id 或 result_id)。

        未装 BiRefNet 时服务端返回 503 → urllib 抛 HTTPError，由调用方降级。
        """
        r = _post_form(self.base_url + '/matting',
                       {'image_id': image_id,
                        'return_rgba': 'true' if return_rgba else 'false'},
                       timeout=self.timeout)
        hdr = r.headers.get('X-Mask-Id') or r.headers.get('X-Result-Id')
        return r.read(), hdr

    def score(self, image_id):
        """pyiqa 画质/美学评分。返回 {image_id, scores:{...}, metrics:[...]}。

        未装 pyiqa 时服务端返回 503 → 调用方降级。
        """
        r = _post_form(self.base_url + '/score', {'image_id': image_id},
                       timeout=self.timeout)
        return json.loads(r.read().decode('utf-8'))


PRESET_SCALE = {'soft': 0.6, 'auto': 1.0, 'strong': 1.4}


class LocalExecutor:
    """本地 Pillow 管线：消费同一份 EditPlan（仅覆盖 LOCAL_SUPPORTED）。"""

    name = 'local'

    def run(self, path, plan, exif=None, preset='auto'):
        k = PRESET_SCALE.get(preset, 1.0)
        params = {kk: float(v) * k for kk, v in (plan.params or {}).items()}
        out, summary = R.retouch_with_plan(path, params, exif)
        summary['engine'] = 'local'
        summary['preset'] = preset
        summary['covered'] = [kk for kk in LOCAL_SUPPORTED
                              if abs(float(params.get(kk, 0) or 0)) >= 0.01]
        return out, summary


class PixelCakeExecutor:
    """把 EditPlan 的操作依次交给 Pixel Cake 执行（模型调度）。

    支持的操作（对应 Pixel Cake 的路由）：
      enhance    → /enhance（16 参数 + 滤镜 + 磨皮）
      relight    → /relight（补光通道）
      inpaint    → /auto-segment 出掩膜 + /inpaint 修复（去路人/杂物/穿帮）
      sky        → /sky/replace（换天）
      face-slim  → /face-slim（瘦脸）
      hair-smooth→ /hair-smooth（祛碎发）
    """

    name = 'pixelcake'

    def __init__(self, client=None, base_url=DEFAULT_CAKE_URL):
        if client is None:
            assert_local(base_url, 'Pixel Cake 服务')
        self.client = client or PixelCakeClient(base_url)

    def run(self, path, plan, exif=None, preset='auto'):
        up = self.client.upload(path)
        image_id = up.get('image_id')
        if not image_id:
            raise RuntimeError('Pixel Cake 上传失败')
        blob = None
        result_id = None
        used = []
        c = self.client
        for op in (plan.ops or []):
            kind = op.get('op', 'enhance')
            try:
                if kind == 'enhance':
                    params = op.get('params') or plan.params
                    skin = bool(op.get('skin_smooth') or params.get('skin_smooth'))
                    blob, result_id = c.enhance(
                        image_id, params, plan.filter, skin_smooth=skin)
                    used.append('enhance' + ('+磨皮' if skin else ''))
                elif kind == 'relight':
                    blob, result_id = c.relight(
                        image_id, op.get('brightness', 0.3), op.get('warmth', 0.1))
                    used.append('relight')
                elif kind == 'inpaint':
                    mask, mask_id = c.auto_mask(image_id, op.get('mask_mode', 'person'))
                    if not (mask_id or mask):
                        raise RuntimeError('自动分割未返回掩膜')
                    blob, result_id = c.inpaint(
                        image_id, mask_id, op.get('prompt', ''))
                    used.append('inpaint(%s)' % op.get('mask_mode', 'person'))
                elif kind == 'sky':
                    blob, result_id = c.sky(image_id, op.get('sky_type', 'blue'),
                                            op.get('blend', 0.7))
                    used.append('sky:' + op.get('sky_type', 'blue'))
                elif kind == 'face-slim':
                    blob, result_id = c.face_slim(image_id, op.get('strength', 0.3))
                    used.append('face-slim')
                elif kind == 'hair-smooth':
                    blob, result_id = c.hair_smooth(image_id, op.get('strength', 0.5))
                    used.append('hair-smooth')
                elif kind == 'matting':
                    rgba = bool(op.get('return_rgba', False))
                    blob, result_id = c.matting(image_id, return_rgba=rgba)
                    used.append('matting(rgba)' if rgba else 'matting')
                    break  # 抠图为终态：保存透明图，不再叠加其它操作
                else:
                    continue
                if blob:                     # 后续操作基于上一步的结果继续
                    try:
                        image_id = c.upload_bytes(blob).get('image_id') or image_id
                    except Exception:
                        pass
            except Exception as e:
                # 单个操作失败不致命：保留已有结果，把失败原因记进步骤
                used.append('%s失败(%s)' % (kind, str(e)[:60]))
        if blob is None:
            raise RuntimeError('Pixel Cake 未返回结果（%s）' % '；'.join(used) or '未知')
        root, _ = os.path.splitext(path)
        out_ext = '.png' if any('matting' in u for u in used) else '.jpg'
        out = root + '_retouched' + out_ext
        with open(out, 'wb') as f:
            f.write(blob)
        return out, {
            'in': path, 'out': out, 'engine': 'pixelcake',
            'result_id': result_id, 'bytes': len(blob),
            'steps': [{'key': 'pixelcake', 'name': 'Pixel Cake 执行',
                       'detail': '%s ｜ 指令：%s' % (' → '.join(used), plan.summary())}],
            'ops': used,
            'lum_before': None, 'lum_after': None,
            'sat_before': None, 'sat_after': None,
        }


def get_executor(config=None):
    cfg = config or {}
    url = cfg.get('cake_url') or DEFAULT_CAKE_URL
    if cfg.get('cake', True) and probe_cake(url)[0]:
        return PixelCakeExecutor(PixelCakeClient(url))
    return LocalExecutor()

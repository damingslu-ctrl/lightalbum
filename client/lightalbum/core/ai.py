# -*- coding: utf-8 -*-
r"""统一调度：分析图像 → 决策层出指令 → 执行层改像素。

    smart_edit(path, goal, exif, config):
        1. 测量：亮度 / 离散 / 饱和度（直方图统计）
        2. 决策：JevBrain（服务在线）或 LocalBrain（兜底） → EditPlan
        3. 执行：PixelCakeExecutor（服务在线）或 LocalExecutor（兜底）
        4. 回传：谁做的决策、谁执行的、改了哪些参数、输出文件路径

决策层与执行层可**独立降级**：Jev 没跑而 Pixel Cake 在跑，也能工作；
反过来也一样。UI 会把实际用到的组合如实显示出来。
"""
from __future__ import annotations

import os

from PIL import Image, ImageOps

from . import brain as B
from . import pixelcake as PC
from . import retouch as R
from .editplan import EditPlan


def measure(path, exif=None):
    """测一幅图的客观指标，作为决策依据。"""
    ctx = {'lum': 0.5, 'lum_std': 0.15, 'sat': 0.25,
           'iso': 0, 'shutter': 0.0, 'ap': '', 'focal': 0}
    try:
        # 解码期降采样 + 缓存（_load_analyze）：避免对大图整张解码再缩，
        # 同会话内 suggest/edit 复用同一解码结果。
        img = R._load_analyze(path, 256)
        st = R._stats(img)
        ctx['lum'] = st['lum']
        ctx['lum_std'] = st['lum_std']
        ctx['sat'] = st['sat']
    except Exception:
        pass
    if exif:
        try:
            ctx['iso'] = int(exif.get('iso') or 0)
        except Exception:
            pass
        try:
            ctx['shutter'] = float(exif.get('shutter') or 0)
        except Exception:
            pass
        ctx['ap'] = exif.get('ap') or ''
        try:
            ctx['focal'] = float(exif.get('focal') or 0)
        except Exception:
            pass
    return ctx


def status(config=None):
    """探测两个服务 + 本机 GPU，供 UI 显示。"""
    cfg = config or {}
    jok, jmsg = B.probe_jev(cfg.get('jev_url') or B.DEFAULT_JEV_URL,
                            cfg.get('jev_key') or B.DEFAULT_JEV_KEY)
    cok, cmsg = PC.probe_cake(cfg.get('cake_url') or PC.DEFAULT_CAKE_URL)
    caps = {'matting': False, 'score': False}
    if cok:
        try:
            caps = PC.probe_cake_caps(cfg.get('cake_url') or PC.DEFAULT_CAKE_URL)
        except Exception:                       # noqa: BLE001
            pass
    lok, lmsg = False, '未启动'
    lurl = cfg.get('llm_url') or B.DEFAULT_LLM_URL
    try:
        _lb = B.LocalLLMBrain(lurl, cfg.get('llm_model') or B.DEFAULT_LLM_MODEL)
        lok = _lb.available()
        lmsg = ('本地 LLM %s 在线' % _lb.model) if lok else ('未启动（%s）' % _lb.model)
    except Exception as e:                       # noqa: BLE001
        lmsg = '配置非法：%s' % e
    gpu = {'available': False, 'name': ''}
    try:
        import subprocess
        out = subprocess.run(
            ['nvidia-smi', '--query-gpu=name,memory.total',
             '--format=csv,noheader'],
            capture_output=True, text=True, timeout=5)
        if out.returncode == 0 and out.stdout.strip():
            parts = [p.strip() for p in out.stdout.strip().split(',')]
            gpu = {'available': True, 'name': parts[0]}
            if len(parts) > 1:
                gpu['memory'] = parts[1]
    except Exception:
        pass
    return {
        'jev': {'ok': jok, 'detail': jmsg,
                'url': cfg.get('jev_url') or B.DEFAULT_JEV_URL},
        'cake': {'ok': cok, 'detail': cmsg,
                 'url': cfg.get('cake_url') or PC.DEFAULT_CAKE_URL,
                 'caps': caps},
        'llm': {'ok': lok, 'detail': lmsg, 'url': lurl,
                'model': cfg.get('llm_model') or B.DEFAULT_LLM_MODEL},
        'gpu': gpu,
        'mode': ('jev' if jok else ('llm' if lok else 'local'))
                + '+' + ('pixelcake' if cok else 'local'),
    }


def smart_edit(path, goal='', exif=None, config=None, preset='auto'):
    """一键智能修图：返回可直接回推给网页的结果 dict。

    preset 只影响**本地兜底管线**的幅度（Pixel Cake 有自己的参数标度）。
    """
    if not path or not os.path.isfile(path):
        raise RuntimeError('文件不存在：%s' % path)

    ctx = measure(path, exif)
    ctx['goal'] = goal or ''

    brain = B.get_brain(config)
    brain_note = ''
    try:
        plan = brain.decide(ctx)
    except Exception as e:                       # noqa: BLE001
        # 决策层（Jev / 本地 LLM）探测通过但实际调用失败 → 降级到规则引擎，
        # 绝不让「决策层出问题」把整条修图链路弄挂。
        if getattr(brain, 'source', '') == 'local':
            raise
        brain_note = '决策层 %s 调用失败（%s），已降级为本地规则引擎' % (
            getattr(brain, 'source', '?'), e)
        brain = B.LocalBrain()
        plan = brain.decide(ctx)
    if not isinstance(plan, EditPlan):
        raise RuntimeError('决策层返回了非法指令')

    executor = PC.get_executor(config)
    try:
        out, summary = executor.run(path, plan, exif, preset=preset)
    except TypeError:                       # 兼容只吃三个参数的执行器
        out, summary = executor.run(path, plan, exif)

    return {
        'in': path, 'out': out,
        'brain': getattr(brain, 'source', 'local'),
        'brain_name': {'jev': 'Jev（本地 LLM）',
                       'llm': '本地 LLM（Ollama）'}.get(
                           getattr(brain, 'source', ''), '本地规则引擎'),
        'brain_note': brain_note,
        'executor': getattr(executor, 'name', 'local'),
        'executor_name': 'Pixel Cake' if getattr(executor, 'name', '') == 'pixelcake'
                         else '本地 Pillow 管线',
        'plan': plan.to_dict(),
        'changed': [{'name': n, 'value': v} for n, v in plan.changed()],
        'ops': [o.get('op') for o in (plan.ops or [])],
        'summary': summary,
        'measure': {'lum': round(ctx['lum'], 3),
                    'lum_std': round(ctx['lum_std'], 3),
                    'sat': round(ctx['sat'], 3)},
    }


def _quality_hint(path, config=None):
    """若 Pixel Cake 在线且装了 pyiqa，上传并调 /score 取画质分；任何失败都返回 None。

    纯增强：拿不到评分不影响推荐（引擎自有测量）。
    """
    try:
        return score(path, config).get('scores') or None
    except Exception:                       # noqa: BLE001
        return None


def suggest_presets(path, exif=None, config=None):
    """本地 AI 预设建议：按画面特征推荐预设（film / portrait / 修复类 / 影调类…）。

    **仅建议，不改图**：是否采纳由用户决定（引擎 1.4.0 起）。全程本地、零云端 ——
    直接委托引擎的纯 Pillow 推荐器 `retouch.recommend_presets`（只用自身测量，无外呼），
    因此不受「决策层服务是否在线」影响，离线也能用。若 Pixel Cake + pyiqa 在线，
    （P4）会把画质评分作为额外证据接进推荐。
    """
    quality = _quality_hint(path, config)
    res = R.recommend_presets(path, exif, quality=quality)
    res['brain'] = 'local'
    res['brain_name'] = '本地规则引擎'
    res['engine'] = R.ENGINE_ID
    res['quality_source'] = 'pixelcake+pyiqa' if quality else None
    return res


def matting(path, config=None, return_rgba=False):
    """BiRefNet 抠图 / 去背景（P3）。需 Pixel Cake + BiRefNet 在线；否则抛错降级。

    本地零云端。返回 {out, rgba, note}：out 为保存的透明 PNG 路径。
    """
    if not path or not os.path.isfile(path):
        raise RuntimeError('文件不存在：%s' % path)
    cfg = config or {}
    url = cfg.get('cake_url') or PC.DEFAULT_CAKE_URL
    if not PC.probe_cake(url)[0]:
        raise RuntimeError('Pixel Cake 未运行（去背景依赖本地重模型）')
    try:
        c = PC.PixelCakeClient(url)
        up = c.upload(path)
        iid = up.get('image_id')
        if not iid:
            raise RuntimeError('Pixel Cake 上传失败')
        blob, hdr = c.matting(iid, return_rgba=return_rgba)
        if not blob:
            raise RuntimeError('BiRefNet 未返回结果')
        root, _ = os.path.splitext(path)
        out = root + ('_rgba.png' if return_rgba else '_matte.png')
        with open(out, 'wb') as f:
            f.write(blob)
        return {'out': out, 'rgba': bool(return_rgba),
                'note': 'BiRefNet 去背景完成（%d 字节）' % len(blob)}
    except Exception as e:                       # noqa: BLE001
        raise RuntimeError('去背景失败：%s' % e)


def score(path, config=None):
    """pyiqa 画质/美学评分（P4）。需 Pixel Cake + pyiqa 在线；否则抛错降级。

    本地零云端。返回 {scores, metrics, note}：
      scores  —— {指标名: 分数}（如 {'musiq': 52.5}）
      metrics —— 实际参与评分的指标名列表
    与 matting() 同风格：失败抛 RuntimeError，由上层（app.py）转成 error 回推。
    """
    if not path or not os.path.isfile(path):
        raise RuntimeError('文件不存在：%s' % path)
    cfg = config or {}
    url = cfg.get('cake_url') or PC.DEFAULT_CAKE_URL
    if not PC.probe_cake(url)[0]:
        raise RuntimeError('Pixel Cake 未运行（画质评分依赖 pyiqa）')
    try:
        c = PC.PixelCakeClient(url)
        up = c.upload(path)
        iid = up.get('image_id')
        if not iid:
            raise RuntimeError('Pixel Cake 上传失败')
        data = c.score(iid) or {}
        scores = data.get('scores') or {}
        if not scores:
            raise RuntimeError('pyiqa 未返回评分（可能未安装或指标全部加载失败）')
        return {'scores': {k: float(v) for k, v in scores.items()},
                'metrics': data.get('metrics') or list(scores.keys()),
                'note': 'pyiqa 画质评分完成：%s' % ', '.join(
                    '%s=%.2f' % (k, float(v)) for k, v in scores.items())}
    except Exception as e:                       # noqa: BLE001
        raise RuntimeError('画质评分失败：%s' % e)


def inpaint(image_path, mask_path, config=None, prompt=''):
    """AI 消除/重绘（Pixel Cake）：按蒙版抹掉区域并智能补全。

    image_path 原图；mask_path 蒙版 PNG（白＝要被消除的区域）。
    需 Pixel Cake 在线且装了 inpainting 模型（LaMa），否则抛错降级。
    返回 {out, note}。
    """
    import base64 as _b64
    if not image_path or not os.path.isfile(image_path):
        raise RuntimeError('原图不存在：%s' % image_path)
    if not mask_path or not os.path.isfile(mask_path):
        raise RuntimeError('蒙版不存在：%s' % mask_path)
    cfg = config or {}
    url = cfg.get('cake_url') or PC.DEFAULT_CAKE_URL
    if not PC.probe_cake(url)[0]:
        raise RuntimeError('Pixel Cake 未运行（AI 消除依赖本地重模型）')
    try:
        c = PC.PixelCakeClient(url)
        up = c.upload(image_path)
        iid = up.get('image_id')
        if not iid:
            raise RuntimeError('原图上传失败')
        mu = c.upload(mask_path)
        mid = mu.get('image_id')
        if not mid:
            raise RuntimeError('蒙版上传失败')
        blob, _hdr = c.inpaint(iid, mid, prompt=prompt or '')
        if not blob:
            raise RuntimeError('AI 消除未返回结果')
        root, ext = os.path.splitext(image_path)
        out = root + '_inpaint.jpg'
        with open(out, 'wb') as f:
            f.write(blob)
        return {'out': out, 'note': 'AI 消除完成（%d 字节）' % len(blob)}
    except Exception as e:                       # noqa: BLE001
        raise RuntimeError('AI 消除失败：%s' % e)

# -*- coding: utf-8 -*-
r"""决策层：把「这张图该怎么修」变成结构化修图指令（EditPlan）。

两个可互换的实现
────────────────
1. JevBrain —— 调本地 jev_local 的 HTTP 服务（真实契约，已核对源码）：
     POST http://<host>:<port>/v1/systemone
     Headers: Authorization: Bearer <api_key> / Content-Type: application/json
     Body: {"model":"jev-latest",
            "state": <字符串/对象：图像测量 + EXIF 上下文>,
            "questions": {"<key>":{"type":"choice|score|noul",
                                   "instructions": "...",
                                   "criteria": {...}|[...]}},
            "images": []}
     响应: {"model":..., "answers":{key:{"type":"choice","choice":..,"confidence":..}
                                        |{"type":"score","score":0..4,...}}, "usage":{...}}
   注意：jev_local 只是「用本地 LLM 复刻 Jev 的 HTTP 契约」，不是真的 Jev 权重；
   且它依赖另一个 llama.cpp 后端（默认 :8097）先跑起来。

2. LocalLLMBrain —— 调本地 LLM/VLM（Ollama 的 OpenAI 兼容端点，Windows 原生）：
     把引擎的客观测量（亮度/离散/饱和/EXIF + 用户目标）作为唯一输入，
     要求模型**只输出参数增量 JSON**；解析失败/未启动一律抛出，由上层兜底。

3. LocalBrain —— 纯规则、零依赖、永远可用。用测得的直方图统计 + EXIF
   （亮度/离散/饱和度/ISO/快门）推出参数增量。服务没装时就靠它。

三者产出同一种 EditPlan，执行层无需知道是谁做的决策。
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request

from .editplan import EditPlan, clamp, PARAM_KEYS

DEFAULT_JEV_URL = 'http://127.0.0.1:8180'   # 8080 常被别的程序占用；Jev 改用 8180
DEFAULT_JEV_KEY = 'local-dev'          # jev_local 的 JEV_API_KEY 默认值

# score 型问题的 5 档 → 参数增量（与服务返回的 0..4 顺序一致）
LEVELS = (-0.35, -0.15, 0.0, 0.15, 0.35)
PARAM_Q = ('brightness', 'contrast', 'saturation', 'denoise')

INTENT_CRITERIA = {
    '补光提亮': '画面偏暗、欠曝，优先提亮并找回暗部层次',
    '胶片风格': '追求胶片质感：柔和对比、略暖、轻微颗粒感',
    '人像美化': '主体是人，优先磨皮、肤色与五官优化',
    '风光通透': '风光场景，优先去灰、提通透、强化层次',
}


# ---------------------------------------------------------------- 本地 LLM（Ollama）
# Windows 原生即可跑（不像 jev_local 需要 WSL2）；Ollama 暴露 OpenAI 兼容端点。
DEFAULT_LLM_URL = 'http://127.0.0.1:11434/v1'
DEFAULT_LLM_MODEL = 'qwen3.5:4b'      # 6GB 显存甜点；也可 gemma4:e4b

LLM_SYSTEM = (
    '你是照片修图的参数决策器。只输出一个 JSON 对象，不要解释、不要 markdown 代码块。'
    'JSON 的键只能取自：%s。值是 -1.0 到 1.0 的浮点数，0 表示不改。'
    '可以额外给一个 "_intent" 字段（中文字符串）说明意图。'
    % '、'.join(PARAM_KEYS))


def is_local_url(url):
    """只允许回环地址 —— 按需求「智能修图全走本地，不碰线上」。"""
    try:
        from urllib.parse import urlparse
        host = (urlparse(url or '').hostname or '').lower()
    except Exception:
        return False
    return host in ('127.0.0.1', 'localhost', '::1', '0.0.0.0')


def assert_local(url, what):
    if not is_local_url(url):
        raise RuntimeError(
            '%s 配置了非本地地址（%s）。智能修图按设定只走本地推理，'
            '请把地址改回 127.0.0.1 / localhost。' % (what, url))


def plan_ops(goal, intent, params):
    """把「修图目标」翻译成有序的执行操作（模型调度）。

    Pixel Cake 的能力不是「一个接口打天下」，按目标分流：
      去杂物/穿帮/纹身 → 自动分割出掩膜 + LaMa 修复
      人像/磨皮        → enhance 带 skin_smooth
      补光             → relight（专用补光通道）
      换天             → sky/replace
      其余             → enhance 十六参数调色
    返回 ops 列表，交给执行层依次调用。
    """
    g = goal or ''
    ops = []
    import re as _re
    if any(k in g for k in ('去路人', '去杂物', '消除', '穿帮', '纹身', '电线')):
        mode = 'ground' if any(k in g for k in ('路人', '杂物', '电线', '背景')) else 'person'
        ops.append({'op': 'inpaint', 'mask_mode': mode,
                    'note': '自动分割掩膜 → LaMa 修复'})
    # 「换天」的自然说法很多：换个天空 / 换天 / 天空替换 / 把天换成星空…
    if _re.search(r'换.{0,4}(天|天空)', g) or '天空替换' in g:
        sky = 'starry' if any(k in g for k in ('星空', '银河', '夜')) else (
              'sunset' if any(k in g for k in ('日落', '晚霞', '黄昏')) else (
              'cloudy' if any(k in g for k in ('云', '阴')) else 'blue'))
        ops.append({'op': 'sky', 'sky_type': sky, 'blend': 0.7})
    if any(k in g for k in ('补光', '提亮', '欠曝')):
        ops.append({'op': 'relight',
                    'brightness': min(0.6, abs(params.get('brightness', 0.2)) + 0.25),
                    'warmth': max(0.0, params.get('warmth', 0.0))})
    if any(k in g for k in ('人像', '磨皮', '皮肤', '胡渣', '牙齿')) or intent == '人像美化':
        p = dict(params)
        p['skin_smooth'] = True
        ops.append({'op': 'enhance', 'params': p, 'skin_smooth': True})
    if any(k in g for k in ('瘦脸', '美型')):
        ops.append({'op': 'face-slim', 'strength': 0.3})
    if any(k in g for k in ('碎发', '头发')):
        ops.append({'op': 'hair-smooth', 'strength': 0.5})
    if not ops:
        ops.append({'op': 'enhance', 'params': dict(params)})
    return ops


def _post(url, payload, key, timeout=20.0):
    req = urllib.request.Request(
        url, data=json.dumps(payload, ensure_ascii=False).encode('utf-8'),
        headers={'Content-Type': 'application/json',
                 'Authorization': 'Bearer %s' % (key or '')},
        method='POST')
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8'))


def _get(url, key=None, timeout=3.0):
    req = urllib.request.Request(url, method='GET')
    if key:
        req.add_header('Authorization', 'Bearer %s' % key)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode('utf-8'))


def _port_open(url, timeout=0.35):
    """裸 socket 快速判断 host:port 是否可连（避免 HTTP 探测白等 timeout）。"""
    try:
        import socket
        from urllib.parse import urlparse
        u = urlparse(url or '')
        host = u.hostname or '127.0.0.1'
        port = u.port or (443 if u.scheme == 'https' else 80)
        socket.create_connection((host, port), timeout=timeout).close()
        return True
    except Exception:                          # noqa: BLE001
        return False


def probe_jev(base_url=DEFAULT_JEV_URL, key=DEFAULT_JEV_KEY, timeout=3.0):
    """探测 Jev 服务是否可用。返回 (ok, 详情字符串)。

    同样先用裸 socket 快速探端口：Jev 没装时（本机默认就是这么用的）
    HTTP 探测会白等 ~2s，而每次 smart_edit 都会走一遍。
    """
    if not _port_open(base_url, 0.35):
        return False, 'jev_local 未启动（端口不通）'
    try:
        info = _get(base_url.rstrip('/') + '/health', key=None, timeout=timeout)
        if info.get('status') == 'ok':
            return True, 'jev_local 在线（模型 %s）' % info.get('model', '?')
        return False, 'jev_local 响应异常'
    except urllib.error.HTTPError as e:
        return False, 'jev_local HTTP %s' % e.code
    except Exception as e:
        return False, 'jev_local 未启动（%s）' % type(e).__name__


class JevBrain:
    """调本地 jev_local 服务做决策；任何失败都自动退回 LocalBrain。"""

    source = 'jev'

    def __init__(self, base_url=DEFAULT_JEV_URL, key=DEFAULT_JEV_KEY,
                 model='jev-latest', timeout=25.0):
        assert_local(base_url, 'Jev 服务')
        self.base_url = (base_url or DEFAULT_JEV_URL).rstrip('/')
        self.key = key or DEFAULT_JEV_KEY
        self.model = model or 'jev-latest'
        self.timeout = timeout

    def available(self):
        return probe_jev(self.base_url, self.key)[0]

    def build_payload(self, context):
        """按真实契约构造请求体（choice 选意图 + score 定参数档位）。"""
        state = {
            'measurements': {
                'luma': context.get('lum'),
                'luma_std': context.get('lum_std'),
                'saturation': context.get('sat'),
            },
            'exif': {
                'iso': context.get('iso'),
                'shutter': context.get('shutter'),
                'aperture': context.get('ap'),
                'focal': context.get('focal'),
            },
            'goal': context.get('goal', ''),
        }
        questions = {
            'intent': {
                'type': 'choice',
                'instructions': '这张照片最适合采用哪种修图方向？',
                'criteria': dict(INTENT_CRITERIA),
            }
        }
        for p in PARAM_Q:
            questions[p] = {
                'type': 'score',
                'instructions': '%s 应该调整到什么程度？' % p,
                'criteria': [
                    '明显降低', '略微降低', '保持不变', '略微增强', '明显增强'
                ],
            }
        return {'model': self.model, 'state': json.dumps(state, ensure_ascii=False),
                'questions': questions, 'images': []}

    def decide(self, context):
        payload = self.build_payload(context)
        try:
            res = _post(self.base_url + '/v1/systemone', payload, self.key,
                        timeout=self.timeout)
        except Exception as e:
            raise RuntimeError('jev_local 调用失败：%s' % e)

        answers = res.get('answers') or {}
        params = {k: 0.0 for k in (
            'brightness', 'contrast', 'saturation', 'warmth', 'sharpness',
            'denoise', 'highlights', 'shadows', 'whites', 'blacks',
            'vibrance', 'clarity', 'tint')}
        for p in PARAM_Q:
            a = answers.get(p) or {}
            if a.get('type') == 'score':
                idx = a.get('score')
                try:
                    idx = int(round(float(idx)))
                except Exception:
                    continue
                idx = max(0, min(len(LEVELS) - 1, idx))
                params[p] = LEVELS[idx]
        # 意图 → 少量风格化补偿（Jev 只给方向，幅度由规则兜底）
        intent = '风光通透'
        ai = answers.get('intent') or {}
        if ai.get('type') == 'choice' and ai.get('choice') in INTENT_CRITERIA:
            intent = ai['choice']
        if intent == '胶片风格':
            params['warmth'] = clamp(params.get('warmth', 0) + 0.10)
            params['contrast'] = clamp(params.get('contrast', 0) + 0.05)
        elif intent == '补光提亮':
            params['shadows'] = clamp(params.get('shadows', 0) + 0.15)
        elif intent == '人像美化':
            params['clarity'] = clamp(params.get('clarity', 0) - 0.05)
        params['sharpness'] = clamp(params.get('sharpness', 0) + 0.15)

        ops = plan_ops(context.get('goal', ''), intent, params)
        return EditPlan(
            intent=intent, params=params, ops=ops,
            reason='jev_local 决策（%s）' % (res.get('model') or self.model),
            source='jev')


class LocalBrain:
    """零依赖规则决策：由实测统计 + EXIF 直接推出参数增量。"""

    source = 'local'

    def decide(self, context):
        lum = float(context.get('lum') or 0.0)
        std = float(context.get('lum_std') or 0.0)
        sat = float(context.get('sat') or 0.0)
        iso = int(context.get('iso') or 0)
        shutter = float(context.get('shutter') or 0)
        goal = (context.get('goal') or '').strip()

        p = {k: 0.0 for k in (
            'brightness', 'contrast', 'saturation', 'warmth', 'sharpness',
            'denoise', 'highlights', 'shadows', 'whites', 'blacks',
            'vibrance', 'clarity', 'tint')}
        why = []

        # 曝光
        if lum < 0.32:
            d = clamp((0.32 - lum) * 1.8, 0.0, 0.50)
            p['brightness'] = d
            p['shadows'] = clamp(0.10 + d * 0.4, 0.0, 0.35)
            p['blacks'] = clamp(0.05 + d * 0.2, 0.0, 0.20)
            why.append('偏暗（亮度 %.2f）→ 提亮 +%.2f' % (lum, d))
        elif lum > 0.68:
            d = clamp((lum - 0.68) * 1.6, 0.0, 0.45)
            p['highlights'] = -d
            p['whites'] = -clamp(d * 0.6, 0.0, 0.30)
            why.append('偏亮（亮度 %.2f）→ 压高光 %.2f' % (lum, d))

        # 反差
        if std < 0.13:
            d = clamp((0.19 - std) * 2.2, 0.0, 0.45)
            p['contrast'] = d
            p['clarity'] = clamp(0.10 + d * 0.3, 0.0, 0.30)
            why.append('发灰（离散 %.2f）→ 对比 +%.2f' % (std, d))
        elif std > 0.30:
            p['contrast'] = -clamp((std - 0.30) * 1.2, 0.0, 0.20)
            why.append('反差过强 → 略收对比')

        # 色彩
        if sat < 0.18:
            d = clamp((0.24 - sat) * 1.8, 0.0, 0.40)
            p['saturation'] = d
            p['vibrance'] = clamp(0.12 + d * 0.3, 0.0, 0.30)
            why.append('色彩偏淡（%.2f）→ 饱和 +%.2f' % (sat, d))
        elif sat > 0.45:
            p['saturation'] = -clamp((sat - 0.45) * 1.0, 0.0, 0.25)
            why.append('色彩过浓 → 略降饱和')

        # 噪点（EXIF 感知）
        if iso >= 6400:
            p['denoise'] = 0.50; why.append('ISO %d 极高感 → 强降噪' % iso)
        elif iso >= 3200:
            p['denoise'] = 0.30; why.append('ISO %d → 中降噪' % iso)
        elif iso >= 1600:
            p['denoise'] = 0.12; why.append('ISO %d → 轻降噪' % iso)
        if shutter >= 4.0:
            p['denoise'] = clamp(p['denoise'] + 0.20)
            why.append('长曝 %.0fs → 热噪降噪' % shutter)

        # 出图锐化（轻微，恒定）
        p['sharpness'] = 0.15

        # 目标意图（自然语言关键词）
        intent = '风光通透'
        g = goal.lower()
        if any(k in goal for k in ('胶片', 'film', '复古')):
            intent = '胶片风格'
            p['warmth'] = clamp(p['warmth'] + 0.12)
            p['contrast'] = clamp(p['contrast'] + 0.06)
            p['saturation'] = clamp(p['saturation'] - 0.05)
            why.append('目标「胶片感」→ 暖调 + 柔对比')
        elif any(k in goal for k in ('黑白', 'mono', '去色')):
            intent = '黑白'
            p['saturation'] = -1.0
            p['clarity'] = clamp(p['clarity'] + 0.15)
            why.append('目标「黑白」→ 去饱和 + 提清晰度')
        elif any(k in goal for k in ('人像', '磨皮', '皮肤')):
            intent = '人像美化'
            p['clarity'] = clamp(p['clarity'] - 0.08)
            why.append('目标「人像」→ 降清晰度（皮肤柔和）')
        elif any(k in g for k in ('清透', '通透', '去灰')):
            p['contrast'] = clamp(p['contrast'] + 0.10)
            p['clarity'] = clamp(p['clarity'] + 0.12)
            why.append('目标「通透」→ 对比 + 清晰度')

        ops = plan_ops(goal, intent, p)

        return EditPlan(intent=intent, params=p, ops=ops,
                        reason='；'.join(why) or '各项指标正常，仅做出图锐化',
                        source='local')


class LocalLLMBrain:
    """用本地小 LLM / VLM（Ollama 的 OpenAI 兼容端点）做决策。

    与 JevBrain 的区别：不依赖 WSL2 的 jev_local，直接在 Windows 上跑 Ollama。
    **输入是引擎的客观测量**（亮度/离散/饱和/EXIF + 用户目标），不是整张原图，
    因此又快又稳、且不需要把照片编码成 base64 传来传去。
    任何失败（未启动 / 超时 / 输出非合法 JSON）都抛出，由 get_brain 的
    available() 预筛 + ai.smart_edit 的兜底保证「绝不让决策层把修图链路弄挂」。
    """

    source = 'llm'

    def __init__(self, base_url=DEFAULT_LLM_URL, model=DEFAULT_LLM_MODEL,
                 timeout=30.0, key=None):
        assert_local(base_url, '本地 LLM 服务')
        self.base_url = (base_url or DEFAULT_LLM_URL).rstrip('/')
        self.model = model or DEFAULT_LLM_MODEL
        self.timeout = timeout
        self.key = key

    def available(self):
        """探测 Ollama 是否在线且（大致）有我们要的模型。失败一律返回 False。

        先用**裸 socket** 快速探端口：未监听时 Windows 未必立刻回 RST，
        直接发 HTTP 会白等一整个 timeout（实测 2.0s）。端口不通就秒退，
        这样「没装 Ollama」的用户几乎感觉不到额外延迟。
        """
        if not _port_open(self.base_url, 0.35):
            return False
        try:
            info = _get(self.base_url + '/models', self.key, timeout=2.5)
        except Exception:                     # noqa: BLE001
            return False
        data = info.get('data') or info.get('models') or []
        names = []
        for d in data:
            if isinstance(d, dict):
                names.append(str(d.get('id') or d.get('name') or ''))
            elif isinstance(d, str):
                names.append(d)
        if not names:
            return False
        if self.model in names:
            return True
        base = self.model.split(':')[0]        # 容忍 qwen3.5:4b vs qwen3.5:4b-q4_K_M
        return any(n.startswith(base) for n in names)

    def build_payload(self, context):
        ctx = context or {}
        goal = (ctx.get('goal') or '').strip() or '（未指定，按画面问题自行判断）'
        measures = (
            '亮度 %.3f（0 全黑 / 1 全白）· 离散 %.3f · 平均饱和 %.3f · '
            'ISO %s · 快门 %ss'
            % (float(ctx.get('lum') or 0), float(ctx.get('lum_std') or 0),
               float(ctx.get('sat') or 0), ctx.get('iso') or 0,
               ctx.get('shutter') or 0))
        user = ('画面客观测量：%s\n用户目标：%s\n\n'
                '请只输出参数增量 JSON。示例：'
                '{"brightness": 0.2, "shadows": 0.3, "_intent": "补光提亮"}'
                % (measures, goal))
        return {
            'model': self.model,
            'temperature': 0,
            'stream': False,
            'messages': [{'role': 'system', 'content': LLM_SYSTEM},
                         {'role': 'user', 'content': user}],
        }

    @staticmethod
    def _extract_text(res):
        try:
            return (res['choices'][0]['message']['content'] or '')
        except Exception as e:                 # noqa: BLE001
            raise RuntimeError('LLM 响应结构异常：%s' % e)

    @staticmethod
    def _parse_params(text):
        """从模型输出里抽出 JSON 参数对象；抽不出就抛（由上层兜底）。"""
        t = (text or '').strip()
        if t.startswith('```'):
            t = t.strip('`')
            if '\n' in t:
                first, rest = t.split('\n', 1)
                t = rest if first.strip().lower() in ('json', '') else t
        i, j = t.find('{'), t.rfind('}')
        if i < 0 or j <= i:
            raise ValueError('LLM 输出里找不到 JSON 对象')
        obj = json.loads(t[i:j + 1])
        if not isinstance(obj, dict):
            raise ValueError('LLM 输出的 JSON 不是对象')
        out = {}
        for k, v in obj.items():
            if k == '_intent':
                out['_intent'] = str(v)[:40]
            elif k in PARAM_KEYS:              # 未知键直接忽略（不信模型乱给）
                try:
                    out[k] = float(v)
                except Exception:              # noqa: BLE001
                    continue
        return out

    def decide(self, context):
        res = None
        try:
            res = _post(self.base_url + '/chat/completions',
                        self.build_payload(context), self.key,
                        timeout=self.timeout)
        except Exception as e:                 # noqa: BLE001
            raise RuntimeError('本地 LLM 调用失败：%s' % e)
        raw = self._parse_params(self._extract_text(res))
        intent = raw.pop('_intent', None) or '本地 LLM 建议'
        p = {k: clamp(raw.get(k, 0.0)) for k in PARAM_KEYS}
        ops = plan_ops(context.get('goal', ''), intent, p)
        return EditPlan(intent=intent, params=p, ops=ops,
                        reason='本地 LLM（%s）依据画面测量给出参数' % self.model,
                        source='llm')


def get_brain(config=None):
    """按配置取决策器，三层优先级（装了就用、没装自动往下退）：

      1. JevBrain      —— jev_local（需 WSL2），装了才用
      2. LocalLLMBrain —— Ollama 本地 LLM（Windows 原生），装了才用
      3. LocalBrain    —— 纯规则，永远可用（兜底）

    每层都要 available() 通过才返回；探测失败通常瞬间返回（连接被拒），
    不会给「什么都没装」的用户增加可感延迟。
    """
    cfg = config or {}
    if cfg.get('jev', True):
        b = JevBrain(cfg.get('jev_url') or DEFAULT_JEV_URL,
                     cfg.get('jev_key') or DEFAULT_JEV_KEY,
                     cfg.get('jev_model') or 'jev-latest')
        if b.available():
            return b
    if cfg.get('llm', True):
        try:
            lb = LocalLLMBrain(cfg.get('llm_url') or DEFAULT_LLM_URL,
                               cfg.get('llm_model') or DEFAULT_LLM_MODEL)
            if lb.available():
                return lb
        except Exception:                      # noqa: BLE001 - 配置非法就当没装
            pass
    return LocalBrain()

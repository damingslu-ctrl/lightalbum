# -*- coding: utf-8 -*-
r"""修图指令计划（决策层与执行层之间的统一契约）。

为什么要有这一层
────────────────
决策层（Jev / 本地规则）只负责「这张图该怎么修」，执行层（Pixel Cake /
本地 Pillow 管线）只负责「按指令改像素」。两层要能自由替换，就必须有
一份双方都认的中间表示 —— 这就是 EditPlan。

参数空间对齐 Pixel Cake 的 /enhance（16 个 Lightroom 风格参数），
取值统一为 **-1.0 .. +1.0 的增量**（0 = 不改）。本地管线会把这些增量
映射成自己的增益系数，Pixel Cake 直接原样提交。
"""
from __future__ import annotations

# 16 个调色参数（与 Pixel Cake /enhance 字段一一对应）
PARAM_KEYS = (
    'brightness', 'contrast', 'saturation', 'warmth', 'sharpness', 'denoise',
    'highlights', 'shadows', 'whites', 'blacks', 'vibrance', 'clarity',
    'tint',
)

# 可批量执行的操作名（对应 Pixel Cake 的路由）
OP_ENHANCE = 'enhance'
OP_RELIGHT = 'relight'
OP_INPAINT = 'inpaint'
OP_FACE_SLIM = 'face-slim'
OP_HAIR = 'hair-smooth'
OP_SKY = 'sky/replace'
OP_COLOR_MATCH = 'color-match'

# 风格预设名（Pixel Cake 内置 9 款，具体名称以服务端为准，未知时传 None）
FILTER_NONE = None


def blank_params():
    return {k: 0.0 for k in PARAM_KEYS}


def clamp(v, lo=-1.0, hi=1.0):
    try:
        v = float(v)
    except Exception:
        return 0.0
    if v != v:            # NaN
        return 0.0
    return max(lo, min(hi, v))


class EditPlan:
    """一份可执行的修图指令。

    intent   人类可读的意图（"提亮 + 胶片感"）
    params   16 个调色增量（dict）
    ops      有序操作列表：[{op, params|args}]
    filter   风格预设名（None = 不用）
    reason   决策依据（给用户在 UI 里看）
    """

    def __init__(self, intent='', params=None, ops=None, filter_name=None,
                 reason='', source='local'):
        self.intent = intent or ''
        p = blank_params()
        if params:
            for k in PARAM_KEYS:
                if k in params:
                    p[k] = clamp(params[k])
        self.params = p
        self.ops = ops or [{'op': OP_ENHANCE, 'params': dict(p)}]
        self.filter = filter_name
        self.reason = reason or ''
        self.source = source        # 'jev' | 'local'

    # ---- 序列化
    def to_dict(self):
        return {
            'intent': self.intent,
            'params': dict(self.params),
            'ops': self.ops,
            'filter': self.filter,
            'reason': self.reason,
            'source': self.source,
        }

    @staticmethod
    def from_dict(d):
        return EditPlan(
            intent=d.get('intent', ''),
            params=d.get('params') or {},
            ops=d.get('ops'),
            filter_name=d.get('filter'),
            reason=d.get('reason', ''),
            source=d.get('source', 'local'),
        )

    # ---- 供 UI 展示的「改了什么」
    def changed(self):
        """返回非零参数列表 [(中文名, 值)]，只列真正动的项。"""
        names = {
            'brightness': '亮度', 'contrast': '对比度', 'saturation': '饱和度',
            'warmth': '色温', 'sharpness': '锐度', 'denoise': '降噪',
            'highlights': '高光', 'shadows': '阴影', 'whites': '白色',
            'blacks': '黑色', 'vibrance': '自然饱和度', 'clarity': '清晰度',
            'tint': '色调',
        }
        out = []
        for k in PARAM_KEYS:
            v = self.params.get(k, 0.0)
            if abs(v) >= 0.01:
                out.append((names.get(k, k), round(v, 2)))
        return out

    def summary(self):
        ch = self.changed()
        if not ch:
            return '画面各项已在目标区间，无需调整'
        return '、'.join('%s %+.2f' % (n, v) for n, v in ch)


# ---------------------------------------------------------------- 参数意图化
def describe_plan(plan):
    """把 plan 翻成一句人话（UI 顶部用）。"""
    bits = []
    for name, v in plan.changed():
        if v > 0:
            bits.append('%s +' % name)
        else:
            bits.append('%s −' % name)
    if not bits:
        return '保持原样'
    return ' '.join(bits[:6])

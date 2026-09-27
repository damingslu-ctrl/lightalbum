# -*- coding: utf-8 -*-
"""1.9.10 三处修复：① 全部作品白屏 ② 首页图带飞快 ③ 开场动画等真正加载完。"""
import io
import sys

ROOT = r'D:\Test\workbuddy\2026-09-18-09-00-13'
HTML = ROOT + r'\index.html'
APP = ROOT + r'\client\lightalbum\app.py'


def apply(path, edits, label):
    s = io.open(path, encoding='utf-8').read()
    for i, (old, new) in enumerate(edits, 1):
        c = s.count(old)
        if c != 1:
            print('FAIL %s edit %d: count=%d' % (label, i, c))
            sys.exit(1)
        s = s.replace(old, new)
        print('  ok %s edit %d' % (label, i))
    io.open(path, 'w', encoding='utf-8').write(s)
    print('WROTE %s (%d bytes)' % (label, len(s)))
    return s


# ---------- index.html ----------
h_edits = [
    # ① 无 AI 分类时退回全量分页视图，避免「全部作品」白屏
    (
        "  if(!state.series && !state.cat){\n"
        "    renderGroupedGallery(list, grid, empty, countEl);\n"
        "    return;\n"
        "  }",
        "  // 1.9.10：分组视图只展示「有 AI 分类」的照片；若一张都没分类过（分类器未跑 /\n"
        "  // 分类丢失），它会渲染成空的 —— 必须退回全量分页视图，否则「全部作品」白屏\n"
        "  // （实测：库里 6349 张的 category 全为空，分组视图 0 张）。\n"
        "  var _hasVault = false;\n"
        "  try{ _hasVault = !!(VAULT && VAULT.items && VAULT.items.length); }catch(e){ _hasVault = false; }\n"
        "  var _hasCats = list.some(p=>catKey(p));\n"
        "  if(!state.series && !state.cat && (_hasCats || _hasVault)){\n"
        "    renderGroupedGallery(list, grid, empty, countEl);\n"
        "    return;\n"
        "  }",
    ),
    # ② 图带上限 24 张（原来 = 照片总数，6345 张 → 12690 节点）
    (
        "  const total = Math.max(10, N);",
        "  // 1.9.10：上限 24 张。原来 total = 照片总数（实测 6345），双份后 12690 个节点，\n"
        "  // 既拖慢首屏，又让图带宽度随张数膨胀、而动画时长固定 360s → 观感「飞快」。\n"
        "  const total = Math.min(24, Math.max(10, N));",
    ),
    # ② 动画时长随张数走：恒定 3s 划过一张，与库大小无关
    (
        "  rowA.innerHTML = rowHtml + rowHtml;                 // 双份实现无缝循环",
        "  rowA.innerHTML = rowHtml + rowHtml;                 // 双份实现无缝循环\n"
        "  // 动画走完 -50%（正好一份 total 张），故时长 = total × 每张停留秒数，\n"
        "  // 使滚动速度与库里照片总数无关，恒定 3s 划过一张。\n"
        "  rowA.style.animationDuration = (total * 3) + 's';",
    ),
    # ③ 本地库握手完成信号（供客户端判断「加载完」）
    (
        "  tryLocalLibrary(window.__libPort);",
        "  // 1.9.10：开场动画的「加载完成」信号 —— 本地库握手结束（成功/失败都算结束）后置位，\n"
        "  // 客户端据此淡出 splash；只看 loadFinished 会在照片还没拉到时就露出空画廊。\n"
        "  tryLocalLibrary(window.__libPort).then(\n"
        "    ()=>{ window.__libHandshake = true; },\n"
        "    ()=>{ window.__libHandshake = true; }\n"
        "  );",
    ),
]
apply(HTML, h_edits, 'index.html')

# ---------- app.py ----------
a_edits = [
    (
        "    # 页面（含 QWebEngine 渲染）加载完成后淡出开场动画；\n"
        "    # 8s 兜底淡出，极端慢启动也绝不挡界面。\n"
        "    w.view.loadFinished.connect(lambda _ok: splash.finish())\n"
        "    QTimer.singleShot(8000, splash.finish)",
        "    # 1.9.10：开场动画要等「内容真的就绪」再淡出。loadFinished 只代表 HTML 解析完，\n"
        "    # 此刻照片还没从本地库取回、画廊仍是空的，直接淡出会露出空画廊（用户反馈）。\n"
        "    # 判定：本地库握手完成 且（库为空 或 画廊已渲染出卡片）；15s 硬兜底绝不挡界面。\n"
        "    _splash_state = {'done': False}\n"
        "\n"
        "    def _finish_splash():\n"
        "        if _splash_state['done']:\n"
        "            return\n"
        "        _splash_state['done'] = True\n"
        "        try:\n"
        "            _ready_timer.stop()\n"
        "        except Exception:\n"
        "            pass\n"
        "        splash.finish()\n"
        "\n"
        "    READY_JS = (\n"
        "        \"(function(){\"\n"
        "        \"if(window.__libHandshake!==true) return 0;\"\n"
        "        \"var n=(typeof PHOTOS!=='undefined'&&PHOTOS)?PHOTOS.length:0;\"\n"
        "        \"if(n===0) return 1;\"\n"
        "        \"return document.querySelectorAll('.g-item').length>0?1:0;\"\n"
        "        \"})()\"\n"
        "    )\n"
        "\n"
        "    def _check_ready():\n"
        "        if _splash_state['done']:\n"
        "            return\n"
        "        try:\n"
        "            w.view.page().runJavaScript(\n"
        "                READY_JS, lambda v: _finish_splash() if v == 1 else None)\n"
        "        except Exception:\n"
        "            _finish_splash()\n"
        "\n"
        "    _ready_timer = QTimer()\n"
        "    _ready_timer.setInterval(300)\n"
        "    _ready_timer.timeout.connect(_check_ready)\n"
        "    _ready_timer.start()\n"
        "    QTimer.singleShot(15000, _finish_splash)",
    ),
]
apply(APP, a_edits, 'app.py')
print('ALL EDITS APPLIED')

# -*- coding: utf-8 -*-
"""
光影集 · 统一回归脚本（CI 一把梭）

把「改功能 → 验证 → 打包」链路里必须实跑的九套件合并成一次调用：
  1. test_retouch_engine.py      引擎算子级（纯 Pillow，合成图，无外网）
  2. test_retouch_ai.py          白平衡/亮度/「均衡图不乱修」
  3. test_ipc.py                 单实例 IPC（连接即唤起 + 回执；卡死实例不进僵尸）
  4. test_local_services.py      本地 AI/服务自启与降级（分类器离线/Pixel Cake/Jev 端口）
       5. node check.js               权威页：语法/ID/函数/外链/地图下钻/完整性
       6. node test_viewer.js         客户端页查看器与进度条
       7. test_inpaint_bridge.py     内嵌编辑器→AI 消除桥接（stub PixelCake，无外网）
       8. test_async_bridge.py       AI 动作不阻塞 GUI（子线程执行 + 回推主线程）

约定（见 ITERATION_GUIDE.md §9）：测试只依赖本地、不联网、**必须实际执行**。
本脚本真实拉起各套件并解析其绿/红标记，输出单一结论：
  - 全部通过 → 打印 `ALL GREEN` 并以 exit 0 结束；
  - 任一失败 → 打印 `REGRESSION FAILED` 并以 exit 1 结束（可直接挂 CI）。

用法：
  python run_all_tests.py                 # 跑全部
  python run_all_tests.py --step engine   # 只跑某一步（engine|ai|ipc|services|inpaint|async|web|viewer）
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT = os.path.join(HERE, 'client')
VENV_PY = os.path.join(CLIENT, 'venv', 'Scripts', 'python.exe')
# MCP 桥接测试需要装了 mcp 的专用 venv（与客户端 venv 隔离）
VENV_MCP_PY = os.environ.get('VENV_MCP_PY') or r'D:\lightalbum_ai\venv_mcp\Scripts\python.exe'


def _run(cmd, cwd):
    """跑子进程，返回 (returncode, combined_text)。"""
    proc = subprocess.run(
        cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        encoding='utf-8', errors='replace',
    )
    return proc.returncode, proc.stdout


def _parse_pair(text, key):
    """从输出里抓 `KEY pass=N fail=M`，返回 (pass, fail) 或 None。"""
    m = re.search(r'%s pass=(\d+) fail=(\d+)' % re.escape(key), text)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


def step_engine():
    rc, out = _run([VENV_PY, 'test_retouch_engine.py'], CLIENT)
    pr = _parse_pair(out, 'RETOUCH_ENGINE_TEST')
    if pr is None:
        return False, '未捕获 RETOUCH_ENGINE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_ai():
    rc, out = _run([VENV_PY, 'test_retouch_ai.py'], CLIENT)
    pr = _parse_pair(out, 'RETOUCH_AI_TEST')
    if pr is None:
        return False, '未捕获 RETOUCH_AI_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_ipc():
    rc, out = _run([VENV_PY, 'test_ipc.py'], CLIENT)
    pr = _parse_pair(out, 'IPC_TEST')
    if pr is None:
        return False, '未捕获 IPC_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_services():
    rc, out = _run([VENV_PY, 'test_local_services.py'], CLIENT)
    pr = _parse_pair(out, 'LOCAL_SERVICES_TEST')
    if pr is None:
        return False, '未捕获 LOCAL_SERVICES_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_inpaint():
    rc, out = _run([VENV_PY, 'test_inpaint_bridge.py'], CLIENT)
    pr = _parse_pair(out, 'INPAINT_BRIDGE_TEST')
    if pr is None:
        return False, '未捕获 INPAINT_BRIDGE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_async():
    rc, out = _run([VENV_PY, 'test_async_bridge.py'], CLIENT)
    pr = _parse_pair(out, 'ASYNC_BRIDGE_TEST')
    if pr is None:
        return False, '未捕获 ASYNC_BRIDGE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_rating():
    rc, out = _run([VENV_PY, 'test_rating_heic.py'], CLIENT)
    pr = _parse_pair(out, 'RATING_HEIC_TEST')
    if pr is None:
        return False, '未捕获 RATING_HEIC_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_frame():
    rc, out = _run([VENV_PY, 'test_frame.py'], CLIENT)
    pr = _parse_pair(out, 'FRAME_TEST')
    if pr is None:
        return False, '未捕获 FRAME_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_facets():
    rc, out = _run([VENV_PY, 'test_facets.py'], CLIENT)
    pr = _parse_pair(out, 'FACET_TEST')
    if pr is None:
        return False, '未捕获 FACET_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_face():
    rc, out = _run([VENV_PY, 'test_face.py'], CLIENT)
    pr = _parse_pair(out, 'FACE_TEST')
    if pr is None:
        return False, '未捕获 FACE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_culling():
    rc, out = _run([VENV_PY, 'test_culling.py'], CLIENT)
    pr = _parse_pair(out, 'CULLING_TEST')
    if pr is None:
        return False, '未捕获 CULLING_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_hub():
    rc, out = _run([VENV_PY, 'test_enginehub.py'], CLIENT)
    pr = _parse_pair(out, 'ENGINEHUB_TEST')
    if pr is None:
        return False, '未捕获 ENGINEHUB_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)

def step_exte2e():
    rc, out = _run([VENV_PY, 'test_extedit_e2e.py'], CLIENT)
    pr = _parse_pair(out, 'EXTEDIT_E2E')
    if pr is None:
        return False, '未捕获 EXTEDIT_E2E 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_enhance():
    rc, out = _run([VENV_PY, 'test_enhance.py'], CLIENT)
    pr = _parse_pair(out, 'ENHANCE_TEST')
    if pr is None:
        return False, '未捕获 ENHANCE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_hlse2e():
    rc, out = _run([VENV_PY, 'test_headless_e2e.py'], CLIENT)
    pr = _parse_pair(out, 'HEADLESS_E2E')
    if pr is None:
        return False, '未捕获 HEADLESS_E2E 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_mcp():
    if not os.path.exists(VENV_MCP_PY):
        return True, 'SKIP (venv_mcp 缺失: %s)' % VENV_MCP_PY
    rc, out = _run([VENV_MCP_PY, 'test_mcp.py'], CLIENT)
    if 'SKIPPED' in out:
        return True, 'SKIP (mcp 未装)'
    pr = _parse_pair(out, 'MCP_TEST')
    if pr is None:
        return False, '未捕获 MCP_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def step_selbridge():
    rc, out = _run([VENV_PY, 'test_selbridge.py'], CLIENT)
    pr = _parse_pair(out, 'SELBRIDGE_TEST')
    if pr is None:
        return False, '未捕获 SELBRIDGE_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


def _node():
    return shutil.which('node') or 'node'


def step_web():
    node = _node()
    if not shutil.which('node') and not os.path.exists(node):
        return False, 'node 不可用，请确认 Node 22 在 PATH'
    rc, out = _run([node, 'check.js'], HERE)
    if rc != 0:
        return False, 'check.js exit=%d' % rc
    ok_syntax = 'SYNTAX_OK' in out
    ok_smoke = 'SMOKE_DONE' in out
    if not (ok_syntax and ok_smoke):
        miss = [n for n, ok in (('SYNTAX_OK', ok_syntax), ('SMOKE_DONE', ok_smoke)) if not ok]
        return False, '缺失标记: %s' % ','.join(miss)
    return True, 'SYNTAX_OK + SMOKE_DONE'


def step_viewer():
    node = _node()
    if not shutil.which('node') and not os.path.exists(node):
        return False, 'node 不可用，请确认 Node 22 在 PATH'
    rc, out = _run([node, 'test_viewer.js'], HERE)
    pr = _parse_pair(out, 'VIEWER_TEST')
    if pr is None:
        return False, '未捕获 VIEWER_TEST 汇总行（exit=%d）' % rc
    p, f = pr
    if rc != 0 or f != 0:
        return False, 'pass=%d fail=%d (exit=%d)' % (p, f, rc)
    return True, 'pass=%d fail=%d' % (p, f)


STEPS = {
    'engine':   ('引擎算子级', step_engine),
    'ai':       ('AI 白平衡/亮度/不乱修', step_ai),
    'ipc':      ('单实例 IPC', step_ipc),
    'services': ('本地服务自启/降级', step_services),
    'inpaint':  ('AI 消除桥接', step_inpaint),
    'async':    ('AI 异步不卡死', step_async),
    'rating':   ('HEIC/EXIF/星级评分', step_rating),
    'frame':    ('相框/EXIF 水印', step_frame),
    'facets':   ('EXIF 器材筛选条', step_facets),
    'face':     ('人物识别（聚类/库列/前端钩子）', step_face),
    'culling':  ('选片引擎（去重/模糊/曝光/评分）', step_culling),
    'hub':      ('引擎调度器 Engine-Hub', step_hub),
    'exte2e':   ('外部编辑回灌 E2E', step_exte2e),
    'hlse2e':   ('无头批量渲染 E2E', step_hlse2e),
    'enhance':  ('分路由 AI 美图（lut3d/路由/LUT 数学）', step_enhance),
    'mcp':      ('MCP 桥接（6 工具）', step_mcp),
    'selbridge': ('选择桥真实服务端 roundtrip', step_selbridge),
    'web':      ('权威页 check.js', step_web),
    'viewer':   ('客户端页 test_viewer.js', step_viewer),
}


def main():
    only = None
    if '--step' in sys.argv:
        i = sys.argv.index('--step')
        if i + 1 < len(sys.argv):
            only = sys.argv[i + 1]
    if only is not None and only not in STEPS:
        print('未知 --step: %s（可选: %s）' % (only, '/'.join(STEPS)))
        return 2

    print('光影集 · 统一回归')
    print('  venv python : %s' % (VENV_PY if os.path.exists(VENV_PY) else '缺失!'))
    print('  node        : %s' % (_node()))
    print('-' * 56)

    results = []
    for key, (label, fn) in STEPS.items():
        if only is not None and key != only:
            continue
        try:
            ok, detail = fn()
        except Exception as e:  # noqa: BLE001 - 套件崩溃也算失败
            ok, detail = False, '异常: %s' % e
        mark = 'PASS' if ok else 'FAIL'
        results.append((key, ok))
        print('  [%s] %-26s %s' % (mark, label, detail))

    print('-' * 56)
    n_fail = sum(1 for _, ok in results if not ok)
    if n_fail == 0:
        print('ALL GREEN  (%d 套件全过)' % len(results))
        return 0
    failed = ', '.join(k for k, ok in results if not ok)
    print('REGRESSION FAILED  (%d 失败: %s)' % (n_fail, failed))
    return 1


if __name__ == '__main__':
    sys.exit(main())

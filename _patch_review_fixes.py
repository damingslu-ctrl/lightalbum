# -*- coding: utf-8 -*-
"""一次性补丁：自查修复 6 处（幂等，逐条断言）。"""
import io

B = r'D:\Test\workbuddy\2026-09-18-09-00-13'


def rd(p):
    return io.open(p, encoding='utf-8', newline='').read()


def wr(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


# ---- 1) develop_engine.py：is16 按 dtype 判定（修全黑图 bug）----
p = B + r'\client\lightalbum\core\engines\develop_engine.py'
s = rd(p)
sep = '\r\n' if '\r\n' in s else '\n'
old = "        is16 = (arr.dtype.itemsize == 2 and int(arr.max()) > 255) if arr.size else False"
new = "        # 按 dtype 判定：uint16 全暗图（max<=255）也算 16bit，\n" \
      "        # 否则 _to_u8 右移 8 位会把整幅图写成全黑（自查发现的真 bug）\n" \
      "        is16 = arr.dtype.itemsize == 2"
assert s.count(old) == 1, 'dev1=%d' % s.count(old)
s = s.replace(old, new)

old = sep.join([
"try:  # stdlib imghdr 在 3.13 被移除，自写扩展名判断",
"    _RAW_EXTS = {'.raw', '.cr2', '.cr3', '.nef', '.arw', '.dng', '.raf',",
"                 '.orf', '.rw2', '.pef', '.srw', '.nrw', '.kdc', '.dcr',",
"                 '.mos', '.erf', '.3fr', '.fff', '.iiq', '.x3f'}",
"except Exception:  # pragma: no cover",
"    _RAW_EXTS = set()"])
new = sep.join([
"# RAW 扩展名判断（stdlib imghdr 在 3.13 被移除，自写）",
"_RAW_EXTS = {'.raw', '.cr2', '.cr3', '.nef', '.arw', '.dng', '.raf',",
"             '.orf', '.rw2', '.pef', '.srw', '.nrw', '.kdc', '.dcr',",
"             '.mos', '.erf', '.3fr', '.fff', '.iiq', '.x3f'}"])
assert s.count(old) == 1, 'dev2=%d' % s.count(old)
s = s.replace(old, new)
wr(p, s)
print('DEV_OK')

# ---- 2) enginehub.py：排队取消后从 _running 摘除 ----
p = B + r'\client\lightalbum\core\enginehub.py'
s = rd(p)
old = sep.join([
"        while not self._capacity_ok(eng, weight):",
"            if handle.cancelled():",
"                return",
"            time.sleep(0.1)",
"        if handle.cancelled():",
"            return"])
new = sep.join([
"        while not self._capacity_ok(eng, weight):",
"            if handle.cancelled():",
"                self._running.pop(handle.id, None)   # 防泄漏：取消即摘除",
"                return",
"            time.sleep(0.1)",
"        if handle.cancelled():",
"            self._running.pop(handle.id, None)",
"            return"])
assert s.count(old) == 1, 'hub=%d' % s.count(old)
s = s.replace(old, new)
wr(p, s)
print('HUB_OK')

# ---- 3) registry.py docstring 补 headless ----
p = B + r'\client\lightalbum\core\engines\registry.py'
s = rd(p)
old = "              ExternalEditorEngine（编辑器路径经 task_set_editor 或"
new = "              HeadlessRenderEngine（darktable-cli/art-cli 无头渲染，\n" \
      "              未配置二进制时降级）\n" \
      "              ExternalEditorEngine（编辑器路径经 task_set_editor 或"
assert s.count(old) == 1, 'reg=%d' % s.count(old)
s = s.replace(old, new)
wr(p, s)
print('REGISTRY_DOC_OK')

# ---- 4) native_bridge.js：按钮文案补 emoji ----
p = B + r'\client\resources\native_bridge.js'
s = rd(p)
old = "      xe.textContent = '外部编辑器';"
new = "      xe.textContent = '\\ud83c\\udfa8 外部编辑器';"
assert s.count(old) == 1, 'bridge=%d' % s.count(old)
s = s.replace(old, new)
wr(p, s)
print('BRIDGE_OK')

# ---- 5) test_enginehub.py：sidecar 用例走真分支 + 删死代码 ----
p = B + r'\client\test_enginehub.py'
s = rd(p)
old = sep.join([
"    # 侧车文件缺失 → FAILED",
"    eng3 = HeadlessRenderEngine()",
"    eng3._bins = {'darktable': None, 'art': None}",
"    jh2 = hub.submit('headless', 'render', src,",
"                     os.path.join(tmp, 'o2.jpg'),",
"                     sidecar=os.path.join(tmp, 'missing.xmp'))",
"    jh2.wait(10)",
"    check('Headless: sidecar 缺失作业 FAILED',",
"          jh2.state == JobState.FAILED, 'state=%s' % jh2.state)",
"",
"    # 假渲染器（.bat 模拟 darktable-cli：copy in→out）端到端",
"    bat = os.path.join(tmp, 'fake_darktable_cli.bat')",
"    with open(bat, 'w') as fh:",
"        fh.write('@echo off\\r\\n'",
"                 'if \"%~3\"==\"\" (copy /Y \"%~1\" \"%~2\" >nul) '",
"                 'else (copy /Y \"%~1\" \"%~3\" >nul)\\r\\n')"])
new = sep.join([
"    # 假渲染器（.bat 模拟 darktable-cli：copy in→out）端到端",
"    bat = os.path.join(tmp, 'fake_darktable_cli.bat')",
"    with open(bat, 'w') as fh:",
"        fh.write('@echo off\\r\\n'",
"                 'if \"%~3\"==\"\" (copy /Y \"%~1\" \"%~2\" >nul) '",
"                 'else (copy /Y \"%~1\" \"%~3\" >nul)\\r\\n')"])
assert s.count(old) == 1, 't1=%d' % s.count(old)
s = s.replace(old, new)

old = "    eng4 = HeadlessRenderEngine()"
assert s.count(old) == 1, 't2=%d' % s.count(old)
new = sep.join([
"    # 侧车文件缺失：配置了二进制才会走到 sidecar 检查（此前误配 None，",
"    # 前置的二进制检查先抛错，测试通过是假象——自查修正）",
"    eng3 = HeadlessRenderEngine()",
"    eng3._bins = {'darktable': sys.executable, 'art': None}",
"    jh2 = hub.submit('headless', 'render', src,",
"                     os.path.join(tmp, 'o2.jpg'),",
"                     sidecar=os.path.join(tmp, 'missing.xmp'))",
"    jh2.wait(10)",
"    check('Headless: sidecar 缺失作业 FAILED（真走到 sidecar 检查）',",
"          jh2.state == JobState.FAILED and isinstance(jh2.error,",
"                                                      FileNotFoundError),",
"          'state=%s error=%r' % (jh2.state, jh2.error))",
"",
"    eng4 = HeadlessRenderEngine()"])
s = s.replace(old, new, 1)
wr(p, s)
print('TEST_OK')

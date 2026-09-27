# -*- coding: utf-8 -*-
"""追加 1.9.35 灯箱尺寸记忆（脚本文件，避免 -c 反引号被 bash 命令替换）。"""
import io

P = r"D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-27.md"
s = io.open(P, encoding="utf-8").read()

entry = (
    "\n"
    "## 灯箱图片不铺满【1.9.35，已交付】\n"
    "- 触发：用户「图库点击图片弹出来的图片不用铺满，稍微小一点」。\n"
    "- 改动（脚本 patch_lightbox_1935.py，rep/计数断言）：index.html 灯箱 `.lb-img` 由\n"
    "  `width:96vw;height:96vh` 收为 `width:90vw;height:85vh`。\n"
    "- 原理：`.lb-stage` 是 `.lb-img` 内的绝对定位 `inset:0`，故 stage 尺寸 = `.lb-img`\n"
    "  尺寸；`lbFit()` 读 stage 的 getBoundingClientRect 把原图等比缩放进该框。缩小\n"
    "  `.lb-img` 后，弹出的大图四周（尤其竖向）留出明显边距，不再是近全屏满铺。\n"
    "- 不动：灯箱动画（lbIn/lbPop）、`.lb-stage img` 的 transform/zoom 查看器、`lbFit`\n"
    "  只缩小不放大（上限 1）的语义全部维持；`lbFit` 锚点缩放公式未改。\n"
    "- 测试影响：test_viewer.js 把 `#lbStage` 的 getBoundingClientRect 桩成固定 800x500，\n"
    "  与 `.lb-img` 的 CSS 尺寸无关，故 lbFit 断言（缩放值=0.1667 / 渲染宽≈666 等）不受影响——\n"
    "  实测 19/0 全绿。\n"
    "- 验证链：node check.js SYNTAX_OK+EXTERNAL_REFS=none+SMOKE_DONE；node test_viewer.js 19/0；\n"
    "  sync_client.py IDENTICAL；包内 `_internal/resources/index.html` 含 `width:90vw;height:85vh`\n"
    "  （1 命中）、旧 `96vw/96vh` = 0。\n"
    "- 构建：1.9.34→1.9.35；BUILD_OK →\n"
    "  client/dist/LightAlbum-1.9.35-win64/LightAlbum/LightAlbum.exe。\n"
)

marker = "## 灯箱图片不铺满"
assert marker not in s, "1.9.35 entry already exists"
io.open(P, "w", encoding="utf-8").write(s.rstrip() + "\n" + entry)
print("memory 1.9.35 appended; marker_count=%d" % (s.count(marker) + 1))

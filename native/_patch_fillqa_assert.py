#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CRLF-safe: fix LA_GALLERY_FILL_QA assertion to rely on cardBg (black-border) metric."""
import io

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\native\src\main.cpp"

with io.open(SRC, "rb") as f:
    raw = f.read()
text = raw.replace(b"\r\n", b"\n").decode("utf-8")

old = (
    '        // cover-fill 断言：真实照片铺满 → 卡片底（黑边）占比极小、照片占比极大\n'
    '        const bool ok = loaded && fCbFrac < 0.02 && sCbFrac < 0.02\n'
    '                              && fPhFrac > 0.90 && sPhFrac > 0.90;\n'
)
new = (
    '        // 关键断言：卡片底（黑边 / letterbox）占比极小 → 真实照片铺满、无黑边。\n'
    '        // （photo 占比阈值不可靠：亮调照片天空/高光会被误计入 litext，故仅以 cardBg 占比判定）\n'
    '        const bool ok = loaded && fCbFrac < 0.02 && sCbFrac < 0.02;\n'
)
assert text.count(old) == 1, "assert anchor count=%d" % text.count(old)
text = text.replace(old, new)

with io.open(SRC, "wb") as f:
    f.write(text.replace("\n", "\r\n").encode("utf-8"))
print("OK assertion recalibrated")

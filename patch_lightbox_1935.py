# -*- coding: utf-8 -*-
"""1.9.35 灯箱图片不再铺满：.lb-img 由 96vw×96vh 收为 90vw×85vh，
留出明显边距（尤其竖向），点击图库图片弹出的大图稍微小一点。
安全范式：读全量 -> 断言 count==1 -> 替换 -> 写回。
"""
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

old = "  .lb-img{width:96vw;height:96vh}"
new = "  .lb-img{width:90vw;height:85vh}"
c = data.count(old)
if c != 1:
    print("FAIL count=%d (expected 1)" % c)
    sys.exit(1)
data = data.replace(old, new, 1)
print("OK replace .lb-img size")

# 复核旧值消失、新值就位
assert data.count("width:96vw;height:96vh") == 0
assert data.count("width:90vw;height:85vh") == 1
print("OK verify new/old")

with io.open(PATH, "w", encoding="utf-8") as f:
    f.write(data)
print("WROTE %d bytes" % len(data))

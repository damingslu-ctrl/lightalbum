# -*- coding: utf-8 -*-
"""1.9.27 灯箱去黑边（纯 CSS，不动 lbFit / JS）。

根因：灯箱「大黑边」由三层叠加造成——
  1) .lb padding:24px           → 四周留白
  2) .lb-box var(--card) 底 + 圆角 + overflow:hidden + width:100%/max-width
                             → 居中卡片，图片两侧/上下是卡片底（深色）
  3) .lb-img background:#0d0c09 → 图片 contain 缩放后四周填死黑

修复：
  - .lb padding 24 → 0
  - .lb-box 去卡片底/圆角/overflow/width/max-width/max-height → 透明满视口
  - .lb-img 去 #0d0c09 底、定 96vw×96vh 确定尺寸（lbFit 仍以 .lb-stage
    inset:0 实测该尺寸，照常量）；去圆角
  - 移动端 .lb-img 清 min-height / 圆角
约束：.lb-stage 仍 position:absolute;inset:0，尺寸由 .lb-img 决定，
      lbFit() 必须继续拿到确定尺寸 —— 故 .lb-img 显式 width/height。
"""
import io

ROOT = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"

with io.open(ROOT, "r", encoding="utf-8") as f:
    html = f.read()


def rep(old, new, label):
    c = html.count(old)
    assert c == 1, "%s 锚点应唯一，实际 %d 次" % (label, c)
    html.replace(old, new)  # noqa  (rep 用下方原地赋值，保留显式计数断言)
    return c


# 由于 html 是不可变字符串，改为收集替换后字符串
new_html = html

# 1) .lb padding 24→0
old1 = (".lb{position:fixed;inset:0;z-index:100;background:rgba(18,16,12,.86);"
        "display:none;align-items:center;justify-content:center;padding:24px}")
new1 = (".lb{position:fixed;inset:0;z-index:100;background:rgba(18,16,12,.86);"
        "display:none;align-items:center;justify-content:center;padding:0}")
assert new_html.count(old1) == 1, "lb padding 锚点应唯一"
new_html = new_html.replace(old1, new1)

# 2) .lb-box 透明满视口（去卡片底/圆角/overflow/width/max-width/max-height/flex-wrap）
old2 = (".lb-box{position:relative;background:var(--card);border-radius:18px;"
        "max-width:min(1280px,94vw);width:100%;max-height:94vh;overflow:hidden;"
        "display:flex;flex-wrap:wrap}")
new2 = (".lb-box{position:relative;display:flex;align-items:center;justify-content:center}")
assert new_html.count(old2) == 1, "lb-box 锚点应唯一"
new_html = new_html.replace(old2, new2)

# 3) .lb-img 去 flex:1/min 与 18px 圆角，定 96vw×96vh（与下方 position:relative 规则合并）
old3 = ".lb-img{flex:1;min-width:280px;min-height:300px;border-radius:18px}"
new3 = ".lb-img{width:96vw;height:96vh}"
assert new_html.count(old3) == 1, "lb-img(flex) 锚点应唯一"
new_html = new_html.replace(old3, new3)

# 4) .lb-img 去 #0d0c09 底、去圆角（保留 position/overflow/flex 居中）
old4 = (".lb-img{position:relative;overflow:hidden;display:flex;"
        "align-items:center;justify-content:center;background:#0d0c09}")
new4 = (".lb-img{position:relative;overflow:hidden;display:flex;"
        "align-items:center;justify-content:center;border-radius:0}")
assert new_html.count(old4) == 1, "lb-img(bg) 锚点应唯一"
new_html = new_html.replace(old4, new4)

# 5) 移动端 .lb-img 清 min-height / 圆角
old5 = ".lb-img{min-height:46vh;border-radius:18px 18px 0 0}"
new5 = ".lb-img{min-height:0;border-radius:0}"
assert new_html.count(old5) == 1, "lb-img 移动端锚点应唯一"
new_html = new_html.replace(old5, new5)

# 标记
MARK = "/* 1.9.27 灯箱去黑边：.lb padding0 / .lb-box 透明满视口 / .lb-img 96vw×96vh 无底 */"
assert new_html.count("1.9.27") == 0, "1.9.27 标记应尚未存在"
# 把标记插在灯箱 CSS 注释后（line 496 注释后）
anchor = "  /* ---------- 灯箱 ---------- */"
assert new_html.count(anchor) == 1, "灯箱注释锚点应唯一"
new_html = new_html.replace(anchor, anchor + "\n  " + MARK)

with io.open(ROOT, "w", encoding="utf-8") as f:
    f.write(new_html)

print("1.9.27 灯箱去黑边：5 处 CSS 替换 + 标记 写入完成")

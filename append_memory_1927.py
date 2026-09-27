# -*- coding: utf-8 -*-
import io

p = r'D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-26.md'
s = io.open(p, encoding='utf-8').read()

entry = u'''
## 灯箱去黑边【1.9.27，已交付】
- **用户需求**：图库点开大图「不要那么大的黑边，最好不要了」。
- **根因（三层叠加）**：① `.lb{padding:24px}` 四周留白；② `.lb-box` 带 `var(--card)` 卡片底 +
  圆角 + `overflow:hidden` + `width:100%`/`max-width`，图片居中后两侧/上下是卡片底；
  ③ `.lb-img{background:#0d0c09}` contain 缩放后四周填死黑。
- **修复（ui_upgrade_1927.py，5 处 rep + 标记，双页 diff=0）**：
  1) `.lb` `padding:24px`→`0`；
  2) `.lb-box` 去卡片底/圆角/overflow/width/max-width/max-height/flex-wrap →
     `position:relative;display:flex;align-items:center;justify-content:center`（透明满视口）；
  3) `.lb-img` 去 `flex:1/min/18px 圆角` → 定 `width:96vw;height:96vh`（确定尺寸供 lbFit 照常量）；
  4) `.lb-img` 去 `background:#0d0c09`、圆角→0（字母边只透出 .lb 的半透明暗 backdrop，不再是死黑实块）；
  5) 移动端 `.lb-img` `min-height:46vh`/圆角 → `min-height:0;border-radius:0`。
- **约束红线守稳**：`.lb-stage{position:absolute;inset:0}` 尺寸由 .lb-img 决定，lbFit() 仍实测该
  尺寸 → 必须保留 .lb-img 显式 width/height（不可改回 auto 否则 lbFit 崩）；
  `.lb-close/.lb-nav/.lb-count` 是 `.lb` 直接子节点、`.lb-tools/.lb-loading` 在 `.lb-img`(position:relative)
  内绝对定位 → 本次改动不影响其定位；`.drawer/.pvl/.pm` transform 一字未动；零外链仍成立。
- **验证**：sync_client.py `IDENTICAL root==client/resources`（diff 0）；`check.js`
  EXTERNAL_REFS=none + SMOKE_DONE；`test_viewer.js` **19/0**（关键 `[1] 大图 lbFit` PASS：
  scale=0.1667 / 居中 tx=ty=0 / 渲染宽度≈666 未被双重缩小，证明新规下 lbFit 仍正确）；
  包内 `_internal/resources/index.html` 含 1.9.27 标记 + `.lb-img{width:96vw;height:96vh}` +
  透明 `.lb-box`（各命中 1）。版本 1.9.26→1.9.27；BUILD_OK →
  `client/dist/LightAlbum-1.9.27-win64/LightAlbum/LightAlbum.exe`。
'''

assert u'## Hero 视差/光斑 + 抽屉光边【1.9.26' in s, '1.9.26 anchor missing'
s = s.rstrip() + u'\n' + entry
io.open(p, 'w', encoding='utf-8').write(s)
print('1.9.27 memory entry appended')

# -*- coding: utf-8 -*-
"""把 1.9.30（黑白/单色增强路由）写入今日日志 2026-09-27.md。"""
import io, os

D = r'D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory'
P = os.path.join(D, '2026-09-27.md')

ENTRY = u'''
## 黑白 / 单色照片增强路由【1.9.30，已交付】（1.9.25 遗留未决项）
- **真问题（此前理解偏了）**：`ROUTE_TABLE` 里 `'黑白'` 这个**分类**本来就 → landscape。
  真正的坑是**被分类成风光/人像、但像素其实是灰度**的照片：走 landscape 时彩色
  3DLUT 会给纯灰度输入（R=G=B）染上颜色；走 portrait 则被 GFPGAN 上色器染色。
- **决策**：黑白照片「**也走** landscape 增强」，但**结果必须保持黑白** ——
  享受 AI 的色调/对比规划，绝不引入颜色。
- **enhance.py 新增（纯逻辑、可单测）**：
  - `is_monochrome(img, threshold=0.03)`：逐像素 (max-min) 通道差均值 < 0.03
    （≈7.6/255）判单色；单通道/灰度图直接 True；0-255 输入自动归一化。
  - `monochrome_route(cat, mono)`：单色且原路由为 portrait 时改走 landscape，其余沿用
    ROUTE_TABLE。
- **server.py `/api/enhance` 三段改造**（`enhance_server :8768` 是**外部**服务、不在包内，
  所以只能在本服务侧处理）：
  1. 单色检测：`Image.open(fp).convert('RGB')` → numpy → `E.is_monochrome()`；
     **任何异常一律 `mono=False`**（RAW 等解不开的仍走原逻辑，绝不因检测失败中断增强）。
  2. 路由改写：单色 + portrait → landscape（避免上色器染色）。
  3. 灰度回写：增强成功后 `out.rsplit('.',1)[0]+'.mono.png'`，
     `Image.open(out).convert('L').convert('RGB')` 存盘，`srv['output']` 与 `preview_url`
     均指向灰度版；响应新增 `mono` 字段。
- **index.html**：增强完成提示加「，已保持黑白」（读 `d.mono`）。
- **测试**：
  - `test_enhance.py` 新增 [8] 单色检测/路由 8 条 → 全量 **32/0**（原 24）。
  - 新增 `test_enhance_mono.py`：起真实 LibraryServer(:8797)，**只劫持转发到 :8768 的那次
    urlopen**（按 URL 含端口判断）→ 单色+人像 → route=landscape 且输出 chroma=0.0000
    （真灰度）；彩色+人像 → route=portrait 且不生成 .mono.png；单色+风光 → landscape+灰度。
  - `test_cull_route.py` 的 enhance 步骤改为 `:8768` 离线时打印 `ENHANCE_SKIP` 而非硬失败
    （环境依赖；逻辑已由 test_enhance_mono.py 覆盖）。
- **坑（本轮真实踩到）**：
  1. 若把 `urllib.request.urlopen` 全局替换成假响应，**测试客户端自己的 POST 也会被短路**，
     根本没打到真实 server → 必须按 URL 里的端口区分，只劫持 :8768。
  2. 托管 python 无 numpy → 测试要用 `client/venv/Scripts/python.exe`。
  3. 补丁脚本里 `"""..."""` 嵌套 `"""docstring"""` 会 SyntaxError → 外层改用 `r\'\'\'...\'\'\'`。
- **附带清理**：`.workbuddy/memory/2026-09-26.md` 被一个后台任务重复追加了一份**已损坏**
  的 1.9.27 条目（shell 反引号把 CSS 类名吃掉了），已用 `dedupe_memory.py` 删除。
- **验证链全绿**：py_compile OK；test_enhance **32/0**；test_enhance_mono `SMOKE_MONO_OK`；
  test_cull_route `SMOKE_CULL_OK`；sync_client `IDENTICAL`；check.js `SYNTAX_OK` +
  `EXTERNAL_REFS=none` + `SMOKE_DONE`；test_viewer.js **19/0**。
- **构建**：1.9.29→1.9.30；BUILD_OK →
  `client/dist/LightAlbum-1.9.30-win64/LightAlbum/LightAlbum.exe`（workpath w0927072514）。
  权威核验：包内 `_internal/resources/index.html` 含「已保持黑白」+ `d.mono`（各 1 命中）；
  Analysis-00.toc 含 `lightalbum.core.enhance` + `lightalbum.core.server`，numpy 已进包
  （单色检测依赖 numpy+PIL，二者都在，不会静默降级）。
'''

if not os.path.isdir(D):
    os.makedirs(D)
with io.open(P, 'a', encoding='utf-8') as f:
    f.write(ENTRY)
print('APPENDED', P)

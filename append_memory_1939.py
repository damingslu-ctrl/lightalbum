# -*- coding: utf-8 -*-
import io

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\.workbuddy\memory\2026-09-27.md"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

entry = r"""

## OCR review + 全量 C++/Qt Widgets 重写决策 + P0 里程碑【1.9.39，已交付】
- 触发：用户「由头到尾全部重新整理，选择其他语言编译，允许不是 web 的」+ 附 open-code-review 技能。
- OCR review（delegate 模式：项目无 git 历史、无 LLM 端点；ocr review 依赖 git diff 无法直跑）：
  - 0 critical / 0 high / 2 medium / 3 low。
  - M1 签名过弱：renderMapGuarded/renderStatsGuarded/#rlPick 均用数量类签名，同数量原地修正
    （重扫/EXIF 回填/分类/评分）检测不到。
  - M2 aiDecide 死载荷：页面发送 aiDecide:true，原生 act_aiEdit(app.py:745) 不消费——
    行为正确（「AI 全权」由 preset='auto' 表达，原生默认值即 auto），但字段语义需锚定。
- 1.9.39 修复（patch_review_fix_1939.py，4 处 rep/断言全绿）：
  photosFp() O(n) 轻量指纹（id/地区/GPS/分类/评分/标题）纳入三处签名；aiDecide 两端注释锚定。
  验证链全绿：check.js SYNTAX_OK+EXTERNAL_REFS=none+SMOKE_DONE；test_viewer 19/0；
  sync_client IDENTICAL；py_compile app.py OK；BUILD_OK 1.9.39；包内核验 photosFp/rlPick 指纹/锚定各 1。
- **架构决策 ADR-0001（用户选定路线 B）：全量 C++/Qt Widgets 重写，含 Lumen 引擎移植，禁止 Web 运行时；
  web 版自 1.9.39 起停止维护，index.html 冻结为金样参考。**
  - 备选对比（A: QML 表现层 / B: 全量 C++ / C: Rust）与理由见 docs/adr/ADR-0001。
  - 分期计划 P0-P6 + index.html→C++ 模块映射 + 工程纪律（单向渲染/数据诚实/金样比对）
    见 docs/REWRITE-PLAN.md。编辑台(fabric 交互)是最大单项，置 P4；Lumen 移植 P5 金样比对。
- P0 已产出：git 仓库初始化（.gitignore 排除 venv/build/dist/.workbuddy）+ ADR + REWRITE-PLAN +
  native/ 骨架（CMakeLists + src/main.cpp，Qt6 Widgets，1440x940 基准）。
- 工具链（下一步）：Qt 6.8.x win64_mingw 经 aqt 装到 D:\qt（遵守下载不进 C 盘）；
  用 Qt 官方 MinGW 13.1，不用 winlibs GCC16.2 混 ABI；命令清单在 REWRITE-PLAN 工具链节。
- 踩坑（本轮真实）：
  1. git add -A 撞上 PyInstaller 并发构建磁盘高峰 → 10 分钟未完成；中断后遗留 .git/index.lock；
     TaskUpdate 对普通后台 shell「not found」但进程仍活（git.exe 59MB 在跑，锁是它持有的，勿强删）。
  2. 本机 rm/Remove-Item 走 genie-trash 安全删除且对 .git 内文件 fail-closed；mv 重命名也因
     文件被占用失败 → 正确做法：等持有锁的 git 进程自然完成（链条含 commit，会自动提交）。
"""

with io.open(PATH, "a", encoding="utf-8") as f:
    f.write(entry)
print("APPENDED 1.9.39 entry, total bytes=%d" % (len(data) + len(entry)))

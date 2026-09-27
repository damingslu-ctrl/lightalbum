# ADR-0001：LightAlbum 全面重写为 C++/Qt Widgets（放弃 Web 表现层）

- 状态：Accepted（2026-09-27）
- 决策人：用户（Software Architect）
- 关联：ADR-0002 工具链（待写）、docs/REWRITE-PLAN.md 分期计划

## 背景（Context）

LightAlbum 现状为「PySide6 外壳 + QWebEngine(Chromium) 渲染单文件 index.html（~964KB，
单 `<script>` ~520KB）」，页面与原生经 HTTP 127.0.0.1:8787 + `__native.call` 桥接。
1.9.32–1.9.38 连续做性能专项（hub 后台线程、去冗余渲染、地图视口门控 + 签名守卫）
后，仍存在结构性开销：

1. Chromium 运行时常驻 ~300–400MB 内存，冷启动占大头；
2. 页面↔原生只能走 HTTP + JSON 桥（端口探测/重连/令牌一整套桥接债）；
3. DOM 重排模型对「数千缩略图 + 巨型 SVG 地图 + Canvas 编辑台」天然吃亏。

用户决策（2026-09-27）：**全量 C++/Qt Widgets 重写，含 Lumen 修图引擎移植**，
接受更长周期与更高风险换取最大控制权；明确禁止 Web/Electron 运行时
（与 AL38 Command Deck 同一决策模式）。

## 决策（Decision）

- 表现层：Qt Widgets（非 QML）+ QGraphicsView/自定义 paint，C++17，CMake 构建。
- 引擎层：Lumen（现纯 Pillow）移植为 C++（stb_image / libjpeg-turbo + 自研算子），
  算法行为以 1.9.39 的 `_audit` 自检/降级语义为基准做**逐算子等价回归**。
- 数据层：SQLite（Qt SQL 或 sqlite3 直连），复用现有 `library.db` schema 与 vault 设计。
- 零 Web 运行时：不引入 QtWebEngine；web 版（独立浏览器打开 index.html）自本 ADR 起停止维护。

## 备选方案对比（Trade-offs）

| 方案 | 工作量 | 性能/控制权 | 主要风险 |
|---|---|---|---|
| **B. 全量 C++/Qt Widgets（选定）** | 极大 | 最高 | 周期长；Lumen 移植画质回归；MinGW/MSVC 工具链 |
| A. QML 重写表现层，Python 核心保留 | 中 | 高 | 编辑台 Canvas 重做；仍是解释层桥接 |
| C. Rust（egui/Slint） | 大 | 高 | 地图/Canvas 生态最弱，人才与维护成本 |

选 B 的理由：用户已有 AL38 同型成功经验（Electron→Win32 C++/MinGW），对原生栈的
维护成本有真实预期；且要求「极大优化」不接受折中。

## 后果（Consequences）

正：
- 去 Chromium：内存 −300MB 级、冷启动大幅缩短、GPU 光栅（QPainter/GRView）；
- 删除 HTTP 桥整层（端口探测/重连/令牌/`__native.call` 协议）；
- 单语言交付，exe 可静态发布。

负/风险（必须缓解）：
- **Lumen 画质回归**：按 ITERATION_GUIDE 的算子清单逐个移植 + 用 1.9.39 输出做金样比对；
- **fabric.js 编辑台**（画笔蒙版/裁剪/拉直/对比/AI消除桥）是最大单项，置于 P4 最后；
- **SVG 世界地图**（233 省国界 + 漂移 + 三级下钻）：GeoJSON→QPainterPath 多边形，
  漂移用定时器平移重绘，预计比 SVG DOM 更快但需重做命中测试；
- web 版即时起停止维护；index.html 冻结在 1.9.39 作为金样参考。

## 量化验收基线（P0 产出后回填）

- 冷启动：待测（1.9.39 QtWebEngine 版 ≈ 2.5s 温跑 page_loaded≈978ms）
- 稳态内存：待测
- 图库滚动帧率：待测（6405 张库）

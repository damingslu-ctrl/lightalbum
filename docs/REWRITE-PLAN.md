# LightAlbum C++/Qt Widgets 重写计划（路线 B）

决策：ADR-0001。基线：git tag `web-final-1.9.39`（index.html 冻结为金样参考）。

## 分期（每期独立验收，可回滚）

| 期 | 内容 | 验收标准 |
|---|---|---|
| P0（本轮） | git 基线 / 1.9.39 review 修复 / ADR+计划 / native 骨架 / 工具链 | 基线可 diff；骨架可 configure（装 Qt 后） |
| P1 外壳 | 主窗口（左侧图标栏+顶栏+KPI条）、SQLite 直读 `library.db`、图库网格（QListWidget::IconMode 窗口化/自绘 model）、胶片条、灯箱（缩放/锚点缩放/适屏） | 打开真库 6405 张；滚动稳定；内存对比表 |
| P2 足迹+日志 | GeoJSON→QPainterPath 世界地图（233省+国界+漂移+三级下钻+命中测试）、6 张统计图（自绘或 KDChart/KddChart） | 与 web 版逐项功能对照通过 |
| P3 工作台 | 修图实验室/智能选片/AI美图 三个表单区（预设下拉/建议芯片/进度条/徽章） | 全部按钮跳转与 1.9.36 接线语义一致 |
| P4 编辑台 | 画笔蒙版/裁剪/旋转/拉直/对比/AI消除桥（QGraphicsScene 重写 fabric 部分） | 与 web 版交互等价清单逐条过 |
| P5 引擎移植 | Lumen → C++：白平衡/色阶/曲线/clarity/vibrance/降噪/USM/去雾/畸变/红眼/HDR/风格……+ `_audit` 自检降级语义 | 金样比对（1.9.39 输出 vs C++ 输出，SSIM/直方图阈值） |
| P6 打包 | 静态/动态 Qt 部署，删除 QtWebEngine 依赖 | 包体积、启动、内存三项对比报告 |

## 原生实装进度（native/）

- P0 工具链与骨架：✅ 完成（Qt6.8.3 + MinGW13.1，首版 exe 101KB，offscreen 冒烟）
- P1 外壳：✅ 完成（主窗口/SQLite 直读/图库网格/胶片条/灯箱）
- P2 足迹+日志：✅ 完成（QPainterPath 世界地图三级下钻 + 统计图，mapdata 零外链编进 exe）
- P3 智能选片（culling）：✅ 完成（CullEngine 移植 MD5/pHash 去重 + 模糊/曝光 + RAW 豁免；
  CullPage 结果网格 + 7 项 KPI；`LA_CULL_SMOKE` 通过：`total=40 cand=25 rej=15 dup=2 blur=1 exp=11 unk=0 top=25`）
- P3 修图实验室（retouch）：✅ 完成（功能型修图引擎 RetouchEngine：曝光/亮度/对比/清晰/自然饱和/饱和度/
  白平衡/USM锐化/高光-阴影/暗角 纯像素运算；16 预设 + recommend() 启发式建议芯片；RetouchPage
  before/after 预览 + 预设下拉 + 手动滑杆 + 导出副本，零云端；`LA_RETOUCH_SMOKE` 通过：
  `ok=1 srcW=512 autoOutW=512 sugChips=3 sugOutNull=0`；独立单测 ENGINE_TEST PASS 验证
  bw 全灰(通道差0)/warm R相对B升温/original 恒等）
- P3 AI美图（美图）：✅ 完成（BeautyPage 风格化预设画廊：复用 RetouchEngine 16 预设生成卡片缩略图 +
  点选套用 + 大图 before/after 预览 + 智能增强 + 导出副本，零云端；`LA_BEAUTY_SMOKE` 通过：
  `ok=1 presets=16 thumbs=16 nullOut=0`；深度 AI 处理留待模型接入）
- P4 编辑台（第一刀：几何编辑）：✅ 完成（并入 RetouchPage：旋转90°⟲⟳/水平垂直翻转/拉直±10°滑杆/
  按比例居中裁剪(自由/1:1/4:3/3:2/16:9)/重置几何；`applyGeometry` 纯函数固定流水线
  拉直→翻转→旋转→裁剪，导出与预览同序；`LA_RETOUCH_SMOKE` 几何断言通过：
  `rotSwap=1 sqOk=1 flipOk=1`。画笔蒙版/AI消除桥（QGraphicsScene 重写 fabric）留后续刀）
- P4 编辑台（第二刀：画笔蒙版）：✅ 完成（MaskCanvas：QGraphicsScene 底图+翡翠叠加双层，
  笔刷写入 ARGB32 蒙版 alpha，1:1 坐标映射；blendMask 纯函数 out=base*(1-f)+toned*f；
  RetouchPage 预览/画布 QStackedLayout 切换 + 笔刷大小/清除/局部应用；局部调整只在涂抹区生效、
  区外保原图；导出与预览同序合成；`LA_RETOUCH_SMOKE maskBlend=1`（蒙版内变/外不变断言）。
  AI消除桥留后续刀）
- P4 编辑台（第三刀：交互裁剪 + 对比视图）：✅ 完成（CropCanvas：四角手柄缩放/框内拖移/空白新建 +
  暗色遮罩外区 + 金边 + 三分线 + aspect 锁定，提交为归一化 cropNorm 进入几何流水线
  （顺序 拉直→翻转→旋转→交互裁剪→比例居中裁剪，导出同序）；CompareWidget：单幅区域金色分割线
  拖拽对比，左原图右修后；RetouchPage 三模式互斥（🖌蒙版/✂裁剪/◐对比）；
  `LA_RETOUCH_SMOKE cropNormOk=1`（归一化半幅裁剪断言）。P4 仅剩 AI消除桥（模型接入））

## index.html → C++ 模块映射（P1 起逐项迁移）

| 现有（index.html 1.9.39） | 目标（native/） |
|---|---|
| `refreshAll()` 单向渲染 DAG | `AppState` + 显式 `refresh(Section)` 派发（保留单向铁律） |
| 图库 `renderGallery`+sig 守卫+loadMore | `GalleryView`（QAbstractListModel 窗口化，签名守卫语义保留） |
| 足迹 `buildWorld/renderMap/renderPhotoMarkers` | `map/WorldMapWidget`（QPainterPath + QTimer 漂移） |
| 灯箱 `lbFit/锚点缩放` | `LightboxDialog`（QGraphicsView, AnchorView 保留 0.2~上限1 语义） |
| 胶片条/检视面板 | `FilmstripBar` / `InspectPanel` |
| 修图实验室 `setupRetouchLab` | `retouch/RetouchLabPanel`（rlAi=AI全权/rlRun=手动预设 双路径语义保留） |
| 选片/美图 | `culling/CullingPanel` / `enhance/EnhancePanel` |
| `__native.call` HTTP 桥 | 直接函数调用 `Core::instance()`（删整层桥接） |
| `core/retouch.py`（Lumen） | `engine/lumen/*`（P5 逐算子移植+金样） |
| `library.db` / vault | 直连 sqlite3；vault 的 PBKDF2+Fernet 换 libsodium 等价实现 |

## 工具链（ADR-0002 素材，下一步执行）

- 编译器：Qt 官方 MinGW 13.1（随 aqt 安装，避免 winlibs GCC 16.2 与 Qt 预编译库的 ABI 边界）；
  AL38 的 winlibs 工具链继续用于 AL38，不混用。
- Qt：6.8.x LTS，**只装 qtbase/qttools**（Widgets 所需最小集），装到 `D:\qt`（遵守：下载不进 C 盘）：

```bat
pip install aqtinstall
aqt list-qt windows desktop --arch 6.8.3      :: 实测存在 win64_mingw ✓
aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O D:\qt   :: MinGW 13.1.0（实测名）
aqt install-qt windows desktop 6.8.3 win64_mingw -O D:\qt
```

- 构建：`cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:\qt\6.8.3\mingw_64 ..`
- aqt 环境已隔离装好：`C:\Users\damin\.workbuddy\binaries\python\envs\aqt\Scripts\aqt.exe`；
  下载临时目录钉 `D:\tmp`（遵守下载不进 C 盘）。

## P1 数据层事实（2026-09-27 只读探查 D:\lightalbum\library.db 实测）

- `photos` 6424 行：`path`(PK)/root/rel/src/size/mtime/thumb/`exif_json`(全部 EXIF 打包为 JSON)/
  missing/seen_at/category/cat_score/cat_at/rating/person。
  → C++ 侧 `QJsonDocument` 解 exif_json 即可，无需第三方 JSON 库。
- `meta` 键值表（last_scan 等）；`vault`（id/path/name/enc/thumb/size/mtime/created_at）。
- SQLite 接入选型：默认 **sqlite3 amalgamation 静态编入**（单文件零依赖，exe 自包含）；
  备选 Qt6::Sql 的 qsqlite 插件（win64_mingw 默认含）。P1 动工时定夺。
- 数据诚实铁律平移：exif_json 字段缺失 = null/0/'' → UI 显示 `—`，统计跳过，严禁编造默认值。

## 工程纪律（沿用本项目铁律的 C++ 版）

1. 渲染单向：`refresh(Section)` 派发，禁止渲染函数互调（对应 web 版 G 节铁律）；
2. 数据诚实：EXIF/GPS 缺失 = null/0/空串 + UI 显示 `—`（L 节铁律平移）；
3. 每期结束跑「功能保留性回归清单」并出对比报告；OCR review 走真 git diff（基线已建）；
4. 颜色/像素级校验必须实测截图，严禁猜测。

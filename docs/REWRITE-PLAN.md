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

## 工程纪律（沿用本项目铁律的 C++ 版）

1. 渲染单向：`refresh(Section)` 派发，禁止渲染函数互调（对应 web 版 G 节铁律）；
2. 数据诚实：EXIF/GPS 缺失 = null/0/空串 + UI 显示 `—`（L 节铁律平移）；
3. 每期结束跑「功能保留性回归清单」并出对比报告；OCR review 走真 git diff（基线已建）；
4. 颜色/像素级校验必须实测截图，严禁猜测。

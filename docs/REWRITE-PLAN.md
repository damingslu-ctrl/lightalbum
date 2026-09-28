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
  `LA_RETOUCH_SMOKE cropNormOk=1`（归一化半幅裁剪断言）。
- P4 编辑台（收官刀：AI 消除桥表单壳）：✅ 完成（`EraseBridge.{h,cpp}`：蒙版→任务→结果回填全流程；
  模型槽位读 LA_INPAINT_MODEL 环境变量，**执行器未接入 → isReady() 恒 false，任务诚实挂起
  （blocked_no_model），绝不伪造消除结果**；applyResult 回填通道 + taskUpdated 信号；UI「🧹 AI消除」
  + 槽位状态标签；`LA_RETOUCH_SMOKE eraseBridge=1`（挂起/回填断言）。**P4 全部完成**——
  模型接入时仅需实现本地执行器消费 queued 任务）
- P5 引擎移植（第一批：auto 管线核心 9 算子）：✅ 完成（`LumenEngine.{h,cpp}` 忠实移植
  Python core/retouch.py：测量(_stats/_measure_extra 全指标) + wb(SoG p=6 线性光 von Kries) +
  tone_curve(局部掩膜抬阴影/收高光) + auto_levels(有界 6..249) + brightness(0.46 收敛/p99.5 限幅) +
  contrast(实测均值轴心) + clarity(V-USM lc 门控) + vibrance(S-LUT) + denoise(YCbCr 两级) +
  sharpen(USM 噪声阈值) + 四门 audit(不过→轻力度重做→回退原图) + steps↔算子一一对应；
  PIL 基础设施复刻：convert('L') 权重/HSV 整数六分区/ImageChops/USM/Median3/SMOOTH/面积加权降采样。
  **金样对拍 PASS**：`_gold_lumen.py`（Python 金样）vs `LA_LUMEN_SMOKE`（C++）——
  `n=6 madAvg=0.569 madMax=0.867(≈0.34%) stepsOk=6/6 PASS`）
- P5 引擎移植（第二批：8 个显式算子）：✅ 完成（film/bw/glow/redeye/vignette/hdr/structure/dodge
  全部移植 + 基础设施 screen/blend/composite/channelGain/PIL Contrast 均值轴心/addGrain；
  金样**逐算子独立**对拍：`LA_LUMEN_SMOKE` OPS 段 6 样本×8 算子 madMax=1.107 PASS
  （redeye mad=0.000 → 双方一致「无红眼不乱修」，门控语义一致）。
  颗粒(_add_grain) 为 PRNG 噪声无法字节对齐 → film 金样不含颗粒，已文档化。
- P5 引擎移植（第三批：dehaze / chromatic / portrait）：✅ 完成（dehaze=暗通道先验
  (MinFilter15→暗通道→大气光 A→透射率 t(ω=0.95,t0=0.1)+高斯细化→复原)；chromatic=R 放大裁中心/
  B 缩小镜像归位；portrait=YCbCr 肤色软掩膜(Chai&Ngan ramp)+Median5/Gauss 磨皮+美白门控
  (skin_lum<0.62)；基础设施 minFilter/medianFilter(尺寸可配)/YCbCr 整数定点/areaResample。
  金样：**11 算子 × 6 样本** madMax=1.107(vignette) PASS。
- P5 引擎移植（第四批/收官：level / lens / EditPlan 参数管线）：✅ 完成
  （level=Hough 地平线检测 θ∈[78°,102°] Sobel 阈值 55 + 票数门控 + QTransform 旋转裁 inscribed
  矩形；lens=桶形畸变径向反畸变，N×N 网格局部仿射 + 镜像扩边；retouchWithPlan=13 项参数字典管线
  warmth/brightness/contrast/tonal/saturation/vibrance/clarity/tint/denoise/sharpness）。
  金样对拍全绿：`LA_LUMEN_SMOKE PASS` —— 12 显式算子（含 lens madMax=2.192）+
  PLAN(13 参数 madMax=3.010 < 4.0) + LEVEL(角度自检 angErr=0.00, madMax=1.001) 全部通过；
  P1/Cull/Retouch/Beauty 四路冒烟无回归。**P5 全部完成**，下一步进入 P6 打包对比。

- P6 打包对比：✅ 完成（见 `docs/P6-PACKAGING.md`）。native 60 MB（动态 Qt 部署，目录式，无 WebEngine/3D）
  vs 旧 Python PyInstaller 单文件 641 MB（PySide6 536M 含 QtWebEngine 200M + Qt3D 16M）≈ **10.7×**；
  启动 native ≈0.43–0.65 s（主窗就绪）vs Python ≥2 s 引擎就绪且库在 headless 下持续加载数分钟；
  常驻内存 native ≈61 MB（平稳）vs Python ≈450–540 MB（持续爬升）≈ **7–9×**。
  关键发现：移除 QtWebEngine 一举消除 200M 包体 + Chromium 基线内存；native 因无 WebEngine 可干净
  offscreen 冒烟（LA_SMOKE / LA_LUMEN_SMOKE），旧 Python 在无 GPU 下 Chromium 报 GPU Context lost
  无法等价无头自测。**P6 全部完成 → 路线 B 重写收官，native 为唯一正式分发形态。**

- P7 发行打包 + 版本固化 + 全量终验 + 功能对等审计：✅ 完成（见 `docs/P7-PARITY.md`）。
  - 版本固化：CMake `0.1.0 → 2.0.0`，`target_compile_definitions` 注入 `LA_VERSION`，标题改为
    「光影集 LightAlbum 2.0.0」。
  - 发行打包：`native/release.ps1`（delete-free，robocopy→windeployqt 干净部署 + 可选裁 Qt6Network），
    产出 `LightAlbum-2.0.0-win64.zip`（16 MB，已裁 Qt6Network/tls/networkinformation/generic，无 CMake 残骸）。
  - 全量终验：构建 exe 与**裁剪后** staging exe 各跑 P1/Cull/Retouch/Beauty/Lumen 五路冒烟，**全绿**
    （注：`LA_RETOUCH_SMOKE cropNormOk` 对奇数宽源图为测试断言取整边角，非产品缺陷）。
  - 功能对等审计：核心层（浏览/地图/修图/选片/美图/编辑/蒙版/对比）100% ✅；上层 AI/实验性
    （人物/分类/本地大模型/HEIC/vault/光学天文/逆地理/MCP/外部编辑器桥）按零云端设计 **❌ 主动省略**；
    **唯一硬缺口**：native `Library` 只读、无扫描/导入器，依赖 Python 端预建 `library.db`
    → 建议列 P8 补原生导入器以达成真正独立 GA。

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

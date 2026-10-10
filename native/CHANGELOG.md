# CHANGELOG — LightAlbum native

遵循 SemVer：新功能 = MINOR，修复 = PATCH，破坏性 = MAJOR。
版本号单一真源：`CMakeLists.txt` 的 `project(... VERSION)`（打包脚本从此处解析）。

## [2.35.0] - 未发布

本轮为「引擎升级·双路线」合并发布：路线 1 把全仓唯一残缺引擎 EraseBridge 从诚实桩升级为
**真引擎**（独立子进程 + LaMa ONNX，零云端）；路线 2 把人脸聚类从贪心单遍升级为**两遍精炼**
（质心迭代，API 零变化）。两条路线合并发一个 MINOR 版本。

### 新增 - ★AI 消除真引擎：EraseBridge 桩→独立执行器（路线 1）

- **进程边界**：新增独立 MSVC 项目 `inpaint/`（ONNX Runtime C++ + SAIC-MLP LaMa ONNX 模型，
  约 191MB），不进 MinGW 主构建、主程序零新链接依赖；宿主侧新增 `core/EraseRunner`
  （常驻 QProcess 子进程封装）以帧协议通信：握手 `READY 1`（就绪）/ `READY 0`（模型缺失，任务诚实
  失败且支持稍后 fetch 重试）→ `TASK <id> <w> <h> <nb> <nm>` + 裸 RGB888 底图 + 裸 alpha-mask
  字节流 → 应答 `OK <id> <w> <h> <no>`（+ 裸 RGB888 结果）/ `ERR <id> <原因>`。**三重隔离**：ABI（MSVC CRT 与
  宿主 MinGW 彻底解耦）、崩溃半径（子进程挂掉只影响当次任务，下一任务自动重拉）、许可
  （模型分发独立于主程序）——沿用 dcraw/ffmpeg 的独立进程先例。
- **契约变真**：`modelReady()` 从桩的恒 false 改为「inpaint.exe 与 lama.onnx 均在（stub 除外）
  且 `READY` 握手成功」；任务状态机 `queued → running → done | failed`（新增 `running`）；未取模型时保持
  诚实 `blocked_no_model`（note 注明零云端/未接入，绝不伪报 done）；失败判定 = 进程非零
  退出 / `ERR` / 结果为空或尺寸 ≠ 底图 → 走既有 `failed` 分支。`RetouchPage` UI 本轮
  零改动（早已 100% 接线）。
- **零云端 + 隐私**：inpaint.exe 不发起任何网络请求；模型由用户在构建期主动运行
  `fetch_lama.ps1` 获取（LICENSE 硬校验，无许可即 fetch 失败；SHA 哨兵 `PIN-ON-FIRST-FETCH`
  钉死之前拒绝真实下载，fail-closed）。送进子进程的是编辑态图，继承修图页注入点的
  私密过滤，本层不再二次遍历。
- **门禁第 70 门 `LA_ERASE_QA`（full）**：契约门走确定性 MinGW 桩 `inpaint_stub.exe`
  （常绿、宿主无关）：A 状态机全序 / B 守恒像素判别（蒙版外逐字节 == 底图 + 蒙版内
  平均差 > 阈值，证明确实动了）/ C 常驻复用（连投 3 任务 `processId` 恒等）/ D 崩溃恢复
  （任务间 kill → 当前任务 failed、下一任务自动重 spawn 并 done）。反例
  `LA_ERASE_QA_FORCE_BROKEN=1` 故意错配结果尺寸 → A/B 双转红（期望式一字不改，非空心）。
- **质量第 71 门 `LA_ERASE_MODEL_QA`（full，opt-in）**：仅当 `LA_LAMA_MODEL` 指向真实
  lama.onnx 时才跑真模型质量断言；本机从未持有模型 → `[SKIP]`（诚实缺席，不计回归）。
- **发布许可契约 C9**：staging 内出现 inpaint.exe 而缺 `LICENSE` 即 throw 拒打包；
  `release.ps1 -SelfTest` 注入「删 LICENSE → C9 转红」。默认包**不含模型**——仅当用户
  主动 fetch 后，release 打包才含 4 件 lama 产物（inpaint.exe / onnxruntime.dll /
  lama.onnx / LICENSE）。
- **已知限制**：消除推理当前是 GUI 线程同步调用（`EraseRunner::run()` 内 `waitForReadyRead`
  20s 超时，由 `EraseBridge` 的 `QTimer::singleShot(0)` 在 GUI 线程触发）——真模型下单张任务可能
  让 UI 秒级冻结、`running` 态可能来不及刷新；异步化（后台线程投递 + 进度回报）列为下一迭代。

### 新增 - 人脸聚类两遍精炼（路线 2）

- **算法**：`FaceCluster::cluster()` 从「贪心 leader 单遍、只与每簇首张脸比余弦」
  （顺序敏感、代表信息量最少）升级为**两遍**：预归一化（余弦降为点积，约 2× 提速）→
  贪心种子（阈值 0.4）→ ≤3 轮质心精炼（每轮全量重分配至最近质心、低于阈值新建单例簇、
  零重分配早停）。顺序不变性由精炼收敛获得；簇代表从「首张脸」变为「质心」。
- **调用侧零改动**：API 签名一字不改，读回 embedding → 聚类 → 落库流程、守恒契约
  （`outputCount==inputCount`）、`setForceBrokenForQa` 反例钩子与标签编号全部保持。
- **成本**：约 10s → 约 25–30s（QtConcurrent 后台一次性作业，不阻 GUI，已与用户确认接受）。
- **门禁强化 `LA_FACE_CLUSTER_QA`**（既有 A–H 八组不动，新增 O/C/S/D 四组）：**O** 顺序
  不变性（同一 12 项集合 3 种排列 → 集合意义同划分；贪心单遍必红，真判别器）、**C** 质心
  优于头脸（每簇 `mean cos(→质心) ≥ mean cos(→首张脸)`）、**S** 同人必同簇（5 条近重复
  embedding 随机排 3 次均同簇）、**D** 异人必分簇（5 条正交 embedding 各成一簇）；新增
  反例钩子 `qaSetForceMergeForQa`（`LA_FACE_CLUSTER_QA_FORCE_MERGE=1` 全塌 group0 →
  C/D 双转红）。测试集为确定性合成集，`measuredMs` 已从 `-Timing` 重测回填。

### 回归

- 全量套件（tier=all，**71 门 = fast 18 + full 53**）：`PASS=59 FAIL=0 CHECK=0 KILL=0
  SKIP=12`，墙钟 63661 ms。12 道 SKIP 皆为需要外部环境变量的门（`LA_SCAN_*` 个人库 /
  `LA_RAW_SAMPLE`/`LA_VIDEO_SAMPLE` 素材 / `LA_ERASE_MODEL_QA` 需 `LA_LAMA_MODEL`），非回归。
- `gate_matrix.ps1 -SelfTest` 全绿：名册 71 门（`fast=18 full=53`、`nonAsciiBytes=0`、
  `textual=71==parsed`、`fails=0`）。
- `release.ps1 -SelfTest`：`LA_RELEASE_QA PASS contracts=9 injected=15 caught=15 baseline=9
  zip=9`。
- 版本 2.34.0 → **2.35.0**（新功能 = MINOR，单一真源 `CMakeLists.txt`）。

## [2.32.0] - 未发布

### 新增 - 自动故事相册（人物故事方向·B）

把「同一趟旅程 / 同一地点同一时段」的照片自动聚合成**故事**，图库页顶栏新增
「故事」筛选条，一键只看某个故事的照片（纯视图态聚合，不写库，遵从铁律 5）。

- **`core/StoryGen.{h,cpp}` 纯函数**：`build(photos, gapDays=5, hidePrivate=true)`
  按「地点（prov→city→region→未知地点）+ 时间窗口（同地点相邻拍摄日间隔 >
  gapDays 拆新故事）」聚合。复用 MapPage 三式日期解析；无日期照片诚实保留为
  「未标注日期」单张故事，绝不丢弃。
- **★守恒契约（核心，铁律 6）**：`Result{inputCount, outputCount}` 断言
  `outputCount==inputCount`；每张照片恰好落入一个故事。`setForceDropForQa`
  QA 钩子注入「丢一张」以证反例转红。
- **`GalleryModel` 接入**：新增 `storyFilterPaths_` 成员 + `setStoryFilter(paths)`，
  `rebuild()` 内 `hasStory && !storyFilterPaths_.contains(p.path)` 跳过；与既有
  personFilter / cluster 等筛选**并存叠加**（不破坏既有语义）。
- **`GalleryPage` UI**：
  - `buildStoryBar()` 建「全部故事」+ 各故事 chip（`build()` 结果），点击
    `model_.setStoryFilter(storyPaths_[id])`；自动解除失效故事。
  - 规模安全：真实库 6425 张可能聚合出成千上万故事，每故事一个 `QPushButton`
    会拖垮首屏并让规模门崩在构造期 → 故事 chip 按 `photoCount` 降序封顶
    `kCap=60`，超出加「…共 M 个故事」禁用提示；`storyPaths_`/`storyTitles_`
    仍全量写入所有故事（点可见 chip 始终可用）。
  - A 增强：人物 chip 改 34×34 圆形首字母头像（`border-radius:17px`、`:checked`
    绿环、`storyAvatarColor()` 用 FNV-1a 哈希 → 8 色板确定性主题色，诚实不伪装
    脸代表图，FaceCluster 仅存 embedding、磁盘无现成脸图）。
  - `updateCount()` 加故事注记「故事：<标题>」。
- **门禁（gates 63 → 64）**：`LA_STORY_QA`（full、`measuredMs=162`、无外部素材、
  合成 `QVector<Photo>`）。正例：守恒 `input==output==7`、每路径恰一次 + 全量覆盖、
  `stories==5`/`nodateSingles==2`、挑上海故事 `setStoryFilter` 后 `rowCount==2`、
  清除后 `==7`。反例 `LA_STORY_QA_FORCE_BROKEN=1` 经 `setForceDropForQa(true)` 令
  `build()` 丢一张 → 守恒/覆盖/分组/无日期**同步**转红（期望值不随 broken 切换，
  铁律 22/41，非空心）。

### 回归

- 全量 `PASS=53 FAIL=0 SKIP=11`（64 门并集）；`LA_STORY_QA [OK]`。
- 规模门 `LA_SCALE_SMOKE`/`LA_SCALE_PERF` 复跑 `galleryCtrls=233` 绿（封顶修复前
  成千上万控件会令 `GalleryPage` 构造期崩、规模门无输出 FAIL）。
- 既有门禁 `LA_CLUSTER_QA`/`LA_FACE_CLUSTER_QA`/`LA_GALLERY_*` 无回归。

## [2.31.0] - 2026-10-09

### 新增 - 视频抽帧磁盘缓存

`VideoDecode::extractPoster` 现在把抽帧结果缓存到磁盘，命中时直接从缓存拷贝，
避免同一视频同一秒反复起 ffmpeg（~0.3-0.8s/次）。Scanner 缩略图、RetouchPage
修图、选帧器回退均自动受益。

- **缓存键**：文件规范绝对路径 + 修改时间 + 大小 + 秒数（SHA-256）。
  不哈希文件内容（大视频太慢），mtime/size 足以捕获常见修改/替换场景。
- **缓存位置**：`QStandardPaths::CacheLocation/frames`，两层子目录防单目录膨胀。
- **默认预算**：512 MiB（`LA_VIDEO_CACHE_BUDGET_BYTES` 可覆盖）；超预算时按
  mtime 淘汰最旧条目。
- **调用方契约不变**：仍返回临时 JPEG 路径，调用方用完删除；缓存文件由
  `VideoDecode` 内部管理。
- **QA 反例安全**：任何 `setForce*ForQa` 钩子激活时自动绕过缓存，防止把
  缺陷形态固化到磁盘（铁律 22）。

### 新增 - 选片页视频适配

选片（Cull）页此前对视频只有「能显示首帧缩略图」，缺视频身份标识与过滤。
本次补齐与图库页一致的视频适配，复用图库页既有 `Lumen::paintVideoBadge`。

- **数据透传**：`Photo.durSec`（Scanner 已用 ffprobe 填充）→ `CullInput.durSec`
  → `CullItem.{mediaType,durSec,isVideo}` → `CullModel::{MediaTypeRole,DurRole,
  IsVideoRole}` → `CullCardDelegate` 徽标。全链路不改既有 image 路径。
- **统一徽标**：从 `paintGalleryCard` 抽出 `Lumen::paintVideoBadge(p, rect, durSec)`，
  Gallery 与 Cull 共用同一绘制（左上圆角暗底 + 白色 ▶ + 时长 `m:ss`），视觉一致。
- **「只看视频」过滤**：`CullProxy::setVideoOnly(bool)` + 分类 chip / 评分下拉共用
  `__video__` 哨兵；选片结束 `onFinished` 自动复位 `setVideoOnly(false)`。
- **QA 反例钩子**：`CullPage::qaSetVideoBadgeDisabled(bool)` 由 `LA_CULL_VIDEO_QA_
  FORCE_BROKEN=1` 驱动，关掉徽标绘制以证像素断言非空洞（徽标像素归零、其余 11 项仍绿）。

### 门禁（gates 62 → 63）

- `LA_VIDEO_CACHE_QA`：命中/失效/清空门。
  - 正例：首次抽帧写入缓存（count=1, bytes>0）；第二次命中（count 不变、内容一致）；
    修改源文件 mtime 后缓存失效并产生新条目（count=2）；`clearFrameCache()` 清空。
  - 反例 `LA_VIDEO_CACHE_QA_FORCE_BROKEN=1`：强制绕过缓存 → C2/C3/C5/C8 转红。
- `LA_CULL_VIDEO_QA`：fast 门、measuredMs=80、无外部素材依赖（合成缩略图）。
  - 正例（12 checks）：数据透传（候选含视频、isVideo/mediaType/durSec 正确）、
    model 3 角色、proxy videoOnly 过滤、像素徽标存在（whiteBadge>5）+ 时长文本可见。
    实测 `checks=12 failed=0 broken=0 badge=63`。
  - 反例 `LA_CULL_VIDEO_QA_FORCE_BROKEN=1`：仅徽标相关断言转红（badge=0），
    其余数据/model/filter 检查仍绿（期望值不随 broken 切换，铁律22/41）。

### 回归

- fast `PASS=16 FAIL=0`；full `PASS=40 FAIL=1 SKIP=5`（FAIL 为 `LA_SCALE_PERF`
  偶发负载敏感门，单独连跑 3 次均通过，非本改动引入）。
- 既有视频门禁 `LA_VIDEO_QA`、`LA_VIDEO_FRAME_QA`、`LA_VIDEO_PLAY_QA`、
  `LA_VIDEO_UI_QA` 无回归。
- 新增 `LA_CULL_VIDEO_QA` 与相邻门无回归：`LA_CULL_EXEMPT_QA`（41 checks PASS）、
  `LA_GALLERY_E_QA`（PASS，证 GalleryCardPainter 抽出安全）、`LA_VIDEO_CACHE_QA`
  （10 checks PASS）均通过。

## [2.30.0] - 2026-10-08

### 新增 - 视频播放（内嵌解码器）

双击视频卡片即以内嵌播放器播放（含声音），覆盖导入支持的 14 种视频格式。
用户可在「ffplay 外部进程（+102MB）」与「内嵌自研解码器」间选择，取后者：
包体保持 92MB、体验内嵌统一、格式全覆盖。

- **架构**：经 `QProcess` 调随包 `ffmpeg.exe`，视频走 **rawvideo 裸流管道**
  （`-an -f rawvideo -pix_fmt rgb24 -s WxH -r fps -`），音频走 **s16le PCM 管道**
  （`-vn -f s16le -ar 44100 -ac 2 -`），两条均带 `-re` 按原速率 1x 播放。
- **绝不链入 libavformat/libavcodec** —— 链接会使项目从「独立进程调用
  （mere aggregation）」变为「同映像链接」，GPL-3.0 copyleft 即传染到本源码。
  走 exe 则2.28.0 确立的许可立场不变。
- **音频输出用Windows 原生 winmm waveOut**（`QAudioSink` 不可用：实测本Qt
  6.8.3 mingw_64 **既无 Qt6Multimedia 也无 Qt6Audio**）。不新增依赖、不增包体。
- **音画同步**：两路同刻启动天然同步；跳转时视频与音频一起 `-ss` 重起。
  音频为**可选增强** —— 音频耗尽或设备报错不牵连视频，优雅降级为静音播放。
- **接线**：复用既有 `MainWindow::openLightbox` 入口按 `mediaType=="video"` 分流，
  故首页 / 图库 / 地图三处「打开」视频自动生效，无需改各入口。
- **控件**：播放 / 暂停（按钮或空格）、进度条拖动跳转、时间显示（分:秒）、Esc 关闭；
  等比缩放居中不拉伸；首帧前显示库内海报，再前「▶ 加载中…」，绝不白屏。

### 门禁（gates 57 → 60）

- `LA_VIDEO_PLAY_QA`：解码门。断言帧数 > 0、末帧非空且尺寸正确、**帧间采样像素
  必不同**（真动态解码，静止图/占位图会红）、pts 递增。正例 `frames=60 lastPts=2360`；
  反例 `frames=0` → A1–A5 全红。
- `LA_VIDEO_UI_QA`：离屏渲染门。`grab()` 后统计画面区非底色像素（铁律24：视觉必须
  实测）。实测正例 `nonBg=5689`、反例仅 `20`（只剩占位文字）。
- `LA_AUDIO_QA`：音频门。**不靠「能听见」判定**（离屏/无设备不可靠），断言
  「waveOut 设备可开」+「解出 PCM 字节 > 0 且按实现块大小对齐」。正例 ≈18 万字节；
  反例 0 字节。
- 三门反例均 `exit=1`、`checks` 数与正例相同（期望值不随 broken 变，铁律22/23）。

### 门禁抓到的真实 bug

- **winmm 缓冲空闲判据**：首版 `freeIdx()` 只认 `WHDR_DONE` 为空闲，但初始缓冲
  `dwFlags=0`（从未播放）→ 无一缓冲带 DONE → 永远返 -1 → 环时空转、PCM 写 0 字节。
  正解：「未prepared **或** 已 DONE」才算空闲。**若只探「设备可打开」会完全漏掉**
  —— 是「断言实际写入字节数 > 0」抓住的。
- **`-vsync` 已移除**：ffmpeg 9.x 报 `Unrecognized option`，只用 `-r`。
- **`avg_frame_rate` 是分数字符串** `"25/1"`，不能复用 probe 里的 `numOf` helper
  （分数 `toDouble` 失败返 0），须按 `/` 拆；`"0/0"` = 未知回退默认帧率。

### 发布

- 回归 fast `PASS=16FAIL=0` / full `PASS=35 FAIL=0 SKIP=9`；gates **57 → 60**
  （fast 保持 16，full 41 → 44）。
- 提交序列 `cf5b46c`（解码内核）→ `7977006`（UI + 灯箱 + 接线）→ `75aacde`（音频）。
- 包体与依赖与 2.29.0 **完全一致**（92MB，无新增第三方二进制）。

## [2.29.0] - 2026-10-08

### 修复 - 2.28.0 接入 RAW/视频后的下游漏接点

- **选片豁免（修复误杀）**：2.28.0 接入 RAW/视频后，质量选片仍按「纯 JPEG」判格式，
  且各自持有独立扩展名表，与 `RawDecode`/`VideoDecode` 漂移 8 种 RAW（Sigma / Pentax /
  Phase One / Mamiya / Ricoh / Sony 等）+ 10 种视频。收敛为**唯一真源**
  `CullInput::exemptFromQualityCull()`：RAW（24 种）与视频（14 种）在质量选片中统一
  **只查分辨率**，不再被「模糊 / 曝光」误淘汰。iCloud 空壳判定同步改走
  `VideoDecode::isVideoExt()`。新增门禁 `LA_CULL_EXEMPT_QA`（fast 层，含极暗合成图三路径对比）。
- **修图页支持 RAW/视频（修复进不去）**：2.28.0 的 RAW 进了库与缩略图，却漏了修图页入口——
  `QImage(原图)` 直读 RAW 返回 `0×0`，RAW 根本进不了编辑器。新增
  `core/ImageLoad::loadForEdit()` 唯一加载器收口照片 / RAW / 视频三路；
  `RetouchEngine::apply` 是与格式无关的纯函数，零改动即可吃 RAW。
  新增门禁 `LA_EDIT_RAW_QA`（含端到端像素门，实测 changed=98.4%）。
- **灯箱后台解码支持 RAW**：全尺寸预览的后台解码任务补齐 RAW/视频分派，保留原
  「先读尺寸、超界放弃」解码炸弹前置防护（外部格式无法解码前取尺寸，改解码后校验）。
  在 `LA_TRANSITION_QA` 补真实断言：FullCache 必须解出 RAW（设 `LA_RAW_SAMPLE` 时生效）。
- **系统审计**：按新铁律「新增格式必查 10 个下游入口」逐一审过 —— 扫描 / 缩略图 /
  EXIF / 选片 / 修图 / iCloud 已合规；去重、分类只吃缩略图，地图 GPS 来自 EXIF，
  均格式无关，本就合规。

### 发布

- 门禁 **gates 57**（fast=16 / full=41）；`LA_GATE_MATRIX_QA PASS fails=0`。
- 回归 fast `PASS=16 FAIL=0` / full `PASS=35 FAIL=0 SKIP=6 KILL=0`。
- 提交序列 `40c19aa` → `01bb203` → `1d57a5d`（均经 pre-commit 复跑 fast 层判 GREEN）。

## [2.28.0] - 2026-10-07

### 新增 - RAW 格式支持（LibRaw 外部解码器）

- **架构**：RAW 走**独立进程**调用 LibRaw 的 `dcraw_emu`，**不链入 exe**。
  故宿主（MinGW/GCC）与解码器（MSVC 2022 预编译）的 CRT/ABI **完全解耦** ——
  这也是可直接使用官方预编译产物、无需自行源码构建的原因。
- **中间格式实测定为 PPM（非 TIFF）**：本机 Qt 插件仅 `qgif/qico/qjpeg/qsvg`，
  **无 `qtiff.dll`**，实测 TIFF 经 `QImageReader` 读出 `null / 0x0`（会静默失败）；
  而 PPM 属 Qt **内建** handler，不依赖插件部署，实测 1737×1157 非空。
- 新增 `core/RawDecode.{h,cpp}` 作为唯一真源（`isRawExt` / `isImportable` /
  `needsExternalDecode` / `available` / `decodeToTempPpm` / `probeSize`）。
  管线仅 3 处接线；`makeThumb` 对 RAW 先解 PPM 再喂**完全相同**的下游逻辑
  （刻意不复制缩略图代码，避免两实现漂移）。
- **EXIF 零改动**：`ExifReader` 按**文件签名**（`II*` / `MM*`）而非扩展名分派
  TIFF 容器，故 DNG 的 EXIF 今天即可解析。实测导入行拿到
  `cam=Canon EOS 350D / iso=100 / sh=1/800 / ap=f/7.1 / focal=20`。
- **尺寸补齐**：`ExifReader` 的 `dim` 走 `QImageReader::size()`，对 RAW 恒 invalid，
  故 RAW 的 `dim` 改由解码实测（PPM 头）取得；仅在 `dim` 缺失时才付解码代价。
- **优雅降级**：解码器缺失时 RAW **不导入** —— 若仍导入会产出一整排无缩略图的
  空卡片，比「看不见 RAW」更糟。
- 门禁 `LA_RAW_QA`（full 层，`need=LA_RAW_SAMPLE` 声明式依赖）。
  修掉一个由此引入的真实回归：并联判据误用语义更宽的 `isImportable`（非 RAW 恒 true），
  致 `!isPhoto && !isImportable` 恒假、白名单被短路放行噪声文件
  （`LA_ICLOUD_QA` total 5→10）。改用 `needsExternalDecode`（只对 RAW 为真）
  并补 7 条负向断言钉死该边界。

### 新增 - 视频格式支持（FFmpeg 外部解码器，视频为一等公民）

- **架构**：与 RAW 同构 —— 经 `QProcess` 调 `ffmpeg.exe`（抽帧）/ `ffprobe.exe`（元数据），
  **不链入 exe**。新增 `core/VideoDecode.{h,cpp}` 为唯一真源。
- **schema 迁移零成本**：复用项目既有幂等补列机制（`Scanner.cpp` 的
  `kExtraColumns` + `ALTER TABLE`），`photos` 新增 `media_type TEXT` / `dur_sec REAL`；
  `Photo` 模型新增 `mediaType` / `durSec`。老库无迁移负担。
- **元数据取客观量**：`ffprobe -of json` 解析（抗版本漂移，优于正则扫文本），
  取得 `duration` / `width` / `height` / `codec_name`。抽帧取 `-ss 00:00:02`
  以避开首帧黑场，封面输出 JPEG（Qt 实读 640×480 非空）。
- **数据诚实**：视频无 EXIF 概念，故 `cam` / `ap` / `sh` / `iso` 一律留空，
  **绝不编造**；时长未知（`<=0`）时徽标只画播放三角、不显示时长。
- **UI**：卡片**左上角**新增播放徽标 + 时长标签（`0:07` / `12s` 两种格式）。
  位置经核对为四角中唯一空闲位 —— 右上已被横竖徽章占用、左下是标题/副行、
  右下是星级（铁律 30：新增可视标记前须核对不吞既有命中区）。
  播放三角用 `QPolygonF` 几何绘制而非字体字形，跨字体稳定。
  数据经 `GalleryModel` 新增 `MediaTypeRole` / `DurRole` 传递（同一真源，不在调用点转算）。
- 门禁 `LA_VIDEO_QA`（full 层，`need=LA_VIDEO_SAMPLE`）：A 组纯函数契约、
  B 组真实抽帧/探测、C 组**徽标像素门**（离屏渲染 `isVideo` 真/假两卡比对左上角，
  双向断言：视频卡必须有徽标、图片卡必须无徽标 → 同时防「恒显」与「永不画」两类退化）。

### 修复

- **★ `QJsonValue::toDouble()` 对字符串静默返回 0.0**：ffprobe 的 `duration` 输出的是
  字符串 `"10.000000"`，而同一 JSON 里的 `width`/`height` 是真数字故 `toInt()` 正常 ——
  这种「一半对一半错」导致首版实现时长恒为 0 却表面正常。现统一走兼容字符串/数字的取值helper。
- **★ 门禁夹具必须让被测阶段可达**：徽标像素门首版传入空 `QPixmap()`，
  导致 `paintGalleryCard` 走骨架屏分支**提前 return**，根本到不了徽标绘制处 ——
  像素差恒 0 却被误读为「徽标没画出来」。改传非空 pixmap 后实测
  `badgeDiffPixels=1018`，证明徽标确实渲染（铁律 20 的变体）。

### 依赖与合规

- **LibRaw 0.22.2**（双许可 LGPL-2.1 / CDDL-1.0）：随包分发 `dcraw_emu.exe` +
  `libraw.dll` 及三份许可文本，缺 `LICENSE.LGPL` 即中止打包。
- **FFmpeg 9.0.2 essentials**（**GPL-3.0**，实测含 `--enable-gpl --enable-version3
  --enable-libx264`）：随包分发 `ffmpeg.exe` + `ffprobe.exe`，
  **GPL-3.0 文本必须随包**，缺失即 throw。
  - `ffplay.exe`（约 107MB）**刻意不随包** —— 本项目只抽帧与读元数据，从不播放。
  - 两个 exe 各约 100MB 且**全静态无 DLL**，故**发布包体积增加约 200MB**。
    这是已知并被接受的取舍。
  - `THIRD_PARTY_NOTICE` 说明：FFmpeg 以独立进程调用，与本项目 exe 不构成同一
    可执行映像（*mere aggregation*），故 **GPL copyleft 不传染**到本项目源代码；
    但我们不隐瞒包内含其完整副本，并附许可文本与源码获取途径。
    若未来改为链入 exe，上述结论**不再成立**，须重新评估整体许可。
- 仓库维持**零二进制**惯例：`.gitignore` 排除 `3rdparty/*/bin/`，
  新增 `fetch_libraw.ps1` / `fetch_ffmpeg.ps1`（均**纯 ASCII 无 BOM**，
  带 `-SelfTest` 契约自检 —— 分别 `contracts=2` / `contracts=3`）。
- `CMakeLists.txt` 在 `POST_BUILD` 把解码器产物拷到 `<exe 同级>/3rdparty/*/bin`，
  使**开发态与发布态走同一条查找路径**；未获取到产物时静默跳过并优雅降级。

### 门禁

- 门总数 **53 → 55**（新增 `LA_RAW_QA`、`LA_VIDEO_QA`，均 full 层）。
- 回归：fast `PASS=15 FAIL=0`（与 2.27.0 基线同数）/ full `PASS=34 FAIL=0`。
- `LA_RELEASE_QA PASS contracts=8 injected=13 caught=13`；
  `LA_GATE_MATRIX_QA PASS gates=55 fast=15 full=40`。

---

## [2.27.0] - 2026-10-07

### 新增 - 方向 A·地图「国家层深化」三件套

- **① `footYear` 贯通国家层**：`setFootYearFilter` 在「已进入国层」时（
  `countryFeature_ >= 0` 且 `gpsPts_/gpsPaths_` 下标对齐）实时重算 `countryPts_/
  countryPaths_/countryDates_` 快照并 `invalidateViewCache()`。国层内切换年份 /
  切换图库，点击命中集与相册载荷不再滞留进入国层那一刻的年份。抽出 `recomputeCountryPts()`
  承载原 `enterCountry` 的几何 pip 过滤（外环 pip + 湖泊 inHole 排除 + <3 单位离岸容差），
  并同步快照 `countryDates_`。
- **② 国层相册按年分桶聚类**：新增 `countryAlbumGroups()`，按 `countryDates_` 的年份降序
  分桶（0 置底），供 `onCountryPicked` 展示与相册聚类门禁断言。
- **③ 热力分档临界点可调**：`heatRamp` 改走 `heatTierOf(dens)`（默认 `heatTierLo_=3` /
  `heatTierHi_=8`）；新增 `setHeatTiers(lo, hi)`（`qBound` 钳制后 `buildWorldPixmap()` +
  `invalidateViewCache()` 双烘焙），UI 灵敏度滑块可直接调档。

### 测试 - 方向 A·国家层深化门禁簇（2.27.0 新增）

- `LA_MAP_COUNTRY_YEAR_QA`：`enterCountry` 注入同国跨年 6 点 → 改年筛选 →
  断言 `countryPts_` 实时收敛（全量 6 / 2024=4 / 2023=2 / 全部 6）。
- `LA_MAP_COUNTRY_ALBUM_QA`：断言 `countryAlbumGroups()` 给出正确逐年分组（2024 单年 4 张）。
- `LA_MAP_HEAT_TIER_QA`：默认 `dens=5 → tier1`；`setHeatTiers(2,3)` 后 `→ tier2`。
- 三道均带 `FORCE_BROKEN` 反例（期望值不随 broken 变，铁律 22/32），反例正确转红。

### 修复 - 门禁派发连通性

- `main.cpp` 开场动画门控 `animOk` 排除清单补入上述三个新门禁 env（`LA_MAP_COUNTRY_YEAR_QA` /
  `LA_MAP_COUNTRY_ALBUM_QA` / `LA_MAP_HEAT_TIER_QA`）。此前遗漏导致这三道门进入开场动画分支、
  永不抵达派发块、被看门狗 20s 判 `no-exit-request` 误杀（铁律 11 指纹）。

## [2.26.0] - 2026-10-07

### 优化 - 方向 A·地图世界层「显示分辨率 + 1:1 blit」

- **取证（先测再改）**：`LA_MAP_PERF` 实测 World 层每帧 ~2.30ms（原因为每帧在 `paintMap` 的 World 分支
  全量重栅格化世界层）。用户决策三项：① 世界层采用「显示分辨率烘焙 + 每帧 1:1 平移 blit」（推荐）；
  ② `LA_MAP_PERF` 采用**证据化宽松门槛**（world≤1.2ms、其余层级 ≤1.5ms）；③ 临时取证探针转常驻 QA 钩子。
- **实施**：`worldPix_` 按显示分辨率烘焙（`size × DPR × worldZoom_`），仅 `buildWorldPixmap()` 一次栅格化；
  每帧 `paintMap` 世界分支仅做 **1:1 平移 blit**（`ensureWorldPix()` 惰性自校验：逻辑尺寸 / zoom 任一变化才重烘焙）。
  命中/绘制继续共用 `viewOrigin()` 变换（铁律 4）。
- **★回归修复（TRACK/HEAT/TRANIM 空世界层）**：5 个内容触发 setter
  （`setGpsPoints`/`setFootLayerMode`/`setHomePoint`/`setArcRegionIds`/`setHeatRamp`）曾被误改为惰性
  `worldDirty_=true`，导致 `qaShootTrack/Heat/TrackAnim` 绘制到空 `worldPix_`。还原为急切实调
  `buildWorldPixmap()`（保持 QA 探针契约）。新增 `qaFixedBake_` 标志使 QA 探针下固定 3× 超采样
  （3000×1500），复现方向 A 之前的确定性像素计数（避免显示分辨率烘焙在 offscreen 下尺寸不定导致像素门 flaky）：
  实测 TRACK=trackPx=268、HEAT=warmPx=42、TRANIM=arrow=59（均与原基线完全一致）。

### ★回归修复（GEO 偶发挂死 rc=3 no-exit，我引入）

- **证据化定位**：`git stash` 对照 2.25.0 基线，GEO **4/4 稳定 PASS（marks=26）**；我的版本偶发 `rc=3`
  （watchdog `no-exit-request`）。确证是我引入的 race，非 pre-existing。
- **根因**：`ensureWorldPix()` 新增 `!qFuzzyCompare(worldPixDpr_, dpr)` 比较，而 offscreen 平台
  `devicePixelRatioF()` **不稳定**（项目记忆确证）；dpr 微差使该检查失败 → 在 GEO 嵌套 `processEvents`
  的 paint 期间触发**重型 `buildWorldPixmap()` 冷重建** → 死锁并撞 20s watchdog。
- **修复**：移除 `worldPixDpr_` 成员与浮点比较，惰性重烘焙仅依据**逻辑尺寸 + zoom**（世界层 1:1 blit
  只需二者一致）；同时移除 `main.cpp` 我加入的 7 处 `DBG_GEO`/`LA_DBG` 调试标记（避免污染正常门禁输出）。
- **验证**：GEO **连跑 8/8 rc=0**（marks=26）；全量地图簇无回退。

### 门禁矩阵

- `LA_MAP_PERF` 实测 world=**1.05ms**（原 ~2.30ms，↓约 54%）、region=0.30ms、province=0.30ms、
  country=0.25ms（其余 ≤0.3ms，远低于证据化宽松门槛 world≤1.2 / 其它≤1.5），rc=0 无 FAIL。
- 全量地图簇（A / TRACK / HEAT / TRACK_ANIM / HEAT_RAMP / REGION / BURST / COUNTRY / YEAR / CLICK / GEO / PERF）
  正例全绿；TRACK/HEAT/TRACK_ANIM 的 FORCE_BROKEN 反例均正确转红（非空心）。
- 版本 2.25.0 → **2.26.0**（MINOR：地图渲染性能优化），bump 后已重新 configure
  （`LA_VERSION="2.26.0"` 已确认烘进编译标志）。

## [2.25.0] - 2026-10-06

### 新增 - 方向 B 门禁深化：`LA_AGENT_QA`（第 50 门，LumenAgent 纯函数值契约）

- **取证依据（先测再改，路线图 §8.2 方向 B「优先门禁深化」）**：`LA_AGENT_SMOKE` 只有
  3 个布尔量（`real/fb/clamp`），而 `LumenAgent` 的四个纯函数存在大面积**值级断言缺失**：
  - `parseShutterSeconds` —— **全库零断言**（仅 `RetouchPage.cpp:783` 一处产品调用）；
  - `contextFrom` —— 仅被间接调用，无任何直接断言；
  - `decideLocal` —— 8 条规则分支（暗/亮/灰/反差过强/淡/浓/冷暖/ISO 档 + 长曝叠加）
    现仅覆盖 3 条，goal 分支（胶片/黑白/人像/通透）**零覆盖**；
  - `clampPlan` —— 13 键白名单现仅测 2 键。
  属铁律 29「存在即恒真」的高发区：把这些实现删掉，门不会红。
- **实施**（纯新增门，零产品行为改动）：`runAgentQa()` 43 条断言，纯函数、零网络、亚秒级。
  - **A. `parseShutterSeconds` 真值表**（12 条）：`1/125`→0.008、`1/60`、`1/4000`、
    `15s`→15、`2.5s`、`0.5`、`30`、空串/`abc`→0、**`1/0`→0（防除零）**、`1/`→0。
  - **B. `clampPlan` 白名单 + clamp 边界**（7 条）：13 键**全保留**（判别器：删白名单即红）、
    `evil`/`_intent`（LLM 元字段）丢弃、`+5→1.0`/`-5→-1.0`/`0→0`。
  - **C. `decideLocal` 方向契约**（14 条，**单参数隔离**承铁律 27）：暗→`brightness>0`、
    亮→`highlights<0`、灰→`contrast>0`、反差过强→`contrast<0`、淡→`saturation>0`、
    浓→`saturation<0`、偏暖→`warmth<0`、偏冷→`warmth>0`、ISO3200→`denoise==0.30`、
    ISO6400→`0.50`、长曝 4s→叠加至 `0.50`、`sharpness==0.15` 恒定、
    目标「黑白」→`saturation==-1.0`+intent、「人像」→clarity 相对基准下降、
    计划恒合法（`ok`/`source=local`/13 键/值域 [-1,1]）。
  - **D. `contextFrom` 测量单调性**（10 条）：黑图→`lum<0.05`、白图→`lum>0.95`、
    灰图→`sat<0.05`、暖图→`cast>0`、ISO/快门/goal **原样透传**。
- **反例（非空心，实测变红）**：`LA_AGENT_QA_FORCE_BROKEN=1` 经
  `LumenAgent::setForceSignFlipForQa(true)` 让 `decideLocal` 把 `brightness`/`saturation`
  符号取反，**精确复现 2.24.0「滑杆方向反向」的真实缺陷形态**。实测
  正例 `PASS checks=43`（rc=0）→ 反例 `FAIL checks=43 failed=4`（rc=1），
  变红项 `dir.dark.brightness+` / `dir.lowsat.saturation+` / `dir.highsat.saturation-` /
  `goal.bw.saturation-1`。**期望值（真值表/键数/阈值）不随 broken 切换**（铁律 22）。
- **实测分层（铁律 12：依据必须实测）**：runner `-Timing` 实测 **70ms**（纯计算、
  无网络无磁盘 → 确定性），远低于 500ms fast 预算 → 登记 `tier="fast"`。
  `gate_matrix -SelfTest` 复核 `gates=49 fast=15 full=34 fails=0`、E8 `missingMeasuredMs=0`。

### ★ 新增铁律 32：注入点必须落在「最终值」之后，且断言数据源须与产品同编码

- **① 注入点位置**：反例旗标首版放在 `p["sharpness"]` 之后、**goal 分支之前**，
  结果「黑白」分支的 `saturation = -1.0` 把注入结果**覆盖** → `goal.bw` 断言在 broken
  下反而变绿 → **反例失效**（承铁律 22 反面）。移到 goal 分支之后即修正。
  **通则：注入要模拟「缺陷发生在哪一步」，就必须插在那一步之后**，否则测的是中间态。
- **② 编码一致性**：门禁构造中文 goal 时首版用 `QString::fromLatin1("黑白")`，
  UTF-8 汉字被逐字节映射成 mojibake → `contains("黑白")` 恒 false → 黑白/人像分支
  **永不触发** → 门禁自己假红 3 条。**凡断言含中文的输入，必须与产品同用
  `QStringLiteral`**（`RetouchPage` 侧传的是 `Photo.shutter` 原文，UTF-8）。
  这条与铁律 14（i18n 排除集）同源：**数据侧与 UI 侧的编码假设必须分别取证**。

### 门禁矩阵

- 登记 `LA_AGENT_QA`（`tier="fast"`, `measuredMs=70`），门数 49→**50**（fast 15 / full 34）。
- **全量回归实测 `PASS=31 FAIL=0 CHECK=0 SKIP=3 KILL=0`（gates=34，~91s）**；
  fast 层 `PASS=15 FAIL=0`（5.3s）。
- 版本 2.24.2 → **2.25.0**（MINOR：新增门禁基建），bump 后已重新 configure
  （`LA_VERSION=\"2.25.0\"` 已确认烘进编译标志）。

### 附：取证纠正 —— `LA_AGENT_SMOKE` 并非空心门

- 本轮曾据 `LA_AGENT_SMOKE PASS real=1 fb=1 clamp=1` 的输出**推断**它是铁律 29 空心门，
  读源码后**自我纠正**：它实际含真实方向断言（暗样张 `brightness>0`、ISO3200
  `denoise>=0.30`、goal 通透 `clarity>=0.10`）与 clamp 白名单断言。
  **教训：不可凭单行摘要给门下判决，须读断言体**（"看起来是恒真"与"确实是恒真"是两件事）。

## [2.24.2] - 2026-10-06

本迭代同时落地路线图**方向 C（相册管理·智能检索）**与**方向 D（规模化·流畅性）**的
§8.2 待办：地图地理热点联动 + 真实库性能回归门。两者均**纯门禁 + 纯新增**，不改既有交互语义。

### 新增 - 方向 C：地图地理热点聚合 + 一键联动图库（`GeoHotspot` + `LA_GEO_LINK_QA`）

- **动机（取证）**：地图此前只有**单个** GPS 光点（`drawGpsDots`），没有"按国家/省聚合"的
  热点层；`MapPage` 也无法从地图一键把图库筛到某个国家/省。§8.2 记为"未落地"。
- **实现**：新增自包含聚合器 `core/GeoHotspot.{h,cpp}`（`GeoHotspot::compute`）：
  - `Level::World` → 按 `Photo.region` 聚合（key 落 `GalleryModel::setRegionFilter`）；
  - `Level::Region` → 按 `Photo.prov` 聚合（key 落 `setProvFilter`）；
  - 质心 = 该组有 GPS 照片的经纬度均值（预投影空间），`count` = 照片数；尊重隐私门。
  - `GeoHotspot.h` **自包含**（自带 `Level` 枚举 + `MAP_W/MAP_H`），避免 core↔ui 头循环。
- **渲染/交互**：`MapView` 新增热点徽章层（半径随 count 对数增长 7→24px，翡翠绿 + 计数），
  World/Region 两级绘制；点击徽章 → `hotspotActivated` → `MapPage::onHotspotActivated`
  → 复用既有 `requestGallery` 管道 → `MainWindow` → `gallery_->applyGeoFilter`（**不新增写库路径**）。
- **门禁 `LA_GEO_LINK_QA`（第 48 门，非空心）**：
  - 正例实测 `PASS marks=26 checks=26 hotspots=3`；断言含**核心契约**：
    对每个热点 `h`，`GalleryModel.setRegionFilter(h.key).rowCount() == h.count`
    （World）/ `setProvFilter(...)`（Region）——即"点热点恰好筛出该子集"，非"存在即恒真"。
  - 反例 `LA_GEO_LINK_QA_FORCE_BROKEN=1`（World 层误按 `prov` 聚合）实测 `FAIL marks=14 checks=22
    hotspots=4`，期望值**不随 broken 切换**（铁律 22）。

### 修复 - 联动徽章吞掉「点国家下钻」命中区（`LA_MAP_CLICK_QA` 由红转绿）

- **症状**：C 联动上线后 `LA_MAP_CLICK_QA` 的 `anchor=FAIL(level=0)`、`all=FAIL(8/10 bad:china;japan)`。
- **根因（实测非猜测）**：徽章质心 = 该国**全部照片 GPS 均值**（中国 5140 张 → r=24px），
  随个人库数据漂移，恰好压住大区锚点（金点，10px 命中区）→ 徽章命中 disc（r+6=30px）吞掉锚点，
  "点国家下钻"这一**既有**交互被联动徽章抢走。
- **修法（三处同源，铁律 4）**：
  1. `hotspotPrePos()`：World 级徽章定位到**稳定的** `RegionInfo::proj`（大区锚点）而非质心；
  2. `hotspotLift() = r + 8`：徽章上抬，`r+8 > r+6` **证明**不与锚点 10px 命中区重叠；
  3. `mousePressEvent` 锚点优先豁免：命中任一锚点 10px 内时让位下钻（兜住质心回退分支）。
  绘制 / 命中 / QA 探针三处统一用同一 lift，**杜绝"看得见点不着"**。
- **验证**：`LA_MAP_CLICK_QA` 全项 OK（`anchor=OK`、`all=OK(10/10)`），`LA_GEO_LINK_QA` 仍 PASS。

### 新增 - 方向 D：真实库性能回归门 `LA_SCALE_REAL_PERF_QA`（第 49 门）

- **动机（取证）**：`LA_SCALE_PERF` 跑的是仓库内合成 fixture（6425 行，**文件在磁盘全不存在**），
  其首屏/滚动只验了"虚拟网格"绘制成本，**从未触碰真实规模下**缩略图取盘/混合格式这些
  真实才贵、且随个人库变化的路径——属"以合成假绿掩盖真实风险"（承接铁律 24/25）。
- **实现**：`runScaleRealPerfQa()` 复用 `runScalePerf` 同一套 `GalleryPage` 首屏 + 滚动跳变帧
  量化，但**强制跑真实库**（`need=LA_SCALE_REAL_DB`，CI 优雅 SKIP，不自造 CHECK 警告态，铁律 26）。
  断言卡"成分"：① 首屏 ② 滚动单帧 paint 预算（默认 15s / 50ms，env 可覆盖）。
- **反例（one-shot 注入，踩坑后修正）**：`LA_SCALE_REAL_PERF_QA_FORCE_BROKEN=1` 经
  `EditorialView::qaSetPaintLagMsForQa(300)` 在**滚动测量前**布防一次 +300ms 绘制延迟。
  - ★ 首版按"逐帧注入"实现 → 一个滚动帧重绘十余瓦片，20 跳累计数十秒，**撞上 qaExit 看门狗
    20s 预算**误报 `no-exit-request`（`EXIT=3`）。改为 **one-shot**（只污染一个 paint 事件，
    总开销与瓦片数无关）后修复。
  - ★ 注入点必须在**滚动阶段**布防：若在首屏前布防，注入会落进 `firstMs`（15s 预算）被吞掉、
    门误绿（铁律 22 的反面：反例该红却绿）。
  - 实测：正例 `PASS firstMs=273 scrollMaxMs=18`；反例 `FAIL scrollMaxMs=311>budget=50`（`EXIT=1`）。
- **实测基线（真实库 6526 行）**：`firstMs≈273 / scrollAvgMs≈9 / scrollMaxMs≈18`。

### 门禁矩阵

- `gate_matrix.ps1` 登记 `LA_GEO_LINK_QA`、`LA_SCALE_REAL_PERF_QA`（后者 `need=LA_SCALE_REAL_DB`，
  `tier` 缺省=full）。**全量回归实测 `PASS=31 FAIL=0 CHECK=0 SKIP=3 KILL=0`（gates=34，~90s）**。
- 个人数据门（`LA_SCALE_REAL_QA` / `LA_SCALE_REAL_PERF_QA` / `LA_SCAN_SMOKE`）在无
  `LA_SCALE_REAL_DB` 时**声明式 SKIP**，个人库绝不进 CI/仓库。

## [2.24.1] - 2026-10-06

### 工具链 - pre-commit 自动门禁从「空转」变「真拦截」

- **问题**：`native/hooks/pre-commit.ps1` 在 `[Console]::IsOutputRedirected`
  （git 永远重定向 stdout）时直接 `exit 0` 跳过——三个迭代的真门禁**从未在提交时被自动跑过**，
  自动保护链是空的。注释里写的原因是「exe 自退需真实控制台，管道下会挂」。
- **取证澄清（关键）**：项目铁律 11 的「stdout 重定向不自退」只针对**直接在 Git Bash 里
  `./exe > f 2>&1`**——那种情况下 exe 的 stdout 管道无人抽取，确实不自退（0 字节假阴性）。
  但 `qa_runner.ps1` 是用 `System.Diagnostics.Process` + `RedirectStandardOutput`
  + `ReadToEnd()` 启动 exe 的，**管道始终被抽取**，与 CI 用的机制完全一致。
  实测：在 git 管道下（stdout 被重定向到文件）直接委托 `qa_runner` 跑 fast 层，
  exe **正常自退**、JSON 正确生成（`PASS=14 FAIL=0`，无 KILL）。故 hook 的 bail 是**过度保守**。
- **修法**：移除 bail，改为真正委托 `qa_runner -Tier fast -Result <TEMP json>`，
  从 JSON 读 `summary.fail` 拦截（`fail>0` → exit 1）。结果走文件、与 stdout 完全解耦，
  不受管道影响。fast 层 ~4s，覆盖回归关键门；CI push 仍跑 full 层。
- **新增过期拦截**：若 `native/build/LightAlbum.exe` 的 mtime 早于任一已暂存的 native 源文件，
  hook **直接 BLOCKED**（提示先重建）——杜绝「改了源码没编译」导致的 stale false-green。
  这是质量门禁比「温柔提醒」更有价值的防线。
- 仍保留 fail-open 兜底：工具链故障时 qa_runner 产不出 JSON（rc 异常）则**告警放行**而非锁死每个提交。
- **端到端验证（三种场景全过）**：
  - 绿灯（exe 最新，门全绿）→ `GREEN` exit 0
  - 过期 exe（源比 exe 新）→ `BLOCKED. exe is OLDER than staged source` exit 1
  - 红门（注入 `LA_RETOUCH_OP_QA_FORCE_BROKEN=1`，22 条变红）→ `BLOCKED. Red gate(s) detected` exit 1

## [2.24.0] - 2026-10-06

### 修复 - 「高光」滑杆方向与 12 算子中的唯一反向约定（用户拖右反而压暗）

- **症状**：修图实验室「高光」滑杆范围 `[-1,1]`，用户向右拖（期望**高光变亮**），
  实际却**把高光压暗**。它是 `RetouchEngine` 12 个算子里**唯一的反向约定**——
  `Pass C` 原为 `t = -p.highlights * (L-0.5) * 2.0`（正值=压暗），
  而同处阴影 `t = +p.shadows * ...`（正值=提亮）、暗角/曝光/亮度等 10 个均为正向。
- **连带缺陷**：`recommend()` 对偏亮图（avgL>0.62）写 `p.highlights -= 0.30`
  意图「压高光」，按旧语义实际是**提亮 +42.90**，与芯片文案「偏亮 · 压高光」相反。
  **为何长期未被发现**：同一批还给了 `exposure -= 0.10`，把整体亮度拉回 **-16.23**，
  看起来"正常"。**只有把算子单独施参才暴露** —— 这正是本轮门禁的核心价值。
- **修法**：统一符号约定为「正值 = 增强同名效果」（与 UI 滑杆标签直觉一致）：
  `t = +p.highlights * (L-0.5) * 2.0`。改后 `recommend()` 现有的 `-= 0.30` 恰好正确，
  **16 个预设逐一复核无需改动**（`film` -0.10 压高光 / `portrait` -0.05 人像惯例 /
  `hdr` +0.10 且 shadows -0.10 = 扩大动态范围 / `drama` 同向加强，语义均正确）。
- **取值影响**：`describe()` 走统一 `add()`，正负号与参数值天然一致，无需改动。

### 新增 - `LA_RETOUCH_OP_QA` 第 47 门：12 算子逐项方向像素断言

**取证发现的缺口**：修图已有 `LA_RETOUCH_SMOKE` / `LA_RETOUCH_A_QA` /
`LA_RETOUCH_UNDO_QA` 三条门，但它们断言的全是**几何与流程**
（旋转/方形/翻转/蒙版混合/裁剪/擦除桥/撤销栈），**12 个像素算子一个都没断言**——
甚至连 `out.width() != 0` 这种恒真式都在输出里。现状只能证明「界面能打开」，
不能证明「曝光滑杆真的改了像素」。故本次走路线图方向 B 的**门禁深化**而非新增滑杆。

**设计原则（关键）**：**每个算子单独施参 + 断言方向**，绝不用组合参数下结论——
因为单算子方向反了常被另一算子掩盖（见上述 highlights 缺陷）。

**27 项断言**：
- `identity_bit_identical`：空参必须**逐位**等同原图（非"输出非空"），
  守 `isEmpty()` 首行短路。
- 11 个算子双向方向断言：曝光/亮度（±均值亮度）、对比（±亮度标准差）、
  **清晰/锐化（±局部梯度能量 |拉普拉斯| 均值**——均值与方差都无法区分"锐化"与"整体加对比"，
  必须看局部梯度**）**、自然饱和/饱和（±饱和度，-1.0 必须≈纯灰）、
  色温（R 升 B 降 G 不变）、色调（R/B 升 G 降 = 品红，反向 = 绿）。
- **空间分布断言**（隔离"只压高光/只提阴影/只加暗角"）：高光+ 必须提亮亮块
  **且不得改动暗块**（cross-contamination，防"压暗块"冒充"提亮块"）、
  阴影+ 必须提亮暗块且不改动亮块、暗角 必须压暗四角而中心变化 ≤6。
- `recommend_highlights_chip_matches_pixels`：芯片文案「压高光」必须真压像素（锁文案与像素一致）。

**阈值标定**：全部来自 `tools/retouch_probe.cpp` 在同一张 128x96 合成图上的**实测 delta**
取安全余量（非猜非估）。探针保留在 `native/tools/` 供复标。

**门禁实测**：
- 正例 `PASS ops=12 checks=27 marks=27`（145ms 三次稳定，fast 层，预算 350ms）
- **修复前正例实测变红**且精准命中 3 条（全在 highlights 上，其余 24 条绿）
  → 证明门非空心，能区分"坏的那个"与"好的那些"
- 反例 `LA_RETOUCH_OP_QA_FORCE_BROKEN=1`（各算子接成恒等，模拟"滑杆拖了像素没动"
  这一最常见真实缺陷形态）实测 **22 条变红**，正例 27/27 绿，
  **期望值不随 broken 切换**（铁律 22）
- 全量 `PASS=32 FAIL=0 SKIP=1`；fast 层 14 门 3343ms；门表 47 门，E8 无缺 `measuredMs`

## [2.23.0] - 2026-10-06

### 新增 - `LA_SCALE_REAL_QA`：真实库规模门禁（路线图方向 D 落地）

**问题**：此前所有规模化门禁跑的都是仓库内合成 fixture（`_scale_fixture.db`，6425 行）。
取证发现 `scale_lib/` 目录在磁盘上**根本不存在**——fixture 的 6425 行声称 `missing=0`，
但**零个文件真实存在**。也就是说现有门禁只测了「内存里 6425 行」，
从未测过「数千张真实照片的解码 / 磁盘 IO / 图库首帧」。而这类退化恰恰是用户能直接感觉到的。

**正例基线（真实库 `D:/lightalbum/library.db`，6526 行，实测标定）**：

| 指标 | 实测值 |
|---|---|
| 行数 | 6526 |
| 磁盘存在率 | 99.71%（6507/6526） |
| `missing` 一致性 | 1.0000（19 行标记缺失，与磁盘缺失数完全吻合） |
| 格式多样性 | 6 种扩展名 |
| 缩略图覆盖率 | 99.54%（含 HEIC 99.7%） |
| 抽样解码 24 张缩略图 | 32 ms |

**门禁断言**：`photos≥3000` / `existRate≥0.90` / `missConsistency≥0.90`（**核心判别器**——
「库声称存在但磁盘没有」的行数占比）/ `exts≥2` / `thumbRate≥0.90` / `galleryCtrls>0` /
`firstMs` 首帧预算 / 抽样解码全部成功。阈值全部由上表实测标定，非猜测。

**为什么解码缩略图而非原图**（实测踩坑记录）：
首版按「抽样解码原始照片」实现，正例即暴露 `decodeMs=89910ms`；放宽预算后复跑更慢，
后台任务实测 `decodeMs=729399ms`（**12 分钟**），且两次运行 90s→729s 剧烈波动。
这正是项目铁律「断言墙钟预算的门即便便宜也不进 PR 层（必 flaky）」的教科书案例——
原图解码耗时高度依赖个人照片内容（12MP JPEG 与大尺寸 HEIC 差异巨大）。
改测**缩略图**（`thumbs/` 下 ~30KB/张，正是图库网格真实走的路径）后 `decodeMs=32ms`，
又快又有代表性，flaky 风险消除。

**反例**：`LA_SCALE_REAL_QA_FORCE_BROKEN=1` 把 `path` 与 `thumbFile` 同时指向不存在的沙盒目录
（精确复现 fixture「声称存在、磁盘没有」的形态，且沙盒名不含 `heic` 等被测格式子串，规避铁律10）。
实测精准命中 **4 条独立断言**：`existRate / missConsistency / exts / thumbRate` 全为 0 → 门红；
正例四项全近 1.0 → 门绿。非空心门。

**真库数据绝不进 CI**：新门沿用 `LA_SCAN_SMOKE` 的 `need` 声明式 SKIP 机制——
`need=@("LA_SCALE_REAL_DB")`，未设环境变量时运行前即判定 `SKIP`（实测
`LA_SCALE_REAL_QA [SKIP] (needs LA_SCALE_REAL_DB)`），而非落成 `CHECK` 警告态或误红。

**门表**：46 门（原 45 + 新门 1），`measuredMs=734`（full 层，实测填回）；
`gate_matrix.ps1 -SelfTest` 全绿（E8 `missingMeasuredMs=0`）。
本地全量回归 `PASS=32 FAIL=0 SKIP=1`；CI 场景 `PASS=31 FAIL=0 SKIP=2`。

## [2.22.1] - 2026-10-05

### 修复 - 用户实测报出的两个缺陷（均先取证根因、再改，后补门禁防回潮）

- **图库出现大片空白**（`EditorialView::updateWindow` 虚拟化几何未回填）：
  `updateWindow()` 原来只做两件事——回收已不可见的绑定瓦片、为**新进入**可见区的行绑定几何；
  对**仍在可见区**的已绑定瓦片直接 `else ++it` 跳过。一旦窗口尺寸/列数变化（`relayout()` 重算
  `cols_/cellW_`），这些瓦片仍带**旧几何**：小视口期绑定的迷你瓦片挤在左上角、宽视口期瓦片
  溢出右缘，中间即大片空洞。现场佐证：标题栏「显示 7 / 7 幅」但仅 3 幅可见。
  修法：`else` 分支补 `setGeometry(featureRect()/cellRect(row)) + update()`，
  使已绑定瓦片在布局变化时实时 reflow。
  **门禁**：`LA_GALLERY_E_QA` 增 reflow 断言——直接改**视图**宽度（不用 `this->resize()` 等父布局传播，
  否则父布局未下发新宽度 → 列数原地不动 → 门自造 flaky，见下）并同步 `relayout()`，
  断言「每个已绑定瓦片几何 == 当前布局」且「feature 宽确实变了」（几何事实，防空心）。
  反例 `LA_GALLERY_E_QA_FORCE_BROKEN=1` 实测变红；正例 `featW=468->570 reflowOK=1`。
  附带量化证据：反例下卡片底像素 **68018 → 11874**（空白正是"卡片像素变少"）。
- **「导入照片」下拉菜单白底白字**（`MainWindow::buildImportMenu` 弹窗未着色）：
  `QMenu` 是**独立顶层弹出窗口**，不继承应用级暗色 palette/QSS，落在系统浅色原生菜单上，
  叠加全局浅色文字 → 白底白字。修法：给该 `QMenu` 逐实例设暗色 QSS，
  配色复用已上线的暗色弹层色系（`#0e130f` 底 / `#d9e4dc` 字 / `#1d2a22` 选中底 /
  `#4fc08a` 选中字 / `#8da996` 禁用字，与 `clusterDimBox_` 一致）。
  **门禁**：① `LA_SMOKE_WINCTL` 增菜单带暗色 QSS 断言（`menuStyled=1/1`）；
  ② `LA_A11Y_CONTRAST_QA` 增菜单 3 组配色 WCAG 审计（`pairs` 15→18）。

### 修复 - 门禁自身缺陷（缺陷②排查中暴露，均有实测依据）

- **`LA_GALLERY_FILL_QA` 让"格式支持"给"绘制逻辑"背黑锅**：原实现取「真库首张**磁盘上存在**的照片」，
  恰好取到 HEIC（Qt 解不出，2.22.0 已实测 `decoded=NULL`）→ `loaded=0` 门红，
  与本门要验的"照片铺满卡片"毫无关系；且成败取决于用户库里恰好排第一的是哪张 → 天然 flaky。
  改为循环取第一张**真正 `load()` 成功**的照片（一张都解不出才判失败，不许空数据蒙混过关）。
- **同一门禁的"卡片底"探测器假阳**：原判定 `|c-(19,26,21)|<12`，把**底部渐隐遮罩下的暗部照片像素**
  误判成黑边。实测底行取值 `(29,29,22)/(27,27,21)/(25,26,22)/(23,25,21)` 各不相同 —— 是照片不是黑边，
  却全落进 ±12 → 假阳 3.678%/4.497%。卡片底是**纯色** `(19,26,21)`，按"近纯色"判定即可：
  实测暗部照片至少一通道偏离 ≥4、真卡片底偏离 0，容差取 **2** 卡在中间（两侧留余量）。
  标定实测：tol=12→3.678%/4.497%（假阳）、tol=6→1.483%/1.920%、tol=2→**0.082%/0.191%**、
  tol=0→0.058%/0.160%（仅 92px 圆角抗锯齿，与卡尺寸无关的常数）。
  **检出力未削弱**（合成图反验：底部 6px 真黑边 → 2.978% 门红 / 无黑边 → 0.000% 门绿）。

## [2.22.0] - 2026-10-05

### 新增 - 客户端体验感（方向：灯箱原图 + 缩略图骨架屏 + 动画纯函数）

以「先测再改」取证，确定真实体验缺口（启动性能**不是**短板：实测首帧 55ms / 滚动 5ms，
排除懒加载方向）。三项改动：

- **灯箱看原图**（最大价值）：灯箱此前显示的是 `thumbFile`（最大 512px 缩略图），放大也看不清细节；
  `Photo.path` 原图绝对路径一直在库里却未被使用。新增 `FullCache`：先秒出缩略图（不阻塞）→
  后台线程解码原图 → 淡入 120ms 替换；失败（`Photo.path` 失效 / HEIC 不支持）诚实降级回缩略图绝不空白。
  **实测依据驱动设计**（Qt 探针，12 张真实库样本）：解码耗时中位 46ms / 最大 175ms（→ 必须异步）；
  解码后内存 7.9~70 MB/张（→ 按字节预算 LRU，默认 140MB ≈ 3 张，复用 `ThumbCache` 已验证范式，
  其注释已踩过「按张数计费 → 1.98 GiB」坑）；HEIC 3 张里 2 张 `decoded=NULL`（→ 失败降级是必需路径）；
  加 40MP 解码炸弹防护（实测最大样本 12.2MP）。
- **缩略图骨架屏**：`GalleryCardPainter` 此前 `isNull` 就什么都不画 → 灰底空卡片。新增可断言
  状态机 `SkeletonState{Ready,Loading,Missing}` + 纯函数 `skeletonPulse(phase,reduceMotion)`（铁律 20：
  产品绘制与门禁断言共用同一实现，杜绝假绿）；脉冲计时器**仅在可见区有未就绪卡片时挂载**，就绪后自停
  （不常驻空转）；`reduceMotion` 呈终态不闪。
- **动画纯函数契约**（补铁律 20 缺口）：导航转场 `onNav` 早已实现（淡出 150 + 上滑 260 + 淡入，
  `animEnabled_` 已是减动效守卫），缺口仅在其几何无可断言实现；本轮把骨架脉冲抽成纯函数并纳入门禁。
- **第 45 门 `LA_TRANSITION_QA`**（full 层，measuredMs=400）：3 线断言 —— A 动画纯函数契约
  （值域/端点/单调/减动效终态/周期）、B 骨架屏像素判别（Loading 与 Ready 渲染必不同、不同相位必不同、
  减动效帧必一致）、C 灯箱原图（FullCache 预算>0 + 真解码 1600×1200 其边长**必须 >512 缩略图档位**，
  该阈值是判别器）。**反例非空心**：`LA_TRANSITION_QA_FORCE_BROKEN=1` 经 `setForceFailForQa` 让真实
  解码恒失败 → 同一批 C 断言转红（期望值永不随 broken 切换，铁律 22）。
- **★关键回归修复**：`FullCache` 析构原未等后台线程池排空，进程 `return 0` 后 worker 仍跑 →
  回调打到已析构对象 → `LA_MAP_CLICK_QA` 退出期间歇性 0xC0000005（断言本身全绿，纯崩溃）。
  修复：析构先 `pending_/callbacks_` 清空再 `QThreadPool::globalInstance()->waitForDone()`，
  并改为由 `MainWindow` 持有注入灯箱（灯箱惰性创建致构造/析构时机与 QObject 父子序冲突）。修复后该门
  连跑 6 次稳定 rc=0。

## [2.21.0] - 2026-10-05

### 新增 - release.ps1 首次端到端实跑 + 两条真实缺陷修复（方向 E 第四步）

`release.ps1` 自 2.5.0 起只跑过 `-SelfTest`（沙盒契约自检），**从未真实端到端执行过**。本轮按 §8.4 准入清单（无长期红门 → 查已知债务）实跑，暴露并修复两条**只有在真实产物上才会出现**的缺陷。

**★取证（先测再改）**：首次真实执行 `release.ps1 -SkipGate` 即跑通（EXITCODE=0，11.7s，28.3 MB zip），产物本体健康——zip 31 条目 `testzip` 无损、MANIFEST 的 exe sha256 与字节数**逐字节匹配**真实文件、C1/C3/C4/C5/C6/C7 契约在真实产物上确实成立。但输出里两处信号指向真实缺陷。

### 修复 1 - 过期手册被静默 ship（C2 契约升级为版本一致性）

**现象**：产物内 `USER-MANUAL.md` 是 **2.0.0 版**（7945 字节，2026-09-28），落后当前 20 个版本，且内容自相矛盾——声明「发行物为 `LightAlbum-2.0.0-win64.zip`（约 16.6 MB）」，而实际交付 `LightAlbum-2.20.0-win64.zip` 28.3 MB；2.18/2.19/2.20 的 AI 分类、人脸识别、人物聚类、高 DPI、双语、无障碍**在手册里完全缺失**。

**根因（两层，皆为「保证成功」的陷阱）**：
1. `Assert-ManualPresent` 只断言「存在且非空」，**不校验内容对应当前版本**；
2. 更关键的是手册匹配的 fallback——「找不到 `LightAlbum-$Version-*.md` 就退回任意 `LightAlbum-*-*.md`」——**使打包在此处永不失败**，代价是把过期手册静默 ship 出去。**一个永不失败的契约等于没有契约。**

**修复**：
- C2 升级为「存在 + 非空 + **描述的版本与所打包版本一致**」：扫描手册内所有 `LightAlbum <x.y.z>` 形态，出现与 `$Version` 不同的版本即抛错并列出全部冲突版本；手册完全不提当前版本也抛错（无法确认即不可信）。
- 移除 fallback，改为**硬失败**并指名 `docs/` 实际有什么、缺什么。docs 目录不存在同样硬失败。
- `-SelfTest` 沙箱基线手册改为提及当前版本（否则基线会因错误原因变红），并**新增一条过期内容注入**：把手册写成描述 2.0.0 而打包 9.9.9 → 精准捕获。**删除文件的旧注入无法证明版本断言有效，只有过期内容能。**

**★反例非空心（实测打穿）**：临时摘除 C2 的内容断言后重跑 `-SelfTest` → `FAIL reason=inject:C2 defect was NOT caught`，EXITCODE=1；恢复后 `PASS contracts=8 injected=13 caught=13`。同一断言正例实测为绿，故非空心。

**新手册**：`docs/LightAlbum-2.21.0-使用说明书.md`（18 章 207 行），补齐 2.0.0→2.21.0 全部能力（相册聚类维度、相似照片去重、AI 分类、识别人物、智能聚类、隐私库、中英双语、无障碍键盘、高 DPI、iCloud 导入、12 滑杆、撤销栈、轨迹/热力/按年筛选），并新增**附录 A 校验与复现**（教用户用 `MANIFEST.txt` 逐文件 `Get-FileHash` 复核、用 `-SelfTest` 验契约）。手册所有事实性声明均**对照代码核实**（12 滑杆 ↔ 迭代指导书 §8.2 方向 B 取证、端口 8766/8767 ↔ `MainWindow.cpp:1284/1342/1402`、HEIC 可解 ↔ 2.6.0 实测）。

### 修复 2 - 发行包混装 Debug exe（新增 C8 契约）

**现象**：SBOM 诚实记录 `build_type: Debug` 但无人注意。`windeployqt --release` 部署的是 **Release Qt DLL**，而 exe 本身是 **Debug 构建**（47 MB，未优化、断言与调试代码仍在）——**发行包是 Debug exe + Release DLL 的混装产物**。

**根因**：`CMakeLists.txt` 从未设置 `CMAKE_BUILD_TYPE`。单配置生成器下该变量为空串，而空串会让 gcc **不带任何优化/DNDEBUG 标志**；本地 build 目录此前一直停在 Debug。这条缺陷能潜伏至今，正是因为 C1–C7 七条契约**无一条覆盖构建类型**——契约设计是「有即测」，此处是「无即漏」。

**修复**：
- **C8 契约**：`Assert-ReleaseBuildType` 硬性要求 `CMAKE_BUILD_TYPE=Release`；`Debug`/未设置/CMakeCache 缺失**一律拒绝**（「无法证明是 Release」不得读作「没问题」）。生产路径在 C3 之后立即断言并打印 `C8 ok: build type is Release`。
- `CMakeLists.txt` 新增：未显式指定时默认 `Release`（多配置生成器不设），显式 `-DCMAKE_BUILD_TYPE=` 仍可覆盖（本地调试用 Debug）。
- 两条反例注入：`Debug` 值 + `CMAKE_BUILD_TYPE` 未设置。

**★实测收益**：exe **47 MB → 3.4 MB**（13.8 倍瘦身），zip **28.3 MB → 17.4 MB**。此前每个发行包都在白送 44 MB 未优化代码。

**★反例非空心（实测打穿）**：把断言中和为 `if ($false)` 后重跑 → `FAIL reason=inject:C8 defect was NOT caught`，EXITCODE=1。

### 修复 3 - 契约自测报告的数字是硬编码的

`LA_RELEASE_QA PASS contracts=7 injected=10 ... baseline=7` 里的 7/10/7 全是**写死的字面量**（只有 `caught` 是真实的）。加了 C8 与第 13 个注入后，报告仍宣称「7 条契约 / 10 注入」——**一份数字不由计算得来的报告，可以静默地与它所报告的内容不一致**，这正是本文件存在的目的所反对的同类缺陷。

修复：新增 `$contractIds`（契约名册唯一真源），`Get-ContractTags` 探针、基线循环、PASS 报告三处全部由它派生。新增 C8 时只需在名册加一行，报告自动正确。现输出 `contracts=8 injected=13 caught=13 baseline=8`（`caught == injected` 是关键：无一漏网）。

### QA（Release 构建下全量回归）

- **构建类型切换后必须重跑全量**：Release 下 `SUMMARY: PASS=43 FAIL=0 CHECK=0 SKIP=1 KILL=0`（44 门并集），墙钟 **168793ms → 86423ms（近半）**——Release 优化对门禁速度亦有实益。
- **抽查 Release 下新门正反例仍有效**：`LA_FACE_CLUSTER_QA` 正例 rc=0、`FORCE_BROKEN` 反例 rc=1（断言未被 NDEBUG 优化掉）。
- **独立复核产物**：zip `testzip` 无损、MANIFEST sha256 + 字节数与真实 exe 逐字节匹配、SBOM 溯源 `git_commit` / `build_type: Release` / `qt_version: 6.8.3` / `compiler_version: MinGW 13.1.0`。
- `LA_RELEASE_QA PASS contracts=8 injected=13 caught=13 baseline=8`（EXITCODE=0）。
- `release.ps1` 端到端复跑 `RELEASE OK`、**零 WARN**（旧版 `WARN: no manual for 2.20.0; falling back to...` 已消失）。

### 文档台账校准（防下次迭代被过时描述带偏）

§8.2 两条描述经 `grep` 核实**已过时**，本轮一并校准：
- ~~清偿 GeoIndex 中文省名债务~~ → **✅ 已于 2.2.2 清偿**（`verifyChinaProvinces()` + 34 省精确名 + `LA_GEO_SMOKE` 断言，见 §7 台账 `P-geo-prov-cn`）。
- 方向 D 的「缩略图多级缓存、列表虚拟化、批量操作事务化」→ **三项均已完成**（2.13.0 / `EditorialView` / `LA_BATCH_TX_QA`）；真正未落地的是「真实库规模门禁」。
- **教训：§8.2 的「待办」会随迭代腐化，选方向前必须先 `grep` 核实是否已被后续迭代清偿**——本轮就差点被 GeoIndex 那条带偏。

### 过程
- `release.ps1`（纯 ASCII 保持，规则 13）：`Assert-ManualPresent` 加 `$expectVersion` 参数 + 内容版本一致性断言；`Assert-ReleaseBuildType` 新增（C8）；手册匹配移除 fallback 改硬失败；`-SelfTest` 沙箱加 `CMakeCache.txt` 存根 + C2 过期内容注入 + C8 两条注入；`$contractIds` 契约名册；`Get-ContractTags` 加 `$cachePath`；生产路径接入 C2 版本实参与 C8 断言。
- `CMakeLists.txt`：`VERSION 2.20.0 → 2.21.0`；新增未指定时默认 `CMAKE_BUILD_TYPE=Release`。
- `docs/LightAlbum-2.21.0-使用说明书.md`：新建（替代 2.0.0 版），18 章 + 校验复现附录。
- `docs/迭代指导书.md`：§7 台账新增 `E-release-end2end` 行；§8.2 方向 E 标记真实闭环 + 两条过时描述校准。

### 范围裁定
本步只交付**发布链端到端验证 + 真实暴露缺陷的修复**；不涉及安装包签名与自动更新通道（仍留作方向 E 后续候选）。

## [2.20.0] - 2026-10-05

### 新增 - 人物自动聚类（方向 C 第五步）

2.19.0 交付「检测标注 + 用户重命名」，但所有检测到人脸共享默认标签「人物1」——**多人合影无法区分**。本步把 2.19.0 已回传却丢弃的 512-d ArcFace `embedding` 用起来，实现**阈值余弦聚类**自动区分不同人物，用户随后仍可在人物 chips 右键改名。

**★三项裁定（用户对齐）**：① 方向 = **人物自动聚类**（补齐 2.19.0 范围裁定留下的缺口）；② 算法 = **阈值余弦聚类**（贪心单连锁，代表脸 = 群首，阈值 0.4）；③ embedding 存储 = **落库 `face_emb` BLOB**（512 × float32 小端 = 2048 字节）。

**取证依据**：`PersonDetector::detect` 在 2.19.0 只取 `faces.size()`、**丢弃 `embedding`**（原第 121 行 `batch.append({p->path, defaultPerson()})`），证明数据基础已具备、只是未利用。

**★schema 迁移（P16 风格）**：`photos` 表新增 `face_emb BLOB`（`ALTER TABLE ... ADD COLUMN`，重复 ADD 报错无害静默忽略）。**embedding 不进内存 `Photo` 副本**（512×4 字节 × 数千张会撑爆内存），按需经 `getFaceEmbeddings()` 从只读主连接读盘。

**core 纯函数 `FaceCluster.{h,cpp}`**（对齐 `PhotoCluster` 范式）：`cluster(items, threshold=0.4, startIndex=1)` 贪心单连锁——按序遍历，对每个 embedding 找「代表脸余弦 >= 阈值」的已有簇，命中并入否则新建；`cosine(a,b)` 两长度一致 float32 BLOB 点积/模；label = `人物N`。**★守恒契约（铁律 6）**：每个输入 path 恰出现一次、`outputCount == inputCount`、无重复无遗漏（`ClusterResult` 显式携带 `inputCount`/`outputCount` 供门断言）。`setForceBrokenForQa` 反例钩子。

**core 桥扩展 `PersonDetector`**：`detectEmbeddings(lib,url,limit,progress,cancel)`——遍历 `person` 为空照片 `POST /face/detect`，**多脸取代表脸**（`score` 最大者，对齐 per-photo 单标签模型）→ `toEmbBlob` 转 float32 小端 BLOB → 整批 `lib->setFaceEmbeddings(batch)`。复用既有 `isLocalUrl` 回环守卫 + `waitReply`；`setSyntheticEmbForQa`/`clearSyntheticEmbForQa` 合成钩子绕过网络。

**★写回 API + 守恒契约**：`setFaceEmbeddings` 单事务 `UPDATE photos SET face_emb=? WHERE path=?`（只改 `face_emb` 列，不进内存）；`applyClusters` 单事务 `UPDATE photos SET person=? WHERE path=? AND (person IS NULL OR person='')`——**WHERE 条件保证不覆盖非空**（用户已重命名的照片绝不被聚类结果污染），内存同步同样只对 `person` 当前为空者生效（与 DB 语义严格一致），复用 `g_forcePersonNoSync` 反例钩子；`getFaceEmbeddings()` 读「person 为空且有 face_emb」的行（缺失 BLOB 跳过）。任一条失败整体 `rollback`。

**UI（视图不持写能力，铁律 5）**：GalleryPage 聚类条新增「智能聚类」按钮 → 只发 `faceClusterRequested()` signal；`MainWindow::runFaceCluster` 镜像 `runPersonDetect`——`probe` / 提示启动 `face_server:8767` / pending 统计 / `QProgressDialog` + `QFutureWatcher<bool>` + `std::atomic<bool>` 取消 + `QtConcurrent::run`，后台链路 `detectEmbeddings` → `getFaceEmbeddings` → `FaceCluster::cluster` → `applyClusters`，完成 `gallery_->refresh()`。**★起始编号防撞号**：扫描既有 `person` token 里的「人物N」取最大 N，`startIndex=N+1`，避免聚类产生的「人物1」与用户已改名的既有标签撞号（撞号会把两个人的照片混进同一 chips）。

### 新增 - LA_FACE_CLUSTER_QA（第 44 门，full 层）

不依赖外部 face_server：用 `PersonDetector::setSyntheticEmbForQa` 注入 6 条确定 embedding（**同一张脸 3 张** + **互不相同的 3 张**），走完「抽 embedding 落库 → 阈值余弦聚类 → applyClusters 写回」全链路。纯离屏自退，不构造 `MainWindow`。断言覆盖：

- **A embedding 落库往返**：`getFaceEmbeddings()` 读回 6 条、字节数 == 512×4。
- **B ★守恒（核心契约）**：`inputCount == outputCount == 6`，每 path 恰一次、无重复。
- **C 分组形状**：4 簇（1 个 3 成员主簇 + 3 个单成员），主簇 label == `人物1`。
- **D DB 写回**：`photos.person` 已写入（非空）。
- **E 内存同步**：`Photo.person` 与 DB 一致（GalleryModel 读内存）。
- **F 检索契约**：`rowCount(人物1)==3`、`rowCount(全部)==6`。
- **G ★无回归**：`renamePerson` 改名后检索仍正确（2.19.0 能力未被聚类写回破坏）。

**反例（非空心）**：`LA_FACE_CLUSTER_QA_FORCE_BROKEN=1` 经 `FaceCluster::setForceBrokenForQa(true)` 让**真实实现**丢一张 → 主断言 B 精准转红，实测 `FAIL ... reason=BROKEN conserve outputCount=5 inputCount=6;conserve listed=5 input=6`。**同一断言在正例实测为绿**（`conserve=1`），故断言必然有效、绝非空心；反例另签名化断言「照片总数仍 == 6」，证明**只坏守恒不坏其它**。

**★分层实测决定（规则 13）**：离屏实测 **215/219/217ms**（三次）。虽在 500ms 快门预算内，但 E6 只对 fast 层**上界**约束、把便宜门留在 strict 层永远合法，且与同族 `LA_PERSON_QA`（500ms）、`LA_CLASSIFY_QA`（1600ms）一致 → 入 **full 层**，`measuredMs=220`（记录实测值而非猜测值）。`release.ps1` 门禁走并集（44 门全跑），打包强度未削弱。

### 修复 / 过程
- **新增** `src/core/FaceCluster.{h,cpp}`：`cluster` / `cosine` / `setForceBrokenForQa`；`ClusterInput`/`ClusterGroup`/`ClusterResult` 结构（`ClusterResult` 显式携带守恒计数）。
- `src/core/PersonDetector.{h,cpp}`：新增 `detectEmbeddings` / `setSyntheticEmbForQa` / `clearSyntheticEmbForQa` / `toEmbBlob`；`detect` 行为不变。
- `src/core/Library.{h,cpp}`：新增 `FaceItem`/`ClusterItem` 结构；`setFaceEmbeddings` / `applyClusters` / `getFaceEmbeddings`；`face_emb BLOB` 列迁移。
- `src/ui/GalleryPage.{h,cpp}`：新增 `faceClusterRequested` signal、「智能聚类」按钮、`qaFireFaceCluster`。
- `src/ui/MainWindow.{h,cpp}`：新增 `runFaceCluster` + 接线 + `#include "../core/FaceCluster.h"`。
- `src/main.cpp`：新增 `runFaceClusterQa()` + `LA_FACE_CLUSTER_QA` 派发 + `core/FaceCluster.h` 引入。
- `gate_matrix.ps1`：新增第 44 门（纯 ASCII 注释，规则 13）。
- `CMakeLists.txt`：`VERSION 2.19.0 → 2.20.0`，登记 `FaceCluster.h/.cpp`。

### QA
- 新门正例 `LA_FACE_CLUSTER_QA PASS rows=6 broken=0 emb=1 conserve=1 group=1 write=1 sync=1 retrieve=1 rename=1`（rc=0）。
- 新门反例 `LA_FACE_CLUSTER_QA_FORCE_BROKEN=1` → `FAIL ... conserve outputCount=5 inputCount=6`（rc=1）。
- **★踩坑取证（`QByteArray::resize()` 不零初始化）**：首轮正例即 RED（`groups=5 want=4; biggest=2 want=3`）。加临时诊断打印字节头定位到根因——`unitVec` 用 `b.resize(kDim*4)` 后只写 1 个分量，**其余 511 个 float 是未初始化堆垃圾**（实测 `nonzero=492`、字节头为 `...indexix_pho`），导致 `cosine` 溢出为 `NaN`。修复：改用 `QByteArray b(kDim*4, '\0')` 显式零初始化；生产侧 `toEmbBlob` 同步加固（每元素虽都覆写，仍显式零初始化以防 `arr.size()` 与写入数不一致留下脏字节）。**该坑对任何「resize + 部分写入 + 当数值用」的场景都成立，已写入铁律 33。**
- `LA_GATE_MATRIX_QA PASS gates=44 fast=13 full=31 errs=0`，E1–E10 全绿。
- **全量回归 `qa_runner.ps1`（44 门并集）：`SUMMARY: PASS=43 FAIL=0 CHECK=0 SKIP=1 KILL=0`**，总墙钟 168793ms（SKIP = `LA_SCAN_SMOKE` 需外部 `LA_SCAN_ROOT/LA_SCAN_DB`，非回归）。
- 隔离复跑 `LA_PERSON_QA` / `LA_CLASSIFY_QA` / `LA_DEDUP_QA` / `LA_I18N_QA` / `LA_BATCH_TX_QA` 全绿，2.19.0/2.18.0 能力无回归。

## [2.19.0] - 2026-10-05

### 新增 - 人脸识别标注 + 人物重命名（方向 C 第四步）

2.18.0 把「人脸识别（人物聚类）留作后续候选，不在此迭代编造」。本步把人物能力落地为**检测标注 + 用户重命名**两个真实可交付项，**不引入聚类算法**（与方案裁定一致：用户要把默认标签「人物1」改成真名，而非自动聚类归并）。

**★方案裁定（方案A + 重命名 UI）**：接本地 `face_server:8767`（onnxruntime CPU，SCRFD-10GF 检测 + ArcFace w600k_r50 识别），检测人脸写 `Photo.person`；并在 `GalleryPage` 人物 chips 提供右键重命名入口（用户把「人物1」改成真名）。**★范围裁定**：本步只交付「人脸检测标注 + 重命名」，人物自动聚类仍不编造。

**face_server 协议**（对齐 `Classifier` 桥范式）：`GET /health` → `{ok,loaded,det,rec,device}`（仅回环 127.0.0.1/localhost/::1）；`POST /face/detect` 收 `{"path":"<本地绝对路径>"}`（**JSON 而非 multipart**），返回 `{ok,count,faces:[{bbox,score,embedding,kps}]}`；无脸 → `faces:[]` 绝不抛错。

**core 桥 `PersonDetector.{h,cpp}`**（对齐 `Classifier`）：`probe(url)` 回环守卫（`isLocalUrl`）+ `/health` 验 `loaded`；`detect(lib,url,limit,progress,cancel)`：① QA 确定路径（`g_synthetic` 注入 → 直接 `lib->setPersons` 返回）；② 真路径先 `probe` 失败即 false；③ 遍历 `lib->photos()` 仅取 `p.person.isEmpty()` 的待检测（**避免覆盖用户重命名**）；④ 逐张 `POST` JSON `{"path":p->path}` 到 `/face/detect`，`faces` 非空则 `batch.append({p->path, defaultPerson()})`；⑤ 整批 `lib->setPersons(batch)`。`defaultPerson()` 返回 `"人物1"`；`setSyntheticForQa`/`clearSyntheticForQa` 合成 QA 钩子绕过网络。

**★写回 API + 守恒契约（铁律 6）**：`Library::setPersons(const QVector<PersonItem>&)` 单事务 `UPDATE photos SET person=? WHERE path=?`，`COMMIT` 成功后才同步内存（经 `pathIx_` O(1)），失败即 `rollback`；`renamePerson(oldName,newName)` 对 `p.person.split(',', SkipEmptyParts)` 做 token 级精确匹配 `tt==oldName` → 替换为 `newName`（空 `newName` = 删除该 token），单事务写回 + 内存同步；`oldName` 空或全库无匹配返回 `true`（幂等）。`static setForcePersonNoSyncForQa` 反例钩子（`g_forcePersonNoSync`）控制「写库但跳过内存同步」。

**UI（视图不持写能力，铁律 5）**：GalleryPage 聚类条新增「识别人物」按钮 → 只发 `personDetectRequested()` signal；人物 chips 右键 `QInputDialog::getText` → 只发 `renamePersonRequested(old,new)` signal（空/未变忽略）；`MainWindow::runPersonDetect` 镜像 `runClassify`——`probe("http://127.0.0.1:8767")`，统计 `p.person.isEmpty()` 的 pending，`QProgressDialog` + `QFutureWatcher<bool>` + `std::atomic<bool>` 取消 + `QtConcurrent::run`，完成后 `gallery_->refresh()` + 提示「已为 %1 张照片标注人物。」。

### 新增 - LA_PERSON_QA（第 43 门，full 层）

不依赖外部 face_server：用 `PersonDetector::setSyntheticForQa` 注入确定结果，测「写回 + 检索 + 重命名契约」。纯离屏自退，不构造 `MainWindow`。断言覆盖：

- **A 索引自检**：`verifyPathIndex()` 通过。
- **B ★守恒**：`setPersons` 不增删照片（总数 == 6）。
- **C DB 写回**：`p0/p1/p2` 落 `人物1`、`p3/p4/p5` 保持无人物（`dbPerson` 读取）。
- **D 内存同步**：`Photo.person` 与 DB 一致（GalleryModel 读内存）。
- **E 检索契约**：`GalleryModel::setPersonFilter("人物1").rowCount()==3`、过滤「张三」=0、空=6。
- **F defaultPerson**：`defaultPerson()=="人物1"`。
- **G ★重命名契约**：`renamePerson("人物1","张三")` 后 DB / 内存 / 检索全部切换（rowCount("张三")==3）。

**反例（非空心）**：`LA_PERSON_QA_FORCE_BROKEN=1` 经 `Library::setForcePersonNoSyncForQa(true)` 让**真实实现**写库但跳过内存同步 → 主断言 ④(memPerson 未同步) / ⑤(按人物 rowCount 失效) 精准转红（实测 `reason=mem p0=;rowCount(人物1)=0 want=3`，且 `dbPerson(p0)=="人物1"` 证明**只坏同步不坏写回**）。反例注入在真实实现上，断言必然有效。

**★分层实测决定（规则 13）**：实测 **496ms**，紧贴 500ms 快门预算（E6）边界。类比 `LA_CLASSIFY_QA`（1600ms）与 `LA_CLUSTER_QA`（1424ms），**保留 full 层**（不挤占 PR 快门，避免偶发 flaky）；`measuredMs=500`。`release.ps1` 门禁走并集（43 门全跑），打包强度未削弱。`LA_GATE_MATRIX_QA` E6/E8 实测校验通过。

### 修复 / 过程
- **新增** `src/core/PersonDetector.{h,cpp}`：`probe` / `detect` / `defaultPerson` / `setSyntheticForQa` / `clearSyntheticForQa`；`g_synthetic` 静态钩子；`isLocalUrl` 回环守卫（同 `Classifier`）；`waitReply` 同步超时。
- `src/core/Library.{h,cpp}`：新增 `PersonItem` 结构；`setPersons` 单事务写回 + 内存同步；`renamePerson` token 级精确匹配 + 单事务写回；`static setForcePersonNoSyncForQa`（`g_forcePersonNoSync`）。
- `src/ui/GalleryPage.{h,cpp}`：聚类条「识别人物」按钮 → `personDetectRequested()`；人物 chips 右键 `QInputDialog` → `renamePersonRequested()`；`qaFirePersonDetect()`。
- `src/ui/MainWindow.{h,cpp}`：`runPersonDetect()`（探测 / 提示启动 `face_server` / `QProgressDialog`+`QFutureWatcher`+`std::atomic` 取消 / 完成刷新）；`#include "../core/PersonDetector.h"`；接线 `personDetectRequested` / `renamePersonRequested`。
- `src/main.cpp`：`runPersonQa()`（A–G 七类断言 + 反例）+ `LA_PERSON_QA` 派发（紧接 `LA_CLASSIFY_QA` 之后）+ `#include "core/PersonDetector.h"`。
- `gate_matrix.ps1`：新增第 43 门（full 层，`measuredMs=500`）。
- `CMakeLists.txt`：`VERSION 2.18.0 → 2.19.0`；登记 `PersonDetector.{h,cpp}`（依赖既有 `Qt6::Network`）。
- i18n：新增 UI 串（识别人物 / 识别人脸服务未就绪 等）以 `translate("LightAlbum", "…")` 字面量直调（铁律 15/16），未跑 `lupdate`（手工直加，沿用既有 `.qm` 嵌入机制）。

### 门数
43 门（新增 1）。fast 层 13 门不变，full 层 29 → 30 门。

## [2.18.0] - 2026-10-05

### 新增 - 智能场景分类检索（方向 C 第三步）

图库此前只能按「分类 / 人物 / 聚类 / 大区 / 省 / 搜索串」浏览，但**分类本身要靠人工选**；本步接入本地 CLIP 场景模型服务，对未分类照片批量推断场景类别并**写回数据库 + 内存**，之后即可用既有的分类筛选做「智能检索」。

**★范围裁定（不编造）**：CLIP 服务仅接**场景模型** → 本步只交付「场景分类 + 按场景检索」。人脸 / 人物识别作为文档化后续（方向 A 已闭环足迹/热力，人物聚类需另行立项），**不在此迭代内伪造**。服务协议对齐历史 `client/lightalbum/core/classifier.py`：`GET {url}/health` 回 `{ok,loaded}`（仅回环 127.0.0.1/localhost/::1）；`POST {url}/classify` 多部件字段名 `files`，返回 `{ok,results:[{i,label,score}],bad:[...]}`（`i`=批内下标）；批量 `BATCH=32`；**只读已存在缩略图、绝不碰原图**。

**core 桥 `Classifier.{h,cpp}`**：`probe(url)` 先回环校验 + `/health` 验 `loaded`；`classify(lib,thumbsDir,url,batch,limit,progress,cancel)` 取「未分类且有缩略图」照片 → `QHttpMultiPart` 批投 `/classify` → 按 `results[i]` 映射 `normalizeLabel` 后 `lib->setCategories` 写回。`normalizeLabel` 把任意原始标签收敛到 8 个固定场景类（人像 / 风光 / 夜景·星空 / 城市·建筑 / 微距·花卉 / 动物 / 美食 / 黑白）+ 英文同义词映射 + 中文子串兜底 + 未知→`其他`。**QA 合成钩子** `setSyntheticForQa` / `clearSyntheticForQa`：门禁注入确定性结果，绕过网络。

**★写回 API + 守恒契约（铁律 6：契约既执行又被测试）**：`Library::setCategories(const QVector<CatItem>&)` 走**单事务原子写回**（`db.transaction()`→逐行 `exec`→`commit()` 成功后才同步内存副本，`findMutable` 经 `pathIx_` O(1)），失败即 `rollback` 返回 `done=0`，杜绝 DB↔内存撕裂。这是与去重/聚类一致的「守恒」不变量：每张照片恰出现一次、分类字段原地更新。

**UI（视图不持写能力，铁律 5）**：GalleryPage 聚类条新增「AI 分类」按钮 → 只发 `classifyRequested()` 信号；MainWindow 接信号跑 `runClassify()`：服务不可用时 `QMessageBox` 提示启动 `D:\lightalbum_ai\start_classifier.bat`；`pending==0` 提示无需跑；否则 `QProgressDialog` + `QFutureWatcher<bool>` + `std::atomic<bool>` 取消 + `QtConcurrent::run` 异步，完成后 `gallery_->refresh()` + 信息框。分类写回后用户即可用既有分类筛选做智能检索。

### 新增 - LA_CLASSIFY_QA（第 42 门，full 层）

不依赖外部 CLIP 服务：用 `Classifier::setSyntheticForQa` 注入确定结果，测「写回 + 检索契约」。纯离屏自退，不构造 `MainWindow`。断言覆盖：

- **A 索引自检**：`verifyPathIndex()` 通过。
- **B ★守恒**：`setCategories` 不增删照片（总数 == 6）。
- **C DB 写回**：`风光`/`人像` 落盘正确、`p4/p5` 保持未分类。
- **D 内存同步**：`Photo.category` 与 DB 一致（GalleryModel 读内存）。
- **E 检索契约**：`GalleryModel::rowCount` 按 `风光`=3 / `人像`=1 / `kNoCategory`=2 / 全部=6 全对。
- **F normalizeLabel 确定性**：`portrait→人像` / `landscape→风光` / `夜景→夜景·星空` / 未知→`其他`。

**反例（非空心）**：`LA_CLASSIFY_QA_FORCE_BROKEN=1` 经 `Library::setForceCategoryNoSyncForQa(true)` 让**真实实现**写库但跳过内存同步 → 主断言 ④(memCat 未同步) / ⑤(按类 rowCount 失效) 精准转红（实测 `reason=mem p0=;mem p3=;rowCount(风光)=0 want=3;...`），证明断言非恒真。反例注入在真实实现上，断言必然有效。

**★分层实测决定（规则 13）**：实测 **1161–1607ms**，超过 500ms 快门预算（E6）→ 归入 **full 层**（类比 `LA_CLUSTER_QA` 1424ms 同判），`measuredMs=1600`。`release.ps1` 门禁走并集（42 门全跑），打包强度未削弱。`LA_GATE_MATRIX_QA` E6/E8 实测校验通过。

### 修复 / 过程
- **新增** `src/core/Classifier.{h,cpp}`：`probe` / `classify` / `normalizeLabel` / `setSyntheticForQa` / `clearSyntheticForQa`；`waitReply` 同步等 reply；仅回环防护。
- **修复（铁律·Qt 生命周期）**：`classify` 真路径 `QHttpMultiPart` 由**栈对象改为堆对象**（`new` + `mgr.post(req, multi)` 由 reply 接管销毁时自动 `delete`）；`paths` 为空分支（未发请求）手动 `delete multi` 防泄漏。栈对象在异步路径会悬垂（UB）。
- `src/core/Library.{h,cpp}`：新增 `CatItem` 结构；`setCategories` 单事务写回 + 内存同步；`static setForceCategoryNoSyncForQa` 反例钩子（`g_forceCategoryNoSync`）；`findMutable` 经 `pathIx_` O(1)。
- `src/ui/GalleryPage.{h,cpp}`：聚类条「AI 分类」按钮 → `classifyRequested()`；`qaFireClassify()`。
- `src/ui/MainWindow.{h,cpp}`：`runClassify()`（探测 / 提示启动 bat / `QProgressDialog`+`QFutureWatcher`+`std::atomic` 取消 / 完成刷新）；`#include "../core/Classifier.h"`。
- `src/main.cpp`：`runClassifyQa()`（A–F 六类断言 + 反例）+ `LA_CLASSIFY_QA` 派发 + `#include "core/Classifier.h"`。
- `gate_matrix.ps1`：新增第 42 门（full 层，`measuredMs=1600`）。
- `CMakeLists.txt`：`VERSION 2.17.0 → 2.18.0`；登记 `Classifier.{h,cpp}`（依赖既有 `Qt6::Network`）。
- i18n：新增 UI 串（AI 分类 / AI 分类服务未就绪 等）以 `translate("LightAlbum", "…")` 字面量直调（铁律 15/16），未跑 `lupdate`（手工直加，沿用既有 `.qm` 嵌入机制）。

### 门数
42 门（新增 1）。fast 层 13 门不变，full 层 28 → 29 门。

## [2.17.0] - 2026-10-05

### 新增 - 相似照片去重（方向 C 第二步）

图库此前已有 pHash 去重能力（藏在 `CullEngine` 的 CullPage 对决模式里），但**不可图库级发现、无独立入口、无 QA 门禁保护、无可逆标记**。本步补齐「图库级可发现 + 独立入口 + 门禁保护 + 可逆标记」，**复用既有算法而非重写**。

**★复用算法，零重写去重逻辑**：把 `CullEngine::computePhash` / `hamming` 从 private **提升为 public**，作为 pHash（DCT-II 低频 64-bit 哈希 + Hamming 近重复判定）**唯一真源**。新增 `core/PhotoDedup.{h,cpp}` 纯函数 `findGroups(photos, thumbsDir, phashThreshold=8, exactMd5=true)`：预处理算 MD5+pHash（缩略图缺失/解码失败即放行该张），MD5 完全相同的先聚「完全重复（MD5 相同）」组，pHash Hamming ≤ 阈值的再聚「高度相似（感知哈希）」组，**每组保留 `origW*origH` 最大者**。

**★核心契约 = 守恒**：每张合法照片恰好出现一次（保留者或被标记者）。`core/PhotoDedup` 暴露 QA 钩子 `setForceDoubleCountForQa(bool)`：反例下把第 0 组的保留者再塞进第 1 组的 `dupPaths` → 真实实现产生**重复计入**，守恒被破坏。

**可逆标记（非破坏性删除）**：`Library` 加 `mark` 字段（`Photo.mark`）+ `BatchField::Mark` + `setMark` / `setMarks`（完全镜像既有 `setRating` / `setPrivates` 事务路径）；沿用 app 既有 `skip` 语义做「待删」标记，**规避物理删除风险**。视图不持写能力（铁律 5）：`DedupDialog` 点「标记其余为待删」只发 `markRequested(dupPaths)` → `GalleryPage::batchMarkRequested` → `MainWindow::setMarks`（写库在 MainWindow）。

**UI**：GalleryPage 聚类条右侧新增「相似照片」按钮 → 弹 `DedupDialog`（只读浏览：保留者缩略图 + 原因 + 相似缩略图行 + 「标记其余为待删」）；标记后该组从列表移除。

### 新增 - LA_DEDUP_QA（第 41 门，fast 层）

纯数据门（`png=$false`），直接调用 `PhotoDedup::findGroups()`，不构造 `MainWindow`、不渲染。合成确定性缩略图注入 7 类断言：

- **A 精确去重**：字节相同的两张图进「完全重复（MD5 相同）」组。
- **B 近重复**：1px 差异的图进「高度相似（感知哈希）」组（Hamming 不变、MD5 不同）。
- **C 计数**：组数 == 2、重复张数 == 2。
- **D 唯一**：完全不相似的棋盘格图不成组。
- **E 缺失**：缩略图缺失的图不入任何组、不崩溃。
- **F 保留最优**：每组保留者分辨率最高（`a1` 4000×3000、`b1` 3000×2000）。
- **G ★守恒**：每张合法照片恰好出现一次（`noDouble` + `dupTotal` + `seenSize`/`seenSet` 校验）。

**反例（非空心）**：`LA_DEDUP_QA_FORCE_BROKEN=1` 经 `setForceDoubleCountForQa(true)` 让**真实实现**把第 0 组保留者重复计入第 1 组 → `noDouble`/`dupTotal` 变红（实测 `marks=12/14`，失败清单 `dupTotal,noDouble` 精准对应「照片被双重计数」）。反例注入在真实实现上，断言必然有效。

**★分层实测决定（规则 13）**：该门虽是纯数据运算，但进程仍付 `QApplication` 启动 + offscreen 初始化成本；实测 **159–162ms < 500ms** 快门预算 → 归入 **fast 层**（`LA_CLUSTER_QA` 同类纯数据门 1424ms 被迫留 full 层，二者形成对照）。`LA_GATE_MATRIX_QA` E6 实测校验通过。

### 修复 / 过程
- **新增** `src/core/PhotoDedup.{h,cpp}`：`DedupPhoto` / `DupGroup` / `findGroups` / `setForceDoubleCountForQa`（含 `g_forceDoubleCount` 反例钩子）。
- `src/core/CullEngine.h`：`computePhash` / `hamming` 由 private 提升 public（pHash 唯一真源）。
- `src/core/Library.{h,cpp}`：`Photo.mark` 字段；迁移 `ALTER TABLE photos ADD COLUMN mark TEXT`；SELECT 第 10 列；`BatchField::Mark`；`applyBatch` 三分支；`setMark` / `setMarks`。
- `src/ui/DedupDialog.{h,cpp}`：只读去重组浏览 + `markRequested` → 父级 `batchMarkRequested`。
- `src/ui/GalleryPage.{h,cpp}`：聚类条右侧「相似照片」按钮 + `openDedup()` 槽 + `batchMarkRequested` 信号 + `thumbsDir_`。
- `src/ui/MainWindow.cpp`：`batchMarkRequested` → `lib_->setMarks` 接线（镜像 `batchRatingRequested` / `batchPrivateRequested`）。
- `src/main.cpp`：`runDedupQa()`（A–G 七类断言 + 守恒反例）+ `LA_DEDUP_QA` 派发 + 新 include。
- `gate_matrix.ps1`：新增第 41 门（fast 层，measuredMs=160）。
- `CMakeLists.txt`：`VERSION 2.16.0 → 2.17.0`；登记 `PhotoDedup.{h,cpp}` / `DedupDialog.{h,cpp}`。
- i18n：手工追加 4 条（相似照片 / 发现 N 组 / 未找到 / 标记其余为待删），`lrelease 270 finished / 0 unfinished`，未跑 `lupdate`。

### 门数
41 门（新增 1）。fast 层 12 → 13 门，full 层 28 门不变。


## [2.16.0] - 2026-10-05

### 新增 - 相册聚类（方向 C 第一步）

图库此前只能按「分类 / 人物 / 大区 / 省 / 搜索串」筛选，缺少「把相册按地点或时间分组浏览」的能力。本步给出聚类条。

**零模型改动**：`Photo` 早已有 `prov` / `city` / `year` / `date` 字段（`core/Library.h`），本步只读取与分组。

**core 纯函数 `PhotoCluster.{h,cpp}`**：`cluster(photos, dim, hidePrivate)` 支持四个维度（省 / 市 / 年 / 月），返回 `QVector<Cluster>{key, label, count, indices}`。簇排序（`count` 降序 + `label` 升序）与分组规则**全部在 core** —— **UI 不得重写分组逻辑**，否则 UI 与门禁会各算一套。UI 只负责「簇 → chip」呈现，门禁直接调用同一函数。

**★核心契约 = 总数守恒**：键为空的照片（无地点 / 无日期）必须归入 `kUnclustered` 簇，**绝不静默丢弃**。聚类的价值就在于让用户看见那些没被归类的照片；丢了就是骗用户。哨兵值用纯 ASCII `"@@unclustered@@"`（真实省/市/年月值不会出现 `@`），既保证不碰撞又便于 `grep` 核验。

**筛选叠加**：`GalleryModel` 新增**独立**的 `clusterDim_` / `clusterKey_` 字段（**不复用** `setProvFilter` / `setCategoryFilter` 等，避免改变既有筛选语义），`rebuild()` 内与其它筛选**并存叠加**（AND 关系）。`setClusterFilter(dim, key)` 传空 key 即清除，与各 setter 口径一致。

**UI**：GalleryPage 顶栏新增维度下拉（按省份 / 按城市 / 按年份 / 按月份）+ 簇 chips（复用既有 `addChip` 范式：可点选中、再点取消、悬停提示簇名）。切换维度会清空当前簇选择（旧键在新维度无意义）。计数栏加「聚类：X」注记。无任何可分簇数据时（如整库都没写省/市）整条隐藏。簇选择与分类/人物筛选**并存**，计数栏会同时显示各注记。

### 新增 - LA_CLUSTER_QA（第 40 门，full 层）

纯数据门（`png=$false`），直接调用 `PhotoCluster::cluster()`，不构造 `MainWindow`、不渲染。注入 8 张确定性 `Photo`（跨省/市/年月，含 1 张无地点无时间、1 张有省无市、1 张私密），18 条断言覆盖四类性质：

- **A 地点聚类**：按省正确分组（安徽 3 / 北京 2 / 未分类 1）；按市正确分组（黄山市 2 / 合肥市 1 / 北京市 1 / 未分类 2）。
- **B 时间聚类**：按年（2023 → 3、2024 → 2、未分类 → 1）、按月（2023-05 → 2）。
- **C ★总数守恒**：各簇 `count` 之和 **== 可见照片数**（含未分类簇）。这是「聚类没骗用户」的核心契约。
- **D 隐私门**：`hidePrivate=false` 时私密照片参与分簇（总数 == 8，多出「浙江省」一簇）。

**反例（非空心）**：`LA_CLUSTER_QA_FORCE_BROKEN=1` 通过 core 层 QA 钩子 `PhotoCluster::setForceDropUnclusteredForQa(true)` 让**真实实现**丢弃空键照片 → 实测 `marks=8/18`，失败清单 `provSum,provSize,provUnc,citySize,cityUnc,yearSize,yUnc,uncLabel,allSum,allSize` **全部集中在总数守恒与未分类簇**，精准对应缺陷形态。反例注入在真实实现上而非测试侧伪造，故断言必然有效。

**★分层被自身门禁拦下（规则 13 的又一次生效）**：初版按「纯数据、无 UI，同类门 `LA_GALLERY_SEARCH_QA` 仅 56ms」推断应放 fast 层，但 `LA_GATE_MATRIX_QA` 的 E6 实测 `1424ms > 500ms` 预算直接判红 —— 该门虽是纯数据运算，进程仍要付满 `QApplication` 启动 + offscreen 平台初始化成本（这部分才是大头）。已诚实改回 full 层，未靠直觉分层。

### 修复 - LA_LUMEN_SMOKE 稳定 KILL（门禁基建）

**症状**：该门连续 `[KILL]`（runner 25s 超时）。**排查走了两次弯路**：

1. 猜「动态壁纸 `webwallpaper64` 占 CPU」→ 停掉它（系统负载 60% → 30%）后**仍 KILL，假设被证伪**。
2. 猜「大图逐像素 MAD 慢」→ 实测金样图仅 **0.15 Mpx**；给足 120s 直跑 → `exit=0` + `LA_LUMEN_SMOKE PASS`（90 行输出）；精确计时 **66.6 秒**。

**根因**：该门做 8 张图 × 12 个算子 = **96 次全帧像素对拍**，本就昂贵；而 `gate_matrix.ps1` 记录的 `measuredMs=7431` 与真实值 **66585 差 9 倍**（记录失真且无人复核）→ 固定 25s 预算下必然 KILL。**E8 断言只校验 `measuredMs` 字段存在，不校验它与 runner 预算是否一致**，所以失真一直没被发现。

**修法**：门表新增 `timeoutSec` 字段，`qa_runner.ps1` 支持**门级超时覆盖**（缺省仍用全局 `-Timeout`），该门设 `90`。挂死检测依然有效，只是预算与真实成本相符。KILL 提示文案同步改用实际生效的 `$gateTimeout` 归因。

**★PowerShell 坑（铁律 32）**：门表项是 **hashtable**，`$g.PSObject.Properties.Name -contains "timeoutSec"` **恒为 false** → 门级超时**静默失效**（表现为「配了却不生效」，极易误判为修法无效）。正解 `$g.ContainsKey("timeoutSec")` + `$g["timeoutSec"]`。同类坑：`Where-Object { $_.n -eq ... }` 对 hashtable 也不按 `.n` 匹配。

### 修复 / 过程
- **新增** `src/core/PhotoCluster.{h,cpp}`：`Dim` 枚举（省/市/年/月）、`kUnclustered` 哨兵、`clusterKey` / `clusterLabel` / `cluster()`、QA 反例钩子 `setForceDropUnclusteredForQa`。
- `src/ui/GalleryModel.{h,cpp}`：`setClusterFilter` / `clearClusterFilter` / `clusterDim` / `hasClusterFilter`；`clusterDim_` / `clusterKey_` 成员；`rebuild()` 内聚类判定（与其他筛选并存叠加）。
- `src/ui/GalleryPage.{h,cpp}`：`clusterBar_` / `clusterDimBox_` / `clusterChipsBar_` / `clusterChips_` 成员；`buildClusterBar()` / `restyleClusterChips()`；构造期挂载聚类条；`buildChips()` 末尾触发重建（数据/隐私门变化时自动跟随）；`updateCount()` 加「聚类：X」注记。
- `src/main.cpp`：`runClusterQa()`（18 断言 + 失败标签诊断输出）；`LA_CLUSTER_QA` 派发；`#include "../core/PhotoCluster.h"`。
- `qa_runner.ps1`：支持门级 `timeoutSec` 覆盖 + KILL 文案归因。
- `gate_matrix.ps1`：新增第 40 门（full 层）；`LA_LUMEN_SMOKE` 加 `timeoutSec=90` 且 `measuredMs` 修正为实测 66585。
- `CMakeLists.txt`：`VERSION 2.15.0 → 2.16.0`；登记 `PhotoCluster.{h,cpp}`。
- i18n：手工追加 6 条（聚类维度提示 + 4 个维度名 + 聚类注记），`lrelease 266 finished / 0 unfinished`，未跑 `lupdate`。

### 门数
40 门（新增 1）。fast 层 12 门不变，full 层 27 → 28 门。

## [2.15.0] - 2026-10-04

### 新增 - 热力色带按密度分级（方向 A 第四步 · 方向 A 收尾）

此前热力层无论密度高低都是同一套暖色（`0xff5a2a` 三段），只能表达「有足迹」，无法表达「哪里更密」。本步让颜色随密度分级，并支持三套色带切换。

**★色带唯一真源（关键决策）**：新增 `MapView::heatRampOf(rampId, tier)` 返回 `{中心色, 中段色, 边缘透明色}`，`heatRamp(dens)` 按密度选档（低 `<3` / 中 `3–7` / 高 `≥8`）。此前 `drawHeatWorld` 与 `drawHeat` **两处各硬编码一份完全相同的颜色表** —— 本步把两者收敛到同一真源，**绘制与门禁断言共用**，杜绝「改了一处忘了另一处」。这是本轮的主要结构收益。

**三套色带**：`HR_Warm` 暖橙（默认，= 2.12.0 基线，`LA_MAP_HEAT_QA` 的 `r-g>90 && r-b>90` 断言依赖它）、`HR_Jade` 翡翠（青绿→翠→亮青）、`HR_Crimson` 绯红（暗红→绯红→亮粉）。三条色带的色相**刻意彼此分离**，且与既有元素全不撞色：翡翠光点（绿主导）、金色航线（`r≈g`）、青色轨迹（蓝主导）、暖橙（`r` 主导）。

**零回归**：`heatRamp_` 默认 `HR_Warm`，且 `HR_Warm` 的低/中档取色贴近 2.12.0 基线色（`0xd97a2a` / `0xf05a22`），`LA_MAP_HEAT_QA` 的暖色像素断言继续成立。

**UI**：顶栏新增色带下拉（暖橙/翡翠/绯红），仅热力层可用、随三态分段联动启禁。切换时 `setHeatRamp` → `buildWorldPixmap()` + `invalidateViewCache()` 双重重烘焙（热力在静态层内，故不增每帧开销）。

### 新增 - LA_MAP_HEAT_RAMP_QA（第 39 门，full 层）

注入三簇密度迥异的足迹点（3 个远离点 / 12 点簇 / 40 点簇，覆盖低中高全部档位），离屏渲染世界层后把每个热力像素按 R/G/B 占比**归类到最接近的档位**，断言三档像素数**均 > 0**（密度分级确实生效）。归类使用与 `heatRampOf` 同一真源的代表色，容差 ±0.18。

**反例（非空心）**：`LA_MAP_HEAT_RAMP_QA_FORCE_BROKEN=1` 通过 `heatRampForceTier_` 强制所有点同档（抹掉密度分级）→ 只剩一档像素、另两档为 0 → `FAIL t0=… t1=0 t2=0` → 门红。

### 修复 - LA_AGENT_SMOKE 长期 flaky（本轮顺带修掉的门禁基建缺陷）

**症状**：该门在多轮迭代中反复偶发红（前几轮判为「负载噪声」，本轮实测**稳定红**两次均 20211ms），被 `qaExit` 看门狗以 `LA_WATCHDOG FAIL gate=LA_AGENT_SMOKE budget_ms=20000 reason=no-exit-request` 误杀。

**根因（先测再改，非代码缺陷）**：`runAgentSmoke()` 的第 ① 步 `LumenAgent::decide(ctx)` 会**探测并连真实 Ollama**（`LA_LLM_URL` 默认 `http://127.0.0.1:11434`）。用户本机 Ollama 正在运行 → 走真实在线推理 → 耗时 15.9–20.3s，**逼近 20s 看门狗预算** → 被误杀。用 `git stash` 对照 2.14.0 基线（绿、15928ms）确证差异来自环境态而非本轮改动。

**修复（让门禁与环境解耦）**：把 `LA_LLM_URL` 在本门内**固定指向关闭端口** `http://127.0.0.1:9`，强制走 350ms 探测后的本地规则兜底 → 耗时 **20211ms → 1183ms**，两次复跑全绿。门末还原环境变量。代价是不再覆盖「真实在线推理」路径（那属集成测试，不适合进 PR 门禁），但换来断言与「用户是否开着 LLM」完全无关 —— 这正是门禁确定性的要义。

`gate_matrix.ps1` 中该门 `measuredMs` 同步更新（8662 → 1000）。full 层总耗时亦因此从 105s 降到 87s。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`HeatRamp` 枚举 + `HeatColors` 结构 + `heatRampOf`/`heatRamp` 唯一真源 + `heatRamp_`/`heatRampForceTier_` 成员 + `setHeatRamp`/`heatRamp` + `qaShootHeatRamp`；`drawHeatWorld`/`drawHeat` 改为调用真源（消除两处硬编码）。
- `src/ui/MapPage.{h,cpp}`：顶栏色带下拉 + 切换接线 + 仅热力层启用联动。
- `src/main.cpp`：`animOk` 排除列表加新门；新增 QA 派发；**`runAgentSmoke()` 离线化（`LA_LLM_URL` 固定指向关闭端口）消除长期 flaky**。
- `gate_matrix.ps1`：新增第 39 门（full 层，ASCII 注释）；`CMakeLists.txt`：`VERSION 2.14.0 → 2.15.0`。

### 门数
39 门（新增 1）。fast 层 12 门不变，full 层 26 → 27 门。**方向 A 全部子项闭环**（时间维 → 双层绘制 → 三态开关 → 箭头/播放 → 按年筛选 → 色带分级）。

## [2.14.0] - 2026-10-04

### 新增 - 按年筛选足迹（方向 A 第三步）

足迹地图此前只能看全量足迹，无法回答「某年我去了哪」。本步给三层（光点/轨迹/热力）加统一的年份维度筛选。

**数据诚实**：年份下拉只列**库中真实出现过的年份**（`computeFootYears()` 扫 `Photo.year` 去重降序），不按 `kpi.minYear..maxYear` 补齐中间空档 —— 没有照片的年份不作为可选项出现。`Photo.year` 在 `Library.cpp:84` 早已填充，本步零模型改动、零重复解析。

**★筛选位置铁律（本步最关键决策）**：筛选在**注入点**（`MapPage::computeGpsPoints`）完成，而非绘制层。原因是 `gpsPts_` / `gpsPaths_` / `gpsDates_` 三者**按下标严格对齐** —— `enterCountry` 里 `gpsPaths_[i]` 与 `gpsPts_[i]` 配对（`MapView.cpp:2304`）构造国家视图的照片路径。若只在绘制时过滤，三者长度将不一致，国家视图会把照片路径配到错误的足迹点上。故 `if (footYear_ > 0 && p.year != footYear_) continue;` 与其余过滤条件同处一个循环，`pts/paths/dates` 一起产出。

**同步入口收敛**：新增 `MapPage::syncFootData()` 作为足迹数据同步的**唯一入口**（注入 pts/dates/paths + 计数 + 记录筛选态）。构造、`setHidePrivate`（隐私门）、年份切换三处全部改走它 —— 铁律：任何改变**可见足迹集合**的操作都必须经此入口，否则三者的下标对齐与缓存失效（`densDirty_`/`arcTargetsDirty_`/`invalidateViewCache`）会各自漂移。

**UI**：顶栏新增年份下拉（`QComboBox`，墨韵深色样式 + 选中态翡翠），首项「全部年份」（值 0），切换时 `syncFootData()` + 航线目标同步 + `stopTrackPlayback()`（集合已变，旧播放进度失效）。筛选对三层同时生效，不因当前处于轨迹层而失效。

### 新增 - LA_MAP_YEAR_QA（第 38 门，full 层）

纯数据契约门（`png=$false`，不渲染）。注入 6 个跨年份确定性 GPS 点（2019/2021/2024 各 2），四类断言：

- **A 点数**：筛选 2024 后 `gpsPts_` 恰为该年点数（2）。
- **B 下标对齐（核心）**：`gpsPts_.size() == gpsPaths_.size() == gpsDates_.size()` —— 直接锁死国家视图依赖的那条不变量。**只断言「点数对」是弱断言**，对齐才是本步真正要防的回归。
- **C 幂等**：`setFootYearFilter` 重复设置同一年不改变态。
- **D 0=全部**：切回 0 恢复全量 6 点。

**反例（非空心）**：`LA_MAP_YEAR_QA_FORCE_BROKEN=1` 让注入点过滤失效 → 6 点全可见 → A 断言 `pts=6 expect=2` → RED。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`footYearFilter_` 成员 + `setFootYearFilter`/`footYearFilter`（幂等）+ `qaShootYear` 探针。
- `src/ui/MapPage.{h,cpp}`：`computeGpsPoints` 增年份过滤（与 pts/paths/dates 同循环）；新增 `computeFootYears()`/`syncFootData()`/`footYear_`/`yearBox_`；三处调用点收敛到 `syncFootData`；补 `QComboBox`/`QList`/`QSet`/`<algorithm>`/`<functional>` 头文件。
- `src/main.cpp`：`animOk` 排除列表加新门；新增 QA 派发。
- `gate_matrix.ps1`：新增第 38 门（full 层，ASCII 注释）；`CMakeLists.txt`：`VERSION 2.13.0 → 2.14.0`。

### 门数
38 门（新增 1）。fast 层 12 门不变，full 层 25 → 26 门。

## [2.13.0] - 2026-10-04

### 新增 - 轨迹方向箭头 + 播放动画（方向 A 第二步）

2.12.0 的轨迹层只有一条静态折线，无法表达「行进方向」与「按时间走过」。本步在不改动数据模型、不破坏性能预算的前提下补齐二者的表达。

**方向箭头（静态烘焙层）**：新增 `drawTrackArrows(p, pts)`，在每段中点绘制一个朝向下一段的三角（`QPainterPath` 三点闭合，随段方向旋转，退化段 `len<1e-6` 跳过）。世界层注入 `drawTrajectoryWorld`、下钻层注入 `drawTrajectory`，**均落在既有一次性栅格化缓存内** → 不增加每帧开销。

**播放头（动态层）**：新增 `drawTrackPlayback(p)` —— 翡翠实心芯 + 随进度收缩的脉冲环 + 身后 3 段递减拖尾。**铁律：播放头只在 `paintMap` 动态段调用，严禁进入 `bakeViewPix`/`buildWorldPixmap`** —— 否则每帧重栅格化整条轨迹，`LA_MAP_PERF` 预算会被击穿。

**位置唯一真源**：`trackPosAt(frac)` 为纯函数，按**累计弧长**（非点序号）插值，使播放头匀速沿折线前进，frac 超出 `[0,1]` 钳到端点。产品绘制与门禁断言**共用同一函数**（契约同源铁律）→ 门禁不可能与实现漂移。

**计时与降级**：`trackTimer_`(16ms) + `trackClock_`，仅播放期挂载、播完自停（不常驻空转）；`reduceMotion_` 为真时直接呈终态（末点脉冲驻留）而不空转。`startTrackPlayback()` 在非轨迹层或轨迹点 < 2 时拒绝起播；`setFootLayerMode` 离开轨迹层即 `stopTrackPlayback()`，防播放态残留到光点/热力层。

**UI**：MapPage 顶栏新增「▶ 播放足迹」按钮（复用既有 jade 深色样式），点击切换播放/停止；**仅轨迹层可用**，随三态分段按钮联动启禁。文案走 `translate("LightAlbum", …)` 字面量直调（遵 2.12.0 铁律 16）。

### 新增 - LA_MAP_TRACK_ANIM_QA（第 37 门，full 层）

注入 5 个确定性 GPS 点 + 递增日期，两类断言：
- **A 箭头存在性**：统计最亮一档青色像素（`b>235 && g>225 && r>170 && b>r+25`，比轨迹亮芯更亮）`> 0`。
- **B 三时刻位置（核心，区分器）**：播放头在 `t=0 / t=mid / t=end` 的**两两位移均 > 8px**。这是「真沿线移动」与「原地闪」的唯一区分器 —— 只断言「有像素」会是空心断言。位移经 `trackPosAt()` 求出，与产品渲染同源。

**反例（非空心）**：`LA_MAP_TRACK_ANIM_QA_FORCE_BROKEN=1` 把播放头钉死在首点 → 三时刻位置全等（位移全 0）→ `FAIL dPM=0.00 dEM=0.00 dPE=0.00` → 门红。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`trackTimer_`/`trackClock_`/`trackActive_`/`trackTOverride_`/`kTrackDurMs` 成员；`drawTrackArrows`/`drawTrackPlayback`/`trackPosAt`/`startTrackPlayback`/`stopTrackPlayback`/`qaShootTrackAnim`；构造函数接线播放时钟；`setFootLayerMode` 离层即停播；`paintMap` 动态段接入播放头。
- `src/ui/MapPage.{h,cpp}`：顶栏「▶ 播放足迹」按钮 + 播放/停止切换 + 随三态分段联动启禁。
- `src/main.cpp`：`animOk` 排除列表加新门；新增 QA 派发（镜像 `LA_MAP_HEAT_QA` 模板）。
- `gate_matrix.ps1`：新增第 37 门（full 层，ASCII 注释，实测 `measuredMs`）；`CMakeLists.txt`：`VERSION 2.12.0 → 2.13.0`。

### 门数
37 门（新增 1）。fast 层 12 门不变，full 层 24 → 25 门。

## [2.12.0] - 2026-10-04

### 新增 - 足迹轨迹 / 热力双模式（方向 A 第一步：数据时间维 + 双层绘制 + 三态开关）

**背景**：足迹地图此前只有「翡翠光点」一种呈现（`FL_Dots`），无法表达"按时间先后走过的路径"与"拍摄密度"这两类信息。本步在**不改动数据模型**的前提下补齐时间维并新增两个可切换层。

**数据时间维（零模型改动）**：`Photo.date` 早已存在（`core/Library.h:28`），本次只做提取与对齐——`MapPage::computeGpsPoints` 扩展出参 `QVector<QDate>* datesOut`，在**同一过滤循环内**（`hasGps && !priv && !(la==0&&lo==0)`）解析日期，保证与 `gpsPts_` 逐点严格对齐；解析失败回退为无效 `QDate`（数据诚实：不编造日期）。日期经 `MapView::setGpsDates()` 注入为平行成员 `gpsDates_`。

**双层绘制（烘焙静态层，不破 LA_MAP_PERF）**：新增 `drawTrajectoryWorld`/`drawHeatWorld`（世界层，`buildWorldPixmap` 内）与 `drawTrajectory`/`drawHeat`（下钻层，注入 `renderRegionStatic` / `renderProvinceStatic` / `renderCountryOnly` 的 `drawGpsDots` 之后）。二者**复用 `viewOrigin` 变换**（铁律：绘制与命中同源），且都落在既有一次性栅格化缓存内，因此帧耗时预算不受影响。轨迹按 `sortedTrackIdx()`（`gpsDates_` 升序 `std::stable_sort`，无日期者置后且保持原序）连折线；热力为密度分级的暖色径向渐变。

**三态开关**：`MapPage` 顶栏新增 `QButtonGroup` 独占三段按钮「光点 / 轨迹 / 热力」，沿用既有墨韵深色样式、`translate("LightAlbum", ...)` 包裹（lupdate 可抽取），连 `view_->setFootLayerMode(0/1/2)`。切换时 `buildWorldPixmap()` + `invalidateViewCache()` 双重重烘焙。

**零回归策略**：`footLayer_` 默认 `FL_Dots`，所有新增绘制均在 `footLayer_ != FL_Dots` 时才执行 → 未切换时输出与 2.11.1 逐像素一致，`LA_MAP_A_QA` / `LA_MAP_PERF` 不受影响。

**颜色分离（颜色校验铁律·实测）**：轨迹取青（蓝主导 `b>r+15 && b>g+15`）、热力取暖（红主导 `r-g>90 && r-b>90`），与既有翡翠光点（绿主导）、金色航线（`r≈g`）色相分离 → 三类像素统计互不污染，门禁可独立断言。

### 新增 - LA_MAP_TRACK_QA（第 35 门，full 层）

注入 5 个确定性 GPS 点（北京/上海/广州/成都/乌鲁木齐）+ 确定性递增日期，`footLayer=轨迹`，离屏渲染世界层并统计**青色轨迹像素**，断言 `>= 30`。经 `System.Diagnostics.Process` 直连自退，输出 `LA_MAP_TRACK_QA trackPx=…`。

**反例（非空心）**：`LA_MAP_TRACK_QA_FORCE_BROKEN=1` 强制回落 `FL_Dots` → 不画轨迹 → `trackPx=0 < 30` → 输出 `FAIL cyan=0` → 门红。

### 新增 - LA_MAP_HEAT_QA（第 36 门，full 层）

同构探针，`footLayer=热力`，统计**暖色热力像素**，断言 `>= 30`。

**反例（非空心）**：`LA_MAP_HEAT_QA_FORCE_BROKEN=1` 强制回落 `FL_Dots` → `warmPx=0 < 30` → 门红。

两门均属 map 类（构造 MainWindow、离屏渲染世界层），按矩阵分层依据落 **full 层**（不写 `tier` 键 = fail-safe 默认），不挤占 500ms fast 预算。

### 修复 / 过程
- `src/ui/MapView.{h,cpp}`：`FootLayer` 枚举 + `gpsDates_`/`footLayer_` 成员 + `setGpsDates`/`setFootLayerMode`/`footLayerMode` + `sortedTrackIdx` + 四个轨迹/热力绘制函数 + `qaShootTrack`/`qaShootHeat` 像素探针（含 FORCE_BROKEN 反例）。
- `src/ui/MapPage.{h,cpp}`：`computeGpsPoints` 增 `datesOut` 出参；两处调用点（构造 + `setHidePrivate`）注入日期；顶栏三态分段按钮 + `setFootLayerMode` 连线。
- `src/main.cpp`：`animOk` 排除列表加入两个新门；新增两处 QA 派发（镜像 `LA_MAP_COUNTRY_QA` 模板）。
- `gate_matrix.ps1`：新增 2 门（full 层，带注释与 `measuredMs`）。
- `CMakeLists.txt`：`VERSION 2.11.1 → 2.12.0`。

### 门数
36 门（新增 2）。fast 层 12 门不变，full 层 22 → 24 门。

## [2.11.1] - 2026-10-04

### 修复 - i18n 默认路径在中文 Windows 误载英文翻译器（debug 客户端「界面显示异常」根因）

**问题**：2.10.0 引入的中英双语 i18n 在部分中文 Windows 上**整屏 UI 变成英文**——「区域统计 / 已分析 / 摄影日志」等核心文案全部回退为 `No Image / Original / Edited / Photo Log` 英译，用户报告"好多之前没问题的现在出现问题了"。

**根因（铁证）**：原默认路径判定用 `QLocale::system().language() != QLocale::Chinese` 决定是否加载 `:/i18n/LightAlbum_en.qm`。Qt6 的 `language()` 返回的是**"区域格式"（locale for formatting）**语言，受 Windows「区域 → 管理 → 非 Unicode 程序的语言」/ 独立区域格式设置影响；在**显示语言为中文、区域格式被设为英语**的机器上会**误返 `English`**（本机实测 `lang=1` 即 English，而 `uiLanguages()` 居首为 `zh-Hans-CN`）。于是中文系统被误判为「非中文」→ 强制加载英文翻译器 → 全 UI 英文化。这是**纯判定缺陷**，与翻译文件本身无关。

**修复**：新增纯函数 `laI18nShouldLoadEn(forced, loc)` 作为语言决策唯一真源：
- 若 `LA_I18N_LANG` 显式设置（`en`/`zh`），**优先**于系统 locale（便于强制/测试）；
- 否则 `language()==Chinese` 或 `uiLanguages()` 任一含 `zh*` → 保持原生中文 UI（不加载）；
- 仅当系统既非中文 locale、`uiLanguages()` 也无 `zh*` 时，才加载英文。

`uiLanguages()` 对应 `GetUserPreferredUILanguages`，反映 OS **显示语言**列表，语义正确，弥补 `language()` 的盲区。中文默认不加载翻译器 → 原生 UI 零回归（与 2.10.0 设计一致）。

**门禁升级**：`LA_I18N_QA`（第 32 门，fast 层）由原 4 条「翻译正确性」断言**扩至 9 条**——在 4 条翻译断言之后，新增 5 条「构造-locale 决策」断言，把 2.10.0 漏测的默认路径焊进门禁（`zh_CN→不加载`、`zh_TW→不加载`、`en_US→加载`、`forced=en` 覆盖系统、`forced=zh` 显式保持中文）。反例 `LA_I18N_QA_FORCE_BROKEN=1` 仍非空心（不加载翻译器 → 4 串全 FAIL）。

### 修复 / 过程
- `src/main.cpp`：新增 `laI18nShouldLoadEn()` 纯函数；默认加载块改用该函数替代 `language()!=Chinese` 判定；`LA_I18N_QA` 门由 4 断言扩至 9 断言（含 5 条决策断言）。
- `CMakeLists.txt`：`VERSION 2.11.0 → 2.11.1`。
- `gate_matrix.ps1`：`LA_I18N_QA` 注释补记决策断言（`tier="fast"` / `measuredMs=210` 不变）。

### 门数
34 门（无新增门）。fast 层 12 门不变。回归：重建后 `LA_I18N_QA PASS entries=9`（含 5 决策断言全过）；修复前 exe 实测 `LA_I18N_DEBUG lang=1 ui=zh-Hans-CN,... loadEn=1 translator=en installed`（复现误载），修复后 `loadEn=0` 且无 `translator=en installed` 行（中文 UI 恢复）。`LA_SCALE_PERF ctrls=60` / `LA_GALLERY_E_QA featureOK=1` 全绿；图库离屏 grab 仅 2 缩略图为探针假象（虚拟化未触发布局），非真实回归。

## [2.11.0] - 2026-10-04

### 变更 - 无障碍 a11y（方向 F 第三步：键盘可达 + 对比度合规）

**问题**：取证发现三处可达性缺口——
- 地图 `MapView`、灯箱 `PhotoLightbox` 未设 `StrongFocus`，键盘无法聚焦或触发操作；
- 多处硬编码提示文字 `#5a6b60` 在深色背景上对比度不足（未达 WCAG 2.1 正文 4.5:1）；
- 「全局焦点环」子项已存在（`Lumen::focusRing()` + `main.cpp` 全局挂载），故免做。

**方案**：
- **键盘可达**：`MapView` 设 `StrongFocus` + `keyPressEvent`（`+`/`=` 放大、`-`/`_` 缩小改 `worldZoom_`、方向键平移、`Enter`/`Return` 下钻 `drillAtCenter`、`Esc` 返回），自绘翡翠焦点环（与全局 focusRing 同色 `0x5f,0xdc,0xa6`）；`PhotoLightbox` 设 `StrongFocus`、`showEvent→setFocus()`、`←`/`→` 基于 `lib_->photos()` 线性相邻下标翻图（端点不回绕）。`EditorialView::qaKeyboardCheck()`（#367 已落）断言 `GalleryTile` StrongFocus + `Enter` 激活。
- **对比度合规**：将 3 处硬编码提示色 `#5a6b60` → `#8da996`（与 `Dark::Muted`/`hint` 一致，满足 WCAG）；新增 `LA_A11Y_CONTRAST_QA` 审计 15 对 DarkPalette 固定色对（正文 4.5:1）。

### 新增 - LA_A11Y_CONTRAST_QA（第 33 门，fast 层）

纯函数 WCAG 2.1 相对亮度审计，覆盖 `Dark::Ink/Muted/Gold/Emerald/Bg/Card/Inset/Line` 与产品内固定硬编码色对的 15 对组合，逐对比对 4.5:1 阈值。纯 offscreen 自退、不构造 MainWindow、无外部 `need` 依赖。

**反例（非空心）**：`LA_A11Y_CONTRAST_QA_FORCE_BROKEN=1` 注入 `BROKEN Line on Inset (1.35:1)` → 门红。

### 新增 - LA_A11Y_KEYBOARD_QA（第 34 门，fast 层）

转发 `EditorialView::qaKeyboardCheck()` + 新增 `MapView::qaKeyboardCheck()`：断言瓦片/地图 `focusPolicy==StrongFocus` 且模拟 `Enter`/`Key_Plus` 触发激活/缩放（`worldZoom_ 1.0→1.25`）。纯 offscreen 自退、不构造 MainWindow、无外部 `need` 依赖。

**反例（非空心）**：`LA_A11Y_KEYBOARD_QA_FORCE_BROKEN=1` 强制 `NoFocus` → `tile not keyboard-focusable` + `mapview not keyboard-focusable` 双 `FAIL` 必红。

两门实测墙钟 **CONTRAST 18ms / KEYBOARD 17ms**（offscreen x3 取最优，2026-10-04），远低于 500ms fast 预算、比最便宜的 full 门（1017ms）还低 → 落 fast 层，使 a11y 获得 PR 层覆盖。

### 修复 / 过程

- `src/ui/MapView.{h,cpp}`：`StrongFocus` + `keyPressEvent` + `zoomBy/panBy/drillAtCenter` 辅助方法 + 翡翠焦点环 + `static int qaKeyboardCheck()`。
- `src/ui/MainWindow.cpp`（PhotoLightbox）：`StrongFocus`、`setPhoto` 存 `thumbsDir_`、`showEvent→setFocus()`、`←`/`→` 翻图 `navigate(int)`、`thumbsDir_` 成员。
- `src/ui/EditorialView.{h,cpp}`（#367）：`qaKeyboardCheck()` 瓦片键盘可达。
- `src/ui/CullPage.cpp` / `LogPage.cpp` / `MapPage.cpp`：提示色 `#5a6b60`→`#8da996`（对比度整改）。
- `src/main.cpp`：新增 `LA_A11Y_CONTRAST_QA` / `LA_A11Y_KEYBOARD_QA` 两门断言分支（位于 `int main` 启动前）。
- `CMakeLists.txt`：`VERSION 2.10.0 → 2.11.0`。
- `gate_matrix.ps1`：登记第 33/34 门，均 `tier="fast"`、`measuredMs=18/17`。**纯 ASCII**。

### 门数

32 → **34** 门。fast 层 10 → **12** 门。回归：fast 层 12 门全绿（wall 4216ms）；`LA_A11Y_CONTRAST_QA` 绿态 `PASS pairs=15`、红态 `BROKEN Line on Inset` 全中；`LA_A11Y_KEYBOARD_QA` 绿态 `PASS tile=keyboard-activatable` + `mapview=keyboard-navigable worldZoom 1.0000->1.2500`、红态双 `FAIL` 全中。全量 34 门 PASS=33 SKIP=1（LA_SCAN_SMOKE 需 env，预期跳过）KILL=0；`LA_AGENT_SMOKE` 受本地 Ollama 模型冷加载影响偶发超时（环境依赖，非代码回归，Ollama 热态 PASS）。

## [2.10.0] - 2026-10-03

### 变更 - 中英双语国际化（方向 F 第二步）

**问题**：核心 UI 文案（按钮、标签、状态、页标题）全部硬编码中文字面量，无法切换语言，
且未来接英文/多语会牵一发而动全身。

**方案**：
- 用 Python 脚本批量将 10 个源文件的 **280 处** `QStringLiteral("中文")` /
  `QString::fromUtf8("中文")` 统一改为 `QCoreApplication::translate("LightAlbum", "中文")`
  （context 固定 `"LightAlbum"`，零运行时成本、可被 `lupdate` 抽取）。
- **排除集（关键）**：过滤/查找/比较键（category、nav、`kNav`、`.value()`、`.contains()`、
  `pv.name` 等）若一侧来自 EXIF/数据而非 UI，对应 UI 串**保持 raw 不包裹**，否则
  过滤/选择逻辑断裂。已逐项核实并保留（CullPage 845-846、GalleryPage 19-21/285、
  MainWindow 66-68/1065、MapView 927/1169、LogPage 519）。
- 翻译文件：`lupdate` 抽取 **249** 条源串 → 策展英译字典填充（含 HTML 实体转义、emoji
  保留、`%1` 占位符完整）→ `lrelease` 编译出 `i18n/LightAlbum_en.qm`（25550 字节）。
- 资源嵌入：新建 `i18n.qrc` 将 `LightAlbum_en.qm` 编为 `:/i18n/LightAlbum_en.qm`，
  在 `CMakeLists.txt` 的 `LA_RESOURCES` 与 `resources.qrc` 并列登记，AUTORCC 嵌入 exe
  → Headless 门禁随取随用、零外链。
- 启动加载：`main.cpp` 在 `Dark::apply` 之后、`MainWindow` 构造之前，按
  `LA_I18N_LANG`（强制覆盖）或 `QLocale::system()`（默认）决定加载并 `installTranslator`：
  **中文（简/繁）默认不加载翻译器 → 原生 UI 零回归**；`en` 或非中文加载英文。

### 新增 - LA_I18N_QA（第 32 门，fast 层）

强制加载 `:/i18n/LightAlbum_en.qm`，断言 4 条核心 UI 串 `translate("LightAlbum", …)`
返回英文且**不等于**源码：

- `无图像` → `No Image`
- `原图` → `Original`
- `修后` → `Edited`
- `摄影日志` → `Photo Log`

实测墙钟 **165–246ms（取 210ms）**，纯 offscreen 自退、不构造 MainWindow、无外部 `need`
依赖 → 符合 fast 层判定（< 500ms 预算），与 `LA_HIDPI_QA`（构造 QWidget、762ms）归 full
形成对照，使翻译正确性获得 PR 层覆盖。

**反例（非空心）**：`LA_I18N_QA_FORCE_BROKEN=1` 跳过翻译器加载 → `translate` 回退源码
→ 4 串全部 `FAIL got=<中文> expect=<英文>`，实测变红（`[FAIL]`），证明门是真实断言。

### 修复 / 过程

- `src/main.cpp`：补 `#include <QTranslator>` / `<QLocale>`；插入 `LA_I18N_QA` 完整断言门
  （仿 `LA_HIDPI_QA`，位于 `int main` 分支前）；正常启动段加入翻译器加载块（`Dark::apply`
  之后、`MainWindow` 之前）。
- `native/i18n.qrc`（新增）：嵌入 `LightAlbum_en.qm` 为 `:/i18n/LightAlbum_en.qm`。
- `CMakeLists.txt`：`LA_RESOURCES` 追加 `i18n.qrc`；`VERSION 2.9.0 → 2.10.0`。
- `gate_matrix.ps1`：登记第 32 门 `LA_I18N_QA`，`tier="fast"`、`measuredMs=210`。**纯 ASCII**。

### 门数

31 → **32** 门。fast 层 9 → **10** 门。回归：fast 层 10 门全绿；`LA_I18N_QA` 绿态
`PASS entries=4`、红态 `FAIL` 4 串全中；中文默认 `LA_I18N_LANG=zh` 不加载翻译器且
`LA_LUMEN_SMOKE` 仍 PASS（零回归）。

## [2.9.0] - 2026-10-03

### 变更 - 高 DPR 渲染（方向 F 第一步）

**问题**：取证 `src` 内 `devicePixelRatio` 匹配数 **0** —— 全部自定义 `QImage` /
`QPixmap` 都以「逻辑像素」烘焙（典型 `viewPix_ = QPixmap(size())`、
`footDotPix_ = QPixmap(D, D)`、离屏 `QImage img(size(), ...)`）。在 125% / 150% /
200% 系统缩放下，OS 会对位图做**最近邻拉伸** → 地图/足迹点/静态层模糊发虚。

**方案**：新增 `native/src/ui/HiDpi.{h,cpp}` 作为**高 DPI 烘焙唯一真源**，把一切
「按 `size()` 烘焙位图」收敛到一处，统一 `设备像素尺寸 = 逻辑尺寸 × dpr` +
`setDevicePixelRatio(d)`，绘制仍用逻辑坐标 → 缩放后清晰。`MapView` 两处接入：

- 下钻静态层 `bakeViewPix`：`viewPix_ = HiDpi::bakePixmap(size())`（原 `QPixmap(size())`）；
- 足迹点精灵：`footDotPix_ = HiDpi::bakePixmap(QSize(D, D))`（原 `QPixmap(D, D)`，`D=120`）。
- `worldPix_`（3× 超采样）不涉及 devicePixelRatio，无需改。

`HiDpi::dpr()`：优先读 `LA_HIDPI_DPR` 覆盖（门禁/Headless 用，模拟指定 DPI 显示器），
否则走 `QGuiApplication::primaryScreen()->devicePixelRatio()`（已含 `QT_SCALE_FACTOR`），
无 GUI 实例回退 1。

### 新增 - LA_HIDPI_QA（第 31 门，full 层）

三断言：**A 机制**（`HiDpi::dpr()==2`）、**B 工具**（`HiDpi::bakeImage(1200,800)`
设备像素宽 == 2400 且 dpr == 2，唯一真源）、**C 产品**（真实 `MapView` 进入 china
大区烘焙静态层后 `viewPix_` 设备像素宽 == 逻辑宽 × 2，端到端依赖本修复）。

门内 `qputenv("LA_HIDPI_DPR","2")` 模拟 200% 显示器，与 offscreen 平台
`devicePixelRatio` 不稳定（被本机 200% 真显接管变 4）解耦 → 门禁**可复现**。

**关键实测修正（本轮最重要）**：首版断言 `qaViewPixDeviceWidth` 写成
`viewPix_.width() * viewPix_.devicePixelRatio()`，而 `QPixmap::width()` **已返回
设备像素**（实际缓冲宽），再乘 dpr → 2400×2 = 4800 假红。修复后直接返回
`viewPix_.width()`（设备像素）。根因是**测试侧双乘**，产品烘焙一直正确。

**反例（非空心）**：`LA_HIDPI_QA_FORCE_LQ=1` 让 `bake*` 故意返回逻辑尺寸
（`dpr=1`、`width()==逻辑宽`）→ B/C 必红。实测：
`LA_HIDPI_QA FAIL B imgDevW=1200 expected=2400 B imgDpr=1 ... C viewPixDevW=1200 ...`。

### 修复

- `src/ui/HiDpi.{h,cpp}`：新增，高 DPI 烘焙唯一真源（含 `LA_HIDPI_DPR` 覆盖与
  `LA_HIDPI_QA_FORCE_LQ` 反例分支）。
- `src/ui/MapView.cpp`：`bakeViewPix` 改用 `HiDpi::bakePixmap(size())`；`footDotPix_`
  改用 `HiDpi::bakePixmap(QSize(D,D))`；暴露 `qaBakeViewPix()` / `qaViewPixDeviceWidth()`
  （修正双乘）。
- `src/main.cpp`：引入 `ui/HiDpi.h`；在 `LA_EXIT_PROBE` 之后插入 `LA_HIDPI_QA` 完整
  断言门（强制 `LA_HIDPI_DPR=2`）。
- `CMakeLists.txt`：将 `HiDpi.{h,cpp}` 接入 `LA_SOURCES`。
- `gate_matrix.ps1`：登记第 31 门 `LA_HIDPI_QA`，`measuredMs=762`，落入 full 层
  （构造 QWidget 并离屏渲染，且 > 500ms 快层预算；无外部 `need` 依赖）。**纯 ASCII**。

### 门数

30 → **31** 门。全量回归两次：`PASS=29 FAIL=0 CHECK=0 SKIP=1 KILL=0`
（`gates=31`，墙钟 ~76s，`VERDICT: GO`）。
`LA_AGENT_SMOKE` 首次全量跑曾 `no-exit-request`（20109ms 略超 20s 预算），隔离复跑
两次均 `PASS`（19133ms / 3136ms），判定为**负载下偶发超时**，与本改动无关（改动不触及
agent 路径）。所有 `LA_MAP_*` 门保持 `[OK]`。

## [2.8.0] - 2026-10-03

### 变更 - 门禁矩阵化（方向 E 第三步）

**问题**：门表内联在 `qa_runner.ps1` 里，30 门在每个 PR 上全量跑。2026-10-03 实测
基线：**全量 30 门 = 73.6s 墙钟**，而门耗时的分布是天然两极的：

| 类别 | 门数 | 单门耗时 | 共因 |
|---|---|---|---|
| 纯逻辑门 | 9 | 56 - 250 ms | `main()` 早期 return，不构造 MainWindow |
| 构造 / fixture 门 | 21 | 338 - 8662 ms | 构造 MainWindow 或载入 6425 张 fixture |

也就是说 77% 的门只占 2% 的时间，而 68% 的 CI 时间花在少数几个必然要在 main 上
跑的门上。PR 每次都付这 73.6s，而其中绝大部分与"这次改动有没有打破东西"无关。

**方案**：新增 `native/gate_matrix.ps1`，作为**门表与分层的唯一真源**。
`qa_runner.ps1` 改为 dot-source 消费它，不再内嵌门表（此前若要在两处分层，必然
出现"门在一处、层在另一处"的漂移）。

**分层语义（fail-safe）**：

- `tier="fast"` —— 实测廉价，**每个 PR 都跑**；
- `tier="full"` —— 昂贵，push 到 main / tag 时跑，本地全量跑与 `release.ps1` 也跑；
- **tier 缺省 = full** —— 新门忘写 tier 只能落进最严的一层，绝不会因遗漏而
  静悄悄削弱 PR 层。

不传 `-Tier` 时是两层并集，与 2.7.0 行为逐字节一致 —— **打包门禁强度未被削弱**。

**实测收益**：fast 层 **9 门 1.74s**，全量 30 门 73.4s（**42 倍**）。

**两处刻意的排除**，都在 `gate_matrix.ps1` 头注释里写明理由：

- `LA_SCALE_PERF`（338 ms）虽快，但断言的是**墙钟预算**，放共享 CI runner 上必
  flaky。PR 层放一个会抖的门，等于教人无视红色 —— 比慢门更糟。
- `LA_RW_SMOKE`（1017 ms）临界，留在严层，让 vault / 加密往返只有一处能坏。

### 新增 - LA_GATE_MATRIX_QA（第 30 门，18 条断言）

它**住在 fast 层**：矩阵的看门狗自己必须在 PR 层被看。

1. E1 真表通过结构校验；E2 两层互斥且并集为全表；
2. E3 **fail-safe 实测**：剥掉所有 tier 后 fast 层为 0、full 层为 30；
3. E4 严层必须真的装着 ≥2000ms 的贵门（18 个）；
4. E5 fast 层无脚本门（矩阵自身除外）、无外部 env 依赖门；
5. E6 **成分而非比例**：fast 层每门 `measuredMs ≤ 500ms` 预算；
6. E7 5 个核心不变量门**必须**留在 fast 层；
7. E8 每门必须带 `measuredMs`（否则分层就成了猜）；
8. E9a-g 七组同源反例；E10 未知 tier 必须抛异常而非静默全跑。

### QA - 反例实测 6/6 变红（并因此改了两条断言）

外部脚本 `_ce324.py` 逐个注入真实缺陷、走**真 runner**、断言判红：

| 反例 | 注入 | 结果 |
|---|---|---|
| CE1 | 核心门 `LA_THUMB_CACHE_QA` 悄悄退出 PR 层 | RED |
| CE2 | 重复门名 | RED |
| CE3 | 外部 env 依赖门被拖进 fast 层 | RED |
| CE4 | 昂贵门（7.4s）被降级进 PR 层 | RED |
| CE5 | 混入未知 tier 值 `fas` | RED |
| CE6 | `measuredMs` 被吞进带引号的 `scriptArgs` | RED |

**首轮只有 3/5 变红，暴露了两条真盲区**：

1. **原 E6 只卡"fast 占比不超过一半"的比例**。把 3640 ms 的地图门塞进 fast 层
   后 fast=10/30，仍"不到一半" → 依然绿。**比例规则根本看不见成分退化**。已改为
   成员资格规则（成本预算 E6 + 核心门 E7）。
2. **CE1 最初写成"删掉一个 tier key 应当变红"，它保持绿色是正确的** —— 无 tier 的门
   本就该落严层。真正暴露的问题是：**没有任何规则能察觉核心门离开了 PR 层**。
   由此新增 E7 与其反例 E9f。

**同轮修掉一个真实数据损坏**：给门表批量追加 `measuredMs` 时，字段被拼进了
`LA_RELEASE_QA` 的**带引号 `scriptArgs` 字符串内部** ——
`scriptArgs="-SelfTest; measuredMs=3135"`，键消失、参数被改。E8 就是为这类"静默
吞字段"而设的。**教训：批量改结构化行必须逐行断言形状，不能只断言行首前缀。**

### 修复

- `qa_runner.ps1`：新增 `-Tier fast|full`、`-Timing`；门表改为消费 `gate_matrix.ps1`；
  摘要与 JSON 增加 `tier` / `totalMs` / 每门 `ms`。
- `iterate.ps1`：新增 `-Tier` 透传；Markdown 报告增加层级、墙钟、每门 ms 列。
  没有这一层，读者会把 9 门的 fast 报告误读成全套通过。
- `.github/workflows/qa.yml`：`pull_request` → fast 层，其余触发 → full 层，
  选择步骤写入 `$GITHUB_ENV`，层级出现在 CI 日志与 artifact 名前缀里。

### 门数

29 → **30** 门。全量回归：`PASS=29 FAIL=0 CHECK=0 SKIP=1 KILL=0`，
`tier=all gates=30`，墙钟 73379ms，`VERDICT: GO`。
（`LA_SCAN_SMOKE` 仍因缺 `LA_SCAN_ROOT` / `LA_SCAN_DB` 自动 `[SKIP]`。）

## [2.7.0] - 2026-10-03

### 修复（门禁自退时序缺陷 —— 此前被误记为「环境噪声」的真缺陷）
- **`LA_CULL_SMOKE` 永久挂死根因**：`CullPage::onFinished()` 的自测分支在**构造期**同步调用 `QApplication::quit()`，而 `app.exec()` 尚未进入。Qt 的 `quit()` 只给「当前事件循环」发退出信号，没有事件循环时它是 **no-op** —— exe 打印完所有断言后照常进入 `app.exec()` 永久空转，门禁表现为 `[KILL]`。历史上被归入「控制台依赖 / 环境族」，本次用最小探针**实证**为代码缺陷。
- 最小复现（探针对照实测，非推断）：`quit()` 在 `exec()` **之前**调用 → `RC=124` 挂死；在事件循环**内**（timer）调用 → `RC=0` 正常退出。
- 修复效果：`LA_CULL_SMOKE` 从 `RC=124` 挂死（曾需 25s 超时被 kill）变为 `RC=0` / 3.9s 自退，输出逐字节一致（250 bytes）。
- 同类审计：全库 9 处 `QApplication::quit()` **逐一核实时机**，确认仅 `CullPage.cpp:1003` 一处违规（其余 8 处本就在事件循环内）。为防复发，9 处**全部**收敛到新契约。

### 新增（自退契约 `src/core/qaExit.{h,cpp}`）
- `QaExit::requestExit(code)`：**幂等**、**无条件** `QTimer::singleShot(0,...)` 排队退出。铁律：不判断「是否在事件循环内」再决定同步/异步——Qt 无公开 API 判断该状态，猜错就是挂死；无条件排队则构造期注册也会在 `exec()` 启动后触发，事件循环内调用仅延迟一个事件迭代（~0ms），**一条路径覆盖两种时机**。
- `QaExit::armWatchdog()`：仅在检测到 `LA_*` 门禁变量时挂载（`activeGate()` 返回空串即直接 return，**真实交互启动绝不挂、绝不杀用户进程**）。超时未退出 → 打印 `LA_WATCHDOG FAIL gate=<门名> budget_ms=<ms> reason=no-exit-request` 并 `_exit(3)`。默认预算 20s，**必须 < runner 的 `-Timeout` 25s**，保证看门狗先于 runner 超时触发，从而拿到归因而非无信息的 `[KILL]`。
- `QaExit::activeGate()`：优先读 `LA_GATE`（运行器显式设），回退扫描环境变量找第一个非辅助 `LA_*`（**排除清单**而非白名单——两者都靠人工维护但失效方向相反：白名单漏新门禁→看门狗不挂→回到静默挂死；排除清单漏新辅助变量→仅归因消息不好看，退出行为不受影响）。
- 全库 `grep "QApplication::quit"` 现在仅在 `qaExit.cpp` 实现内部与注释中命中，`src/` 下裸调用为 0。

### QA（新增 `LA_EXIT_QA`，9 断言全绿 + 同源反例）
- 设计要点：门禁**自身**用 `QProcess` 拉起同一 exe 的三种子进程模式（`LA_EXIT_PROBE=early|deferred|hang`），把「Qt 的 quit 时序语义」这一外部事实**固化为可断言契约**。
- (A) 契约 2 项：`early`（exec 前裸 `quit()`，**预期挂死**）必须 `waitForFinished` 返回 false；`deferred`（`QaExit::requestExit`）必须 `rc=0` 且带 `deferred PASS` 标记。
- (B) **同源反例 E4**：断言两模式 `finished` **必须不同**。事故的整个教训就是「两种时序行为不同」；若哪天变成相同，契约即失去意义 → FAIL「assertion is vacuous」。
- (C) 看门狗 4 项：故意不请求退出时，必须在预算内以 `rc=3` 退出，且归因行同时含 `LA_WATCHDOG FAIL`（E7）与 `gate=<门名>`（E8，**指名道姓**，而非笼统「有个门挂了」）。
- (D) 纯函数 2 项：`activeGate()` 在本进程应返回 `LA_EXIT_QA`（E9）；`requestExit` 连调三次仍只触发一次退出（幂等）。
- **反例有效性已实测（双重验证，防断言空心）**：把 `requestExit` 实现改回同步 `QCoreApplication::exit(code)`（精确复现事故形态）后重建 → ① `LA_EXIT_QA` `RC=1` RED，`fails=2`（E3 + E4 双重命中）；② 真实门 `LA_CULL_SMOKE` 被看门狗抓住，`RC=3` + `LA_WATCHDOG FAIL gate=LA_CULL_SMOKE`。随后恢复正确实现。
- **本门在设计过程中当场抓到自身的第二个盲区**：看门狗原挂在 `MainWindow` 构造前，而 `LA_EXIT_PROBE` 子进程入口在 `main()` 顶部就 `return app.exec()`，走不到挂载点 → E5 报「watchdog never fired」。据此把挂载点前移到 `QApplication` 构造之后、任何分支之前。**这正是「先写断言再实现」的价值：盲区是门禁自己发现的，不是靠复盘。**
- 门数 26 → **27**；运行器新增设 `LA_GATE`，使看门狗归因能指名门名。

### 门数
- 26 → **27** 门。历史遗留的 `LA_CULL_SMOKE [KILL]`（此前被当作「已知环境族」放过）现已修复为正常绿门。

## [2.6.0] - 2026-10-03

### 功能（导入 iCloud 相册）
- 顶栏「＋」导入钮改为**下拉菜单**：`导入目录…` / `导入 iCloud 相册` / （分隔线）/ 探测状态行。空库空态同步给出并排双按钮——新用户第一次接触应用的入口不能把 iCloud 藏在下拉里。
- 新增 `src/core/ICloudSource.{h,cpp}`：iCloud 本地落盘根**多候选探测**（`iCloud Photos/Photos` → `iCloud Photos` → `iCloud Drive/Photos`，相对 `QStandardPaths::PicturesLocation`）。写死一条会在别的机器上静默「导入 0 张」，故按优先级取第一个真实存在者。
- **空壳拒绝**：`detectRoot()` 要求候选目录里至少有一个媒体文件，只有 `desktop.ini` 的空壳不算命中——避免虚假「已找到 iCloud」。探测不到时菜单项**禁用**并说明原因，绝不静默导入 0 张后报「完成」。
- 确认弹窗展示真实根路径 + 真实文件数，并说明「云端未下载的占位文件会跳过，在 iCloud 下载完成后重跑即可增量补齐」。
- **写入路径不新建**：iCloud 与普通目录两个入口都收敛到新抽出的 `runImportInto(dir, title)`，缩略图缓存键、增量规则（size+mtime 未变则跳过）、missing 标记语义完全同源（铁律：缓存/写入入口必须唯一）。

### 取证（先测再改，本机实测而非推断）
- iCloud 落盘形态：`%USERPROFILE%\Pictures\iCloud Photos\Photos` 实测存在，**平铺** 6778 个文件（仅 1 个子目录 `untitled.afcatalog`），不按相册分目录。扩展名分布：`.jpg` 5088 / `.heic` 777 / `.png` 561 / `.mp4` 120 / `.mov` 102 / `.dng` 58 / `.arw` 39。
- **基线先行**：改动前先用现有 `LA_SCAN_SMOKE` 门跑 iCloud 风格沙盒，得 `added=6 placeholders=2 total=8` —— 证明 `Scanner::syncRoot` **本就能正确处理 iCloud 目录**（含占位符跳过）。故本次不动扫描器核心，缺口在**入口可发现性**。
- **格式支持实测**（自建 `QImageReader` 探针，逐格式 read() 实测，禁猜）：`.jpg` ok 4656×3496；`.heic` **ok 3840×2160**（Qt 能解，白名单已有）；`.png` ok；`.dng`/`.arw` **NULL `Unsupported image format`**；`.mp4`/`.mov` 同。→ RAW 与视频**不纳入**白名单（纳入只会产出空缩略图卡片），`.dng/.arw` 保持被 `kImgExt` 静默忽略。
- 占位符判定为**两条独立判据**（实测覆盖）：`< 24KB`（`isIcloud` 时）与 `.icloud` sidecar / `RECALL_ON_OPEN` 文件属性。门禁对两条分别构造样本。

### QA（新增 `LA_ICLOUD_QA`，15 断言全绿 + 反例）
- (A) 探测纯函数 8 项：候选数=3、首选路径、默认 PicturesLocation 非空、命中沙盒根、文件数=9、**空壳拒绝**、根下 true / 无关目录 false。
- (B) 端到端 `syncRoot` 5 项：`total=5`（3 png + 2 占位；DNG/AAE/ini/afcatalog 全部不计）、`added=3`、`placeholders=2`、`errors=0`、**幂等** `added=0 unchanged=3`。
- (C) **反例 2 项**：另建**不含 "icloud" 子串**的独立临时根跑同样内容 → `placeholders=0 added=4`，与正例 `(2, 3)` 显著不同；C2 断言正反例**必须不同**，若相同则 B3 是空心断言。实测踩到两个坑并修掉：① 1px PNG 仅 ~70 字节，会被 `isIcloud && size<24KB` 判成占位（`placeholders=6 added=0`）→ 改用撑到 ~30KB 的合法 PNG；② 反例根若沿用 `la_icloud_qa` 沙盒名，`contains("icloud")` 路径子串判定同样命中 → 反例自身失效（`placeholders=1`）→ 反例根改用 `la_plainctl_qa`。
- 门数 25 → **26**。

### 修复
- **★ 启动性能：iCloud 探测改懒构建**。`buildImportMenu()` 原在顶栏构造期**同步**调 `detectRoot()`，而 `detectRoot()` 会对候选目录做 `entryInfoList`（本机实测 iCloud 目录 **6778 个 `QFileInfo`**）—— 等于把这笔 IO 压进 UI 启动主线程。改法：构造期只建菜单骨架（**零 IO**，「导入 iCloud 相册」先置灰 + 状态行显示「正在检测…」），首次 `QMenu::aboutToShow`（用户已点开、正在等）时才探测并启用。
- `isUnderICloudRoot()` 首轮用 `p` 与 `path` 自比（恒真）→ 改为纯「逐级上剥目录链比对候选根」。
- `title` 未被 `QFutureWatcher::finished` lambda 捕获 → 编译错误 `'title' is not captured`，已加入捕获列表。

## [2.5.0] - 2026-10-02

### 工程（方向 E·发布 CI 强化：第二步 依赖与产物溯源 SBOM + 可复现性验证）
- **取证：发布链路存在 3 个真实缺陷**（本步把「能打包」推进到「产物可溯源、构建可复现」）：
  1. **许可证合规缺口（真实缺陷）**：`docs/THIRD_PARTY_NOTICE.md` 只声明了 Python 侧（历史资产）的来源，而当前优化重心 `native/src/core/CullEngine.cpp/.h` 是从 `ai-photo-manager`（MIT）派生的 pHash / DCT-II / Hamming 近邻去重实现 —— `grep -rn "MIT License|Copyright (c)|SPDX|third-party" native/src/` 命中 **0 行**，两个源文件零版权声明；更严重的是 **dist 内完全没有 license/notice 文件**，MIT 要求的版权声明随二进制分发根本没被满足。NOTICE 文件写着「已归属」，代码里却可能随时被摘掉声明且无人察觉。
  2. **产物不可溯源（SBOM缺失）**：zip 里只有 exe + Qt 运行库 + 手册 + MANIFEST，**无法回答「这个包是哪次提交、哪个 Qt、哪个 GCC 编出来的」**。版本号只出现在文件名里，包内无任何构建元数据。
  3. **★「可复现」声明是假的（locale-dependent 排序）**：`New-ReleaseManifest` 原用 `Sort-Object FullName`，而 PowerShell `Sort-Object` 默认是**区域文化序（culture-sensitive, locale-dependent）**而非序数序。实测证据：同一批文件 `Sort-Object` 排成 `a_b.txt | ab.txt | a-b.txt | zz.txt | Zz.txt`（`zz` 在 `Zz` 前），而 `StringComparer::Ordinal` 排成 `Zz.txt | a-b.txt | a_b.txt | ab.txt | zz.txt`。后果：同一棵文件树在 zh-CN / en-US Windows 上 MANIFEST 顺序不同 → 跨机器 diff 全是噪声，两次构建「不一致」其实是 locale 差异而非产物差异。
- **修复 3（序数排序）**：`New-ReleaseManifest` 改用 `[Array]::Sort($paths, [System.Comparison[System.Object]] { param($a,$b) [System.StringComparer]::Ordinal.Compare($a.rel, $b.rel) })`，并在沙盒里放入 `Zz.txt` + `zz.txt` 作为**文化序与序数序结果相反的判别对** —— 只有真正按序数排，改回 `Sort-Object` 才会转红。
- **新增 C6 `Assert-LicenseCompliance($staging, $noticeName, $srcRoot, $derivedRel)`（许可证合规）**，两半：① dist 内 `THIRD_PARTY-NOTICE.md` 存在且非空；② 声明的每个派生源文件**前 12 行内**必须有 `// SPDX-License-Identifier:` 头 —— 防 NOTICE 文件继续声称「已归属」而代码里的声明被悄悄摘掉。
- **新增 `BUILDINFO.txt`（SBOM / 构建溯源）**：由 `New-BuildInfo($staging, $name, $version, $srcRoot)` 生成，**纯 ASCII**，写入 `version / git_commit / build_type / qt_version / qt_dir / compiler / compiler_version / packaged_at / packaged_by / packaged_on_host / file_count / total_bytes / note`。**全部字段从既有文件读出，无任何手工同步项**：`build/CMakeCache.txt`（`CMAKE_BUILD_TYPE` / `CMAKE_CXX_COMPILER` / `Qt6_DIR`）、`git rev-parse --short HEAD`、`g++ --version`。生成顺序为 **trim 之后、MANIFEST 之前**（trim 改变文件集，MANIFEST 必须覆盖 BUILDINFO）。
- **BUILDINFO 被纳入 C3 断言**：`$need` 增至 `@("LightAlbum.exe", "platforms/qoffscreen.dll", "platforms/qwindows.dll", $BUILDINFO_NAME)` —— **不可追溯的 zip 不是可出货产物**。由此真实路径顺序调整为：C2/C6 在 3.7 步跑，BUILDINFO 生成后（4.5）才跑 C3（4.7）。
- **新增 C7 `Assert-ManifestReproducible($staging, $name)`（可复现性）**，两半：
  - 半 (a) **先读磁盘上现有 MANIFEST**，校验条目按 `StringComparer::Ordinal` 严格升序且 path 无重复。**顺序至关重要**：若先重生成再校验，会把被篡改/乱序的 manifest「修好」，注入的缺陷永远抓不到（这是本轮实测踩到并修复的**设计缺陷**：C7 初版自证清白，10 个注入里唯独 C7 的两条 100% 漏抓）。
  - 半 (b) 连续两次 `New-ReleaseManifest` 逐字节比对（`h2 -eq h3`），并与 on-disk `h1` 一致。
- **C7 的反例设计方法论**：「两次生成是否字节一致」无法被确定性注入（同树枚举两次必然相同），故额外引入**产物级不变量**「序数严格升序 + 无重复」作为可确定性注入的失败源：注入 ① `[array]::Reverse($ent)` 制造乱序；② 重复首条 entry。
- **SPDX 头落地**：`native/src/core/CullEngine.cpp`（+19 行）/`.h`（+3 行）加**纯 ASCII 英文** SPDX 块，含上游 URL、MIT 归属、派生点说明（pHash / DCT-II / Hamming 去重）、指向 `THIRD_PARTY-NOTICE.md`，并写明「删除此头会违反 C6 并使 `LA_RELEASE_QA` 失败」。`docs/THIRD_PARTY_NOTICE.md` 补全 C++/Qt 原生侧（能力表逐行补 `CullEngine::` 对应符号、新增第 5 条改造说明、新增「随包分发」章节）。
- **契约自检从 5 条扩到 7 条，注入从 5 条扩到 10 条**：C1×1、C2×1、C3×2（含删 BUILDINFO）、C4×1、C5×1、C6×2（含删 NOTICE / 剥 SPDX 头）、C7×2（含乱序 / 重复 entry）。**沙盒增强**：写 `THIRD_PARTY-NOTICE.md`、造假带 SPDX 头的 `src/core/CullEngine.cpp/.h`、调 `New-BuildInfo` 生成沙盒 BUILDINFO、放入 `Zz.txt` + `zz.txt` 判别对。
- **实测**：
  - `LA_RELEASE_QA PASS contracts=7 injected=10 caught=10 baseline=7 zip=9`（EXIT=0，10 个注入缺陷全部被对应契约抓住）。
  - **反例 A（断言非空心）** `release.ps1 -SelfTest -NoInject` → `FAIL reason=NOINJECT contracts=7 ... -- contract assertions unproven`（EXIT=1）。
  - **反例 B（真实 C6）**：用 Python 字节级精确删除 `CullEngine.cpp` 的 SPDX 行（`removed 1 line(s)`，`grep -c` = 0）后跑真实打包 → `throw C6 no SPDX-License-Identifier header in derived source: src/core/CullEngine.cpp`，**打包在 C6 被阻断**（EXIT=1）；已还原并核验（391 行 CRLF、SPDX 计数 1、无残留 .bak）。
  - **反例 C（真实排序缺陷）**：把序数排序改回 `Sort-Object` → `baseline:C7 did not pass on a clean sandbox`（EXIT=1）；还原后 EXIT=0 —— **证明 C7 真能抓住 locale 缺陷**。
  - **真实打包冒烟** `release.ps1 -SkipGate`：`C1 ok (2.5.0)` / `C2/C6 ok: manual present, license attribution shipped` / `SBOM: BUILDINFO.txt (14 lines)` / `C3 ok: exe + qoffscreen + qwindows + BUILDINFO all present and non-empty` / `MANIFEST: 30 files, self-verified, reproducible=E26AA669099B` / `C5 ok 17.2 MB, 31 entries` / `RELEASE OK`。（`dxcompiler.dll` 缺失 warning 属既有无害告警。）
  - **BUILDINFO 真实取值核验**：`version 2.5.0` / `git_commit e581d87` / `build_type Release` / `qt_version 6.8.3` / `qt_dir D:/qt/6.8.3/mingw_64/lib/cmake/Qt6` / `compiler D:/qt/Tools/mingw1310_64/bin/g++.exe` / `compiler_version ... 13.1.0` / `packaged_at 2026-10-02T23:20:54` / `packaged_by damin` / `packaged_on_host LUC` / `file_count 29` / `total_bytes 42219170`。（首轮 `qt_version : unknown` 系 `Get-QtVersionFromCache` 误用 `Select-String -Path $qt6Dir`（该参数是文件路径而 Qt6_DIR 是目录值）→ 静默返回空，已改用 `[regex]::Match` 修好。）
  - **MANIFEST 真实内容核验**：可解析条目 `grep -c "^[0-9a-f]{64}"` = 30；含 `BUILDINFO.txt`(525 B) 与 `THIRD_PARTY-NOTICE.md`(4869 B) 两条；首行序数为 `BUILDINFO.txt` 在 `D3Dcompiler_47.dll` 之前。
  - **产物独立可跑**：`cd native/LightAlbum-*-win64 && export QT_QPA_PLATFORM=offscreen LA_SMOKE=1 && ./LightAlbum.exe > f 2>&1` → `EXIT=0`、`174` 字节、`LA_P1_SMOKE_OK photos=6425 years=8 provs=11 span=2019-2026`（脱离 `build/` 目录）。
  - **Runner 端到端**：`qa_runner.ps1 -Only LA_RELEASE_QA` → `LA_RELEASE_QA [OK]`、`RUNNER_EXIT=0`、JSON `{"summary":{"skip":0,"pass":1,"fail":0,"check":0},"gates":[{"detail":"","mark":"[OK]","name":"LA_RELEASE_QA"}]}`。
- **纯 ASCII 铁律自纠**：`Get-QtVersionFromCache` 注释里的中文省略号 `…`（`e2 80 a6`）破坏纯 ASCII 约束 → 改`...`，复核 `nonascii 0`。
- 版本 2.4.0→**2.5.0**；门数仍 25（`LA_RELEASE_QA` 由 5 契约升级为 7 契约，非新增门）。

## [2.4.0] - 2026-10-02

### 工程（方向 E·发布 CI 强化：第一步 发布契约化 + CI 覆盖修复）
- **取证：发布链路存在 6 个真实缺陷**（本步不新增用户可见功能，而是把「能打包」变成「打包可验证」）：
  1. **`$ts` 作用域缺陷**：`release.ps1` 里 `$ts` 只在 `if (Test-Path $staging)` 分支内定义，而 `-TrimNetwork` 分支也用它给裁剪文件打时间戳 → **首次打包（staging 尚不存在）时 `$ts` 为 `$null`**，裁剪产物退化为 `Qt6Network.dll_`（无时间戳），下次 trim 静默覆盖。
  2. **CHANGELOG 与版本零校验**：版本真源在 `CMakeLists.txt`，但 `release.ps1`/`iterate.ps1` 中 `CHANGELOG` 出现次数为 **0** —— 改了版本号却没写 CHANGELOG，照样产出「可发布」zip。
  3. **手册 glob 硬编码** `LightAlbum-2.0.0-*.md`：当前版本已 2.3.0，仅因手册文件名恰好还是 2.0.0 才碰巧命中；一旦手册改名即静默不打进包。
  4. **分发物无清单**：zip 打完只打印体积，无任何 manifest —— 分发完整性不可校验、两次构建不可比对。
  5. **CI 覆盖漏洞**：`.github/workflows/qa.yml` 的 `paths` 只含 `src`/`resources`/`data` → **改动门禁运行器本身（`qa_runner.ps1`/`release.ps1`/`iterate.ps1`）完全不触发 CI**，门禁可被悄悄削弱而无人察觉。
  6. **门禁判定无留痕**：结论只在 runner 日志里，`iterate.ps1` 的 Markdown 报告写在 `TEMP` 的时间戳文件中，无人可打开。
- **5 条发布契约（C1-C5）抽为接受显式路径的独立函数**：C1 `Assert-VersionSync`（CHANGELOG 顶部 `## [x.y.z]` 必须 == CMakeLists `project(VERSION)` == 待打包版本）、C2 `Assert-ManualPresent`、C3 `Assert-CoreArtifacts`（`LightAlbum.exe` + `platforms/qoffscreen.dll` + `qwindows.dll` 非空）、C4 `Assert-ManifestConsistent`、C5 `Assert-ZipIntegrity`。**这些契约在真实打包路径里真的执行**（抛错即中止），同时被 `-SelfTest` 在一次性沙盒上驱动 —— **生产代码零 seam、无测试专用分支**。
- **`MANIFEST.txt`**：staging 内生成 `sha256 <TAB> 字节数 <TAB> 相对路径`清单，**在 trim 之后生成**以描述真正出货的内容；随包分发。
- **新增第 25 门 `LA_RELEASE_QA`（脚本门）**：`qa_runner.ps1` 新增 `kind="ps1"` 门类型（跑脚本而非 exe，KILLED 文案区分；**不依赖 exe 自退**，故不受「真实控制台」约束）。`-SelfTest` 在临时沙盒建假 CMakeLists/CHANGELOG/dist/zip，先确认干净基线 5 条全绿，再**逐个注入 5 类缺陷**并断言对应契约是唯一被抓到的那条。
- **CI 加固**：`paths` 补 `native/*.ps1`、`native/docs/**` 及 workflow 自身；新增 `Collect machine-readable gate result`（把 `TEMP` 下时间戳 JSON 拷进工作区）+ `upload-artifact` 留痕；新增**串行 `package` job**（`needs: qa`）在每次 push 真跑 `release.ps1` —— 打包器从此不再只在人的机器上运行。
- **脚本编码治理**：`release.ps1` 原含 452 个非 ASCII 字节且无 BOM → PS 5.1 按 GBK 解析，中文字符串必然乱码。**全脚本转纯 ASCII**（注释亦然），与 `qa_runner.ps1`/`iterate.ps1` 对齐。
- **实测**：
  - `LA_RELEASE_QA PASS contracts=5 injected=5 caught=5 baseline=5 zip=6`（EXIT=0，5 个注入缺陷全部被对应契约抓住）。
  - **反例** `release.ps1 -SelfTest -NoInject` → `FAIL reason=NOINJECT contracts=5 injected=0 caught=0 -- contract assertions unproven`（EXIT=1）；经 `qa_runner` 判定为 `[FAIL]`（端到端非空心）。
  - **真实 C1 反例**：把 CHANGELOG 顶部改为 `2.9.9` → `C1 version mismatch: CMakeLists=2.3.0 CHANGELOG=2.9.9`，打包**前**即中止（EXIT=1），文件已还原。
  - **真实打包冒烟** `release.ps1 -SkipGate`：`C1 ok: CHANGELOG top entry matches CMakeLists (2.4.0)` / `C2/C3 ok` / `MANIFEST: MANIFEST.txt (28 files, self-verified)` / `C5 ok: ...LightAlbum-2.4.0-win64.zip (17.2 MB, 29 entries)` / `RELEASE OK`。
  - **产物独立可跑**：在 dist 目录内 `QT_QPA_PLATFORM=offscreen LA_SMOKE=1 ./LightAlbum.exe` → `LA_P1_SMOKE_OK photos=6425 years=8 provs=11 span=2019-2026`（EXIT=0，脱离 `build/`）。
  - `iterate.ps1 -NoBuild -Only LA_RELEASE_QA` → `ITERATION VERDICT: GO`；workflow YAML 校验通过（`qa` 8 步 → `package` 7 步 `needs: qa`）。
  - 回归：`LA_GALLERY_BATCH_QA` / `LA_BATCH_TX_QA` / `LA_THUMB_CACHE_QA` / `LA_SMOKE` / `LA_SCALE_SMOKE` / `LA_SCALE_PERF` / `LA_GALLERY_E_QA` 全绿。
- 版本 2.3.0→**2.4.0**；`qa_runner` 24 门 → **25 门**。

## [2.3.0] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第五步 b 图库多选 + 批量工具条）
- **交付「批量操作」的用户可见入口**（2.2.9 只建了原子批量写层，本步接线到 UI，方向 D至此完整闭环）。
- **`EditorialView` 多选状态机**（沿用主流图库语义，**不破坏**既有单击开灯箱）：
  - 无修饰 → 单选（清多选集）+ 打开灯箱；**Ctrl** → 切换该行选中；**Shift** → 以焦点行为锚点做范围选择。
  - `GalleryTile::activated` 改为携带 `Qt::KeyboardModifiers`，点击统一走`clickRow(row, mods)`；多选手势**不触发灯箱**。
  - 选中态改由 `selRows_`（`QSet<int>` 模型行号）驱动，池化瓦片重绑（`updateWindow`）与 `refreshSelectionVisual()` 共用同一判定——虚拟化下滚出再滚回选中态不丢。
  - 新增 API：`selectedRows`/`selectedPaths`/`selectAll`/`clearSelection`/`toggleRow`/`clickRow`；新增信号 `selectionChanged(count)`。
- **`GalleryPage` 批量工具条**：位于计数与视图之间，**选中为空时整条隐藏**（不占视觉空间）；含 `已选 N 张` + `★1..★5` + `🔒 私密` + `取消选择`。计数文案追加「已选 N」注记。
- **架构铁律：视图层不持有写能力**——`GalleryPage` 只有 `const QVector<Photo>*`（只读借用），按钮只发 `batchRatingRequested` / `batchPrivateRequested` 信号，**库写入一律在 `MainWindow`（持 `lib_`）执行**，复用 `Library::setRatings`/`setPrivates` 的单事务原子语义；结果以 `QMessageBox` 反馈「请求 N 张 / 成功 M 张 / 已原子提交 或 已整体回滚: 原因」。批量私密后额外 `setHidePrivate(hidePrivate())` 重跑隐私过滤。
- **新增第 24 门 `LA_GALLERY_BATCH_QA`**（headless 自建临时库，不碰真实 db）：断言 ① 工具条初始隐藏/有选中显示/取消回隐 + 5 个星级按钮 ② Ctrl 累加与再点取消，**且不触发灯箱** ③ Shift 范围（2→5 得 4 张、反向 2→1 得 2 张）④ 无修饰点击必触发灯箱（既有行为守护）⑤ 越界 row 不改变选中 / 全选 / 取消边界 ⑥ 批量 signal 载荷（paths 数与评分值正确、触发后自动清选中）⑦ **端到端**：signal → `Library::setRatings` → DB 与内存副本一致（各 3 张 rating=4）。
- **实测**：
  - `LA_GALLERY_BATCH_QA PASS rows=8 ctrl=3 range=4 lamp=1 selectAll=8 stars=5 ratingSig=1 privSig=1 e2e=3 bar=1`（EXIT=0）。
  - **反例** `LA_BATCH_QA_LAMPASS=1`（强制多选手势也开灯箱）正确 `FAIL reason=LAMPASS: ctrl click must NOT open lightbox, but gate forced it open`（EXIT=1，非空心）。
  - 回归全绿：`LA_GALLERY_E_QA featureOK=1` / `LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=60`（51→60 为批量条 8 子控件 +1 bar）/ `LA_SCALE_PERF PASS firstMs=40 scrollMaxMs=18` / `LA_BATCH_TX_QA PASS` / `LA_THUMB_CACHE_QA PASS` / `LA_GALLERY_SEARCH_QA marks=7` / `LA_P1_SMOKE_OK` / `LA_MAP_CLICK_QA all=OK(10/10)`。
- **版本 2.2.9→2.3.0（MINOR：新用户可见功能）**；`qa_runner` 23 门 → **24 门**。至此方向 D（规模化与流畅性）六向全部落地。
## [2.2.9] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第五步批量写回事务化·第一阶段数据层）
- **取证：native 侧写回面根本没有批量入口**——`Library` 只有单条 `setRating`/`setPrivate`，唯一调用方是灯箱点星（`MainWindow.cpp:208`）。所谓「批量操作」零实现，故本步先补**原子批量写层 + 首个可用批量 API**。同时定位一个隐性复杂度问题：`findMutable` 原为**线性扫描 6425 项**，批量场景会退化成 O(N²)（6425 条 × 6425 次比较 ≈ 4×10⁷）。
- **`BatchResult` + 原子批量 API**：`setRatings(paths, rating)` / `setPrivates(paths, priv)`，单条 API 改为其特例（长度为 1 的批量）——**统一唯一事务入口，禁旁路直写**。
  - **单事务原子**：任一条 `exec` 失败或 `commit` 失败 → 整体 `rollback()`，`done` 归零并给出 `failedIndex`（首个失败下标）+ `error`（SQL 原文）。
  - **prepared statement 只prepare 一次**，循环 `addBindValue`+`exec` → 6425 条也只解析一次 SQL。
  - **内存副本仅在 commit 成功后同步**——否则中途失败会出现「DB 已回滚、内存已改」的撕裂（本步刻意防御点）。
  - 路径不存在 → `numRowsAffected()==0`，`done` 少但 `ok=true`（**不是错误**）；空批 → 成功空操作。
- **O(1) path 索引**：`pathIx_`（`QHash<QString,int>`）+ `rebuildPathIndex()`（`photos_` 排序完成后建，保证下标与最终顺序一致）+ `verifyPathIndex()`（QA 自检双向一致）；`findMutable` 改走索引，**禁线性扫描**。
- **新增第 23 门 `LA_BATCH_TX_QA`**（headless 自建临时库，**不碰真实 library.db**）：断言 ① 索引双向一致 ② 空批=成功空操作 ③ 批量打星原子成功且 DB↔内存同步 ④ 不存在路径→`done` 少但仍成功 ⑤ **失败整体回滚**（DB 与内存都保持原值、**无撕裂**——核心红线）⑥ rating 越界 `qBound` 收敛（9→5, -3→0）⑦ 批量隐私标记同语义 ⑧ 触发器注入下 `failedIndex` 定位精度。
  - 失败源用**生产代码零 seam** 的两种真实手段：默认 `BEGIN EXCLUSIVE` 独占写锁（UPDATE 必SQLITE_BUSY）；`LA_BATCH_TX_QA_FAIL_AT=<n>` 用 QA 自建 `BEFORE UPDATE ... RAISE(ABORT)` 触发器在第 n 条精确失败。
- **实测**：
  - `LA_BATCH_TX_QA PASS rows=6 failAt=-1 idx=1 empty=1 atomic=1 rollback=1 clamp=1 priv=1`（EXIT=0）。
  - `LA_BATCH_TX_QA_FAIL_AT=3` 触发器精确注入 → `PASS ... failAt=3`（EXIT=0，断言`failedIndex==3` 且整体回滚）。
  - **反例** `LA_BATCH_TX_QA_NOLOCK=1`（不制造失败源）正确 `FAIL reason=NOLOCK: expected FAIL but batch succeeded (assertion is vacuous)`（EXIT=1）——**证明回滚断言非恒真**。
  - 回归全绿：`LA_THUMB_CACHE_QA PASS` / `LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=51` / `LA_SCALE_PERF PASS firstMs=42 scrollMaxMs=19` / `LA_P1_SMOKE_OK` / `LA_GALLERY_SEARCH_QA PASS marks=7`。
- 版本 2.2.8→2.2.9；`qa_runner` 22 门 → **23 门**。
## [2.2.8] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第四步缩略图多级缓存）
- **`ThumbCache` 两级 +按字节预算 LRU（替代单级 360px / 按张数计费）**：
  - **取证根因**：旧实现单一 360px 级 + `QCache::setMaxCost(4096)` 按**张数**计费，每张 360×360×4B ≈ 506KB → 上限 **4096×506KB ≈ 1.98 GiB**；且 216px 普通瓦片与 feature 大图（~468px）共用同一张 360px 图——大图不够大、小图白白占内存。
  - 改为 **L1 `kTile` 256px**（普通瓦片）/ **L2 `kFeature` 512px**（feature 2×2）两级，**按字节预算**淘汰：默认 `LA_THUMB_BUDGET1`=64MiB + `LA_THUMB_BUDGET2`=48MiB，合计 ~112MiB（**远低于旧 1.98GiB**）。
  - **单次解码一次派生两级**（`LoadTask` 复用同一份源像素，避免二次读盘/解码）。
  - **刻意保留的旧行为**：① 负缓存（`negative_` 跨级共享，防重试风暴；旧实现用「空 QPixmap 存进 QCache」表达同语义）；② `pending_` 按文件名去重 + `finish()` 经 `QueuedConnection` 回主线程；③ `QPointer` 生命周期守卫；④ 某级因预算淘汰时 `cached()` **回落另一级**（宁可给大图也不给空图）且不重复触发后台加载。
  - 实现要点：`ByteLru` = `QHash<QString,QPixmap>` + `mutable QList<QString> order_`（front=MRU，淘汰从尾取）；`cost(pm)=w*h*(depth/8)`（**`QPixmap` 无 `sizeInBytes()`，那是 `QImage` 的 API**）。
- **接线（API 向后兼容）**：`GalleryModel::pixmapAt(row, level=kTile)` 增level 默认参数并透传；`GalleryTile::paintEvent` 按 `feature_` 选级、`prefetchVisible()` 按 row==0 选级。既有调用点零改动。
- **新增第 22 门 `LA_THUMB_CACHE_QA`**（headless 无需 db，现场合成 8 张 512×512 样图）：断言①一次解码派生两级且两级像素不同 ② L1 字节 < L2 字节 ③ 同级重复 `request` 按文件名去重（任务数 == 文件数）④ 负缓存（缺文件重复 request 只触发一次后台任务、两回调皆空图）⑤ 字节预算生效（压小预算→该级张数受限且 `bytes<=budget`）⑥ 跨级回落（淘汰后 `cached()` 回落另一级、不重触发加载）。
- **实测**：`LA_THUMB_CACHE_QA PASS files=8 px=256/512 l1=8 l2=8 bytes=2097152/8388608 derived=16 req=8 evictL1=2 evictL2=1`（EXIT=0）；8 图 L1 仅 2MiB、L2 8MiB。**反例** `LA_THUMB_TILE_PX=32`（预算放得下全部→不淘汰）正确 `FAIL reason=B_evict l1count=8`（EXIT=1，非空心）。回归：`LA_GALLERY_E_QA featureOK=1`、`LA_SCALE_SMOKE PASS photos=6425 galleryCtrls=51`、`LA_SCALE_PERF PASS firstMs=46 scrollMaxMs=14`、`LA_SMOKE LA_P1_SMOKE_OK`、`LA_MAP_A_QA jade=17`、`LA_GALLERY_SEARCH_QA PASS marks=7` 全绿。
- 版本 2.2.7→2.2.8；`qa_runner` 21 门 → **22 门**。
## [2.2.7] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第三步列表虚拟化）
- **`EditorialView` 窗口化虚拟化（列表虚拟化落地）**：彻底移除「`rebuild()` 为每张照片 `new GalleryTile` 控件（6425 个）」的非虚拟化实现——这正是 D 第二步 `LA_SCALE_PERF` 基线指出的 `firstMs≈815–994ms` 主因。改为：
  - 去 `QGridLayout`，`content_` 设为**虚拟总高**（feature 2×2 + 流式网格），由该高度驱动滚动条；
  - **池化 `GalleryTile`**（`pool_`/`free_`/`bound_`）：仅实例化「可见视口 + 上下各 2 行缓冲」的瓦片；`scroll`/`resize` 时按可见模型行 `recycle` + `setRow(row,feature)` 重绑复用，杜绝逐图 new/delete；`GalleryTile` 新增 `setRow()` 支持池化重绑。
  - `relayout()` 重算列数/单元格/虚拟高并 `updateWindow()`；`resizeEvent`(60ms 去抖)/`valueChanged` 触发重算与重定位；`prefetchVisible()` 改为仅预取当前绑定（可见）行 ±1。
  - **对外 API 语义零破坏**：`featureGeometry()`/`tileGeometry(int)`/`tileCount()`(=模型行数)/`columnCount()`/`prefetchVisible()`/`photoActivated` 全部保留等价行为，确保 `LA_GALLERY_E_QA`(几何/颜色)、`LA_SCALE_SMOKE`、`LA_SCALE_PERF` 不破。
- **实测收益（D 第二步基线守护兑现「虚拟化落地后数值显著下降」）**：`LA_SCALE_PERF` 首屏 `firstMs` 由 ~841ms **降至 41ms（约 20×）**，滚动帧 `scrollMaxMs` 保持 15ms（≤50）；`galleryCtrls` 由 6470 **降至 51**（仅可见池化瓦片 + chrome）。`LA_GALLERY_E_QA` `featureOK=1` 不变；`LA_SCALE_SMOKE` 仍 `PASS photos=6425`。
- **反例确证非空心**：`LA_SCALE_PERF_FIRST=30`（<实测 38/41）正确 `FAIL`（`reason=firstMs=38>budget=30`），回归红线真实有效。
- 版本 2.2.6→2.2.7；`qa_runner` 21 门不受影响（`galleryCtrls` 断言仅要求 ≥0）。

## [2.2.6] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第二步流畅性基线门禁）
- **新增门禁 `LA_SCALE_PERF`（规模化流畅性基线）**：复用 `LA_SCALE_SMOKE` 库定位逻辑（env
  `LA_SCALE_DB` → `LA_DB` → 向上搜 `_scale_fixture.db` 合成 6425 张），构造 `GalleryPage`
  量化 ① 首屏耗时（`firstMs`：构造→首绘，含 6425 瓦片实例化 + 布局 + 可见区预取）② N 次滚动跳变帧
  paint 均值/峰值（`scrollAvgMs`/`scrollMaxMs`，保持 <120ms 节流、隔离纯 paint、不触发缩略图预取）。
  预算经 env 覆盖：`LA_SCALE_PERF_FIRST`（默认 3000ms）/ `LA_SCALE_PERF_SCROLL`（默认 50ms）/
  `LA_SCALE_PERF_JUMPS`（默认 20）。`GalleryPage` 新增公开访问器 `qaScrollBar()` 暴露
  `EditorialView` 滚动条供 offscreen 模拟滚动。断言 `photos≥1000` 且两项 ≤ 预算。
- **取证结论（先测再改基线）**：当前 `EditorialView` 在 `rebuild()` 实例化**全部 6425 个 `GalleryTile`
  控件**（非虚拟化），`firstMs≈815–994ms`、滚动帧 `≤12ms`；`ctrls=6470`（6425 瓦片 + ~45 chrome）。
  本门为后续「列表虚拟化」优化（§8.2）建立可回归守护——虚拟化落地后数值将显著下降，届时收紧预算。
- **双 QApplication 铁律再守**：`runScalePerf()` 复用 `main()` 既有 `QApplication`，仅自建 `Library`，
  不新建 `QCoreApplication`（避免退场 `SIGSEGV`）。
- **实测**：`LA_SCALE_PERF PASS photos=6425 firstMs=841 scrollAvgMs=10 scrollMaxMs=12`
  `ctrls=6470`（EXIT=0）；`LA_SCALE_PERF_FIRST=500` 时正确 `FAIL`（阈值断言生效，非空心 PASS）；
  `qa_runner -Only` 报告 `[OK]`。`qa_runner.ps1` 登记第 21 门。版本 2.2.5→2.2.6。

## [2.2.5] - 2026-10-02

### 功能（方向 D·规模化与流畅性：第一步真实库回归门禁）
- **新增门禁 `LA_SCALE_SMOKE`（真实库规模化回归）**：加载真实库（env
  `LA_SCALE_DB` 优先 → `LA_DB` → 从 exe 目录向上搜索提交于仓库根的
  `_scale_fixture.db`，合成 6425 张镜像真库规模），断言 `open` 成功、照片数
  `≥ LA_SCALE_MIN`（默认 1000）、`kpi` 已填充（`kpi.total==n` / `yearCount≥1` /
  `provCount≥1`）；构造 `GalleryPage`（真实图库网格）+ `MapView`（真实 GPS 足迹）
  offscreen 不崩、首屏渲染不超时（`mapMs ≤ LA_SCALE_MAP_MS` 默认 15000）。
- **合成 scale fixture `_scale_fixture.db`（6425 张，3.18MB）**：`_gen_scale_fixture.py`
  生成，覆盖 18 年份 / 15 省份 / ~80% GPS / 8 分类 / 人物；缩略图故意指向不存在文件
  （验证 `ThumbCache` 负缓存兜底缺失缩略图不崩）。`LA_SCALE_DB` 可指向用户真库覆盖。
- **双 QApplication 铁律再守**：`runScaleSmoke()` 复用 `main()` 既有 `QApplication`，
  仅自建 `Library` 打开真实库，不新建 `QCoreApplication`（避免退场 `SIGSEGV`）。
- **实测**：`LA_SCALE_SMOKE PASS photos=6425 years=18 provs=15 span=2008-2025 gps=5140`
  `galleryCtrls=6470 mapMs=4`，退出码 0；`LA_SCALE_MIN=10000` 时正确 `FAIL`（阈值断言生效，
  非空心 PASS）。版本 2.2.4→2.2.5。

## [2.2.4] - 2026-10-02

### 功能（方向 C·相册管理与智能检索：第一步相册内搜索）
- **相册页新增搜索框（`GalleryPage.cpp`）**：构造函数 `personBar_` 之后插入
  `QLineEdit` 搜索栏（`searchLe`，圆角深色、聚焦描边 `#4fc08a`、带清除按钮），
  `textChanged` 信号 `connect` 到既有 `applyFilter()`（设 `searchText_` →
  `model_.setFilter(q)` + `updateCount()`）。
- **`GalleryModel::rebuild()` 搜索 haystack 由 title/rel/cam 扩展为全字段**：
  现含 `title / rel / cam / region / prov / city / date / rating`，覆盖
  地点（省/市/国家）/ 时间（年月）/ 评分 / 关键词，子串 `CaseInsensitive` 匹配。
- **新增门禁 `LA_GALLERY_SEARCH_QA`**：直接驱动 `GalleryModel` 断言 7 类过滤
  （北京市/北京=1、`china`=2、`2023`=1、裸`5`=2、`Canon`=1、`夜景`=1、清空=4），
  纯数据无需 UI。**实测 `LA_GALLERY_SEARCH_QA PASS marks=7 checks=7`**，退出码 0。
- **事故修复（同迭代）**：`runGallerySearchQa()` 初版在 `main()` 已构造 `QApplication`
  之后又建 `QCoreApplication` → 双实例退场 `SIGSEGV`（实测 `EXIT=139` 且 `marks=6`）。
  删除第二个实例，复用既有 `QApplication`；同步把测试数据中 `家猫` 日期
  `2024-01-15`→`2024-01-12`，消除裸串 `5` 误命中日期尾号（2→3）的噪声。版本 2.2.3→2.2.4。

## [2.2.3] - 2026-10-02

### 功能（方向 B·修图：非破坏式撤销/重做栈）
- **新增与 UI 无关的纯状态机 `RtHistory`（`src/core/RtHistory.h`）**：`RtEditState`
  聚合全部编辑状态（修调 `cur_`、AI 计划 `agentPlan_`/`agentMode_`、几何
  `rot_/flipH_/flipV_/angle_/aspect_/cropNorm_`）；`RtHistory` 提供
  `clear/push/canUndo/canRedo/undo/redo/current/size/pos`，`push` 截断 redo 尾且跳过无变化提交。
  原图 `geoSrc_` 只读、永不被改，撤销/重做 = 快照重套原图重算出图，**天然非破坏**。
- **`RetouchPage` 接入撤销/重做**：`doApply()` 末尾（非 `restoring_` 态）提交快照；
  `loadPhoto()` 建立基线并 `resetHistory()`；`onUndo/onRedo` 经 `restoring_` 标志防重入 push，
  `applyState()` 回写状态并 `rebuildGeo()` 重算；`updateUndoRedoButtons()` 同步按钮可用态。
  动作条新增 `↶ 撤销`/`↷ 重做` 按钮；`Ctrl+Z` 撤销、`Ctrl+Shift+Z`/`Ctrl+Y` 重做（`QShortcut`）。
- **新增门禁 `LA_RETOUCH_UNDO_QA`**：直接驱动 `RtHistory` 断言基线/推入/双级 undo/redo/
  push 截断 redo 尾/无变化不记，纯状态机无需 UI。**实测 `LA_RETOUCH_UNDO_QA PASS marks=9 checks=9`**；
  回归 `LA_RETOUCH_A_QA ok=1`（构造 RetouchPage 不崩）。版本 2.2.2→2.2.3。

## [2.2.2] - 2026-10-02

### 功能（方向 A·地图：清偿 GeoIndex 中文省名债务）
- **`kCities` 中国条目省名由英文改为精确中文省名**（逐字对齐 `:/data/mapdata.json`
  `provinces.china` 的 DataV 34 省）：北京→北京市、上海→上海市、广州/深圳→广东省、
  成都→四川省、昆明/大理/丽江→云南省、杭州→浙江省、西安→陕西省、武汉→湖北省、
  长沙/张家界→湖南省、乌鲁木齐→新疆维吾尔自治区、拉萨→西藏自治区、哈尔滨→黑龙江省、
  大连/沈阳→辽宁省、厦门→福建省、桂林/南宁→广西壮族自治区、西宁→青海省、兰州→甘肃省、
  贵阳/遵义→贵州省、南京→江苏省、青岛→山东省、长春→吉林省、呼和浩特→内蒙古自治区、
  海口→海南省、台北→台湾省、香港→香港特别行政区、澳门→澳门特别行政区。
  多边形主路径本就返回中文省名，此修使**兜底（最近城市）路径与之完全一致**，
  彻底消除中英文混用（同一城市：多边形命中显示"辽宁省"、走城市兜底却显示
  "Liaoning Province" 的残留不一致）。
- **新增 `GeoIndex::verifyChinaProvinces()` 数据完整性自检**：遍历城市表，凡
  `region=="china"` 的条目，`prov` 须无 ASCII 字母且落在已知 34 省白名单内，否则返回
  违规项；`runGeoSmoke`（`LA_GEO_SMOKE`）在门禁内调用并据其 FAIL，使该债务**门禁可断言、
  不可回退**。
- `runGeoSmoke` 新增 成都 已知点（30.57,104.07 → 四川省/成都，主路径命中多边形），
  已知点断言扩至 6。

### 实测
- `LA_GEO_SMOKE PASS loaded=1`：6 已知点全 `ok`（雷克雅未克 / 北京 / 广州 / 大连 /
  成都 / 南海诚实空），无 `GEO badprov` 违规行 → `verifyChinaProvinces()` 零命中。
- 增量构建通过（`main.cpp` + `GeoIndex.cpp` 编译 + 链接，`LightAlbum.exe` 100% Built）。
- 注：本机 git-bash/headless 上下文下 `qa_runner.ps1` 因安全策略禁用
  `.NET Process.Start` 且缺真实控制台无法自退，故全 19 门自动执行交由 CI
  (`qa.yml` 真实 Windows runner + 控制台) 与 `release.ps1` 卡点负责；本轮门禁以
  Git-Bash 文件重定向直跑 `LA_GEO_SMOKE` 实测通过。

## [2.2.1] - 2026-10-02

### 修复（QA 门修正，无用户态行为变化）
- **`LA_GEO_SMOKE` 由红转绿**：DataV 34 省切换后，多边形主路径返回**中文省名**
  （北京市 / 广东省 / 辽宁省），但测试仍期望英文（`Beijing Municipality` 等），
  该门自切换后一直 RED 且因历史未跑 + Git Bash 吞输出长期未被发现。
  将 3 处测试期望对齐到设计内中文真值。

### 变更
- 新增 `docs/客户端迭代指导书.md`（全模块 SOP：模块总览 + 全门禁目录 + 工具链 +
  铁律 + CheckList + 迭代 4 实例）。
- **关键运维发现写入 SOP**：原生 Windows Qt exe 在 Git Bash(MSYS) 下输出被吞、
  表现为「门禁无输出/秒退」假阴性；跑全部门禁**必须用 PowerShell**（`& $exe 2>&1`）。

### 已知债务（已于 2.2.2 清偿）
- `src/core/GeoIndex.cpp` 的 `kCities` 中国条目省名原为英文（兜底路径），与多边形
  中文不一致；**已在 2.2.2 逐条改为 DataV 34 省精确中文名**，并由
  `verifyChinaProvinces()` 在 `LA_GEO_SMOKE` 门禁内断言、不可回退。

### 实测
- `LA_GEO_SMOKE PASS loaded=1`（5 已知点全 ok：雷克雅未克 / 北京 / 广州 / 大连 / 南海诚实空）
- 构建零 error；产物 `LightAlbum-2.2.1-win64.zip`（版本号由 `release.ps1` 从 CMakeLists 解析）

## [2.2.0] - 2026-10-02

### 新增
- **下钻视图滚轮缩放（1–8x，光标锚定）+ 拖拽平移**：大区 / 省 / 国家视图都能放大看密集照片簇并拖动。
  变换统一到唯一真源 `viewOrigin()`（= 拟合 scale × `viewZoom_` + `viewPan_`），
  绘制与命中同源 → 放大后点省/点国/点卡片依然准确。
- 进入/退出下钻视图自动重置缩放与平移；平移带边界夹取，不会拖成全空白。

### 变更
- 交互语义统一：世界层与下钻层的「点空白」都改为**按下待命、原地松开才返回上一级**
  （拖动即平移，不再一按就跳走）。

### 实测
- `LA_MAP_CLICK_QA` 十三断言全 OK（新增 `vzoom=OK(zoom=1.56)`；`blank` 断言改为
  按下+松开合成，因语义变更）
- `LA_MAP_PERF`：world=2.20 / region=0.75 / province=0.70 / country=0.55 ms
- `LA_MAP_REGION_QA`：china provinces=34 land=157234；`LA_MAP_SMOKE` 不变

## [2.1.0] - 2026-10-02

### 新增
- **「▶ 播放本区作品」**：省 / 大区 / 国家三层级通用，一键用爆炸弹层播放当前区域全部作品（最多 10 张，尊重隐私门）。
- **世界层导航**：顶部「⌂ 回到中国」按钮（缩放归 1 + 中国居中），底部显示当前缩放倍数 `缩放 ×N`。
- 国家视图：世界层任意国家可点（含亚洲全境），无专辑国家进入国界轮廓视图 + 该国 GPS 作品爆炸弹层。
- 世界层滚轮缩放（1–4x，光标锚定）与拖拽平移；3x 超采样烘焙；源数据抽稀 0.18/0.08（2608 环 + 12 湖泊镂空）。

### 修复
- 国家视图爆炸弹层从未播放：`startBurst` 的 `level_` 守卫只认省视图。
- 世界层点击/悬停热区与绘制错位：命中测试与绘制的 scale 基准不一致。
- DataV 中文省名 + 扁平环导致 GeoIndex 静默丢省（`photo.prov` 全空）→ 环数据统一为双层包裹。
- 足迹光点过大（blit 倍率 10→5→3.5）；下钻放大后光晕变巨斑（改 `sqrt(scale)`）。

### 变更
- 流畅性：下钻视图静态层缓存 + 航线批量合并 + 密度/目标集/文字 halo 三级缓存。
  region 帧耗 52.90ms → 0.35ms（1200×800，20 帧均值）。
- QA：`LA_MAP_CLICK_QA` 十二断言；新增 `LA_MAP_PERF` 帧耗时门、`LA_MAP_COUNTRY_QA` 国家视图像素门。

### 实测（离屏门）
- `LA_MAP_CLICK_QA`：country/anchor/province/burst/card/blank/zoom/all/ctry/cburst/reset/regBurst 全 OK
- `LA_MAP_PERF`：world≈2.35 / region≈0.35 / province≈0.35 / country≈0.30 ms
- `LA_MAP_SMOKE`：rings=2608 regions=10 provinces=233 zh=233 names=2596
- `LA_MAP_REGION_QA`：china provinces=34 land≈157234；`LA_MAP_COUNTRY_QA`：Vietnam slate=27182

## [2.0.0] - 2026-09（基线）
- native C++/Qt 桌面端基线版本：图库 / 地图 / 精选 / 修图 / 统计五大页。

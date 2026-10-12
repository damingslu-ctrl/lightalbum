# 设计规格：引擎升级 第二轮（Lumen 显式算子产品化 + HEIC/WebP EXIF 补全 + Erase residual 收尾）

## 0. 背景与取证

第一轮引擎升级已交付：MotionKit（2.34）、EraseBridge 真引擎 + FaceCluster 两趟精炼（2.35）、
Erase 异步化 + 帧协议短读根治（2.36）、设计语言 v2 P1（2.37，与引擎轨无关但已发布）。

2026-10-12 对 `native/src/core/` 全部 ~25 个引擎模块做了只读巡查（每模块状态/限制/升级方向，
证据均 file:line；巡查明细见本地 scratch `native/_engine_survey_2026-10-12.md`，不随仓提交）。
本轮取巡查排名前列中**效果/成本比最高且相互独立**的三条路线：

1. **R1 Lumen 显式算子产品化** —— 巡查 #1（效果 高 / 成本 中）
2. **R2 HEIC/WebP EXIF 补全** —— 巡查 #2（效果 高 / 成本 中）
3. **R3 Erase residual 收尾** —— 巡查 #7（效果 中 / 成本 低；已交付功能的资源泄漏与收尾）

其余候选（AI 索引去外部依赖 #3、Lumen 多线程+float #4、A/V 同步 #5、StoryGen 人物主题 #6、
统一引擎注册表 #8）与本轮解耦，留待后续轮次，见 §9。

## 1. 决策记录

- **决策依据**：用户指令「继续找到每个引擎的升级方向并且按顺序进行升级」+ 长期授权
  「按效果最好选择、直接推进」；只读巡查排名为排序依据。本 spec 呈用户评审门（铁律流程）。
- **本弧版本目标**：2.38.0（收口时 bump + CHANGELOG + 手册；打包与提交全部本地，不推送）。
- **顺序**：R1 → R2 → R3（按巡查排名取效果优先；R3 与 R1/R2 无依赖，其小改动并入本弧顺手收尾）。
- **不动已交付**：MotionKit 全家、EraseBridge 主链、FaceCluster 算法本身（仅 R3 清其陈旧注释）。

---

## 2. 路线 R1 —— Lumen 13 显式算子产品化（RetouchPage 一键增强）

### 2.1 现状与取证

- `LumenEngine.h:68-89`：P5-2/3/4 三批**共 13 个显式算子全部已实现**且经金样对拍——
  filmStyle、blackWhite、glowStyle、redeyeRemove、vignetteCorrect、toneMapHdr、structure、
  dodge（P5-2）；dehaze、chromaticCorrect、portraitBeautify（P5-3）；levelCorrect、lensCorrect（P5-4）。
- **零产品入口**：`grep src/ui/` 对以上算子名零命中（已实证）；唯一调用方是
  `main.cpp:5285+ runLumenSmoke` 的 kOps 金样烟测（`gate_matrix.ps1:103` LA_LUMEN_SMOKE，
  measuredMs=66585）。
- RetouchPage 现仅 agent 模式走 `LumenEngine::retouchWithPlan`（RetouchPage.cpp:879-880，
  QtConcurrent）；滑杆路径走 RetouchEngine（RetouchPage.cpp:617-618 等）。
- 结论：**引擎与门禁已就绪，缺的纯是 UI 接线**——本轮把它接出来，是零新依赖的最大价值解锁。

### 2.2 交互设计

RetouchPage 新增「一键增强」工具区，13 算子按语义分三组（组内平铺入口）：

| 组 | 算子（入口名） |
|---|---|
| 影调 | 胶片 filmStyle / 黑白 blackWhite / HDR toneMapHdr / 结构 structure / 减淡 dodge / 柔光 glowStyle |
| 人像 | 红眼 redeyeRemove / 美颜 portraitBeautify |
| 校正 | 水平 levelCorrect / 镜头 lensCorrect / 暗角 vignetteCorrect / 去雾 dehaze / 色差 chromaticCorrect |

- **行为**：点击 → 对当前编辑源图（≤1024px，RetouchPage.cpp:44 kMaxSrc 既有上限）QtConcurrent
  异步执行 → 完成后写回 RetouchPage 编辑态并**走其现有撤销/历史通道**（RtHistory 体系，具体
  接口 plan 阶段钉 file:line）；执行中忙碌态（该入口禁用 + 轻量进行提示），GUI 永不阻塞。
- **参数**：一律用引擎默认值（真·一键）；参数可调版明确不做（§9）。
- **诚实反馈**：结果含像素级信息时如实呈现——红眼已修点数（redeyeRemove 出参 pixels）、
  美颜肤色占比（skinFrac）、水平检测角度（levelDetectAngle，无直线→0 即 no-op 并提示
  "未检测到可校正的倾斜"）。
- **视觉语言**：遵循设计语言 v2（Theme token、间距 4px 网格）；**零新增 emoji 图标**
  （C5 棘轮 ceiling=30 只降不升，LA_PALETTE_QA 硬约束）；工具区不破坏现有 RetouchPage 布局。
- **性能预期**（plan 阶段以实测回填）：单算子 1024px CPU double 数学约 0.3–3s；重算子
  （toneMapHdr/dehaze/portraitBeautify）偏上限；异步 + 忙碌态保证体验。

### 2.3 契约

- **引擎数学零改动**：LumenEngine.{h,cpp} 不动，金样烟测 LA_LUMEN_SMOKE 是回归护栏。
- **不写库**：纯编辑态图像变换，不触 Library/SQLite → 与单写者契约无交互。
- **撤销语义**：复用现有编辑历史通道，不新造机制；撤销一步回到算子执行前状态。

### 2.4 门禁

- **新增脚本门 `LA_LUMEN_OPS_UI_QA`（fast 层，静态+自检）**：断言 `src/ui/` 对 13 个算子名
  各 ≥1 处真实调用（含事件接线，非注释）；`FORCE_BROKEN=1` 反例：向期望清单**注入一个不存在
  的算子名**（要求 14/13 覆盖）→ 必红（禁空心门）；F1 fail-open 守卫：扫描根产出 0 文件判
  FAIL；`-SelfTest` 覆盖。
- **烟测断言**：在现有 retouch 系 smoke（main.cpp runRetouch* 家族）中增补：一键增强 13 个
  入口控件按 objectName `findChild` 全部存在（offscreen 可跑，不依赖点击）。
- `gate_matrix.ps1` 注册 + measuredMs 按铁律 25/30/31 实测回填；.ps1 全 ASCII（铁律 37）。

### 2.5 交付物清单

- `native/src/ui/RetouchPage.{h,cpp}`：工具区 UI + 13 接线 + 异步 + 撤销集成 + objectName。
- `native/src/main.cpp`：retouch 系 smoke 补 13 入口存在性断言。
- `native/gate_matrix.ps1`：注册 `LA_LUMEN_OPS_UI_QA` + measuredMs。
- `native/CHANGELOG.md` `[2.38.0]` R1 节。

---

## 3. 路线 R2 —— HEIC/WebP EXIF 容器解析补全

### 3.1 现状与取证

- `ExifReader.cpp:274-297` 只按文件签名三分派：JPEG 0xFFD8→APP1、PNG 8 字节签名→eXIf、
  TIFF/DNG II/MM→整文件。**无 ftyp（HEIF/ISOBMFF）分支、无 RIFF（WebP）分支**；
  `ExifReader.h:281-283` 自述 "WebP/HEIC/RAW 不解析"；无结果即 `fillMtimeFallback`
  （ExifReader.cpp:305）。
- **真实影响**：用户 iCloud 库有 777 张 .heic（迭代指导书 P-icloud 行）→ 导入后无拍摄
  日期/GPS，只剩 mtime 回退；时间轴、地图、故事、Cull 全部降级。
- 抽取出的 TIFF blob 可直接喂**现有 TIFF 解析器**（ExifReader.cpp:299 起全解析），
  无需重写解析——本轮只补"容器→blob"一层。

### 3.2 设计

- **WebP（RIFF）**：`RIFF`+size+`WEBP` 后逐 chunk 扫描；`EXIF` chunk payload 即 TIFF blob
  （兼容两种写入器：直接 TIFF 头 / 前置 `Exif\0\0` 前缀）；chunk 尺寸防呆。
- **HEIC（ISOBMFF）**：最小 box walker——顶层找 `meta` full box → `iinf`（`infe` 条目
  item_type=`Exif` 取 item_ID）→ `iloc`（按 item_ID 取 offset/length；支持 4/8 字节
  offset/length size 配置；`construction_method` 0 即文件内偏移）。取出的 payload 前 4 字节为
  到 TIFF 头的偏移 → 抽 TIFF blob。防呆：box size 合法性、盒链读取总量上限（64MB）、
  深度上限、越界即放弃。**无新依赖**（纯 QFile 读）。
- **回退链不变**：任何一步失败 → 如实走既有 mtime 回退（诚实降级，不装成功）。
- **接入口**：ExifReader 内新增 `extractWebpExif` / `extractHeicExif` 两函数 + 分派分支；
  Scanner（Scanner.cpp:357）与派生的日期/地理链自动受益，无改动。

### 3.3 门禁

- **新增门 `LA_EXIF_CONTAINER_QA`（full 层，外来进程门）**：合成夹具（C++ 内构造确定字节）——
  (a) 真结构 WebP：RIFF 头 + `EXIF` chunk 载已知 TIFF blob（含 DateTimeOriginal+GPS）；
  (b) 最小 HEIC：`ftyp`+`meta(iinf/iloc)`+`mdat` 载 4 字节偏移 + 同款 TIFF blob。
  断言两容器均解析出等价元数据。反例：越界 box size / 自指循环盒 → 不崩、走 mtime 回退、
  门断言"无崩溃 + 回退语义"。
- **真样本实证**：env 门 `LA_EXIF_REAL_SAMPLE`（need=LA_HEIC_SAMPLE，本地真 .heic 路径，
  值只作本地 env，**不入仓库/CHANGELOG/文档**——既有 LA_RAW_SAMPLE 判例）。
- **零回归**：JPEG/PNG/TIFF 三格式行为不变（既有解析断言全绿）。

### 3.4 交付物清单

- `native/src/core/ExifReader.{h,cpp}`：两抽取函数 + 分派 + 防呆。
- `native/src/main.cpp`：`runExifContainerQa`（合成夹具 + 反例）。
- `native/gate_matrix.ps1`：注册 `LA_EXIF_CONTAINER_QA`（+ env 门行）+ measuredMs。
- `native/CHANGELOG.md` `[2.38.0]` R2 节。

---

## 4. 路线 R3 —— Erase residual 收尾（淘汰 + 取消 + 陈旧文档）

### 4.1 现状与取证

- **内存无界**：`EraseBridge.cpp:57` submit() 向 `tasks_` 追加，全文件零移除（:40/:44/:65/
  :92/:110 仅查找/遍历）→ 每任务保留 base+mask+result QImage。
- **无取消**：UI 无 cancel 路径，Runner 无 kill 指令（2.36 异步化已解 GUI 冻结，取消是最后
  一块拼图）。
- **陈旧注释**：`EraseBridge.h:2` 仍写"表单壳…模型槽位留空"（2.35 已实装）；
  `FaceCluster.h:6-7` 仍描述贪心单连锁（2.35 已两趟）。

### 4.2 设计

- **淘汰**：完成任务列表保留最近 **N=8**（常量，如 `kTasksKeep`）；`submit()` 时修剪最旧
  **已完成**任务（running 永不淘汰）；淘汰即释放 QImage。
- **取消**：`EraseBridge::cancel(id)` → Runner 中止当前任务：kill 子进程 + 状态机复位
  （下次 submit 自动重启，冷启动 ~8s 为**已知诚实代价**）；该任务态 = `cancelled`；
  RetouchPage 进行中任务出现取消按钮。取消不影响后续任务提交（复位后可继续）。
- **注释订正**：`EraseBridge.h:2`、`FaceCluster.h:6-7` 改为与实现一致的描述。

### 4.3 门禁

- **扩 `LA_ERASE_QA`（不新增门数）**：
  - G1 淘汰：合成提交 9 个任务（桩延迟钩子复用 2.36 的 E 组机制）→ 全部完成后
    `tasks` 大小 ≤8 且最旧已不可见。
  - G2 取消：长桩任务进行中 cancel → 态 `cancelled`、子进程已终止、**随后的新任务仍能成功
    完成**（证明复位有效）。
  - 反例钩子（env）各一：关淘汰 → G1 红；忽略 cancel → G2 红。
- measuredMs 重测回填。

### 4.4 交付物清单

- `native/src/core/EraseBridge.{h,cpp}`：淘汰 + cancel + 注释订正。
- `native/src/core/EraseRunner.{h,cpp}`：abort（kill+复位）。
- `native/src/core/FaceCluster.h`：注释订正（2 行）。
- `native/src/ui/RetouchPage.cpp`：进行中任务取消按钮 + 接线。
- `native/src/main.cpp`：runEraseQa 补 G1/G2 + 反例。
- `native/gate_matrix.ps1`、`native/CHANGELOG.md` `[2.38.0]` R3 节。

---

## 5. 验证计划

1. **构建**：MinGW，`cmake --build build --target LightAlbum -j 8`；零新链接依赖（三条路线
   均无新库）。
2. **新门首红后绿**（PowerShell only，per 记忆 `gates-run-via-powershell`）：
   - `qa_runner.ps1 -Only LA_LUMEN_OPS_UI_QA`（接线前必红 → 接线后绿）；FORCE_BROKEN 转红。
   - `qa_runner.ps1 -Only LA_EXIF_CONTAINER_QA`（实现前必红 → 后绿）；越界/循环反例转红。
   - `LA_ERASE_QA` G1/G2（实现前红 → 后绿）；两条 env 反例转红。
   - `-Timing` 回填三门 measuredMs（LA_ERASE_QA 重测）。
3. **名册自检**：`gate_matrix.ps1 -SelfTest` → gates=74（fast 19 + full 55）；E11 nonAscii=0；
   E12 textual==parsed；fails=0。
4. **全量回归**：full 层 → `FAIL=0 CHECK=0 KILL=0`；SKIP 仅既有 need= 门。
5. **行为抽验**（本地真样本）：用户真 .heic（env 传入）解析出拍摄日期/GPS；RetouchPage 手动
   过一遍 13 入口（重算子响应时间记录）；Erase 取消一次真任务。
6. **打包端到端**：2.38.0 bump → release.ps1 → RELEASE OK（C1..C9）；空标记提交 + tag
   v2.38.0，**全部本地，不推送**。

---

## 6. 铁律合规映射

| 铁律 | 落点 |
|---|---|
| 20 门禁与实现同源 | R1 静态门测真调用点 + smoke 测真控件；R2 门测真解析器；R3 扩组测真淘汰/取消 |
| 22 期望不随开关分支 | 各反例钩子只改输入/清单，期望式恒为规格值 |
| 25/30/31 先测后定 | 三门 measuredMs 实测回填，不估 |
| 29 宿主无关/无随机 | R1 静态扫描+findChild；R2 合成夹具字节确定；R3 桩任务固定图案 |
| 34 禁空心门 | R1 FORCE_BROKEN、R2 越界反例、R3 env 反例均为强判别 |
| 37/7 .ps1 纯 ASCII | gate_matrix.ps1 新增注释块全 ASCII |
| C5 棘轮 | R1 新 UI 零新增 emoji（LA_PALETTE_QA ceiling=30 只降不升） |

---

## 7. 记忆 / 台账

- `CHANGELOG.md` 新增 `[2.38.0]`（R1/R2/R3 三节 + 回归行）。
- `docs/迭代指导书.md`：§3 门数 72→**74**（fast 19 + full 55）；§7 台账一行。
- 项目记忆更新：`engine-upgrade-roadmap.md`（round 2 落地，路线 4-7 与巡查 #3-#8 留档）；
  新增 `exif-container-parse.md`（HEIC/WebP 容器 → TIFF blob 抽取范式）。

---

## 8. 风险与回退

| 风险 | 缓解 |
|---|---|
| R1 13 入口 UI 拥挤/破坏布局 | 分组呈现；plan 阶段以现有 RetouchPage 结构定式；纯增量 UI，可整块回退 |
| R1 重算子耗时超预期 | 异步 + 忙碌态兜底；实测回填；仍超预期则该算子降级为"提示较慢"或移出（逐算子可裁决） |
| R2 HEIC 变体多（iloc 特殊形态/超大盒） | fail-open 走 mtime 回退（诚实降级）；读取上限 + 越界防护；真样本实证先行 |
| R2 改动伤既有三格式 | 既有解析断言全绿为新门前置；分派仅新增分支 |
| R3 取消后冷启动 8s 体验 | UI 文案如实提示；Runner 复位逻辑单测（G2 双重断言） |
| R3 淘汰误伤 | running 永久 pin；仅修剪已完成且超 N 的最旧项 |

**回退**：各路线独立可回退（R1 为 UI 增量文件改动；R2 为 ExifReader 增量分支；R3 局部）。
全程增量提交，绝不 `git reset/checkout/clean`。

---

## 9. 未决 / 后续（明确不在本轮）

- 巡查 #3 AI 索引去外部依赖（路线 4）：Classifier/PersonDetector 随包 ONNX 子进程化（成本高，
  单独立弧）；分类器 SigLIP 权重升级（服务端）。
- 巡查 #4 LumenEngine 多线程 + float 化（性能）；#5 A/V 同步时钟（路线 5）；#6 StoryGen
  人物主题 + 封面；#8 统一引擎注册表（路线 7）。
- 小项：GeoHotspot 省视图热点层；RawDecode 全尺寸按需切换；PhotoCluster 新维度；
  CullEngine 阈值设置化；LumenAgent Ollama 决策模型部署（部署动作，代码已就绪）。
- R1 算子参数可调版；FaceCluster 增量/可配阈值/密度聚类（D9 砍掉项）。

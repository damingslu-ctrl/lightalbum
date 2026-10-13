# 设计规格：引擎升级 第三轮（LumenEngine 算子内并行 + 量化安全 float）

## 0. 背景与取证

第一轮引擎升级交付：MotionKit（2.34）、EraseBridge 真引擎 + FaceCluster 两趟精炼（2.35）、
Erase 异步化 + 帧协议短读根治（2.36）。第二轮交付（本地，未打包/tag）：R1 Lumen 13 显式算子
产品化（RetouchPage 一键增强）、R2 HEIC/WebP EXIF 容器解析、R3 Erase 淘汰+取消收尾，版本 2.38.0。

2026-10-12 对 `native/src/core/` 全部 ~25 个引擎模块做了只读巡查（证据均 file:line，明细见本地
scratch `native/_engine_survey_2026-10-12.md`，不随仓提交）。巡查「路线 4-7 核验」与排名确认
本轮目标路线仍未做：

- **本轮 = 巡查排名 #4 / 路线 6 的性能子集（§14b）**：LumenEngine 算子**内**多线程（QtConcurrent
  按行分块）+ 对量化后结果安全的 double→float。这是本轮唯一选定路线。

**取证（2026-10-13 现场核验，非引用旧巡查）**：

- `native/src/core/LumenEngine.cpp` 全文 grep `QtConcurrent|QThreadPool|std::thread|concurrent` **零命中**
  → 引擎内部**无任何算子内并行**；逐像素热点是朴素串行 for（如 `:32`、`:36` 双 for 结构，
  `gaussianBlur` `:254-310`）。
- 第二轮引入的 QtConcurrent 仅在**整算子级**（`RetouchPage.cpp:39` include、`:995` `QtConcurrent::run`），
  把整个 op 丢后台线程，**未**触及 op 内部像素循环 → 与本轮「算子内并行」正交、不重叠。
- `Qt6::Concurrent` 已在 `CMakeLists.txt:19` find_package、`:139` 链接进 `LightAlbum` 目标
  → 本改造**零新依赖、不改 CMakeLists**。
- 金样门 `LA_LUMEN_SMOKE`（`runLumenSmoke` `main.cpp:5582-5736`）是 **MAD 容差门**（详见 §5），
  非精确像素门 → 为 double→float 与并行提供了实证安全边界。
- 金样 fixture 已在盘（`_gold_lumen/` 121 文件：`in_0..5.png`、`gold_*.png`、`op_*_*.png`、
  `steps_*.json`、`in_level_*.png`/`op_level_*.png`/`level_meta_*.json`）→ 改造后**可即时对拍验证**。

**排名对照（本轮排除项及理由，见 §9）**：#3 AI 索引去外部依赖（效果高/成本高/主机受限：需拉取校验
ONNX 模型、复刻 erase 的 MinGW+本地 OR SDK 先例，风险最高）；#5 A/V 同步时钟；#6 StoryGen 人物主题+封面；
#8 统一引擎注册表（成本高，触双引擎全对拍）。#4 与 #1/#2/#7（第二轮）无耦合。

---

## 1. 决策记录

- **决策依据**：用户长期指令「继续找到每个引擎的升级方向并且按顺序进行升级」+「按效果最好那个选择、
  继续下一轮」；brainstorming 呈上 2-3 方案 + 本 spec 呈用户评审门（铁律流程）。**方案 A（选择性算子内
  并行 + 量化安全 float）经用户认可**（用户回「都可以，按效果最好那个选择继续下一轮」）。
- **效果/可行性论证**：#3 原始效果最高，但本主机无管理员权限、C: 近满、真模型编译链主机受限（见
  用户记忆 Host-no-admin-rights）→ 落地风险最高。#4 综合「效果 × 主机可行性」最优：修核心 CPU 卡点
  （烟测单门 66.6s）、零新依赖、且被既有金样对拍门守护防回归。
- **本弧版本目标**：CMakeLists VERSION 2.38.0 → **2.39.0**（收口时 bump + CHANGELOG + 手册；本地提交）。
  注：2.38.0 与 2.39.0 的**打包/tag/推送均仍为用户门控的独立步骤**，本轮不触发（见记忆 release-state、
  push auth 仅覆盖过 2.36.0）。
- **门禁数**：**不新增门**。并行/float 由既有 `LA_LUMEN_SMOKE` 扩展一个「串行 vs 并行 逐字节一致」
  决定性子断言守护（§5），roster 维持 74。铁律 #20（门与实现同源）由同一提交落门改+引擎改满足。

---

## 2. 架构：`lumenParallelRows` 分块执行器

新增**匿名命名空间**文件局部工具（放 `LumenEngine.cpp` 顶部，与 `clamp255 :23` 同区）：

```cpp
// 逐行块并行执行器：把 [0,h) 切成若干行块，用 QtConcurrent::blockingMap 分派。
// body(y0,y1) 负责行区间 [y0,y1)；仅用于"每输出像素只依赖自身/邻域读、只写自身像素"的纯映射循环。
// LA_LUMEN_THREADS=1 -> 强制串行（金样门做串行/并行逐字节比对；也用于调试确定性）。
static void lumenParallelRows(int h, const std::function<void(int,int)>& body) {
    int nt = 0;
    const QByteArray tv = qgetenv("LA_LUMEN_THREADS");
    if (!tv.isEmpty()) nt = tv.toInt();
    if (nt <= 0) nt = QThread::idealThreadCount();
    if (nt <= 1 || h <= 1) { body(0, h); return; }            // 串行退化路径（零并发，结果即基准）
    const int nchunk = std::max(1, std::min(h, nt * 4));       // 4x 超订阅，负载均衡
    const int rows = (h + nchunk - 1) / nchunk;
    QVector<QPair<int,int>> blocks;
    for (int y = 0; y < h; y += rows) blocks.append(qMakePair(y, qMin(y + rows, h)));
    QtConcurrent::blockingMap(blocks, [&](const QPair<int,int>& b){ body(b.first, b.second); });
}
```

- **调用侧统一模式**：每个热点循环由 `for (int y=0;y<h;++y){ QRgb* d=...scanLine(y); ... }` 改写为
  `lumenParallelRows(h, [&](int y0,int y1){ for (int y=y0;y<y1;++y){ <同一函数体的逐行代码，逐字不变> } });`。
- **线程安全前提（写入契约）**：`body` 内每个线程只触碰自己行区间的 `scanLine(y)`。QImage 首次非常量
  `scanLine()` 触发 detach（写时复制），detach 在并行调用**之前**的单线程点完成（改造时确保先取到可写
  缓冲或先 detach），并行体内不再引发 detach。
- **`idealThreadCount` 兜底**：返回 <=0 时按 1 处理（纯串行），避免异常环境。

---

## 3. 并行覆盖范围（逐循环清单，证据 file:line）

### 3.1 并行化（纯映射：每输出像素只读自身/邻域、只写自身像素）

| 目标 | 位置 | 并行粒度 |
|---|---|---|
| `gaussianBlur` 横向 pass | `:271-289` | 逐行（本 pass 只读同行） |
| `gaussianBlur` 纵向 pass | `:290-308` | 逐行（读邻域 tmp 行、写 img 行，tmp 只读、img 逐行独立） |
| `pointRgb` | `:114-123` | 逐行 |
| `pointGray` | `:58-67` | 逐行 |
| `chopGray` | `:29-45` | 逐行 |
| `invertGray` | `:47-56` | 逐行 |
| `enhanceBrightness` | `:471-482` | 逐行 |
| `enhanceColor` | `:484-499` | 逐行 |
| `channelGain` | `:902-913` | 逐行 |
| `blend` | `:847-862` | 逐行 |
| `screen` | `:864-879` | 逐行 |
| `composite` | `:881-900` | 逐行 |
| `hsv2rgb` | `:215-252` | 逐行 |
| `toL8` | `:171-182` | 逐行 |
| `rgb2hsv` | `:184-213` | 逐行（**保持整型截断**，见 §4.2；仅并行不改精度） |
| `blackWhite` mix 循环 | `:1008-1016` | 逐行（就地读==写自身像素） |
| `vignetteCorrect` 径向全分辨率循环 | `:1105-1117` | 逐行（就地、每像素算 r2 无邻域） |
| `redeyeRemove` pass1 分类 | `:1036-1044` | 逐行 + **`cnt` 改每线程局部求和后合并**（见 §3.2 例外） |
| `redeyeRemove` pass2 校正 | `:1047-1055` | 逐行（读 hit[] 只读、写 img 逐行独立） |
| `unsharpMask` 应用段 | `:317-330` | 逐行（blur 已被上面并行；应用就地逐行） |
| `median3` / `median3G` / `smoothG` / `smoothFilter` | `:334-356`/`:70-90`/`:93-112`/`:358-381` | 逐行 **halo±1**（读源缓冲邻域、写自身新 out 行；源只读） |
| `minFilter` | `:1220-1249` | 逐行 **halo=k/2**（dehaze k=15→halo7；读源、写自身 out 行） |
| `medianFilter`(size=5) | `:1251-1279` | 逐行 halo±2（portraitBeautify 用） |
| `lensCorrect` tile 网格 | `:1744-1766` | **按 tile 并行**（tile 完全独立、写各自 out 区域、读只读 pad） |

> halo 滤波与 lens 的并行安全性：这些循环**读的是不透明/只读的源缓冲、写独立的新 out 缓冲**（或就地读==
> 写同行像素），邻域读只涉及源、不涉及被并行写入的目标 → 块间无读写竞态，结果与串行**逐字节一致**。

### 3.2 明确保持串行（测量/归约/控制/精度敏感，保护金样门精确断言）

- 全部**测量与决策**：`measureStats :502-529`、`measureExtra :531-577`、`hist8 :125-133`、
  `boxDownscale :425-469`、`areaResample :385-422`、percentile/`std::partial_sort`（dehaze `:1366`）、
  `levelCorrect` Hough 累加器 `:1592-1600`、`levelDetectAngle`、`whiteBalance` 幂均值归约 `:593-601`、
  `contrast`/`enhanceContrastPil` 均值归约 `:920-924`、`portraitBeautify` 蒙版计数/肤色亮度归约 `:1481`/`:1502`、
  `vignetteCorrect` 环柱归约 `:1073-1094`、`redeyeRemove` 面积门。
- **`addGrain :936-960` 保持串行**：其 `static thread_local std::mt19937 gen(1234567)` +
  `normal_distribution`（`:948-950`）非「位置的纯函数」，并行会产出不同噪声场破坏可重现性（头注 `:9-12`
  强调金样 parity；虽 grain 文档称不入金样比对 `:1000`，仍保守串行，且它非热点）。
- 顺序管线 `autoPipeline :1986-2079`、`retouchWithPlan :1914-1966`：串行阶段链不变，**只因其成员 op
  内部并行而间接受益**（不新增阶段间并行）。

> 理由：归约/排序的浮点**加法顺序**会变；金样门对 `autoPipeline` **step-key 序列**是**精确相等**断言、对
> `levelDetectAngle` 是 ±1.0° 容差断言（§5）。串行保持这些 → 零回归。且这些归约多作用在 ≤256 缩略图上，
> 并行收益低、风险高 → 明确排除。

---

## 4. double→float 精度策略

### 4.1 仅对「量化后即定、且不参与下游阈值判定」的逐像素颜色算术切 float

范围：`hsv2rgb`（`:215-252`，末 `clamp255(int(x*255+0.5))`）、`enhanceBrightness`（`:471-482` ×f 后 +0.5）、
`channelGain`（`:902-913`）、`blend`（`:847-862` 线性插值）、`vignetteCorrect` 径向增益（`:1105-1117`
`gain`/乘算后 `clamp255(int(..+0.5))`）、`gaussianBlur` 横向/纵向累加器（`:262-268` kernel、`double acc[3]`
→ float；末 `clamp255(int(acc+0.5))` 吸收）。

**为何安全（实证）**：金样门像素判据是**平均 MAD**，容差 8.0（Block A/B）/ 4.0（plan）/ 6.0（level）。
float 舍入至多把单个通道 `uchar` 翻 ±1；即便最坏「每像素每通道差 1」，MAD=1.0 ≪ 各容差；真实 float↔double
分歧只翻转极少数像素，MAD 变化 ≪0.1。

### 4.2 保持 double / 整型，禁止切 float

- **整型截断语义不可动**（切 float 会破坏金样）：`toL :166-169`（定点 19595/38470/7471）、
  `ycbcrSplit :1281-1300`（`long long int` 截断，`:1293` 注释明言阈值敏感）、`denoise` YCbCr 拆分
  `:803-806`、`rgb2hsv` 六扇区整型 `:200-208`、`chopMul`/`screen`/`composite` 整型 `/255`。
- **测量/决策 double 保持 double**：§3.2 所列归约、percentile、`skinMask` 阈值经 `areaResample` 均值保持
  `:1497-1498`、`dehaze` `partial_sort` 破并列——这些是「比较阈值」处，float 舍入可翻转**二元判定**，
  进而改 step-key 序列。保持原精度 → 判定不变 → 精确门不破。

---

## 5. 门禁与测试策略（铁律 #20 门与实现同源）

### 5.1 现状：`LA_LUMEN_SMOKE`（`runLumenSmoke` `main.cpp:5582-5736`）判据

- 注册：`gate_matrix.ps1:103 @{n="LA_LUMEN_SMOKE"; png=$false; timeoutSec=90; measuredMs=66585}`，
  **无 tier 字段 = full 档**（push/release/local 跑，非每 PR）；`timeoutSec=90` 是**墙钟 kill 预算**，
  `measuredMs=66585` 是遥测（仅 E4 验字段**存在**，非对实测时长断言）；`LA_LUMEN_SMOKE` 由 env 自退派发
  （`main.cpp:6398-6399`）。
- **Block A autoPipeline**（`:5589-5635`）：尺寸**结构**必须相等（否则 mad=999 FAIL）+ RGB **平均 MAD**
  `kMadLimit=8.0`（`:5586`）；step-key 序列**精确相等** `keys == goldKeys`（`:5621-5623`）；综合
  `pass = n>0 && stepsOk==n && madMax<=8.0`（`:5632-5633`）。
- **Block B 12 显式算子**（`:5655-5682`，kOps 见 `:5638-5639`）：`madOf` 平均 MAD，`mad>8.0` 判负。
- **Block C retouchWithPlan**（`:5684-5707`）：`kPlanLimit=4.0`（最紧）。
- **Block D level**（`:5709-5732`）：`kLevelLimit=6.0` + `angErr>1.0`（`levelDetectAngle` ±1.0° 容差）。
- 金样从盘读（`LA_GOLD_DIR`，默认路径已含全部 fixture），无内嵌字节数组、无 memcmp。
- **无**确定性重跑断言、**无**门内时长断言。

### 5.2 新增决定性子断言（本轮核心防护，同提交落）

在 `runLumenSmoke` 内，对代表性热点各跑两次并**逐字节比对**（不等则 FAIL 并打印差异首坐标）：

```cpp
// 对每个纳入并行的 op（至少 lensCorrect、vignetteCorrect、一个经 gaussianBlur 的 op 如 glowStyle/toneMapHdr、
// 一个 median/min 如 portraitBeautify/dehaze、一个 float-convert 如 hsv2rgb 路径）：
//   outSerial = op(in)  在 LA_LUMEN_THREADS=1 环境进程下
//   outPar    = op(in)  在默认（并行）环境进程下
//   断言 outSerial 与 outPar 每字节全等（不是 MAD 容差，是 memcmp==0）。
```

落地形态：因门是 env 自退单进程，实现改为门内**同进程两跑**——第一跑前用 `qputenv("LA_LUMEN_THREADS","1")`
+ 复位，取基准；第二跑 `qunsetenv` 取并行；`memcmp` 两 QImage `constBits()`（同尺寸同格式）逐字节相等。
- **为何这比 MAD 强**：并行若引入任何跨块写竞态、或误把某归约并行化改变了结果，逐字节比对**必红**，
  即便 MAD 仍在容差内（铁律 #34 反例要咬得住）。
- **与既有精确断言的边界**：step-key 序列 / 尺寸 / 角度 由 §3.2「测量保持串行」保护不回归；本断言保护
  「并行化不改变像素」。二者互补。

### 5.3 反例钩子（证明门不是空心）

- 临时把某**跨块共享写**塞进并行体（例：让 `redeyeRemove` pass1 的 `cnt` 直接并行自增共享变量而不做每线程
  局部和），串行/并行两跑逐字节比对 → 该门须变 RED。验证后回退此实验性改动。
- float 越界反例：把 `ycbcrSplit` 截断改 float → Block A step-key 或 skinMask 阈值应翻转致 MAD/精确项变红。

### 5.4 measuredMs 回填（铁律 #25/#31 实测非估）

全量对拍绿后，`gate_matrix.ps1:103` `measuredMs=66585` 用 `qa_runner.ps1 -Timing` **实测**回填预期大幅下降值。
并行改造后 `timeoutSec=90` 大概率可下调（headroom 仍留），是否下调在收口时依实测决定并同步注释（纯 ASCII）。

### 5.5 全量回归

改造后跑 full tier 全量（现状基线 PASS=62/FAIL=0/CHECK=0/SKIP=12/KILL=0，roster 74）；除 `LA_LUMEN_SMOKE`
外其余门**不得**变化。特别注意 `LA_LUMEN_OPS_UI_QA`（#73，静态接线门，`covered=13/13`）与
`LA_RETOUCH_A_QA`（读 op 行为）不受引擎内部改造影响。

---

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| 并行误改归约/排序 → step-key 序列变（精确断言破） | §3.2 测量/归约/排序**一律串行**；§5.2 逐字节比对兜底 |
| float 翻转阈值二元判定 → MAD/角度/step-key 破 | §4.2 精度敏感处保持 double/整型；仅 §4.1 量化后颜色算术切 float |
| QImage detach 在并行体内发生 → 数据竞争 | 并行调用前在单线程点完成 detach/取可写缓冲；体内只 `scanLine(y)` 自己行 |
| halo 滤波邻域读到未写区 | halo 滤波只读**源**（只读、已完整），写独立 out；lens tile 读只读 pad → 块间无依赖 |
| idealThreadCount 异常值 | <=0 兜底为 1（串行退化） |
| addGrain 噪声不可重现 | 保持串行（§3.2） |
| 小图并行开销 > 收益 | 分块 `nchunk=min(h, nt*4)`；h 极小时退化为少块或单块；必要时 `LA_LUMEN_THREADS=1` |

---

## 7. 性能预期

- 逐像素映射与 `gaussianBlur` 近线性于 `idealThreadCount`（典型桌面 4–8 物理核 → 期望 4–6×）。
- `gaussianBlur` 为**单一最高杠杆**：`denoise/clarity/structure/toneCurve/glowStyle/toneMapHdr/dodge/
  skinMask→portraitBeautify/dehaze/planClarity/paramTone/hpStats` 皆经它。
- 量化指标 = `LA_LUMEN_SMOKE` 实测 `measuredMs` 下降（66585 → 实测回填）。用户体验面 = RetouchPage
  一键增强各 op 的全分辨率墙钟延迟（第二轮整算子后台化消除了 UI 冻结，本第三轮进一步压缩**单次算子
  完成延迟**）。

---

## 8. 触及文件

- `native/src/core/LumenEngine.cpp`：新增 `lumenParallelRows` + 各热点循环改写 + §4.1 float 切换。
- `native/src/core/LumenEngine.h`：**预计不改**（helper 文件局部）；若 `LA_LUMEN_THREADS` 需对外文档则加注释。
- `native/src/main.cpp`：`runLumenSmoke` 增 §5.2 串行/并行逐字节决定性子断言。
- `native/CMakeLists.txt`：**不改**（Qt6::Concurrent 已链接 `:19`/`:139`）。
- `native/gate_matrix.ps1`：`measuredMs` 实测回填（§5.4）；如降 `timeoutSec` 同步**纯 ASCII** 注释（铁律 #37）。
- 收口：`native/CHANGELOG.md`（新增 `[2.39.0]`）、`native/docs/迭代指导书.md`（§3 门数不变 74、§7 路线 6 性能行）。
  使用说明书**本轮不新增**（先例：2.38.0 未随码出册；说明书按发布需要在打包 stage 补，且存放于仓库根 `docs/`）。

---

## 9. 明确不做（YAGNI）

- **不并行化归约/排序**（#B 方案否决）：加法顺序 → step-key 精确门破，收益低。
- **不引 OpenMP/TBB/std::execution**（#C 方案否决）：新依赖，项目已用 QtConcurrent；且主机限制。
- **不碰 AI 索引去外部依赖 / GPU-RHI 渲染路径 / 统一引擎注册表**（路线 4/6-full/7）：留后续轮次。
- **不改 A/V 同步（路线5）/ StoryGen 人物主题（#6）**：与本弧正交。
- **不动第二轮已交付**：RetouchPage 整算子后台化（`QtConcurrent::run :995`）、13 算子 UI 接线、EXIF 容器、
  Erase 淘汰/取消 —— 本弧只在 `LumenEngine.cpp` 内部动。
- **不改 `LA_LUMEN_OPS_UI_QA` 反例逻辑**（第二轮 FIX C 刚去分支化，铁律 #22 已合规）。

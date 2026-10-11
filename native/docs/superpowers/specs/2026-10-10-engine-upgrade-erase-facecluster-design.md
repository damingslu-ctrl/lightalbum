# 设计规格：引擎升级 双路线（EraseBridge 补全 + FaceCluster 算法升级）

- 日期：2026-10-10
- 版本：2.34.0 → **2.35.0**（新功能 = MINOR，单一真源 `native/CMakeLists.txt`）
- 状态：设计已确认，待实施
- 范围：全仓 7 条引擎升级路线（见上轮"引擎升级路线路线图"）中，本轮**仅**做路线 1 与路线 2；其余 5 条留路线图 TODO，不动。

---

## 0. 背景与取证

客户端有 28 个引擎级子系统。上轮取证定位两处短板：

**EraseBridge（`src/core/EraseBridge.{h,cpp}`）**——全仓唯一残缺引擎。58 行表单壳，`modelReady()` 恒 false（`EraseBridge.cpp:14`），任务全部停在 `blocked_no_model`，**无执行器、无门禁**。UI 侧（`RetouchPage`）**已 100% 接好**：涂抹蒙版 → `submit(geoImg_, canvas_->mask())`（`RetouchPage.cpp:998`）→ `onEraseTaskUpdated` 回填 `afterImg_`/`localOut_`/预览（`RetouchPage.cpp:1002-1018`）。唯一缺口是模型执行器 + `modelReady()` 变真。

**FaceCluster（`src/core/FaceCluster.cpp:34-49`）**——贪心 leader 单遍聚类，仅与每簇**首张脸（head rep）**比余弦：
- **顺序敏感**：不同输入顺序产生不同划分；
- **代表 = 首张脸**：信息量最少的那张，精度受限（文档自承）。
守恒契约（`outputCount==inputCount`）+ 反例钩子 `setForceBrokenForQa` 已就绪，`LA_FACE_CLUSTER_QA` 已注册。

**既有重依赖先例（决定架构选型）**：
- RAW：`dcraw_emu`（LibRaw **MSVC 预编译**）走**独立进程**（QProcess）做 ABI 解耦——宿主 MinGW 与解码器 CRT 彻底隔离（`RawDecode.h`）。
- 视频/音频：ffmpeg 走**独立进程 + 裸流管道**，同时隔离 GPL-3.0 传染（`VideoDecode.h`/`AudioPlayer.h`）。
- 二者均：build 时 `fetch_*.ps1` 下载到 `3rdparty/<vendor>/bin/`（不入库），运行时相对 exe 目录查找，`release.ps1` staging 拷贝并做许可 C-scan 硬卡（MIT 附声明、GPL/LGPL 附 LICENSE 文件，缺失即 `throw`）。
- `fetch_ffmpeg.ps1` 明确接受 zip 体积 +~200MB（ffmpeg+ffprobe 各 ~100MB）换视频能力。

---

## 1. 决策记录（已与用户逐项确认）

| # | 决策 | 选定 |
|---|---|---|
| D1 | 消除后端 | 本地 ONNX 大模型（LaMa 类修复） |
| D2 | 进程边界 | **独立子进程 `inpaint.exe`**（方案 A，复用 dcraw/ffmpeg 管道范式）——ABI 隔离 + 崩溃半径可控 + 许可隔离 |
| D3 | 本轮范围 | **接真模型**（非仅搭骨架） |
| D4 | 模型 | **SAIC-MLP/LaMa ONNX（~191MB）**，`fetch_lama.ps1` 下载时**硬校验 LICENSE 文件存在**（无 license → fetch 失败，不许进入打包） |
| D5 | 包体 | 接受 `release.zip` 17.4MB → **~230MB**（模型主导，同 ffmpeg +200MB 先例） |
| D6 | inpaint.exe 构建 | **独立 MSVC 项目**（`inpaint/`，不进 MinGW CMake），运行时经 QProcess 调用 |
| D7 | FaceCluster 升级 | 两遍：贪心种子 + 迭代质心精炼（R ≤ 3，早停）；**API 签名零变化** |
| D8 | FaceCluster 成本 | 接受 ~10s → **~25–30s**（QtConcurrent 后台一次性作业，不阻 GUI） |
| D9 | YAGNI 砍掉 | 增量聚类（需 DB schema）、阈值可配、DBSCAN/HDBSCAN —— **全部留作下轮** |
| D10 | 发版 | 两路线**合并一版发 2.35.0**，§7 台账一行合并 |

---

## 2. 路线 1 —— EraseBridge 从桩到真引擎

### 2.1 架构与数据流

```
RetouchPage (UI，已全接线，本轮不改一行)
    │  submit(base, mask)
    ▼
EraseBridge (QObject，GUI 线程)
    │  queued 任务经 QProcess 送 inpaint.exe
    ▼
EraseRunner (新，EraseBridge 持有的 QProcess 封装，常驻子进程)
    │  stdin  : PPM(base) + PGM(mask alpha)
    ▼  stdout: PPM(result)
inpaint.exe  [新，独立 MSVC 项目产物]
    ├─ onnxruntime.dll   (ONNX Runtime C++ API)
    └─ lama.onnx         (SAIC-MLP LaMa，~191MB)
    │
    ▼  读回 QImage
EraseBridge::applyResult(id, result) → emit taskUpdated(id) → UI 回填
```

`inpaint.exe` 随 `EraseBridge` 生命周期**常驻**，摊薄 spawn 开销；一次任务 = 一次协议往返。

### 2.2 管道协议（inpaint.exe 契约）

- **启动握手**：进程拉起后首行 stdout 输出 `READY <w> <h> <maxbytes>`；`EraseRunner` 读到该行才认 `modelReady()==true`。
- **单任务请求**（stdin，长度前缀帧，纯 ASCII 头 + 二进制体）：
  `TASK <id> <w> <h>\n` → 二进制 PPM(P6) base → 二进制 PGM(P5) mask（255=消除区）。
- **单任务响应**（stdout）：`OK <id>` 或 `ERR <id> <reason>` → 成功则紧跟二进制 PPM(P6) result。
- **诚实语义**：`inpaint.exe` 绝不返回"与原图相同却声称成功"——mask 内必有改动（门禁 B 组逐像素核验）。

### 2.3 关键契约

| 契约点 | 语义 |
|---|---|
| `modelReady()` | `inpaint.exe` 存在 **且** `lama.onnx` 存在 **且** 首行握手 `READY` 成功（取代桩的恒 false） |
| 任务状态机 | `queued → running → done \| failed`（**新增 `running`**）；`blocked_no_model` 仅当 `modelReady==false` 时使用（保留原诚实挂起） |
| 失败判定 | 进程非零退出 / stdout `ERR` / result 空或尺寸 ≠ base → `applyResult` 走**既有** `failed` 分支（`EraseBridge.cpp:39-42`），绝不伪造 `done` |
| 零云端 | `inpaint.exe` 不发起任何网络请求；模型仅 `fetch_lama.ps1`（用户主动构建期下载）落盘 |
| 隐私（铁律 21） | 送进 `inpaint.exe` 的是 `geoImg_`（编辑态图），继承 RetouchPage 既有私密过滤，注入点已过滤，本层不再遍历 |

### 2.4 门禁 `LA_ERASE_QA`（full 层，新第 70 门）

正例（需 `3rdparty/lama/bin/lama.onnx` + `inpaint.exe`；缺失 → SKIP，同 `LA_SCAN_*` 语义，非回归）：

- **A 契约（状态机走通）**：提交合法任务 → 观测 `queued→running→done` 全序；`applyResult(id, null)` 或错尺寸 result → `failed`；未 fetch 模型 → 诚实 `blocked_no_model`（`note` 含"零云端/未接入"，不谎报 done）。
- **B 守恒（像素判别，最强断言）**：200×200 已知图案 + 中央 40×40 圆形蒙版 → ①**mask 外**每像素逐字节 == base（消除不得泄漏到蒙版外）②**mask 内** mean-abs-diff > 阈值（证明确实动了，非原样返回）。反例把 mask 外改动一点即转红（非空心，铁律 34）。
- **C 常驻复用**：同一 `EraseBridge` 连投 3 任务 → `inpaint.exe` 的 `processId()` 三次恒等（进程只起一次）。
- **D 崩溃恢复**：任务间 `kill` 子进程 → 当前任务 `failed`；下一任务自动重 spawn 并 `done`（坏任务不牵连后续）。

反例 `LA_ERASE_QA_FORCE_BROKEN=1`：`EraseRunner` 故意把 result 尺寸报错配（模拟执行器产出畸形结果），期望式一字不改（铁律 22）→ 断言 B 守恒与 A 的 done 语义双双转红。

名册注册（`gate_matrix.ps1`，全 ASCII 注释，铁律 37）：`@{n="LA_ERASE_QA"; png=$false; tier="full"; measuredMs=<从 -Timing 实测回填>}`。

### 2.5 打包 / 许可（`release.ps1` + `fetch_lama.ps1`）

- 新增 `fetch_lama.ps1`（**纯 ASCII**，铁律 7：PS 5.1 无 BOM 按 GBK 解码，含中文注释会"unterminated string"）：下载 `lama.onnx` + `LICENSE` + `inpaint.exe` + `onnxruntime.dll` 到 `3rdparty/lama/bin/`。
- 新增许可契约 **C9**：`3rdparty/lama/LICENSE` 必须存在且非空（对齐现有 `LICENSE.LGPL` 检查），缺失即 `throw` 拒绝打包（一个永不失败的契约 = 没有契约，见 2.21.0 教训）。
- `release.ps1` staging 拷贝 `inpaint.exe + onnxruntime.dll + lama.onnx + LICENSE`；`SelfTest` 注入"删 LICENSE → C9 转红"。

### 2.6 交付物清单

**新增文件**：
- `inpaint/inpaint.cpp`（~150 行，C++ + ONNX Runtime C++ API，stdin/stdout 管道协议）
- `inpaint/CMakeLists.txt`（独立 MSVC 项目，产 `inpaint.exe`）
- `inpaint/BUILD.md`（MSVC 构建 + ONNX Runtime 链接步骤，供复现）
- `native/fetch_lama.ps1`（下载模型 + 硬校验 LICENSE，ASCII）
- `native/src/core/EraseRunner.{h,cpp}`（QProcess 封装：握手、投喂、读回、崩溃重启）

**改动文件**：
- `native/src/core/EraseBridge.{h,cpp}`（真 `modelReady()` + 状态机扩 `running` + 持有 `EraseRunner`）
- `native/CMakeLists.txt`（版本 2.35.0 + staging 块）
- `native/gate_matrix.ps1`（注册 `LA_ERASE_QA`）
- `native/src/main.cpp`（`runEraseQa()` + 派发，置于早退分支，仿 `LA_TRANSITION_QA`/`LA_MOTION_QA` 兄弟门不进 animOk）
- `native/release.ps1`（C9 许可 + staging + SelfTest 注入）
- `CHANGELOG.md` `[2.35.0]` 路线 1 节

### 2.7 UI

`RetouchPage` 已完全就绪，**本轮不动 UI**。`modelReady()` 从桩变真即全线打通。

---

## 3. 路线 2 —— FaceCluster 从贪心到两遍精炼

### 3.1 算法

```
1. 预归一化：每个 emb → 单位长度（余弦降为点积，省 sqrt + 两次 norm，约 2× 提速）
2. 种子：现有贪心 leader（阈 threshold，默认 0.4）——O(n·k)
3. 精炼循环 R ≤ 3：
     a. 每簇质心 = mean(单位 emb)，再归一化
     b. 每 item 重新分配到 nearest 质心；若 max cos < threshold → 新建 singleton 簇
     c. 本轮无任何 reassignment → 早停
4. 输出 ClusterResult（groups + 守恒计数）
```

- **顺序不变性**来自精炼收敛：种子阶段的顺序差异被重分配抹平。
- **代表 = 质心**（向量和），不再取单张头脸。
- **守恒天然保持**：每遍重分配都是全量 partition，每 item 恰属一簇。

### 3.2 契约保持（调用侧零改动）

- **API 不变**：`ClusterResult cluster(const QVector<ClusterInput>&, double threshold, int startIndex)` 签名一字不改。`main.cpp:3667 runFaceCluster` 的读回 emb → 聚类 → `AiWriter::applyClusters` 流程不动。
- **反例钩子不变**：`setForceBrokenForQa(true)` 仍在末尾从首个非空簇丢一张 → 原守恒断言转红。
- **标签编号不变**：`"人物%1".arg(startIndex + gid)`（gid 单调）。

### 3.3 门禁强化 `LA_FACE_CLUSTER_QA`（现有 A–H 八组不动，新增 O/C/S/D 四组）

| 组 | 断言 | 现贪心 | 两遍精炼 |
|---|---|---|---|
| **O** 顺序不变性 | 同一 12-item 集 3 种排列 → **集合意义**上分区相同（不要求簇序） | ❌ 必红 | ✅ |
| **C** 质心优于头 | 每簇 `mean(cos(item,centroid)) ≥ mean(cos(item,head_rep))` | ❌ | ✅ |
| **S** 同人必同簇 | 5 近重复 emb（同向量 + 高斯噪声 σ=0.02）随机排 3 次均同簇 | ⚠️ 顺序敏感 | ✅ |
| **D** 异人必分簇 | 5 正交 emb 必各成一簇 | ✅ 已 | ✅ |
| 反例 1（原）| `LA_FACE_CLUSTER_QA_FORCE_BROKEN=1` 丢一张 → **A 守恒** 转红 | — | — |
| **反例 2（新）**| `LA_FACE_CLUSTER_QA_FORCE_MERGE=1` 全塞 group 0 → **D 分簇** + **C 质心** 双转红 | — | — |

合成测试集确定性生成（固定种子 / 硬编码向量，铁律 29：无随机、宿主无关）。`measuredMs` 因算法变重需从 `-Timing` **重测回填**（铁律 30/31）。

### 3.4 交付物清单

**改动文件（均无 UI/schema 变更）**：
- `native/src/core/FaceCluster.cpp`：`cluster()` 扩到 ~120 行（归一化 + 种子 + 精环 + `qaMergeAllForQa` 第二反例）
- `native/src/core/FaceCluster.h`：**无改动**（内部工具 `normalize()` 设私有静态）
- `native/src/main.cpp::runFaceClusterQa`：追加 O/C/S/D 四组 + FORCE_MERGE 反例
- `native/gate_matrix.ps1`：`LA_FACE_CLUSTER_QA` 注释块补新组 + `measuredMs` 重测回填
- `CHANGELOG.md` `[2.35.0]` 路线 2 节

---

## 4. 验证计划（§1 第 ⑥ 步）

1. **构建**：`cmake --build build -j 4`（MinGW）——主程序零新链接依赖（`inpaint.exe` 独立 MSVC 项目，不参与 MinGW 构建）。
2. **inpaint.exe 就位**：`fetch_lama.ps1` → 确认 `3rdparty/lama/bin/{lama.onnx,inpaint.exe,onnxruntime.dll,LICENSE}` 齐全。
3. **新门实测**（PowerShell only，per 记忆 `gates-run-via-powershell`）：
   - `qa_runner.ps1 -Only LA_ERASE_QA` → PASS；`LA_ERASE_QA_FORCE_BROKEN=1` → FAIL（非空心）。
   - `qa_runner.ps1 -Only LA_FACE_CLUSTER_QA` → PASS（含 O/C/S/D）；两个 FORCE 反例 → FAIL。
   - `-Timing` 回填两门 `measuredMs`。
4. **名册自检**：`gate_matrix.ps1 -SelfTest` → E1 gates=70 fast=18 full=52；E11 nonAsciiBytes=0；E12 textual==parsed；fails=0。
5. **全量回归**：`qa_runner.ps1`（tier=all，70 门）→ `FAIL=0 CHECK=0 KILL=0`；SKIP 集合仅变化在需要 lama 素材时。
6. **打包端到端**：`release.ps1` → `RELEASE OK` 零 WARN；`-SelfTest` 的 C9 注入（删 LICENSE）转红；核对 zip 内含 4 件 lama 产物 + 体积符合预期。

---

## 5. 铁律合规映射

| 铁律 | 落点 |
|---|---|
| 20 门禁与实现同源 | 两门均测真执行器/真算法，非另立桩 |
| 22 期望不随开关分支 | 两门反例期望式恒为规格值 |
| 25/30/31 先测后定 | measuredMs 全部从 `-Timing` 实测回填，不估 |
| 29 宿主无关/无随机 | FaceCluster 合成集确定种子；Erase 用固定图案蒙版 |
| 34 禁空心门 | Erase B（mask 内必改）、FaceCluster O（现算法必红）都是强判别 |
| 37/7 .ps1 纯 ASCII | `gate_matrix.ps1` 注释、`fetch_lama.ps1` 全 ASCII |
| 6/21 注入点一次过滤 | Erase 吃编辑态图，继承私密过滤，不另立遍历 |

---

## 6. 记忆 / 台账（§1 第 ⑧ 步）

- `CHANGELOG.md` 新增 `[2.35.0]`（路线 1 节 + 路线 2 节 + 回归行）。
- `docs/迭代指导书.md`：§3 门数 69→**70**（fast 18 + full 52）；§7 台账一行 `N-erase-facecluster`。
- 项目记忆新增两条：
  - `erase-inpaint-external-process.md` —— QProcess 常驻管道协议 + 模型许可硬校验（路线 1 范式，供未来重依赖接入照抄）。
  - `face-cluster-two-pass.md` —— 两遍精炼算法 + O/C/S/D 断言设计（路线 2 决策）。
- 路线图更新：路线 3–7 保留在"引擎升级路线图"TODO（本轮未做，勿据此认为已做）。

---

## 7. 风险与回退

| 风险 | 缓解 |
|---|---|
| LaMa 权重许可非纯 MIT | `fetch_lama.ps1` 硬校验 LICENSE 存在 + `release.ps1` C9 打包卡；许可存疑则 fetch 失败，不进入产物 |
| ONNX Runtime 版本/模型格式不匹配 | `inpaint.exe` 版本化随 `lama.onnx` 一起 fetch；握手 `READY` 失败 → 诚实 `blocked_no_model`（不崩主程序） |
| FaceCluster 变重拖慢首启 | 已确认在 QtConcurrent 后台，不阻 GUI；R ≤ 3 早停控成本；measuredMs 实测暴露回归 |
| 包体 230MB 引发发布/下载问题 | 与 ffmpeg +200MB 同量级先例；如否决，路线 1 可切"只搭骨架 + 合成执行器"（本轮已选接真模型，此项仅备查） |
| inpaint.exe 独立 MSVC 构建不可复现 | `inpaint/BUILD.md` 记录完整步骤 + 版本锁定；`fetch_lama.ps1` 优先下预编译产物 |

**回退**：全程在既有未提交工作之上增量进行，绝不 `git reset/checkout/clean`；每路线独立可回退（路线 2 纯函数改动，回退即还原 `FaceCluster.cpp`；路线 1 新文件为主，`EraseBridge` 改动可控）。

---

## 8. 未决 / 后续（明确不在本轮）

- 路线 3 MotionKit 预设扩容、路线 4 AI 索引去外部依赖、路线 5 A/V 同步、路线 6 LumenEngine GPU、路线 7 统一引擎注册表。
- FaceCluster 增量聚类 / 阈值可配 / 密度聚类（D9 砍掉项）。
- 端到端真机（非 offscreen）验证消除效果与聚类观感。

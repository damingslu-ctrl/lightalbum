# 引擎升级第四轮（AI 索引去外部化）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 CLIP 分类与人脸检测两条外部 Python HTTP 链路替换为随包 ONNX 子进程（faceinfer.exe / clipinfer.exe），版本 2.40.0，门禁名册 74→78。

**Architecture:** spec `native/docs/superpowers/specs/2026-10-13-engine-round4-local-ai-subprocess-design.md`（commit fc72f48）是裁定权威。两个独立 MinGW 子进程工程（inpaint/ 模板）+ `core/AiSubprocessRunner` 共享底座 + 门面 HTTP→子进程净替换 + 平价黄金对拍（oracle=本机 face_server.py / venv_cake open_clip 同进程直调，无 HTTP）。

**Tech Stack:** C++17/Qt6.8.3 MinGW、onnxruntime C API v1.19.2（复用已钉死 DLL + ort_mingw_shim.h）、Python 仅开发期 oracle/导出（venv_face、venv_cake，不进运行时）。

## Global Constraints

- 版本 `2.40.0`；roster 74→**78**（fast +2：#75 LA_FACE_AI_QA、#76 LA_CLIP_AI_QA；full +2 opt-in：#77 LA_CLIP_MODEL_QA、#78 LA_FACE_MODEL_QA）。
- 零新依赖（onnxruntime.dll 三处 bin 各放同一钉死副本）；零云端；faceinfer/clipinfer 仅 CPUExecutionProvider。
- 铁律 #20（门与实现同提交）、#25/#31（measuredMs 只来自 `-Timing` 实测回填）、#34（反例必须咬）、#37（gate_matrix.ps1 新增行纯 ASCII）。
- **EraseRunner / EraseProto.h / LA_ERASE_QA 一行不动**（spec §2.2）；g_synthetic/g_syntheticEmb 钩子原样保留。
- 真人脸 fixture 图与黄金 JSON **绝不入库**（env-only，spec §7#78 隐私红线）；用户真实样本路径只走环境变量。
- 永不 push、不 `git add -A`、不 `--no-verify`、不跑 `lupdate`；不删 `native/build/3rdparty|native/3rdparty` 下 lama 三件套。
- 构建/QA 一律 PowerShell（命令模板见下）；src 改动提交前先重建（pre-commit stale-exe 守卫）。
- 平价移植常量 verbatim 来源 = `D:/lightalbum_ai/face_server.py` 与 `classifier_server.py`（oracle 源码），逐行移植即忠实翻译，评审不得按「重复逻辑」缺陷处理（控制器预裁定 R-P1：oracle verbatim 移植是本 spec 的正确性机制本身）。
- SHA 钉死：detection.onnx `5838f7f…5b91`、recognition.onnx `4c06341…9e43`（spec 附录 A，已复核）。
- 构建命令模板：`powershell -ExecutionPolicy Bypass -Command "Set-Location 'D:\Test\workbuddy\2026-09-18-09-00-13'; $env:PATH='C:\Users\damin\.workbuddy\binaries\python\envs\default\Lib\site-packages\cmake\data\bin;D:\qt\Tools\mingw1310_64\bin;'+$env:PATH; cmake --build native/build --target LightAlbum -j 8"`；门：`& native\qa_runner.ps1 -Exe native\build\LightAlbum.exe -Only <GATE> [-Timing]`。

## File Structure（新增/修改一览）

| 路径 | 责任 |
|---|---|
| `native/src/core/AiProto.h` | 新：REQ/RES/ERR 帧读写（体必读满），face/clip/桩三方共用单一事实源 |
| `faceinfer/CMakeLists.txt` `faceinfer.cpp` `scrfd.h/.cpp` `align.h/.cpp` `faceinfer_stub.cpp` | 新：人脸子进程（SCRFD+ArcFace）+ hermetic 桩 |
| `clipinfer/CMakeLists.txt` `clipinfer.cpp` `preproc.h/.cpp` `rules.cpp` `clipinfer_stub.cpp` | 新：分类子进程（ViT-B-32 塔+规则）+ hermetic 桩 |
| `native/src/core/AiSubprocessRunner.h/.cpp` | 新：owner-QThread 持久 QProcess + READY 握手 + 同步 callBlocking + 单飞守卫 |
| `native/src/core/FaceRunner.h/.cpp` `ClipRunner.h/.cpp` | 新：findExe/模型 sibling 校验/REQ 组装门面 |
| `native/src/core/PersonDetector.cpp` `Classifier.cpp` | 改：传输层替换（HTTP→runner），JSON 解析零改动 |
| `native/src/ui/MainWindow.cpp` | 改：3 处调用去 url 实参 + 2 组「未就绪」文案 |
| `native/src/main.cpp` | 改：4 个新门 runXxxQa + QA 分派 |
| `ai_export/export_clip_onnx.py` `gen_face_fixtures.py` `gen_clip_fixtures.py` `README.md` | 新：一次性导出 + 黄金 oracle 脚本（开发工具） |
| `native/fetch_ai.ps1` + `native/3rdparty/{face,clip}/**` | 新：SHA 钉死/verify-before-land/LICENSE 硬检 + 入库树 |
| `native/release.ps1` | 改：C10 合约（throw×3 注入测试）+ 装配腿 ×2 |
| `native/gate_matrix.ps1` `CMakeLists.txt` `CHANGELOG.md` `docs/迭代指导书.md` | 改：名册/版本/文档收口 |

构建子进程 exe：`cmake -S faceinfer -B faceinfer/build`（MinGW 独立工程，同 inpaint 惯例）；产物手工拷至 `native/3rdparty/face/bin/`（inpaint.exe 先例）。

---

### Task 1: AiProto.h + faceinfer 工程骨架 + 桩 + #75 门（协议壳先行）

**Files:**
- Create: `native/src/core/AiProto.h`、`faceinfer/CMakeLists.txt`、`faceinfer/faceinfer_stub.cpp`、`faceinfer/faceinfer.cpp`（仅 main 循环壳 + `--version` 应答）
- Modify: `native/src/main.cpp`（runFaceAiQa + 分派）、`native/gate_matrix.ps1`（注册 #75，fast）

**Interfaces:**
- Produces: `aiproto::` 命名空间：`bool readLine(QIODevice&, QByteArray&, int timeoutMs)`、`bool readExactly(QIODevice&, QByteArray&, qint64 n, int timeoutMs)`、帧常量 `REQ/RES/ERR/READY` 前缀串、`struct Frame { quint64 id; QByteArray kind; QByteArray payload; }` 解析器。桩/真 exe/runner 三方都 include 此头。
- Produces: 桩行为 env 钩子表（E1 组，供 #75）：`LA_FACE_STUB=1` 强制桩、`LA_FACE_FORCE_NOMODEL/ERR/HANG/SLOW/FRAG=1` 对应失败模式。

- [ ] **Step 1: AiProto.h 全文**（新建，单一头，无 Qt 依赖以便独立工程复用）：

```cpp
#pragma once
// AI 子进程线协议（spec §5）。stdout 只走帧，日志只走 stderr。
// 血泪规约（EraseProto 判例继承）：RES 体必读满——waitForReadyRead 的
// 到达批次边界任意，readExactly 循环直到 n 字节或超时。
#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#include <io.h>
#define AI_PROTO_FORCE_BINARY() _setmode(_fileno(stdout), _O_BINARY)
#else
#define AI_PROTO_FORCE_BINARY() ((void)0)
#endif

namespace aiproto {
inline const char* kReq   = "REQ ";
inline const char* kRes   = "RES ";
inline const char* kErr   = "ERR ";
inline const char* kReady = "READY";

struct Header {
    unsigned long long id = 0;
    char kind[16] = {0};   // "path" | "jpg"
    unsigned long long len = 0;
};

// 解析 "REQ <id> <kind> <len>"。返回 false = 坏帧。
inline bool parseReq(const char* line, size_t n, Header& h) {
    char tok[8] = {0};
    unsigned long long a = 0, c = 0;
    int used = 0;
    if (std::sscanf(line, "%7s %llu %15s %llu %n",
                    tok, &a, h.kind, &c, &used) < 4) return false;
    if (std::strncmp(tok, "REQ", 3) != 0 || used <= 0) return false;
    if (static_cast<size_t>(used) != n) return false;
    h.id = a; h.len = c; return true;
}
} // namespace aiproto
```

（`readLine`/`readExactly` 在 C++ 侧由 AiSubprocessRunner 用 QProcess 实现；在子进程侧由各 .cpp 内一个 20 行 stdin 读器实现——两处实现都受 #75 FRAG/超时断言覆盖。）

- [ ] **Step 2: faceinfer_stub.cpp**（模式=inpaint_stub：确定性回显 + FORCE env 失败矩阵）。桩对任意 `REQ id path len`+body：`FRAG=1` 时把 RES 体按 64B 分片 fprintf；`ERR=1` 回 `ERR id loadfail`；`HANG=1` 睡 90s 不回帧；`SLOW=1` 每帧睡 3s；否则回确定性 JSON：`{"ok":true,"count":1,"faces":[{"bbox":[0,0,8,8],"score":0.9,"kps":[[1,1],[2,2],[3,3],[4,4],[5,5],],"embedding":[0.0 x512]}]}`。`READY 1` 起步；`NOMODEL=1` 则 `READY 0` 后每 REQ 回 `ERR id nomodel`。
- [ ] **Step 3: faceinfer/CMakeLists.txt**：照抄 `inpaint/CMakeLists.txt` 结构（MinGW `-static` + `-include ort_mingw_shim.h` + `ONNXRUNTIME_ROOT`），目标 `faceinfer` + `faceinfer_stub`（桩不链 ORT，纯 winmm/stdio）；POST_BUILD 拷 onnxruntime.dll。**本任务 faceinfer.cpp 只是壳**：链 ORT 会话创建留空 TODO-free——壳直接 `READY 0`（模型路径检查失败分支），保证编译+链接+帧协议自检通过；T2/T3 填实现。
- [ ] **Step 4: main.cpp `runFaceAiQa()`**（E 组 G1-G8 断言见 spec §7：handshake/nomodel/roundtrip/FRAG/ERR/timeout+restart/busy-single-flight/exe_missing；runner 尚未存在 → 本任务门**只发 G1 子集**：以 QProcess 直起桩、协议级断言 G1(READY1)/G2(READY0+nomodel)/G4(FRAG 读满)/G5(ERR 帧)；G3/G6/G7/G8 随 T4 接入 runner 后补全）。计数令牌行 `LA_FACEAI_G <name> <k=v>…`。FORCE 反例：篡改桩期望表（qaShoot 反例分支恒 exit 路径按铁律 #22 不分支）。
- [ ] **Step 5: gate_matrix.ps1 注册 #75**（fast tier，timeoutSec=30，measuredMs 先填 0 由 -Timing 回填）。E11/E12 纯 ASCII 自检。
- [ ] **Step 6: 构建 + 桩构建 + 门绿 + 反例咬合**（跑两次 FORCE 反例并留输出）+ Commit ×1（门与实现同提交，铁律 #20）。

---

### Task 2: SCRFD 检测移植 + oracle 黄金脚本

**Files:**
- Create: `faceinfer/scrfd.h/.cpp`；Modify: `faceinfer/faceinfer.cpp`（接 ORT det 会话）
- Create: `ai_export/gen_face_fixtures.py`、`ai_export/README.md`

**Interfaces:**
- Consumes: T1 的 AiProto.h、faceinfer 壳。
- Produces: `Faceinfer::Det scrfd_detect(const std::vector<uint8_t>& rgbHWC, int w, int h)` → `{dets(N,5), kpss(N,5,2)}`（原图像素坐标）；CLI 隐藏旗标 `--det-selftest <png> <gold.json>`（#78 复用其比较核）。

- [ ] **Step 1: 移植（逐行翻译 face_server.py:57-134 `_anchors`/`distance2bbox`/`distance2kps`/`_nms` + :141-153 `_load` 改 ORT C API + :161-190 `detect()`）**。常量 `INPUT_SIZE=640, STRIDES{8,16,32}, NUM_ANCHORS=2, DET_THRESH=0.5f, NMS_THR=0.4f` 一字不差。图像读入用 QImageReader（**RGB**，face_server 是 BGR——blob 组装处按 `[...,::-1]` 语义换成先 BGR 化或调整通道序，二选一并在代码注释钉死方向）；cv2.resize → 通用双线性 `bilin_resize()`（放 `faceinfer/imgkit.h`，供 T3 复用）；`(x-127.5f)/128.0f`；输出分组按 `o.shape[-1]∈{1,4,10}` + 元素数降序映射 stride，verbatim 复刻 :170-183 的 bycount 逻辑；NMS 的 `+1` 像素宽/高约定与 `1e-9` epsilon 保留。
- [ ] **Step 2: `ai_export/gen_face_fixtures.py`**（oracle，跑在 `D:\lightalbum_ai\venv_face`）：直接 `sys.path.insert(0, r"D:\lightalbum_ai"); from face_server import FaceEngine`（**不起 HTTP、同进程直调**），对 env `LA_FACE_IMG` 指定的若干张本地图跑 `detect_image()`，逐图写 `<img>.gold.json`（faces 数组原样，含 5 位小数 embedding）。脚本打印 SHA 清单。README 记录运行方式。产物一律不入库。
- [ ] **Step 3: 构建 faceinfer（模型在 `faceinfer/` 或 3rdparty 路径经 env `LA_FACE_MODEL_DIR` 指到 `D:\lightalbum_ai\models\face`）+ 实测**：对 ≥3 张含脸图（用户本地临时目录，不入库）`--det-selftest` vs gold：人脸数相等、IoU≥0.98、kps 均距≤2px 全列 PASS。
- [ ] **Step 4: Commit**（scrfd + imgkit + gen_face_fixtures.py + README；`faceinfer.cpp` 壳转真）。

---

### Task 3: 对齐+嵌入移植 + faceinfer 端到端 + #78 真模型门（opt-in 注册）

**Files:**
- Create: `faceinfer/align.h/.cpp`；Modify: `faceinfer/faceinfer.cpp`、`faceinfer/CMakeLists.txt`
- Create: `native/src/main.cpp` `runFaceModelQa()`；Modify: `native/gate_matrix.ps1`（#78 full, `need=@("LA_FACE_MODEL","LA_FACE_IMG")`, `opt` 语义同 LA_ERASE_MODEL_QA）

**Interfaces:**
- Produces: `align_similarity_transform(const float kps[5][2], float M[6])`（Umeyama 4-DOF 闭式）+ `warp_affine_bilinear(const std::vector<uint8_t>& rgb, int w, int h, const float M[6], uint8_t out[112*112*3])`。
- Produces: faceinfer 真 REQ path 应答：`RES <id> <len>` + JSON `{"ok":true,"count":N,"faces":[{bbox,score,kps,embedding512(5dp)}]}` 与 face_server `/face/detect` 逐字段同形（200 路径；读盘失败 `ERR id loadfail` ≙ 其 400）。

- [ ] **Step 1: Umeyama（完整给出，OpenCV estimateAffinePartial2D 同族闭式）**：质心 `cs=mean(kps)`、`ct=mean(REF)`；去均值后 `H=Σ (p-cs) q^T` 2×2；`R=atan2(H12-H21, H11+H22)` 角；`c=cos(r), s=sin(r)`；`scale = (H11+H22)*c + (H21-H12)*s) / (Σ|p-cs|²)`（分母零 → scale=1）；`M = [ c*scale, -s*scale, tx; s*scale, c*scale, ty ]`，`t = ct - scale*R*cs`。REF 模板 = face_server.py:59-60 六行常数逐字。**方向注意**：cv2.estimateAffinePartial2D(src=kps, dst=REF) 返回 src→dst 正向 M，warpAffine 用它正向映射取反向采样——实现为逆向：`inv(M)` 后对 112×112 每像素 `(x',y') = invM·(x,y)` 采样，越界填 0。
- [ ] **Step 2: ArcFace 会话**：blob `(x-127.5)/128`，RGB NCHW 1×3×112×112；`feat/norm(+1e-12)`；JSON 序列化 `%g`→5 位小数舍入（`round(v,5)` 语义用 `std::round(v*1e5)/1e5`，输出为 JSON number）。
- [ ] **Step 3: 端到端实测**（同 T2 图组 + gold）：count 精确等、IoU≥0.98、kps≤2px、余弦≥0.999、scoreΔ≤0.02 五列全 PASS；READY 实测 ms 记录（进 #78 measuredMs 与附录 A 回填）。
- [ ] **Step 4: `runFaceModelQa()`**：env 存在 → 起真 faceinfer.exe（QProcess 直起，不经 runner——门只验数值平价，线程机械归 #75）逐 fixture REQ → 五列容差断言；env 缺 → gate 不发（qa_runner need= skip 机制）。注册 #78（full tier，timeoutSec=120）。反例：`LA_FACE_QA_FORCE_BROKEN=1` 注坏 REF 模板常数 → 断言必红（铁律 #34）。
- [ ] **Step 5: 构建 + 双门复跑（#75 壳不受影响）+ Commit**。

---

### Task 4: AiSubprocessRunner + FaceRunner + PersonDetector 净替换 + #75 补全

**Files:**
- Create: `native/src/core/AiSubprocessRunner.h/.cpp`、`native/src/core/FaceRunner.h/.cpp`
- Modify: `native/src/core/PersonDetector.cpp`（删 isLocalUrl/probe(url)/QNAM 传输段）、`native/src/ui/MainWindow.cpp`（:1535/:1599 文案 + 调用去 url）、`native/src/main.cpp`（#75 G3/G6/G7/G8 补全）、`native/gate_matrix.ps1`（#75 measuredMs 回填）、i18n `.ts`（zh 两条新文案）

**Interfaces:**
- Produces: `class AiSubprocessRunner`（QObject，moveToThread 于构造方）：
  `enum class Rst { Ok, Err, Timeout, Crashed, Busy, BlockedNoModel, NoExe }`
  `struct Reply { Rst rst; QByteArray json; QByteArray errCode; }`
  `void configure(const QStringList& exeCandidates, const QStringList& modelFiles, int readyBudgetMs, const QString& qaHookEnvName);`
  `Reply call(const QString& kind, quint64 id, const QByteArray& body, int timeoutMs=60000);`  // 任意非 GUI 线程
  `bool ensureReady(Rst* out);`  // probe 语义：拉起+READY1；NoExe/BlockedNoModel 透出
  `static bool sIsMainThread();`
- Produces: `FaceRunner::probe()` / `FaceRunner::detect(const QString& absPath, QJsonDocument& out)`（REQ kind="path" body=utf8 路径）。
- Consumes: T1-3 的 AiProto/faceinfer。

- [ ] **Step 1: 实现要点（评审锚）**：owner QThread + `Qt::BlockingQueuedConnection` invokeMethod 到 `slotCall()`（调用线程 tryLock 失败即 `Busy` 立即返回——**先 tryLock 再连接**，杜绝自锁）；`slotCall` 内 waitForStarted/写帧/waitForReadyRead 循环（复用 readExactly）；进程 errorOccurred→Crashed→**下次 call 自动重启一次**（重启后再失败即透 Timeout/Crashed）；每次 call 前重查 modelFiles sibling（热插拔）；响应 JSON 解析后即弃（无缓存表，内存纪律 §8）。EraseRunner 不引用不改动。
- [ ] **Step 2: PersonDetector 三函数改造**：`probe()` 无参化=FaceRunner.ensureReady；`detect/detectEmbeddings` 循环体 `QNAM POST → FaceRunner::detect()`，**「失败→整张放过」分支逐字保留**（Rst≠Ok 视同 !ok）；embedding 取最大 score 脸 + `toEmbBlob` 段零改动；g_synthetic 短路零改动。
- [ ] **Step 3: MainWindow 文案**：「请先启动本地人脸服务…face_server.py」→「AI 引擎未就绪（缺模型或引擎文件，先运行 native/fetch_ai.ps1 并构建 faceinfer）」（两条，分类那条随 T9）。`.ts` zh 同步（手工词条，禁 lupdate）。
- [ ] **Step 4: #75 补全 G3/G6/G7/G8**（经真 runner：roundtrip 解析等值、HANG→Timeout+重启复绿、并发第二路 Busy、候选全缺 NoExe）+ 反例复跑。
- [ ] **Step 5: 全绿（#75/#78/#70 LA_ERASE_QA 回归必绿=底座未动证据）+ `git grep` 证明 PersonDetector/MainWindow 无 `8767|lightalbum_ai|isLocalUrl` + 重建 + Commit**。

---

### Task 5: face 轨收口验证（无新代码，验证+文档小步）

- [ ] **Step 1:** fast tier 全跑（pre-commit 级）+ `LA_GUI_THREAD_QA` 复绿（门面线程语义未变证据）。
- [ ] **Step 2:** `--det-selftest` 扩到 5 图复跑留档；`faceinfer/faceinfer.cpp` 头注释平价声明更新（oracle 版本号 + SHA）。
- [ ] **Step 3:** Commit `chore(faceinfer): 第四轮 face 轨实测留档`（若 Step1/2 无代码改动则允许空手归并到 T6 提交，由控制器判定，不制造噪声提交）。

---

### Task 6: CLIP 导出脚本 + prompts.bin + 黄金 fixture 脚本

**Files:**
- Create: `ai_export/export_clip_onnx.py`、`ai_export/gen_clip_fixtures.py`
- Modify: `ai_export/README.md`

**Interfaces:**
- Produces: `native/3rdparty/clip/bin/clip_vit_b32_img.onnx`（opset17 fp32, in 1×3×224×224, out 1×512）+ `prompts.bin`（magic `LAPROMPTv1` + int32 8×512 float32-LE 行主序 + 尾部 32B SHA）+ 两文件 SHA256（钉入 T10 fetch_ai.ps1 与本步 README）。

- [ ] **Step 1: export_clip_onnx.py**（venv_cake torch2.6+open_clip）：`set HF_HOME=D:\lightalbum_ai\hf_cache`；`open_clip.create_model_and_transforms('ViT-B-32','laion2b_s34b_b79K')` eval() fp32；`torch.onnx.export(model.encode_image 包装, randn(1,3,224,224), …, opset_version=17, input_names=['image'], output_names=['feat'])`；文本侧把 classifier_server.py:39-58 的 **LABELS 表逐字复制入脚本**并按 :137-141 算法（逐 prompt `encode_text`→行归一→类内均值→再行归一）算 8×512 → 写 prompts.bin + SHA 尾。打印两 SHA。
- [ ] **Step 2: gen_clip_fixtures.py**：对 `LA_CLIP_IMGS`（逗号分隔图路径，用仓库已入库的 QA 安全图 + oracle 预筛非边界样本）跑 `open_clip` 原生推理（与服务器同 preprocess，CPU fp32），按 classifier_server.py:160-183 `is_monochrome` + 判定规则出 `<img>.gold.json`（label/score/note/top3(4dp)/sat）。
- [ ] **Step 3: 实跑两脚本**，产物落 `native/3rdparty/clip/bin/`，golden 落 env `LA_CLIP_GOLD_DIR`（不入库；README 记录复现命令）。
- [ ] **Step 4: Commit**（脚本 + onnx/prompts.bin + LICENSE 占位随 T10；本任务含两文件入 git LFS-free 直存——605MB→onnx 实测约 600MB+，入库体积在提交前 `git status` 复核后如实报告）。

---

### Task 7: clipinfer.exe（preproc+塔+规则）+ 桩 + #76 门

**Files:**
- Create: `clipinfer/{CMakeLists.txt,clipinfer.cpp,preproc.h/.cpp,rules.cpp,clipinfer_stub.cpp}`
- Modify: `native/src/main.cpp`（runClipAiQa）、`native/gate_matrix.ps1`（#76 fast）

**Interfaces:**
- Produces: `clipinfer REQ jpg` → `RES` JSON `{"ok":true,"label":"人像","score":0.87,"note":"…","top":[["人像",0.87],…3 项4dp]}`（字段与 classifier_server `/classify` results 单项同形）。
- Produces: 桩 env 钩子 `LA_CLIP_FORCE_*` 同 T1 表 + `LA_CLIP_PROMPTS_BAD=1`（校验头反例）。

- [ ] **Step 1: preproc**：shorter-side=224 缩放 + center-crop 224 + CLIP mean/std 归一 RGB NCHW。**bicubic 变体裁定**（spec 风险 #1）：实现 Catmull-Rom(a=-0.5) 与 scipy-mitchell 两 kernel（编译期旗标），对 6 张 fixture 跑 §4 容差（label/note 逐字等、top-probs Δ≤0.02/sat Δ≤1.0 全列），取全过者留码、另一删（评审看最终码）。PIL HSV 判定移植 classifier_server.py:160-169（`convert('RGB').resize((48,48))` 默认双线性 → 复用 imgkit；HSV 公式 PIL C 源 verbatim 翻译：V=max、S=(V-min)/(V+eps)… 以实现期对拍定案）。
- [ ] **Step 2: rules.cpp**：classifier_server.py:170-230 判定三段（黑白优先 0.99/low_confidence/mono_guard）逐字翻译，`MONO_SAT_THRESHOLD=18.0/MONO_SAT_GUARD=45.0/OTHER_SCORE_THRESHOLD=0.20/logit 100.0` 一字不差；prompts.bin 启动校验（magic+SHA 尾）。
- [ ] **Step 3: 真 REQ jpg 端到端**：ORT 会话 + softmax + top3 → JSON。桩 + CMake（同 T1 模板，无 Qt）。
- [ ] **Step 4: runClipAiQa + #76**（G 组同 #75 表 + PROMPTS_BAD 反例 + timeoutSec=30）；`--parity-selftest` 跑 §4 断言全 PASS 留档。
- [ ] **Step 5: Commit** + 注册 #77 `LA_CLIP_MODEL_QA`（full, `need=@("LA_CLIP_MODEL","LA_CLIP_GOLD_DIR")`, runClipModelQa 同 T3#78 模式，FORCE_BROKEN 反例注坏 MONO_SAT_THRESHOLD=0.0 → 必红）。

---

### Task 8: ClipRunner + Classifier 净替换

**Files:**
- Create: `native/src/core/ClipRunner.h/.cpp`
- Modify: `native/src/core/Classifier.cpp`、`native/src/ui/MainWindow.cpp`（:1473 文案）、`.ts`、`main.cpp`/`gate_matrix.ps1`（#76 G3/G6/G7/G8 补全 + measuredMs）

- [ ] **Step 1: ClipRunner**（基于 T4 runner，REQ kind="jpg"，body=**图像绝对路径的 UTF-8 原字节**（len=路径字节数，**不是**图片字节——本行原文写「缩略 JPEG 字节」已被 T7 实物订正，见 spec §2.3/§5 的 R-75 块；开工前先读 `clipinfer/clipinfer.cpp:224-238` 与 `native/src/main.cpp:6211-6217` 两处实物对齐口径）。批 32 循环保留为**批次调度**（进度计数不漂），传输逐张化；**per-image 失败→该张放过**（原「整批放过」的收窄，方向一致），`normalizeLabel` 与 `items/paths[idx]` 组装零改动。probe 无参化。
- [ ] **Step 2: MainWindow 文案替换 + .ts**。
- [ ] **Step 3: #76 补全 + 反例 + 全绿（#70/#75 回归）+ `git grep` 无 `8766`**。
- [ ] **Step 4: Commit**。

---

### Task 9: fetch_ai.ps1 + 3rdparty 树 + LICENSE

**Files:**
- Create: `native/fetch_ai.ps1`、`native/3rdparty/face/{LICENSE…,bin/*}`、`native/3rdparty/clip/{LICENSE…,bin/prompts.bin}`（onnx 于 T6 已入）
- 结构蓝本 = `native/fetch_lama.ps1` 全文（参数表/verify-before-land/-SelfTest 三件套逐件仿制）

- [ ] **Step 1:** `-LocalSource` 模式（默认 `D:\lightalbum_ai\models\face`）：SHA 表 = spec 附录 A 两值 + T6 产出的 clip 两值；**先算后拷**（落地前校验，同 Fix4 判例）；`-SelfTest` 注坏 SHA/缺 LICENSE/缺源必 throw。
- [ ] **Step 2:** LICENSE 包：face=buffalo_l/insightface 研究用条款（含 SCRFD/ArcFace 上游说明 + 来源 URL 记录）；clip=open_clip MIT + laion2b_s34b_b79K 数据 CC-BY-4.0 + 权重源 hf_cache blob SHA；各配 `THIRD_PARTY-NOTICES.md`（lama 先例格式）。缺任一文件 → fetch throw（硬检）。
- [ ] **Step 3:** 三处 bin 的 onnxruntime.dll 同钉死副本（从既有 v1.19.2 校验后拷入）；`faceinfer.exe`/`clipinfer.exe` 构建产物拷入 bin（手工入库先例）。
- [ ] **Step 4:** 实跑 `fetch_ai.ps1` 全绿 + `-SelfTest` 三反例咬合留档 + Commit。

---

### Task 10: release.ps1 C10 装配与硬检

**Files:**
- Modify: `native/release.ps1`

- [ ] **Step 1:** C10 合约 = C9 逐样复制改参数（staged faceinfer/clipinfer：exe+双模型+prompts+LICENSE+NOTICES+onnxruntime.dll 齐备，缺任 → throw）；注入测试 ×3（缺 LICENSE/缺模型/缺 DLL）进既有自检段（:534-551 邻居）；装配腿照 lama 段模式新增两段。
- [ ] **Step 2:** windeployqt 腿复验 **Qt6Concurrent.dll 自动入包**（2.39.0 遗留前置项闭环——若 windeployqt 不识别，加显式拷贝一行并注释 WHY）；MANIFEST/BUILDINFO 自动覆盖新文件（验证 46→~54 entries）。
- [ ] **Step 3:** `release.ps1 -Package` **干跑至 staging 检查**（不打完整 zip——打包仍属用户仪式；C10 自检 + staging 清单贴报告）+ Commit。

---

### Task 11: 全量 78 门回归 + 2.40.0 收口

**Files:**
- Modify: `native/CMakeLists.txt`（VERSION 2.39.0→2.40.0）、`native/CHANGELOG.md`（新 `[2.40.0]` 节）、`native/docs/迭代指导书.md`（§3 门数 78 口径 + §7 新路线行「路线4 AI 索引去外部化 LANDED」+ 旧路线4措辞订正）

- [ ] **Step 1:** 全量 full tier：Expected FAIL=0 CHECK=0 KILL=0 **SKIP=14**（基线 12 + #77/#78 opt-in 未挂 env；若本机 env 全挂则 PASS 计数增加、SKIP 回落，报告如实写两种口径之一以 -Timing 实跑为准）。#75/#76/#77/#78 全 PASS。
- [ ] **Step 2:** measuredMs 全部 -Timing 回填（#75/#76 fast；#77/#78 若跑则回填，否则 0+注释）。
- [ ] **Step 3:** CHANGELOG 新节要点：两进程+底座+净退役（8766/8767 零命中引用）、平价容差实测数字（IoU/余弦/sat/label 实测最差不小于承诺值）、roster 78、zip 体积增量诚实、opt-in 门与隐私红线。
- [ ] **Step 4:** 指导书 §3/§7 + CMakeLists 版本 + 重建 + Commit `release-prep(2.40.0): …`。
- [ ] **Step 5:** 收尾汇报（commit 列表、4 门证据、平价最差值、git grep 退役证明）。

---

### Task 12: 终审（whole-branch review）+ 修复波 + 记忆收口

- [ ] SDD 规程：`f01a222` 式终审（区间 spec..HEAD 终审官：平价 verbatim 抽查 vs oracle 源、runner 线程契约（无 GUI 线程 call、Busy 非阻塞、重启一次语义）、协议三方帧一致性、i18n 词条、LICENSE/C10 完整性、roster ASCII）→ ONE fix wave → scoped re-review → ledger/memory（roadmap+release-state 更新 2.40.0 local-only 态 + Qt6Concurrent 闭环记录）。

---

## Self-Review（控制器写计划后自查）

1. **Spec 覆盖**：§2.1/2.2/2.3=T1/T3/T4/T7/T8；§3=T2/T3；§4=T6/T7；§5=T1；§6=T9/T10；§7=T1/T3/T7（#77 注册并入 T7 Step5、#75/76 补全 T4/T8）；§8=T4/T8 分支保留条款；§9=File Structure 全行有归属；§10=T11 验收清单。#75 G 组拆分（T1 四子组+T4 补四子组）为刻意增量交付，roster 计数在 T1 即含 #75 注册行——无缺口。
2. **占位符**：T5 Step3 的条件归并、T7 bicubic/HSV 的「对拍定案」是 spec §风险 条款的精确引用（裁定机制+顺序均已指定），非 TBD。
3. **类型一致**：`Rst/Reply` 自 T4 定义后 T8 使用一致；`LA_FACE_FORCE_*`/`LA_CLIP_FORCE_*` 钩子命名族一致；`REQ <id> <kind> <len>` 与 `parseReq` sscanf 形一致；prompts.bin magic 三处（T6 写/T7 验/T9 SHA 表）同名 `LAPROMPTv1`。

# 引擎升级第四轮：AI 索引去外部化（faceinfer + clipinfer 随包子进程）设计规格

> 版本目标 **2.40.0**。门禁名册 **74 → 78**（+4）。零云端、零新增第三方运行时依赖
> （onnxruntime.dll 复用已钉死的 v1.19.2 单一 DLL）。

**For agentic workers:** 本 spec 是实施计划的裁定权威；计划与 spec 冲突时以 spec 为准。

---

## 1. 目标与范围

把「AI 全量索引」的两条外部 Python HTTP 服务链路（CLIP 分类 :8766、人脸检测+识别
:8767）替换为**随包发布的本地 ONNX 子进程**，复用 inpaint.exe（2.35/2.36）已验证的
「持久 QProcess + READY 握手 + 模型 sibling 校验 + fetch SHA 钉死 + LICENSE 硬检 +
hermetic 桩门 + opt-in 真模型门」全套模式。

范围内：
- `faceinfer.exe`：SCRFD-10GF 检测 + ArcFace w600k_r50 识别，C++ 移植 insightface 官方
  解码/对齐/嵌入算法（oracle = 本机 face_server.py，已声明逐像素对拍语义）。
- `clipinfer.exe`：OpenCLIP ViT-B-32/laion2b_s34b_b79K 图像塔 ONNX + 预计算 8 类英文
  prompt 文本向量 sidecar + 分类判定规则（黑白确定性/低置信/护栏）完整移植。
- `core/AiSubprocessRunner` 共享底座 + `FaceRunner`/`ClipRunner`。
- `PersonDetector` / `Classifier` 门面签名语义不变、传输层 HTTP→子进程**净替换**
  （不留开关，8766/8767 常量与 URL 注入全部删除）。
- `ai_export/` 离线导出与黄金 fixture 生成脚本（开发工具，不随包）。
- `native/fetch_ai.ps1`、3rdparty 树、release.ps1 装配扩展、4 道新门禁、
  CHANGELOG/指导书/使用说明书收口。

范围外（明确不做）：
- SigLIP 2 升级（本机权重 .incomplete，HF 不可达；导出脚本留 arch 参数即止，不测）。
- LumenAgent/Ollama 决策层（部署动作非代码）；enhance_server/BiRefNet/pyiqa。
- P3 设置页（其「AI 连接」条目因本轮端口退役而简化，属设计语言轨，另行）。
- 嵌套线程池专职化决策（2.39.0 打包前置项，与本 spec 无关）。
- GPU/显存路径（CPUExecutionProvider 是唯一 provider，与 lama 一致）。

## 2. 进程架构

### 2.1 两个独立随包子进程（否决合并单进程）

| 进程 | 常驻会话 | 模型文件（同目录 sibling） | 冷启动 READY 预算 |
|---|---|---|---|
| `faceinfer.exe` | SCRFD det + ArcFace rec | `detection.onnx`(=det_10g, 16,923,827B) + `recognition.onnx`(=w600k_r50, 174,383,860B) | 30s（实测后回填，参照 lama 198MB≈8.5s） |
| `clipinfer.exe` | ViT-B-32 image encoder | `clip_vit_b32_img.onnx` + `prompts.bin` | 60s（605MB 塔，实测后回填） |

理由：崩溃域/生命周期/许可证包独立；分类与人脸在 app 里本就是互斥的两条批跑链路；
与 lama 先例 1:1 同构，评审心智成本最低。

### 2.2 `core/AiSubprocessRunner`（共享底座）

自 EraseRunner 提炼的通用机械，**EraseRunner 一行不动**（已发布、LA_ERASE_QA/E 组
非阻塞断言已锁死其行为，禁止回归面）。底座拥有：

- 专属 owner `QThread` + 持久 `QProcess`（cwd=exe 目录），`waitForStarted` 5s；
- `READY 1` / `READY 0` 握手行（stdout 首行，前缀匹配，stderr 仅日志）；
- 每次 submit 前重查模型 sibling 存在性（热插拔语义，同 EraseRunner::modelOkFor）；
- 同步 `request(id, header, body) -> Response`：调用线程阻塞在 mutex+waitCondition
  上，owner 线程唯一持有 QProcess；**响应超时 60s**（与现 HTTP transferTimeout 等值）；
- `blocked_no_model` / `exe_missing` / `timeout` / `crashed` 四类结构化失败；
- QA 注入钩子：`qaSetForceNoExecutor`、`qaInjectBadFrame`（命名沿用 LA_ERASE_QA 先例）。

**单飞不变式**（2.39.0 终审嵌套池教训的同族防线）：同一 runner 同一时刻只允许一个
in-flight 请求；第二并发调用立即返回失败并计数（不排队、不死锁）。app 层现有模态
进度对话框天然串行化两条批跑链路，runner 守卫是文档化硬防线（头文件注释 3 行，
说明 WHY）。runner **永不可在 GUI 线程调用 request**（owner QThread + 阻塞等待会冻
GUI）；门面是静态同步函数，调用点全部在 `QtConcurrent::run` 工作线程内——维持现状。

### 2.3 门面净替换

- `PersonDetector::detect/detectEmbeddings`：删 probe(url)/QNAM/JSON-POST 传输段，
  改为 FaceRunner 请求；**响应 JSON 与 `/face/detect` 逐字段同形**
  （`{"ok":true,"count":N,"faces":[{bbox,score,embedding,kps}]}`，embedding 保持
  **5 位小数舍入**后入 2048B float32-LE BLOB——库里既有嵌入的语义契约不破）。
  「网络层失败→整张放过」「无脸→faces:[]不标注」「已标注跳过」三条既有容错逐字保留。
- `Classifier::classify`：multipart 批 32 → 逐张 `REQ jpg <len>` + **UTF-8 图像绝对路径字节**
  （**2026-10-14 R-75 订正**：本行原写「JPEG 字节流」，与已落地并通过 13/13 平价实测的
  clipinfer 帧口径矛盾——body 是路径不是图片字节，见 `clipinfer/clipinfer.cpp:224-238`、
  `native/src/main.cpp:6211-6217`；与 §5 的 face 侧 `det` 路径体同族，见 §5 末 R-75 块。
  好处：子进程自持解码，Qt 侧不再重复读盘/转码，且 `Classifier.cpp:162-170` 既有
  「按路径读盘」语义直接继承）
  （**逐张失败放过** = 原整批放过的收窄版，语义方向一致）；响应 JSON 与 `/classify`
  的 `results` 逐字段同形，`normalizeLabel` 最后一英里映射保留不动。
- `probe()` 签名去掉 `url` 参数（`static bool probe()`），语义 = 进程可拉起且 READY 1
  且模型齐备；MainWindow 三处调用点与「服务未就绪」两个 QMessageBox 文案改为
  「AI 引擎未就绪（缺模型或引擎文件，先运行 native/fetch_ai.ps1 并构建 faceinfer/clipinfer）」。
  `isLocalUrl` 回环守卫随 HTTP 层退役（隐私语义由「根本不开端口」更强地继承）；
  LumenAgent 自身守卫不受影响。g_synthetic / g_syntheticEmb QA 短路钩子原样保留。

## 3. faceinfer 平价契约（移植清单 = face_server.py 逐常数 verbatim）

- 输入：按请求携带的**绝对路径**读盘（QImageReader，等效 cv2.imread BGR→显式映射 RGB），
  失败 → `ok:false` 不抛错。
- 检测：`INPUT_SIZE=640`、`STRIDES=(8,16,32)`、`NUM_ANCHORS=2`、`DET_THRESH=0.5`、
  `NMS_THR=0.4`；letterbox：`scale=640/max(h,w)`，`nh/nw=int(round(dim*scale))`，
  黑底画布左上贴放；blob `(x-127.5)/128.0`，RGB NCHW；输出按元素数降序映射 stride、
  `bbox*kp = distance2bbox/kps`、`/scale` 回原图坐标；NMS 含 `+1` 像素约定与
  `1e-9` 分母 epsilon——全部按 face_server.py `detect()`/`_nms()`/`_anchors()` 逐行移植。
- 对齐：5 点 kps → `REF` 模板（insightface 常量 verbatim）→ 4 自由度最小二乘相似变换
  （= cv2.estimateAffinePartial2D 默认 RANSAC + 内点集 LM 精修；LS 闭式精化为容差吸收判例 R-09）→ 112×112 仿射 warp
  （逆向映射双线性，越界取 0）；`(x-127.5)/128.0`，RGB NCHW → ArcFace → L2 归一
  （`+1e-12`）→ 512 维，JSON 出 5 位小数。
- **平价门禁（第 78 门，opt-in）容差**：同一 fixture 图 C++ 输出 vs oracle 黄金 JSON：
  人脸数精确相等；每 bbox IoU ≥ 0.98；kps 平均距离 ≤ 2.0 原图像素；embedding
  余弦 ≥ 0.999；score 差 ≤ 0.02。容差非逐字节的理由：cv2 resize/warp 定点插值与
  QImage 浮点双线性存在 ≤1 LSB 系统性微差，insightface 语义契约本身即阈值制
  （FaceCluster 阈值 0.4 远粗于 0.001 级余弦差）。

## 4. clipinfer 平价契约

- 导出（一次性离线，`ai_export/export_clip_onnx.py`，跑在 `D:\lightalbum_ai\venv_cake`
  的 torch 2.6 + open_clip 内）：ViT-B-32/laion2b_s34b_b79K `encode_image` 子图 →
  ONNX（opset 17，fp32，输入 1x3x224x224）+ `prompts.bin`（8 类 prompt 文本特征按
  classifier_server.py L137-141 同款「逐 prompt 归一→类内均值→行归一」预计算，
  float32-LE 8×512，附 magic+版本+SHA 尾注）。产物 SHA256 钉死进 fetch 清单。
  权重源 = 本机 hf_cache 完整 blob（605,143,316B），不依赖网络。
- 预处理：PIL/open_clip 等效 resize(shorter=224, bicubic Catmull-Rom a=-0.5?
  **以实现期对拍实测定 kernel 变体**) + center-crop 224 +
  mean(0.48145466,0.4578275,0.40821073)/std(0.26862954,0.26130258,0.27577711)。
  （真源 venv_cake `open_clip/constants.py:1`；代码侧 `clipinfer/preproc.cpp:234` 逐字同值）
- 推理：image encoder → 特征 L2 归一 → `logits = 100.0 * feat @ text_feats^T` →
  softmax(8)。
- 判定规则 verbatim（顺序即优先级）：
  1. 黑白确定性优先：48×48 缩样 HSV 平均饱和度 ≤ `MONO_SAT_THRESHOLD=18.0` →
     label=黑白, score=0.99, note=`low_saturation(%.1f)`；
  2. `best_score < OTHER_SCORE_THRESHOLD=0.20` → 其他, note=`low_confidence`；
  3. 模型判黑白且饱和度 > `MONO_SAT_GUARD=45.0` → 次优非黑白类（≥0.20 采信，
     note=`mono_guard(sat=%.1f)`）否则其他。
  LABELS 8 类 × prompt 清单逐字入 exe 资源（仅用于 prompts.bin 校验头，运行时不再
  tokenize）。响应含 top3 `[(name, prob 4dp)]` 以兼容既有 results 解析。
- **平价门禁（第 77 门，opt-in）容差**：fixture 图上 label/note 逐字符相等、
  top-probs 逐类差 ≤ 0.02、sat 差 ≤ 1.0。fixture 选取须避开判定边界（用 oracle
  概率预筛 |Δp|<0.05 的样本剔除）。
  - **2026-10-14 T7 修复波订正（R-87）：`low_confidence` 族对上述「避开判定边界」预筛是记档例外**。
    预筛的仓内机械化在 `ai_export/gen_clip_fixtures.py:60`（`GAP_MIN = 0.10`）与 `:175-176`（剔除判据），
    比本条的 |Δp|<0.05 更严。对 `note=low_confidence` 族它**数学上不可满足**：8 类 softmax 下
    top-1=p 时 top-2 ≥ (1−p)/7，故 gap = top1−top2 ≤ (8p−1)/7；该族定义要求
    p < `OTHER_SCORE_THRESHOLD=0.20`，代入得 gap < 0.6/7 ≈ 0.0857 < 0.10 ⇒ 真实图里
    不存在同时满足 `score<0.20` 与 `gap>0.10` 的样本（实证同向：T6 预筛 160 张候选池无 top1<0.20；
    在册 12 真图 min gap 0.7838）。故此族夹具由确定性合成器 `ai_export/gen_clip_synthetic_lowconf.py`
    产出（生成代码入库；PNG 与 `.gold.json` 按 R-06 绝不入库），族内自带下限
    `GAP_MIN_FAMILY=0.055`（= 2×top3 容差 0.02 + 安全垫 0.015）并在生产侧响亮校验。
    **例外的边界（不许退化成「两次都判错也算绿」）**：`#77` 对每一份 `low_confidence` 金
    断言 `screen.gap_top1_top2 > 0.04`（`native/src/main.cpp:6447-6451`，字段由
    `gen_clip_fixtures.py:160` 写入金内 `screen`）；**缺该字段或不满足即 FAIL**，不许静默跳过。
    注：本条例外破的是仓内更严常量，§4 自身的 |Δp|<0.05 地板未被破（在册合成金 gap=0.0589 > 0.05）。

## 5. 线协议（`native/src/core/AiProto.h`，face/clip 共用；EraseProto 不动）

- 请求一行：`REQ <id> <kind> <len>\n`；kind：face 侧 `det`（canonical，T2 评审裁定；
  len=UTF-8 路径字节数，body=路径原字节；faceinfer 对旧词 `path` 同义接受，历史兼容）；
  clip 侧 `jpg`（**len=UTF-8 图像绝对路径字节数，body=路径原字节**）。
  - **2026-10-14 T7 收口订正（R-75）**：本条原文写「clip 侧 `jpg`（body=JPEG 原始字节）」，
    与 T1–T5 已落地并评审通过的 face 轨（body=路径）不对称；T7 实现按 **path** 落地
    （`clipinfer/clipinfer.cpp:224-238` + `preproc::loadRgb` 走 `MultiByteToWideChar(CP_UTF8)`，
    #77 侧 `native/src/main.cpp:6211-6217` 发 `img.toUtf8()`），13/13 oracle 平价实测绿。
    采认 path 口径并以此为准：**等价性**在于退役中的 HTTP 桥发送的 part body 本就是
    `QFile(QDir(thumbsDir).filePath(p->thumbFile)).readAll()`（`Classifier.cpp:162-170`），
    子端按同一路径读回的字节与之逐字相同，分类结果不因搬运方式改变；**收益**是与 face 轨
    同一套 body 语义（底座 `AiSubprocessRunner` 零分叉）、父进程无需为 32 张缩略图驻留内存
    （spec §8 内存纪律同向）。若日后要让 clipinfer 收字节流，属**新需求**，须另立任务并重取平价。
- 成功应答：`RES <id> <len>\n` + len 字节 UTF-8 JSON；失败应答：`ERR <id> <code>\n`
  （code：`nomodel|loadfail|inferfail|badframe`，**clip 侧另加 `badprompts`**——R-76 补记，
  T7 实物 `clipinfer/clipinfer.cpp:114` 在 `prompts.bin` 内嵌矩阵 SHA 失配时即刻 `READY 0` 保活、
  每 REQ 诚实回 `ERR badprompts`，避免把 351MB 塔的加载失败误读成模型缺失；码表由子进程各自持有，
  `AiProto.h` 不列枚举，故此处即唯一文档真源）。stdout 只走协议帧（`_O_BINARY`
  强制，日志全走 stderr）——三条 lama 血泪规约 verbatim 继承。
- 握手：会话全部建好才发 `READY 1`；模型缺失发 `READY 0` 后**保活等 SIGTERM**，
  每 REQ 回 `ERR nomodel`。
- 响应 JSON 尺寸上界：人脸数 ×(512×8 字符) 量级，runner 缓冲按 `len` 精确读
  （体读循环教训 = EraseProto fix-async-regression，帧内分片必读满）。

## 6. 模型来源、装配与许可

- `native/fetch_ai.ps1`（模式逐字仿 fetch_lama.ps1）：`-LocalSource <dir>` 从
  `D:\lightalbum_ai` 拷贝 detection/recognition ONNX 至 `native/3rdparty/face/`，
  **先验 SHA256 后落地**（钉死值见附录 A，已实测复核）；clip 产物走 `ai_export` 导出的
  onnx+prompts.bin 同规则；onnxruntime.dll 从既有 v1.19.2 钉死 zip 复用（三处 bin
  各放一份副本，进程 cwd 隔离）；LICENSE 硬检（buffalo_l 系 insightface 研究用许可、
  open_clip MIT + laion2b 数据 CC-BY-4.0——逐包 LICENSE/`THIRD_PARTY-NOTICE.md`（**单数**，
  R-28b：与仓内既有先例 `inpaint/THIRD_PARTY-NOTICE.md` 及 `release.ps1:370` C9 的断言对象
  一致；本节原写复数 NOTICES 会让 T10 的 C10 无法照 C9 逐样复制）齐备
  才放行）；`-SelfTest` 注入反例复用生产校验。
- 构建：`faceinfer/`、`clipinfer/` 两独立 CMake 工程（MinGW `-static` +
  `ort_mingw_shim.h` 复用，同 inpaint 模板）；构建产物**不入库**——
  本节原写「exe 入库于 `native/3rdparty/{face,clip}/bin/`（inpaint.exe 先例：手工入库随 git 走）」，
  经 R-29b 实测证伪：`git ls-files native/3rdparty/lama/bin/` = **0**（`inpaint.exe` 从未入库），
  `.gitignore:14-16` 逐条忽略三个 `bin/`，本仓既定政策 = **第三方二进制磁盘就位、git 只放许可文本**。
  exe 与模型由 `native/fetch_ai.ps1` 就位，`3rdparty/{face,clip}/bin/` 同法忽略。
  - **既有例外（2026-10-14 R-77，T7 评审范围外观察的裁定）**：`native/3rdparty/clip/bin/` 目前
    `git ls-files` = **2**（`clip_vit_b32_img.onnx` 351,614,324 B + `prompts.bin` 16,436 B，由 T6 的
    `d3e36d2` 入库）。裁定**保持跟踪、不 `git rm --cached`**：untrack 只改树态（历史载荷与未来 push
    体积一条字节不减，真正的历史处置属 R-49 推送决断、属用户门），却会把「克隆即跑 #77」变成
    「克隆必须先跑 fetch」——而 R-56 实测这份 onnx 是全机唯一副本，fetch 的 clip 分支只有
    verify-only/重导出两条腿，比现状更脆；`prompts.bin` 另承载启动常量。因此 T9 的入库判据是
    **分区断言**：`face/bin` ls-files = **0**（新政策正面证据）、`clip/bin` ls-files = **2** 且脚本注释
    与报告必须写明这是 R-77 例外及其成因，不许伪装成 0。`.gitignore` 两行照加（tracked 优先于 ignore，
    加行是给未来的自己指路）。
- release.ps1：C9 家族扩展为 **C10**（staged faceinfer/clipinfer 无 LICENSE/无模型/无
  onnxruntime.dll → 硬 throw；注入测试 ×3 同 C9 模式）；装配腿照 lama 段逐样复制；
  zip 体积诚实记录（+≈0.95GB 级，本地 zip 可接受，MANIFEST/BUILDINFO 自动覆盖）。
  windeployqt 需同时验证 Qt6Concurrent.dll 随包（2.39.0 遗留前置项，同次打包腿解决）。

## 7. 门禁设计（roster 74 → 78：**fast +0，full +4**）

> **2026-10-14 T7 收口订正（R-76）**：本节原预测「fast +1，full +3」，把 #76 记在 fast 层。
> 实测推翻：`qa_runner -Timing` ×4 = 8019 / 7102 / 7149 / 7011 ms，max-of-warm **8019ms**
> 远超 E6 的 500ms fast 预算，注册即落 full（`Get-GateTier` 默认 full，`gate_matrix.ps1:701-705`）。
> `-SelfTest` 实跑终态 = **gates=78 fast=19 full=59 E6 overBudget=0**，与 #75 的
> T4a 先例同型（6992ms → full）。连带 §「R-39 CHANGELOG 数字」的 78 = fast 19 + full 59。

- **#75 `LA_FACE_AI_QA`（full，hermetic；T4a 由 fast 降级）**：门驱动真
  AiSubprocessRunner 底座（faceinfer_stub.exe 仅作子进程，同 inpaint_stub 模式），
  跑协议全谱：READY1/READY0→blocked_no_model、REQ/RES/ERR 帧、体分片读满、
  超时、crash 重启、单飞拒绝、FORCE 钩子。反例须咬（铁律 #34）。
  T4a 实测 measuredMs=6992（G3-G8 真实 runner 全谱 ≈7s，超 E6 fast 500ms 预算）→ full。
- **#76 `LA_CLIP_AI_QA`（full，hermetic 桩；R-76 由 fast 订正为 full，实测 8019ms）**：同构，
  含 prompts.bin 校验头反例。
- **#77 `LA_CLIP_MODEL_QA`（full，opt-in `need=@("LA_CLIP_MODEL","LA_CLIP_GOLD_DIR")`；
  R-76 订正：原写单 env。模型目录与黄金目录两项都必须声明——`qa_runner.ps1:75-82` 的语义是
  「`need` 里任一项缺失 → `[SKIP] (needs …)` 诚实计数」，只声明一项会让「有模型没夹具」的
  半挂载态被误报为实跑；两项齐全后的 8 条前置破坏（exe 缺 / READY≠1 / 目录缺 / 金缺失等）
  才落进 #77 内部的诚实 FAIL）**：真
  clipinfer.exe + 导出产物 + 黄金 fixture JSON 跑 §4 容差断言；env 缺 → SKIP 诚实计数。
- **#78 `LA_FACE_MODEL_QA`（full，opt-in `need=@("LA_FACE_MODEL","LA_FACE_IMG")`）**：
  真 faceinfer.exe vs 黄金 JSON 跑 §3 容差断言（T3 落地时 env 名定为 LA_FACE_IMG，
  与 oracle 生成脚本 `ai_export/gen_face_fixtures.py` 同名复用）。**fixture 隐私红线**：
  含真人脸的 fixture 图与黄金 JSON 只放本地 env 指定目录，**绝不入库**（沿用户样本
  env-only 铁律）；oracle 生成脚本入库，产物不入库。
- 既有门影响：LA_GUI_THREAD_QA/AiWriter 相关门不动（门面语义未变）；三处
  `MainWindow` 静态接线门若引用旧文案/端口常量，实现期同步改。
- gate_matrix.ps1 纯 ASCII 铁律（#37）适用全部新增行。

## 8. 错误与降级语义（用户可见矩阵）

| 状态 | probe | 批跑行为 |
|---|---|---|
| exe 或模型缺失 | false | 弹「AI 引擎未就绪」引导跑 fetch_ai.ps1；库不脏（T12 FIX-6 订正：`fetch_ai.ps1` **不随包分发**，故发行文案不得点名该脚本，只说缺什么、数据不出机、并指向用户界面上真能再点的按钮——本行原措辞是设计期口径， shipped 文案以 `MainWindow.cpp` 三条「AI 引擎未就绪」为准） |
| READY 超时/crash | true→运行中转 false | 当张放过，progress 继续，收尾汇总计数（T12 FIX-14：face 轨 `PersonDetector::detect`/`detectEmbeddings` 与 CLIP 轨同型补齐——`failed`/`stopped` 汇总，返回值 =「本轮是否全部干净跑完」，不再恒 true） |
| 单张推理失败 | — | 当张放过（=原网络失败语义），计入收尾汇总计数 |
| 取消 | — | 批间检查生效，进程保活（无 in-flight kill：单张 ≤ 数百 ms，协议不设 kill 码）；UI 落「中断（可能已部分写入）」支，不落「已为 N 张…」的假成功（T12 FIX-15 恢复三态） |

内存纪律（2.38 R3 教训前置）：runner 不缓存任何响应体（解析后即释放），REQ id 单调
复用无表。

## 9. 文件结构

| 文件 | 动作 |
|---|---|
| `faceinfer/{CMakeLists.txt,faceinfer.cpp,scrfd.h,scrfd.cpp,align.h,align.cpp}` | 新建 |
| `clipinfer/{CMakeLists.txt,clipinfer.cpp,preproc.h,preproc.cpp,rules.cpp}` | 新建 |
| `faceinfer/inpaint_stub 同型桩 ×2`（`faceinfer_stub.cpp`/`clipinfer_stub.cpp`） | 新建 |
| `native/src/core/{AiProto.h,AiSubprocessRunner.h/cpp,FaceRunner.h/cpp,ClipRunner.h/cpp}` | 新建 |
| `native/src/core/{PersonDetector.cpp,Classifier.cpp}`、`native/src/ui/MainWindow.cpp` | 修改（传输替换+文案） |
| `native/src/main.cpp` | 修改（4 门 runXxxQa + stub 分派） |
| `ai_export/{export_clip_onnx.py,gen_face_fixtures.py,gen_clip_fixtures.py,README.md}` | 新建 |
| `native/fetch_ai.ps1`、`native/3rdparty/{face,clip}/**` | 新建 |
| `native/release.ps1`、`native/gate_matrix.ps1`、`native/CMakeLists.txt` | 修改 |
| `native/CHANGELOG.md`、`native/docs/迭代指导书.md`、`docs/LightAlbum-2.40.0-使用说明书.md` | 修改/新建 |

## 10. 验收标准

1. 全量 78 门回归：FAIL=0 CHECK=0 KILL=0，SKIP=12+2（#77/#78 opt-in 基线；若开发机
   env 齐则 PASS 计入）；#75/#76 hermetic 全绿且反例咬合有书面证据。
2. §3/§4 容差在各自 fixture 集上逐项达标（黄金 JSON 由 oracle 脚本产出，报告贴数）。
3. `git grep` 证明：`8766|8767|lightalbum_ai|isLocalUrl`（Classifier/PersonDetector/
   MainWindow 范围）零命中；EraseRunner/`LA_ERASE_QA` 行为零变化（门绿即证）。
4. release.ps1 打包腿端到端：C10 三注入反例 throw；zip 含 2 exe×(模型+dll+LICENSE)；
   Qt6Concurrent.dll 在包（2.39 遗留项一并闭环）。
5. 门面三函数签名除 `probe()` 去 url 参数外零变化；调用点仅 MainWindow 三处。
6. CHANGELOG/指导书 §3 门数 78/§7 路线行诚实记录（含 zip 体积增量与 opt-in skip 基线漂移）。

### 附录 A（已钉死部分 + 实现期回填项）
- `detection.onnx`(=det_10g.onnx) SHA256 `5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91`（16,923,827B）
- `recognition.onnx`(=w600k_r50.onnx) SHA256 `4c06341c33c2ca1f86781dab0e829f88ad5b64be9fba56e56bc9ebdefc619e43`（174,383,860B）
  （源目录内 detection/recognition 与 det_10g/w600k 逐字节等价，已实测复核；随包命名用 detection.onnx/recognition.onnx，与 face_server.py `FaceEngine` 读取名一致）
- 待回填：clip_vit_b32_img.onnx / prompts.bin SHA256（导出步产出时钉入 fetch_ai.ps1）；
- 待回填：faceinfer/clipinfer READY 实测 ms（-Timing 唯一来源，铁律 #25/#31）。

## 风险登记（实施期逐个闭环，不阻塞 spec）

1. **bicubic kernel 变体不确定** → §4 已定为对拍驱动定案；若 center-crop 尺寸歧义
   （224 vs 224-shorter），以 oracle 为准。
2. **ORT C++ 对导出图 opset 兼容** → opset 17 ≤ ORT 1.19 上限；导出即验。
3. **estimateAffinePartial2D 闭式解与 OpenCV 数值差** → 容差制吸收；kps 断言兜底。
4. **clip 605MB 塔冷启动 > 60s READY 预算** → 实现期实测，超预算则调预算并记录
   （不改判定语义）。
5. **真人脸 fixture 隐私** → env-only 红线（§7#78），入库扫描由 pre-commit fast 门
   的既有 scratch 排除约定兜底。

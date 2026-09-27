# 本地 AI 模型选型报告 —— 接入光影集客户端

> 目标：为「光影集 LightAlbum」客户端寻找**更合适、更强大**的本地 AI 模型并给出可落地的接入方案。
> 约束：**全程本地（零云端）**、单卡 **RTX 2060 6GB**、许可须可自用、不破坏现有降级链。
> 调研时间：2026-09-22（模型生态变化快，落地前请复核版本号与许可）。

---

## 0. 结论先行（TL;DR）

现有栈的短板很集中：**分类器太旧**（OpenCLIP ViT-B-32，2022 年）、**决策层其实没装**（Jev 需要 WSL2，当前一直走规则兜底）、**抠图用错了工具**（SAM2 是分割不是 matting，发丝边缘吃亏）、**没有画质/美学评分能力**。

建议做 4 项升级（按性价比排序）：

| 优先级 | 能力域 | 现在 | 建议换成 | 许可 | 适配 6GB |
|---|---|---|---|---|---|
| ★★★ | 图像分类/打标 | OpenCLIP **ViT-B-32** laion2b | **SigLIP 2**（so400m 或 base） | Apache 2.0 | ✅ 0.4–1.7GB |
| ★★★ | 决策层（理解+出参） | Jev（**未装**，需 WSL2） | **Gemma 4 E4B** 或 **Qwen3.5-4B**（Ollama） | Apache 2.0 | ✅ ~3GB Q4 |
| ★★☆ | 抠图/Matting | SAM2（分割，非 matting） | **BiRefNet** | **MIT** | ✅ ~3.2GB 峰值 |
| ★★☆ | 画质/美学评分（**新能力**） | 无 | **pyiqa**（CLIP-IQA / TOPIQ-NR） | Apache 2.0 | ✅ 轻量 |

> ⚠️ **许可陷阱**：`RMBG-2.0` 常被中文博客说成 MIT/Apache，但其**官方模型卡写明是 CC BY-NC 4.0（非商用）**，商用需向 BRIA 单独授权。**同样效果的 `BiRefNet` 是 MIT**，直接用 BiRefNet 即可规避。
> 同理 `CodeFormer` 是 S-Lab License（偏非商用），人脸修复优先 `GFPGAN`（Apache 2.0）。

---

## 0.1 落地状态（2026-09-22 更新）

| 阶段 | 内容 | 状态 |
|---|---|---|
| P1 | 分类器模型**可配置**，SigLIP 2 优先、权重缺失**自动兜底** ViT-B-32 | ✅ 已落地并实测（`classifier_server.py`；本机离线验证：`fallback:true, model:ViT-B-32/laion2b_s34b_b79k`，`/classify` 正常） |
| P1+ | `fetch_models.bat` 一键预下 SigLIP 2 权重（联网跑一次即生效） | ✅ 已提供 |
| P2 | **决策层本地 LLM**：`brain.LocalLLMBrain`（Ollama，OpenAI 兼容） | ✅ 已落地并实测（伪 OpenAI 端点集成测试，`test_local_services.py` 16→**36 断言**全绿） |
| P2+ | 决策层三档优先级 Jev → 本地 LLM → 规则引擎；任一层失败自动降级 | ✅ 已落地 |
| P2+ | 客户端「服务设置」新增 Jev / **本地 LLM 地址与模型名** | ✅ 已落地（`index.html`，随客户端页同步） |
| 附带修复 | 服务探测先裸 socket 探端口，避免没装服务时 HTTP 白等 timeout | ✅ `get_brain` 实测 **2.4s → 0.7s** |
| P3 | BiRefNet `/matting`（后端端点 + 客户端 `matting()` + 执行器 `matting` 终态 op + app `act_matting` + UI「去背景」按钮 + `native_bridge` 转发） | ✅ 代码已落地（**本会话 shell 不可用**：未编译 / 未跑 `test_local_services.py [E]` / 未下 BiRefNet 权重，待真机验证） |
| P4 | pyiqa `/score` 画质评分（后端端点 + 客户端 `score()` + `ai._quality_hint` 接进 `recommend_presets` 当证据 + `suggest_presets` 透传 quality） | ✅ 代码已落地（同上，待真机验证） |

**本机环境实测事实（决定了 P3/P4 为何卡住）**：
- `huggingface.co` **不可达**（curl 12s 超时）→ 任何新权重都下不下来；
- `venv_cake` 已有 open_clip 3.3.0 / transformers 5.17.0 / torch 2.6.0+cu124 / timm；
- `pyiqa`、`kornia` **未安装**；`Ollama` **未安装**；
- 分类器(8766) 与 Pixel Cake(8765) **正在运行**，本次改动均为「可配置 + 兜底」，未中断在跑的服务。

**用户侧下一步（有网时）**：
1. 跑 `D:\lightalbum_aietch_models.bat` → 下 SigLIP 2 → 重启分类服务即生效；
2. 装 Ollama 并 `ollama pull qwen3.5:4b` → 界面「服务设置」填地址即可启用本地 LLM 决策（默认 `http://127.0.0.1:11434/v1`）；
3. P3/P4 一键部署：跑 `D:\lightalbum_ai\setup_ai.bat` → 装 pyiqa/timm/kornia + 预下 BiRefNet + 拉 Ollama 模型。重启 Pixel Cake 后，实验室徽章显示「抠图：BiRefNet 就绪」「画质评分：pyiqa 就绪」。

> ⚠️ **本会话（2026-09-22）Bash/PowerShell 工具均不可用**（Bash 持续报参数校验错误、PowerShell 吞输出），故 P3/P4 代码**写完但未执行验证**：未 `py_compile`、未跑 `test_local_services.py [E]`、未实际下载权重/装包。代码严格沿用 P1/P2 已验证的「可配置 + 导入级降级」模式（新 service 内部懒导入、端点 503 降级、不影响 8765 其它路由）。真机/shell 恢复后请按顺序验证：
> `cd client && ..\venv\Scripts\python.exe -m py_compile lightalbum/core/{pixelcake,ai,retouch,app}.py`
> `..\venv\Scripts\python.exe test_local_services.py` （应 pass≥56 fail=0）
> `python sync_client.py && node check.js && node test_viewer.js`
> 再 `build.py` 出 1.5.1，最后 `setup_ai.bat` + 重启服务。

---

## 1. 现状盘点

### 1.1 三个接入点（事实核对，来自源码）

| 层 | 服务 | 端口 | 当前模型/能力 | 实现位置 |
|---|---|---|---|---|
| 分类 | CLIP 分类服务 | `:8766` | **OpenCLIP ViT-B-32 / laion2b_s34b_b79k** + 单色启发式；8 类零样本（英文 prompt） | `D:\lightalbum_ai\classifier_server.py` |
| 执行 | Pixel Cake | `:8765` | `/enhance`(16 参数) `/relight` `/auto-segment`(SAM2) `/inpaint`(**LaMa**) `/face-slim` `/hair-smooth` `/made-up` `/sky/replace` `/color-match`；人脸走 MediaPipe | `D:\lightalbum_ai\pixel-cake\backend` |
| 决策 | Jev（本地 LLM） | `:8180` | **未安装**（README 明确需 WSL2，本机未装发行版）→ 实际一直走 `LocalBrain` 规则兜底 | `D:\lightalbum_ai\jev_local` |
| 修图 | 流明 Lumen | — | 纯 Pillow，16 预设 + 本地预设建议（1.4.0） | `client/lightalbum/core/retouch.py` |

客户端调度：`core/ai.py`（测量 → 决策 → 执行）｜决策器 `core/brain.py`（`JevBrain` / `LocalBrain`）｜执行器 `core/pixelcake.py`（`PixelCakeExecutor` / `LocalExecutor`）｜中间契约 `core/editplan.py`（16 个 -1..+1 参数）。

### 1.2 硬件与约束

- **GPU：RTX 2060 6GB**（sm_75），torch **2.6.0+cu124**，CUDA 生效。
- 显存只有 6GB ⇒ **所有候选模型必须"分时占用"**（各自独立进程、串行使用），不能同时常驻。
- `brain.assert_local()` 硬守卫：**只允许 127.0.0.1/localhost**，任何云端方案直接报错。
- 模型缓存固定 `D:\lightalbum_ai\hf_cache`，已设 `HF_HUB_OFFLINE` 强制离线。
- 客户端 venv 会被 PyInstaller 打进 exe ⇒ **重模型绝不进客户端 venv**，一律放 `D:\lightalbum_ai`。

---

## 2. 能力域 × 候选模型对比

### 2.1 图像分类 / 打标（替换 OpenCLIP ViT-B-32）

| 模型 | 规模 | 零样本 ImageNet-1k | 多语言 | 许可 | 6GB | 备注 |
|---|---|---|---|---|---|---|
| OpenCLIP ViT-B-32（现状） | 0.15B | ~72.8%（DataComp-1B 档） | ❌ 英文 | MIT | ✅ | 2022 年的老货，零样本偏弱 |
| **`ViT-SO400M-16-SigLIP2-384`** | 0.88B | **84.1%** ⬆️ +11pt | ✅ **多语言（WebLI）** | **Apache 2.0** | ✅ ~1.7GB | **首选**；`open_clip` 直接支持，改 1 行 |
| `ViT-SO400M-14-SigLIP-384`（SigLIP v1） | 0.88B | 83.1% | ✅ | Apache 2.0 | ✅ ~1.7GB | v1 也够；v2 更优 |
| `ViT-L-16-256`（SigLIP2） | 0.43B | 82.5% | ✅ | Apache 2.0 | ✅ ~0.8GB | 轻量档 |
| `ViT-H-14-378-quickgelu`（DFN-5B） | 0.99B | 84.4% | ❌ | 见 DFN 条款 | ✅ ~1.9GB | 精度更高、非多语言 |
| `ViT-gopt-16-SigLIP2-384` | 1.0B | 85.0% | ✅ | Apache 2.0 | ⚠️ ~2GB | SigLIP2 大档，6GB 仍可 |
| `PE-Core-bigG-14-448`（Meta Perception Encoder） | ~2.5B | **85.4%（开源最高档）** | ✅ | 见 Meta PE 条款 | ⚠️ ~5GB | 追极限精度才考虑，显存吃紧 |
| **DINOv3** | 0.3–7B | 强（需轻量适配头，非纯零样本） | ❌ | **Meta DINOv3 License（自定义）** | ✅ 小档 | 更适合做**特征库**（相似图/去重/聚类） |
| Recognize Anything (RAM++) | 0.4B+ | 开放词汇**多标签**打标 | ❌ | Apache 2.0 | ✅ | 若业务要"每张图一堆标签"用这个 |

> 数据来源：OpenCLIP 官方模型表（WebLI / DataComp / DFN 各档零样本 ImageNet-1k）。
> **从 72.8% 到 84.1%，是 +11 个百分点的量级提升** —— 这就是"分类器太旧"的具体代价。

**结论**：分类器首选 **SigLIP 2 so400m**。附带收益：**多语言** ⇒ 分类 prompt 可以直接写中文，不再受"CLIP 只吃英文"的限制；且同一权重可当**图像嵌入**用于相似图/去重。

### 2.2 多模态理解 / 决策层（替换或加强 Jev）

| 模型 | 规模 | Q4 显存 | 许可 | 多模态 | 备注 |
|---|---|---|---|---|---|
| **Gemma 4 E4B** | ~4.5B | **~3GB** | **Apache 2.0** | ✅ 文本+图+视频，**含音频** | 2026-04 发布；128K 上下文；"effective" 参数设计，同尺寸里很能打 |
| **Qwen3.5-4B** | 4B | **~3GB** | Apache 2.0 | ✅ **原生多模态** | 2026-02/03 发布；**201 语言**、262K 上下文；中文最好 |
| Qwen3-VL-4B | 4B | ~3–5GB | Apache 2.0 | ✅ | 2025-09；视觉理解成熟稳定 |
| Phi-4-mini | 3.8B | ~3GB | MIT | ❌ 纯文本 | 指令遵循/推理强，但**看不懂图** |
| MiniCPM-V 4.5 / o 4.5 | 8–9B | ~7–9GB | 自定义（商用需登记） | ✅ | 效果好，但 **6GB 吃不下**，且许可偏严 |
| Qwen3-30B-A3B (MoE) | 30B(3B 激活) | ~17GB | Apache 2.0 | ❌ | 单卡 6GB 无望，记下即可 |
| Gemma 4 12B Unified | 12B | ~6.6GB | Apache 2.0 | ✅ 编码器无关多模态 | 6GB 卡**刚好卡不住**（要留 KV cache），不建议 |

**结论**：本机 6GB 的现实选择是 **Gemma 4 E4B** 或 **Qwen3.5-4B**（都是 Apache 2.0、都原生多模态、都约 3GB）。中文场景优先 Qwen3.5-4B；要音频/更稳的英文指令优先 Gemma 4 E4B。
运行方式统一用 **Ollama**（自带 OpenAI 兼容 API：`http://127.0.0.1:11434/v1`）。

> 💡 **架构洞察**：Qwen3.5-4B / Gemma 4 E4B **本身就能看图**。理论上一个 4B VLM 可以同时承担「分类理解」和「决策出参」，把两个服务合成一个。
> 但**不推荐这么做**：CLIP/SigLIP 单张嵌入只要几毫秒且能批量，对**整库几千张**的打标成本极低；VLM 单张要秒级。
> **正确分工** = 廉价嵌入模型做**全库打标/检索/去重**，小 VLM 只对**用户正在看的那一张**做深度理解与出参。

### 2.3 抠图 / Matting（增强现有 SAM2 路径）

| 模型 | 规模 | 峰值显存 | 许可 | 备注 |
|---|---|---|---|---|
| SAM 2（现状，`/auto-segment`） | 0.04–2.4B | 取决于档位 | Apache 2.0 | 是**语义分割**，边界糙，发丝/半透明吃亏 |
| **BiRefNet** | 0.22B | ~3.2GB | **MIT** | **发丝级 alpha**，精度天花板，首选 |
| RMBG-2.0 | 0.2B | ~3.2GB | **CC BY-NC 4.0（非商用）** | 效果与 BiRefNet 同级，**许可不能用** ⚠️ |
| InSPyReNet | — | ~2.1GB | MIT | 速度快、显存省，精度略逊 |
| MODNet | 0.02B | ~2.8GB | Apache 2.0 | 轻量，人像视频抠图常用 |
| BEN2 / FeyNoBg | 0.26B | — | 开源 | 较新，生态资料少 |

**结论**：**BiRefNet（MIT）** 是唯一"既达到 RMBG-2.0 级精度、又许可干净"的选择。落到 Pixel Cake 里加一个 `/matting` 端点即可。

### 2.4 超分 / 修复 / 去噪（Pixel Cake 增强）

| 模型 | 用途 | 许可 | 备注 |
|---|---|---|---|
| Real-ESRGAN x4plus | 通用超分/老图修复 | BSD-3-Clause | 现实退化数据训练，鲁棒；**首选通用档** |
| SwinIR / HAT | 追求画质（慢） | Apache 2.0 | 基准分更高，显存/耗时大 |
| NAFNet | 去噪 + 去模糊 | MIT | 轻量、参数少；适合高 ISO/手抖 |
| Retinexformer | 低光照增强 | 见仓库 | 夜景提亮，比简单曲线更保色 |

### 2.5 人脸修复（Pixel Cake 增强）

| 模型 | 许可 | 备注 |
|---|---|---|
| **GFPGAN** | Apache 2.0 | **许可干净，优先** |
| CodeFormer | **S-Lab 1.0（偏非商用）** | 效果强，但许可要看清 ⚠️ |
| RestoreFormer++ | 见仓库 | 备选 |

### 2.6 画质 / 美学评分（**新能力**，与 1.4.0 的「本地 AI 建议」天然互补）

| 方案 | 许可 | 6GB | 备注 |
|---|---|---|---|
| **pyiqa（IQA-PyTorch）** | Apache 2.0 | ✅ | 一个包给一堆指标：CLIP-IQA、TOPIQ-FR/NR、MUSIQ、NIMA、DBCNN、LIQE、Q-Align(4bit) |
| CLIP-IQA | MIT | ✅ 轻量 | 「锐利 vs 模糊」「好 vs 差」成对 prompt，几乎零成本 |
| TOPIQ-NR | Apache 2.0 | ✅ | 无参考画质，弱光/噪声/模糊都能打分 |
| Q-Align | — | 4bit 可跑 | LLaVA 系，分更高但重；建议按需调用 |

**用途**：给图库**按画质排序**、挑出"模糊/欠曝/高噪"待修片、给「本地建议」加一条"这张画质偏软，建议结构/锐化"的证据。**这是当前完全缺失、但性价比最高的一块。**

### 2.7 老照片上色（可选）

| 模型 | 许可 | 备注 |
|---|---|---|
| **DDColor** | Apache 2.0 | 双解码器 + ConvNeXt，自然；4 个预训练档 |
| DeOldify | MIT（权重另议） | 更"复古"，色彩偏保守 |

---

## 3. 6GB 显存下的目标栈

核心原则：**分时占用**（每个能力独立进程，用不到就不加载）。下面按"常驻 / 按需"分层：

| 层 | 模型 | 显存 | 加载策略 |
|---|---|---|---|
| 常驻轻量 | SigLIP 2 **base**（嵌入/打标） | ~0.4GB | 服务启动即载，全库批量吞吐 |
| 常驻轻量 | pyiqa CLIP-IQA | ~0.3GB | 与上同一进程亦可 |
| 按需（大） | SigLIP 2 **so400m**（精度档） | ~1.7GB | 需要高精度分类/检索时才切 |
| 按需（大） | **BiRefNet** matting | ~3.2GB | 用户点"抠图"时加载；用完可卸 |
| 按需（大） | **Gemma 4 E4B / Qwen3.5-4B** | ~3GB | 用户点「AI 一键修图」时加载 |
| 按需（大） | Real-ESRGAN / NAFNet / GFPGAN | 1–4GB | 各自独立，串行 |

**关键约束**：BiRefNet(3.2GB) 与 4B VLM(3GB) **不能同时**留在 6GB 卡上。现有架构本来就是"多服务分端口 + 独立降级"，天然满足；只需在客户端**保证不并发触发**（或在服务端加显存互斥/空闲卸载）。

---

## 4. 接入方式（映射到现有契约）

| 升级项 | 装到哪 | 以什么协议暴露 | 客户端改哪里 |
|---|---|---|---|
| **SigLIP 2** | `D:\lightalbum_ai\classifier_server.py`（`venv_cake` 已有 torch） | 保持 `POST /classify` + `GET /health`，**契约不变** | 服务端换**一行**模型名：`create_model('ViT-B-32', pretrained='laion2b_s34b_b79k')` → `create_model('ViT-SO400M-16-SigLIP2-384', pretrained='webli')`；`labels` 可换成中文 prompt。客户端 `classifier.py` **无需改** |
| **Gemma 4 E4B / Qwen3.5-4B** | Ollama（独立，非 Python 进程） | OpenAI 兼容 `POST http://127.0.0.1:11434/v1/chat/completions` | `core/brain.py` 新增 `LocalLLMBrain`（复用 `assert_local()` 守卫），把 VLM 输出的 JSON 映射成 `EditPlan` 的 16 参数；`LocalBrain` 保留为兜底。`JevBrain` 不动 |
| **BiRefNet** | Pixel Cake 内（已有 torch/CUDA + 服务框架） | 新增 `POST /matting`（form: `image_id`）→ 返回带 alpha 的 PNG | `core/pixelcake.py` 加 `matting()` 方法；`brain.plan_ops` 增加"去背景"意图路由 |
| **pyiqa 画质/美学评分** | Pixel Cake 内或独立小服务 | 新增 `POST /score`（form: `image_id`）→ `{quality, aesthetic}` | `core/retouch.py` 的 `recommend_presets` 增加一路"画质证据"；`core/pixelcake.py` 加 `score()` |
| Real-ESRGAN / NAFNet / GFPGAN / DDColor | Pixel Cake 内 | 各自 `POST /upscale` `/denoise` `/face-restore` `/colorize` | `brain.plan_ops` 增意图路由；不可用时降级到 Lumen 现有算子 |

> **不变式**：所有新服务必须 **只绑 127.0.0.1**；所有新调用必须走 `assert_local()`。契约尽量沿用现有风格（JSON / form 混用是历史包袱，新增的**统一用 JSON**）。

---

## 5. 迁移路径（分阶段，每步可回退）

**P0 · 准备（零侵入）**
- 把权重预下到 `D:\lightalbum_ai\hf_cache`；确认 `HF_HUB_OFFLINE` 下能加载。
- 记录基线：当前分类准确率、`/health` 的 `models_loaded`、典型修图耗时。

**P1 · 分类器换 SigLIP 2（最小改动、收益立现）**
- 改 `classifier_server.py` 一行模型名 + prompt 换中文；重跑全库分类 → 对比准确率。
- 回退：改回原常量。客户端**全程不动**。

**P2 · 决策层换成小 VLM（把"没装"变成"装上"）**
- 装 Ollama → `ollama run qwen3.5:4b`（或 `gemma4:e4b`）；验证 `/v1/chat/completions` 可用。
- `brain.py` 加 `LocalLLMBrain`：输入沿用现有 `ctx`（亮度/离散/饱和 + EXIF），要求 VLM 输出 **严格 JSON** 的 16 参数增量 → `EditPlan`。
- 保留 `LocalBrain` 兜底 + `assert_local` 守卫；UI 徽章自然显示"决策：本地 LLM"。
- 回退：`get_brain()` 优先顺序调回。

**P3 · 增补 matting + 画质评分（新能力）**
- Pixel Cake 加 `/matting`（BiRefNet）与 `/score`（pyiqa）。
- `pixelcake.py` 加方法 + `plan_ops` 加意图；不可用时 UI 隐藏入口、引擎照常。
- 把 `/score` 的结果接进 `recommend_presets`（"画质偏软/偏噪"作为新证据）。

**P4 · 可选重模型**：Real-ESRGAN / GFPGAN / DDColor / Retinexformer，按需上。

---

## 6. 许可与合规（自用也建议留痕）

| 模型 | 许可 | 商用 |
|---|---|---|
| SigLIP 2 / Gemma 4 / Qwen3.5 / Qwen3-VL | Apache 2.0 | ✅ |
| Phi-4-mini | MIT | ✅ |
| **BiRefNet** / InSPyReNet | MIT | ✅ |
| GFPGAN | Apache 2.0 | ✅ |
| Real-ESRGAN | BSD-3-Clause | ✅ |
| NAFNet | MIT | ✅ |
| DDColor | Apache 2.0 | ✅ |
| pyiqa / CLIP-IQA / TOPIQ | Apache 2.0 / MIT | ✅ |
| **RMBG-2.0** | **CC BY-NC 4.0** | ❌ 需向 BRIA 授权 ⚠️ |
| **CodeFormer** | **S-Lab 1.0** | ⚠️ 偏非商用 |
| **DINOv3** | Meta DINOv3 License（自定义） | ⚠️ 需读条款 |
| MiniCPM-V | 自定义（商用需登记） | ⚠️ |

---

## 7. 风险与权衡

1. **显存是硬墙**：6GB 决定了"不可能全常驻"。方案依赖**分时 + 独立降级**，需在客户端保证不并发触发重模型。
2. **VLM 出参不稳定**：小 VLM 的 JSON 可能不合规 → 必须**严格校验 + 失败回落 `LocalBrain`**（沿用现有契约的字段级 `clamp`）。
3. **量化副作用**：Q4 会掉一些指令遵循能力；决策这种"要精确出参"的任务，建议**temperature=0 + JSON schema 约束**，并在冒烟测试里固定用例回归。
4. **离线更新成本**：权重是几百 MB–几 GB 级，且 `HF_HUB_OFFLINE=1` ⇒ 升级要**手动预下**，不能靠自动拉取。
5. **SigLIP 2 的收益边界**：对**单标签**零样本提升明显；若业务要"多标签 + 属性"，得换 RAM++/Tag2Text 这类打标模型。
6. **不要为了"更强"而引入云端**：`assert_local()` 是这套栈的价值底线，任何"顺手加个 API 兜底"都会破坏隐私承诺。

---

## 8. 立即可执行的命令（示例）

```bat
:: P1 分类器 —— 权重预下（或直接让服务首次运行下载）
:: 路线 A（推荐，改动最小）：沿用 open_clip 常量
D:\lightalbum_ai\venv_cake\Scripts\python.exe -c "import open_clip; open_clip.create_model_and_transforms('ViT-SO400M-16-SigLIP2-384', pretrained='webli')"
:: 路线 B（open-clip-torch >= 2.31 且 timm >= 1.0.15）：走 timm 的 HF 权重
D:\lightalbum_ai\venv_cake\Scripts\python.exe -c "from open_clip import create_model_from_pretrained, get_tokenizer; create_model_from_pretrained('hf-hub:timm/ViT-SO400M-14-SigLIP2')"
:: 模型名可先用 open_clip.list_models() 核对当前版本是否包含

:: P2 决策层 —— Ollama（需先装 Ollama for Windows）
ollama pull qwen3.5:4b
ollama serve
curl http://127.0.0.1:11434/v1/models

:: P3 抠图/评分 —— 依赖（在 venv_cake 内，先停服务避免文件锁）
D:\lightalbum_ai\venv_cake\Scripts\python.exe -m pip install pyiqa timm kornia
```

> 安装提醒（沿用既有踩坑记录）：Pixel Cake 服务在跑时会锁 `cv2.pyd`，**改依赖前必须先停服务**；opencv 只保留 `opencv-contrib-python` 一个包。

---

## 9. 一句话总结

**把「快而廉的嵌入模型（SigLIP 2）+ 小而能看图的 VLM（Qwen3.5-4B / Gemma 4 E4B）+ 许可干净的抠图（BiRefNet）+ 画质评分（pyiqa）」拼成新栈**：分类器一行换掉、决策层终于真正装上、抠图从"分割"升级到"matting"、并补上画质维度 —— 全部 Apache/MIT，全部 6GB 可跑，全部只需本地回环。

---

*本报告为选型建议，未改动任何代码。落地前请复核模型版本号与许可条款（模型生态月度级变化）。*

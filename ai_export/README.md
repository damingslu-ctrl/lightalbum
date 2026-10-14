# ai_export —— AI oracle 工具脚本（第四轮：本地 ONNX 子进程平价验证轨）

本目录**只入库脚本与随包 ONNX 产物**；一切含真人脸的 fixture 图、真人照片与黄金 JSON
**绝不入库**（R-06 隐私红线，spec §7 #78 fixture 隐私条款）。产物永远只落在 env 指定的本地目录。

| 脚本 | 轨 | oracle 真源 | 产物去向 |
|---|---|---|---|
| `gen_face_fixtures.py` | face | `face_server.FaceEngine` | `%TEMP%`（不入库） |
| `export_clip_onnx.py` | clip | `classifier_server.py` + hf_cache | `native/3rdparty/clip/bin/`（**入库**） |
| `gen_clip_fixtures.py` | clip | `classifier_server.classify_batch` | `%TEMP%`（不入库） |

## gen_face_fixtures.py —— face 平价黄金夹具生成器（SCRFD/ArcFace oracle）

oracle 真源 = `D:\lightalbum_ai\face_server.py` 的 `FaceEngine`（**同进程直调，不起 HTTP**），
依赖 `D:\lightalbum_ai\venv_face` 内的 onnxruntime / numpy / opencv（模型
`D:\lightalbum_ai\models\face\detection.onnx` + `recognition.onnx`，SHA 见设计规格附录 A）。

运行方式（PowerShell）：

```powershell
# 1) 准备 ≥3 张含真人脸的本地照片（建议放 %TEMP%，别放仓库目录）
$env:LA_FACE_IMG = "C:\Users\<you>\AppData\Local\Temp\la_face_fix\face1.png;...face2.png"
#    也接受目录（半角分号分隔多个条目；目录内自动收 jpg/jpeg/png/bmp/webp）

# 2) 跑 oracle，逐图产出 <img>.gold.json（faces 数组 verbatim，embedding 5 位小数）
& D:\lightalbum_ai\venv_face\Scripts\python.exe `
    D:\Test\workbuddy\2026-09-18-09-00-13\ai_export\gen_face_fixtures.py
```

脚本会逐图打印人脸数与 图片/gold 的 SHA-256 清单（钉死可复现性；SHA 只进本地报告，不进 git）。

## export_clip_onnx.py —— CLIP 图像塔 ONNX + prompts.bin 导出（第四轮 T6）

oracle 真源 = `D:\lightalbum_ai\classifier_server.py` 的判定/文本特征算法，权重 =
`D:\lightalbum_ai\hf_cache\models--laion--CLIP-ViT-B-32-laion2B-s34B-b79K\…\open_clip_model.safetensors`
（605,143,316B，与 spec §4 参考基线同 blob）。跑在 `D:\lightalbum_ai\venv_cake`
（torch 2.6.0+cu124 / open_clip 3.3.0），**同进程直调，不起 HTTP**。

模型 pin 本轮钉死 **ViT-B-32 / laion2b_s34b_b79k fp32 eval() CPU**（spec §1：SigLIP 2 属
范围外，本机权重 .incomplete 且 HF 不可达；脚本留 `CLIP_MODEL`/`CLIP_PRETRAINED` 参数位即止，
换 arch 后两产物 SHA 全变，必须重跑 T7/T9 对拍）。

离线红线：`HF_HOME` + `HF_HUB_CACHE` + `HF_HUB_OFFLINE=1` 三件套缺一不可 —— laion 快照实际
落在 `hf_cache\models--laion--…` **顶层**（不在 `hf_cache\hub\` 下），只有 `HF_HUB_CACHE`
指向 `hf_cache` 本身才命中，与 `D:\lightalbum_ai\start_classifier.bat` 同款双变量设置；
本 host 到 HF 的网络不可靠，任何情况下都不许脚本发起下载。

运行方式（PowerShell）：

```powershell
$env:LA_CLIP_LOG = "C:\Users\damin\AppData\Local\Temp\la_clip_export\export.log"   # 可选：stdout UTF-8 镜像
# $env:LA_CLIP_OUT_DIR = "…"        # 可选：默认 native\3rdparty\clip\bin
& D:\lightalbum_ai\venv_cake\Scripts\python.exe `
    D:\Test\workbuddy\2026-09-18-09-00-13\ai_export\export_clip_onnx.py
```

产物（**这两个二进制入库**，LFS-free 直存 = spec §2.1「随包 ONNX」设计；LICENSE 占位随 T10）：

| 文件 | 字节 | SHA-256（T10 `fetch_ai.ps1` 钉死） |
|---|---|---|
| `native\3rdparty\clip\bin\clip_vit_b32_img.onnx` | 351,614,324 | `0c172d0f3c412302294962140942875abb78115b1dffdbc393ddd7fecc9c90c6` |
| `native\3rdparty\clip\bin\prompts.bin` | 16,436 | `d7f65e31385a430ab160315891c12b2b2b6e6cd416365d495a7a62c5b06af2c1` |

计划里「605MB→ONNX 约 600MB+」是按**整模型** safetensors（视觉塔+文本塔 153M 参数）估的；
ONNX 只导 `encode_image` 子图（≈88M 参数），实测 351,614,324 B ≈ 335 MiB。READY 预算仍按
spec §2.1 的 60s 上限走 T7 实测回填。

ONNX 契约：input `image` = `tensor(float)` `1×3×224×224`（已归一 RGB NCHW，
mean/std = CLIP 常量 0.48145466/0.4578275/0.40821078、0.26862954/0.26130258/0.27577711）、
output `feat` = `tensor(float)` `1×512`、opset 17、fp32、**定长无动态轴**。
导出可信度实测（onnxruntime 1.30 `CPUExecutionProvider` 复算同一输入）：
`max|Δ|=8.106e-06`、`mean|Δ|=6.655e-07`、`cos=1.000000000` —— 远小于 #77 概率容差 0.02。
对拍参考张量 `%TEMP%\la_clip_export\{in_f32,ref_feat_f32}.npy`（同种子可复算，不入库，T7 可复用）。
注：venv_cake 未装 `onnx`（torch.onnx.export 序列化需要），脚本把借道目录**追加到
sys.path 末尾**（默认 `venv_face\Lib\site-packages`，可用 `LA_CLIP_ONNX_SITE` 覆盖）——
cake 自有 numpy/protobuf/torch 永远优先命中，不劫持数值路径；绝不为此 pip 改动 oracle venv。

### prompts.bin byte layout（R-12b 钉死，T7 `rules.cpp` 按此解析）

| 偏移 | 字节 | 内容 |
|---|---|---|
| 0 | 10 | magic ASCII `LAPROMPTv1` |
| 10 | 2 | 保留（v1 恒 `00 00`） |
| 12 | 4 | int32-LE `N = 8` |
| 16 | 4 | int32-LE `D = 512` |
| 20 | 16,384 | `N×D` float32-LE 行主序；第 i 行 = `LABELS[i]` 的归一均值文本特征（‖·‖=1） |
| 16,404 | 32 | SHA-256，覆盖 **magic..matrix**（不含自身） |

合计 `20 + 16384 + 32 = 16,436 B`。行序 = `classifier_server.py:31-56` LABELS 表序：
`0=人像 1=风光 2=夜景·星空 3=城市·建筑 4=微距·花卉 5=动物 6=美食 7=黑白`；
本产物内嵌矩阵 SHA = `d06822a29f77911c5559d1f770793123da9f4f8e438a78bb4a82ec9dee9fc481`
（T7 启动校验即：重算 magic..matrix 的 SHA-256 与尾部 32B 比对，不等 → `READY 0`/`badframe`）。

算法 verbatim（`classifier_server.py:137-141`）：逐 prompt `encode_text` → 行 L2 归一 →
**类内均值**（`feat.mean(dim=0)`）→ 再行 L2 归一。LABELS 表逐字复制入脚本，且运行时
`import classifier_server` 做 `LABELS ==` 全等交叉校验（漂移即抛，表变了导出必失败，不会静默产出错表）。
落盘**前**用 `struct` 独立复读内存 blob 断言 长度/magic/保留位/N/D/逐元素位等/行范数=1/尾部 SHA，
落盘**后**再对磁盘字节校验一遍；任一不符即抛，绝不写坏文件。

## gen_clip_fixtures.py —— CLIP 平价黄金夹具生成器（CLIP oracle）

同进程直调 `classifier_server.load_model()` + `classify_batch()`（**不起 HTTP**）——
gold 由**服务器自己的判定代码路径**产出（黑白确定性 → low_confidence → mono_guard 三段
+ top3 4dp + sat），不是手抄等价逻辑（R-12d）。返回行 verbatim 写 `<name>.gold.json`。

夹具来源（R-12a）：**仓库里没有已入库的 QA 安全照片**（计划 Step 2 的前提不成立），故沿用
face 轨先例 —— 图与 gold **一律不入库**，只经 env 消费。本 host 复现用：

```powershell
$env:LA_CLIP_IMGS       = "C:\Users\damin\Pictures\iCloud Photos\Photos"          # 逗号分隔，条目可为图或目录
$env:LA_CLIP_SCREEN_MAX = "160"      # 池子过大时确定性等距抽样上限（可复现）
$env:LA_CLIP_PICK       = "12"       # 目标夹具数（下限 6，覆盖 ≥4 类）
$env:LA_CLIP_FIX_DIR    = "C:\Users\damin\AppData\Local\Temp\la_clip_fix"          # 选中图拷进这里（= #77 喂的路径）
$env:LA_CLIP_GOLD_DIR   = "C:\Users\damin\AppData\Local\Temp\la_clip_fix"          # gold 落此；未设则写 <img>.gold.json 于图旁
$env:LA_CLIP_LOG        = "C:\Users\damin\AppData\Local\Temp\la_clip_export\gen_clip.log"
& D:\lightalbum_ai\venv_cake\Scripts\python.exe `
    D:\Test\workbuddy\2026-09-18-09-00-13\ai_export\gen_clip_fixtures.py
```

CUDA 红线（Windows 坑）：`CUDA_VISIBLE_DEVICES=''` 在 Windows CRT 语义下是**删除变量**
（本机实测 `load_model` 仍落 `cuda`），故脚本在进程内把 `torch.cuda.is_available` 钉成
`False`，让 `classifier_server` 走 `device='cpu'` 且**不进 :188-192 的 autocast fp16 分支**；
加载完 `device!='cpu'` 直接硬失败退出。理由：gold 必须与随包 clipinfer 同一条 CPU fp32
数值路径（`CPUExecutionProvider` 是唯一 provider），否则 oracle 与 ONNX 从一开始就不是同一个函数。

### 预筛余量（R-12c，face 轨 R-11 教训的 CLIP 版）

任何落在决策边界上的候选一律弃用（`screen` 字段随 gold 一起留档，便于复核）：

| 维度 | 判据 | 被这条线翻转的分支 |
|---|---|---|
| `top1 − top2` | ≥ 0.10 | 类别归属（→ label / top3 全列） |
| `‖sat − 18.0‖` | ≥ 6.0 | `MONO_SAT_THRESHOLD` 黑白确定性优先 |
| `‖sat − 45.0‖` | ≥ 6.0 | `MONO_SAT_GUARD` 黑白误报护栏 |
| `‖top1 − 0.20‖` | ≥ 0.05 | `OTHER_SCORE_THRESHOLD` → 「其他」 |
| `‖护栏次优 − 0.20‖` | ≥ 0.05 | mono_guard 采信 / 回落「其他」 |

选中集硬性下限（不满足即 exit 2，绝不静默产出弱夹具）：≥6 张、覆盖 ≥4 个不同 label、
至少一张**确信黑白**（sat ≤ 12.0）与一张**确信彩色**（sat ≥ 51.0）。

### 本次实跑结果（160 张池 → 112 张越界合格 → 选 12 张，覆盖 9 个 label）

| fixture（`%TEMP%\la_clip_fix\`，不入库） | label | score | sat | note | gap | ‖s−18‖ | ‖s−45‖ | ‖t−.20‖ |
|---|---|---|---|---|---|---|---|---|
| `D1DAA4C5-E2A7-476E-85B8-D368D2BDF39C.png` | 人像 | 0.9993 | 29.7 | — | 0.9986 | 11.7 | 15.3 | 0.7993 |
| `IMG_1645.JPG` | 其他 | 0.9936 | 83.1 | `mono_guard(sat=83.1)` | 0.9895 | 65.1 | 38.1 | 0.7936（次优距 0.20 = 0.1959） |
| `IMG_1844(1).JPG` | 动物 | 0.9999 | 65.1 | — | 0.9998 | 47.1 | 20.1 | 0.7999 |
| `IMG_1981(1).JPG` | 城市·建筑 | 0.9978 | 52.1 | — | 0.9966 | 34.1 | 7.1 | 0.7978 |
| `IMG_2288.JPG` | 黑白 | 0.9674 | 37.5 | — | 0.9539 | 19.5 | 7.5 | 0.7674 |
| `IMG_2540.PNG` | 夜景·星空 | 0.9259 | 105.9 | — | 0.8718 | 87.9 | 60.9 | 0.7259 |
| `IMG_3694.JPG` | 美食 | 1.0 | 117.1 | — | 1.0 | 99.1 | 72.1 | 0.8 |
| `IMG_3721.JPG` | 美食 | 1.0 | 129.1 | — | 1.0 | 111.1 | 84.1 | 0.8 |
| `IMG_3953.JPG` | 美食 | 1.0 | 176.1 | — | 1.0 | 158.1 | 131.1 | 0.8 |
| `IMG_8409.JPG` | 黑白 | 0.99 | 3.8 | `low_saturation(3.8)` | 0.8511 | 14.2 | 41.2 | 0.7235 |
| `IMG_8530.JPEG` | 风光 | 0.9728 | 66.5 | — | 0.9468 | 48.5 | 21.5 | 0.7728 |
| `IMG_9387.JPG` | 微距·花卉 | 0.8588 | 116.6 | — | 0.7838 | 98.6 | 71.6 | 0.6588 |

判定分支覆盖：模型直判（9 张）/ 黑白确定性优先（`IMG_8409`，注意它 top1 本是 美食 0.9235，
被 sat 3.8 强制翻成 黑白 0.99 —— 正是 rules.cpp 三段的第一段）/ 模型判黑白但未触护栏
（`IMG_2288`，sat 37.5 ∈ (18,45)）/ 护栏回落「其他」（`IMG_1645`）。
**唯一未覆盖分支**：`low_confidence` 版「其他」（需 top1 ≤ 0.15）—— 160 张池里零命中，
T7 若要测这条，得换池子或人工造一张纯色/噪声图（gold 仍须由 oracle 产）。

gold JSON 字段：`label/score/note/top/sat` = `classify_batch` 返回行 **verbatim**（top3 4dp），
另附 `image/oracle/model/thresholds/screen` 溯源块。#77 按 spec §4 容差消费：
label/note 逐字符等、top-probs 逐类 Δ≤0.02、sat Δ≤1.0。

## 消费方

- `faceinfer.exe --det-selftest <image> <gold.json>`（T2）：读 gold 的 `faces[]`，
  断言 人脸数精确相等 / 每脸 IoU ≥ 0.98 / kps 五点欧距均值 ≤ 2.0 px，exit 0=PASS。
- #78 `LA_FACE_MODEL_QA`（T3 注册，opt-in）：真 faceinfer.exe 起子进程 REQ/RES 全链路
  跑五列容差（追加 embedding 余弦 ≥ 0.999、score 差 ≤ 0.02）。
- `clipinfer.exe`（T7）：启动校验 `prompts.bin`（magic + 内嵌 SHA 尾），运行时只跑
  `clip_vit_b32_img.onnx` + prompts 矩阵，不再 tokenize。
- #77 `LA_CLIP_MODEL_QA`（T7 注册，opt-in）：经 `LA_CLIP_IMGS` + `LA_CLIP_GOLD_DIR`
  消费上面的本地夹具（**夹具与 gold 永不入库**），逐张比 label/note 逐字符、top3 逐类
  Δ≤0.02、sat Δ≤1.0。

## 约定

- 图像字节必须与两侧解码一致：推荐 PNG（无损），避免 JPEG 双解码器（libjpeg-turbo vs
  Qt）像素级分歧——IoU/kps 容差本身可吸收，但 PNG 让 FAIL 归因唯一（算法而非解码）。
- gold 的 faces 顺序 = face_server `_nms` keep 序；selftest 比较核按 IoU 贪心 1:1 匹配，
  不依赖顺序。
- **CLIP 轨例外**：夹具以 JPEG 为主（`REQ jpg` 就是 clipinfer 的线上格式，生产路径本就是
  JPEG 双解码器，分歧只由 R-12c 余量 + #77 容差吸收）；本批混入 2 张 PNG
  （`D1DAA4C5-….png`、`IMG_2540.PNG`）——它们是库里真实存在的无损照片类型，T7 的解码入口
  必须按 magic 嗅探格式而非假定 `jpg` 三字符，否则 #77 会在 PNG 夹具上假红。
- sat 判定移植注意（给 T7）：`classifier_server.py:160-169` 是
  `img.convert('RGB').resize((48,48))` —— PIL 的 `resize` 默认 resample 是 **BICUBIC（非双线性）**，
  且大图直降 48×48 时 PIL 内部先做 `reducing_gap` 预滤波抗锯齿。若 imgkit 用朴素 48×48
  双线性/三次插值，高饱和图的 sat 绝对值可偏移远超 #77 的 Δ≤1.0（决策本身因 R-12c ≥6.0
  余量不会翻转，但断言会红）—— 按 spec §4「以实现期对拍实测定 kernel 变体」处理，
  要么复刻 PIL 预滤波链，要么把 #77 的 sat 断言改成决策带一致。


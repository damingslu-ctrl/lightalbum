# 第三方代码归属声明

## ai-photo-manager（选片引擎能力来源）

- 项目：`laoziocean-code/ai-photo-manager`
- 地址：https://github.com/laoziocean-code/ai-photo-manager
- 许可：**MIT License**，Copyright (c) 2026 AI摄影管家 Contributors
- 吸收进本项目的位置：
  - Python 客户端（历史资产）：`client/lightalbum/core/culling.py`、`client/lightalbum/core/engines/culling_engine.py`
  - **C++/Qt 原生（当前优化重心）**：`native/src/core/CullEngine.cpp`、`native/src/core/CullEngine.h`
    —— 这两个文件带 `SPDX-License-Identifier: MIT` 头，由发布契约 **C6** 强制校验；
    本文件以 `THIRD_PARTY-NOTICE.md` 之名随 dist 一起分发（契约 C6 同时校验它在包内）。

### 吸收的能力

| 能力 | 原实现 | 本项目落地（Python / C++ 原生） |
|---|---|---|
| MD5 精确去重 | `preprocessing/deduplication.py` | `culling.compute_md5` / `CullEngine::computeMd5` |
| pHash 相似检测（纯 numpy DCT-II，与 imagehash 一致，零 scipy） | `deduplication.PHash / _dct2 / find_groups` | `culling.PHash` / `CullEngine::computePhash` + `dct2` |
| 五档去重强度（关闭/极严/严格/标准/宽松/极宽 → None/3/5/8/12/18） | `DEDUP_LEVELS` | `culling.DEDUP_LEVELS` / `CullEngine` 去重阈值 |
| Laplacian 方差模糊检测（统一缩放最长边 512，阈值 80；失败记 999 不误杀） | `preprocessing/blur_detect.py` | `culling.blur_score` / `CullEngine::blurScore` |
| 曝光检测（暗 <25 / 亮 >230 占比 → 正常/欠曝/过曝/严重欠曝/严重过曝） | `preprocessing/exposure_detect.py` | `culling.exposure_stats` / `CullEngine::exposureStats` |
| 分辨率过滤（最短边 ≥800 且 ≥0.5MP；尺寸未知则放行） | `preprocessing/quality_filter.py` | `culling.assess` / `CullEngine::assess` |
| **RAW 豁免**（Laplacian 对夜空等暗场景系统性失灵，RAW 不做模糊/曝光淘汰） | `quality_filter._filter_quality` | `culling._filter_quality` / `CullEngine`（`isRaw` 由原名扩展名判定） |
| 相似组保留「最清晰者」 | `quality_filter._best` | `culling._sharpest` / `CullEngine` 相似去重保留最清晰 |
| 候选集 / 废片集 + `reject_reason`（先本地过滤再交给 AI，省算力） | `quality_filter.filter_photos` | `culling.filter_photos` / `CullEngine`（`status` + `rejectReason`） |
| 8 维评分权重 + S/A/B/C/D 等级 | `scoring/scorer.py` | `culling.WEIGHTS / compute_total / grade` / `CullEngine::computeTotal` + `grade` |
| TopN 精选 | `selection/top_selector.py` | `culling.select_top` / `CullEngine`（`topN` 选项） |
| 四文件夹归档（精选/普通/废片/去重） | `selection.export_organized` | `culling.export_organized`（C++ 侧由原生归档 UI 承担） |

### 刻意未吸收的部分

- **云端视觉模型接入**（`core/ai/` 的 `OpenAICompatibleModel` / `AnthropicModel`、
  prompt 模板与 JSON 容错解析）：本项目铁律是修图与 AI **零云端**，故不引入。
  8 维评分中主观维度改由本地 AI（musiq / Ollama）经 `ai_scores` 参数注入补全。
- API Key 加密（`utils/crypto.py`）：本项目已有 `core/vault.py`（PBKDF2+Fernet）。
- HTML 杂志风报告（`core/report/`）：暂不需要。

### 针对本项目做的改造

1. **零云端**：只填本地可测维度（清晰度 / 光影），主观维度留 `None`，
   按剩余权重重归一化计总分并回报 `coverage` —— 遵守数据诚实铁律，不编造默认值。
   （原实现把缺失维度当 0 分，会把好照片拉低。）
2. **零重依赖**：本机客户端环境无 cv2 / imagehash / scipy，
   故 pHash 用纯 numpy DCT-II、模糊检测在无 cv2 时用四邻域 Laplacian，功能等价。
3. RAW 缩略图优先走 rawpy 内嵌预览，避免整幅解码拖慢批量选片。
4. 引擎层剥掉不可序列化的 `PHash` 对象（只留 `phash_str`），保证跨桥接 JSON 安全。
5. **C++/Qt 原生重写**：`native/src/core/CullEngine.*` 用 `QImage` + 纯 C++ 数学重写了上述
   pHash/DCT-II/相似去重/模糊/曝光/评分逻辑，算法与 Python 侧一致但不依赖 numpy/OpenCV。
   文件头带 `SPDX-License-Identifier: MIT` + 上游 URL + 版权行。

---

## FFmpeg（视频格式支持，2.28.0 起）

- 项目：**FFmpeg**
- 地址：https://ffmpeg.org / 构建来源 https://www.gyan.dev/ffmpeg/builds/
- 取用形式：官方预编译 Windows 产物 `ffmpeg-release-essentials.zip`
  （实测版本串 `9.0.2-essentials_build`，由 MSYS2 gcc 16.2.0 构建）
- 许可：**GNU General Public License version 3（GPL-3.0）**
  —— 实测该构建的 `configuration` 含 `--enable-gpl --enable-version3 --enable-libx264`，
  **是 GPL-3.0，不是 LGPL**。许可证全文随附于 `3rdparty/ffmpeg/LICENSE`。
- 版权：Copyright (c) 2000-2026 the FFmpeg developers

### 重要：本项目自身的授权

本项目此前以 MIT 方式声明归属（部分源文件带 `SPDX-License-Identifier: MIT`）。
引入 GPL-3.0 的 FFmpeg 后，经审慎评估采取以下立场：

- **本项目的源代码许可不变**（仍为 MIT）。理由：FFmpeg 是以**独立进程**方式调用的
  （见 `native/src/core/VideoDecode.cpp`，经 `QProcess` 调 `ffmpeg.exe` / `ffprobe.exe`），
  与 `LightAlbum.exe` **不构成同一可执行映像、不存在链接或重链接关系**。
  在 GPL 术语下这属于 *mere aggregation*（简单聚合），FFmpeg 的 copyleft **不传染**
  到本项目源代码。此点与下方 LibRaw 的论证结构相同。
- **但我们不隐瞒**：发布包内**确实包含** GPL-3.0 程序的完整副本。因此：
  - 完整的 GPL-3.0 文本随包分发（`3rdparty/ffmpeg/LICENSE`），由 `release.ps1`
    在缺失时**直接 throw 中止打包**；
  - 本文件同时说明该组件的来源、版本与许可，便于接收方行使 GPL 赋予的
    重新分发与获取源码的权利（FFmpeg 源码：https://github.com/FFmpeg/FFmpeg）。
- 若未来需要**将 FFmpeg 链入本 exe**（而非独立进程调用），则上述「不传染」结论
  **不再成立**，必须重新评估本项目整体许可。

### 刻意未使用的能力（减小体积与许可面）

- **`ffplay.exe` 不随包分发**（约 107MB）：本项目只抽帧与读元数据，**从不播放视频**。
- 只用到「抽 1 帧 JPEG」与「读容器/视频流元数据」两项能力；
  构建期链接的 `libx264` 等编码器对本项目**完全无用**（属上游通用构建的固定配置）。
- 随包仅 `ffmpeg.exe` + `ffprobe.exe`（各约 100MB，**全静态、无 DLL**），
  故发布包体积增加约 200MB —— 这是已知并被接受的取舍。

---

## LibRaw（RAW 格式解码，2.28.0 起）

- 项目：**LibRaw**
- 地址：https://www.libraw.org / https://github.com/LibRaw/LibRaw
- 取用形式：官方预编译 Windows 产物 `LibRaw-0.22.2-Win64.zip`（MSVC 2022 构建，64 位）
- 许可：**双许可，由使用者择一** —
  1. **GNU LESSER GENERAL PUBLIC LICENSE version 2.1**（`LICENSE.LGPL`）
  2. **COMMON DEVELOPMENT AND DISTRIBUTION LICENSE (CDDL) Version 1.0**（`LICENSE.CDDL`）
- 版权：Copyright (C) 2008-2025 LibRaw LLC（http://www.libraw.org, info@libraw.org）
- 上游致谢（见 `3rdparty/libraw/COPYRIGHT`）：
  - `dcraw.c` — Copyright 1997-2018 Dave Coffin
  - DCB demosaic / FBDD denoise — BSD-like 3-clause，Copyright (C) 2010 Jacek Gozdz
  - X3F library — BSD-style，Copyright (c) 2010 Roland Karlsson
  - Adobe DNG SDK 1.4 部分代码 — MIT，Copyright (c) 2005 Adobe Systems Incorporated

### 为什么以「独立进程」方式使用（合规要点）

本项目**不把 `libraw.dll` 链入 `LightAlbum.exe`**，而是通过 `QProcess` 调用其官方命令行
工具 `dcraw_emu.exe`（见 `native/src/core/RawDecode.cpp`）。由此：

- 解码器与宿主 exe **不构成同一个可执行映像**，不存在链接/重链接关系；
  宿主（MinGW/GCC 构建）与解码器（MSVC 构建）的 CRT/ABI **互不约束** ——
  这也是可以直接使用官方 MSVC 预编译产物、无需自行从源码构建的原因。
- 随包分发的二进制仅为**上述命令行工具与其运行时 DLL**，完整许可文本随附。

### 随包分发

- `native/3rdparty/libraw/` 下的 `LICENSE.LGPL`、`LICENSE.CDDL`、`COPYRIGHT`
  随 dist 一并分发（LGPL 2.1 §4 / CDDL §4 均要求随二进制附带许可文本）。
- 上述文件由 `native/fetch_libraw.ps1` 在获取产物时一并落位；该脚本**纯 ASCII**
  （PS 5.1 无 BOM 按 GBK 解析中文注释会报「字符串缺少终止符」）。

### 仓库不收录二进制

`native/3rdparty/libraw/bin/` 已列入 `.gitignore`（本仓库零二进制惯例）。
产物由 `fetch_libraw.ps1` 按需下载，`CMakeLists.txt` 在 `POST_BUILD` 阶段将其
复制到 `<exe 同级>/3rdparty/libraw/bin`，使开发态与发布态走**同一条查找路径**。
未获取到产物时 RAW 支持**优雅降级为不导入**（`RawDecode::isImportable`），
绝不产生「有记录但无缩略图」的空卡片。

---

## ONNX Runtime + LaMa 模型（AI 消除执行器，2.35.0 起）

- 项目：
  1. **Microsoft ONNX Runtime** — https://github.com/microsoft/onnxruntime ，
     版本钉死 **v1.19.2**（Windows x64 C/C++ 发行包）；许可 **MIT**，全文见
     `inpaint/THIRD_PARTY-NOTICE.md`（打包时随附为 `3rdparty/lama/THIRD_PARTY-NOTICE.md`）。
  2. **LaMa 图像修复模型权重** — HuggingFace `Carve/LaMa-ONNX` 的
     `lama_fp32.onnx`（advimman/lama, WACV 2022 的 ONNX 导出），本地落名 `lama.onnx`；
     实测 208,044,816 B，SHA-256 已钉死于 `native/fetch_lama.ps1`。许可 **Apache-2.0**
     （模型卡 `license: apache-2.0`；HF 仓库内无 LICENSE blob，文本取自导出方官方
     GitHub 源 `Carve-Photos/lama`），全文随包 `3rdparty/lama/LICENSE`。

### 为什么以「独立进程」方式使用（合规要点）

`inpaint.exe`（仓库根 `inpaint/` 独立项目，MSVC 或 MinGW 构建）经 QProcess 帧协议与
宿主通信（`native/src/core/EraseRunner.cpp`）：

- ONNX Runtime **只链进子进程映像**，`LightAlbum.exe` 零新增链接依赖——与 FFmpeg/LibRaw
  同理属 *mere aggregation*；MIT 义务由「声明随子进程产物分发」满足。
- 模型权重是**运行期数据文件**，不链接进任何可执行映像；Apache-2.0 允许随发行包再分发，
  条件是附许可文本并保留归属（由打包契约 C9 保障）。
- 零云端：`inpaint.exe` 不发起任何网络请求；权重与 dll 由用户/构建者**主动运行
  `fetch_lama.ps1` 才落盘**（fail-closed：SHA 未钉死拒绝真实下载、LICENSE 缺失拒绝落盘），
  未 fetch 则发行包不含这 4 件 lama 产物。

### 随包分发

- `native/release.ps1` 契约 **C9**：staging 内出现 `3rdparty/lama/bin/inpaint.exe` 则必须
  同场携带 `3rdparty/lama/LICENSE` 与 `3rdparty/lama/THIRD_PARTY-NOTICE.md`，缺一即
  **打包中止并 throw**（`-SelfTest` 注入臂已证明该契约能触发）。
- `fetch_lama.ps1` 为纯 ASCII（PS 5.1 兼容，同 `fetch_libraw.ps1` 教训）。

### 仓库不收录二进制

`native/3rdparty/lama/bin/` 与 `inpaint/build/` 均已列入 `.gitignore`（零二进制惯例）；
`inpaint.exe` 只能由构建者按 `inpaint/BUILD.md` 本地构建，任何脚本不下载它。

---

## 随包分发

- `docs/THIRD_PARTY_NOTICE.md` 在打包时复制为 dist 根目录的 **`THIRD_PARTY-NOTICE.md`**，
  随 zip 一同分发（MIT 要求随二进制附带归属声明）。
- 打包路径由 `native/release.ps1` 的契约 **C6 `Assert-LicenseCompliance`** 强制执行：
  ① dist 内 `THIRD_PARTY-NOTICE.md` 存在且非空；② 列出的每个派生源文件首个非空行
  必须含 `SPDX-License-Identifier`。任一不满足 → **打包中止并 throw**。
- 该契约同样被 `release.ps1 -SelfTest` 在一次性沙盒里注入缺陷驱动（QA 门 `LA_RELEASE_QA`）。

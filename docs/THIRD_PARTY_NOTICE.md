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

## 随包分发

- `docs/THIRD_PARTY_NOTICE.md` 在打包时复制为 dist 根目录的 **`THIRD_PARTY-NOTICE.md`**，
  随 zip 一同分发（MIT 要求随二进制附带归属声明）。
- 打包路径由 `native/release.ps1` 的契约 **C6 `Assert-LicenseCompliance`** 强制执行：
  ① dist 内 `THIRD_PARTY-NOTICE.md` 存在且非空；② 列出的每个派生源文件首个非空行
  必须含 `SPDX-License-Identifier`。任一不满足 → **打包中止并 throw**。
- 该契约同样被 `release.ps1 -SelfTest` 在一次性沙盒里注入缺陷驱动（QA 门 `LA_RELEASE_QA`）。

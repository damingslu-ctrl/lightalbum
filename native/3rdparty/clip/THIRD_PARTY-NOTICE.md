# THIRD_PARTY NOTICE — native/3rdparty/clip/

本目录（`bin/` + 本文件 + `LICENSE`）是随包 `clipinfer.exe`（CLIP ViT-B-32 图像塔 +
判定规则）的第三方与模型依赖声明。文件名用**单数** `THIRD_PARTY-NOTICE.md`（R-28b：与
仓内先例 `inpaint/THIRD_PARTY-NOTICE.md`、`native/release.ps1` 的 `$NOTICE_NAME` 及 C9
断言对象一致）；`LICENSE` 与本文件位于 `bin/` **上一级**（lama 先例）。

**本文件的必须出现项是四条**（R-54，缺一不可）：① 权重 MIT + 训练数据 CC-BY-4.0 + blob
溯源；② ONNX Runtime 1.19.2 MIT；③ vendored `stb_image` v2.30；④（face 侧同构，见
`3rdparty/face/THIRD_PARTY-NOTICE.md`）。理由：T10 的 C10 硬检只断言 NOTICE **非空**
（C9 先例同粒度），抓不到"漏了 stb"或"把条款写软了"，所以存在性断言由
`native/fetch_ai.ps1` 的 `Invoke-ProvenanceChecks` 承担，并由 `-SelfTest` 用"删掉
`stb_image` 那一行必须变红"反证其为活断言。

## 1. CLIP 权重（open_clip / laion2b_s34b_b79K）— **MIT**；训练数据 LAION-2B — **CC-BY-4.0**

| 随包文件 | 内容 | 字节数 | SHA-256（fetch 硬检值） |
|---|---|---|---|
| `bin/clip_vit_b32_img.onnx` | CLIP ViT-B-32 **图像塔**（`encode_image`）ONNX 导出 | 351,614,324 | `0c172d0f3c412302294962140942875abb78115b1dffdbc393ddd7fecc9c90c6` |
| `bin/prompts.bin` | 文本侧提示词嵌入矩阵（离线预导，运行期零文本塔） | 16,436 | `d7f65e31385a430ab160315891c12b2b2b6e6cd416365d495a7a62c5b06af2c1` |

- 权重模型仓：<https://huggingface.co/laion/CLIP-ViT-B-32-laion2B-s34B-b79K>
  （HF 官方 API `cardData`：`license: mit`，tag `license:mit` → **权重 MIT**）。
- **blob 溯源**：源权重 =
  `D:\lightalbum_ai\hf_cache\models--laion--CLIP-ViT-B-32-laion2B-s34B-b79K\snapshots\1a25a446712ba5ee05982a381eed697ef9b435cf\open_clip_model.safetensors`
  = **605,143,316 B**（快照名 `1a25a446712ba5ee05982a381eed697ef9b435cf`；台账记其 SHA-256
  前缀 `ac4f8c4b88af6d96…`，本轮未复算全值，故只按前缀记录、不冒充完整摘要）。
  随包 onnx 只含视觉塔，故体积小于该 safetensors。
- 导出链：`ai_export/export_clip_onnx.py`（`OUT_DIR` 默认即 `native\3rdparty\clip\bin`，
  就地生产、无中间拷贝）。**不存在第二次下载**（R-56）：该 onnx 不是 HF 直下物，
  `fetch_ai.ps1` 的 clip 分支只有三级 —— ① 目标已存在且 SHA 命中 → verify-only（本机默认快路径，
  门 #77 今天就靠它）；② 目标缺失且给了 `-ClipOnnx` → 先算 SHA 后落地；③ 目标缺失又没给源 →
  throw 并给出可复现离线命令
  `& D:\lightalbum_ai\venv_cake\Scripts\python.exe ai_export\export_clip_onnx.py`。
  禁止任何 `Invoke-WebRequest`/curl 兜底。
- `prompts.bin` 另承载一个**内嵌矩阵 SHA** `d06822a29f77911c5559d1f770793123da9f4f8e438a78bb4a82ec9dee9fc481`
  （`clipinfer` 启动校验常量）。**它与整文件 SHA-256 不同，勿混**。
- 训练数据：LAION-2B（`laion2B-en` 子集）— **CC-BY-4.0**
  <https://creativecommons.org/licenses/by/4.0/>；数据集说明 <https://laion.ai/blog/laion-5b/>。
  该条款约束的是**训练语料**，语料本身不随包分发。
- 上游代码：`mlfoundations/open_clip`（MIT）。本目录 `LICENSE` = 其 `LICENSE` 文件
  （1,229 B）的**逐字节副本**，取自控制端缓存
  `.superpowers/sdd/2026-10-13-engine-round4-local-ai-subprocess/upstream-open_clip-LICENSE.txt`
  （SHA-256 `4a5b4f13ea4792a8211a69f32d2b460b6d520e57cd23c6301e707c76a6e97a55`，
  上游 URL <https://raw.githubusercontent.com/mlfoundations/open_clip/main/LICENSE>，
  HTTP 200，1,229 B）。原文如下（未手打、未改写）：

```
Copyright (c) 2012-2021 Gabriel Ilharco, Mitchell Wortsman, 
Nicholas Carlini, Rohan Taori, Achal Dave, Vaishaal Shankar, 
John Miller, Hongseok Namkoong, Hannaneh Hajishirzi, Ali Farhadi, 
Ludwig Schmidt

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## 2. ONNX Runtime 1.19.2 — **MIT**（运行时依赖）

- 项目：Microsoft ONNX Runtime — <https://github.com/microsoft/onnxruntime>
- 随包文件：`bin/onnxruntime.dll`，v1.19.2 win-x64，11,234,848 B，
  SHA-256 `14119125df2dcf9ff3e083afdba5fcc4b09b4186d8762404eb7b1fbccde3fcf2`
  ——与 `native/3rdparty/lama/bin/onnxruntime.dll`、`face/bin/onnxruntime.dll` 为**同一钉死副本**
  （实测同 SHA，即官方 v1.19.2 zip 那份；三处 bin 各一份，进程 cwd 隔离）。
- 发行包 URL（口径同 `fetch_lama.ps1`）：
  <https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-win-x64-1.19.2.zip>
- 许可文本取自解包目录 `D:\la-tools\onnxruntime-win-x64-1.19.2\LICENSE`（**1,094 B 在盘**，
  SHA-256 `c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674`）的落盘原文拷贝
  （未手打；仅把源文件的 CRLF 行尾归一为本仓库统一的 LF，文本逐字符相同——本轮实测：抽出
  下方代码块与源文件行尾归一后做全等比较为真），如下：

```
MIT License

Copyright (c) Microsoft Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

分发要求：随 `clipinfer.exe` 分发的 `onnxruntime.dll` 属该库的对象代码形式，上述版权与
许可声明须随发行包文档一并携带（由 T10 的 release.ps1 装配腿负责）。

## 3. stb_image v2.30 — public domain（vendored 单头文件，随源码入库）

`clipinfer.exe` 全程无 Qt（R-15 裁定），读盘解码由 vendored 单头文件
`clipinfer/stb_image.h` 承担（magic 嗅探，JPEG/PNG 通解）。

- 文件：`clipinfer/stb_image.h`，**283,010 B**，作者 Sean Barrett。
- 该文件第 1 行原文（逐字节引自仓内文件）：

```
/* stb_image - v2.30 - public domain image loader - http://nothings.org/stb
```

- 按其文件头/文件尾原文，本软件为 public domain，并给出**双授权可选**
  （"available under 2 licenses" —— ALTERNATIVE A 为 MIT，ALTERNATIVE B 为
  Public Domain / Unlicense）：

```
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
```

- 归属义务：public domain / Unlicense 臂要求保留本声明；MIT 臂要求随所有副本携带
  版权与许可声明 —— 因此本条必须出现在本 NOTICE 中（T12 终审机械检查项 R-83-B2）。
- 注意与 `bin/` 的区别：`stb_image.h` 是**源码**，入库正确（R-29b）；模型二进制**不入库**，
  两者无关。

## 4. `clipinfer.exe` 自身（本仓自建，非第三方）

- 源码在 `clipinfer/`（独立 CMake 工程，MinGW `-static` + `ort_mingw_shim.h`，模板 = `faceinfer/`）；
  判定规则真源 `clipinfer/rules.cpp`（`kLabels[8]` 为名册权威）。本仓自有代码，许可随本仓库。
- 构建产物**不入库**，由 `native/fetch_ai.ps1` 从 `clipinfer/build/`（或 `-ClipInferDir` 指定目录）
  拷入 `bin/`；拷贝前后各算一次 SHA-256 并断言相等（就地自检拷贝完整性）。exe 是重建产物，
  **故意不钉 SHA-256**（R-109），缺失时 fetch 直接 throw 并给出可复现构建命令
  （绝不静默跳过，判例 `fetch_lama.ps1:231`）。
- 本进程无 Qt 依赖（"终端机零运行时"判例在本工程恢复成立）：发布装配腿只需随包带
  `onnxruntime.dll`，无 windeployqt 腿。

## 5. 树态与 fetch 校验契约

- 入库现状（R-77 分区断言，**不是违规**）：`native/3rdparty/clip/bin/` 里
  `clip_vit_b32_img.onnx` + `prompts.bin` **由 T6 的 `d3e36d2` 提交并保持跟踪**
  （`git ls-files native/3rdparty/clip/bin` = 2），而 `faceinfer.exe` / `clipinfer.exe` /
  `onnxruntime.dll` 只落盘不入库（`git ls-files native/3rdparty/face/bin` = 0）。
  历史体积的真正处置属 R-49 推送决断（用户门），此处不做 `git rm --cached`。
- `native/fetch_ai.ps1` 硬检本目录：`LICENSE` 非空、`THIRD_PARTY-NOTICE.md` 非空，
  必现条目 `stb_image` / `MIT` / `CC-BY-4.0` / `onnxruntime` 各命中 ≥1，
  以及第 1/2 节的 SHA-256 钉死值。`-SelfTest` 在**临时沙箱**内注入反例（注坏 SHA、删
  `LICENSE`、删 `THIRD_PARTY-NOTICE.md`、删 `stb_image` 行、缺 clip 产物、缺 exe 构建产物），
  真实的 `native/3rdparty/**` 不被破坏（R-110）。

# THIRD_PARTY NOTICE — native/3rdparty/face/

本目录（`bin/` + 本文件 + `LICENSE`）是随包 `faceinfer.exe`（SCRFD 检测 + ArcFace 识别）
的第三方与模型依赖声明。文件名用**单数** `THIRD_PARTY-NOTICE.md`（R-28b：与仓内既有先例
`inpaint/THIRD_PARTY-NOTICE.md` 与 `native/release.ps1` 的 `$NOTICE_NAME` / C9 断言对象一致，
brief 原写的复数 NOTICES 作废）。`LICENSE` 与本文件位于 `bin/` **上一级**（lama 先例
`3rdparty/lama/LICENSE`）。

运行期零网络、零云端：`bin/` 内每个字节由 `native/fetch_ai.ps1` 在**落地前**算 SHA-256
并比对钉死值（先算后拷，R-26），不匹配则 throw 且不写一个字节。`bin/` 内容**不入库**
（`.gitignore`，R-29b：本仓既定政策 = 第三方二进制磁盘就位、git 只放许可文本）。

## 1. 人脸模型权重（buffalo_l 系）— **仅限非商业研究用途**

| 随包文件 | 上游模型 | 字节数 | SHA-256（fetch 硬检值） |
|---|---|---|---|
| `bin/detection.onnx` | SCRFD-10GF（insightface 原名 `det_10g.onnx`，两者逐字节等价） | 16,923,827 | `5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91` |
| `bin/recognition.onnx` | ArcFace w600k_r50（insightface 原名 `w600k_r50.onnx`，两者逐字节等价） | 174,383,860 | `4c06341c33c2ca1f86781dab0e829f88ad5b64be9fba56e56bc9ebdefc619e43` |

- 模型来源（即 `D:\lightalbum_ai\face_server.py:4` 记录的来源）：
  <https://hf-mirror.com/immich-app/buffalo_l> —— `detection/model.onnx` → SCRFD-10GF，
  `recognition/model.onnx` → ArcFace w600k_r50。随包改名与 `face_server.py` 的
  `FaceEngine` 读取名一致（`detection.onnx` / `recognition.onnx`）。
- HuggingFace 官方 API `cardData`（2026-10-15 取回）：`license: other`、
  `license_name: insightface`、
  `license_link: https://github.com/deepinsight/insightface/tree/master/python-package#license`。
- 上游该 License 节原文（`https://raw.githubusercontent.com/deepinsight/insightface/master/python-package/README.md`，
  HTTP 200，2026-10-15 取回），逐字引用：

> The library code is released under the **MIT License**, for academic and commercial use.
> **The pretrained models provided with this library are for non-commercial research only**,
> whether downloaded automatically or manually.

即：**库代码 MIT（学术与商用皆可）／随库提供的预训练权重仅非商业研究用途
（non-commercial research only）**。

⚠ 发布态约束（R-24，落地位置 = R-40）：LightAlbum 第四轮把这两支权重随包分发，其上游条款是
**仅非商业研究用途**。本 NOTICE **不构成**、也**不得被引用为**商用授权；措辞不得软化为
"可商用"。CHANGELOG 与发行文档须与本文件逐字一致（T11/T12 引用点）。本目录 `LICENSE`
是同一条款的落盘记录（上游无独立 LICENSE blob，故为逐字引用件而非许可全文拷贝）。

## 2. ONNX Runtime 1.19.2 — MIT（运行时依赖）

- 项目：Microsoft ONNX Runtime — <https://github.com/microsoft/onnxruntime>
- 随包文件：`bin/onnxruntime.dll`，v1.19.2 win-x64，11,234,848 B，
  SHA-256 `14119125df2dcf9ff3e083afdba5fcc4b09b4186d8762404eb7b1fbccde3fcf2`
  ——与 `native/3rdparty/lama/bin/onnxruntime.dll` 是**同一钉死副本**（实测同 SHA，
  即官方 zip 那份）；`lama/bin`（既有）+ `face/bin` + `clip/bin` 三处各放一份，进程 cwd 隔离。
- 发行包 URL（口径同 `fetch_lama.ps1`）：
  <https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-win-x64-1.19.2.zip>
- 用途：`faceinfer.exe` 加载并推理 `detection.onnx` / `recognition.onnx`（ORT C API）。
- 许可：**MIT License**。许可文本取自解包目录
  `D:\la-tools\onnxruntime-win-x64-1.19.2\LICENSE`（1,094 B，
  SHA-256 `c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674`）的**落盘原文**
  拷贝（未手打、未改写；仅把源文件的 CRLF 行尾归一为本仓库统一的 LF，文本逐字符相同——
  复核口径：抽出下方代码块与源文件做行尾归一后全等比较，本轮实测为真），如下：

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

分发要求：随 `faceinfer.exe` 分发的 `onnxruntime.dll` 属该库的对象代码形式，上述版权与
许可声明须随发行包文档一并携带（由 T10 的 release.ps1 装配腿负责）。

## 3. Qt 运行时（faceinfer.exe 的运行期依赖，仅声明关系）

`faceinfer.exe` 自 T2 起链最小 Qt6Core/Qt6Gui 并以 `QImageReader` 读盘（spec §3 钉死口径），
故其发布态需 `Qt6Core.dll` / `Qt6Gui.dll` + imageformats 插件（windeployqt 口径，装配归 T10）。
Qt 为 LGPL-3.0（含例外条款）/ GPL-2.0 双许可；本轮**不修改、不重分发 Qt 源码**，仅以动态
链接形式随包携带官方构建产物，归属见仓库既有 `docs/THIRD_PARTY_NOTICE.md`。本节只声明关系，
不替 Qt 之外的内容背书许可。

## 4. `faceinfer.exe` 自身（本仓自建，非第三方）

- 源码在 `faceinfer/`（独立 CMake 工程，MinGW `-static` + `ort_mingw_shim.h`，模板 = `inpaint/`）；
  本仓自有代码，许可随本仓库。
- 构建产物**不入库**，由 `native/fetch_ai.ps1` 从 `faceinfer/build/`（或 `-FaceInferDir` 指定目录）
  拷入 `bin/`；拷贝前后各算一次 SHA-256 并断言相等（就地自检拷贝完整性）。
  exe 是重建产物，**故意不钉 SHA-256**（R-109：钉死会让每次重建打红），缺失时 fetch 直接 throw
  并给出可复现构建命令（绝不静默跳过，判例 `fetch_lama.ps1:231`）。

## 5. fetch 校验契约（本文件的机械断言点）

`native/fetch_ai.ps1` 对本目录硬检：`LICENSE` 非空、`THIRD_PARTY-NOTICE.md` 非空、
必现条目 `non-commercial research` / `MIT` / `onnxruntime` 各命中 ≥1（`Select-String`），
以及第 1/2 节全部 SHA-256 钉死值。`-SelfTest` 在**临时沙箱**里注入反例证明断言会咬：
注坏 SHA、删 `LICENSE`、删 `THIRD_PARTY-NOTICE.md`、删必现条目行、`-LocalSource` 缺源文件
（`detection.onnx` 与 `det_10g.onnx` 两名皆缺）、缺 `faceinfer.exe` 构建产物。
真实的 `native/3rdparty/**` 不会被 `-SelfTest` 破坏（R-110）。

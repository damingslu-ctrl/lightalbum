# THIRD-PARTY NOTICES — inpaint/

本目录产物 `inpaint.exe` 的构建期第三方依赖声明。运行时不下载、不包含任何其他第三方内容
（零云端）。模型权重（lama.onnx）的许可与随包 LICENSE 由 `tools/fetch_lama.ps1`
（Task 7）统一获取与校验，不在本文件范围。

## ONNX Runtime（构建期链接依赖）

- 项目：Microsoft ONNX Runtime — <https://github.com/microsoft/onnxruntime>
- 使用版本：v1.19.2（Windows x64 C/C++ API 发行包，见 BUILD.md §1）
- 用途：`inpaint.exe` 加载并推理 lama.onnx（LaMa 图像修复模型）
- 许可：MIT License

```
MIT License

Copyright (c) Microsoft Corporation.

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

分发要求：随 `inpaint.exe` 分发的 `onnxruntime.dll` 属该库的对象代码形式；上述版权与
许可声明须随发行包文档一并携带（Task 8 release.ps1 的许可证暂存步骤负责打包此文件）。

## LaMa 模型（运行时数据依赖，仅声明关系）

- 上游代码：advimman/lama（LaMa: Resolution-robust Large Mask Inpainting with
  Convolutions, WACV 2022），其仓库代码为 MIT 许可；本仓库不编译、不链接该代码。
- 运行时权重：Carve/LaMa-ONNX 的 `lama_fp32.onnx`（重命名 `lama.onnx`），为上述模型的
  ONNX 导出。**权重许可：Apache-2.0**（HF 模型卡声明 `license: apache-2.0`；仓库内并无
  LICENSE blob，故许可文本取自导出方官方 GitHub 源 `Carve-Photos/lama`），由
  fetch_lama.ps1 硬校验后落盘为 `3rdparty/lama/LICENSE`（`bin/` 上一级），发行打包时
  随附（契约 C9）。
- 构建者义务（**2026-10-11 已履行**）：SHA-256 已以实测钉死
  `1FAEF530…EB68D6`（208,044,816 B）；已人工核对 Apache-2.0 再分发条款——允许随
  LightAlbum 发行包再分发，条件为随附许可文本并保留归属，由 C9 与 fetch 落盘纪律
  满足；无需改为用户自取模式。本 NOTICE 不替模型权重之外的任何内容背书许可。

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
  ONNX 导出。**权重文件自身的许可证以 HuggingFace 仓库内 LICENSE 文件为准**，由
  Task 7 的 fetch_lama.ps1 强制与模型同目录落盘，Task 8 release 打包时随附。
- 构建者义务：首次定版 SHA-256 时（fetch_lama.ps1 fail-closed pin），人工核对 HF 仓库页
  声明的权重许可是否允许随 LightAlbum 发行包再分发；若不允许，改为用户自取模式并更新
  release 打包口径。本 NOTICE 不替模型权重背书许可。

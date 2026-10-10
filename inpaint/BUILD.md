# inpaint.exe 构建指南（ONNX Runtime，MSVC 或 MinGW）

独立于 `native/`（MinGW Qt 父进程）的**发布用**消除执行器：读 TASK 帧 → LaMa ONNX 推理 →
写 OK/ERR 帧。协议即契约（铁律 20），帧字节与 `native/src/core/EraseProto.h`、
`native/tools/inpaint_stub.cpp` 完全对齐。ORT 是稳定 C ABI，**MSVC 与 MinGW g++ 都能构建**
（2026-10-11 本仓库 QA 主机即以 MinGW 完成构建 + LA_ERASE_MODEL_QA 首绿，见 §4）。
真 exe 就位且同目录有 lama.onnx 时，`EraseRunner::findExe` 候选 1 会命中它——**跑桩门
（LA_ERASE_QA）或全量套件前必须移走 build 树里的真 exe 三件套**，否则桩的时序钩子被劫持。

## 0. 前置一览

| 依赖 | 版本钉 | 来源 | 许可 |
|---|---|---|---|
| CMake ≥ 3.21 + 编译器（二选一：VS2022 MSVC v143，或 Qt 自带 MinGW g++ 13.1） | 任意近期版 | microsoft.com / qt.io | 免费社区版可用 |
| ONNX Runtime C/C++ win-x64 | **v1.19.2**（zip 包） | github.com/microsoft/onnxruntime/releases | MIT（见 THIRD_PARTY-NOTICE.md） |
| lama.onnx（LaMa 推理权重 ONNX 导出） | Carve/LaMa-ONNX 的 `lama_fp32.onnx`，本地重命名 `lama.onnx`（实测 208,044,816B≈198MiB） | HuggingFace，由 Task 7 `tools/fetch_lama.ps1` 统一下发+校验 | 见 §2 与 NOTICE |

零云端铁律：以上全部由**人**在构建期下载核对；`inpaint.exe` 运行期不访问任何网络。

## 1. ONNX Runtime 下载与解包

1. 打开 <https://github.com/microsoft/onnxruntime/releases/tag/v1.19.2>
2. 下载 `onnxruntime-win-x64-1.19.2.zip`（C/C++ API 构建，非 GPU 包即可；CPU provider 足够）。
3. 解包到固定路径，例如 `C:\dev\onnxruntime-win-x64-1.19.2`。目录应含：
   - `include\onnxruntime_cxx_api.h`
   - `lib\onnxruntime.lib`（MSVC 导入库）+ `lib\onnxruntime.dll`（+ `onnxruntime_providers_shared.dll`）。
   ⚠️ 该 zip **不含任何 CMake config 文件**（v1.19.2 实测目录只有 `include/` 与 `lib/`）——
   `find_package(onnxruntime CONFIG)` 走不通；CMakeLists 已改为按 `ONNXRUNTIME_ROOT` 直接引用
   头文件与 lib/dll（旧版对 config 包的引用是纸面假设，2026-10-11 修正）。
4. 版本若升级（1.19.x 之内可接受），同步更新本表与 Task 7 fetch 脚本的 ORT dll 版本，保持一致。

## 2. lama.onnx 就位

正式发布以 `tools/fetch_lama.ps1`（Task 7）为**唯一权威**下载与 SHA-256 校验来源
（fail-closed：脚本内 `$expectedSha` 在人工贴入真实 SHA 前拒绝落盘）。手动核对口径：

- 源：<https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx>
- 本地文件名固定为 `lama.onnx`，与 `inpaint.exe` **同目录**部署。
- 计算并记录 SHA-256：`certutil -hashfile lama.onnx SHA256`，与 fetch_lama.ps1 的 pin 一致。
- The HF repo Carve/LaMa-ONNX ships **no LICENSE file**（2026-10-11 API 树核对；其 card 元数据
  声明 license:apache-2.0）。Apache-2.0 正文取自导出方仓库 `Carve-Photos/lama` 的 LICENSE
  （fetch_lama.ps1 `$urlLicense` 已指向 raw 地址）。Also lands at
  `native\3rdparty\lama\LICENSE` (one level ABOVE `bin\`, per the libraw/ffmpeg precedent;
  shared contract of fetch_lama.ps1 and release.ps1 block 3.67 -- NOT inside `bin\`).

运行时路径解析：父端 `EraseRunner` 把子进程 cwd 设为 exe 所在目录，代码里用相对路径
`L"lama.onnx"` 打开——即 `<exe目录>\lama.onnx`。

## 3. 模型张量契约核对表（换模型时必做；2026-10-11 已对钉死模型实测核对）

代码在 session 建立后**运行时查询全部输入/输出节点名**（`GetInputNameAllocated` 等），
不硬编码名字；对钉死的 Carve `lama_fp32.onnx` 实测布局为**双输入**（早期文档假设的单张
`(1,5,H,W)` 五通道输入是另一派 SAIC 导出的惯例，对本模型是错的——首喂即被 ORT 以
`Got: 5 Expected: 3` 拒绝，2026-10-11 契约修正）：

- [x] 输入 0（实测名 `image`）：`(batch,3,mh,mw)` float32，RGB，值 **÷255 归一 [0,1]**。
- [x] 输入 1（实测名 `mask`）：`(batch,1,mh,mw)` float32，白=1=待修复区。
- [x] 空间轴 **固定**（本导出 512×512，batch 动态）。执行器因此对任意尺寸帧做
      **确定性半像素 bilinear 往返缩放**（缩到模型尺寸推理、缩回原生分辨率合成），
      不再要求动态轴导出；换 512 以外固定尺寸的模型无需改代码（运行时读取 mh/mw）。
- [x] 输出（实测名 `output`）：`(batch,3,mh,mw)` float32，**值域 0..255（非 [0,1]！）**。
      归一化方向以官方样例对复现验证（image.jpg+mask.png → output_onnx_fp32.png，
      本布局合成 MAE≈0.015；错一个归一化因子 MAE≥0.28，脚本见台账）。若你的导出输出
      [0,1] 而本代码按 0..255 处理，结果会几乎全黑——用样例图目测即可发现。
- [x] 首跑看 stderr：打印 `model loaded, image="..." mask="..." output="..." fixed WxH`。
      输入数≠2 或通道布局不符 → initModel 诚实返回 false（READY 0 + 每 TASK 回 ERR），
      不会带错误布局硬跑。

## 4. 构建步骤

**MinGW（本仓库 QA 主机实测路径，无需 MSVC）：**

```bat
cmake -S inpaint -B inpaint\build -G "MinGW Makefiles" ^
  -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" ^
  -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" ^
  -DONNXRUNTIME_ROOT="D:/onnxruntime-win-x64-1.19.2"
cmake --build inpaint\build
```

MinGW 分支要点（CMakeLists 已内置）：
- `-include ort_mingw_shim.h`：ORT 头在 `_WIN32` 下用 MSVC 方言写 `_stdcall`/SAL 注解，
  MinGW 的 specstrings.h 缺这些 token，用 shim 兜平（纯空宏，C ABI 语义不变）。
- `-static`：否则 g++ 默认动态链 `libgcc_s_seh-1.dll`/`libstdc++-6.dll`/`libwinpthread-1.dll`，
  终端用户机器没有 Qt MinGW 运行时→发布包起不来。加 `-static` 后 `objdump -p` 实测仅剩
  KERNEL32/msvcrt/onnxruntime 三项导入（2026-10-11）。

**MSVC：**

```bat
cd /d <repo>\inpaint
cmake -S . -B build -A x64 ^
  -DONNXRUNTIME_ROOT="C:/dev/onnxruntime-win-x64-1.19.2" ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

说明：
- `-A x64`：VS 生成器默认 win32，必须显式 x64 以匹配 ORT win-x64 包。
- CMakeLists 按 `ONNXRUNTIME_ROOT` 直接引用 include/ 与 lib/onnxruntime.lib|.dll（zip 无
  CMake config 包，`find_package CONFIG` 走不通，见 §1 警告）。
- 产物：`inpaint.exe` + 同目录自动拷入的 `onnxruntime.dll`
  （及 `onnxruntime_providers_shared.dll`，若包内含）。
- 源文件含 UTF-8 中文注释：MSVC 必须 `/utf-8`（target 已加），请勿删除该编译选项。

## 5. 部署布局（与 Task 8 release.ps1 对齐）

```
<appdir>\3rdparty\lama\bin\
    inpaint.exe
    onnxruntime.dll                     (及包内其它 onnxruntime*.dll)
    lama.onnx                           (fetch_lama.ps1 落盘)

<appdir>\3rdparty\lama\LICENSE          (model license, one level ABOVE bin, not inside bin\)
```

父端 `EraseRunner::findExe()` 第一候选即 `<appdir>/3rdparty/lama/bin/inpaint.exe`；
开发树没有它时回退 `inpaint_stub.exe` 跑 QA。

## 6. 运行时行为契约（调试时必读）

- **stdout 是二进制帧通道**：程序启动即 `_setmode(_O_BINARY)`（stdin/stdout），
  这是正确性必需（CRT 文本模式会把 RGB 体内的 0x0A 扩成 0x0D0A，帧长崩坏）。
- **所有日志（本程序 + ORT）只走 stderr**。ORT 环境级别设为 `ORT_LOGGING_LEVEL_ERROR`；
  若你把级别调高，确认它仍写 stderr——任何写到 stdout 的日志都会污染协议流。
- 启动握手：先建 session 再打印 `READY 1`；**模型缺失/加载失败不退出**，打印 `READY 0`
  并保持循环，之后每个 TASK 回 `ERR <id> no model`。父端 `ensureStarted` 只要求首行
  以 `READY` 开头，`applyResult` 把任务标失败——无崩溃、无挂起、可换模型后由父端自动重启恢复。
- 父端预算（2026-10-11 真机校准）：`waitForStarted` **5000ms**、首帧 READY `waitForReadyRead`
  **30000ms**——真模型冷启动（198MiB session 加载+图优化）本机实测 READY 于 8.1~8.5s
  到达（`ORT_ENABLE_ALL`；降 `ORT_ENABLE_BASIC` 也只到 6.7s），旧的 5000ms 拍脑袋预算
  正是 LA_ERASE_MODEL_QA 首跑的失败根因（executor unavailable）。2.36.0 异步化后该等待
  在 owner(worker) 线程，不阻塞 GUI。单任务应答预算仍 **20000ms**：推理耗时与输入尺寸
  无关（固定 512 往返缩放），本机实测 3.7~3.9s/帧。
- 帧错误路径：TASK 头解析失败/短读 → 直接退出循环返回 0（父端 QProcess 侦测结束并重启）；
  nb/nm 与 w·h 不符 → `ERR <id> BADSIZE`；推理异常 → `ERR <id> INFER`。OK 响应恒 `no == w*h*3`。

## 7. 构建者自检清单

- [x] `cmake` 配置 + 编译零 error —— MinGW 路径已在 QA 主机实测（2026-10-11）；
      MSVC 路径仍由构建者自行执行（本机无 MSVC，保持诚实记录）。
- [ ] ORT C++ API 面核对：本文件用到的调用为 `Env(ORT_LOGGING_LEVEL_ERROR)`、
      `SessionOptions::SetIntraOpNumThreads/SetGraphOptimizationLevel`、
      `Session(env, L"lama.onnx", opt)`（Windows 宽字符路径构造）、
      `GetInputNameAllocated/GetOutputNameAllocated + AllocatorWithDefaultOptions`（≥1.11 才有；
      若编译报此类名不存在，说明包版本对不上 §1，换 1.19.2；极老版本才需退回
      `GetInputName(0, allocator)` + 手动 `ReleaseString`）、
      `MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)`、
      `Value::CreateTensor<float>`、6 参 `Session::Run`（返回 `std::vector<Ort::Value>` 的重载）、
      `Value::GetTensorData<float>()`、`GetTensorTypeAndShapeInfo().GetElementCount()`。
      任何一个对不上 → 以你包内 `onnxruntime_cxx_api.h` 为准修正，勿改协议侧。
- [ ] Netron §3 五项核对全过。
- [ ] `inpaint.exe` 与 `lama.onnx`、`onnxruntime.dll` 同目录后，用 LightAlbum 实际消除一次：
      mask 外像素与底图逐字节相同（守恒），mask 内无接缝感。
- [ ] 删除 lama.onnx 再启动：父端不崩、任务显示错误字符串（含 `no model`），放回模型后自动恢复。
- [ ] 跑 Task 9 的 `LA_ERASE_MODEL_QA` 门（`LA_LAMA_MODEL=1` opt-in）全绿。

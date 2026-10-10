# inpaint.exe 构建指南（MSVC + ONNX Runtime）

独立于 `native/`（MinGW Qt 父进程）的**发布用**消除执行器：读 TASK 帧 → LaMa ONNX 推理 →
写 OK/ERR 帧。协议即契约（铁律 20），帧字节与 `native/src/core/EraseProto.h`、
`native/tools/inpaint_stub.cpp` 完全对齐。本目录只在 Windows + MSVC 下构建；
本仓库 CI 与 QA 主机（无 MSVC / 无 ORT）**不编译它**——QA 门绿靠 MinGW stub，真实 exe
由构建者按下述步骤本地构建后用 Task 9 的 LA_ERASE_MODEL_QA 门验证。

## 0. 前置一览

| 依赖 | 版本钉 | 来源 | 许可 |
|---|---|---|---|
| Visual Studio 2022 (MSVC v143) + CMake ≥ 3.21 | 任意近期版 | microsoft.com | 免费社区版可用 |
| ONNX Runtime C/C++ win-x64 | **v1.19.2**（zip 包） | github.com/microsoft/onnxruntime/releases | MIT（见 THIRD_PARTY-NOTICE.md） |
| lama.onnx（LaMa 推理权重 ONNX 导出） | Carve/LaMa-ONNX 的 `lama_fp32.onnx`，本地重命名 `lama.onnx`（约 191MB） | HuggingFace，由 Task 7 `tools/fetch_lama.ps1` 统一下发+校验 | 见 §3 与 NOTICE |

零云端铁律：以上全部由**人**在构建期下载核对；`inpaint.exe` 运行期不访问任何网络。

## 1. ONNX Runtime 下载与解包

1. 打开 <https://github.com/microsoft/onnxruntime/releases/tag/v1.19.2>
2. 下载 `onnxruntime-win-x64-1.19.2.zip`（C/C++ API 构建，非 GPU 包即可；CPU provider 足够）。
3. 解包到固定路径，例如 `C:\dev\onnxruntime-win-x64-1.19.2`。目录应含：
   - `include\onnxruntime_cxx_api.h`
   - `lib\onnxruntime.lib` + `lib\onnxruntime.dll`（+ `onnxruntime_config.cmake` 等 config 包文件）
4. 版本若升级（1.19.x 之内可接受），同步更新本表与 Task 7 fetch 脚本的 ORT dll 版本，保持一致。

## 2. lama.onnx 就位

正式发布以 `tools/fetch_lama.ps1`（Task 7）为**唯一权威**下载与 SHA-256 校验来源
（fail-closed：脚本内 `$expectedSha` 在人工贴入真实 SHA 前拒绝落盘）。手动核对口径：

- 源：<https://huggingface.co/Carve/LaMa-ONNX/resolve/main/lama_fp32.onnx>
- 本地文件名固定为 `lama.onnx`，与 `inpaint.exe` **同目录**部署。
- 计算并记录 SHA-256：`certutil -hashfile lama.onnx SHA256`，与 fetch_lama.ps1 的 pin 一致。
- Also fetch `LICENSE` from the same repo; it lands at `native\3rdparty\lama\LICENSE` (one level ABOVE `bin\`, per the libraw/ffmpeg precedent; shared contract of fetch_lama.ps1 and release.ps1 block 3.67 -- NOT inside `bin\`).

运行时路径解析：父端 `EraseRunner` 把子进程 cwd 设为 exe 所在目录，代码里用相对路径
`L"lama.onnx"` 打开——即 `<exe目录>\lama.onnx`。

## 3. Netron / 节点名核对表（构建前必做）

代码在 session 建立后**查询第 0 输入/第 0 输出的实际节点名**
（`GetInputNameAllocated`/`GetOutputNameAllocated`），因此不依赖导出命名，任意 lama.onnx
均可用；`inpaint.cpp` 顶部常量 `IN_NAME_DOC="input"`、`OUT_NAME_DOC="output"` 仅是
SAIC-MLP/LaMa-ONNX 导出惯例的**文档**。但张量**布局**是硬契约，第一次对某个 lama.onnx
构建/运行前，用 Netron（<https://netron.app> 本地打开 lama.onnx）或
`python -c "import onnx; m=onnx.load('lama.onnx'); print([(i.name,[d.dim_value or d.dim_param for d in i.type.tensor_type.shape.dim]) for i in m.graph.input])"`
逐项核对：

- [ ] 唯一输入：名字（惯例 `input`），shape `(1,5,H,W)` 或 `(batch,5,height,width)`，dtype **float32**。
- [ ] H、W 为**动态轴**（symbolic，如 `height`/`width`）。⚠️ 若导出钉死 512×512 固定分辨率，
      本执行器的任意尺寸喂入会由 ORT 在 `Run` 抛异常 → 父端收到 `ERR <id> INFER`，任务诚实失败。
      此时必须换动态轴导出或改代码做分块/缩放——核对后在 Task 9 门里用非 512 尺寸图片实测。
- [ ] 通道语义（本实现约定，若与你的导出不同须改 `runInpaint` 的组装段并同步此处）：
      ch0..2 = RGB÷255，ch3 = mask÷255（255=消除区），ch4 = 全 1。
- [ ] 唯一输出：名字（惯例 `output`），shape `(1,3,H,W)`，dtype **float32**，值域约定 [0,1]
      （代码做 ×255 + clamp，若模型输出本身是 0..255 则结果会饱和成白——用样例图目测验证）。
- [ ] 首跑看 stderr：代码会打印 `model loaded, input="..." output="..."`，与实际名一致即通过。

## 4. 构建步骤

```bat
cd /d <repo>\inpaint
cmake -S . -B build -A x64 ^
  -DONRUNTIME_ROOT="C:/dev/onnxruntime-win-x64-1.19.2" ^
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

说明：
- `-A x64`：VS 生成器默认 win32，必须显式 x64 以匹配 ORT win-x64 包。
- `find_package(onnxruntime CONFIG REQUIRED)` 命中 ORT zip 包内的 config 文件，得到
  `onnxruntime::onnxruntime` 导入目标；若你改用系统安装版 ORT，把安装前缀放进
  `CMAKE_PREFIX_PATH` 亦可（`ONRUNTIME_ROOT` 只是便捷别名，同时用于 POST_BUILD 拷 dll）。
- 产物：`build\Release\inpaint.exe` + 同目录自动拷入的 `onnxruntime.dll`
  （及 `onnxruntime_providers_shared.dll`，若包内含）。
- 源文件含 UTF-8 中文注释，target 已加 `/utf-8`；请勿删除该编译选项。

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
- 父端预算：`waitForStarted/ReadyRead` 各 **5000ms**——冷盘加载 ~191MB ONNX + 图优化
  若超 5s 会导致父端首启失败（下一任务会重试）。若实测超时，可把 `ORT_ENABLE_ALL` 降为
  `ORT_ENABLE_BASIC` 换启动速度（推理性能略降），并在 Task 9 门记录实测。
- 帧错误路径：TASK 头解析失败/短读 → 直接退出循环返回 0（父端 QProcess 侦测结束并重启）；
  nb/nm 与 w·h 不符 → `ERR <id> BADSIZE`；推理异常 → `ERR <id> INFER`。OK 响应恒 `no == w*h*3`。

## 7. 构建者自检清单（首次出包）

- [ ] `cmake` 配置 + 编译零 error（本仓库 QA 主机无 MSVC，编译验证**由构建者执行**）。
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

# 类 Lightroom 编辑器选型调研（LightAlbum 内置编辑器替换/增强）

> 调研日期：2026-09-24 · 背景：Lumen（core/retouch.py，纯 Pillow）缺 RAW 支持、无非破坏参数栈、无本地蒙版。目标是在 GitHub 找类 LR 开源方案，评估与 LightAlbum（PySide6 + Engine-Hub）的集成路径。

## 一、候选总览（GitHub API 实测数据）

| 项目 | Stars | 许可证 | 语言 | 最近提交 | 版本 | 定位 |
|---|---|---|---|---|---|---|
| [darktable](https://github.com/darktable-org/darktable) | **13,124** | GPL-3.0 | C | **2026-09-24**（当天） | 5.6.1 | 最完整的开源「Lightroom Classic」：lighttable 目录库 + darkroom 非破坏 RAW 开发 + map/tether/print，OpenCL GPU 加速，约 80 个处理模块 |
| [RawTherapee](https://github.com/RawTherapee/RawTherapee) | 4,182 | GPL-3.0 | C++ | 2026-09-21 | 5.12+ | 纯 RAW 开发引擎（不做目录管理），去马赛克算法最强（AMaZE/RCD/LMMSE/DCB），小波降噪、高光恢复一流 |
| [ART (Another RawTherapee)](https://github.com/artraweditor/ART) | 478 | GPL-3.0 | C++ | 2026-09-23 | 1.2x+ | RT 的精简 fork：界面接近 LR、内置**本地调整蒙版**（笔刷/渐变/参数/色彩/亮度）、文件夹式工作流（无需导入目录库），实验性 Vulkan GPU 管线 |
| [PhotoDemon](https://github.com/tannerhelland/PhotoDemon) | 2,358 | **BSD**（宽松） | VB6 | 2026-08-18 | 8.x | Windows 专用**便携**照片编辑器（22MB 单目录、免安装），图层/PSD/文本层/宏录制/批处理，200+ 工具；**32 位**；明确 No-AI 政策 |
| [rawpy](https://github.com/letmaik/rawpy) | — | **MIT**（+LibRaw LGPL-2.1） | Python/C | 2026-08-30 | 0.27 | LibRaw 的 Python 封装：RAW 解码 + demosaic + 后处理一行代码，**Windows wheel 现成**，是嵌入 RAW 能力的正确答案 |
| LightZone | ~小 | BSD-3 | Java | 增长乏力 | — | Zone System 风格 RAW 编辑器，活跃度已落后，不建议押注 |

> 参考：digiKam（GPL，偏目录管理弱编辑）、Filmulator（胶片模拟小众）不在第一梯队。

## 二、关键判断：不能「直接嵌入」，但有三条现实集成路径

darktable/RawTherapee/ART 是 **GTK 桌面程序**，PhotoDemon 是 **VB6/Win32**——都没有可作为控件嵌入 PySide6/QtWebEngine 的 SDK。所以「替换内置编辑器」的可落地形态是：

### 路径 A：外部编辑器跳转（推荐第一步，改动最小）
照搬 LR 的「Edit in External Editor」模式：
1. 客户端选中照片 → 复制一份到 `.external-edit/` 工作目录（RAW 或 16-bit TIFF）；
2. 以子进程启动外部编辑器打开该副本；
3. 客户端监听该目录的导出产物（QFileSystemWatcher）→ 自动重新入库、建立「版本组」。
- **优点**：立刻获得完整 LR 级暗房；零许可证问题（独立进程，GPL 不传染）；不动 Lumen 一行代码。
- **落点**：Engine-Hub 新增 `ExternalEditorEngine`（进程管理 + 目录监听），与 `AiEngine` 同型。

### 路径 B：rawpy = DevelopEngine（补齐 RAW 短板，纯嵌入）
`pip install rawpy`（MIT 许可、Windows wheel 现成、依赖 numpy）：
```python
import rawpy
with rawpy.imread(path) as raw:
    rgb = raw.postprocess(use_camera_wb=True, output_bps=16)  # 16-bit RGB
# → 写 16-bit TIFF → 交给 Lumen 做影调/预设
```
- RAW 解码/白平衡/demosaic/降噪交给 LibRaw，Lumen 继续负责 RGB 影调——**职责边界完全符合 ADR-0012**（重计算 → 原生引擎）。
- LibRaw 双许可 LGPL-2.1 / CDDL，作为动态库调用不传染 MIT/闭源侧。

### 路径 C：darktable-cli / art-cli 无头渲染（未来的「Develop 模块」内核）
- `darktable-cli <input> [<xmp>] <output>` 与 `art-cli` 支持完全无头出图，编辑参数存于 **.xmp 侧车文件**（非破坏参数栈的现成格式）；
- 可把「预设」做成参数文件，批量渲染 = LR 的 Develop + Batch Export；
- GPL 程序以独立进程调用，无许可证传染。

## 三、选型结论

| 需求场景 | 推荐 | 理由 |
|---|---|---|
| 「我要 LR 那种编辑体验」 | **ART** | 界面最接近 LR、自带本地蒙版、免导入、单文件侧车参数、周更；外部跳转首选 |
| 「我要最全的摄影工作流」 | darktable | 13k★ 社区最大、模块最全、GPU 加速；但学习曲线陡，且它自带目录库会与 `library.db` 职责重叠（只用它的 darkroom 也行） |
| 「追求 RAW 极致画质/科研级控制」 | RawTherapee | demosaic 与降噪最强；纯出图不管理库 |
| 「可随客户端再分发的像素编辑器」 | **PhotoDemon** | 唯一 **BSD 宽松许可**，便携免安装，适合打包进 LightAlbum 目录作为捆绑外部编辑器（注意 32 位/2GB 图像上限） |
| 「把 RAW 能力嵌进客户端本体」 | **rawpy** | MIT、wheel 现成、Engine-Hub 天然适配 |

**建议组合**：路径 B（rawpy DevelopEngine）打底 + 路径 A（ART 或 PhotoDemon 外部跳转）给完整暗房体验；darktable-cli 留作后续批量渲染选项。

## 四、风险与注意事项
1. **许可证**：GPL-3.0（darktable/RT/ART）以独立进程调用不传染；若想把它们的**代码**编进 LightAlbum 本体则会被传染——不要这么做。
2. PhotoDemon 是 32 位，单图上限约 2GB；且作者明确拒绝 AI/LLM 贡献——集成时只当黑盒。
3. rawpy 的 Windows wheel 不含 LCMS 色彩引擎（macOS/Linux 有）；强色彩管理需求走路径 A/C 更稳。
4. darktable 官方自称 "not a free Lightroom replacement"（scene-referred 流程与 LR 心智模型不同），用户预期要管理。

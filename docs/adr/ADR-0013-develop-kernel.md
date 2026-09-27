# ADR-0013：开发内核（DevelopEngine）与外部编辑器跳转（ExternalEditorEngine）

日期：2026-09-25 · 状态：已采纳（1.9.1 落地） · 前置：ADR-0012 Engine-Hub

## 背景

用户需求：寻找类 Lightroom 的开源软件替换 LightAlbum 内置编辑器（Lumen）。
选型调研（`docs/类LR编辑器选型调研.md`，GitHub API 实测）结论：
- darktable / RawTherapee / ART 均为 GTK 桌面程序，PhotoDemon 为 VB6/Win32，
  **均无可嵌入 PySide6 的 SDK**，不能直接当内置控件；
- 推荐组合：**rawpy 打底（嵌入 RAW 开发内核）+ 外部编辑器跳转（ART/PhotoDemon）**。

## 决策

### D1：rawpy 作为「开发内核」嵌入 —— DevelopEngine
- `core/engines/develop_engine.py`：LibRaw（rawpy 0.27.1，MIT + LibRaw LGPL-2.1）
  负责 RAW 解码 / 相机白平衡 / demosaic / FBDD 降噪；Lumen 继续负责 RGB 影调。
- 契约：`task_probe()`（可用性/版本）+ `task_develop(src, out_dir, bit_depth, …)`。
- 输出策略：16-bit TIFF（有 tifffile 则真 16bit，否则**诚实降级** 8bit 并在
  summary 注明）；8bit JPEG/PNG 供快速预览。
- `vram_weight=0`（CPU 管线，不参与 6GB 显存分时）；`max_jobs=2`。
- rawpy 缺失 / 非 RAW 输入 → Hub 层 `JobState.FAILED`，绝不阻断启动。

### D2：外部编辑器跳转 —— ExternalEditorEngine（LR「Edit in External Editor」模式）
- `core/engines/external_editor_engine.py`：
  - `task_plan`（纯路径规划：`<src>/_external_edit/<stem>/` + `output/`）；
  - `task_launch`（复制全新工作副本 → **独立进程**启动编辑器，Windows 用
    DETACHED_PROCESS|NEW_PROCESS_GROUP，不随客户端退出而杀）；
  - `task_scan`（mtime 轮询产物，供回灌入库；GUI 侧后续可换 QFileSystemWatcher）；
  - `task_set_editor` / `task_editor`（运行时配置；支持环境变量
    `LIGHTALBUM_EXT_EDITOR` 预配置）。
- 编辑器建议：**ART**（GPL-3.0，最像 LR）或 **PhotoDemon**（BSD，便携可捆绑）。

### D3：注册与打包
- `registry.build_default_hub` 始终注册两引擎（develop / extedit）；
- `engines/__init__.py` 显式 import，保证 PyInstaller 收集；
- venv 安装 rawpy 0.27.1 + numpy 2.5.3（pip 缓存放 D 盘）。

## 后果

正面：RAW 支持补齐（此前 Lumen 只能吃 RGB）；非破坏开发的参数载体（sidecar）
有了演进起点；外部编辑器零成本获得完整 LR 级暗房；GPL 以独立进程调用无传染。
负面：安装体积 +（numpy≈20MB + rawpy≈2MB）；16bit 输出依赖 tifffile（未装时降级）；
外部跳转的 GUI 入口（按钮/菜单/版本组回灌）本轮未做，属下一步。

## 备选方案对比（取舍）

| 方案 | 结论 |
|---|---|
| 把 darktable/ART 代码编进本体 | 否决：GPL 传染，且 GTK 与 Qt 技术栈冲突 |
| 自研 RAW 解码 | 否决：LibRaw 20 年相机格式积累不可复制 |
| darktable-cli/art-cli 无头渲染 | 保留为后续「批量 Develop」选项（sidecar=预设），本轮未做 |
| 仅做外部跳转不做内核 | 部分采纳：外部跳转做了，但 RAW 内核是客户端本体的能力短板，必须补 |

## 验证

- `test_enginehub.py` 34/34（新增 develop 9 项 / extedit 8 项：合成 uint16 编码、
  非 RAW 失败被 Hub 捕获、防御式 probe/launch、产物扫描 mtime 过滤、真子进程启动）；
- `run_all_tests.py` **ALL GREEN 9 套件**；版本 1.9.0 → **1.9.1**；
- 打包 `dist/LightAlbum-1.9.1-win64`，Analysis-00.toc 核验：
  `enginehub` + 8 个引擎子模块 + `rawpy/_rawpy.cp313-win_amd64.pyd/raw_r.dll/VCOMP140.DLL` + `numpy` 全部进包。

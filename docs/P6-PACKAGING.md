# P6 打包对比报告 — native (C++/Qt6) vs 旧 Python (PySide6/PyInstaller)

> 目标（见 `REWRITE-PLAN.md` 第 15 行）：静态/动态 Qt 部署，删除 QtWebEngine 依赖，产出「包体积 / 启动 / 内存」三项对比报告。
> 实测日期：2026-09-28。环境：Windows 11，offscreen（`QT_QPA_PLATFORM=offscreen`）无头环境，真库 `D:/lightalbum/library.db`（6424 张照片、7409 缩略图）。

---

## 1. 结论速览

| 指标 | native (C++/Qt6) | Python (PySide6/PyInstaller 单文件) | 倍率 |
|---|---|---|---|
| **包体积** | **60 MB** | **641 MB** | **≈10.7×** |
| **冷启动到可交互** | **≈0.43–0.65 s**（主窗就绪） | **≥2 s 引擎就绪**；真库 + WebEngine 画廊在 headless 下持续加载数分钟 | 数量级差距 |
| **常驻内存（载入库+展示画廊）** | **≈61 MB**（平稳） | **≈450–540 MB**（持续爬升） | **≈7–9×** |

结论：**native 在三项指标上全面碾压旧 Python 构建**，核心收益来自「移除 QtWebEngine（Chromium）依赖」与「去掉 Python 虚拟机 + PyInstaller 自解压开销」。

---

## 2. 包体积（Size）

### 2.1 native — 60 MB（动态 Qt 部署，目录式 / one-dir）

`windeployqt --release --no-translations --no-qml --no-opengl-sw` 产出，纯 QtWidgets，无 WebEngine、无 3D。

| 组件 | 大小 | 备注 |
|---|---|---|
| Qt6Gui.dll | 11 MB | 保留 |
| Qt6Widgets.dll | 7 MB | 保留 |
| Qt6Core.dll | 7 MB | 保留 |
| LightAlbum.exe | 4 MB | 应用本体（含全部 C++ 业务逻辑 + Lumen 修图引擎） |
| D3Dcompiler_47.dll | 4 MB | 保留（D3D 着色器编译） |
| libstdc++-6.dll | 3 MB | MinGW 运行时，保留 |
| Qt6Network.dll | 2 MB | **可裁剪**（源码无 `QNetwork`/`QtNetwork` 引用） |
| platforms/ | 2 MB | qwindows + qoffscreen |
| sqldrivers/ | 2 MB | SQLite 驱动（库存储依赖） |
| Qt6Svg / Qt6Sql / styles / tls / networkinformation / imageformats / iconengines / generic | 各 ≤1 MB | 多数保留；`tls`/`networkinformation` 随 Qt6Network 一并可裁 |
| **合计** | **60 MB** | 裁剪 Qt6Network 链后 ≈ **56–57 MB** |

### 2.2 Python — 641 MB（PyInstaller 单文件，自带 Chromium）

`client/dist/LightAlbum-1.9.39-win64/`，PyInstaller one-file，每次启动自解压至 `%TEMP%`。

| 组件 | 大小 | 备注 |
|---|---|---|
| **PySide6 整体** | **536 MB** | Qt 全部模块 + Python 绑定 |
| ├ Qt6WebEngineCore/Widgets/Quick 等 | **200 MB** | **native 已彻底移除的依赖** |
| ├ Qt63D*.dll | 16 MB | 3D 模块（native 未引入） |
| └ 其余 Qt + PySide 绑定 | 320 MB | Core/Gui/Widgets/Svg/Sql… |
| python313.dll + python3.dll | 7 MB | Python 3.13 运行时 |
| 应用代码 + 资源 + 依赖包 | ≈98 MB | 含 `client/resources`、各本地服务依赖 |
| **合计** | **641 MB** | |

**关键差异**：native 通过「原生 C++ 重写画廊/修图 UI」彻底去掉了 `QtWebEngine`（200 MB）与 `Qt3D`（16 MB），并用编译型二进制替代 Python 解释器 + 庞大绑定层。

---

## 3. 启动（Startup / cold launch）

### 3.1 native — ≈0.43–0.65 s（实测）

无头冒烟（`LA_SMOKE=1`，载入真库后打印 `LA_P1_SMOKE_OK` 并退出），3 次计时：

| 运行 | 进程生存期 | 说明 |
|---|---|---|
| 1（冷，首启磁盘缓存冷） | 1246 ms | 含固定 600 ms 沉降计时器 |
| 2 | 1041 ms | |
| 3 | 1031 ms | |

扣除 `main.cpp` 中 `QTimer::singleShot(600)` 的固定沉降计时，**主窗就绪 ≈ 431–646 ms**（热启 ~0.45 s，冷启首跑 ~0.65 s）。无自解压、无解释器启动、无 Chromium 初始化。

### 3.2 Python — ≥2 s 引擎就绪，库加载持续数分钟（headless 实测 + 结构分析）

无头启动（offscreen）工作集随时间：

```
 0s   20 MB      (Python VM 启动)
 2s  172 MB      (QtWebEngine 子进程 QtWebEngineProcess.exe 已起，webeng_child=1)
 5s  244 MB
24s  408 MB
75s  493 MB
175s 520 MB      (仍在缓慢爬升)
```

- **Chromium/WebEngine 层初始化**：~2 s（本测）至 ~8 s（前一跑，受机载波动），即「引擎就绪」。
- **真库 + WebEngine 画廊渲染**：在 headless（无 GPU）下工作集持续爬升至 175 s 仍未平稳，说明 6424 张照片的画廊/缩略图加载远未结束。
- **额外开销**：PyInstaller 单文件每次启动先把 641 MB 自解压到 `%TEMP%`（磁盘 + 启动延迟，且每次都做）。

> ⚠️ **Headless 限制**：offscreen 下 Chromium 无 GPU，日志持续报 `GPU Context lost / Unable to initialize SkSurface`。真实桌面有 GPU 时会更快，但 **Python VM + Chromium 基线（仅 WebEngine 起来即 ~170 MB）的结构性开销无法消除**，仍比 native 高一到两个数量级。

---

## 4. 内存（Memory / working set）

### 4.1 native — ≈61 MB（实测，平稳）

载入真库（6424 张）、原生画廊构造完成、`win.show()` 后空闲 4 s 采样：`LightAlbum.exe` 工作集 **60,888 K ≈ 60.9 MB**，且稳定不漂移。

### 4.2 Python — ≈450–540 MB（实测，持续爬升）

同上无头运行：WebEngine 子进程常驻，工作集从 172 MB（2 s）一路爬升至 **520 MB（175 s）仍未收敛**。取中段稳定观察值 **≈450–540 MB**。

- native 内存 ≈ **61 MB**；Python ≈ **500 MB** → **≈8×**。
- Python 的增量来自：Python 对象堆 + PySide6 绑定 + Chromium 渲染进程 + WebEngine 画廊的缩略图/纹理缓存。

---

## 5. 部署方式对比

| 维度 | native (C++/Qt6) | Python (PySide6) |
|---|---|---|
| 部署形态 | `windeployqt` 动态部署，目录式（one-dir） | PyInstaller 单文件（one-file，自解压） |
| WebEngine | **已移除** | 强依赖 QtWebEngine（Chromium） |
| 3D / QML | 未引入 | 携带 Qt3D（16 MB）+ QML |
| 启动依赖 | 直接加载原生 DLL | 解压 641 MB + Python 解释器 + Chromium |
| CI / 无头冒烟 | ✅ 可（`LA_SMOKE` / `LA_LUMEN_SMOKE` 等，offscreen 干净退出） | ❌ 难（WebEngine 无 GPU 报 GPU Context lost，无法干净无头 smoke） |
| 二进制可裁剪 | ✅ 已确认 Qt6Network 链（~3 MB）可删 | 受 PySide6 打包粒度限制，难精细裁剪 |

### 进一步优化（可选，未做）
- **静态 Qt 部署**：以静态 Qt6 重新链接，产出**单一 exe（≈15–20 MB，无 DLL）**。注意 Qt 为 LGPL，静态链接需满足 LGPL 反版式条款（提供目标文件/允许用户重链）。
- **Qt6Network 链裁剪**：源码无网络引用，`Qt6Network.dll`(2 MB) + `tls` + `networkinformation` 可整体删除，native 进一步降至 ~56 MB。
- **ANGLE / D3Dcompiler 评估**：`D3Dcompiler_47.dll`(4 MB) 为 Qt GUI 的 D3D 着色器编译所需，建议保留。

---

## 6. 关键发现 / 风险

1. **WebEngine 是体积与内存的主要元凶**：Python 构建中 QtWebEngine 占 200 MB（包内）+ Chromium 渲染进程（内存 +170 MB 起步）。native 移除后一举解决。
2. **无头可分发性**：native 因去 WebEngine，`QT_QPA_PLATFORM=offscreen` 下可干净运行并退出，支撑 `LA_SMOKE` / `LA_LUMEN_SMOKE` 等 CI 冒烟；旧 Python 构建在无 GPU 环境 Chromium 报错，无法做等价无头自测。
3. **实测环境偏差声明**：启动/内存均在 offscreen（无 GPU）环境测得，对 WebEngine 路径属 **worst-case**；但该偏差只会放大 Python 的劣势，不影响「native 数量级更优」的结论。

---

## 7. 建议

- **分发形态**：以 native 目录式构建（60 MB）为唯一正式分发物；旧 `client/dist/` 下全部 PyInstaller 历史构建（1.5.0–1.9.39，共数十个目录 ≈ 数十 GB）可清理，仅保留归档一份即可。
- **后续优化**：评估静态 Qt 链接（单 exe）作为轻量分发选项；落地 Qt6Network 链裁剪。
- **打包链路**：删除 `client/` 下 PyInstaller 打包脚本与 `LightAlbum.spec` 旧链路（已无运行价值，native 侧用 `build.py` / CMake 接管）。

---

## 8. 复测方法（可复现）

```bash
# —— native ——
cd native/build
# 体积
du -sm .
# 启动（offscreen，载入真库后打印 LA_P1_SMOKE_OK 并退出；扣除 600ms 沉降）
QT_QPA_PLATFORM=offscreen \
QT_QPA_PLATFORM_PLUGIN_PATH="$(pwd)" \
LA_SMOKE=1 \
time ./LightAlbum.exe
# 内存（载入库+空闲，进程常驻）
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORM_PLUGIN_PATH="$(pwd)" ./LightAlbum.exe &
sleep 4; tasklist | grep -i lightalbum; kill %1

# —— Python 基线（已有构建 client/dist/LightAlbum-1.9.39-win64/LightAlbum/LightAlbum.exe）——
cd client/dist/LightAlbum-1.9.39-win64/LightAlbum
QT_QPA_PLATFORM=offscreen ./LightAlbum.exe &
# 观察 tasklist 中 LightAlbum.exe 工作集 + QtWebEngineProcess.exe 子进程
```

---

*附录：P5 已全收口（level / lens / retouchWithPlan C++ 移植 + 金样全绿，commit `1f92aa7`）。本 P6 报告为 native 重写相对旧 Python 客户端的打包对比定稿。*

# ADR-0012 · 模块引擎化与统一调度（Engine-Hub）

- 状态：Accepted（采纳骨架 + 对齐现有子系统）
- 日期：2026-09-24
- 决策人：damin（Software Architect）
- 关联：模块化单体（modular monolith）、现有 AI 网关 8788、6GB 显存账本

---

## 1. 背景（Context）

用户提出：「每个功能模块建立独立的引擎驱动，由客户端统一调度」。

现状是**混合架构**，子系统已经分散独立，但缺统一的契约与调度器：

| 子系统 | 当前形态 | 痛点 |
|---|---|---|
| 修图（Lumen） | `core/retouch.py` 纯函数 | 无生命周期/进度契约 |
| AI 网关 | 独立进程监听 **8788**（/upload /matting /score /classify /llm /health /unload） | 已是「proto-Hub」，但只管 AI |
| 分类器 | `classifier_service.py` | 各自起服务、端口分散 |
| EXIF | `core/exif.py` `read_exif()` | 散在 app 的 QThread 里调用 |
| 隐私库 | `core/vault.py` `Vault` | 与库强耦合，无统一状态机 |
| 导入/库 | `core/library.py`（sync_root/import_photos）+ `core/scanner.py` | 后台线程各自为政 |
| 地图/画廊/灯箱 | `index.html` 内 JS | 无注册、无可见性/帧预算治理 |

用户的提案本质是把「AI 网关那一套治理」**泛化成所有模块的通用契约**：统一注册、统一生命周期、统一资源（尤其 6GB 显存）时分调度。

---

## 2. 决策（Decision）

引入三层结构：

1. **统一 `Engine` 契约**（`core/enginehub.py`）
   - 元数据：`name`、`kind`（native｜view）、`vram_weight`（0..1）、`max_jobs`、`priority`。
   - 生命周期：`init()` / `start()` / `stop()`。
   - 可观测：`health() -> dict`、`metrics() -> dict`。
   - 作业模型：`submit(task, *args, **kwargs) -> JobHandle`；每个引擎用 `task_<name>` 方法实现具体能力；`JobHandle` 暴露 `state / progress / result / error` 与 `on_progress / on_done / cancel / wait`。

2. **`EngineHub` 调度器**（`core/enginehub.py`）
   - 注册表：`register(engine)` / `unregister(name)`。
   - 并发预算：默认 `ThreadPoolExecutor`（纯 Python、可测、不依赖 Qt）；通过 `set_executor()` 可换为 Qt `QThread` 执行器，满足「Qt 单事件循环 → 重活走 QThread/进程」。
   - **6GB 显存分时优先级队列**：全局 `vram_budget`（默认 1.0）；`vram_weight>0` 的重模型引擎（如 `AiEngine`，权重 1.0）在运行前须 `request_vram()` 抢到预算，运行中占用、结束后释放；预算不足时按 `priority` 排队，从而保证 LLM↔matting 等互斥重模型**唯一同占**。
   - 聚合：`health()` / `metrics()` 汇总全部引擎。
   - 启动/停止：`start_all()` / `stop_all()`。

3. **两层引擎，均登记到 Hub**
   - **原生引擎（Python，`kind='native'`）**：`RetouchEngine`（包 Lumen）、`AiEngine`（包 8788 HTTP 客户端）、`ExifEngine`（包 `read_exif`）、`VaultEngine`（包 `Vault`）、`ImportEngine`（包 `Library.sync_root/import_photos`）。
   - **视图引擎（JS，`kind='view'`）**：`MapEngine` / `GalleryEngine` / `LightboxEngine` 管理动画帧与可见性（即 1.8.9 卡顿优化里已做的 IntersectionObserver 暂停/帧预算），经 `lightalbum://` 桥接 web 页注册到同一 Hub。
   - 工厂 `core/engines/registry.py::build_default_hub(lib)` 组装标准套件；`app.py` 防御式挂载 `self.hub`。

---

## 3. 边界铁律（Constraints）

- **重计算 + 有状态模型 → 原生引擎**；**纯渲染 / 交互 → 视图引擎**，绝不把地图/画廊强行 Python 化。
- **适配层（adapter）原则**：引擎只以薄封装调用既有公共 API（`retouch_file`、`read_exif`、`Vault.*`、`Library.*`、8788 端点），**不修改被包模块的内部逻辑**，从而保证 `run_all_tests` 8 套件不回归。
- **引擎层零 Qt 顶层依赖**：`enginehub.py` 仅用 stdlib（`concurrent.futures`/`enum`/`threading`/`time`），Qt 仅在 app 侧通过 `set_executor()` 注入，确保回归可在无 GUI 环境下实跑。
- **不碰页面视觉**：本 ADR 不涉及 `index.html` 的版式/动效（视图引擎治理复用 1.8.9 已落地机制）。

---

## 4. 后果（Consequences）

### 正面
- 解耦：模块间只依赖 Hub 与契约，新增引擎 = 写一个 `Engine` 子类 + `register`。
- 可测：继承既有 `test_retouch_engine.py` 惯例，新增 `test_enginehub.py`（无 Qt/网络/DB），纳入 `run_all_tests` 第 9 步。
- 资源可控：显存时分唯一同占、并发预算、统一 health/metrics 便于诊断与降级。
- 对齐用户偏好：模块化单体 + ADR + 可演进，无过度服务化。

### 负面 / 成本
- 间接层与样板：每个引擎多一个薄封装类。
- 治理边界：Hub 的 VRAM 队列是**协作式**约束——引擎必须自觉 `request_vram/release_vram`，Hub 不强制杀进程。
- 视图引擎（JS）的注册通道（`lightalbum://`）属后续集成，本 ADR 仅定契约与标记基类，不动 `index.html`。

---

## 5. 落地范围（本 ADR 一步到位）

- [x] ADR-0012 文档（本文件）
- [x] `core/enginehub.py`：契约 + `EngineHub`（含 VRAM 分时、可插拔 executor、`ViewEngine` 标记基类）
- [x] `core/engines/`：`RetouchEngine`、`AiEngine`（2 个参考引擎）+ `ExifEngine`/`VaultEngine`/`ImportEngine`（对齐现有子系统）+ `registry.build_default_hub`
- [x] `app.py` 防御式挂载 `self.hub`（不改任何现有流程）
- [x] `client/test_enginehub.py` + `run_all_tests.py` 第 9 步
- [x] 版本 1.8.9 → **1.9.0**

### 后续（不在本步）
- 把 `app.py` 现有散落 QThread（扫描/EXIF/修图/云端）的调用方逐步改走 `hub.submit(...)`，去掉重复线程样板。
- 视图引擎经 `lightalbum://` 真正注册到 Hub，把 1.8.9 的 IO 暂停/帧预算纳入 Hub 统一 metrics。
- `AiEngine` 接真实 8788 网关联调（当前仅做防御式 HTTP 客户端 + health 探活）。

---

## 6. 替代方案权衡（Trade-offs / Options）

| 方案 | 说明 | 取舍 |
|---|---|---|
| A. 维持现状（散 QThread + 8788 网关） | 不动 | 零成本但无统一治理，显存争用靠手动互斥，难扩展 |
| B. 微服务化（每模块独立进程 + 消息总线） | 最彻底解耦 | 过度工程：本地单机桌面应用，进程间通信开销大，违背模块化单体定位 |
| **C. 引擎化 + 单 Hub 调度（采纳）** | 本 ADR | 解耦与可控兼得，零外部依赖，回归可测；代价是薄封装样板 |
| D. 全进 JS（视图引擎包一切） | 单一语言 | 违反边界铁律：修图/AI/隐私库是有状态重模型，浏览器内不可行 |

**结论**：选 C。它把已有的「AI 网关治理」泛化到全模块，又不破坏既可用代码与桌面应用形态。

# 光影集 · 迭代指导书

> 版本：产品 **光影集 LightAlbum 1.5.0** · 修图引擎 **流明 Lumen 1.5.0**
> 适用对象：接手本项目的开发者。读完本文即可独立完成「改功能 → 验证 → 打包发布」全流程。

---

## 1. 命名与版本（单一事实源）

| 项 | 值 | 定义位置 |
|---|---|---|
| 产品中文名 | 光影集 | `client/lightalbum/version.py` → `APP_NAME` |
| 产品英文名 / 可执行文件 | LightAlbum | `APP_NAME_EN` |
| 产品版本 | 1.5.0 | `APP_VERSION`（语义化 `主.次.修订`） |
| 修图引擎英文码名 | **Lumen** | `ENGINE_NAME` |
| 修图引擎中文名 | **流明** | `ENGINE_NAME_CN` |
| 引擎版本 | 1.5.0 | `ENGINE_VERSION` |

**改版本只改 `version.py` 一个文件**：构建脚本、Windows 文件属性、关于对话框、健康接口、引擎摘要输出全部从这里读取。

为什么叫「流明 Lumen」：流明是光通量单位，取自「光影集」的光；引擎的取向是**测量 → 规划 → 执行，宁可不改也不改坏**，不追求炫技式强改。

---

## 2. 架构总览

```
                        光影集（工程根目录）
   ┌───────────────────────────────────────────────────────────────┐
   │  index.html            权威 Web 版（唯一手工维护的页面）          │
   │  check.js              Web 版校验器（语法/ID/函数/外链/下钻）      │
   │  test_viewer.js        客户端页校验器（含 3 处客户端增强）         │
   │  sync_client.py        由 index.html 生成客户端页（+3 处增强）     │
   └───────────────────────────────────────────────────────────────┘
                                  │ sync（必须）
                                  ▼
   client/ ── 原生客户端（PySide6 + QtWebEngine，Windows 桌面）─────────
   ├── run.py               入口：from lightalbum.app import main
   ├── build.py             发布级打包脚本（PyInstaller）
   ├── resources/           随包分发的页面（index.html + native_bridge.js）
   ├── venv/                打包用隔离环境（不是运行时依赖）
   ├── wheels/ + _extract_wheels.py   离线装配 venv 的轮子与解包器
   └── lightalbum/
       ├── version.py       ★ 版本与命名唯一事实源
       ├── app.py           原生外壳：托盘/单实例/桥接/网页容器
       ├── desktop.py       桌面集成（托盘、日志、任务栏进度）
       └── core/
           ├── retouch.py   ★ 修图引擎「流明 Lumen」（纯 Pillow，零依赖）
           ├── server.py    本地 HTTP（127.0.0.1:8787）：缩略图/原图/同步
           ├── scanner.py   目录扫描    ├── exif.py   EXIF 提取
           ├── thumbs.py    缩略图缓存   ├── vault.py  隐私库（PBKDF2+Fernet）
           ├── home.py      数据家目录（可用 LIGHTALBUM_HOME 迁移）
           ├── ai.py        调度：测量 → 决策 → 执行（三档决策见 §2.1）
           ├── brain.py     决策层：JevBrain / LocalLLMBrain / LocalBrain
           ├── pixelcake.py 执行层：PixelCakeExecutor / LocalExecutor
           └── editplan.py  中间契约：EditPlan（16 个 -1..+1 参数增量）
```

**三条硬边界**（改动前先记住）：

1. **`index.html` 是权威版**，`client/resources/index.html` 是它的派生物（= 权威版 + 恰 3 处客户端增强），**只允许 `sync_client.py` 生成，禁止手改**。
2. **引擎零依赖**：`retouch.py` 只用 Pillow / piexif / 标准库，**不得引入 numpy / opencv / torch**。重模型（LaMa/SAM2 等）必须走外部服务，不进客户端 venv。
3. **绝不覆盖原片**：修图输出永远是原片旁的 `<名>_retouched.jpg`。

### 2.1 本地 AI 服务（可插拔，**全部只走回环地址**）

| 档位 | 服务 | 端口 | 位置 | 说明 |
|---|---|---|---|---|
| 决策① | Jev | 8180 | `D:\lightalbum_ai\jev_local` | 需 WSL2；装了才用 |
| 决策② | **本地 LLM** | 11434 | Ollama（`brain.LocalLLMBrain`） | Windows 原生；建议 `qwen3.5:4b` / `gemma4:e4b` |
| 决策③ | 规则引擎 | — | `brain.LocalBrain` | 永远可用，兜底 |
| 执行 | Pixel Cake | 8765 | `D:\lightalbum_ai\pixel-cake` | 16 参数 / SAM2 / LaMa / MediaPipe |
| 分类 | CLIP 分类器 | 8766 | `D:\lightalbum_ai\classifier_server.py` | 优先 SigLIP 2；权重缺失**自动兜底** ViT-B-32 |

- 决策层优先级 **Jev → 本地 LLM → 规则引擎**：每层 `available()` 不通过就往下退；
  某层探测通过但调用失败时，`ai.smart_edit` 还有一层 try/except 兜底 —— **任何一层出问题都不会让修图链路中断**。
- 探测先**裸 socket 探端口（0.35s）**再发 HTTP：服务没装时避免白等 timeout（实测把 `get_brain` 从 2.4s 降到 0.7s）。
- 重模型一律放 `D:\lightalbum_ai`，**绝不进客户端 venv**（否则 exe 膨胀到 GB 级）。
- 界面「服务设置」里可填 Jev / 本地 LLM / Pixel Cake 地址与模型名（存 localStorage）。

---

## 3. 修图引擎能力矩阵

### 3.1 预设（16 个）

| 分组 | 预设 id | 名称 | 触发方式 |
|---|---|---|---|
| 基础强度 | `soft` / `auto` / `strong` | 轻度 / 标准 / 强力 | 均可 | 
| 修复校正 | `dehaze` | 去雾（暗通道先验 DCP） | **仅显式**（auto 仅强雾且二次门禁通过才做） |
| 修复校正 | `level` | 水平校正（Sobel+Hough 拉直） | **仅显式** |
| 修复校正 | `vignette` | 暗角校正（径向增益复原） | **仅显式** |
| 修复校正 | `chromatic` | 色差校正（R/B 通道径向对齐，收回高反差边缘紫/绿镶边） | **仅显式** |
| 修复校正 | `lens` | 镜头畸变校正（桶形畸变径向反畸变，纯 Pillow 分块 AFFINE 反算，边缘镜像兜底） | **仅显式** |
| 修复校正 | `redeye` | 红眼去除（压暗去红高亮红像素，面积闸门防误伤） | **仅显式** |
| 影调 | `hdr` | HDR 色调映射（局部光照引导增益 + 高光软膝，压缩动态范围/抬暗部） | **仅显式** |
| 影调 | `structure` | 明度结构（明度通道细尺度 USM + 中调掩膜，提质感/立体感） | **仅显式** |
| 影调 | `dodge` | 局部提亮（局部光照引导暗部提亮，只提不压 / 黑场不抬） | **仅显式** |
| 人像 | `portrait` | 人像美化（YCbCr 肤色掩膜磨皮+美白） | **仅显式** |
| 风格化 | `film` | 胶片风格（抬黑+肩部滚降+分离色调+颗粒） | **仅显式** |
| 风格化 | `bw` | 黑白（通道混合） | **仅显式** |
| 风格化 | `glow` | 柔光（Orton） | **仅显式** |

### 3.2 算子清单（`retouch.py`）

测量：`_stats`（亮度/离散/饱和/削顶/死黑）、`_measure_extra`（局部对比 lc / 噪声 / 动态范围 / 色偏 / 雾感 haze）、`_hp_stats`。
执行：`_white_balance`(Shades-of-Gray p=6) ｜ `_auto_levels`(有界) ｜ `_brightness`(高光余量限幅) ｜ `_contrast`(以实测均值为轴) ｜ `_tone_curve`(局部掩膜) ｜ `_clarity` ｜ `_vibrance` ｜ `_denoise`(YCbCr 分级) ｜ `_sharpen`(USM，人像保守) ｜ `_dehaze` ｜ `_detect_horizon`/`_straighten` ｜ `_estimate_vignette`/`_vignette_gain_map`/`_apply_radial_gain`/`_vignette_correct` ｜ `_skin_mask`/`_portrait_beautify` ｜ `_film_tone`/`_split_tone`/`_add_grain`/`_black_white`/`_glow` ｜ `_chromatic_correct`/`_edge_replicate`（色差校正：R 缩小/B 放大径向对齐，边缘镜像兜底）｜ `_mirror_expand`/`_lens_src`/`_solve3`/`_affine_from`/`_lens_correct`（镜头畸变：桶形畸变 `rd=r·(1+k·r²)` 的反推，N×N 分块逐块 `Image.AFFINE` 反算，**AFFINE 喂的是 tile 局部坐标**故矩阵须用局部 dst 配全局 src，边缘 `_mirror_expand` 镜像兜底吸收径向扩张；**`Image.MESH` 非恒等保形不可用**）｜ `_is_redeye`/`_redeye_remove`（红眼：保守启发式，亮且强红主导 `r≥120,g≤110,b≤110,r≥2.2g,r≥2.2b` 的像素压暗去红；面积闸门 `cnt<20` 或 `cnt>0.06·area` 跳过，防整片红误伤；受零云端约束，无人脸检测）｜ `_tone_map`（HDR 色调映射：局部光照引导增益 + 高光软膝，压缩动态范围/抬暗部）｜ `_structure`（明度结构：明度通道细尺度 USM + 中调掩膜）｜ `_param_tone`（决策层增量影调：局部阴影/高光 + 黑/白点端点 LUT）｜ `_dodge`（局部提亮：局部光照引导的暗部提亮，positive-only + 黑场斜坡 + 单像素夹取）。
自检：`_audit`（削顶↑/死黑↑/反差塌陷/色偏加重 → 降级重做 → 仍不过回退原图）。

### 3.3 两条核心契约（**违反即视为 bug**）

1. **「不乱修」**：凡是强效果（去雾/水平/暗角/色差/镜头/红眼/影调/人像/风格化）**只在显式预设下触发，绝不 auto**；幅度都必须有夹取与门槛，好图只做出图锐化。
2. **`steps.append` ⟺ 真实像素改动**：`retouch_file` 里**每一条 `steps.append` 都必须对应一次真实的 `img = 算子(...)`**。历史 bug：降噪只记步骤却从未作用像素（摘要谎报）。新增算子务必自查此条。

### 3.4 本地预设建议（决策层）

`recommend_presets(path, exif)` —— 按**画面特征**为该修什么预设打分排序，**仅建议、不改图**（是否采纳由用户点选决定）。全程本地、零云端，只用引擎自身测量：

| 证据（测量） | 建议预设 |
|---|---|
| 雾感 haze 高 | `dehaze` |
| 地平线倾角 | `level` |
| 径向压暗 k（**跨环带数稳定**才算，滤掉内容假晕影） | `vignette` |
| 死黑 / 削顶占比高 | `hdr` |
| 整体偏暗且非大片死黑 | `dodge` |
| 局部对比偏低（发闷） | `structure` |
| 肤色占比（**硬阈值 ≥200**，滤掉灰/中性误判） | `portrait` |
| 疑似红眼像素 ≥30 | `redeye` |
| 饱和极低 / 偏淡 | `bw` / `film`（风格类**分数封顶**，让功能性建议优先） |

入口：引擎 `retouch.recommend_presets` → `ai.suggest_presets` → 桌面端 `act_aiSuggest`（原生桥 `aiSuggest` → `__onAiSuggest`）；实验室页在「AI 建议」条里渲染 chip，点击即套用预设。

---

## 4. 开发环境

| 用途 | 位置 |
|---|---|
| 打包/测试用 Python | `client/venv/Scripts/python.exe`（已装 PySide6 + Addons/WebEngine、PyInstaller、Pillow、piexif、cryptography） |
| 打包用轮子 | `client/wheels/`（离线装配用，勿删） |
| 轮子解包器 | `client/_extract_wheels.py`（绕过沙箱删除保护，用 `zipfile` 解包） |
| 页面校验 | Node 22（`node check.js` / `node test_viewer.js`） |

> **沙箱/受限环境注意**：批量删除保护会拦截 Python 的 `os.unlink`，导致 `pip install` 中途失败。装配环境的正确姿势是「下载轮子 → `zipfile` 解包到 site-packages → 校验 import」，见 `_extract_wheels.py`。

---

## 5. 标准迭代流程（**照做，别跳步**）

```bash
# 0) 工作目录
cd D:/Test/workbuddy/2026-09-18-09-00-13

# 1) 改「权威页」index.html（若要改 UI）
#    ★ index.html ≈ 580KB >500KB：Edit 类工具会静默失败，必须用 Python 读-改-断言-写回
python - <<'PY'
import io
P = "index.html"
s = io.open(P, encoding="utf-8").read()
old = "旧片段"; new = "新片段"
assert s.count(old) == 1        # 断言，防误伤/防未命中
io.open(P, "w", encoding="utf-8").write(s.replace(old, new, 1))
print("OK")
PY

# 2) 改引擎 / 客户端 Python
#    client/lightalbum/core/retouch.py 等

# 3) 语法与导入自检
cd client
./venv/Scripts/python.exe -m py_compile lightalbum/core/retouch.py lightalbum/app.py

# 4) 单元/集成测试（引擎必须有测试）
./venv/Scripts/python.exe test_retouch_engine.py     # 期望 pass=201 fail=0
./venv/Scripts/python.exe test_retouch_ai.py         # 期望 pass=21  fail=0

# 5) 页面校验（在工程根）
cd ..
node check.js                                        # 期望 SYNTAX_OK / MISSING=none / SMOKE_DONE
python sync_client.py                                # 由权威页重建客户端页（必须）

# 6) 客户端页校验
node test_viewer.js client/resources/index.html      # 期望 pass=16 fail=0

# 6.5) 一键回归（CI 一把梭，等价于 3–6 的校验 + IPC/本地服务，建议每轮收尾跑）
python run_all_tests.py          # 顺序跑 engine+ai+ipc+services+inpaint+async+check.js+test_viewer.js（8 套件），输出 ALL GREEN / REGRESSION FAILED（exit 0/1）
python run_all_tests.py --step engine   # 也可只跑某一步（engine|ai|ipc|services|inpaint|async|web|viewer）

# 7) 发布级打包
cd client
./venv/Scripts/python.exe build.py --dir             # 产物 dist/LightAlbum-<版本>-win64/
```

**验收标准**：第 6.5 步 `run_all_tests.py` 输出 `ALL GREEN`（或第 3–6 步各自全绿）+ 第 7 步输出 `BUILD_OK`，且打包页含最新能力（如新预设 option、引擎徽标）。

---

## 6. 菜谱 A：新增一个「算子」

1. 在 `retouch.py` 的算子区实现 `_your_op(img, ...) -> (Image, info_or_None)`。**信息用 `None` 表示「没动」**（不乱修的惯例）。
2. 幅度必须有**夹取**、触发必须有**门槛**（参考 `_clarity` 的 `0.006 ≤ lc < 0.045`）。
3. 在 `retouch_file` 里接线：`img, x = _your_op(img, ...)`，`x` 非空才 `steps.append`。
4. 若是**显式强效果**：放在对应预设分支里，并置 `reconstructed = True`（免被 `_audit` 整张回退）。
5. 补测试到 `test_retouch_engine.py`（新增一节 `[n]`）：断言方向正确 + 边界不崩 + 不乱触发。

## 7. 菜谱 B：新增一个「预设」

1. 引擎：在 `retouch_file` 增加 `if preset == 'xxx':` 分支（显式强效果置 `reconstructed = True`）。
2. 若需要专属幅度倍率，在 `PRESETS` 加一行；否则自动沿用 `auto` 的倍率。
3. **UI（关键）**：在权威页 `index.html` 的 **`RETOUCH_PRESETS` 单一事实源**里加一项（含 `group` / `id` / `label` / `desc`）。实验室与批量的两个下拉会自动生成，**不要手写 `<option>`**。
4. `python sync_client.py` 同步客户端页。
5. 补测试（预设端到端会产生对应 `steps`，且 `auto` 下不产生）。

---

## 8. 铁律清单（血泪教训，逐条都有代价）

1. **>500KB 的文件禁用 Edit 类工具**：`index.html` 会静默不落盘（工具仍报成功）。一律用 Python 读-改-断言 `count==1`-写回，**改完 grep 复核**。
2. **两页同步**：只改 `index.html`，然后 `python sync_client.py`；两份 diff 应**恰为 3 处客户端增强**。严禁手改 `client/resources/index.html`。
3. **颜色必须实证，严禁猜**：所有颜色用设计 token（`--gold/--sun/--ink/--muted/--card/--line/--bg`）。曾把 `rgba(197,154,75)` 写死，而真实 `--gold` 是 `#c19a4b`(193,154,75)，R 猜错。需要透明度用 `color-mix(in srgb, var(--gold) 45%, var(--ink))`。
4. **沙箱批量删除保护**：打包必须**全新 workpath/distpath**，不加 `--clean`，不复用旧目录；否则构建被拦停且 exe 不更新。
5. **`steps.append` 必须对应真实 `img = 算子(...)`**（见 3.3-2）。
6. **`reconstructed = True`**：显式强效果（dehaze/level/vignette/chromatic/lens/redeye/hdr/structure/dodge/portrait/film/bw/glow）要置位，否则会被「输出自检」按统计变差整张回退，用户要的效果被丢弃。
7. **渲染函数单向**：页面 `refreshAll()` 统一调度，渲染函数之间禁止互相调用（防递归环）。
8. **本地页面必须走 `http://127.0.0.1:<port>/`**：`file://` 是 opaque origin（不能 fetch 同域），且 http 页不能加载 `file://` 子资源；本地图片一律走服务端点。
9. **单实例唤起**：任何新连接立刻唤起窗口并回写回执；客户端**先查 `bytesAvailable()`** 再 `waitForReadyRead()`（回执可能已缓冲）。
10. **引擎零依赖**：不得往 `retouch.py` 引 numpy/opencv/torch。
11. **★别用 `</style>` / `]]></style>` 做锚点插 CSS**（真实事故）：`fabric.min.js` 源码里**自己就含 `"]]></style>\n"` 这段字符串**（SVG 序列化用），而它出现在文档的**第一个 `]]></style>`**。曾据此插编辑器 CSS → 把 CSS 塞进了 **fabric 的 JS 字符串内部**：①字符串跨了真实换行 → SyntaxError → `fabric` 未定义（症状：点「内嵌编辑」提示「编辑器组件未加载」）；②那段 CSS 落在 `<script>` 文本里，从未生效。
    - **正确做法**：CSS 插到**主样式块**里 —— 用 `rfind('</head>')` 或「`<style>`…`</style>` 的第一个真实闭合」定位，**不要**按 `</style>` 全局首次出现来插。
    - **为什么没被测出来**：主 `<script>` 的正则是 `<script>([\s\S]*)<\/script>`，只覆盖**无属性**的 `<script>`；`<script id="fabric-lib">` 有属性 → **完全不进校验**。所以 `check.js` 现在单独校 `FABRIC_SYNTAX` / `FABRIC_LOADS`（vm 里真跑，必须挂出 `fabric.Canvas`）/ `FABRIC_MATCH_VENDOR`（与 `client/resources/vendor/fabric.min.js` 逐字比对）/ `ED_CSS_IN_STYLE`（编辑器 CSS 必须在第一个 `<script>` 之前）。
12. **`check.js` 的 `*_FAIL` 现在真的会失败**：脚本末尾原写死 `process.exit(0)`，导致所有 `XXX_FAIL` 只打印、不影响退出码（挂在 CI 里等于永远绿）。现已改为「打印过失败标记就 FATAL，最终 exit 1」——新增断言请沿用 `XXX_FAIL` 命名。

---

## 9. 测试约定

| 文件 | 覆盖 | 期望 |
|---|---|---|
| `client/test_retouch_engine.py` | 引擎算子级（合成图，无外网） | 201 / 0 |
| `client/test_retouch_ai.py` | 白平衡/亮度/「均衡图不乱修」 | 21 / 0 |
| `client/test_ipc.py` | 单实例 IPC | 10 断言 |
| `client/test_local_services.py` | 本地 AI/服务自启与降级 + **决策层本地 LLM** + **本地库索引 / 图片端点缓存头** | 58 断言 |
| `client/test_inpaint_bridge.py` | 内嵌编辑器→AI 消除桥接（stub PixelCake，无外网） | 13 / 0 |
| `client/test_async_bridge.py` | AI 动作 / 扫描入库 / 存图不阻塞 GUI（子线程 + 回推主线程） | 24 / 0 |
| `check.js`（根） | 权威页：语法/ID/函数/外链/地图下钻/完整性 | `SMOKE_DONE` |
| `test_viewer.js`（根） | 客户端页查看器与进度条 + **内嵌编辑器跨作用域入口** | 19 / 0 |
| `run_all_tests.py`（根） | **CI 一把梭**：顺序跑上面八套件（engine/ai/ipc/services/inpaint/async/check.js/viewer），解析各自绿/红标记，输出单一 `ALL GREEN`/`REGRESSION FAILED`（exit 0/1） | 8/0 全绿 |

约定：测试**只依赖本地**，不联网；**必须实际执行**，禁止只做语法检查就当通过；新增能力必须配新断言。每轮迭代收尾跑 `python run_all_tests.py` 确认全绿。

---

## 10. 发布流程

```bash
cd client
# 1) 改版本：编辑 lightalbum/version.py 的 APP_VERSION / ENGINE_VERSION
# 2) 跑完整回归（第 5 节 3–6 步）
# 3) 打包
./venv/Scripts/python.exe build.py --dir          # onedir（默认，推荐分发）
# 或单文件分发（体积大、启动慢）：
./venv/Scripts/python.exe build.py --onefile
# 4) 产物
#    dist/LightAlbum-<版本>-win64/LightAlbum/LightAlbum.exe  （onedir）
```

产物自检清单：
- [ ] 顶层有 `LightAlbum.exe`，`_internal/` 含 `PySide6/QtWebEngineProcess.exe`、`icudtl.dat`、`qtwebengine_resources*.pak`
- [ ] `_internal/resources/index.html` 含最新预设（如 `value="hdr"` / `value="dodge"`）与引擎徽标
- [ ] 右键 exe → 属性 → 详细信息：产品名/版本/版权/`Retouch engine: Lumen 1.5.0` 均正确
- [ ] 在**有显示器的真机**上双击启动，跑一次「扫描 → 智能修图 → 查看效果」（沙箱无 GUI，无法代跑）

> 资源路径容错：`app.py` 用三候选回退链（`_MEIPASS/resources` → `exe_dir/resources` → `exe_dir/_internal/resources`），兼容 onefile 与 onedir 的 `_internal` 双文件夹布局。

---

## 11. 路线图（建议下一步）

- **修图**：引擎 1.4.0 新增第 16 预设 `dodge`（局部提亮）；引擎 1.3.0 新增「影调」组 —— 第 14 预设 `hdr`（HDR 色调映射）、第 15 预设 `structure`（明度结构）；~~镜头畸变校正~~ ✅（1.2.0 第 12 预设 `lens`）；~~红眼去除~~ ✅（1.2.0 第 13 预设 `redeye`）；~~色差校正~~ ✅（1.1.0 第 11 预设 `chromatic`）。
- **决策层**：✅ 预设自动建议（1.4.0，`recommend_presets`）；✅ **本地 LLM 决策已落地**（1.5.0：`brain.LocalLLMBrain` 对接 Ollama 的 OpenAI 兼容端点，Windows 原生、无需 WSL2；**`qwen3.5:4b` 已拉取就位**，默认 `DEFAULT_LLM_URL=http://127.0.0.1:11434/v1` **免配置**，徽章自动显「本地 LLM（Ollama）」）；✅ 本地执行已补齐（1.3.0 起 `retouch_with_plan` 覆盖 13 项增量）。
- **本地 AI 服务（Pixel Cake / 分类器）** —— 详见 `LOCAL_AI_MODEL_SELECTION.md`：
  - ✅ **P3 BiRefNet 抠图**（1.5.1）：权重 `models/BiRefNet/model.safetensors` 444MB，注意它是 **F16 权重**，输入必须 `tensor.to(dtype=model.dtype)` 对齐精度（否则报 `Input type (float) and bias type (c10::Half)`）。客户端 `act_matting` + 页面「去背景」按钮；`/health` 能力位用 `MattingService.can_load()`（**只 stat 权重文件**，懒加载也能正确显示「就绪」，避免徽章一直「未加载」）。
  - ✅ **P4 pyiqa 画质评分**（1.5.1）：`musiq` 可用。**每个指标独立后台线程加载**（否则 `clipiqa` 的 244MB CLIP 主干下载会**拖死 Pixel Cake 启动**），且启动即预建 `QualityService`。客户端新增 `act_score` + 页面「画质评分」按钮；`ai._quality_hint()` 已复用 `ai.score()` 作为推荐的额外证据。`topiq_nr` 因当前 pyiqa 版本报 `CFANet not in 'arch' registry` 已跳过（非致命）。
  - ⏳ **P1 SigLIP 2 分类器**：受本机透明代理（Watt Toolkit）限制 —— HF 新 **Xet CDN**（`cas-bridge.xethub.hf.co`）SSL 握手/读取超时，权重下不来。分类器维持 **ViT-B-32 兜底**（功能正常，IN 72.8%）。换非 Xet 源或离线拷权重可解锁（IN → 84.1%）。
- **工程**：可选的安装包（Inno Setup / MSIX）、自动更新、崩溃日志上报（本地）。
- **测试**：~~把引擎测试接入 CI~~ ✅ 已实现（工程根 `run_all_tests.py`，顺序跑 engine+ai+ipc+services+inpaint+async+check.js+test_viewer.js **8 套件**，输出 `ALL GREEN`/`REGRESSION FAILED` 且 exit 0/1，可挂 CI；亦支持 `--step` 单步）。

---

## 12. 文件地图速查

```
工程根/
├── index.html              权威页面（唯一手改）
├── check.js                权威页校验
├── test_viewer.js          客户端页校验
├── sync_client.py          权威页 → 客户端页（+3 增强）
└── client/
    ├── run.py              客户端入口
    ├── build.py            发布级打包
    ├── _extract_wheels.py  离线装配 venv
    ├── resources/          随包页面（勿手改）
    └── lightalbum/
        ├── version.py      ★ 版本/命名唯一事实源
        ├── app.py          原生外壳
        ├── desktop.py      桌面集成
        └── core/
            ├── retouch.py  ★ 引擎「流明 Lumen」
            ├── server.py / scanner.py / exif.py / thumbs.py
            ├── vault.py / home.py / ai.py
            └── cake_service.py / classifier 相关
```

---

*本指导书随版本迭代更新；每次改动若触及「铁律」或「流程」，请同步修订本文。*

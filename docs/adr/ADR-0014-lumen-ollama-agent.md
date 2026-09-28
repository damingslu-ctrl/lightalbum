# ADR-0014 Lumen × Ollama AI 修图智能体（本地决策层）

日期：2026-09-28　状态：已实施（P9）

## 背景

native 2.0.0 的修图执行层（LumenEngine，13 项增量管线）已金样全绿，但「这张图该怎么修」
此前只有两条路：手动滑杆 / `RetouchEngine::recommend()` 规则推荐。Python 端已有成熟决策层
`core/brain.py`（LocalLLMBrain → LocalBrain 三层兜底），本机 Ollama + `qwen3.5:4b` 就绪。
P9 要求把本地大模型接进 native 修图链路，成为「AI 修图智能体」。

## 决策

1. **测量驱动，不传图**（沿袭 brain.py 铁律）：决策输入是引擎客观测量
   （亮度/离散/饱和/高光阴影占比/色偏/雾度/噪声 + EXIF ISO·快门 + 用户目标文字），
   不做 base64 图像上传。快（热调用 ~2.5s）、稳、省显存。
2. **输出 = 13 参数增量 JSON**：`retouchWithPlan` 契约键白名单（brightness/contrast/saturation/
   warmth/sharpness/denoise/highlights/shadows/whites/blacks/vibrance/clarity/tint），
   clamp [-1,1]、未知键丢弃、缺键=0。执行直接走 P5-4 金样管线，不新增执行路径。
3. **零云端铁律不破**：URL 必须 127.0.0.1/localhost/[::1]（`isLocalUrl` 校验）；
   Ollama 探测用裸 socket 350ms 快速探端口（未安装秒退，不白等 HTTP timeout）。
4. **Ollama 调用细节**（实测坑）：`/api/chat` + `format:"json"` + `think:false`
   （qwen3.5:4b 是思考型模型，不关思考会把 num_predict 全部吃光、content 为空）+
   `temperature:0`。请求必须带路径 `/api/chat`（打根路径 405 Method Not Allowed）。
5. **失败兜底**：端口不通 → 本地规则（LocalBrain 忠实移植，含色偏→warmth 校正、
   EXIF 感知降噪）；在线但输出不合法 → 同样落兜底，reason 前缀注明（诚实降级）。
6. **UI 模式隔离**（RetouchPage）：`agentMode_` 为独立执行路径（doApply/export 同走
   `retouchWithPlan`，预览所见=导出所得）；任何手动滑杆/预设/重置操作即退出智能体模式，
   杜绝两套算子静默串扰。计划中与滑杆同名的键回填显示（仅供观察后续接手动微调）。
7. **Qt6Network 回归**：P6 曾裁掉 Qt6Network（省 2MB）；P9 起仅回环调用 Ollama 需要 QNetworkAccessManager
   → 链接回归（release.ps1 不再 `-TrimNetwork`），zip 15.5→16.6 MB。回环限定使攻击面与云端暴露不变。

## 后果

- 正面：修图从「规则推荐」升级为「LLM 决策 + 规则兜底」双引擎；与 Python 端 brain.py 架构对齐，
  未来统一迁移/对拍成本低。
- 代价：+1.1MB 包体；LLM 冷加载首调用可达 ~30s（模型载入显存），UI 以禁用按钮+进度提示；
  6GB 显存下与其他本地模型（BiRefNet/分类器）仍需分时占用（既有约定）。
- 冒烟：`LA_AGENT_SMOKE`（真实路径 + 强制兜底 + clamp 白名单断言）加入验证链。

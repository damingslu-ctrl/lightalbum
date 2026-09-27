# -*- coding: utf-8 -*-
"""1.7.2 修图实验室重排：
- 编辑台(#edModal) 从「点「内嵌编辑」才弹出的模态」改为常驻 #retouch 左列，锁定作品后自动载入画布
- 实验室重排为工作台布局：左列(编辑台+进度+结果) + 右侧步骤卡侧栏(锁定/预设/执行/AI工具)
- openEditor/closeEditor/Escape/绑定 同步适配；新增 __edSync 自动同步
铁律：二进制读入 → 归一 CRLF → 每处替换 count==1 断言 → 写回；零外链不新增。
"""
import io, sys

P = 'index.html'
raw = open(P, 'rb').read()
data = raw.decode('utf-8').replace('\r\n', '\n')

def rep(old, new, label):
    global data
    n = data.count(old)
    assert n == 1, 'REP FAIL [%s]: count=%d' % (label, n)
    data = data.replace(old, new)
    print('ok  %s' % label)

# ---------- 1. sec-sub 文案 ----------
rep('''    <div class="sec-sub">选一幅作品，引擎按真实 EXIF（ISO / 快门）决定降噪强度，按直方图实测决定各环节幅度 —— 每一步改了什么都会列出来，并基于曝光、器材与天文数据给出拍摄建议。修图在本地客户端执行，输出 <code>*_retouched.jpg</code>，绝不覆盖原片。</div>''',
'''    <div class="sec-sub">编辑台常驻下方左列，锁定作品后画布自动就位 —— 不用再点「内嵌编辑」。引擎按真实 EXIF（ISO / 快门）决定降噪强度，按直方图实测决定各环节幅度，每一步改了什么都会列出来，并基于曝光、器材与天文数据给出拍摄建议。修图在本地客户端执行，输出 <code>*_retouched.jpg</code>，绝不覆盖原片。</div>''',
    'sec-sub')

# ---------- 2. CSS：.rl-bar 旧横条 → 工作台布局 ----------
rep('''  .rl-bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:14px 0 18px}
  .rl-bar select{padding:8px 12px;border-radius:10px;border:1px solid var(--line);
    background:var(--card);color:var(--ink);font:inherit;max-width:340px}''',
'''  /* ---------- 修图实验室：工作台布局（编辑台常驻左列 + 步骤卡侧栏） ---------- */
  .rl-grid{display:grid;grid-template-columns:minmax(0,1fr) 336px;gap:20px;align-items:start}
  .rl-main{display:flex;flex-direction:column;gap:18px;min-width:0}
  .rl-side{display:flex;flex-direction:column;gap:14px;min-width:0;position:sticky;top:78px}
  .rl-card{background:var(--card);border:1px solid var(--line);border-radius:14px;
    padding:14px 16px;display:flex;flex-direction:column;gap:10px}
  .rl-step{display:flex;align-items:center;gap:8px;font-size:12px;font-weight:800;
    letter-spacing:.08em;color:var(--muted)}
  .rl-step i{font-style:normal;flex:0 0 20px;height:20px;display:grid;place-items:center;
    border-radius:7px;color:var(--gold);font-size:11.5px;
    background:color-mix(in srgb,var(--gold) 16%,transparent);
    border:1px solid color-mix(in srgb,var(--gold) 34%,transparent)}
  .rl-acts{display:flex;gap:8px;flex-wrap:wrap}
  .rl-acts .btn{flex:1 1 auto}
  .rl-card select{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);
    background:var(--bg);color:var(--ink);font:inherit}
  .rl-card input[type=text]{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);
    background:var(--bg);color:var(--ink);font:inherit}
  .rl-card .hint{margin:0;font-size:12px}
  body.dark .rl-card{background:#1d232b;border-color:#2c343c}
  @media(max-width:1080px){
    .rl-grid{grid-template-columns:1fr}
    .rl-side{position:static}
  }''',
    'css rl-bar->workbench')

# ---------- 3. CSS：编辑器模态 → 内嵌常驻 ----------
rep('''  /* ── 内嵌图片编辑器（fabric.js v6.7.1，MIT）──────────────────── */
  .ed{position:fixed;inset:0;z-index:120;display:flex;align-items:center;justify-content:center}
  .ed[hidden]{display:none}
  .ed-bg{position:absolute;inset:0;background:rgba(18,16,12,.72)}
  .ed-panel{position:relative;z-index:1;width:min(1100px,95vw);max-height:92vh;display:flex;flex-direction:column;
    background:var(--card);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--shadow);overflow:hidden}''',
'''  /* ── 内嵌编辑台（fabric.js v6.7.1，MIT）——常驻修图实验室左列，不再点击弹出 ── */
  .ed{position:relative;z-index:auto;display:flex;flex-direction:column;
    background:var(--card);border:1px solid var(--line);border-radius:16px;overflow:hidden;
    box-shadow:0 1px 2px rgba(0,0,0,.05)}
  .ed[hidden]{display:none}
  .ed-panel{position:relative;width:auto;max-height:none;display:flex;flex-direction:column;
    background:transparent;border:0;border-radius:0;box-shadow:none}
  .ed-empty{position:absolute;inset:0;z-index:2;display:grid;place-items:center;text-align:center;
    background:#15140f;color:var(--muted);padding:24px;font-size:13.5px;line-height:1.75}
  .ed-empty[hidden]{display:none}
  .ed-empty b{display:block;font-size:15px;color:var(--ink);margin-bottom:6px}''',
    'css editor inline')

# ---------- 4. CSS：ed-stage 高度 ----------
rep('''  .ed-stage{position:relative;flex:1;min-height:320px;display:grid;place-items:center;background:#15140f;overflow:hidden;padding:14px}''',
'''  .ed-stage{position:relative;flex:1;min-height:380px;max-height:64vh;display:grid;place-items:center;background:#15140f;overflow:hidden;padding:14px}''',
    'css ed-stage')

# ---------- 5. CSS：rl-ai / rl-cfg 进侧栏后去掉负外边距 ----------
rep('''  .rl-ai{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:-4px 0 16px}''',
'''  .rl-ai{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:0}''',
    'css rl-ai margin')

rep('''    padding:14px;margin:0 0 16px;border:1px dashed var(--line);border-radius:12px}''',
'''    padding:14px;margin:0;border:1px dashed var(--line);border-radius:12px}''',
    'css rl-cfg margin')

# ---------- 6. CSS：触控目标 / 暗色 选择器跟随新容器 ----------
rep('''  body.touch .btn,body.touch .rl-bar select,body.touch .rt-pick select,
  body.touch .rl-bar input{min-height:44px}''',
'''  body.touch .btn,body.touch .rl-card select,body.touch .rt-pick select,
  body.touch .rl-card input{min-height:44px}''',
    'css touch targets')

rep('''  body.dark .rl-bar select{background:#1d232b}''',
'''  body.dark .rl-card select{background:#171d24}''',
    'css dark select')

# ---------- 7. CSS：窄屏块 ----------
rep('''    /* 修图实验室：窄屏改单列，对比图上下排 */
    .rl-out{grid-template-columns:1fr}
    .rl-bar select{max-width:none;flex:1 1 100%}
    #rlRun{flex:1 1 100%}''',
'''    /* 修图实验室：窄屏改单列，对比图上下排 */
    .rl-out{grid-template-columns:1fr}
    #rlRun{flex:1 1 auto}''',
    'css narrow block')

# ---------- 8. HTML：实验室主体重排 ----------
rep('''  <div class="rl-bar">
    <span class="rl-lock" id="rlLock" title="当前锁定作品：修图 / 内嵌编辑只针对这一张；从画廊「🖌 跳转修图」可换图">
      🔒 锁定：<span id="rlLockName">未锁定</span>
    </span>
    <select id="rlPreset" aria-label="修图预设"></select>
    <button class="btn primary" id="rlRun">开始智能修图</button>
    <input type="text" id="rlGoal" spellcheck="false" placeholder="修图目标，如：更有胶片感 / 提亮 / 人像磨皮" style="flex:1 1 220px;min-width:180px;padding:8px 12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--ink);font:inherit">
    <button class="btn" id="rlAi">AI 一键修图</button>
    <button class="btn" id="rlMatte">去背景（BiRefNet）</button>
    <button class="btn" id="rlScore">画质评分（pyiqa）</button>
    <button class="btn" id="rlEdit">内嵌编辑</button>
    <span class="rl-msg" id="rlMsg" role="status" aria-live="polite"></span>
    <span class="rl-msg" id="rlMatteMsg" role="status" aria-live="polite"></span>
    <span class="rl-msg" id="rlScoreMsg" role="status" aria-live="polite"></span>
  </div>
  <p class="rl-desc" id="rlDesc" aria-live="polite"></p>
  <div id="rlSuggest" style="display:none;flex-wrap:wrap;gap:8px;align-items:center;margin:10px 0 0"></div>
  <div class="rl-prog" id="rlProg" hidden>
    <div class="rl-prog-track"><i class="rl-prog-fill" id="rlProgFill"></i></div>
    <div class="rl-prog-txt" id="rlProgTxt" role="status" aria-live="polite"></div>
  </div>
  <div class="rl-ai" id="rlAiBar">
    <span class="rl-badge" id="rlBrainBadge">决策：本地规则引擎</span>
    <span class="rl-badge" id="rlExecBadge">执行：流明 Lumen 引擎（本地 Pillow）</span>
    <span class="rl-badge" id="rlMatteBadge" hidden>抠图：—</span>
    <span class="rl-badge" id="rlScoreBadge" hidden>画质评分：—</span>
    <button class="btn sm" id="rlAiCfg">服务设置</button>
  </div>
  <div class="rl-cfg" id="rlCfg" style="display:none">
    <div class="pm-row"><label>Jev 服务地址</label><input id="cfgJev" type="text" spellcheck="false" placeholder="http://127.0.0.1:8180"></div>
    <div class="pm-row"><label>Jev API Key</label><input id="cfgJevKey" type="text" spellcheck="false" placeholder="local-dev"></div>
    <div class="pm-row"><label>Pixel Cake 地址</label><input id="cfgCake" type="text" spellcheck="false" placeholder="http://127.0.0.1:8765"></div>
    <div class="pm-row"><label>本地 LLM 地址</label><input id="cfgLlm" type="text" spellcheck="false" placeholder="http://127.0.0.1:11434/v1（Ollama）"></div>
    <div class="pm-row"><label>本地 LLM 模型</label><input id="cfgLlmModel" type="text" spellcheck="false" placeholder="qwen3.5:4b"></div>
    <div class="hint">两个服务都需要单独部署（模型 2GB+）。未启动时会<strong>自动降级</strong>：决策走本地规则引擎、执行走本地 Pillow 管线，功能不中断。</div>
  </div>
  <div id="rlOut" aria-live="polite">
    <div class="rl-empty">从上方选择一幅作品 → 点「开始智能修图」。摄影建议不依赖客户端，选完作品即自动生成。</div>
  </div>''',
'''  <div class="rl-grid">
    <div class="rl-main">
      <!-- 内嵌编辑台：常驻可见，锁定作品后自动载入画布（不再点击弹出） -->
      <div class="ed" id="edModal">
        <div class="ed-panel" role="region" aria-label="图片编辑台">
          <div class="ed-head">
            <strong>编辑台</strong>
            <span class="ed-sub" id="edSub">画笔涂抹要消除的区域 → AI 消除；裁剪/旋转/拉直/调整即时预览，应用后输出 *_edited.*</span>
            <button class="btn sm" id="rlEdit" title="重新载入当前锁定作品的画布">重载画布</button>
            <button class="btn sm" id="edClose" type="button">收起</button>
          </div>
          <div class="ed-tools">
            <button class="btn sm ed-tool" data-tool="brush" type="button">画笔蒙版</button>
            <button class="btn sm ed-tool" data-tool="crop" type="button">裁剪框</button>
            <button class="btn sm" id="edRot90" type="button">旋转 90&deg;</button>
            <label class="ed-lb">拉直<input type="range" id="edAng" min="-15" max="15" value="0" step="0.5"></label>
            <button class="btn sm ed-tool" data-tool="compare" type="button">对比</button>
            <label class="ed-lb">笔刷<input type="range" id="edBrush" min="4" max="90" value="24"></label>
            <label class="ed-lb">亮度<input type="range" id="edBr" min="-60" max="60" value="0"></label>
            <label class="ed-lb">对比<input type="range" id="edCt" min="-60" max="60" value="0"></label>
            <label class="ed-lb">饱和<input type="range" id="edSt" min="-60" max="60" value="0"></label>
          </div>
          <div class="ed-stage" id="edStage">
            <div class="ed-empty" id="edEmpty">
              <div>
                <b>编辑台待命</b>
                本地客户端可用时，锁定作品的画布会自动载入；纯网页环境仅展示修图结果与建议。<br>
                <button class="btn sm" id="edReopen" type="button" style="margin-top:10px">重新打开编辑台</button>
              </div>
            </div>
            <canvas id="edCanvas"></canvas>
            <img class="ed-orig" id="edOrigImg" alt="" hidden>
            <div class="ed-cmp" id="edCmp" hidden><span>原图</span><input type="range" id="edCmpR" min="0" max="100" value="50"><span>编辑后</span></div>
          </div>
          <div class="ed-foot">
            <button class="btn" id="edReset" type="button">重置</button>
            <button class="btn" id="edSaveMask" type="button">保存蒙版</button>
            <button class="btn" id="edInpaint" type="button">AI 消除（用蒙版）</button>
            <button class="btn primary" id="edApply" type="button">应用并保存</button>
            <span class="ed-msg" id="edMsg" role="status" aria-live="polite"></span>
          </div>
        </div>
      </div>

      <div class="rl-prog" id="rlProg" hidden>
        <div class="rl-prog-track"><i class="rl-prog-fill" id="rlProgFill"></i></div>
        <div class="rl-prog-txt" id="rlProgTxt" role="status" aria-live="polite"></div>
      </div>

      <div id="rlOut" aria-live="polite">
        <div class="rl-empty">从右侧锁定一幅作品 → 点「开始智能修图」。摄影建议不依赖客户端，选完作品即自动生成。</div>
      </div>
    </div>

    <aside class="rl-side">
      <div class="rl-card">
        <div class="rl-step"><i>1</i>锁定作品</div>
        <span class="rl-lock" id="rlLock" title="当前锁定作品：修图 / 编辑台只针对这一张；从画廊「🖌 跳转修图」可换图">
          🔒 锁定：<span id="rlLockName">未锁定</span>
        </span>
        <p class="hint">修图结果与编辑台都只针对锁定的一张；去画廊点缩略图「🖌 跳转修图」即可换图。</p>
      </div>
      <div class="rl-card">
        <div class="rl-step"><i>2</i>选择预设</div>
        <select id="rlPreset" aria-label="修图预设"></select>
        <p class="rl-desc" id="rlDesc" aria-live="polite"></p>
        <div id="rlSuggest" style="display:none;flex-wrap:wrap;gap:8px;align-items:center"></div>
      </div>
      <div class="rl-card">
        <div class="rl-step"><i>3</i>执行修图</div>
        <input type="text" id="rlGoal" spellcheck="false" placeholder="修图目标，如：更有胶片感 / 提亮 / 人像磨皮">
        <div class="rl-acts">
          <button class="btn primary" id="rlRun">开始智能修图</button>
          <button class="btn" id="rlAi">AI 一键修图</button>
        </div>
        <span class="rl-msg" id="rlMsg" role="status" aria-live="polite"></span>
      </div>
      <div class="rl-card">
        <div class="rl-step"><i>＋</i>AI 工具</div>
        <div class="rl-acts">
          <button class="btn" id="rlMatte">去背景（BiRefNet）</button>
          <button class="btn" id="rlScore">画质评分（pyiqa）</button>
        </div>
        <span class="rl-msg" id="rlMatteMsg" role="status" aria-live="polite"></span>
        <span class="rl-msg" id="rlScoreMsg" role="status" aria-live="polite"></span>
      </div>
      <div class="rl-ai" id="rlAiBar">
        <span class="rl-badge" id="rlBrainBadge">决策：本地规则引擎</span>
        <span class="rl-badge" id="rlExecBadge">执行：流明 Lumen 引擎（本地 Pillow）</span>
        <span class="rl-badge" id="rlMatteBadge" hidden>抠图：—</span>
        <span class="rl-badge" id="rlScoreBadge" hidden>画质评分：—</span>
        <button class="btn sm" id="rlAiCfg">服务设置</button>
      </div>
      <div class="rl-cfg" id="rlCfg" style="display:none">
        <div class="pm-row"><label>Jev 服务地址</label><input id="cfgJev" type="text" spellcheck="false" placeholder="http://127.0.0.1:8180"></div>
        <div class="pm-row"><label>Jev API Key</label><input id="cfgJevKey" type="text" spellcheck="false" placeholder="local-dev"></div>
        <div class="pm-row"><label>Pixel Cake 地址</label><input id="cfgCake" type="text" spellcheck="false" placeholder="http://127.0.0.1:8765"></div>
        <div class="pm-row"><label>本地 LLM 地址</label><input id="cfgLlm" type="text" spellcheck="false" placeholder="http://127.0.0.1:11434/v1（Ollama）"></div>
        <div class="pm-row"><label>本地 LLM 模型</label><input id="cfgLlmModel" type="text" spellcheck="false" placeholder="qwen3.5:4b"></div>
        <div class="hint">两个服务都需要单独部署（模型 2GB+）。未启动时会<strong>自动降级</strong>：决策走本地规则引擎、执行走本地 Pillow 管线，功能不中断。</div>
      </div>
    </aside>
  </div>''',
    'html lab workbench')

# ---------- 9. HTML：移除底部弹出模态 ----------
rep('''<div class="ed" id="edModal" hidden>
  <div class="ed-bg" id="edBg"></div>
  <div class="ed-panel" role="dialog" aria-label="图片编辑器" aria-modal="true">
    <div class="ed-head">
      <strong>图片编辑</strong>
      <span class="ed-sub" id="edSub">画笔涂抹要消除的区域 → 交给 AI 消除；裁剪/旋转/拉直/调整即时预览</span>
      <button class="btn sm" id="edClose" type="button">关闭</button>
    </div>
    <div class="ed-tools">
      <button class="btn sm ed-tool" data-tool="brush" type="button">画笔蒙版</button>
      <button class="btn sm ed-tool" data-tool="crop" type="button">裁剪框</button>
      <button class="btn sm" id="edRot90" type="button">旋转 90&deg;</button>
      <label class="ed-lb">拉直<input type="range" id="edAng" min="-15" max="15" value="0" step="0.5"></label>
      <button class="btn sm ed-tool" data-tool="compare" type="button">对比</button>
      <label class="ed-lb">笔刷<input type="range" id="edBrush" min="4" max="90" value="24"></label>
      <label class="ed-lb">亮度<input type="range" id="edBr" min="-60" max="60" value="0"></label>
      <label class="ed-lb">对比<input type="range" id="edCt" min="-60" max="60" value="0"></label>
      <label class="ed-lb">饱和<input type="range" id="edSt" min="-60" max="60" value="0"></label>
    </div>
    <div class="ed-stage" id="edStage">
      <canvas id="edCanvas"></canvas>
      <img class="ed-orig" id="edOrigImg" alt="" hidden>
      <div class="ed-cmp" id="edCmp" hidden><span>原图</span><input type="range" id="edCmpR" min="0" max="100" value="50"><span>编辑后</span></div>
    </div>
    <div class="ed-foot">
      <button class="btn" id="edReset" type="button">重置</button>
      <button class="btn" id="edSaveMask" type="button">保存蒙版</button>
      <button class="btn" id="edInpaint" type="button">AI 消除（用蒙版）</button>
      <button class="btn primary" id="edApply" type="button">应用并保存</button>
      <span class="ed-msg" id="edMsg" role="status" aria-live="polite"></span>
    </div>
  </div>
</div>''',
'''<!-- 内嵌编辑台已迁入 #retouch 左列（.rl-main），此处不再有弹出模态 -->''',
    'html remove modal')

# ---------- 10. JS：openEditor 适配内嵌（silent 参数 + edEmpty 空态 + 幂等早退） ----------
rep('''  function openEditor(){
    if(!(window.__native && window.__native.call)){ warn('内嵌编辑仅本地客户端可用'); return; }
    var p = rlCurPhoto();   // 锁定作品优先（画廊「跳转修图」设定），否则第一张
    if(!p){ warn('没有可编辑的作品，先从画廊跳转修图'); return; }
    var abs = p.path || (window.__nativeAbs && window.__nativeAbs[p.rel]) || '';
    if(!abs){ warn('该照片没有本机路径，无法编辑'); return; }
    var hd = p.orig || p.img;
    if(!hd){ warn('无可用图像源'); return; }
    if(typeof fabric === 'undefined'){ warn('编辑器组件未加载（fabric.js 缺失），请重启客户端后重试'); return; }

    $('edModal').hidden=false;
    var stage=$('edStage');''',
'''  function openEditor(silent){
    function bail(t){
      if($('edEmpty')) $('edEmpty').hidden=false;   // 回到「待命」空态
      if(!silent) warn(t);
    }
    if(!(window.__native && window.__native.call)){ bail('内嵌编辑仅本地客户端可用 —— 打开客户端后画布自动就绪'); return; }
    var p = rlCurPhoto();   // 锁定作品优先（画廊「跳转修图」设定），否则第一张
    if(!p){ bail('没有可编辑的作品，先从画廊跳转修图'); return; }
    var abs = p.path || (window.__nativeAbs && window.__nativeAbs[p.rel]) || '';
    if(!abs){ bail('该照片没有本机路径，无法编辑'); return; }
    if(ed && ed.abs===abs){ if($('edEmpty')) $('edEmpty').hidden=true; if(!silent) msg('编辑台已就绪'); return; }
    var hd = p.orig || p.img;
    if(!hd){ bail('无可用图像源'); return; }
    if(typeof fabric === 'undefined'){ bail('编辑器组件未加载（fabric.js 缺失），请重启客户端后重试'); return; }

    if($('edEmpty')) $('edEmpty').hidden=true;
    var stage=$('edStage');''',
    'js openEditor')

# ---------- 11. JS：fabric 初始化失败回空态 ----------
rep('''    }catch(err){
      $('edModal').hidden=true;
      warn('编辑器初始化失败：' + (err && err.message ? err.message : err));
      return;
    }''',
'''    }catch(err){
      if($('edEmpty')) $('edEmpty').hidden=false;
      warn('编辑器初始化失败：' + (err && err.message ? err.message : err));
      return;
    }''',
    'js fabric catch')

# ---------- 12. JS：closeEditor 回空态而非隐藏面板 ----------
rep('''  function closeEditor(){
    if($('edModal')) $('edModal').hidden=true;
    if(ed && ed.fc){ ed.fc.dispose(); ed=null; }
  }''',
'''  function closeEditor(){
    if(ed && ed.fc){ ed.fc.dispose(); ed=null; }
    if($('edEmpty')) $('edEmpty').hidden=false;   // 编辑台常驻：收起画布只回空态，不隐藏面板
  }''',
    'js closeEditor')

# ---------- 13. JS：__edSync 自动同步 ----------
rep('''  window.__openEditor = openEditor;
  window.__closeEditor = closeEditor;''',
'''  window.__openEditor = openEditor;
  window.__closeEditor = closeEditor;

  // 编辑台自动同步：修图实验室每次重渲染（锁定作品变化 / 照片列表变化）后，
  // 若客户端可用且画布未就位（或锁定图已变），静默重建画布 —— 免「点内嵌编辑才出现」。
  var edSyncAbs=null, edSyncTimer=null;
  window.__edSync = function(){
    if(!(window.__native && window.__native.call)) return;
    if(typeof fabric === 'undefined') return;
    if(typeof rlCurPhoto !== 'function') return;
    var p = rlCurPhoto(); if(!p) return;
    var abs = p.path || (window.__nativeAbs && window.__nativeAbs[p.rel]) || '';
    if(!abs) return;
    if(ed && ed.abs===abs){ edSyncAbs=null; return; }
    if(edSyncAbs===abs) return;               // 已在排队
    edSyncAbs=abs;
    if(edSyncTimer) clearTimeout(edSyncTimer);
    edSyncTimer=setTimeout(function(){ edSyncTimer=null; openEditor(true); },160);
  };
  try{ if(typeof rlCurPhoto==='function') window.__edSync(); }catch(_){}''',
    'js __edSync')

# ---------- 14. JS：Escape 不再关闭常驻编辑台 ----------
rep('''    document.addEventListener('keydown', function(e){
      if(e.key==='Escape' && $('edModal') && !$('edModal').hidden) closeEditor();
    });''',
'''    // 编辑台常驻内嵌：不再响应 Escape 关闭（画布是页面的一部分，误按 Esc 不应清空工作区）''',
    'js escape')

# ---------- 15. JS：绑定 重载画布 / 重新打开 ----------
rep('''    if((b=$('rlEdit'))) b.addEventListener('click', openEditor);''',
'''    if((b=$('rlEdit'))) b.addEventListener('click', function(){ openEditor(false); });   // 重载画布
    if((b=$('edReopen'))) b.addEventListener('click', function(){ openEditor(false); });''',
    'js bind rlEdit/edReopen')

# ---------- 16. JS：renderRetouchLab 挂 __edSync ----------
rep('''  updateLockLabel();
  renderLabAll();
}''',
'''  updateLockLabel();
  renderLabAll();
  if(window.__edSync) window.__edSync();   // 编辑台随锁定作品自动就位（客户端环境）
}''',
    'js renderRetouchLab hook')

# ---------- 17. JS：两个结果回推钩子（单张 AI + 批量修图）换图后都同步编辑台 ----------
old_hook = '''    if(hit){ rlSelId = hit.id; updateLockLabel(); }'''
new_hook = '''    if(hit){ rlSelId = hit.id; updateLockLabel(); if(window.__edSync) window.__edSync(); }'''
n_hook = data.count(old_hook)
assert n_hook == 2, 'REP FAIL [result hooks]: count=%d' % n_hook
data = data.replace(old_hook, new_hook)
print('ok  js result hooks (x2)')

# ---------- 18. check.js：ED_CSS 锚点跟随内嵌形态 ----------
raw2 = open('check.js', 'rb').read()
ck = raw2.decode('utf-8').replace('\r\n', '\n')
old_ck = "const edPos = h.indexOf('.ed{position:fixed');"
new_ck = "const edPos = h.indexOf('.ed{position:relative');"
assert ck.count(old_ck) == 1, 'check.js anchor count=%d' % ck.count(old_ck)
ck = ck.replace(old_ck, new_ck)
open('check.js', 'wb').write(ck.replace('\n', '\r\n').encode('utf-8'))
print('ok  check.js ED_CSS anchor')

# ---------- 写回 ----------
out = data.replace('\n', '\r\n').encode('utf-8')
open(P, 'wb').write(out)

# ---------- 终检 ----------
final = out.decode('utf-8')
assert final.count('id="edEmpty"') == 1
assert final.count('id="edReopen"') == 1
assert final.count('id="edModal"') == 1
assert 'role="dialog"' not in final
assert 'position:fixed;inset:0;z-index:120' not in final
assert final.count('__edSync') >= 4
assert '(https?:)//' not in final
import re
ext = re.findall(r'(?:src|href)="(?:https?:)?//', final)
assert not ext, ext
print('ALL REPS DONE  size=%dKB' % (len(out)//1024))

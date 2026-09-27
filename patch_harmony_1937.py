# -*- coding: utf-8 -*-
# 1.9.37 智能选片(cull) + AI修图(retouch) UI 与整体「雾隐·墨韵」玻璃拟态风格统一
# 安全范式：读全量 -> 每处 old 断言 count==1 -> 替换 -> 写回 -> 复核不变量
import io, sys

PATH = r"D:\Test\workbuddy\2026-09-18-09-00-13\index.html"
with io.open(PATH, "r", encoding="utf-8") as f:
    data = f.read()

repls = []

# ---- 1) .rl-card 改为与整体 .card 一致的玻璃拟态（毛玻璃+阴影+变量圆角） ----
repls.append((
"  .rl-card{background:var(--card);border:1px solid var(--line);border-radius:14px;\n"
"    padding:14px 16px;display:flex;flex-direction:column;gap:10px}",
"  .rl-card{background:var(--glass);border:1px solid var(--glass-line);border-radius:var(--r);\n"
"    padding:14px 16px;display:flex;flex-direction:column;gap:10px;\n"
"    box-shadow:var(--shadow);backdrop-filter:var(--glass-blur);-webkit-backdrop-filter:var(--glass-blur)}\n"
"  @supports not (backdrop-filter:blur(1px)){.rl-card{background:var(--card)}}",
"#1 .rl-card 玻璃拟态化"))

# ---- 2) 删除 retouch 深色主题下对 .rl-card 的实色覆盖（改由 --glass 统一） ----
repls.append((
"  body.dark .rl-card{background:#1d232b;border-color:#2c343c}",
"  /* .rl-card 跟随 --glass / --glass-line，深浅主题与整体 .card 一致，无需单独覆盖 */",
"#2 删除 .rl-card 深色实色覆盖"))

# ---- 3) .rl-card 内控件背景 var(--bg) -> var(--card)（统一为卡片内实色内嵌控件） ----
repls.append((
"  .rl-card select{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);\n"
"    background:var(--bg);color:var(--ink);font:inherit}\n"
"  .rl-card input[type=text]{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);\n"
"    background:var(--bg);color:var(--ink);font:inherit}",
"  .rl-card select{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);\n"
"    background:var(--card);color:var(--ink);font:inherit}\n"
"  .rl-card input[type=text]{width:100%;padding:8px 12px;border-radius:10px;border:1px solid var(--line);\n"
"    background:var(--card);color:var(--ink);font:inherit}",
"#3 .rl-card 控件背景统一"))

# ---- 4) 智能选片：控件输入框去掉未定义 --panel 回退（浅色主题黑底 bug） ----
repls.append((
".cull-field select,.cull-field input{background:var(--panel,#0f1622);color:inherit;border:1px solid var(--line,#243044);\n  border-radius:8px;padding:7px 10px;font-size:14px;min-width:120px}",
".cull-field select,.cull-field input{background:var(--card);color:var(--ink);border:1px solid var(--line);\n  border-radius:8px;padding:7px 10px;font-size:14px;min-width:120px}",
"#4 cull 输入框修正"))

# ---- 5) 智能选片：结果缩略卡 .cull-item 同样修正 --panel ----
repls.append((
".cull-item{background:var(--panel,#0f1622);border:1px solid var(--line,#243044);border-radius:10px;overflow:hidden;font-size:12px}",
".cull-item{background:var(--card);border:1px solid var(--line);border-radius:10px;overflow:hidden;font-size:12px}",
"#5 cull-item 修正"))

# ---- 6) 智能选片：去重组 .dup-grp 同样修正 --panel ----
repls.append((
".dup-grp{background:var(--panel,#0f1622);border:1px solid var(--line,#243044);border-radius:10px;padding:10px;display:flex;gap:12px}",
".dup-grp{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px;display:flex;gap:12px}",
"#6 dup-grp 修正"))

# ---- 7) 智能选片：结果图占位底色由硬编码近黑 -> 设计变量（浅色主题不再黑块） ----
repls.append((
".cc-img{aspect-ratio:1/1;background:#0a0f17;display:flex;align-items:center;justify-content:center}",
".cc-img{aspect-ratio:1/1;background:var(--line);display:flex;align-items:center;justify-content:center}",
"#7 cc-img 占位底色统一"))

# ---- 执行 + 断言 ----
for old, new, label in repls:
    c = data.count(old)
    if c != 1:
        print("FAIL [%s] count=%d (expected 1)" % (label, c))
        sys.exit(1)
    data = data.replace(old, new, 1)
    print("OK   [%s]" % label)

with io.open(PATH, "w", encoding="utf-8") as f:
    f.write(data)

# ---- 复核关键不变量 ----
checks = {
    "rl-card 玻璃化 background:var(--glass)": data.count(".rl-card{background:var(--glass)"),
    "rl-card 玻璃降级回退": data.count("@supports not (backdrop-filter:blur(1px)){.rl-card{background:var(--card)}"),
    "rl-card 深色实色覆盖已删": data.count("body.dark .rl-card{background:#1d232b"),
    "rl-card 控件 background:var(--card)": data.count("background:var(--card);color:var(--ink);font:inherit"),
    "残留 --panel 引用(应为0)": data.count("var(--panel"),
    "cull 输入框已用 var(--card)": data.count(".cull-field select,.cull-field input{background:var(--card)"),
    "翡翠绿语义色保留 rl-badge.on": data.count(".rl-badge.on{background:rgba(90,143,107,.14)"),
    "cc-img 已用 var(--line)": data.count(".cc-img{aspect-ratio:1/1;background:var(--line)"),
}
allok = True
for k, v in checks.items():
    ok = (v == 0) if "残留" in k or "已删" in k else (v >= 1)
    if not ok:
        allok = False
    print(("OK  " if ok else "CHK ") + "%-55s %d" % (k, v))
print("WROTE %d bytes" % len(data))
sys.exit(0 if allok else 1)

# -*- coding: utf-8 -*-
"""gen_clip_fixtures.py —— CLIP 平价黄金夹具生成器（oracle，第四轮 T6）。

同进程直调 `D:\\lightalbum_ai\\classifier_server.py`（**不起 HTTP**，杜绝端口漂移/服务
版本歧义）：`load_model()` → 把 PIL 图喂给 `classify_batch()`，让 gold 由**服务器自己的
判定代码路径**产出（黑白确定性 / low_confidence / mono_guard 三段 + top3 4dp + sat），
而不是手抄一份等价逻辑。返回行 verbatim 写成 `<name>.gold.json`。

候选预筛（R-12c，face 轨 R-11 教训的 CLIP 版）：从 `LA_CLIP_IMGS`（逗号分隔的图路径或
目录）确定性等距抽样 `LA_CLIP_SCREEN_MAX` 张，逐张算判定余量，**剔除任何落在决策边界
附近的样本**：
    · top1−top2 概率差 < 0.10                    → 弃（类别归属会随数值路径翻转）
    · sat 距 18.0（MONO_SAT_THRESHOLD） < 6.0     → 弃（黑白/彩色翻转）
    · sat 距 45.0（MONO_SAT_GUARD）      < 6.0     → 弃（mono_guard 护栏翻转）
    · top1 距 0.20（OTHER_SCORE_THRESHOLD）< 0.05  → 弃（是否归「其他」翻转）
    · mono_guard 命中时，次优非黑白类分距 0.20 < 0.05 → 弃（护栏放行/回落翻转）
选中集要求：≥6 张、覆盖 ≥4 个不同 label、至少一张「确信黑白」（sat ≤ 12.0）与一张
「确信彩色」（sat ≥ 51.0）；不满足即硬失败退出（exit 2），绝不静默产出弱夹具。

隐私红线（R-06 / R-12a / spec §7 #78 同族条款）：**仓库里没有已入库的 QA 安全照片**，
夹具图与 gold JSON 一律不入库 —— 本脚本本身入库，产物永远只落在 env 指向的本地目录
（建议 %TEMP%\\la_clip_fix），第 77 门 `LA_CLIP_MODEL_QA` 只经 env
`LA_CLIP_IMGS` + `LA_CLIP_GOLD_DIR` 消费（SHA 清单只进本地报告）。

数值红线：模型 pin 走 env `CLIP_MODEL`/`CLIP_PRETRAINED`（import 之前钉死
ViT-B-32/laion2b_s34b_b79k），HF 双缓存变量 + `HF_HUB_OFFLINE=1` 绝不下载；**并强制
CPU fp32**（`CUDA_VISIBLE_DEVICES=''` 在 Windows CRT 下等于删变量，故进程内再把
`torch.cuda.is_available` 钉 False，见 main()）—— 与随包 clipinfer（CPUExecutionProvider、
prompts.bin 同表同算法）同一条数值路径；加载完若 `device!='cpu'` 直接硬失败退出。

fixture 保持 JPEG（与 clipinfer 的 `REQ jpg` 线上格式同构）—— face 轨推荐 PNG 的
「解码归因唯一」原则在此不适用：生产路径本就是 JPEG 双解码器（libjpeg vs PIL），
≤1 LSB 微差由 R-12c 余量 + #77 容差（Δp≤0.02 / Δsat≤1.0）吸收。

运行方式见同目录 README.md。
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys

# ---------------------------------------------------------------- 环境（必须在 import torch/classifier_server 之前）
os.environ.setdefault("HF_HOME", r"D:\lightalbum_ai\hf_cache")
os.environ.setdefault("HF_HUB_CACHE", r"D:\lightalbum_ai\hf_cache")
os.environ.setdefault("HF_HUB_OFFLINE", "1")
os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")
os.environ.setdefault("CUDA_VISIBLE_DEVICES", "")
os.environ["CLIP_MODEL"] = os.environ.get("CLIP_MODEL", "ViT-B-32")
os.environ["CLIP_PRETRAINED"] = os.environ.get("CLIP_PRETRAINED", "laion2b_s34b_b79k")

sys.path.insert(0, r"D:\lightalbum_ai")
import classifier_server  # noqa: E402 - oracle 真源（import 不触发 uvicorn，仅 __main__ 起服务）

GAP_MIN = 0.10          # top1−top2 概率差下限
SAT_MARGIN_MIN = 6.0    # sat 距 18.0 / 45.0 的下限
OTHER_MARGIN_MIN = 0.05  # top1（或护栏次优）距 0.20 的下限
MONO_LABEL = "黑白"     # LABELS 第 8 类名（classifier_server.py:53，与判定环同字面量）
MONO_OK_SAT = 18.0 - SAT_MARGIN_MIN    # 确信黑白：sat ≤ 12.0
COLOR_OK_SAT = 45.0 + SAT_MARGIN_MIN   # 确信彩色：sat ≥ 51.0
MIN_FIXTURES = 6
MIN_LABELS = 4
BATCH = 32
IMG_EXT = (".jpg", ".jpeg", ".png", ".bmp", ".webp")


class _Tee:
    """stdout 同时镜像到 UTF-8 日志文件（避开 PowerShell GBK 控制台的中文乱码留档问题）。"""

    def __init__(self, stream, fh):
        self._s, self._f = stream, fh

    def write(self, data):
        self._s.write(data)
        self._f.write(data)
        self._f.flush()

    def flush(self):
        self._s.flush()
        self._f.flush()


def _open_log() -> None:
    path = os.environ.get("LA_CLIP_LOG", "").strip()
    if not path:
        return
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    sys.stdout = _Tee(sys.stdout, open(path, "w", encoding="utf-8", newline="\n"))
    print(f"[gen_clip_fixtures] stdout tee → {path}")


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _int_env(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    return int(raw) if raw else default


def collect_candidates() -> list[str]:
    """LA_CLIP_IMGS（逗号分隔，条目可为图路径或目录）→ 候选池（目录内按名排序收图）。"""
    raw = os.environ.get("LA_CLIP_IMGS", "").strip()
    if not raw:
        sys.exit("LA_CLIP_IMGS not set: comma-separated image paths OR directories "
                 "(fixture photos stay local, never git)")
    pool: list[str] = []
    for part in raw.split(","):
        part = part.strip().strip('"')
        if not part:
            continue
        if os.path.isdir(part):
            for name in sorted(os.listdir(part)):
                if name.lower().endswith(IMG_EXT):
                    fp = os.path.join(part, name)
                    # iCloud 未下载槽文件常是 <10KB 占位，直接跳过（省解码失败噪声）
                    try:
                        if os.path.getsize(fp) < 50_000:
                            continue
                    except OSError:
                        continue
                    pool.append(fp)
        elif os.path.isfile(part):
            pool.append(part)
        else:
            print(f"[gen_clip_fixtures] WARN skip (not found): {part}", file=sys.stderr)
    if not pool:
        sys.exit("LA_CLIP_IMGS resolved to zero usable images")
    cap = _int_env("LA_CLIP_SCREEN_MAX", 160)
    if len(pool) > cap:                    # 确定性等距抽样：与池大小/顺序无关，可复现
        pool = [pool[i * len(pool) // cap] for i in range(cap)]
    return pool


def open_pil(path: str):
    from PIL import Image

    im = Image.open(path)
    im.load()
    return im


def margins(row: dict) -> dict:
    """把 oracle 返回行折成决策余量（R-12c 的全部四个维度）。"""
    top = row["top"]                      # [(name, prob 4dp)] 降序
    best, second = float(top[0][1]), float(top[1][1]) if len(top) > 1 else 0.0
    sat = float(row["sat"])
    m = {
        "gap_top1_top2": round(best - second, 4),
        "sat_dist_18": round(abs(sat - 18.0), 1),
        "sat_dist_45": round(abs(sat - 45.0), 1),
        "top1_dist_020": round(abs(best - 0.20), 4),
    }
    note = row.get("note") or ""
    if note.startswith("mono_guard"):     # 护栏采信/回落也必须在边界之外
        # '黑白' 字面量与 classifier_server.py:220-231 判定环内一致（不靠 index 猜表序）
        alt = next((float(p) for n, p in top if n != MONO_LABEL), 0.0)
        m["guard_alt_dist_020"] = round(abs(alt - 0.20), 4)
        m["alt_score"] = alt
    return m


def eligible(m: dict) -> tuple[bool, str]:
    if m["gap_top1_top2"] < GAP_MIN:
        return False, "gap<%.2f" % GAP_MIN
    if m["sat_dist_18"] < SAT_MARGIN_MIN:
        return False, "|sat-18|<%.1f" % SAT_MARGIN_MIN
    if m["sat_dist_45"] < SAT_MARGIN_MIN:
        return False, "|sat-45|<%.1f" % SAT_MARGIN_MIN
    if m["top1_dist_020"] < OTHER_MARGIN_MIN:
        return False, "|top1-0.20|<%.2f" % OTHER_MARGIN_MIN
    if "guard_alt_dist_020" in m and m["guard_alt_dist_020"] < OTHER_MARGIN_MIN:
        return False, "|alt-0.20|<%.2f" % OTHER_MARGIN_MIN
    return True, ""


def pick(rows: list[dict], want: int) -> tuple[list[dict], list[str]]:
    """余量优先的多样性选取：先覆盖每类一张（含确信黑白/确信彩色），再按余量补齐。"""
    ok = [r for r in rows if r["ok"]]
    if len(ok) < MIN_FIXTURES:
        return [], ["eligible=%d < %d" % (len(ok), MIN_FIXTURES)]
    ok.sort(key=lambda r: (-r["m"]["gap_top1_top2"], -r["m"]["sat_dist_18"]))

    chosen: list[dict] = []
    by_label: dict[str, list[dict]] = {}
    for r in ok:
        by_label.setdefault(r["row"]["label"], []).append(r)
    # 1) 强制锚点：一张确信黑白 + 一张确信彩色（各取该类余量最大者）
    mono = [r for r in ok if r["row"]["sat"] <= MONO_OK_SAT]
    color = [r for r in ok if r["row"]["sat"] >= COLOR_OK_SAT]
    problems: list[str] = []
    if not mono:
        problems.append("no confidently-monochrome fixture (sat<=%.1f)" % MONO_OK_SAT)
    else:
        chosen.append(mono[0])
    if not color:
        problems.append("no well-colored fixture (sat>=%.1f)" % COLOR_OK_SAT)
    elif color[0] not in chosen:
        chosen.append(color[0])
    # 2) 类别覆盖：每个 label 补一张（取该类余量最大、未选中的）
    for label, lst in sorted(by_label.items(), key=lambda kv: -len(kv[1])):
        if len(chosen) >= want:
            break
        nxt = next((r for r in lst if r not in chosen), None)
        if nxt is not None:
            chosen.append(nxt)
    # 3) 数量补齐（按余量降序，取 label 多样性最高的顺序）
    for r in ok:
        if len(chosen) >= want:
            break
        if r not in chosen:
            chosen.append(r)

    labels = {r["row"]["label"] for r in chosen}
    if len(chosen) < MIN_FIXTURES:
        problems.append("chosen=%d < %d" % (len(chosen), MIN_FIXTURES))
    if len(labels) < MIN_LABELS:
        problems.append("labels=%d < %d" % (len(labels), MIN_LABELS))
    final = [r for r in ok if r in chosen]
    final.sort(key=lambda r: r["path"])
    return final, problems


def main() -> int:
    _open_log()
    # Windows CRT 语义：`_putenv("NAME=")`（即 os.environ['NAME']=''）是**删除**变量，
    # 所以 CUDA_VISIBLE_DEVICES='' 从 PowerShell/Git Bash 侧传进来并不可靠（本机实测
    # load_model 仍落到 cuda）。这里再补一刀：进程内把 is_available 钉成 False，
    # classifier_server.load_model() 便走 `device='cpu'` 且**不进 autocast fp16 分支**
    # （:188-192 只有 cuda 才 autocast）—— gold 必须与随包 clipinfer 同一条 CPU fp32
    # 数值路径，否则 oracle 与 ONNX 的对拍从一开始就不是同一个函数。
    import torch

    torch.cuda.is_available = lambda: False          # type: ignore[assignment]
    st = classifier_server.load_model()
    if not st["loaded"]:
        sys.exit("oracle load_model failed: %s" % st["error"])
    print(f"[gen_clip_fixtures] oracle=classifier_server.classify_batch (in-process)  "
          f"model={st['model_used']}  device={st['device']}  "
          f"mono/sat={classifier_server.MONO_SAT_THRESHOLD}  "
          f"guard={classifier_server.MONO_SAT_GUARD}  "
          f"other={classifier_server.OTHER_SCORE_THRESHOLD}")
    if st["device"] != "cpu":
        sys.exit("CUDA_VISIBLE_DEVICES 未生效：gold 必须在 CPU fp32 路径产出（与随包 "
                 "clipinfer 的 CPUExecutionProvider 同数值路径）")

    paths = collect_candidates()
    print(f"[gen_clip_fixtures] 筛选池 {len(paths)} 张（LA_CLIP_SCREEN_MAX 决定上限，"
          f"等距抽样可复现）")

    rows: list[dict] = []
    batch: list[str] = []
    opened = 0
    for p in paths:
        batch.append(p)
        if len(batch) < BATCH:
            continue
        rows.extend(_screen(batch))
        opened += BATCH
        print(f"[gen_clip_fixtures] 已筛 {opened}/{len(paths)}（eligible "
              f"{sum(1 for r in rows if r['ok'])}）", flush=True)
        batch = []
    if batch:
        rows.extend(_screen(batch))

    print("\n[gen_clip_fixtures] —— 预筛表（全部候选，含被弃原因）")
    print("%-56s %-10s %7s %6s %6s %6s %6s %6s  %s" % (
        "file", "label", "top1", "gap", "|s-18|", "|s-45|", "|t-.20|", "sat", "弃因/OK"))
    for r in sorted(rows, key=lambda x: (not x["ok"], -x["m"]["gap_top1_top2"])):
        m, row = r["m"], r["row"]
        print("%-56s %-10s %7.4f %6.4f %6.1f %6.1f %6.4f %6.1f  %s" % (
            os.path.basename(r["path"])[:56], row["label"], row["top"][0][1],
            m["gap_top1_top2"], m["sat_dist_18"], m["sat_dist_45"], m["top1_dist_020"],
            row["sat"], r["why"] or "OK"), flush=True)

    want = _int_env("LA_CLIP_PICK", 8)
    chosen, problems = pick(rows, want)
    if problems:
        print("\n".join("[gen_clip_fixtures] FAIL " + p for p in problems), file=sys.stderr)
        print("[gen_clip_fixtures] 提示：扩大 LA_CLIP_IMGS 池或调高 LA_CLIP_SCREEN_MAX 后重跑",
              file=sys.stderr)
        return 2
    print(f"\n[gen_clip_fixtures] 选中 {len(chosen)} 张，覆盖 label "
          f"{sorted({c['row']['label'] for c in chosen})}")

    fix_dir = os.environ.get("LA_CLIP_FIX_DIR", "").strip()
    gold_dir = os.environ.get("LA_CLIP_GOLD_DIR", "").strip()
    final_rows = []
    for c in chosen:
        src = c["path"]
        dst = src
        if fix_dir and os.path.abspath(fix_dir) != os.path.dirname(os.path.abspath(src)):
            os.makedirs(fix_dir, exist_ok=True)
            dst = os.path.join(fix_dir, os.path.basename(src))
            if os.path.abspath(dst) != os.path.abspath(src):
                shutil.copyfile(src, dst)
        final_rows.append((dst, c))

    # gold 由最终 fixture 文件本身重跑一遍产生（字节与源一致，路径即 #77 要喂的路径）
    gold_src_rows = _screen([p for p, _ in final_rows])
    by_path = {r["path"]: r for r in gold_src_rows}

    produced = 0
    for dst, c in final_rows:
        row = by_path.get(dst, {}).get("row") or c["row"]
        m = by_path.get(dst, {}).get("m") or c["m"]
        out_dir = gold_dir or os.path.dirname(dst)
        os.makedirs(out_dir, exist_ok=True)
        gold = os.path.join(out_dir, os.path.splitext(os.path.basename(dst))[0] + ".gold.json")
        payload = {
            "image": os.path.abspath(dst),
            "oracle": "classifier_server.classify_batch (in-process, CPU fp32, no HTTP)",
            "model": st["model_used"],
            "thresholds": {
                "MONO_SAT_THRESHOLD": classifier_server.MONO_SAT_THRESHOLD,
                "MONO_SAT_GUARD": classifier_server.MONO_SAT_GUARD,
                "OTHER_SCORE_THRESHOLD": classifier_server.OTHER_SCORE_THRESHOLD,
            },
            # ↓ verdict 字段 verbatim（#77 按 label/note 逐字符、top 逐类、sat 判容差）
            "label": row["label"],
            "score": row["score"],
            "note": row["note"],
            "top": row["top"],
            "sat": row["sat"],
            "screen": m,
        }
        with open(gold, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=1)
        produced += 1
        print(f"[gen_clip_fixtures] {os.path.basename(dst)}: label={row['label']} "
              f"score={row['score']} sat={row['sat']} note={row['note']} "
              f"gap={m['gap_top1_top2']}")
        print(f"[gen_clip_fixtures] SHA256 img  {sha256(dst)}  {dst}")
        print(f"[gen_clip_fixtures] SHA256 gold {sha256(gold)}  {gold}")
    print(f"[gen_clip_fixtures] gold 产出 {produced} 份 → {gold_dir or '<图旁>'}"
          f"（不入库，#77 经 LA_CLIP_IMGS/LA_CLIP_GOLD_DIR 消费）")
    if fix_dir:
        print(f"[gen_clip_fixtures] fixture 目录 → {fix_dir}（不入库）")
    return 0


def _screen(batch: list[str]) -> list[dict]:
    """一批图 → oracle 判定行 + 余量 + 是否越界。坏图跳过并计入 stderr。"""
    pils, kept = [], []
    for p in batch:
        try:
            pils.append(open_pil(p))
            kept.append(p)
        except Exception as e:  # noqa: BLE001 - 占位图/解码失败不毁整轮
            print(f"[gen_clip_fixtures] SKIP unreadable {p}: {type(e).__name__}: {e}",
                  file=sys.stderr)
    if not pils:
        return []
    out = []
    res = classifier_server.classify_batch(pils)
    for p, row in zip(kept, res):
        m = margins(row)
        good, why = eligible(m)
        out.append({"path": p, "row": row, "m": m, "ok": good, "why": why})
    return out


if __name__ == "__main__":
    sys.exit(main())

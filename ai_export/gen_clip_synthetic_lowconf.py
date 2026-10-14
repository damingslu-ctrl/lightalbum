# -*- coding: utf-8 -*-
"""gen_clip_synthetic_lowconf.py —— low_confidence 合成夹具生成器（第四轮 T7 修复波 I-5 入库）。

产物：`LA_CLIP_FIX_DIR/synthetic_lowconf.png` + 同目录 `synthetic_lowconf.png.gold.json`
（I-3 完整 basename 金名契约）。金由 oracle `classifier_server.classify_batch` 进程内
CPU fp32 重跑盘上字节原产（T6 判例：**绝不手写金**）。PNG 与金**永不入库**（R-06），
入库的是这份确定性生成代码（固定 seed、固定裁片池、固定判据）。

为什么走「记档例外」而不是按轴 12 收口 1 重选真实退化图——数学不可行：
    `gen_clip_fixtures.py` 真实照片族的预筛 `GAP_MIN = 0.10`（:57/:172）是 spec §4
    「避开判定边界」的机械化实现，比报告里的 2Δ=0.04 更严，采认它。但 low_confidence
    分支要求 top1 < OTHER_SCORE_THRESHOLD = 0.20，而 8 类 softmax 下
        top2 ≥ (1 − top1)/7 > 0.1143  ⇒  gap = top1 − top2 ≤ (8·top1 − 1)/7 < 0.0857
    即 **score<0.20 ∧ gap>0.10 对任何样本（真实或合成）都不可满足**（T6 实测 160 张
    候选池亦无一例 top1<0.20，clip-gold-margins.txt 12 真图 min gap=0.7838 全部
    top1≥0.8588）。故低置信族对 GAP_MIN 记档例外；例外≠无留量——
    **对 2Δ = 2×0.02 = 0.04（#77 top3 逐对容差 ×2，翻序保序下限）仍须留量**：
    本生成器真路径验收判据 gap_top1_top2 ≥ 0.055，#77 门在金读取处响亮断言
    gold.screen.gap_top1_top2 > 0.04（缺字段或不满足 → FAIL，评审 I-5 修法）。

判定边界余量（spec §4 同族，全部在真路径验收，不满足即 exit 2 响亮弃产）：
    · note == "low_confidence"（回落低置信分支，非 low_saturation/mono_guard）
    · top1 ∈ [0.155, 0.190]：距 0.20 至少 0.010 = 2×score 带(0.005)，不翻普通分支
    · gap_top1_top2 ≥ 0.055：top1/top2 有序性留量 > 2×top3 带(0.02) + 0.015 安全垫
    · |sat−18| ≥ 4 ∧ |sat−45| ≥ 4（低置信分支隐含 sat>18，即 sat ∈ [22.0, 41.0]）

算法（v4 判例收编，来源 `_t7_scratch/gen_synthetic_v4.py`）：以 8 张真实夹具裁片等权
混合为照片统计起点（非对抗扰动，护 torch↔ORT 平价），对 224×224 像素做可微梯度精修：
固定目标类 A = 起点图 argmax，概率目标向量 = A:0.18、其余 7 类均分 0.82/7（此构型
gap≈0.063 恰在可行域内），Adam 最小化 MSE(softmax(100·f·Tᵀ), target)，每步夹回起点
L∞ 半径 0.12 近邻球；命中判据即快照。最终验收一律走 classify_batch 真路径（CPU fp32），
金由盘上 PNG 字节重跑产生。

依赖与运行：需真实夹具裁片池（LA_CLIP_FIX_DIR 内 8 张图，本脚本不入库它们）与 oracle
腿 python（需装有 torch；本机实测 `D:\\lightalbum_ai\\venv_cake\\Scripts\\python.exe`，
torch 2.6.0，CPU fp32 推理，只读使用。`venv_face` 实测**无** torch，不能作本脚本的腿）。

    $env:LA_CLIP_FIX_DIR='<本地夹具目录>'; \
    D:\\lightalbum_ai\\venv_cake\\Scripts\\python.exe ai_export\\gen_clip_synthetic_lowconf.py

模型 pin 走 env CLIP_MODEL/CLIP_PRETRAINED（默认 ViT-B-32/laion2b_s34b_b79k），HF 双缓存
+ HF_HUB_OFFLINE=1 绝不下载；CUDA_VISIBLE_DEVICES='' 在 Windows CRT 下等于删变量，故
进程内把 torch.cuda.is_available 钉 False（gen_clip_fixtures.py 同判例），device!=cpu 硬失败。
"""
from __future__ import annotations

import json
import os
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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np
import torch
torch.manual_seed(0)                            # 起点扰动（判例：_t7_scratch/gen_synthetic_v4.py）
torch.cuda.is_available = lambda: False          # type: ignore[assignment]

import classifier_server                         # noqa: E402 - oracle 真源
from gen_clip_fixtures import margins, sha256, _open_log, open_pil  # noqa: E402
from PIL import Image

FIX_DIR = os.environ.get("LA_CLIP_FIX_DIR", "").strip()
if not FIX_DIR:
    sys.exit("LA_CLIP_FIX_DIR not set (fixture PNG/gold output dir; stays local, never git)")
# 8 类各取一张真实夹具的固定裁片池（RandomState(7) 定裁片坐标 → 全程可复现）。
POOL = [os.path.join(FIX_DIR, n) for n in (
    "D1DAA4C5-E2A7-476E-85B8-D368D2BDF39C.png", "IMG_8530.JPEG", "IMG_2540.PNG",
    "IMG_1981(1).JPG", "IMG_9387.JPG", "IMG_1844(1).JPG", "IMG_3694.JPG", "IMG_2288.JPG")]
MEAN = torch.tensor([0.48145466, 0.4578275, 0.40821073]).view(1, 3, 1, 1)
STD = torch.tensor([0.26862954, 0.26130258, 0.27577711]).view(1, 3, 1, 1)
RAD = 0.12                       # L∞ 近邻球半径：保持照片统计特性（v4 判例）
BATCH, STEPS = 4, 800
T_GTGT = 0.18                    # 目标类概率（构型 gap = 0.18 - 0.82/7 ≈ 0.0629）
GAP_MIN_FAMILY = 0.055           # 真路径验收留量（> 2Δ=0.04 + 0.015 安全垫）
TOP1_LO, TOP1_HI = 0.155, 0.190  # 距 0.20 至少 2×score 带；距 0.135 gap 可达下限留量
SAT_LO, SAT_HI = 22.0, 41.0      # |sat−18|≥4 ∧ |sat−45|≥4（低置信分支隐含 sat>18）


def accept(row: dict, m: dict) -> tuple[bool, list[str]]:
    """真路径验收判据（全部条件逐条给弃因，绝不静默放行）。"""
    bad: list[str] = []
    if row["note"] != "low_confidence":
        bad.append("note=%r != low_confidence" % (row["note"],))
    top1 = float(row["top"][0][1])
    if not (TOP1_LO <= top1 <= TOP1_HI):
        bad.append("top1=%.4f outside [%s,%s]" % (top1, TOP1_LO, TOP1_HI))
    if m["gap_top1_top2"] < GAP_MIN_FAMILY:
        bad.append("gap=%.4f < %.3f (2x0.02 + margin)" % (m["gap_top1_top2"], GAP_MIN_FAMILY))
    if not (SAT_LO <= float(row["sat"]) <= SAT_HI):
        bad.append("sat=%.1f outside [%s,%s]" % (row["sat"], SAT_LO, SAT_HI))
    return (not bad), bad


def main() -> int:
    _open_log()
    missing = [p for p in POOL if not os.path.isfile(p)]
    if missing:
        sys.exit("fixture pool images missing under LA_CLIP_FIX_DIR: %s"
                 % ", ".join(os.path.basename(p) for p in missing))
    st = classifier_server.load_model()
    if not st["loaded"]:
        sys.exit("oracle load_model failed: %s" % st["error"])
    if st["device"] != "cpu":
        sys.exit("CUDA pin failed: device=%s (gold 必须 CPU fp32)" % st["device"])
    model, prep, text_feats = st["model"], st["preprocess"], st["text_feats"]
    print("[synthetic_lowconf] oracle=%s model=%s device=%s"
          % ("classifier_server.classify_batch (in-process)", st["model_used"], st["device"]))

    rng = np.random.RandomState(7)              # 裁片池固定 → 全程可复现
    comps = []
    for p in POOL:
        im = open_pil(p).convert("RGB")
        im.thumbnail((256, 256), Image.LANCZOS)
        s = 128
        x = rng.randint(0, max(1, im.width - s)); y = rng.randint(0, max(1, im.height - s))
        a = np.asarray(im.crop((x, y, x + s, y + s)), dtype=np.float64)
        big = np.zeros((s, s, 3)); big[:a.shape[0], :a.shape[1]] = a
        comps.append(big)
    base = np.clip(sum(comps) / float(len(comps)), 0, 255).astype(np.uint8)
    u0 = torch.tensor(base.astype(np.float32) / 255.0).permute(2, 0, 1).unsqueeze(0)
    u0 = torch.nn.functional.interpolate(u0, size=(224, 224), mode="bicubic",
                                         align_corners=False).clamp(0, 1)
    u0 = (u0.repeat(BATCH, 1, 1, 1) + 0.01 * torch.randn(BATCH, 3, 224, 224)).clamp(0, 1)

    def probs(x: torch.Tensor) -> torch.Tensor:
        f = model.encode_image((x - MEAN) / STD)
        f = f / f.norm(dim=-1, keepdim=True)
        return torch.softmax(100.0 * f @ text_feats.T, dim=-1)

    # 固定目标类 = 起点图 argmax；概率目标向量：A=0.18，其余 7 类均分 → gap≈0.063 可行构型。
    with torch.no_grad():
        a_cls = int(probs(u0[0:1]).argmax(dim=-1).item())
    n_cls = text_feats.shape[0]
    target = torch.full((n_cls,), (1.0 - T_GTGT) / (n_cls - 1))
    target[a_cls] = T_GTGT
    print("[synthetic_lowconf] target class idx=%d p1=%.2f rest=%.5f (gap构型≈%.4f)"
          % (a_cls, T_GTGT, float(target[0] if a_cls != 0 else target[1]),
             T_GTGT - (1.0 - T_GTGT) / (n_cls - 1)))

    u = u0.clone().detach().requires_grad_(True)
    lo, hi = (u0 - RAD).clamp(0, 1), (u0 + RAD).clamp(0, 1)
    opt = torch.optim.Adam([u], lr=0.01)
    snap, snap_stat = None, None
    with torch.inference_mode(False):
        for step in range(STEPS):
            p = probs(u.clamp(0, 1))
            loss = ((p - target) ** 2).sum(dim=-1).mean()
            opt.zero_grad()
            loss.backward()
            opt.step()
            with torch.no_grad():
                u.clamp_(lo, hi)
            if step % 20 == 0 or step == STEPS - 1:
                with torch.no_grad():
                    pc = probs(u.clamp(0, 1))
                    sp, _ = pc.sort(dim=-1, descending=True)
                    p1 = sp[:, 0]; gap = sp[:, 0] - sp[:, 1]
                    good = (p1 >= TOP1_LO) & (p1 <= TOP1_HI) & (gap >= GAP_MIN_FAMILY)
                    hit = torch.nonzero(good).flatten()
                    b = int(hit[gap[hit].argmax()].item()) if hit.numel() else int(p1.argmin().item())
                    print("[synthetic_lowconf] step=%-3d min_loss=%.5f best=(p1=%.4f gap=%.4f) hit=%d"
                          % (step, float(loss), float(sp[b, 0]), float(gap[b]), int(hit.numel() > 0)), flush=True)
                    if hit.numel():
                        snap = u[b].clamp(0, 1).detach().clone()
                        snap_stat = (float(sp[b, 0]), float(gap[b]))
                        break
    if snap is None:
        print("[synthetic_lowconf] FAIL: 优化未达 top1∈[%.3f,%.3f] ∧ gap≥%.3f"
              % (TOP1_LO, TOP1_HI, GAP_MIN_FAMILY), file=sys.stderr)
        return 2

    arr = (snap.permute(1, 2, 0).numpy() * 255 + 0.5).astype(np.uint8)
    dst = os.path.join(FIX_DIR, "synthetic_lowconf.png")
    Image.fromarray(arr, "RGB").save(dst, format="PNG")
    print("[synthetic_lowconf] PNG written (opt p1=%.4f gap=%.4f) -> %s"
          % (snap_stat[0], snap_stat[1], dst))

    # 真路径验收 + 金（盘上字节重跑，oracle 原产）。
    pil = Image.open(dst); pil.load()
    row = classifier_server.classify_batch([pil])[0]
    m = margins(row)
    print("[synthetic_lowconf] REAL label=%s score=%s note=%s top1=%.4f gap=%.4f sat=%.1f"
          % (row["label"], row["score"], row["note"], row["top"][0][1],
             m["gap_top1_top2"], row["sat"]))
    okf, bad = accept(row, m)
    if not okf:
        print("[synthetic_lowconf] FAIL 真路径验收: " + "; ".join(bad), file=sys.stderr)
        return 2
    # I-3 金名契约：完整 basename（含扩展名）+ ".gold.json"，与 gen_clip_fixtures.py 及
    # #77 消费侧（QFileInfo(img).fileName()）同一口径。
    gold = os.path.join(FIX_DIR, os.path.basename(dst) + ".gold.json")
    payload = {
        "image": os.path.abspath(dst),
        "oracle": "classifier_server.classify_batch (in-process, CPU fp32, no HTTP)",
        "model": st["model_used"],
        "thresholds": {
            "MONO_SAT_THRESHOLD": classifier_server.MONO_SAT_THRESHOLD,
            "MONO_SAT_GUARD": classifier_server.MONO_SAT_GUARD,
            "OTHER_SCORE_THRESHOLD": classifier_server.OTHER_SCORE_THRESHOLD,
        },
        "label": row["label"], "score": row["score"], "note": row["note"],
        "top": row["top"], "sat": row["sat"], "screen": m,
    }
    with open(gold, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=1)
    print("[synthetic_lowconf] GOLD %s" % os.path.basename(gold))
    print("[synthetic_lowconf] SHA256 img  %s  %s" % (sha256(dst), dst))
    print("[synthetic_lowconf] SHA256 gold %s  %s" % (sha256(gold), gold))
    return 0


if __name__ == "__main__":
    sys.exit(main())

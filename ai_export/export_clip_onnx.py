# -*- coding: utf-8 -*-
"""export_clip_onnx.py —— CLIP 图像塔 ONNX + prompts.bin 离线导出（oracle，第四轮 T6）。

跑在 `D:\\lightalbum_ai\\venv_cake`（torch 2.6.0+cu124 / open_clip 3.3.0），**同进程直调**，
不起 HTTP。一次会话产出两件随包产物（落 `native\\3rdparty\\clip\\bin\\`）：

1. `clip_vit_b32_img.onnx`：ViT-B-32/laion2b_s34b_b79k 的 `encode_image` 子图，
   opset 17、fp32、eval()，输入定长 `image` = 1×3×224×224，输出 `feat` = 1×512。
   （文本塔不参与导出 —— clipinfer 只跑视觉塔，文本侧走 prompts.bin sidecar。）
2. `prompts.bin`：8 类英文 prompt 的**预计算文本特征** 8×512 float32-LE，
   算法 = classifier_server.py:137-141 verbatim（逐 prompt `encode_text` → 行归一 →
   类内均值 → 再行归一），前置 magic 头、尾部 SHA-256 校验（byte layout 见 README 表）。

模型 pin：本轮**只用 ViT-B-32/laion2b_s34b_b79k**（spec §1 范围外：SigLIP 2 不测；
权重 .incomplete 且 HF 不可达）。换 arch 只需 env `CLIP_MODEL`/`CLIP_PRETRAINED`，
但产物 SHA 随之改变，必须重跑 T7/T9 对拍。

离线红线：`HF_HOME`/`HF_HUB_CACHE` 钉死本机缓存，`HF_HUB_OFFLINE=1` —— 绝不让脚本
去下载（本 host 到 HF 的网络不可靠）。双变量缺一不可：laion 快照实际落在
`hf_cache\\models--laion--…`（与 `D:\\lightalbum_ai\\start_classifier.bat` 同款设置）。

数值红线：`CUDA_VISIBLE_DEVICES=''` 强制 CPU fp32 —— 与随包 clipinfer 的
CPUExecutionProvider 同一条数值路径，杜绝 CUDA autocast fp16 造成的 gold/ONNX 分歧。

运行方式见同目录 README.md。产物入库（LFS-free 直存，随包设计）；两文件 SHA-256
打印在 stdout，钉入 T10 `fetch_ai.ps1` 与本 README。
"""
from __future__ import annotations

import hashlib
import os
import struct
import sys

# ---------------------------------------------------------------- 环境（必须在 import torch/open_clip 之前）
os.environ.setdefault("HF_HOME", r"D:\lightalbum_ai\hf_cache")
os.environ.setdefault("HF_HUB_CACHE", r"D:\lightalbum_ai\hf_cache")
os.environ.setdefault("HF_HUB_OFFLINE", "1")
os.environ.setdefault("TRANSFORMERS_OFFLINE", "1")
os.environ.setdefault("CUDA_VISIBLE_DEVICES", "")

ARCH = os.environ.get("CLIP_MODEL", "ViT-B-32")
PRETRAINED = os.environ.get("CLIP_PRETRAINED", "laion2b_s34b_b79k")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT_DIR = os.environ.get(
    "LA_CLIP_OUT_DIR", os.path.join(REPO, "native", "3rdparty", "clip", "bin"))
ONNX_NAME = "clip_vit_b32_img.onnx"
PROMPTS_NAME = "prompts.bin"

# prompt 用英文 —— CLIP 是英文语料训练的，中文 prompt 会明显掉精度。
# ↓↓ classifier_server.py:31-55 LABELS 表 **逐字复制**（单一事实来源；下方
#    _assert_labels_match_server() 在运行时与服务器模块做全等校验，防漂移）。
LABELS = [
    ('人像',   ['a portrait photograph of a person',
                'a close-up photo of a human face',
                'a photo of people posing for the camera']),
    ('风光',   ['a landscape photograph of mountains',
                'a scenic photo of a forest or lake',
                'a wide-angle nature landscape photo']),
    ('夜景·星空', ['a photo of a starry night sky with the milky way',
                'a long exposure night cityscape photo',
                'an astrophotography image of stars']),
    ('城市·建筑', ['a photograph of city buildings and skyscrapers',
                'an architectural photo of a building facade',
                'an urban street scene with buildings']),
    ('微距·花卉', ['a macro photograph of a flower',
                'an extreme close-up photo of a small object',
                'a shallow depth of field macro shot']),
    ('动物',   ['a photograph of an animal',
                'a wildlife photo of a bird or mammal',
                'a photo of a pet dog or cat']),
    ('美食',   ['a photograph of food on a plate',
                'a food photography shot of a meal',
                'a close-up photo of delicious food']),
    ('黑白',   ['a black and white monochrome photograph',
                'a grayscale photo with no color']),
]

# ---------------------------------------------------------------- prompts.bin byte layout（R-12b 钉死，T7 按此解析）
MAGIC = b"LAPROMPTv1"          # offset 0..9   （10B）
RESERVED = b"\x00\x00"         # offset 10..11 （2B 保留，v1 恒零）
HEAD_LEN = 20                  # 10 + 2 + int32-LE N(12..15) + int32-LE D(16..19)
SHA_LEN = 32                   # 尾部 32B = SHA-256(magic..matrix)，不含自身


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _assert_labels_match_server() -> None:
    """与 oracle 真源的 LABELS 做全等校验（拿得到就校，拿不到只警告，不阻断导出）。"""
    srv = r"D:\lightalbum_ai"
    if srv not in sys.path:
        sys.path.insert(0, srv)
    try:
        import classifier_server  # noqa: F401 - 只需模块常量，import 不触发 uvicorn
    except Exception as e:  # noqa: BLE001 - 独立环境跑导出时允许缺席
        print(f"[export_clip] WARN 未能 import classifier_server 做 LABELS 交叉校验：{e}",
              file=sys.stderr)
        return
    if classifier_server.LABELS != LABELS:
        raise AssertionError("LABELS 与 classifier_server.py 不一致（逐字复制已漂移）")
    print("[export_clip] LABELS == classifier_server.LABELS  （8 类，逐字校验通过）")


def build_text_feats(model, tokenizer, device):
    """classifier_server.py:137-141 文本特征算法 verbatim（行归一→类内均值→再行归一）。"""
    import torch

    feats = []
    with torch.no_grad():
        for _, prompts in LABELS:
            toks = tokenizer(prompts).to(device)
            tf = model.encode_text(toks)
            tf = tf / tf.norm(dim=-1, keepdim=True)
            feats.append(tf.mean(dim=0, keepdim=True))
        text_feats = torch.cat(feats, dim=0)
        text_feats = text_feats / text_feats.norm(dim=-1, keepdim=True)
    return text_feats.float().cpu()


def serialize_prompts(text_feats) -> bytes:
    """8×512 float32-LE 行主序 + magic/保留/N/D 头 + 尾部 SHA（全量内存构造，不落盘）。"""
    import torch

    n, d = int(text_feats.shape[0]), int(text_feats.shape[1])
    if (n, d) != (len(LABELS), 512):
        raise AssertionError(f"text_feats shape 意外：{text_feats.shape}（期望 8×512）")
    if not bool(torch.isfinite(text_feats).all()):
        raise AssertionError("text_feats 含 NaN/Inf，拒绝写盘")
    matrix = text_feats.numpy().astype("<f4").tobytes(order="C")   # float32-LE, row-major
    blob = MAGIC + RESERVED + struct.pack("<ii", n, d) + matrix
    return blob + hashlib.sha256(blob).digest()


def verify_prompts_blob(blob: bytes, text_feats) -> dict:
    """落盘前用 struct 独立复读校验：magic/保留位/N/D/逐元素位等/SHA 尾（不符即抛）。"""
    import numpy as np

    exp_rows = text_feats.numpy().astype("<f4")
    n, d = len(LABELS), 512
    total = HEAD_LEN + n * d * 4 + SHA_LEN
    if len(blob) != total:
        raise AssertionError(f"prompts.bin 长度 {len(blob)} != 期望 {total}")
    if blob[:len(MAGIC)] != MAGIC:
        raise AssertionError("magic 头不符（期望 %r）" % MAGIC)
    if blob[len(MAGIC):HEAD_LEN - 8] != RESERVED:
        raise AssertionError("保留字节非零")
    rn, rd = struct.unpack_from("<ii", blob, 12)
    if (rn, rd) != (n, d):
        raise AssertionError(f"N/D = {rn}/{rd}，期望 {n}/{d}")
    off = HEAD_LEN
    sha_stored = blob[-SHA_LEN:]
    if hashlib.sha256(blob[:-SHA_LEN]).digest() != sha_stored:
        raise AssertionError("尾部 SHA-256 与 magic..matrix 实算不一致")
    max_abs = 0.0
    for i in range(n):
        row = np.array(struct.unpack_from("<%df" % d, blob, off + i * d * 4), dtype="<f4")
        diff = float(np.max(np.abs(row - exp_rows[i])))
        max_abs = max(max_abs, diff)
        norm = float(np.linalg.norm(row.astype("f8")))
        if abs(norm - 1.0) > 1e-5:
            raise AssertionError(f"第 {i} 行未归一（‖row‖={norm}）")
    if max_abs != 0.0:
        raise AssertionError(f"复读位差不为 0（max|Δ|={max_abs}）")
    return {"n": rn, "d": rd, "bytes": len(blob), "sha": sha_stored.hex(),
            "max_abs_reparse_diff": max_abs}


class _Tee:
    """stdout 同时镜像到 UTF-8 日志文件（PowerShell GBK 控制台会把中文打成乱码，
    SHA/arch 行 ASCII 无碍，但 LABELS/tolerance 注释需要可读留档）。env LA_CLIP_LOG 未设则直通。"""

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
    print(f"[export_clip] stdout tee → {path}")


def _ensure_onnx_importable() -> None:
    """torch.onnx.export 需要 python `onnx` 包做序列化；venv_cake 里没有装。

    绝不为此改动 oracle venv（不 pip 进 cake）。退路：把同机已有 onnx 的 venv 的
    site-packages **追加到 sys.path 末尾** —— cake 自有包（numpy/protobuf/PIL/torch）
    永远优先命中，只有 `onnx` 这一颗从对面解析，零版本劫持风险。可用
    LA_CLIP_ONNX_SITE 指定别的 site-packages 目录。
    """
    try:
        import onnx  # noqa: F401
        return
    except ImportError:
        pass
    cands = [p for p in [
        os.environ.get("LA_CLIP_ONNX_SITE"),
        r"D:\lightalbum_ai\venv_face\Lib\site-packages",
        r"D:\lightalbum_ai\venv_mcp\Lib\site-packages",
    ] if p and os.path.isdir(os.path.join(p, "onnx"))]
    for site in cands:
        if site not in sys.path:
            sys.path.append(site)          # 末位追加：不劫持 cake 的任何既有包
        try:
            import onnx
        except ImportError:
            sys.path.remove(site)
            continue
        print(f"[export_clip] onnx {onnx.__version__} 借道 {site}（venv_cake 未装 onnx，"
              f"仅末位追加解析，torch/onnx 数值路径不受影响）")
        return
    raise SystemExit("torch.onnx.export 需要 `onnx` 包：venv_cake 未安装，且未找到可借用的 "
                     "site-packages（设 LA_CLIP_ONNX_SITE=<含 onnx 的 site-packages> 重试）")


def main() -> int:
    import torch
    import open_clip

    _open_log()
    print(f"[export_clip] torch={torch.__version__}  open_clip={open_clip.__version__}  "
          f"device=cpu(fp32)  arch={ARCH}/{PRETRAINED}")
    print(f"[export_clip] HF_HOME={os.environ['HF_HOME']}  "
          f"HF_HUB_CACHE={os.environ['HF_HUB_CACHE']}  "
          f"HF_HUB_OFFLINE={os.environ['HF_HUB_OFFLINE']}")
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR, exist_ok=True)
    onnx_path = os.path.join(OUT_DIR, ONNX_NAME)
    prompts_path = os.path.join(OUT_DIR, PROMPTS_NAME)

    _assert_labels_match_server()

    # ---- 模型（候选只有一个；权重缺失即硬失败，绝不静默兜到别的 arch）
    torch.manual_seed(0)
    model, _, _preprocess = open_clip.create_model_and_transforms(
        ARCH, pretrained=PRETRAINED)
    model = model.to("cpu").eval().float()
    tokenizer = open_clip.get_tokenizer(ARCH)
    print("[export_clip] 模型已加载（fp32 eval CPU）")

    # ---- 1) 图像塔 ONNX（定长 1×3×224×224 → 1×512）
    class ImageTower(torch.nn.Module):
        """只包 encode_image，让 ONNX 图严格等于服务器推理走的子图。"""

        def __init__(self, clip):
            super().__init__()
            self.clip = clip

        def forward(self, image):          # image: 1×3×224×224 已归一 RGB NCHW
            return self.clip.encode_image(image)

    tower = ImageTower(model)
    dummy = torch.randn(1, 3, 224, 224, dtype=torch.float32)
    with torch.no_grad():
        ref = tower(dummy)
    if tuple(ref.shape) != (1, 512):
        raise AssertionError(f"encode_image 输出 shape {tuple(ref.shape)}，期望 (1, 512)")

    _ensure_onnx_importable()
    torch.onnx.export(
        tower, (dummy,), onnx_path,
        opset_version=17,
        input_names=["image"],
        output_names=["feat"],
        do_constant_folding=True,
    )   # torch 2.6 经典（非 dynamo）导出路径，与 opset17 定点；产物由 onnxruntime 复算对拍
    print(f"[export_clip] ONNX 已导出 {onnx_path}  ({os.path.getsize(onnx_path):,} B)")

    # 交叉对拍用参考张量落 %TEMP%（不入库）：同种子同输入，T7/评审可用 onnxruntime 复算。
    import numpy as np

    tmp = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "la_clip_export")
    os.makedirs(tmp, exist_ok=True)
    np.save(os.path.join(tmp, "in_f32.npy"), dummy.numpy())
    np.save(os.path.join(tmp, "ref_feat_f32.npy"), ref.numpy())
    print(f"[export_clip] 对拍参考 → {tmp}\\in_f32.npy / ref_feat_f32.npy（不入库）")

    # ---- 2) prompts.bin（文本塔只跑一次，产物是纯常量表）
    text_feats = build_text_feats(model, tokenizer, "cpu")
    blob = serialize_prompts(text_feats)
    meta = verify_prompts_blob(blob, text_feats)        # 落盘前置校验（失败即抛，不写坏文件）
    with open(prompts_path, "wb") as f:
        f.write(blob)
    disk = open(prompts_path, "rb").read()
    if disk != blob:
        raise AssertionError("prompts.bin 落盘字节与内存 blob 不一致")
    verify_prompts_blob(disk, text_feats)               # 落盘后再校一次（真从磁盘读回的字节）
    print(f"[export_clip] prompts.bin 已写 {prompts_path}  "
          f"(N={meta['n']} D={meta['d']} {meta['bytes']:,} B，struct 复读+SHA 双校验通过)")

    # ---- 3) SHA 清单（钉入 T10 fetch_ai.ps1 + README）
    print(f"[export_clip] LABELS 顺序（prompts.bin 行序，index→label）："
          + " | ".join("%d=%s" % (i, n) for i, (n, _) in enumerate(LABELS)))
    print("[export_clip] prompts 行范数（应=1.000000）: "
          + " ".join("%.6f" % float(torch.linalg.norm(text_feats[i])) for i in range(len(LABELS))))
    for p in (onnx_path, prompts_path):
        print(f"[export_clip] SHA256 {sha256_file(p)}  {os.path.getsize(p):>12,} B  {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

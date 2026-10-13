# -*- coding: utf-8 -*-
"""gen_face_fixtures.py —— face 平价黄金夹具生成器（oracle，第四轮 T2）。

同进程直调 face_server.FaceEngine（不起 HTTP，杜绝端口漂移/服务版本歧义）：
对 env LA_FACE_IMG 指定的若干张本地含脸图逐图跑 detect_image()，
把 faces 数组原样写成 <img>.gold.json（含 5 位小数 embedding，face_server verbatim）。

隐私红线（R-06 / spec §7 #78）：真人脸 fixture 图与 gold JSON 一律不入库——
本脚本本身入库，产物永远只落在 LA_FACE_IMG 指向的本地目录（建议 %TEMP%）。

运行方式见同目录 README.md。
"""
from __future__ import annotations

import hashlib
import json
import os
import sys

# oracle 真源直调（brief Step 2 verbatim：sys.path 注入，不起 HTTP）。
sys.path.insert(0, r"D:\lightalbum_ai")
import cv2  # noqa: E402
from face_server import FaceEngine  # noqa: E402


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def image_list_from_env() -> list[str]:
    raw = os.environ.get("LA_FACE_IMG", "").strip()
    if not raw:
        sys.exit("LA_FACE_IMG not set: semicolon-separated absolute image paths "
                 "(photos containing real faces; outputs stay local, never git)")
    imgs: list[str] = []
    for part in raw.split(";"):
        part = part.strip().strip('"')
        if not part:
            continue
        if os.path.isdir(part):
            for name in sorted(os.listdir(part)):
                if name.lower().endswith((".jpg", ".jpeg", ".png", ".bmp", ".webp")):
                    imgs.append(os.path.join(part, name))
        elif os.path.isfile(part):
            imgs.append(part)
        else:
            print(f"[gen_face_fixtures] WARN skip (not found): {part}", file=sys.stderr)
    if not imgs:
        sys.exit("LA_FACE_IMG resolved to zero usable images")
    return imgs


def main() -> int:
    eng = FaceEngine()
    if not eng.loaded:
        sys.exit("oracle FaceEngine failed to load models (check D:\\lightalbum_ai\\models\\face)")

    paths = image_list_from_env()
    print(f"[gen_face_fixtures] oracle=face_server.FaceEngine (in-process) images={len(paths)}")
    failed = 0
    for p in paths:
        img = cv2.imread(p)                      # BGR，与 face_server 服务路径同构
        if img is None:
            print(f"[gen_face_fixtures] SKIP unreadable: {p}", file=sys.stderr)
            failed += 1
            continue
        faces = eng.detect_image(img)            # bbox/score/kps + embedding(round 5) verbatim
        gold = os.path.splitext(p)[0] + ".gold.json"
        payload = {"image": os.path.abspath(p), "count": len(faces), "faces": faces}
        with open(gold, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=1)
        print(f"[gen_face_fixtures] {p}: faces={len(faces)}")
        print(f"[gen_face_fixtures] SHA256 img  {sha256(p)}  {p}")
        print(f"[gen_face_fixtures] SHA256 gold {sha256(gold)}  {gold}")
    if failed:
        print(f"[gen_face_fixtures] {failed} image(s) unreadable", file=sys.stderr)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

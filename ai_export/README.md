# ai_export —— AI oracle 工具脚本（第四轮：本地 ONNX 子进程平价验证轨）

本目录**只入库脚本**；一切含真人脸的 fixture 图与黄金 JSON **绝不入库**（R-06 隐私红线，
spec §7 #78 fixture 隐私条款）。产物永远只落在 env 指定的本地目录。

## gen_face_fixtures.py —— face 平价黄金夹具生成器（SCRFD/ArcFace oracle）

oracle 真源 = `D:\lightalbum_ai\face_server.py` 的 `FaceEngine`（**同进程直调，不起 HTTP**），
依赖 `D:\lightalbum_ai\venv_face` 内的 onnxruntime / numpy / opencv（模型
`D:\lightalbum_ai\models\face\detection.onnx` + `recognition.onnx`，SHA 见设计规格附录 A）。

运行方式（PowerShell）：

```powershell
# 1) 准备 ≥3 张含真人脸的本地照片（建议放 %TEMP%，别放仓库目录）
$env:LA_FACE_IMG = "C:\Users\<you>\AppData\Local\Temp\la_face_fix\face1.png;...face2.png"
#    也接受目录（半角分号分隔多个条目；目录内自动收 jpg/jpeg/png/bmp/webp）

# 2) 跑 oracle，逐图产出 <img>.gold.json（faces 数组 verbatim，embedding 5 位小数）
& D:\lightalbum_ai\venv_face\Scripts\python.exe `
    D:\Test\workbuddy\2026-09-18-09-00-13\ai_export\gen_face_fixtures.py
```

脚本会逐图打印人脸数与 图片/gold 的 SHA-256 清单（钉死可复现性；SHA 只进本地报告，不进 git）。

## 消费方

- `faceinfer.exe --det-selftest <image> <gold.json>`（T2）：读 gold 的 `faces[]`，
  断言 人脸数精确相等 / 每脸 IoU ≥ 0.98 / kps 五点欧距均值 ≤ 2.0 px，exit 0=PASS。
- #78 `LA_FACE_MODEL_QA`（T3 注册，opt-in）：真 faceinfer.exe 起子进程 REQ/RES 全链路
  跑五列容差（追加 embedding 余弦 ≥ 0.999、score 差 ≤ 0.02）。

## 约定

- 图像字节必须与两侧解码一致：推荐 PNG（无损），避免 JPEG 双解码器（libjpeg-turbo vs
  Qt）像素级分歧——IoU/kps 容差本身可吸收，但 PNG 让 FAIL 归因唯一（算法而非解码）。
- gold 的 faces 顺序 = face_server `_nms` keep 序；selftest 比较核按 IoU 贪心 1:1 匹配，
  不依赖顺序。

#pragma once
// scrfd —— SCRFD-10GF 人脸检测 C++ 移植（oracle 真源 = D:\lightalbum_ai\face_server.py
// FaceEngine.detect()/_anchors/distance2bbox/distance2kps/_nms，逐行翻译，R-P1 判例：
// 平价移植的逐行对应是正确性机制而非重复缺陷）。
// 常量 verbatim：INPUT_SIZE=640, STRIDES{8,16,32}, NUM_ANCHORS=2, DET_THRESH=0.5f,
// NMS_THR=0.4f；NMS 含 +1 像素宽/高约定与 1e-9 分母 epsilon。
#include <cstdint>
#include <string>
#include <vector>

namespace Faceinfer {

struct Det {
    // dets：扁平 N×5，行序 x1,y1,x2,y2,score —— 原图像素坐标（face_server 同义 (N,5)）。
    std::vector<float> dets;
    // kpss：扁平 N×5×2（每脸 5 点 (x,y) 原图像素坐标；无脸时两者皆空）。
    std::vector<float> kpss;
};

// 建立 det ORT 会话（进程一次）。modelPath 为 UTF-8 磁盘路径；成功 true。
// 会话句柄模块内私有——scrfd_detect 按 verbatim 签名 (rgbHWC, w, h) 消费之。
bool scrfd_init(const std::string& modelPath);

// 会话是否可用（READY 语义镜像 EraseRunner::modelOkFor：模型在且会话建好）。
bool scrfd_ready();

// 推理：输入交错 RGB HWC（w×h×3，QImageReader 直出通道序——BGR→RGB 方向已在
// blob 组装处按 face_server `[...,::-1]` 语义钉死，见 scrfd.cpp 内注释块）。
// 无脸返回空 Det；会话未建或 ORT 异常抛 std::runtime_error（调用方转 ERR 帧）。
Det scrfd_detect(const std::vector<uint8_t>& rgbHWC, int w, int h);

} // namespace Faceinfer

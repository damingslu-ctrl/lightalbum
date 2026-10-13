#pragma once
// imgkit —— faceinfer 侧通用图像核（T2 建，T3 对齐复用同族 kernel 语义）。
// 平价对象 = face_server.py 的 cv2.resize(src, (nw, nh))（默认 INTER_LINEAR, uint8）。
// 无 Qt/无 OpenCV 依赖：纯 stdint 实现，半像素中心对齐的确定性双线性（铁律29：宿主无关）。
#include <cstdint>

namespace imgkit {

// 交错 HWC uint8 图缩放：src 为 sw×sh×channels 行主序字节（stride = sw*channels），
// dst 写 dw×dh×channels（stride = dw*channels）。
// 映射式与 cv2 INTER_LINEAR 同族：fx = (x + 0.5) * sw / dw - 0.5，越界钳到 [0, sw-1]
// （等价 borderType=BORDER_REFLECT 在钳位采样区间内的效果，与 inpaint resizeGrayU8 判例一致）；
// 权重用 float 双线性（与 OpenCV uint8 定点 1/2048 量化差 ≤1 LSB，
// 由 §3 容差 IoU≥0.98 / kps≤2px 吸收——spec §3 明示容差存在即为此）。
// channels 取 1..4（灰度/RGB/BGR/RGBA 均可）。dw/dh ≤ 0 或未分配 dst 为未定义行为（调用方守卫）。
void bilin_resize(const uint8_t* src, int sw, int sh,
                  uint8_t* dst, int dw, int dh, int channels);

// 通用 float 采样（T3 warp/对齐复用）：对 sw×sh×channels 交错 HWC float 平面做
// 同 kernel 双线性点采样，结果写 dst[channels]；fx/fy 为源坐标系浮点位置，越界返回 false
// （调用方按 cv2 warpAffine 越界填 0 语义处理）。
bool bilin_sample_f32(const float* src, int sw, int sh, int channels,
                      float fx, float fy, float* dst);

} // namespace imgkit

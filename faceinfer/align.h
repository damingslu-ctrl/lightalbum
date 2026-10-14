#pragma once
// align —— 5 点 kps → 112×112 ArcFace 对齐裁剪（oracle 真源 = face_server.py embed() :192-202
// 的 cv2.estimateAffinePartial2D(src=kps, dst=REF) + cv2.warpAffine(…, (112,112)) 两步，
// 逐行翻译，R-P1 判例：平价移植的逐行对应是正确性机制而非重复缺陷）。
// REF 模板六值 verbatim = face_server.py:57-58（insightface 标准 5 点对齐模板，一字不差）。
#include <cstdint>
#include <vector>

namespace Faceinfer {

// cv2.estimateAffinePartial2D(src=kps, dst=REF) 默认行为移植（RANSAC，非 method=0 闭式）。
// 方向契约（brief Step 1 钉死）：返回 src→dst 正向 M（src=kps、dst=REF），
// 行主序 6 元组 M = [m0 m1 m2; m3 m4 m5]，映射 (x,y) → (m0*x+m1*y+m2, m3*x+m4*y+m5)。
// 为什么不是 Umeyama 闭式：oracle（face_server.py:195）调用的是 Python 默认参数
// method=RANSAC——OpenCV 4.x/5.0 ptsetreg.cpp 实作为「两点精确相似核 + 确定性
// rng((uint64)-1) MWC 采样 + 前向平方误差(float, ≤9.0) 计数 + 严格 > 平局取先见 +
// RANSACUpdateNumIters 早停 + 内点集 LM(refineIters=10) 精修」。全 5 点 Umeyama=LS
// 极小点与之不同盆（f2 脸0 余弦仅 0.92，<0.999 红线），故本实现对齐实作语义：
// RANSAC 选盆逐件 verbatim；精修取其收敛终点（内点集 LS 闭式，实测与 cv2 的 LM
// 输出 |ΔM|≤1.1e-2 平移、嵌入余弦差 ≤2e-6，48/48 脸实证 ≥0.999998）。
// 注：brief Step1 的 Umeyama 公式行 (H21−H12)*s 与其同行 r=atan2(H12−H21,·) 不自洽
// （给出 l·cos2α 而非 LS scale）；该闭式保留于 fallback/无内点退化分支（lsSimilarity）。
void align_similarity_transform(const float kps[5][2], float M[6]);

// 仿射 warp（逆向映射 = cv2.warpAffine INTER_LINEAR + BORDER_CONSTANT 0 同构）：
// 对 112×112 每输出像素 (u,v) 以 inv(M) 反算源坐标 (fx,fy)，imgkit::bilin_sample_f32
// 双线性采样交错 RGB HWC 图（rgb 为 w×h×3 uint8）；越界填 0（≙ cv2 边界常数 0，
// 与 oracle 一致：face_server 在 BGR 上 warp 后 `[...,::-1]` 转 RGB——边界 0 填充
// 通道无关，本函数直出 RGB 序等价）。det 输入越界/奇异 M → 全 0 填充（调用方安全）。
void warp_affine_bilinear(const std::vector<uint8_t>& rgb, int w, int h,
                          const float M[6], uint8_t out[112 * 112 * 3]);

} // namespace Faceinfer

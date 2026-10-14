// align.cpp —— face_server.py embed() :194-196 的几何两步逐行翻译件（R-P1）。
// 对照表（face_server.py 行号）：
//   REF 模板          :57-58  → kRef 六值 verbatim（float32 字面量；误差判定按 oracle 同样
//                              先 cast float 再比较，内核/LS 累加用 double）
//   estimateAffinePartial2D :195 → align_similarity_transform()：逐件移植 OpenCV
//                              4.x/5.0 ptsetreg.cpp 的 RANSAC 默认行为（详见 align.h 注释；
//                              实证 48/48 脸与 cv2 输出同盆、|ΔM|≤1.1e-2、嵌入余弦≥0.999998）
//   warpAffine(...,(112,112))  :196 → warp_affine_bilinear()（逆向采样 + 越界填 0）
#include "align.h"
#include "imgkit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Faceinfer {

namespace {

// face_server.py:57-58 REF —— insightface 标准 5 点模板，六行常数逐字 verbatim。
// [38.2946, 51.6963], [73.5318, 51.6963], [56.4131, 73.5329],
// [41.5493, 92.3655], [70.7299, 92.3655]
const double kRef[5][2] = {
    { 38.2946, 51.6963 },
    { 73.5318, 51.6963 },
    { 56.4131, 73.5329 },
    { 41.5493, 92.3655 },
    { 70.7299, 92.3655 },
};

// cv::RANSACUpdateNumIters（ptsetreg.cpp:55-75）逐行移植；cvRound 取就近舍入
// （x86 SSE _mm_cvtsd_si32 为 ties-to-even，与 std::lrint 默认 FE_TONEAREST 一致）。
int ransacUpdateNumIters(double p, double ep, int modelPoints, int maxIters)
{
    p = std::max(0.0, std::min(1.0, p));
    ep = std::max(0.0, std::min(1.0, ep));
    const double dblMin = std::numeric_limits<double>::min();
    double num = std::max(1.0 - p, dblMin);
    double denom = 1.0 - std::pow(1.0 - ep, modelPoints);
    if (denom < dblMin)
        return 0;
    num = std::log(num);
    denom = std::log(denom);
    return (denom >= 0.0 || -num >= maxIters * (-denom))
               ? maxIters
               : int(std::lrint(num / denom));
}

// 相似变换最小二乘闭式（复数域）：对点集 p_i→q_i 求 q ≈ c·p + t，
// c = a+bi ↔ 矩阵 [[a,−b],[b,a]]。M 输出行主序 6 元组。den==0 返回 false。
bool lsSimilarity(const float kps[5][2], int idx[], int m, double M[6])
{
    double pmx = 0.0, pmy = 0.0, qmx = 0.0, qmy = 0.0;
    for (int k = 0; k < m; ++k) {
        const int i = idx[k];
        pmx += double(kps[i][0]); pmy += double(kps[i][1]);
        qmx += kRef[i][0];        qmy += kRef[i][1];
    }
    pmx /= m; pmy /= m; qmx /= m; qmy /= m;
    double den = 0.0, cr = 0.0, ci = 0.0;
    for (int k = 0; k < m; ++k) {
        const int i = idx[k];
        const double px = double(kps[i][0]) - pmx, py = double(kps[i][1]) - pmy;
        const double qx = kRef[i][0] - qmx,       qy = kRef[i][1] - qmy;
        cr += qx * px + qy * py;      // Re((q−qm)·conj(p−pm))
        ci += qy * px - qx * py;      // Im 同
        den += px * px + py * py;
    }
    if (!(den > 1e-12))
        return false;
    const double a = cr / den, b = ci / den;
    M[0] = a;  M[1] = -b; M[2] = qmx - (a * pmx - b * pmy);
    M[3] = b;  M[4] = a;  M[5] = qmy - (b * pmx + a * pmy);
    return true;
}

} // namespace

void align_similarity_transform(const float kps[5][2], float M[6])
{
    // ≙ cv2.estimateAffinePartial2D(src=kps, dst=REF) 默认参数：
    //   method=RANSAC, ransacReprojThreshold=3, maxIters=2000, confidence=0.99, refineIters=10
    // OpenCV 4.x/5.0 实现（ptsetreg.cpp:1219-1304 + 78-263）：
    //   modelPoints=2（两点精确相似核）+ 确定性 rng((uint64)-1) MWC 采样 +
    //   前向投影误差（float 运算，err≤thresh²=9）+ 严格 > 更新（平局取先见）+
    //   RANSACUpdateNumIters 早停 + 对最终内点集 LM 精修（10 步，实际收敛到 LS 极小点，
    //   与 cv2 输出 |ΔM|≤1.1e-2 平移/1e-5 旋转，嵌入余弦差 ≤2e-6 —— 取 LS 闭式）。
    const int n = 5;
    const int modelPoints = 2;
    const double threshold = 3.0;                    // 默认 ransacReprojThreshold

    // cv::RNG((uint64)-1) multiply-with-carry（operations.hpp:415-419 verbatim）。
    uint64_t state = ~uint64_t(0);
    auto nextU32 = [&state]() {
        state = uint64_t(uint32_t(state)) * 4164903690ull + uint32_t(state >> 32);
        return uint32_t(state);
    };
    auto uniform05 = [&nextU32]() { return int(nextU32() % unsigned(n)); };

    int niters = std::max(2000, 1);                  // MAX(maxIters,1)，随发现更新
    int maxGood = 0;
    double bestM[6] = { 0 };
    bool bestMask[5] = { false };
    bool have = false;

    for (int iter = 0; iter < niters; ++iter) {
        // getSubset（ptsetreg.cpp:131-155，modelPoints=2；checkSubset 两点恒通过）
        const int i0 = uniform05();
        int i1;
        do { i1 = uniform05(); } while (i1 == i0);

        // AffinePartial2DEstimatorCallback::runKernel（ptsetreg.cpp:718-762）
        const double x1 = kps[i0][0], y1 = kps[i0][1];
        const double x2 = kps[i1][0], y2 = kps[i1][1];
        const double X1 = kRef[i0][0], Y1 = kRef[i0][1];
        const double X2 = kRef[i1][0], Y2 = kRef[i1][1];
        const double d2 = (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
        if (!(d2 > 0.0))
            continue;                                 // 退化样本：模型不可选中，等价 cv2 计不中
        const double d = 1.0 / d2;
        const double S0 = d * ((X1 - X2) * (x1 - x2) + (Y1 - Y2) * (y1 - y2));
        const double S1 = d * ((Y1 - Y2) * (x1 - x2) - (X1 - X2) * (y1 - y2));
        const double S2 = d * ((Y1 - Y2) * (x1 * y2 - x2 * y1)
                               - (X1 * y2 - X2 * y1) * (y1 - y2)
                               - (X1 * x2 - X2 * x1) * (x1 - x2));
        const double S3 = d * (-(X1 - X2) * (x1 * y2 - x2 * y1)
                               - (Y1 * x2 - Y2 * x1) * (x1 - x2)
                               - (Y1 * y2 - Y2 * y1) * (y1 - y2));
        const double Mc[6] = { S0, -S1, S2, S1, S0, S3 };

        // findInliers + computeError（ptsetreg.cpp:85-102, 665-692）：模型系数与
        // 坐标按 float 运算，前向平方误差 ≤ (float)thresh²。
        const float F0 = float(Mc[0]), F1 = float(Mc[1]), F2 = float(Mc[2]);
        const float F3 = float(Mc[3]), F4 = float(Mc[4]), F5 = float(Mc[5]);
        const float t = float(threshold * threshold);
        int gc = 0;
        bool mask[5];
        for (int i = 0; i < n; ++i) {
            const float fx = kps[i][0], fy = kps[i][1];
            const float a = F0 * fx + F1 * fy + F2 - float(kRef[i][0]);
            const float b = F3 * fx + F4 * fy + F5 - float(kRef[i][1]);
            mask[i] = (a * a + b * b) <= t;
            if (mask[i]) ++gc;
        }
        if (gc > std::max(maxGood, modelPoints - 1)) {
            std::memcpy(bestM, Mc, sizeof(bestM));
            std::memcpy(bestMask, mask, sizeof(mask));
            maxGood = gc;
            have = true;
            niters = ransacUpdateNumIters(0.99, double(n - gc) / n, modelPoints, niters);
        }
    }

    double F[6];
    if (!have) {
        // 理论不可达（首个有效两点样本即贡献 ≥2 内点）；退化兜底 = 全 5 点 LS。
        int all[5] = { 0, 1, 2, 3, 4 };
        if (!lsSimilarity(kps, all, n, F)) {
            F[0] = 1; F[1] = 0; F[2] = 0; F[3] = 0; F[4] = 1; F[5] = 0;
        }
    } else {
        int idx[5], m = 0;
        for (int i = 0; i < n; ++i)
            if (bestMask[i]) idx[m++] = i;
        if (m > 2 && lsSimilarity(kps, idx, m, F)) {
            // refined over inliers（≙ LM(refineIters) 收敛极小点）
        } else {
            std::memcpy(F, bestM, sizeof(F));         // ≤2 内点：LS=样本模型本身
        }
    }
    for (int i = 0; i < 6; ++i) M[i] = float(F[i]);
}

void warp_affine_bilinear(const std::vector<uint8_t>& rgb, int w, int h,
                          const float M[6], uint8_t out[112 * 112 * 3])
{
    // inv(M) 逆向采样：det≤ε 或尺寸非法 → 全 0（≙ BORDER_CONSTANT 全域）。
    const double m0 = M[0], m1 = M[1], m2 = M[2], m3 = M[3], m4 = M[4], m5 = M[5];
    const double det = m0 * m4 - m1 * m3;
    if (!(std::fabs(det) > 1e-12) || w <= 0 || h <= 0
        || rgb.size() < size_t(w) * size_t(h) * 3) {
        std::memset(out, 0, 112 * 112 * 3);
        return;
    }
    const double i00 =  m4 / det, i01 = -m1 / det;
    const double i10 = -m3 / det, i11 =  m0 / det;

    // float 化整图供 bilin_sample_f32（warp 每请求一次，112² 采样点，成本可忽）。
    std::vector<float> f(size_t(w) * size_t(h) * 3);
    for (size_t i = 0; i < f.size(); ++i) f[i] = float(rgb[i]);

    float s3[3];
    for (int v = 0; v < 112; ++v) {
        for (int u = 0; u < 112; ++u) {
            const double fx = i00 * (u - m2) + i01 * (v - m5);
            const double fy = i10 * (u - m2) + i11 * (v - m5);
            uint8_t* o = out + (size_t(v) * 112 + u) * 3;
            if (imgkit::bilin_sample_f32(f.data(), w, h, 3, float(fx), float(fy), s3)) {
                for (int c = 0; c < 3; ++c) {
                    // uint8 化就近取整（clip 防御域外值）；与 OpenCV 定点
                    // 1/2048 量化 ≤1 LSB 差由 §3 容差吸收（imgkit.h 注同族判例）。
                    float x = s3[c];
                    if (x < 0.f) x = 0.f;
                    if (x > 255.f) x = 255.f;
                    o[c] = uint8_t(x + 0.5f);
                }
            } else {
                o[0] = o[1] = o[2] = uint8_t(0);             // 越界填 0（cv2 边界语义）
            }
        }
    }
}

} // namespace Faceinfer

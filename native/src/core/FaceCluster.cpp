// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "FaceCluster.h"
#include <cmath>

// ---- QA 反例状态（打开→cluster 丢一张，破坏守恒）----
static bool g_forceBroken = false;
void FaceCluster::setForceBrokenForQa(bool on) { g_forceBroken = on; }

// 单位化一份 float32 embedding（长度非 4 倍数或零向量原样返回）。
static QByteArray fcNormalize(const QByteArray& emb)
{
    QByteArray out = emb;
    const int n = out.size() / 4;
    if (n <= 0) return out;
    float* f = reinterpret_cast<float*>(out.data());
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += double(f[i]) * double(f[i]);
    if (s <= 0.0) return out;                     // 零向量：不除，保持原样
    const double inv = 1.0 / std::sqrt(s);
    for (int i = 0; i < n; ++i) f[i] = float(double(f[i]) * inv);
    return out;
}

// 两个已单位化向量的点积（== 余弦）。长度须一致。
static double fcDotN(const float* a, const float* b, int n)
{
    double d = 0.0;
    for (int i = 0; i < n; ++i) d += double(a[i]) * double(b[i]);
    return d;
}

double FaceCluster::cosine(const QByteArray& a, const QByteArray& b)
{
    const int n = a.size() / 4;            // float32 元素数
    if (n <= 0 || b.size() != a.size()) return 0.0;
    const float* fa = reinterpret_cast<const float*>(a.constData());
    const float* fb = reinterpret_cast<const float*>(b.constData());
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; ++i) {
        const double x = double(fa[i]);
        const double y = double(fb[i]);
        dot += x * y;
        na  += x * x;
        nb  += y * y;
    }
    if (na == 0.0 || nb == 0.0) return 0.0;
    return dot / std::sqrt(na * nb);
}

ClusterResult FaceCluster::cluster(const QVector<ClusterInput>& items,
                                   double threshold, int startIndex)
{
    ClusterResult r;
    r.inputCount = items.size();

    // 群首 emb（按簇出现顺序），用于余弦匹配。
    QVector<QByteArray> reps;
    for (const ClusterInput& it : items) {
        int gid = -1;
        for (int g = 0; g < reps.size(); ++g) {
            if (cosine(it.emb, reps[g]) >= threshold) { gid = g; break; }
        }
        if (gid < 0) {                     // 新建簇，本张为群首代表脸
            gid = reps.size();
            reps.append(it.emb);
            ClusterGroup grp;
            grp.id    = gid;
            grp.label = QStringLiteral("人物%1").arg(startIndex + gid);
            r.groups.append(grp);
        }
        r.groups[gid].paths.append(it.path);   // 天然保证每 path 恰一次
    }

    // 守恒计数
    int out = 0;
    for (const ClusterGroup& g : r.groups) out += g.paths.size();
    r.outputCount = out;

    // QA 反例：从首个非空簇移除首张，使 outputCount = inputCount - 1 → 守恒断言转红。
    if (g_forceBroken) {
        for (ClusterGroup& g : r.groups) {
            if (!g.paths.isEmpty()) {
                g.paths.removeFirst();
                --r.outputCount;
                break;
            }
        }
    }
    return r;
}

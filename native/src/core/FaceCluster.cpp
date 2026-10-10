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

// 一遍质心精炼：重算每簇质心（成员单位向量均值再单位化）→ 每 item 重分配到最近
// 质心（>=threshold），否则新建 singleton 簇。返回本轮 reassignment 数。
static int fcRefine(const QVector<QByteArray>& norm, int dim, double threshold,
                    QVector<int>& assign, QVector<QByteArray>& cents)
{
    const int g0 = cents.size();
    QVector<QVector<double>> sum(g0, QVector<double>(dim, 0.0));
    QVector<int> cnt(g0, 0);
    for (int i = 0; i < norm.size(); ++i) {
        const int g = assign[i];
        if (g < 0 || g >= g0) continue;
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        for (int d = 0; d < dim; ++d) sum[g][d] += f[d];
        ++cnt[g];
    }
    QVector<QByteArray> newCent(g0);
    for (int g = 0; g < g0; ++g) {
        if (cnt[g] == 0) { newCent[g] = cents[g]; continue; }
        QByteArray b(dim * 4, '\0');
        float* o = reinterpret_cast<float*>(b.data());
        double s = 0.0;
        for (int d = 0; d < dim; ++d) { const double v = sum[g][d] / cnt[g]; o[d] = float(v); s += v * v; }
        const double inv = s > 0.0 ? 1.0 / std::sqrt(s) : 0.0;
        for (int d = 0; d < dim; ++d) o[d] = float(o[d] * inv);
        newCent[g] = b;
    }
    cents = newCent;
    int changed = 0;
    for (int i = 0; i < norm.size(); ++i) {
        if (assign[i] == -2) continue;              // 维度非法：singleton 固定，不参与
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        int best = -1; double bestc = -1.0;
        for (int g = 0; g < cents.size(); ++g) {
            const double c = fcDotN(f, reinterpret_cast<const float*>(cents[g].constData()), dim);
            if (c >= threshold && c > bestc) { bestc = c; best = g; }
        }
        if (best < 0) { best = cents.size(); cents.append(norm[i]); }
        if (assign[i] != best) { assign[i] = best; ++changed; }
    }
    return changed;
}

ClusterResult FaceCluster::cluster(const QVector<ClusterInput>& items,
                                   double threshold, int startIndex)
{
    ClusterResult r;
    r.inputCount = items.size();
    const int dim = items.isEmpty() ? 0 : items.at(0).emb.size() / 4;

    QVector<QByteArray> norm;
    norm.reserve(items.size());
    for (const ClusterInput& it : items) norm.append(fcNormalize(it.emb));

    // ---- 种子：贪心 leader（与旧行为一致，仅用于给精环一个好起点）----
    QVector<int> assign(items.size(), -2);          // -2 = 维度非法 singleton
    QVector<QByteArray> cents;
    for (int i = 0; i < items.size(); ++i) {
        if (dim <= 0 || norm[i].size() / 4 != dim) { assign[i] = -2; continue; }
        const float* f = reinterpret_cast<const float*>(norm[i].constData());
        int best = -1; double bestc = -1.0;
        for (int g = 0; g < cents.size(); ++g) {
            const double c = fcDotN(f, reinterpret_cast<const float*>(cents[g].constData()), dim);
            if (c >= threshold && c > bestc) { bestc = c; best = g; }
        }
        if (best < 0) { best = cents.size(); cents.append(norm[i]); }
        assign[i] = best;
    }

    // ---- 精炼：≤3 轮，收敛早停 ----
    for (int iter = 0; iter < 3; ++iter)
        if (fcRefine(norm, dim, threshold, assign, cents) == 0) break;

    // ---- 落簇 + 守恒 ----
    const int nGroups = cents.size();
    for (int g = 0; g < nGroups; ++g) {
        ClusterGroup grp; grp.id = g;
        grp.label = QStringLiteral("人物%1").arg(startIndex + g);
        r.groups.append(grp);
    }
    // 维度非法项：每项自成一簇（追加在末尾，label 顺延），保持旧「非法 emb→新簇」语义。
    int extra = 0;
    for (int i = 0; i < items.size(); ++i) {
        int g = assign[i];
        if (g == -2) g = nGroups + (extra++);
        while (r.groups.size() <= g) {
            ClusterGroup grp; grp.id = r.groups.size();
            grp.label = QStringLiteral("人物%1").arg(startIndex + grp.id);
            r.groups.append(grp);
        }
        r.groups[g].paths.append(items.at(i).path);   // 天然保证每 path 恰一次
    }

    int out = 0;
    for (const ClusterGroup& g : r.groups) out += g.paths.size();
    r.outputCount = out;

    // QA 反例 1（原有语义）：从首个非空簇丢一张 → 守恒转红。
    if (g_forceBroken) {
        for (ClusterGroup& g : r.groups) {
            if (!g.paths.isEmpty()) { g.paths.removeFirst(); --r.outputCount; break; }
        }
    }
    return r;
}

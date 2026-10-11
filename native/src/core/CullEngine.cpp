// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
//
// This file contains work derived from ai-photo-manager
//   https://github.com/laoziocean-code/ai-photo-manager
//   MIT License, Copyright (c) 2026 AI Photography Butler Contributors
//
// Derived parts absorbed here:
//   - perceptual hash (pHash): grayscale -> (hashSize*4)^2 -> 2D DCT-II ->
//     low-frequency hashSize^2 block compared against its median
//   - the DCT-II transform itself (F = C * A * C^T)
//   - near-duplicate grouping by Hamming distance over pHash
//
// Full attribution: docs/THIRD_PARTY_NOTICE.md, shipped inside the release
// dist as THIRD_PARTY-NOTICE.md. Removing this header is a contract violation
// and fails the LA_RELEASE_QA gate (contract C6).
//
// Everything else in this file (Qt/native port, Laplacian blur, exposure,
// scoring, RAW exemption, fail-open policy) is original LightAlbum work.
#include "CullEngine.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <vector>
#include <cmath>

static const double PI = 3.14159265358979323846;

// C++17 安全的 64-bit 种群计数（不用 std::popcount，后者需 C++20）
static int popcount64(quint64 x)
{
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return int((x * 0x0101010101010101ULL) >> 56);
}

// —— 模糊：四邻域 Laplacian 方差（与 Python 无 cv2 分支口径一致）——
// v = a[y,x+1] + a[y,x-1] + a[y+1,x] + a[y-1,x] - 4*a[y,x]
double CullEngine::blurScore(const QImage& gray)
{
    if (gray.format() != QImage::Format_Grayscale8)
        return 999.0;
    const int w = gray.width(), h = gray.height();
    if (w < 3 || h < 3) return 999.0;
    double sum = 0, sum2 = 0;
    long cnt = 0;
    for (int y = 1; y < h - 1; ++y) {
        const uchar* up   = gray.constScanLine(y - 1);
        const uchar* cur  = gray.constScanLine(y);
        const uchar* down = gray.constScanLine(y + 1);
        for (int x = 1; x < w - 1; ++x) {
            const double v = double(up[x + 1]) + double(up[x - 1])
                           + double(down[x])  + double(up[x])
                           - 4.0 * double(cur[x]);
            sum += v; sum2 += v * v; ++cnt;
        }
    }
    if (cnt == 0) return 999.0;
    const double mean = sum / cnt;
    const double var = sum2 / cnt - mean * mean;
    return var > 0 ? var : 0.0;
}

void CullEngine::exposureStats(const QImage& gray, double& mean, double& under,
                               double& over, QString& verdict)
{
    mean = under = over = 0; verdict = QCoreApplication::translate("LightAlbum", "未知");
    if (gray.format() != QImage::Format_Grayscale8) return;
    const int w = gray.width(), h = gray.height();
    if (w == 0 || h == 0) return;
    qulonglong s = 0;
    long u = 0, o = 0;
    const int n = w * h;
    for (int y = 0; y < h; ++y) {
        const uchar* p = gray.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int v = p[x];
            s += v;
            if (v < 25) ++u;
            else if (v > 230) ++o;
        }
    }
    mean = double(s) / n;
    under = double(u) / n;
    over  = double(o) / n;
    if (under > 0.45 && mean < 70)       verdict = QCoreApplication::translate("LightAlbum", "严重欠曝");
    else if (over > 0.45 && mean > 185)  verdict = QCoreApplication::translate("LightAlbum", "严重过曝");
    else if (under > 0.25)               verdict = QCoreApplication::translate("LightAlbum", "欠曝");
    else if (over > 0.25)                verdict = QCoreApplication::translate("LightAlbum", "过曝");
    else                                 verdict = QCoreApplication::translate("LightAlbum", "正常");
}

// —— 感知哈希：灰度 → (hashSize*4)² → 2D DCT-II → 低频 hashSize² 与中位数比较 ——
// 与 imagehash.phash 算法一致；hashSize=8 → 64 bit 正好装入 quint64。
quint64 CullEngine::computePhash(const QImage& src, int hashSize)
{
    if (src.isNull()) return 0;
    const int N = hashSize * 4;
    QImage g = src.convertToFormat(QImage::Format_Grayscale8)
                  .scaled(N, N, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (g.isNull()) return 0;
    // 构建 N×N 浮点矩阵
    std::vector<std::vector<double>> A(N, std::vector<double>(N));
    for (int y = 0; y < N; ++y) {
        const uchar* p = g.constScanLine(y);
        for (int x = 0; x < N; ++x) A[y][x] = p[x];
    }
    // 余弦基 C[k][i] = 2*cos(pi*k*(2i+1)/(2N))
    std::vector<std::vector<double>> C(N, std::vector<double>(N));
    const double k = PI / (2.0 * N);
    for (int ki = 0; ki < N; ++ki)
        for (int i = 0; i < N; ++i)
            C[ki][i] = 2.0 * std::cos(double(ki) * double(2 * i + 1) * k);
    // DCT-II：F = C * A * C^T
    std::vector<std::vector<double>> tmp(N, std::vector<double>(N, 0.0));
    for (int ki = 0; ki < N; ++ki)
        for (int j = 0; j < N; ++j) {
            double s = 0;
            for (int i = 0; i < N; ++i) s += C[ki][i] * A[i][j];
            tmp[ki][j] = s;
        }
    std::vector<std::vector<double>> F(N, std::vector<double>(N, 0.0));
    for (int ki = 0; ki < N; ++ki)
    for (int li = 0; li < N; ++li) {
        double s = 0;
        for (int j = 0; j < N; ++j) s += tmp[ki][j] * C[j][li]; // F = tmp · C^T
        F[ki][li] = s;
    }
    // 取低频 hashSize×hashSize，比较中位数
    std::vector<double> low;
    low.reserve(hashSize * hashSize);
    for (int y = 0; y < hashSize; ++y)
        for (int x = 0; x < hashSize; ++x)
            low.push_back(F[y][x]);
    std::vector<double> sorted = low;
    std::sort(sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2];
    quint64 bits = 0;
    for (size_t i = 0; i < low.size(); ++i)
        if (low[i] > med) bits |= (quint64(1) << i);
    return bits;
}

double CullEngine::sharpnessScore(double var)
{
    if (var < 0) return -1;            // None
    if (var <= 0) return 0;
    if (var >= 600) return 100.0;
    const double s = 100.0 * (std::log10(var / 20.0 + 1.0)
                              / std::log10(600.0 / 20.0 + 1.0));
    return std::max(0.0, std::min(100.0, s));
}

double CullEngine::lightingScore(const QString& verdict)
{
    // 与 Python _LIGHTING_BY_VERDICT 一致
    static const QHash<QString, double> m = {
        {QCoreApplication::translate("LightAlbum", "正常"), 85.0}, {QCoreApplication::translate("LightAlbum", "欠曝"), 60.0},
        {QCoreApplication::translate("LightAlbum", "过曝"), 60.0}, {QCoreApplication::translate("LightAlbum", "严重欠曝"), 25.0},
        {QCoreApplication::translate("LightAlbum", "严重过曝"), 25.0},
    };
    return m.value(verdict, -1.0);    // 未知 → -1 (None)
}

double CullEngine::computeTotal(double sharpness, double lighting, double& coverage)
{
    // 权重（WEIGHTS 子集，本地可测）
    const double wSharp = 0.12, wLight = 0.15;
    double num = 0, den = 0;
    if (sharpness >= 0) { num += sharpness * wSharp; den += wSharp; }
    if (lighting  >= 0) { num += lighting  * wLight; den += wLight; }
    coverage = den / (wSharp + wLight);
    if (den <= 0) return 0.0;
    return num / den;
}

int CullEngine::hamming(quint64 a, quint64 b)
{
    return popcount64(a ^ b);
}

static QString gradeOf(double total)
{
    if (total >= 90) return QStringLiteral("S");
    if (total >= 80) return QStringLiteral("A");
    if (total >= 70) return QStringLiteral("B");
    if (total >= 60) return QStringLiteral("C");
    return QStringLiteral("D");
}

CullReport CullEngine::analyze(const QVector<CullInput>& inputs,
                                const CullOptions& opts, ProgressFn progress)
{
    CullReport rep;
    rep.total = inputs.size();
    QVector<CullItem> items;
    items.reserve(inputs.size());
    const int n = inputs.size();
    for (int idx = 0; idx < n; ++idx) {
        const CullInput& in = inputs[idx];
        CullItem it;
        it.thumbPath = in.thumbPath;
        it.category = in.category;   // 透传场景分类，供选片页 chips 筛选
        it.mediaType = in.isVideo ? QStringLiteral("video") : QStringLiteral("image");
        it.isVideo = in.isVideo;
        it.durSec = in.durSec;
        it.priv   = in.priv;         // 2.33.0 透传私密标记，供选片页锁定隐藏

        // MD5（文件字节；解码失败时仍能算）
        {
            QFile f(in.thumbPath);
            if (f.open(QIODevice::ReadOnly)) {
                QCryptographicHash h(QCryptographicHash::Md5);
                if (h.addData(&f)) it.md5 = QString::fromLatin1(h.result().toHex());
            }
        }

        QImage img(in.thumbPath);
        if (img.isNull()) {
            // 解码失败：放行（失败即放行）
            it.blur = 999.0; it.isBlurry = false;
            it.exposureVerdict = QCoreApplication::translate("LightAlbum", "未知");
            it.sharpness = -1; it.lighting = -1;
            it.resolutionOk = true;
            it.status = QStringLiteral("candidate");
            rep.unknownCount++;
        } else {
            QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
            // 最长边缩到 512
            int m = std::max(gray.width(), gray.height());
            if (m > 512) {
                double s = 512.0 / m;
                gray = gray.scaled(int(gray.width() * s), int(gray.height() * s),
                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            it.blur = blurScore(gray);
            it.isBlurry = it.blur < opts.blurThreshold;
            double mean, under, over;
            QString verdict;
            exposureStats(gray, mean, under, over, verdict);
            it.exposureMean = mean; it.exposureVerdict = verdict;
            it.lighting = lightingScore(verdict);
            it.sharpness = sharpnessScore(it.blur);
            it.phash = computePhash(img, opts.hashSize);
        }

        // 分辨率（用原始宽高；dim 解析不到则放行）
        if (in.origW > 0 && in.origH > 0) {
            double mp = double(in.origW * in.origH) / 1e6;
            it.resolutionOk = (mp >= opts.minMp) && (std::min(in.origW, in.origH) >= opts.minDim);
        } else {
            it.resolutionOk = true;
        }

        items.append(it);
        if (progress) progress(idx + 1, n, QCoreApplication::translate("LightAlbum", "预处理 %1/%2").arg(idx + 1).arg(n));
    }

    // —— 精确去重（MD5）——
    QHash<QString, QVector<int>> byMd5;
    for (int i = 0; i < items.size(); ++i)
        if (!items[i].md5.isEmpty()) byMd5[items[i].md5].append(i);
    int grpNo = 0;
    for (auto it = byMd5.begin(); it != byMd5.end(); ++it) {
        if (it.value().size() < 2) continue;
        ++grpNo;
        // 保留最清晰者
        int keep = it.value().first();
        for (int j : it.value())
            if (items[j].sharpness > items[keep].sharpness) keep = j;
        for (int j : it.value()) {
            if (j == keep) { items[j].dupGroup = grpNo; continue; }
            items[j].status = QStringLiteral("rejected");
            items[j].rejectReason = QCoreApplication::translate("LightAlbum", "完全重复（MD5 相同）");
            items[j].dupGroup = grpNo;
            items[j].dupOf = items[keep].thumbPath;
            rep.dupCount++;
        }
    }

    // —— 相似去重（pHash Hamming）——
    if (opts.dedupThreshold >= 0) {
        QVector<int> valid;
        for (int i = 0; i < items.size(); ++i)
            if (items[i].status != QStringLiteral("rejected") && items[i].phash != 0)
                valid.append(i);
        QSet<int> used;
        grpNo = 1000;
        for (int a = 0; a < valid.size(); ++a) {
            int i = valid[a];
            if (used.contains(i)) continue;
            QVector<int> grp; grp.append(i); used.insert(i);
            for (int b = a + 1; b < valid.size(); ++b) {
                int j = valid[b];
                if (used.contains(j)) continue;
                if (hamming(items[i].phash, items[j].phash) <= opts.dedupThreshold) {
                    grp.append(j); used.insert(j);
                }
            }
            if (grp.size() < 2) continue;
            ++grpNo;
            int keep = grp.first();
            for (int j : grp)
                if (items[j].sharpness > items[keep].sharpness) keep = j;
            for (int j : grp) {
                if (j == keep) { items[j].dupGroup = grpNo; continue; }
                items[j].status = QStringLiteral("rejected");
                items[j].rejectReason = QCoreApplication::translate("LightAlbum", "高度相似（感知哈希）");
                items[j].dupGroup = grpNo;
                items[j].dupOf = items[keep].thumbPath;
                rep.dupCount++;
            }
        }
    }

    // —— 质量淘汰 ——
    for (int i = 0; i < items.size(); ++i) {
        CullItem& it = items[i];
        if (it.status == QStringLiteral("rejected")) continue;
        if (inputs[i].exemptFromQualityCull()) {
            // RAW / 视频豁免：只查分辨率
            // （2.29.0：isVideo 与 isRaw 共享本分支，判据收敛到 CullInput 单一口径）
            if (!it.resolutionOk) {
                it.status = QStringLiteral("rejected");
                it.rejectReason = QCoreApplication::translate("LightAlbum", "分辨率过低");
            } else it.status = QStringLiteral("candidate");
            continue;
        }
        if (it.isBlurry) {
            it.status = QStringLiteral("rejected");
            it.rejectReason = QCoreApplication::translate("LightAlbum", "疑似模糊/对焦不实");
            rep.blurryCount++;
            continue;
        }
        if (it.exposureVerdict == QCoreApplication::translate("LightAlbum", "严重欠曝") ||
            it.exposureVerdict == QCoreApplication::translate("LightAlbum", "严重过曝")) {
            it.status = QStringLiteral("rejected");
            it.rejectReason = it.exposureVerdict;
            rep.exposureCount++;
            continue;
        }
        if (!it.resolutionOk) {
            it.status = QStringLiteral("rejected");
            it.rejectReason = QCoreApplication::translate("LightAlbum", "分辨率过低");
            continue;
        }
        it.status = QStringLiteral("candidate");
    }

    // —— 候选评分 + TopN ——
    QVector<CullItem> candidates;
    for (auto& it : items) {
        if (it.status == QStringLiteral("candidate")) {
            double cov;
            it.total = computeTotal(it.sharpness, it.lighting, cov);
            it.coverage = cov;
            it.grade = gradeOf(it.total);
            candidates.append(it);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const CullItem& a, const CullItem& b) { return a.total > b.total; });
    const int top = qMin(opts.topN, candidates.size());
    for (int i = 0; i < top; ++i) rep.topItems.append(candidates[i]);

    // —— 汇总去重组 ——
    QHash<QString, CullDupGroup> byKept;
    for (auto& it : items) {
        if (it.dupOf.isEmpty()) continue;
        if (it.total <= 0) {          // 去重被拒项未参与候选评分 → 补算（维度预处理已算好）
            double cov;
            it.total = computeTotal(it.sharpness, it.lighting, cov);
            it.coverage = cov;
            it.grade = gradeOf(it.total);
        }
        rep.dupItems.append(it);      // P25 对决模式素材
        auto g = byKept.find(it.dupOf);
        if (g == byKept.end()) {
            CullDupGroup dg; dg.keptThumb = it.dupOf; dg.reason = it.rejectReason;
            byKept.insert(it.dupOf, dg);
            g = byKept.find(it.dupOf);
        }
        g.value().similar.append(it.thumbPath);
    }
    rep.dupGroups = byKept.values();

    rep.candidateCount = candidates.size();
    rep.rejectedCount = items.size() - candidates.size();
    return rep;
}

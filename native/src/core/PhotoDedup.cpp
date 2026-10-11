// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "PhotoDedup.h"
#include <QImage>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSet>
#include <QHash>
#include <algorithm>

static bool g_forceDoubleCount = false;

void PhotoDedup::setForceDoubleCountForQa(bool on) { g_forceDoubleCount = on; }

QVector<DedupDesc> PhotoDedup::describe(const QVector<DedupPhoto>& photos,
                                        const QString& thumbsDir,
                                        bool exactMd5,
                                        const std::function<void(int)>& progress,
                                        const std::function<bool()>& cancel)
{
    QVector<DedupDesc> descs;
    descs.reserve(photos.size());
    for (int i = 0; i < photos.size(); ++i) {
        if (cancel && cancel()) break;      // 提前停下：调用方须丢弃残缺结果
        const DedupPhoto& p = photos[i];
        DedupDesc d;
        d.path   = p.path;
        d.pixels = qint64(p.origW) * qint64(p.origH);
        const QString abs = QDir(thumbsDir).filePath(p.thumbFile);
        QImage img(abs);
        if (!img.isNull()) {                // 失败即放行：ok=false 的照片不参与分组
            d.ok = true;
            if (exactMd5) {
                QFile f(abs);
                if (f.open(QIODevice::ReadOnly))
                    d.md5 = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
            }
            d.phash = CullEngine::computePhash(img, 8);
        }
        descs.append(d);
        if (progress) progress(int(descs.size()));
    }
    return descs;
}

QVector<DupGroup> PhotoDedup::group(const QVector<DedupDesc>& descs, int phashThreshold)
{
    QVector<DupGroup> out;
    QSet<int> used;   // 已归组的下标（含保留者与被判重复者）
    const int n = descs.size();

    // —— 第 1 步：MD5 精确去重（字节完全相同）——
    // md5 为空即「未算/不可读」，天然不入桶（exactMd5=false 时全程无精确组）。
    {
        QHash<QByteArray, QVector<int>> byMd5;
        for (int i = 0; i < n; ++i) {
            if (!descs[i].ok || descs[i].md5.isEmpty()) continue;
            byMd5[descs[i].md5].append(i);
        }
        for (auto it = byMd5.begin(); it != byMd5.end(); ++it) {
            if (it.value().size() < 2) continue;
            int keep = it.value().first();
            for (int j : it.value())
                if (descs[j].pixels > descs[keep].pixels) keep = j;   // 保留最高分辨率
            DupGroup g;
            g.keptPath   = descs[keep].path;
            g.keptPixels = descs[keep].pixels;
            g.reason     = QCoreApplication::translate("LightAlbum", "完全重复（MD5 相同）");
            for (int j : it.value()) {
                if (j == keep) continue;
                g.dupPaths.append(descs[j].path);
                used.insert(j);
            }
            used.insert(keep);
            out.append(g);
        }
    }

    // —— 第 2 步：pHash 近重复分组（Hamming ≤ 阈值）——
    // 只在「可用候选」上两两比较（2.33.0）：候选按原下标升序，贪心首匹配语义与
    // 旧版逐格扫描完全一致，只是不再为不可用/已归组的行反复做集合查询。
    if (phashThreshold >= 0) {
        QVector<int> cand;
        cand.reserve(n);
        for (int i = 0; i < n; ++i)
            if (descs[i].ok && descs[i].phash != 0) cand.append(i);

        for (int k = 0; k < cand.size(); ++k) {
            const int a = cand[k];
            if (used.contains(a)) continue;
            QVector<int> grp; grp.append(a); used.insert(a);
            for (int m = k + 1; m < cand.size(); ++m) {
                const int b = cand[m];
                if (used.contains(b)) continue;
                if (CullEngine::hamming(descs[a].phash, descs[b].phash) <= phashThreshold) {
                    grp.append(b); used.insert(b);
                }
            }
            if (grp.size() < 2) continue;
            int keep = grp.first();
            for (int j : grp)
                if (descs[j].pixels > descs[keep].pixels) keep = j;   // 保留最高分辨率
            DupGroup g;
            g.keptPath   = descs[keep].path;
            g.keptPixels = descs[keep].pixels;
            g.reason     = QCoreApplication::translate("LightAlbum", "高度相似（感知哈希）");
            for (int j : grp) {
                if (j == keep) continue;
                g.dupPaths.append(descs[j].path);
            }
            out.append(g);
        }
    }

    // —— QA 反例：故意破坏守恒（同一张出现两次）——
    if (g_forceDoubleCount && out.size() >= 2)
        out[1].dupPaths.append(out[0].keptPath);

    return out;
}

QVector<DupGroup> PhotoDedup::findGroups(const QVector<DedupPhoto>& photos,
                                         const QString& thumbsDir,
                                         int phashThreshold, bool exactMd5)
{
    return group(describe(photos, thumbsDir, exactMd5), phashThreshold);
}

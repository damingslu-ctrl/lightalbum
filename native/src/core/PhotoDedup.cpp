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

namespace {
struct Rec {
    int       idx = -1;
    QString   path;
    QByteArray md5;      // 空 = 未算（exactMd5=false 或无文件）
    quint64   phash = 0;
    qint64    pixels = 0;
    bool      ok = false;   // 缩略图可用
};
} // namespace

QVector<DupGroup> PhotoDedup::findGroups(const QVector<DedupPhoto>& photos,
                                         const QString& thumbsDir,
                                         int phashThreshold, bool exactMd5)
{
    QVector<DupGroup> out;
    QVector<Rec> recs;
    recs.reserve(photos.size());

    // —— 预处理：为每张照片取缩略图，算 MD5 + pHash ——
    for (int i = 0; i < photos.size(); ++i) {
        const DedupPhoto& p = photos[i];
        Rec r;
        r.idx    = i;
        r.path   = p.path;
        r.pixels = qint64(p.origW) * qint64(p.origH);
        const QString abs = QDir(thumbsDir).filePath(p.thumbFile);
        QImage img(abs);
        if (img.isNull()) { recs.append(r); continue; }   // 失败即放行
        r.ok = true;
        if (exactMd5) {
            QFile f(abs);
            if (f.open(QIODevice::ReadOnly))
                r.md5 = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        }
        r.phash = CullEngine::computePhash(img, 8);
        recs.append(r);
    }

    QSet<int> used;   // 已归组的下标（含保留者与被判重复者）

    // —— 第 1 步：MD5 精确去重（字节完全相同）——
    if (exactMd5) {
        QHash<QByteArray, QVector<int>> byMd5;
        for (int i = 0; i < recs.size(); ++i) {
            if (!recs[i].ok || recs[i].md5.isEmpty()) continue;
            byMd5[recs[i].md5].append(i);
        }
        for (auto it = byMd5.begin(); it != byMd5.end(); ++it) {
            if (it.value().size() < 2) continue;
            int keep = it.value().first();
            for (int j : it.value())
                if (recs[j].pixels > recs[keep].pixels) keep = j;   // 保留最高分辨率
            DupGroup g;
            g.keptPath   = recs[keep].path;
            g.keptPixels = recs[keep].pixels;
            g.reason     = QCoreApplication::translate("LightAlbum", "完全重复（MD5 相同）");
            for (int j : it.value()) {
                if (j == keep) continue;
                g.dupPaths.append(recs[j].path);
                used.insert(j);
            }
            used.insert(keep);
            out.append(g);
        }
    }

    // —— 第 2 步：pHash 近重复分组（Hamming ≤ 阈值）——
    if (phashThreshold >= 0) {
        int grpNo = 1000;
        for (int a = 0; a < recs.size(); ++a) {
            if (used.contains(a) || !recs[a].ok || recs[a].phash == 0) continue;
            QVector<int> grp; grp.append(a); used.insert(a);
            for (int b = a + 1; b < recs.size(); ++b) {
                if (used.contains(b) || !recs[b].ok || recs[b].phash == 0) continue;
                if (CullEngine::hamming(recs[a].phash, recs[b].phash) <= phashThreshold) {
                    grp.append(b); used.insert(b);
                }
            }
            if (grp.size() < 2) continue;
            ++grpNo;
            int keep = grp.first();
            for (int j : grp)
                if (recs[j].pixels > recs[keep].pixels) keep = j;   // 保留最高分辨率
            DupGroup g;
            g.keptPath   = recs[keep].path;
            g.keptPixels = recs[keep].pixels;
            g.reason     = QCoreApplication::translate("LightAlbum", "高度相似（感知哈希）");
            for (int j : grp) {
                if (j == keep) continue;
                g.dupPaths.append(recs[j].path);
            }
            out.append(g);
        }
    }

    // —— QA 反例：故意破坏守恒（同一张出现两次）——
    if (g_forceDoubleCount && out.size() >= 2)
        out[1].dupPaths.append(out[0].keptPath);

    return out;
}

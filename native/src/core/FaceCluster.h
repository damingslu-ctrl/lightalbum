// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·人物自动聚类（方向 C 第四步的下一步）：阈值余弦聚类纯函数（core 层）。
// 输入：每张待聚类照片的 512-d ArcFace embedding（小端序 float32 字节流）。
// 算法：贪心单连锁——按序遍历，对每个 embedding 找「代表脸（群首 emb）余弦相似度
//       >= threshold」的已有簇，命中则并入，否则新建簇。代表脸取群首。
// 守恒契约（铁律6）：每个输入 path 恰出现一次；outputCount == inputCount；无重复、无遗漏。
//   —— 聚类本身不增删照片，只是把「同一个人的多张脸」归到同一 label 下。
// label：人物N（N 从 startIndex 起算，避免与库内既有「人物N」撞号）。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>

// 聚类输入项：path 标识 + embedding BLOB（512 * 4 字节）。
struct ClusterInput {
    QString    path;
    QByteArray emb;
};

// 一个簇：label + 成员 path 列表。
struct ClusterGroup {
    int         id = 0;       // 簇内编号（0 起）
    QString     label;        // 人物N
    QStringList paths;        // 成员照片 path（按并入顺序）
};

// 聚类结果：簇列表 + 守恒计数（供 QA 断言）。
struct ClusterResult {
    QVector<ClusterGroup> groups;
    int inputCount  = 0;      // 输入照片数
    int outputCount = 0;      // 所有簇成员数之和（守恒：== inputCount）
};

class FaceCluster {
public:
    // 阈值余弦聚类。threshold 默认 0.4（ArcFace 余弦经验阈值）；startIndex 控制
    // 「人物N」起始编号（默认 1）。守恒契约保证 outputCount == inputCount。
    static ClusterResult cluster(const QVector<ClusterInput>& items,
                                 double threshold = 0.4,
                                 int startIndex = 1);

    // QA 反例钩子：打开后 cluster 故意丢一张（使守恒断言转红，非空心）。
    static void setForceBrokenForQa(bool on);

    // QA 反例 2：打开后把所有成员塌进 group 0（破坏分簇语义，测 O/C/D）。
    static void qaSetForceMergeForQa(bool on);

    // 余弦相似度（仅对长度一致的 float32 BLOB 有效；否则返回 0）。供 QA 复核。
    static double cosine(const QByteArray& a, const QByteArray& b);
};

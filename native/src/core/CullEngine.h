// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
// Derived from ai-photo-manager (MIT). See THIRD_PARTY-NOTICE.md in the dist.
#pragma once
// P3 智能选片引擎（C++/Qt 原生移植，对应 client/lightalbum/core/culling.py）
// 设计铁律（平移自 Python 版 + 原生约束）：
//  - 零云端：只算本地可测维度（清晰度/光影），其余维度不编造、权重重归一化。
//  - 基于本地缩略图分析（D:\lightalbum\thumbs\<hash>.jpg 恒存在），不依赖可能缺失的原图。
//  - RAW 豁免：原生缩略图都是 jpg，故 isRaw 仅由原名扩展名决定，RAW 不走模糊/曝光淘汰。
//  - 失败即放行：缩略图解码失败 → 模糊记 999（不判模糊）、曝光判未知，宁多留不误杀。
#include <QString>
#include <QVector>
#include <QImage>
#include <functional>

struct CullInput {
    QString thumbPath;          // 本地缩略图绝对路径
    int     origW = 0, origH = 0;
    bool    isRaw = false;       // 原图扩展名是否 RAW（仅影响淘汰策略）
    QString category;           // CLIP 场景分类标签（空 = 未分类），供选片页筛选
};

struct CullItem {
    QString thumbPath;
    double  blur = 0;            // Laplacian 方差；999=无法评估
    bool    isBlurry = false;
    QString exposureVerdict;     // 正常/欠曝/过曝/严重欠曝/严重过曝/未知
    double  exposureMean = 0;
    QString md5;
    quint64 phash = 0;           // 64-bit 感知哈希（hashSize=8）
    bool    resolutionOk = true;
    double  sharpness = -1;      // -1 表示 None（无法评估）
    double  lighting = -1;       // -1 表示 None
    double  total = 0;           // 0-100 加权（仅本地维度）
    QString grade;
    double  coverage = 0;        // 参与计分的权重占比
    QString status;              // candidate / rejected
    QString category;            // CLIP 场景分类标签（透传自 CullInput）
    QString rejectReason;
    QString dupOf;               // 去重时被指向的保留者缩略图
    int     dupGroup = 0;
};

struct CullDupGroup {
    QString     keptThumb;
    QString     reason;
    QStringList similar;
};

struct CullReport {
    int total = 0;
    int candidateCount = 0;
    int rejectedCount = 0;
    int blurryCount = 0;
    int exposureCount = 0;
    int dupCount = 0;
    int unknownCount = 0;        // 解码失败
    QVector<CullItem>  topItems; // 候选按总分降序
    QVector<CullDupGroup> dupGroups;
    QVector<CullItem>  dupItems; // 去重被拒项（已补算本地维度分），供 P25 对决模式组队
};

struct CullOptions {
    int    dedupThreshold = 8;   // pHash Hamming 阈值；<0 表示不做相似去重（仍做 MD5 精确去重）
    double blurThreshold = 80.0;
    int    minDim = 800;
    double minMp = 0.5;
    int    hashSize = 8;
    int    topN = 60;
};

class CullEngine {
public:
    using ProgressFn = std::function<void(int cur, int total, const QString& msg)>;
    static CullReport analyze(const QVector<CullInput>& inputs,
                              const CullOptions& opts = CullOptions(),
                              ProgressFn progress = nullptr);
private:
    static double blurScore(const QImage& gray);
    static void   exposureStats(const QImage& gray, double& mean, double& under,
                                double& over, QString& verdict);
    static quint64 computePhash(const QImage& src, int hashSize);
    static double sharpnessScore(double var);
    static double lightingScore(const QString& verdict);
    static double computeTotal(double sharpness, double lighting, double& coverage);
    static int    hamming(quint64 a, quint64 b);
};

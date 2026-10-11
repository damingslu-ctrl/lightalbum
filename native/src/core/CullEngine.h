// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
// Derived from ai-photo-manager (MIT). See THIRD_PARTY-NOTICE.md in the dist.
#pragma once
// P3 智能选片引擎（C++/Qt 原生移植，对应 client/lightalbum/core/culling.py）
// 设计铁律（平移自 Python 版 + 原生约束）：
//  - 零云端：只算本地可测维度（清晰度/光影），其余维度不编造、权重重归一化。
//  - 基于本地缩略图分析（D:\lightalbum\thumbs\<hash>.jpg 恒存在），不依赖可能缺失的原图。
//  - RAW / 视频豁免：原生缩略图都是 jpg（RAW 由 LibRaw 解码、视频由 ffmpeg 抽帧），
//    其暗部 Laplacian 与曝光统计**系统性失真**且语义上不适用于视频，
//    故二者只查分辨率、不走模糊/曝光淘汰。判据统一由 CullInput::exemptFromQualityCull()
//    给出（isRaw 由 RawDecode::isRawExt 驱动、isVideo 由 Photo::mediaType 驱动），
//    **禁止在 UI 层另立扩展名表**（2.29.0 修：原 isRawSuffix 漂移 8 种 RAW）。
//  - 失败即放行：缩略图解码失败 → 模糊记 999（不判模糊）、曝光判未知，宁多留不误杀。
#include <QString>
#include <QVector>
#include <QImage>
#include <functional>

struct CullInput {
    QString thumbPath;          // 本地缩略图绝对路径
    int     origW = 0, origH = 0;
    bool    isRaw = false;       // 原图扩展名是否 RAW（仅影响淘汰策略）
    // 2.29.0 视频：与 RAW 同享「只查分辨率」豁免。理由同样是**缩略图不是真实画质** ——
    // 视频缩略图是抽帧封面，拿它的 Laplacian/曝光去淘汰视频在语义上不成立
    // （与「视频无 EXIF 不编造」同源的一致性要求）。
    bool    isVideo = false;
    double  durSec = 0;         // 2.31.0 视频时长（秒，非视频为 0），透传到 CullItem 供 UI 显示
    QString category;           // CLIP 场景分类标签（空 = 未分类），供选片页筛选
    // 2.33.0 隐私门：私密标记透传（与 category 同为「UI 需要据以过滤的透传字段」）。
    //   ★铁律 21：过滤在注入点做，UI 不得另立一份照片遍历。
    bool    priv = false;

    // 2.29.0：RAW / 视频是否豁免质量淘汰（只查分辨率）。
    // ★ 唯一真源：UI 层不得再另立扩展名表（原 isRawSuffix 16 种 vs RawDecode 24 种，
    //   漂移 8 种导致 SRW/IIQ/FFF/KDC/K25/MOS/PXT/SRF 的 RAW 被误杀 —— 铁律 6）。
    bool exemptFromQualityCull() const { return isRaw || isVideo; }
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
    // 2.31.0 视频适配：透传自 CullInput，供选片页绘制徽标与「只看视频」过滤
    QString mediaType = QStringLiteral("image");
    double  durSec = 0;
    bool    isVideo = false;
    // 2.33.0 隐私门：透传自 CullInput，供选片页在锁定时隐藏该卡片（铁律 21）
    bool    priv = false;
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
    // 复用点：相似照片去重（PhotoDedup）直接调用本算法，保持 pHash 唯一真源。
    static quint64 computePhash(const QImage& src, int hashSize);
    static int    hamming(quint64 a, quint64 b);
private:
    static double blurScore(const QImage& gray);
    static void   exposureStats(const QImage& gray, double& mean, double& under,
                                double& over, QString& verdict);
    static double sharpnessScore(double var);
    static double lightingScore(const QString& verdict);
    static double computeTotal(double sharpness, double lighting, double& coverage);
};

// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·智能相册：相似照片去重核心。
// 纯函数、UI 无关、可被 QA 门禁断言。复用 CullEngine::computePhash /
// CullEngine::hamming（pHash 算法唯一真源，禁止在此重实现一份）。
//
// 与 CullEngine 的关系：CullEngine 把去重作为"选片/淘汰"流水线的一环，
// 本类把它抽成独立、可被图库直接调用的能力（不跑模糊/曝光评分），
// 并补齐「图库级可发现 + 可逆标记」缺口。
#include <QString>
#include <QVector>
#include <functional>
#include "CullEngine.h"

struct DedupPhoto {
    QString path;        // 原图绝对路径（写库标识用）
    QString thumbFile;   // 缩略图文件名（与 thumbsDir 拼接取图）
    int     origW = 0, origH = 0;  // 原图尺寸；"保留最优"= 最高分辨率
};

struct DupGroup {
    QString           keptPath;   // 保留者原图路径
    QVector<QString>  dupPaths;   // 被判重复/相似的其它原图路径
    QString           reason;     // "完全重复（MD5 相同）" / "高度相似（感知哈希）"
    qint64            keptPixels = 0;  // 保留者 origW*origH（"保留最优"断言用）
};

// 阶段 1 的产物：单张照片的可比特征，不含任何本地化文案。
// 线程契约（2.33.0）：describe() 要在 worker 线程跑，QTranslator 并非线程安全，
// 所以可翻译字符串（reason）一律留到 GUI 线程的 group() 里再取。
struct DedupDesc {
    QString    path;
    QByteArray md5;        // 空 = 未计算（exactMd5=false 或文件不可读）
    quint64    phash = 0;  // 0 = 缩略图不可用（与 computePhash 的「全零即无效」一致）
    qint64     pixels = 0;
    bool       ok = false; // 缩略图可解码
};

class PhotoDedup {
public:
    // 在 photos 上做去重分组；thumbsDir 为缩略图目录。
    //   phashThreshold : pHash Hamming 阈值（默认 8，与 CullEngine 一致）
    //   exactMd5       : 是否先做 MD5 精确去重（字节完全相同）
    // 失败即放行：缩略图缺失/解码失败 → 该照片不参与分组（不报错、不丢失）。
    // 守恒契约：每张合法照片恰好出现一次（要么作某组 keptPath，要么在某组
    //           dupPaths；绝不出现在两组、也绝不被静默丢弃）。
    // 等价于 describe() + group()，保留给不需要进度的调用方与 LA_DEDUP_QA。
    static QVector<DupGroup> findGroups(const QVector<DedupPhoto>& photos,
                                        const QString& thumbsDir,
                                        int phashThreshold = 8,
                                        bool exactMd5 = true);

    // ---- 2.33.0 两阶段拆分（把 IO+解码从 GUI 线程挪走）----
    // 阶段 1：逐张解码缩略图并算 MD5/pHash（纯 IO/CPU，不取译文，可在 worker 线程跑）。
    //   progress(done) 每完成一张回调一次（done = 已处理张数，可空）。
    //   cancel() 返回 true 时提前停下；调用方须丢弃部分结果（残缺集分组会造假分组）。
    static QVector<DedupDesc> describe(const QVector<DedupPhoto>& photos,
                                       const QString& thumbsDir,
                                       bool exactMd5 = true,
                                       const std::function<void(int)>& progress = nullptr,
                                       const std::function<bool()>& cancel = nullptr);
    // 阶段 2：由描述符分组（纯内存，取 reason 译文 → 必须在 GUI 线程调用）。
    // md5 全为空时精确去重环节自然无组，故不再单列 exactMd5 开关。
    static QVector<DupGroup> group(const QVector<DedupDesc>& descs, int phashThreshold = 8);

    // ---- QA 钩子（反例用，注入真实实现而非测试侧伪造）----
    // 打开后 findGroups 故意把第 1 组的保留者再塞进第 2 组的 dupPaths，
    // 使同一张照片出现两次 → 破坏「总数守恒」不变量。门禁守恒断言因此变红，
    // 证明断言有效（非空心）。
    static void setForceDoubleCountForQa(bool on);
};

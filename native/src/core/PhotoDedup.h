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

class PhotoDedup {
public:
    // 在 photos 上做去重分组；thumbsDir 为缩略图目录。
    //   phashThreshold : pHash Hamming 阈值（默认 8，与 CullEngine 一致）
    //   exactMd5       : 是否先做 MD5 精确去重（字节完全相同）
    // 失败即放行：缩略图缺失/解码失败 → 该照片不参与分组（不报错、不丢失）。
    // 守恒契约：每张合法照片恰好出现一次（要么作某组 keptPath，要么在某组
    //           dupPaths；绝不出现在两组、也绝不被静默丢弃）。
    static QVector<DupGroup> findGroups(const QVector<DedupPhoto>& photos,
                                        const QString& thumbsDir,
                                        int phashThreshold = 8,
                                        bool exactMd5 = true);

    // ---- QA 钩子（反例用，注入真实实现而非测试侧伪造）----
    // 打开后 findGroups 故意把第 1 组的保留者再塞进第 2 组的 dupPaths，
    // 使同一张照片出现两次 → 破坏「总数守恒」不变量。门禁守恒断言因此变红，
    // 证明断言有效（非空心）。
    static void setForceDoubleCountForQa(bool on);
};

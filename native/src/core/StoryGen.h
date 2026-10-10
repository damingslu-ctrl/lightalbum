// 自动故事相册（人物故事方向·B）：按「地点 + 时间窗口」把照片自动聚合成一组故事。
//
// 设计要点（对齐项目铁律，与 PhotoCluster 同构）：
//  · **纯函数、零 UI 依赖**：输入 Photo 列表，输出故事列表。门禁可直接调用，
//    不必构造 MainWindow（与 LA_FACE_CLUSTER_QA 的做法一致）。
//  · **数据诚实**：每张照片必须恰好归入一个故事（含「未标注日期」单张故事），
//    **绝不静默丢弃** —— 聚合的价值就在于「让用户看见没被归类的那些」。
//  · **零模型改动**：Photo 已有 prov/city/region/date 字段（core/Library.h），
//    本模块只做读取与分组。
//  · **守恒契约**：outputCount（各故事 photoCount 之和）必须 == inputCount，
//    且每张照片的路径恰好出现一次 —— 这是 LA_STORY_QA 的核心断言（反例打这里）。
#pragma once
#include <QString>
#include <QVector>
#include <QDate>
#include "Library.h"

namespace StoryGen {

struct Story {
    QString     id;          // 稳定键：place + 首张日期（如 "北京|2024-05-01"）；
                              //   无日期故事用 "place|nodate|path" 保证唯一
    QString     title;       // 面向用户：如 "北京 · 2024年5月" / "上海 · 2024年5月-6月"
    QString     place;       // 地点名（prov 优先 → city → region → 未知地点）
    QDate       begin;       // 故事起（最早拍摄日；无日期故事为无效日期）
    QDate       end;         // 故事止（最晚拍摄日）
    QStringList paths;       // 该故事的照片路径（保持输入原序）
    int         photoCount = 0;
};

struct Result {
    QVector<Story> stories;
    int inputCount  = 0;     // 进入聚合（通过隐私门）的照片数
    int outputCount = 0;     // sum(story.photoCount)，必须 == inputCount
};

// gapDays：同地点、相邻两张照片拍摄日间隔 > gapDays 则拆成不同故事（一次行程）。
// hidePrivate：跳过私密照片（与 GalleryModel 口径一致）。
Result build(const QVector<Photo>& photos, int gapDays = 5, bool hidePrivate = true);

// 便捷：把 Result 中所有故事的路径展开成一个列表（含「未标注日期」单张故事）。
QStringList allPaths(const Result& r);

// QA 反例钩子：置 true 后 build() 会**丢弃**最后一张照片（模拟「故事聚合骗用户」
// 的真实缺陷形态：总数不守恒）。生产路径永不调用；仅供 LA_STORY_QA_FORCE_BROKEN
// 验证守恒断言非空心。默认 false。
void setForceDropForQa(bool on);
bool forceDropForQa();

} // namespace StoryGen

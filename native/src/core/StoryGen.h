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

// 故事标题：地点 + 时间范围。四种诚实形态，逐条由 LA_STORY_QA 断言（2.33.0）。
//  · 无日期        → 「地点 · 未标注日期」
//  · 单日有日期    → 「地点 · 2024年5月20日」
//    （2.32.0 误把 `begin == end` 也归进「未标注日期」，等于对明明有 EXIF 的照片
//     撒谎，违反数据诚实铁律 —— 单日故事与无日期故事是两回事，不可合并判定）
//  · 同年跨月      → 「地点 · 2024年5月-6月」
//  · 跨年          → 「地点 · 2023年12月-2024年1月」
//    （2.32.0 省略结束年 → 2023-12~2024-01 读成「2023年12月-1月」，跨年事实被吞）
QString titleFor(const QString& place, const QDate& begin, const QDate& end);

// QA 反例钩子：置 true 后 build() 会**丢弃**最后一张照片（模拟「故事聚合骗用户」
// 的真实缺陷形态：总数不守恒）。生产路径永不调用；仅供 LA_STORY_QA_FORCE_BROKEN
// 验证守恒断言非空心。默认 false。
void setForceDropForQa(bool on);
bool forceDropForQa();

} // namespace StoryGen

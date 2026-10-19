#pragma once
// 2.42.0 波3「主动」智能策展系统 · 引擎（spec §2.2）。
//
// 铁律 20：全纯函数 —— 与产品同一份实现，门禁 LA_CURATE_QA 直调，不构造 UI。
// 铁律：本模块零 IO、零系统钟（时间一律经 Now 注入）、零外部调用（文案为模板拼接，非 LLM）。
// 数据诚实（R7）：缺数据源的模板返回空 + skipReason，绝不编造。
#include "CurateState.h"
#include "Library.h"     // Photo
#include <QString>
#include <QStringList>
#include <QVector>

namespace Curate {

// 模板规格：id（枚举序即 V6 §三表序）/ i18n 展名 key / 数据源 token / 方向。
struct TemplateSpec {
    TemplateId id = TemplateId::OnThisDay;
    QString    nameKey;                          // .ts 展名 key 字面量
    QString    sourceId;                         // 数据源 token（非空；缺源即 skip 的前提）
    Direction  direction = Direction::Up;
};

// 12 模板表（静态只读）。恒 12 条，id 无重，方向 Up×11 + Down×1（仅 BadShotSweep）。
const QVector<TemplateSpec>& templateTable();

// 候选池 = 全库 − 私密（hidePrivate 时）− 近 4 周已展出（recentExhibitPaths）。
// 返回指向 all 元素的指针（生命周期与 all 绑定）。now 供调用方显式化时间口径
// （v1 的「近 4 周」由调用方 CurateScheduler 依展出时间戳算好后传入）。
QVector<const Photo*> candidatePool(const QVector<Photo>& all, bool hidePrivate,
                                    const QStringList& recentExhibitPaths, const Now& now);

// 分桶器（计划裁决 3）：键命名固定 cat:/person:/y:/ym:/city:/tag:，
// 桶内值为 pool 下标（与调用方列表绑定）。
Buckets bucket(const QVector<const Photo*>& pool);

// 模板 id 的稳定 token（键空间与 i18n 映射用），如 "OnThisDay"。
const char* templateIdToken(TemplateId id);

// 冷启动策略（G，spec §3）：入库 <50 → 无展；<200 → 仅 NewIntake/HiddenReunion；否则全 12。
QVector<TemplateId> coldStartTemplates(int totalCount);

// 选题器：每个模板各产 0..1 个候选主题。缺数据源 → 返回空（调用方据此置 skipReason，R7）。
// 计划裁决 R-T4：spec §2.2 的 bucket(pool)→pool 下标 与 propose(...,all,...) 无法复合
// （下标是 pool 相对、propose 无 pool）；故 propose 第 3 参取 pool（const Photo* 视图，
// 所需字段全在 Photo 上；all 不额外提供信息）。
QVector<Theme> propose(TemplateId id, const Buckets& bk, const QVector<const Photo*>& pool,
                       const Now& now);

// 全局选题分：score = w.fresh·新鲜感 + w.pref·偏好(恒0) − w.repeat·重复惩罚。
// ★ 画质不在此式（只在展内排序/主图选择，见 compose）。
double score(const Theme& t, const Weights& w, const ExhibitHistory& h, const Now& now);

// 频控：每周 ≤1 · 同人同题 60 天冷却 · 静音（全局或模板级）→ 返回是否放行。
bool frequencyAllows(const Theme& t, const ExhibitHistory& h, const MuteState& m, const Now& now);

}   // namespace Curate

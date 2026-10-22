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

// 模板规格：id（枚举序即 V6 §三表序）/ 展名模板 / 数据源 token / 方向。
struct TemplateSpec {
    TemplateId id = TemplateId::OnThisDay;
    QString    nameTmpl;                         // `.ts` source = 中文展名模板，`%1` = 展内张数
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

// token → 模板 id（静音表回读用）。未知 token 返回 false，**不**回落到任一模板：
// 静音行宁可如实显示原 token，也不冒充某一类展（铁律 21）。
bool templateIdFromToken(const QString& token, TemplateId* out);

// 静音行的模板可读名：取展名模板「 · 」之前的本地化部分（12 条译文共用同一分隔符），
// 不另建第二份名字表——展名只此一处真源。
QString templateLabelFor(TemplateId id);

// 冷启动策略（G，spec §3）：入库 <50 → 无展；<200 → 仅 NewIntake/HiddenReunion；否则全 12。
QVector<TemplateId> coldStartTemplates(int totalCount);

// 选题器：每个模板各产 0..1 个候选主题。缺数据源 → 返回空（调用方据此置 skipReason，R7）。
// 计划裁决 R-T4：spec §2.2 的 bucket(pool)→pool 下标 与 propose(...,all,...) 无法复合
// （下标是 pool 相对、propose 无 pool）；故 propose 第 3 参取 pool（const Photo* 视图，
// 所需字段全在 Photo 上；all 不额外提供信息）。
// ★ 返回的 Theme::title 恒空：候选帧数不等于展出帧数（compose 还要裁 5..9 + 去冗余），
//   此处填名会夸大张数（铁律 21）。展名只在 compose 产出。
QVector<Theme> propose(TemplateId id, const Buckets& bk, const QVector<const Photo*>& pool,
                       const Now& now);

// 全局选题分：score = w.fresh·新鲜感 + w.pref·偏好(恒0) − w.repeat·重复惩罚。
// ★ 画质不在此式（只在展内排序/主图选择，见 compose）。
double score(const Theme& t, const Weights& w, const ExhibitHistory& h, const Now& now);

// 频控：每周 ≤1 · 同人同题 60 天冷却 · 静音（全局或模板级）→ 返回是否放行。
bool frequencyAllows(const Theme& t, const ExhibitHistory& h, const MuteState& m, const Now& now);

// 成展器：展内排序 = w3·画质（**唯一消费 quality 的地方**，R7）；
//   主图 = 展内画质分最高；陪展 = 类内多样（phash 邻域去冗余）；总量裁到 5..9。
//   ★ 只对本主题的候选帧算画质（不碰全库）；thumbsDir 与 Photo::thumbFile 拼接取图。
//   ★ 展名在此产出：帧数定稿后先查 .ts 目录再填 `%1`，返回的 Theme::title 即展签文案。
Theme compose(const Theme& t, const QVector<Photo>& all, const QString& thumbsDir);

// 画质分（V6「凝视分」本波定义）：CullEngine::blurScore+exposureStats 合成归一化到 [0,1]。
//   缩略图不可读 → 返回 -1（失败即未知，绝不编造）。
double qualityScore(const Photo& p, const QString& thumbsDir);

// 门禁探针（spec §5.2 解码成本红线「O(展内张数)，不得对全库」的可测化）：
//   qaResetScanProbe() 归零后，qaQualityCalls()/qaHashCalls() 读自上次归零以来
//   qualityScore 的调用数 / pHash 的实际解码数（缓存命中不计）。qaSetForceFullScan
//   仅供门禁反例臂复现 T5 误读的旧形态（对全候选先算画质再裁展）。
void qaResetScanProbe();
int  qaQualityCalls();
int  qaHashCalls();
void qaSetForceFullScan(bool on);

// 废片巡检（spec §2.2 / 红线 3）：连拍冗余 · 糊图 · 截图过期 三条规则**并集**，
//   逐条记为 `BadShotRecord{path, why}`（why ∈ {burst, blurry, stale-shot}）。
//   ★ 只打包，永不删除 —— 本函数**无任何删除/写库调用**（编译期签名只收 const）。
//   ★ RAW/视频豁免糊图判定（承 CullEngine.h:9-13 铁律：缩略图不代表其真实画质）。
BadShotBatch inspectBadShots(const QVector<Photo>& all, const QString& thumbsDir, const Now& now);

// 批次内每条记录的理由 token 列表（序与 items 一致；UI 展示「为什么建议退藏」用）。
QStringList badShotWhy(const BadShotBatch& batch);

}   // namespace Curate

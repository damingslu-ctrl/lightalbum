#pragma once
// 2.42.0 波3「主动」智能策展系统 · 领域纯数据（spec §2.1）。
//
// 铁律：本头文件零 Qt widget / 零 IO / 零系统钟依赖 —— 只放值类型。所有编排逻辑
// 在 CurateEngine（纯函数）与 CurateScheduler（线程编排）里，门禁 LA_CURATE_QA 直调。
// 持久层「行形状」（ExhibitRow / AlbumRow）不在此，见 Library.h（SDD 预检裁决 PF-1）。
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

namespace Curate {

// 12 类模板，序即 V6 §三表序（spec §2.3）。
enum class TemplateId { OnThisDay, PersonAnniv, HiddenReunion, CitySlice, HuePilgrimage,
                        MonoHour, BeyondBurst, TravelReel, FestivalEcho, NewIntake,
                        LightArchive, BadShotSweep };
// 由末位枚举推导的模板数：门禁断言恒 12（少一枚即红）。spec §2.1 的枚举序是契约。
constexpr int kTemplateCount = static_cast<int>(TemplateId::BadShotSweep) + 1;

enum class Direction { Up, Down };                          // Down 仅 BadShotSweep

// 全局选题权重；pref 恒 0（R2：v1 无偏好学习，门禁断言 w2==0）。
struct Weights { double fresh = 0.55, pref = 0.0, quality = 0.30, repeat = 0.15; };

// 一个候选主题（展名 + 一句话小记 + 主图/陪展帧序 + 可解释打分）。
struct Theme {
  TemplateId id = TemplateId::OnThisDay;
  QString title; QString blurb;              // 展名 + 小记（模板文案，非 LLM）
  QVector<QString> paths;                    // 主图在最前，随后 4–8 陪展
  QString primaryPath;                       // 主图（展内画质分最高）
  // 主题身份键（打分新鲜感/频控冷却的键空间）；如 {"tmpl:PersonAnniv","person:Alice"}。
  // 计划裁决 R-T4b：spec §2.1 的 Theme 无此字段，但 score/frequencyAllows 需据键查历史，
  // 而二者形参不含 pool/all —— 故把键随主题携带（值型，无副作用）。
  QStringList topicKeys;
  double score = 0.0; QVector<QString> reasons;   // 打分可解释（门禁断言用）
};

// 成展结果；invalid 时必带 skipReason（R7：缺源不编造，如实跳过）。
struct Exhibit {
  bool valid = false;
  Theme theme;
  qint64 generatedAt = 0;
  QString skipReason;
};

struct BadShotRecord { QString path; QString why; };   // why ∈ {burst, blurry, stale-shot}
struct BadShotBatch  { QVector<BadShotRecord> items; qint64 builtAt = 0; };

// 注入式时间（spec 计划裁决 1）：门禁可复现，禁读系统钟。
struct Now { qint64 epochSec = 0; int y = 0, m = 0, d = 0; };

// 候选池分桶结果：key = "cat:X" / "person:X" / "y:2026" / "ym:2026-10" / "city:X" / "tag:X"。
// 桶内为 all/pool 列表里的下标（不是指针）—— 生命周期与调用方列表绑定。
struct Buckets { QHash<QString, QVector<int>> byKey; };

// 展出历史（供新鲜感/重复惩罚/频控消费）。
// lastShownByKey / cooldownUntil 的 key = "person:X" 或 "tmpl:X"。
struct ExhibitHistory {
  QStringList shownPaths;
  QHash<QString, qint64> lastShownByKey;
  QHash<QString, qint64> cooldownUntil;
  // 最近一次任意展出时间（频控「每周 ≤1」用）。计划裁决 R-T4b：spec §2.1 无此字段，
  // 但周频控需要全库级「上次展出」口径，故显式携带（由 Library::lastExhibitAt 填充）。
  qint64 lastExhibitAt = 0;
};

// 静音状态（模板级静音 + 全局静音开关）。
struct MuteState { QStringList mutedTemplates; bool globallyMuted = false; };

// 缺源理由（非空即缺源，R7）。
struct SkipReason { QString code; };

}   // namespace Curate

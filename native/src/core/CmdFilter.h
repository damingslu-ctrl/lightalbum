#pragma once
// 2.41.0 spec §2.4 ⌘K v1 的过滤值类型 + 过滤谓词所需的两条时间真源（四季表、日期解析）。
// 独立成头（不放 CmdKPanel.h）：GalleryModel/门禁要消费它，而 CmdKPanel 是 QWidget
// 带 Q_OBJECT——include 会把整个 UI 头拖进数据模型层。零 Qt UI 依赖（Core only）。
#include <QString>
#include <QDate>

struct CmdFilter {
    int yearMin = 0, yearMax = 0;          // 0=未指定（含相等：2019 年查询 ⇒ min=max=2019）
    int month = 0, day = 0;                // 1..12 / 1..31；0=未指定
    QString season, person, tokenPlace, category;   // 空=未指定
    QString text;                          // 计划裁决 1：兜底 contains 词（剩余内容）
    // inline：定义放头里，否则 GalleryModel/门禁各自链接不到（原 CmdKPanel.cpp 出定义）。
    bool empty() const
    {
        return yearMin == 0 && yearMax == 0 && month == 0 && day == 0
            && season.isEmpty() && person.isEmpty() && tokenPlace.isEmpty()
            && category.isEmpty() && text.isEmpty();
    }
};

// 四季唯一真源：3-5 春 / 6-8 夏 / 9-11 秋 / 12-2 冬。索引序 = ⌘K 季字表序（春=0）。
// 前厅回退链（LobbyPage）与 ⌘K 季节筛选（GalleryModel）共用此表——铁律 20，
// 第二份「哪几个月算一季」的定义不许出现。非法月份（越界/0）返回 -1，绝不落到「冬」。
inline int seasonIndexOf(int month)
{
    if (month < 1 || month > 12) return -1;
    if (month >= 3 && month <= 5)  return 0;
    if (month >= 6 && month <= 8)  return 1;
    if (month >= 9 && month <= 11) return 2;
    return 3;
}
inline QString seasonNameOf(int month)
{
    static const char* kSeason[4] = { "春", "夏", "秋", "冬" };
    const int i = seasonIndexOf(month);
    return i < 0 ? QString() : QString::fromUtf8(kSeason[i]);
}

// Photo::date 三式解析（ISO / EXIF 冒号 / 带时分秒）。⌘K 的月/日/季谓词需要真日期，
// 不是字符串切片。入参取串而非 Photo：本头保持零 core 依赖。
inline QDate photoDateOf(const QString& dateStr)
{
    const QString s = dateStr.trimmed();
    if (s.isEmpty()) return QDate();
    QDate d = QDate::fromString(s.left(10), QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid()) d = QDate::fromString(s.left(10), QStringLiteral("yyyy:MM:dd"));
    if (!d.isValid()) d = QDate::fromString(s, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return d;
}

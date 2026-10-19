#pragma once
// 2.41.0 spec §2.2 前厅（Lobby）——私人记忆美术馆的「厅心」。
//
// 前厅不是仪表盘，是美术馆进门第一眼：墙上挂着「今日展」——往年今日的九张照片；
// 玻璃指令栏（T8 接线为 ⌘K v1 入口；本任务先放占位胶囊，厅内不放输入框）；
// 最新到馆横条（12 张）。空库时换成引导导入卡（不伪造数据，铁律 21 数据诚实）。
//
// 铁律 20（同源）：onThisDay / recent 是**纯函数**（无 UI、无状态、确定性），
// 门禁 LA_LOBBY_QA 直调它们，产品 UI 也直调它们——绝不复刻第二份逻辑。
#include <QWidget>
#include <QVector>
#include <QDate>
#include <QString>
#include "../core/Library.h"

class ThumbCache;

class LobbyPage : public QWidget {
    Q_OBJECT
public:
    LobbyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);

    void setHidePrivate(bool hide);
    void refresh();

    // —— 纯函数（LA_LOBBY_QA 直调；确定性、不读写任何全局状态）——
    //
    // onThisDay：往年「今日」——回退链
    //   ① 同月同日（含闰日并入：today 为 2-28 时并入 2-29）
    //   ② 不足 3 张 → 同月全库
    //   ③ 仍不足   → 同季（3-5 春 / 6-8 夏 / 9-11 秋 / 12-2 冬）跨年
    //   ④ 仍不足   → 有多少给多少（可为空，空态由 UI 表达；不回填假数据）
    //   排序：|photo.year − today.year| 升序 → rating 降序 → date 串倒序。
    static QVector<const Photo*> onThisDay(const QVector<Photo>& in, const QDate& today,
                                           bool hidePrivate, int maxN = 9);

    // recent：最新到馆——photos 已按 date ISO 串倒序，usable 过滤后头取 maxN。
    static QVector<const Photo*> recent(const QVector<Photo>& in,
                                        bool hidePrivate, int maxN = 12);

signals:
    void openGaze(const QString& startPath);
    void navigateTo(int row);
    void importRequested();
    void icloudImportRequested();
    void commandRequested();   // 2.41.0 T8：玻璃指令栏点击 → ⌘K 开馆

private:
    void rebuild();
    QWidget* makeHeroCard();
    QWidget* makeThumbStrip();
    QWidget* makeThumbCard(const Photo& p, int sidePx, bool feature);
    QWidget* makeCommandBar();
    QWidget* makeEmptyGuide();

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    bool hidePrivate_ = true;
    ThumbCache* cache_ = nullptr;

    QWidget* content_ = nullptr;
    QWidget* heroCard_ = nullptr;
    QWidget* recentStrip_ = nullptr;
    QWidget* emptyGuide_ = nullptr;
};

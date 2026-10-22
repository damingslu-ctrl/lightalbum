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
#include <QStringList>
#include <functional>
#include "../core/Library.h"
#include "../core/CurateState.h"

class ThumbCache;

class LobbyPage : public QWidget {
    Q_OBJECT
public:
    LobbyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);

    void setHidePrivate(bool hide);
    void refresh();

    // 2.42.0 波3 T9：今日一展卡的数据入口（spec §2.4）。`Exhibit::valid==false`
    // （冷启动 <50 / 缺源 / 被频控）时**整卡不出现**，不渲染空壳、不编造文案。
    void setExhibit(const Curate::Exhibit& e);

    // 2.43.0 波4 §2.5：生成期骨架。pending 期间「今日一展」槽位换成骨架卡（等待可见），
    // 收尾恒由 CurateScheduler::generated 触发——节流/无候选也要收，不许只挂成功分支。
    bool setPending(bool on);
    bool pendingSkeletonVisible() const;

    // —— 纯函数（LA_LOBBY_QA 直调；确定性、不读写任何全局状态）——
    //
    // 卡片文案（展名 + 小记）**只由 Curate 模板产出的 theme 字段拼接**（spec §3 J：
    // 文案非 LLM、无外部调用、不掺时间戳）。产品侧作为整卡的 accessibleName 暴露，
    // 门禁 J 组直调本函数——同一份，绝不复刻第二份拼接。
    static QString buildCardText(const Curate::Exhibit& e);

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

    // 2.42.0 波3 T9 三手势 + 开展入口（spec §2.1「三手势是意图，落库由 GUI 线程做」）。
    // 本类**不写库**：只把意图发出去，落库/合集/静音由 MainWindow 接线（T11）。
    void keepExhibit(const QStringList& paths, const QString& title);      // 收进合集
    void swapExhibit();                                                   // 换一展
    void muteTemplate(const QString& templateId);                          // 永久静音该模板
    void openCuration(const QStringList& paths, const QString& title,
                      const QString& blurb);                              // 进凝视序列

private:
    void rebuild();
    QWidget* makeHeroCard();
    QWidget* makeCurateCard();
    QWidget* makePendingCard();
    QWidget* makeThumbStrip();
    QWidget* makeThumbCard(const Photo& p, int sidePx, bool feature,
                           std::function<void()> onActivated = nullptr);
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
    QWidget* curateCard_ = nullptr;
    QWidget* skeleton_ = nullptr;
    bool pending_ = false;
    Curate::Exhibit exhibit_;
};

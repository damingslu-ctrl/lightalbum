#pragma once
// 2.41.0 spec §2.4 ⌘K v1（检索层）——词典解析 + E4 开馆层面板。
//
// v1 全确定性、零网络、零模型：parse / parseChips 是**纯函数**（无状态、确定性、可直调），
// 门禁 LA_CMDK_QA 与产品 UI 共用同一份（铁律 20：绝不复刻第二份解析逻辑）。
//
// 词典四级（spec §2.4）：①时间词法（去年/前年/今年/XXXX年/春夏秋冬/N月/N日）
// ②人物（调用侧传入 personDict，前缀模糊命中）③地点（调用侧传入 placeDict）
// ④风格（Classifier 8 类中文字面）；兜底 = 剩余内容 trim → text（老 contains 能力零丢失）。
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
#include <QDate>
#include "../core/CmdFilter.h"
#include "../core/Library.h"
#include "Theme.h"

class QLineEdit;
class QPushButton;
class QLabel;

class CmdKPanel : public QWidget {
    Q_OBJECT
public:
    explicit CmdKPanel(const QVector<Photo>* photos, QWidget* parent = nullptr);

    // 开馆层：记录空间语义键（灯/回放用）；E4 金描边由构造期皮肤承担。
    void openFor(Lumen::Hall s);
    void setHidePrivate(bool hide);        // 锁定态：人物词典派生自 hidePrivate 集合
    void focusInput();                     // 2.41.0 T8：开馆即落光标（面板本身不可聚焦）
    void setStashAvailable(bool on);       // 2.43.0 波4 R7：图库有勾选 → 暂存可点

signals:
    void applyFilter(const CmdFilter& f);
    void openRoom(const QString& person);
    void openGazeFiltered(const CmdFilter& f);
    void lookBackToday(const QDate& d);
    void stashRequested(const QString& name);   // 标题 = 输入框原文；载荷由调用方取选择集

public:
    static CmdFilter parse(const QString& sentence, const QStringList& personDict = {},
                           const QStringList& placeDict = {});
    static QStringList parseChips(const QString& sentence, const QStringList& personDict = {},
                                  const QStringList& placeDict = {});

private:
    void rebuildHints();
    void refreshChips();
    void onSubmit();
    QStringList personDict() const;
    QStringList placeDict() const;
    QStringList candidatesFor(const QString& chip, const CmdFilter& f) const;
    void cycleChip(const QString& chip);

    const QVector<Photo>* photos_ = nullptr;
    bool hidePrivate_ = true;
    Lumen::Hall space_ = Lumen::Hall::Gallery;
    QLineEdit* input_ = nullptr;
    QPushButton* stash_ = nullptr;
    QLabel* note_ = nullptr;               // 空解析说明（默认隐藏）
    QWidget* hintRow_ = nullptr;
    QWidget* chipRow_ = nullptr;
    QStringList removedChips_;             // 纯 UI 态：被 × 移除的 chip
    QHash<QString, QString> chipOverride_; // 纯 UI 态：chip → 当前候选文案
    QHash<QString, int> cycleIdx_;         // 纯 UI 态：chip → 候选游标
};

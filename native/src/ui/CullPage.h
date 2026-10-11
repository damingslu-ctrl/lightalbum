#pragma once
#include <QWidget>
#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include <QSortFilterProxyModel>
#include <QThread>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QMap>
#include "../core/Library.h"
#include "../core/CullEngine.h"

class QStackedWidget;
class QListView;
class DuelPane;        // P25 对决模式 PK 面板（CullPage.cpp 内定义）
class QScrollArea;
class QComboBox;
class QListWidget;
class QSpinBox;
class QToolButton;

// 选片结果网格模型（透传 category / userMark 供筛选与标记渲染）
class CullModel : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;  // 继承 QObject* 构造
    enum Role { ThumbRole = Qt::UserRole + 1, ScoreRole, GradeRole,
                ReasonRole, StatusRole, CategoryRole, MarkRole,
                MediaTypeRole, DurRole, IsVideoRole,
                PrivRole };  // 2.33.0 隐私门（锁定时代理与标记列表据此排除）
    void setItems(const QVector<CullItem>& items, const QString& thumbsDir);
    int rowCount(const QModelIndex& = QModelIndex()) const override { return items_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    void setMark(int row, const QString& mark);    // mark: "" / "like" / "skip" / "lock"
    // 2.33.0 隐私门：唯一入口在 CullPage::setHidePrivate（铁律 21），此处只接受其下发。
    //   锁定态下 markedThumbs 必须跳过私密行 —— 锁定区/胶片条不得绕过隐私门。
    void setHidePrivate(bool hide) { hidePrivate_ = hide; }
    QMap<QString,int> categoryCounts() const;     // 仅统计当前结果集
    QVector<QString> markedThumbs(const QString& mark) const;
    int rowOfThumb(const QString& thumbPath) const;
    CullItem itemAt(int row) const;               // P25：对决读项（越界返回空项）
    int upsertItem(const CullItem& it, const QString& thumbsDir);  // P25：不存在则追加并返回行号
private:
    struct Row { CullItem item; QPixmap pm; QString mark; };
    QVector<Row> items_;
    bool hidePrivate_ = true;    // P16 隐私门（默认锁，与 GalleryModel 口径一致）
};

// 代理：场景分类 + 标记态 双重过滤（chips 与「只看」下拉共用）
class CullProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setCategoryFilter(const QString& c) { cat_ = c; invalidateFilter(); }
    void setMarkFilter(const QString& m) { mark_ = m; invalidateFilter(); }
    void setVideoOnly(bool v) { videoOnly_ = v; invalidateFilter(); }
    void setHidePrivate(bool h) { hidePrivate_ = h; invalidateFilter(); }  // 2.33.0 隐私门
protected:
    bool filterAcceptsRow(int srow, const QModelIndex& sparent) const override;
private:
    QString cat_;   // 空 = 全部
    QString mark_;  // 空 = 全部
    bool    videoOnly_ = false;
    bool    hidePrivate_ = true;   // 2.33.0 私密照在锁定时不入视图
};

// 卡片委托：缩略图 + 分数/等级 + 拒绝理由 + 标记徽标
class CullCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CullCardDelegate(QObject* parent = nullptr);
    void paint(QPainter* p, const QStyleOptionViewItem& o,
               const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& o,
                   const QModelIndex& idx) const override;
};

// 审美档案雷达（双六边形 · 6 轴设备/场景分布；诚实文案直接借鉴择影）
class RadarWidget : public QWidget {
    Q_OBJECT
public:
    explicit RadarWidget(QWidget* parent = nullptr);
    void setData(const QVector<double>& vals, const QStringList& labels,
                 int sampleCount, const QString& note);
    QSize sizeHint() const override { return QSize(300, 320); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<double> vals_;   // 0..100，6 轴
    QStringList     labels_;
    int             sample_ = 0;
    QString         note_;
};

// 分析工作线程
class CullWorker : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;  // 继承 QObject* 构造
    void setInputs(QVector<CullInput> in, CullOptions o) {
        inputs_ = std::move(in); opts_ = o;
    }
    void run() override {
        const CullReport rep = CullEngine::analyze(inputs_, opts_,
            [this](int c, int t, const QString& m){ emit progress(c, t, m); });
        emit finished(rep);
    }
signals:
    void finished(const CullReport& rep);
    void progress(int cur, int total, const QString& msg);
private:
    QVector<CullInput> inputs_;
    CullOptions opts_;
};

class CullPage : public QWidget {
    Q_OBJECT
public:
    explicit CullPage(const QVector<Photo>* photos, const QString& thumbsDir,
                      QWidget* parent = nullptr);
    // 2.33.0 隐私门（P16 补漏）：此前 vault 锁/解锁从不通知选片页，私密照在
    //   「锁着」的状态下仍被分析、显示、可标记 —— 与图库/地图/日志页口径不一致。
    //   ★铁律 21：锁定态由本入口一次下发（模型 + 代理 + 雷达），不在各处另判。
    void setHidePrivate(bool hide);
    bool hidePrivate() const { return hidePrivate_; }
    // 自测钩子：同步跑前 cap 张并直接出结果（LA_CULL_SMOKE / LA_UI_SHOT）
    void autoRun(int cap = 80);

    // 2.31.0 QA 反例钩子：关闭/恢复选片卡片视频徽标绘制
    static void qaSetVideoBadgeDisabled(bool disabled);

private slots:
    void onAnalyze();
    void onProgress(int cur, int total, const QString& msg);
    void onFinished(const CullReport& rep);
    void onChipClicked(const QString& cat);
    void onMarkFilterChanged(int idx);
    void onSelectionChanged(const QModelIndex& cur, const QModelIndex& prev);
    void onMark(const QString& mark);

private:
    // P25 对决模式
    void buildDuelSession();                 // 从报告去重组重建仲裁队
    void duelEnterMode();
    void duelExitMode();
    void duelPick(int side);                 // 0=左留 1=右留（败者标 skip）
    void duelBoth(const QString& mark);      // "skip"=双弃 / "lock"=双锁
    void duelAfterDecision();
    void duelRender();
    void markDuelItem(const CullItem& it, const QString& mark);
    void runDuelQa(const QString& png);      // 离屏自测（LA_CULL_DUEL_QA）
    bool eventFilter(QObject* obj, QEvent* ev) override;

    void buildChips(const QMap<QString,int>& counts);
    void rebuildLockArea();
    void rebuildFilmstrip();
    void updateRadar();
    void selectByThumb(const QString& thumbPath);
    void setKpi(int idx, const QString& v, const QString& label);

    // 2.33.0：分析输入的**唯一收集口**（onAnalyze 与 autoRun 此前各写一份完全相同的
    //   遍历，隐私门只能两边各补一遍 → 必然漂移）。隐私过滤在此一处完成。
    QVector<CullInput> collectInputs() const;

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    bool    hidePrivate_ = true;    // P16 隐私门（默认锁，与 GalleryModel 同口径）
    CullModel*      model_ = nullptr;
    CullProxy*      proxy_ = nullptr;
    CullWorker*     worker_ = nullptr;
    QPushButton*    runBtn_ = nullptr;
    QLabel*         progress_ = nullptr;
    QVector<QLabel*> kpis_;
    QStackedWidget* gridStack_ = nullptr;   // 0=空态引导 / 1=结果网格
    QListView*      view_ = nullptr;

    // 场景筛选 chips
    QWidget*        chipRow_ = nullptr;
    QVector<QPushButton*> chips_;
    QString         activeCat_;

    // 目标张数 stepper
    QSpinBox*       topNSpin_ = nullptr;

    // 标记「只看」下拉
    QComboBox*      markCombo_ = nullptr;

    // 选中态明细条
    QWidget*        selBar_ = nullptr;
    QLabel*         selThumb_ = nullptr;
    QLabel*         selName_ = nullptr;
    QPushButton*    likeBtn_ = nullptr;
    QPushButton*    skipBtn_ = nullptr;
    QPushButton*    lockBtn_ = nullptr;
    QPushButton*    unlockBtn_ = nullptr;

    // 锁定区（顶部）
    QFrame*         lockFrame_ = nullptr;
    QLabel*         lockTitle_ = nullptr;
    QListWidget*    lockList_ = nullptr;

    // 底部裁剪胶片条
    QScrollArea*    filmScroll_ = nullptr;
    QWidget*        filmRow_ = nullptr;

    // 右栏审美档案雷达
    RadarWidget*    radar_ = nullptr;
    QLabel*         radarNote_ = nullptr;
    QLabel*         radarCount_ = nullptr;

    QMap<QString,int> catCounts_;   // 缓存，供重建 chips
    int             lockCount_ = 0;
    int             lastRadarN_ = 0;

    // P25 对决模式状态
    struct DuelGroup { QVector<CullItem> members; QVector<int> alive; };
    QVector<DuelGroup> duelGroups_;
    int             duelIdx_ = 0;
    QWidget*        duel_ = nullptr;        // 实际构造为 QFrame（QWidget 不画 QSS 底）
    DuelPane*       paneL_ = nullptr;
    DuelPane*       paneR_ = nullptr;
    QLabel*         duelHead_ = nullptr;
    QPushButton*    duelBtn_ = nullptr;     // 操作条入口「开始对决」
    QPushButton*    duelBackBtn_ = nullptr;
    CullReport      lastReport_;
};

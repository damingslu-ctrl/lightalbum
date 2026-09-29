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
                ReasonRole, StatusRole, CategoryRole, MarkRole };
    void setItems(const QVector<CullItem>& items, const QString& thumbsDir);
    int rowCount(const QModelIndex& = QModelIndex()) const override { return items_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    void setMark(int row, const QString& mark);    // mark: "" / "like" / "skip" / "lock"
    QMap<QString,int> categoryCounts() const;     // 仅统计当前结果集
    QVector<QString> markedThumbs(const QString& mark) const;
    int rowOfThumb(const QString& thumbPath) const;
private:
    struct Row { CullItem item; QPixmap pm; QString mark; };
    QVector<Row> items_;
};

// 代理：场景分类 + 标记态 双重过滤（chips 与「只看」下拉共用）
class CullProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setCategoryFilter(const QString& c) { cat_ = c; invalidateFilter(); }
    void setMarkFilter(const QString& m) { mark_ = m; invalidateFilter(); }
protected:
    bool filterAcceptsRow(int srow, const QModelIndex& sparent) const override;
private:
    QString cat_;   // 空 = 全部
    QString mark_;  // 空 = 全部
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
    // 自测钩子：同步跑前 cap 张并直接出结果（LA_CULL_SMOKE / LA_UI_SHOT）
    void autoRun(int cap = 80);

private slots:
    void onAnalyze();
    void onProgress(int cur, int total, const QString& msg);
    void onFinished(const CullReport& rep);
    void onChipClicked(const QString& cat);
    void onMarkFilterChanged(int idx);
    void onSelectionChanged(const QModelIndex& cur, const QModelIndex& prev);
    void onMark(const QString& mark);

private:
    void buildChips(const QMap<QString,int>& counts);
    void rebuildLockArea();
    void rebuildFilmstrip();
    void updateRadar();
    void selectByThumb(const QString& thumbPath);
    void setKpi(int idx, const QString& v, const QString& label);

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
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
};

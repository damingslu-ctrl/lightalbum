#pragma once
#include <QWidget>
#include <QAbstractListModel>
#include <QStyledItemDelegate>
#include <QThread>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include "../core/Library.h"
#include "../core/CullEngine.h"

// 选片结果网格模型
class CullModel : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;  // 继承 QObject* 构造
    enum Role { ThumbRole = Qt::UserRole + 1, ScoreRole, GradeRole,
                ReasonRole, StatusRole };
    void setItems(const QVector<CullItem>& items, const QString& thumbsDir);
    int rowCount(const QModelIndex& = QModelIndex()) const override { return items_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;

private:
    struct Row { CullItem item; QPixmap pm; };
    QVector<Row> items_;
};

// 卡片委托：缩略图 + 分数/等级 + 拒绝理由
class CullCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CullCardDelegate(QObject* parent = nullptr);
    void paint(QPainter* p, const QStyleOptionViewItem& o,
               const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& o,
                   const QModelIndex& idx) const override;
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

private slots:
    void onAnalyze();
    void onProgress(int cur, int total, const QString& msg);
    void onFinished(const CullReport& rep);

private:
    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    CullModel*    model_ = nullptr;
    CullWorker*   worker_ = nullptr;
    QPushButton*  runBtn_ = nullptr;
    QLabel*       progress_ = nullptr;
    QVector<QLabel*> kpis_;
    void setKpi(int idx, const QString& v, const QString& label);
};

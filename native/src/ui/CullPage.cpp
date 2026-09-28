#include "CullPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListView>
#include <QStackedWidget>
#include <QPainter>
#include <QPixmap>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <algorithm>
#include <cstdio>

// ---------------------------------------------------------------- CullModel
void CullModel::setItems(const QVector<CullItem>& items, const QString& thumbsDir)
{
    beginResetModel();
    items_.clear();
    items_.reserve(items.size());
    for (const CullItem& it : items) {
        Row r; r.item = it;
        const QString path = QDir(thumbsDir).filePath(it.thumbPath);
        QPixmap pm(path);
        if (!pm.isNull())
            r.pm = pm.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        items_.append(r);
    }
    endResetModel();
}

QVariant CullModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= items_.size()) return QVariant();
    const Row& r = items_[idx.row()];
    switch (role) {
        case ThumbRole:  return r.item.thumbPath;
        case ScoreRole:  return r.item.total;
        case GradeRole:  return r.item.grade;
        case ReasonRole: return r.item.rejectReason;
        case StatusRole: return r.item.status;
        case Qt::DecorationRole: return r.pm;
        default: break;
    }
    return QVariant();
}

// ---------------------------------------------------------------- 委托
CullCardDelegate::CullCardDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize CullCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(232, 268);
}

void CullCardDelegate::paint(QPainter* p, const QStyleOptionViewItem& o,
                             const QModelIndex& idx) const
{
    p->save();
    const QRect r = o.rect.adjusted(6, 6, -6, -6);
    const bool sel = (o.state & QStyle::State_Selected);
    // 卡片底（P12：对齐 Lumen token——旧值 0xc9a24b/0x121614 为 P10 色板残留）
    p->setPen(QPen(sel ? QColor(0xd9, 0xa9, 0x4e) : QColor(0x26, 0x31, 0x2a), sel ? 2 : 1));
    p->setBrush(QColor(0x13, 0x1a, 0x15));
    p->drawRoundedRect(r, 10, 10);

    const QPixmap pm = idx.data(Qt::DecorationRole).value<QPixmap>();
    QRect imgR(r.x() + 8, r.y() + 8, r.width() - 16, 168);
    if (!pm.isNull()) {
        const QPixmap scaled = pm.scaled(imgR.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p->drawPixmap(imgR.x() + (imgR.width() - scaled.width()) / 2,
                      imgR.y() + (imgR.height() - scaled.height()) / 2, scaled);
    } else {
        p->setPen(QColor(0x5a, 0x6b, 0x60));
        p->drawText(imgR, Qt::AlignCenter, QStringLiteral("无缩略图"));
    }

    const QString grade = idx.data(CullModel::GradeRole).toString();
    const double score = idx.data(CullModel::ScoreRole).toDouble();
    const QString status = idx.data(CullModel::StatusRole).toString();
    const QString reason = idx.data(CullModel::ReasonRole).toString();

    QRect txtR(r.x() + 10, imgR.bottom() + 6, r.width() - 20, r.height() - imgR.height() - 12);
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = p->font(); f.setBold(true); f.setPointSizeF(13);
    p->setFont(f);
    p->drawText(txtR, Qt::AlignLeft, QStringLiteral("%1  %2").arg(grade).arg(score, 0, 'f', 1));

    p->setPen(QColor(0x8d, 0xa3, 0x96));
    f.setBold(false); f.setPointSizeF(10); p->setFont(f);
    const QString sub = (status == QStringLiteral("rejected")) ? reason
        : QStringLiteral("本地精选");
    p->drawText(txtR.adjusted(0, 18, 0, 0), Qt::AlignLeft | Qt::AlignTop, sub);
    p->restore();
}

// ---------------------------------------------------------------- CullPage
static bool isRawSuffix(const QString& s)
{
    static const QStringList raw = {
        "raw","cr2","cr3","nef","arw","dng","raf","orf","rw2","pef",
        "sr2","x3f","3fr","mef","mrw","nrw"};
    return raw.contains(s, Qt::CaseInsensitive);
}

static void parseDim(const QString& dim, int& w, int& h)
{
    w = h = 0;
    const int x = dim.indexOf('x');
    if (x < 0) return;
    bool ok1 = false, ok2 = false;
    const int v1 = dim.left(x).toInt(&ok1);
    const int v2 = dim.mid(x + 1).toInt(&ok2);
    if (ok1 && ok2) { w = v1; h = v2; }
}

CullPage::CullPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(12);

    // 概览条
    auto* kp = new QWidget(this);
    auto* kl = new QHBoxLayout(kp);
    kl->setContentsMargins(0, 0, 0, 0); kl->setSpacing(10);
    const QStringList labels = {QStringLiteral("已分析"), QStringLiteral("候选"),
                                QStringLiteral("废片"), QStringLiteral("去重组"),
                                QStringLiteral("模糊"), QStringLiteral("曝光"),
                                QStringLiteral("解码失败")};
    for (const QString& l : labels) {
        auto* lb = new QLabel(QStringLiteral("<b style='font-size:15px;color:#f4f7f4;'>—</b><br>"
            "<span style='font-size:11px;color:#8da396;'>%1</span>").arg(l), kp);
        // P12：居中 + 最小宽度，防止「解码失败」等 4 字标签被压成两行
        lb->setAlignment(Qt::AlignCenter);
        lb->setMinimumWidth(80);
        lb->setStyleSheet(QStringLiteral("QLabel{background:#131a15;border:1px solid #26312a;"
            "border-radius:10px;padding:8px 14px;}"));
        kpis_.append(lb);
        kl->addWidget(lb);
    }
    kl->addStretch(1);
    lay->addWidget(kp);

    // 操作条
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    runBtn_ = new QPushButton(QStringLiteral("开始分析"), bar);
    runBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f0d08a, stop:1 #b0863a);border:none;border-radius:9px;"
        "padding:8px 18px;color:#10130f;font-weight:bold;}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}"));
    progress_ = new QLabel(QStringLiteral("基于本地缩略图 · 零云端 · 失败即放行"), bar);
    progress_->setStyleSheet("color:#8da396;");
    bl->addWidget(runBtn_);
    bl->addWidget(progress_);
    bl->addStretch(1);
    lay->addWidget(bar);

    // 网格
    auto* view = new QListView(this);
    view->setViewMode(QListView::IconMode);
    view->setResizeMode(QListView::Adjust);
    view->setSpacing(4);
    view->setGridSize(QSize(232, 268));
    view->setWordWrap(false);
    view->setStyleSheet("QListView{background:#0c100d;border:1px solid #26312a;"
        "border-radius:12px;} QListView::item:selected{background:transparent;}");
    model_ = new CullModel(this);
    view->setModel(model_);
    view->setItemDelegate(new CullCardDelegate(view));

    // P12：分析前大面板不再是一片黑洞——第 0 页放空态引导，出结果切到第 1 页网格
    gridStack_ = new QStackedWidget(this);
    auto* emptyHint = new QLabel(QStringLiteral(
        "尚无分析结果\n\n导入照片后点击「开始分析」\n基于本地缩略图打分 · 零云端 · 失败即放行"), this);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setStyleSheet(QStringLiteral("QLabel{color:#8da396;font-size:13px;background:transparent;}"));
    gridStack_->addWidget(emptyHint);   // index 0
    gridStack_->addWidget(view);        // index 1
    gridStack_->setCurrentIndex(0);
    lay->addWidget(gridStack_, 1);

    connect(runBtn_, &QPushButton::clicked, this, &CullPage::onAnalyze);

    // 自测钩子：LA_CULL_SMOKE 下自动跑前 40 张并打印
    if (!qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")) onAnalyze();
}

void CullPage::setKpi(int idx, const QString& v, const QString& label)
{
    if (idx < 0 || idx >= kpis_.size()) return;
    kpis_[idx]->setText(QStringLiteral(
        "<b style='font-size:15px;color:#f4f7f4;'>%1</b><br>"
        "<span style='font-size:11px;color:#8da396;'>%2</span>").arg(v, label));
}

void CullPage::onAnalyze()
{
    if (!photos_) return;
    QVector<CullInput> inputs;
    inputs.reserve(photos_->size());
    for (const Photo& ph : *photos_) {
        if (ph.thumbFile.isEmpty()) continue;
        CullInput in;
        in.thumbPath = QDir(thumbsDir_).filePath(ph.thumbFile);  // 完整绝对路径
        parseDim(ph.dim, in.origW, in.origH);
        in.isRaw = isRawSuffix(QFileInfo(ph.path).suffix());
        inputs.append(in);
    }
    if (inputs.isEmpty()) {
        progress_->setText(QStringLiteral("无可用缩略图"));
        return;
    }
    // 自测只跑前 40 张，省时
    if (!qEnvironmentVariableIsEmpty("LA_CULL_SMOKE") && inputs.size() > 40)
        inputs = inputs.mid(0, 40);

    runBtn_->setEnabled(false);
    progress_->setText(QStringLiteral("分析中 0/%1").arg(inputs.size()));

    if (!worker_) worker_ = new CullWorker(this);
    worker_->setInputs(inputs, CullOptions());
    connect(worker_, &CullWorker::progress, this, &CullPage::onProgress, Qt::UniqueConnection);
    connect(worker_, &CullWorker::finished, this, &CullPage::onFinished, Qt::UniqueConnection);
    worker_->start();
}

void CullPage::onProgress(int cur, int total, const QString&)
{
    progress_->setText(QStringLiteral("分析中 %1/%2").arg(cur).arg(total));
}

void CullPage::onFinished(const CullReport& rep)
{
    runBtn_->setEnabled(true);
    setKpi(0, QString::number(rep.total), QStringLiteral("已分析"));
    setKpi(1, QString::number(rep.candidateCount), QStringLiteral("候选"));
    setKpi(2, QString::number(rep.rejectedCount), QStringLiteral("废片"));
    setKpi(3, QString::number(rep.dupGroups.size()), QStringLiteral("去重组"));
    setKpi(4, QString::number(rep.blurryCount), QStringLiteral("模糊"));
    setKpi(5, QString::number(rep.exposureCount), QStringLiteral("曝光"));
    setKpi(6, QString::number(rep.unknownCount), QStringLiteral("解码失败"));

    model_->setItems(rep.topItems, thumbsDir_);
    if (gridStack_) gridStack_->setCurrentIndex(model_->rowCount() > 0 ? 1 : 0);

    if (!qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")) {
        std::fprintf(stderr,
            "LA_CULL_SMOKE total=%d cand=%d rej=%d dup=%d blur=%d exp=%d unk=%d top=%d\n",
            rep.total, rep.candidateCount, rep.rejectedCount, rep.dupGroups.size(),
            rep.blurryCount, rep.exposureCount, rep.unknownCount, rep.topItems.size());
        std::fflush(stderr);
        QApplication::quit();
    } else {
        progress_->setText(QStringLiteral("完成 · 候选 %1 / 废片 %2 / 去重 %3 组")
            .arg(rep.candidateCount).arg(rep.rejectedCount).arg(rep.dupGroups.size()));
    }
}

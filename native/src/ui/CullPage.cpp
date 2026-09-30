#include "CullPage.h"
#include "Theme.h"
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
#include <QScrollArea>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QToolButton>
#include <QFrame>
#include <QItemSelectionModel>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QTimer>
#include <QKeyEvent>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace {
constexpr double PI = 3.14159265358979323846;

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

// 光圈解析：兼容 "f/2.8" / "2.8" / "f/1.4-5.6"（取首个数值）
static double parseAperture(const QString& s)
{
    if (s.isEmpty()) return 0;
    QRegularExpression re(QString::fromUtf8(R"(([0-9]+\.?[0-9]*))"));
    QRegularExpressionMatch m = re.match(s);
    if (m.hasMatch()) return m.captured(1).toDouble();
    return 0;
}
} // namespace

// ---------------------------------------------------------------- CullModel
void CullModel::setItems(const QVector<CullItem>& items, const QString& thumbsDir)
{
    beginResetModel();
    items_.clear();
    items_.reserve(items.size());
    for (const CullItem& it : items) {
        Row r; r.item = it; r.mark.clear();
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
        case CategoryRole: return r.item.category;
        case MarkRole:   return r.mark;
        case Qt::DecorationRole: return r.pm;
        default: break;
    }
    return QVariant();
}

void CullModel::setMark(int row, const QString& mark)
{
    if (row < 0 || row >= items_.size()) return;
    items_[row].mark = mark;
    const QModelIndex i = index(row, 0);
    emit dataChanged(i, i);
}

QMap<QString,int> CullModel::categoryCounts() const
{
    QMap<QString,int> m;
    for (const Row& r : items_)
        if (!r.item.category.isEmpty()) m[r.item.category]++;
    return m;
}

QVector<QString> CullModel::markedThumbs(const QString& mark) const
{
    QVector<QString> v;
    for (const Row& r : items_)
        if (r.mark == mark) v.append(r.item.thumbPath);
    return v;
}

int CullModel::rowOfThumb(const QString& thumbPath) const
{
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].item.thumbPath == thumbPath) return i;
    return -1;
}

CullItem CullModel::itemAt(int row) const
{
    return (row >= 0 && row < items_.size()) ? items_[row].item : CullItem();
}

// P25：对决胜者若为「被拒相似项」（不在候选网格）→ 追加成候选行，随总览/胶片条/雷达联动
int CullModel::upsertItem(const CullItem& it, const QString& thumbsDir)
{
    const int exist = rowOfThumb(it.thumbPath);
    if (exist >= 0) return exist;
    Row r; r.item = it;
    QPixmap pm(QDir(thumbsDir).filePath(it.thumbPath));
    if (!pm.isNull())
        r.pm = pm.scaled(220, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int row = items_.size();
    beginInsertRows(QModelIndex(), row, row);
    items_.append(r);
    endInsertRows();
    return row;
}

// ---------------------------------------------------------------- CullProxy
bool CullProxy::filterAcceptsRow(int srow, const QModelIndex& sparent) const
{
    const QModelIndex idx = sourceModel()->index(srow, 0, sparent);
    if (!cat_.isEmpty()) {
        if (idx.data(CullModel::CategoryRole).toString() != cat_) return false;
    }
    if (!mark_.isEmpty()) {
        if (idx.data(CullModel::MarkRole).toString() != mark_) return false;
    }
    return true;
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
    const QString mark = idx.data(CullModel::MarkRole).toString();

    QRect txtR(r.x() + 10, imgR.bottom() + 6, r.width() - 20, r.height() - imgR.height() - 12);
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = p->font(); f.setBold(true); f.setPointSizeF(13);
    p->setFont(f);
    p->drawText(txtR, Qt::AlignLeft, QStringLiteral("%1  %2").arg(grade).arg(score, 0, 'f', 1));

    p->setPen(QColor(0x8d, 0xa3, 0x96));
    f.setBold(false); f.setPointSizeF(10); p->setFont(f);
    QString sub;
    if (mark == QStringLiteral("like"))      sub = QString::fromUtf8("♥ 喜欢");
    else if (mark == QStringLiteral("skip"))  sub = QString::fromUtf8("已略过");
    else if (mark == QStringLiteral("lock"))  sub = QString::fromUtf8("已锁定");
    else sub = (status == QStringLiteral("rejected")) ? reason
                                                   : QStringLiteral("本地精选");
    p->drawText(txtR.adjusted(0, 18, 0, 0), Qt::AlignLeft | Qt::AlignTop, sub);

    // 标记徽标（单字，避免 emoji 字体缺失）：喜/略/锁
    if (!mark.isEmpty()) {
        QString ch, col;
        if (mark == QStringLiteral("like"))      { ch = QString::fromUtf8("喜"); col = "#4fc08a"; }
        else if (mark == QStringLiteral("skip")) { ch = QString::fromUtf8("略"); col = "#e0705f"; }
        else { ch = QString::fromUtf8("锁"); col = "#d9a94e"; }
        QRect br(imgR.x() + 6, imgR.y() + 6, 22, 22);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x06, 0x08, 0x06, 210));
        p->drawRoundedRect(br, 6, 6);
        p->setPen(QColor(col));
        QFont bf = p->font(); bf.setBold(true); bf.setPointSizeF(12); p->setFont(bf);
        p->drawText(br, Qt::AlignCenter, ch);
    }
    p->restore();
}

// ---------------------------------------------------------------- RadarWidget
RadarWidget::RadarWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(300, 320);
}

void RadarWidget::setData(const QVector<double>& vals, const QStringList& labels,
                          int sampleCount, const QString& note)
{
    vals_ = vals; labels_ = labels; sample_ = sampleCount; note_ = note;
    update();
}

void RadarWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int W = width(), H = height();
    const int cx = W / 2, cy = H / 2 - 4;
    const int R = qMin(W, H) / 2 - 36;
    const int N = vals_.size();
    if (N < 3) return;

    QColor grid(0x26, 0x31, 0x2a);
    // 同心六边形
    for (int ring = 1; ring <= 4; ++ring) {
        const double rr = R * ring / 4.0;
        QPolygonF poly;
        for (int i = 0; i < N; ++i) {
            const double a = -PI / 2 + i * 2 * PI / N;
            poly << QPointF(cx + rr * std::cos(a), cy + rr * std::sin(a));
        }
        p.setPen(QPen(grid, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
    }
    // 辐条 + 轴标签 + 数值
    QFont f = p.font(); f.setPointSizeF(10); p.setFont(f);
    for (int i = 0; i < N; ++i) {
        const double a = -PI / 2 + i * 2 * PI / N;
        const QPointF edge(cx + R * std::cos(a), cy + R * std::sin(a));
        p.setPen(QPen(grid, 1));
        p.drawLine(cx, cy, edge.x(), edge.y());
        const double lx = cx + (R + 16) * std::cos(a);
        const double ly = cy + (R + 16) * std::sin(a);
        const int align = (std::cos(a) > 0.3) ? Qt::AlignLeft
                        : (std::cos(a) < -0.3) ? Qt::AlignRight : Qt::AlignCenter;
        p.setPen(QPen(QColor(0x8d, 0xa3, 0x96), 1));
        p.drawText(QRectF(lx - 42, ly - 9, 84, 16), align | Qt::AlignVCenter, labels_.value(i));
        if (i < vals_.size()) {
            p.setPen(QPen(QColor(0xd9, 0xa9, 0x4e), 1));
            p.drawText(QRectF(lx - 42, ly + 7, 84, 14), align | Qt::AlignVCenter,
                       QString::number(int(vals_[i])) + "%");
        }
    }
    // 数据多边形
    QPolygonF data;
    for (int i = 0; i < N; ++i) {
        const double a = -PI / 2 + i * 2 * PI / N;
        const double v = (i < vals_.size()) ? qBound(0.0, vals_[i], 100.0) : 0;
        const double rr = R * v / 100.0;
        data << QPointF(cx + rr * std::cos(a), cy + rr * std::sin(a));
    }
    p.setPen(QPen(QColor(0x4f, 0xc0, 0x8a), 2));
    p.setBrush(QColor(0x4f, 0xc0, 0x8a, 60));
    p.drawPolygon(data);
    p.setBrush(QColor(0xd9, 0xa9, 0x4e));
    p.setPen(Qt::NoPen);
    for (const QPointF& pt : data) p.drawEllipse(pt, 3, 3);
}

// ---------------------------------------------------------------- DuelPane（P25 对决面板）
// 自绘：卡底 + 引擎倾向金框/金章 + 等比图 + 金色分条 + 分级分值 + 文件名/拒因
class DuelPane : public QWidget {
    Q_OBJECT
public:
    explicit DuelPane(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(300, 280); }
    void setItem(const CullItem& it, const QPixmap& pm) { it_ = it; pm_ = pm; update(); }
    void setFavorite(bool f) { if (fav_ != f) { fav_ = f; update(); } }
    void clear() { it_ = CullItem(); pm_ = QPixmap(); fav_ = false; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect r = rect().adjusted(1, 1, -1, -1);
        p.setPen(QPen(fav_ ? QColor(0xd9, 0xa9, 0x4e) : QColor(0x26, 0x31, 0x2a),
                       fav_ ? 2.4 : 1));
        p.setBrush(QColor(0x13, 0x1a, 0x15));
        p.drawRoundedRect(r, 12, 12);

        const QRect img = r.adjusted(10, 10, -10, -64);
        if (!pm_.isNull()) {
            const QPixmap sc = pm_.scaled(img.size(), Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
            p.drawPixmap(img.x() + (img.width() - sc.width()) / 2,
                         img.y() + (img.height() - sc.height()) / 2, sc);
        } else {
            p.setPen(QColor(0x5a, 0x6b, 0x60));
            p.drawText(img, Qt::AlignCenter, QString::fromUtf8("无缩略图"));
        }

        if (fav_) {   // 引擎倾向：金底徽章（分高者）
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xd9, 0xa9, 0x4e));
            const QRectF b(r.x() + 10, r.y() + 10, 66, 22);
            p.drawRoundedRect(b, 7, 7);
            p.setPen(QColor(0x17, 0x13, 0x08));
            QFont f = p.font(); f.setBold(true); f.setPointSizeF(10.5); p.setFont(f);
            p.drawText(b, Qt::AlignCenter, QString::fromUtf8("引擎倾向"));
        }

        const double sc = qBound(0.0, it_.total, 100.0) / 100.0;
        QRect bar(r.x() + 12, r.bottom() - 34, r.width() - 24, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x26, 0x31, 0x2a));
        p.drawRoundedRect(bar, 2, 2);
        if (sc > 0) {
            QRect fill = bar; fill.setWidth(int(bar.width() * sc));
            p.setBrush(QColor(0xd9, 0xa9, 0x4e));
            p.drawRoundedRect(fill, 2, 2);
        }

        p.setPen(QColor(0xf4, 0xf7, 0xf4));
        QFont f = p.font(); f.setBold(true); f.setPointSizeF(13); p.setFont(f);
        p.drawText(QRect(r.x() + 12, r.bottom() - 28, r.width() - 24, 20), Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(it_.grade).arg(it_.total, 0, 'f', 1));
        p.setPen(QColor(0x8d, 0xa3, 0x96));
        f.setBold(false); f.setPointSizeF(9.5); p.setFont(f);
        QString sub = QFileInfo(it_.thumbPath).fileName();
        if (!it_.rejectReason.isEmpty())
            sub += QString::fromUtf8("  ·  ") + it_.rejectReason;
        p.drawText(QRect(r.x() + 12, r.bottom() - 12, r.width() - 24, 14),
                   Qt::AlignLeft, sub);
    }

private:
    CullItem it_;
    QPixmap  pm_;
    bool     fav_ = false;
};

// ---------------------------------------------------------------- CullPage
CullPage::CullPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    auto* outH = new QHBoxLayout(this);
    outH->setContentsMargins(16, 16, 16, 16);
    outH->setSpacing(12);

    auto* mainCol = new QWidget(this);
    auto* root = new QVBoxLayout(mainCol);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // 概览条（7 KPI）
    auto* kp = new QWidget(this);
    auto* kl = new QHBoxLayout(kp);
    kl->setContentsMargins(0, 0, 0, 0); kl->setSpacing(10);
    const QStringList labels = {QString::fromUtf8("已分析"), QString::fromUtf8("候选"),
                                QString::fromUtf8("废片"), QString::fromUtf8("去重组"),
                                QString::fromUtf8("模糊"), QString::fromUtf8("曝光"),
                                QString::fromUtf8("解码失败")};
    for (const QString& l : labels) {
        auto* lb = new QLabel(QString::fromUtf8("<b style='font-size:15px;color:#f4f7f4;'>—</b><br>"
            "<span style='font-size:11px;color:#8da396;'>%1</span>").arg(l), kp);
        lb->setAlignment(Qt::AlignCenter);
        lb->setMinimumWidth(80);
        lb->setStyleSheet(QString::fromUtf8("QLabel{background:#131a15;border:none;"
            "border-radius:10px;padding:8px 14px;}"));
        kpis_.append(lb);
        kl->addWidget(lb);
    }
    kl->addStretch(1);
    root->addWidget(kp);

    // 操作条：开始分析 + 目标张数 stepper + 只看下拉 + 帮助
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(10);

    runBtn_ = new QPushButton(QString::fromUtf8("开始分析"), bar);
    runBtn_->setStyleSheet(Lumen::btnGold());

    auto* stepWrap = new QWidget(bar);
    auto* sl = new QHBoxLayout(stepWrap);
    sl->setContentsMargins(0, 0, 0, 0); sl->setSpacing(6);
    auto* stepLbl = new QLabel(QString::fromUtf8("目标张数"), bar);
    stepLbl->setStyleSheet(QStringLiteral("color:#8da396;"));
    auto* minus = new QPushButton(QString::fromUtf8("−"), bar);
    auto* plus  = new QPushButton(QString::fromUtf8("+"), bar);
    topNSpin_ = new QSpinBox(bar);
    topNSpin_->setRange(5, 600);
    topNSpin_->setValue(60);
    topNSpin_->setSuffix(QString::fromUtf8(" 张"));
    topNSpin_->setStyleSheet(Lumen::input());
    minus->setFixedWidth(26); plus->setFixedWidth(26);
    minus->setStyleSheet(QStringLiteral("QPushButton{background:#131a15;border:1px solid #26312a;"
        "border-radius:7px;color:#f4f7f4;}"));
    plus->setStyleSheet(minus->styleSheet());
    sl->addWidget(stepLbl); sl->addWidget(minus); sl->addWidget(topNSpin_); sl->addWidget(plus);
    connect(minus, &QPushButton::clicked, this, [this]{ topNSpin_->setValue(topNSpin_->value() - 10); });
    connect(plus,  &QPushButton::clicked, this, [this]{ topNSpin_->setValue(topNSpin_->value() + 10); });

    markCombo_ = new QComboBox(bar);
    markCombo_->addItem(QString::fromUtf8("全部"), QString());
    markCombo_->addItem(QString::fromUtf8("只看喜欢"), QStringLiteral("like"));
    markCombo_->addItem(QString::fromUtf8("只看略过"), QStringLiteral("skip"));
    markCombo_->addItem(QString::fromUtf8("只看锁定"), QStringLiteral("lock"));
    markCombo_->setStyleSheet(Lumen::input());

    progress_ = new QLabel(QString::fromUtf8("基于本地缩略图 · 零云端 · 失败即放行"), bar);
    progress_->setStyleSheet(QStringLiteral("color:#8da396;"));

    bl->addWidget(runBtn_);

    // P25 对决模式入口（分析完成后可点；空结果禁用）
    duelBtn_ = new QPushButton(QString::fromUtf8("开始对决"), bar);
    duelBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #d9a94e;border-radius:8px;"
        "padding:6px 16px;color:#d9a94e;}"
        "QPushButton:disabled{border-color:#26312a;color:#5a6b60;}"));
    duelBtn_->setEnabled(false);
    connect(duelBtn_, &QPushButton::clicked, this, [this]{ duelEnterMode(); });
    bl->addWidget(duelBtn_);

    bl->addWidget(stepWrap);
    bl->addWidget(markCombo_);
    bl->addWidget(progress_);
    bl->addStretch(1);
    root->addWidget(bar);

    // 场景筛选 chips 行（横向滚动）
    auto* chipScroll = new QScrollArea(this);
    chipScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    chipScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipScroll->setWidgetResizable(true);
    chipScroll->setFixedHeight(46);
    chipScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"));
    chipRow_ = new QWidget(chipScroll);
    chipRow_->setLayout(new QHBoxLayout);
    chipRow_->layout()->setContentsMargins(0, 0, 0, 0);
    chipRow_->layout()->setSpacing(8);
    chipScroll->setWidget(chipRow_);
    root->addWidget(chipScroll);

    // 选中态明细条（点击卡片后显示）
    selBar_ = new QWidget(this);
    auto* sbl = new QHBoxLayout(selBar_);
    sbl->setContentsMargins(12, 8, 12, 8);
    sbl->setSpacing(10);
    selBar_->setStyleSheet(QStringLiteral("QWidget{background:#0e1410;border:none;"
        "border-radius:10px;}"));
    selBar_->setVisible(false);
    selThumb_ = new QLabel(selBar_);
    selThumb_->setFixedSize(56, 56);
    selThumb_->setStyleSheet(QStringLiteral("QLabel{background:#000;border-radius:8px;}"));
    selThumb_->setAlignment(Qt::AlignCenter);
    selName_ = new QLabel(selBar_);
    selName_->setStyleSheet(QStringLiteral("color:#f4f7f4;font-size:12px;"));
    auto mkMark = [this](const QString& t, const QString& m, const QString& css){
        auto* b = new QPushButton(t, selBar_);
        b->setStyleSheet(css);
        connect(b, &QPushButton::clicked, this, [this, m]{ onMark(m); });
        return b;
    };
    likeBtn_  = mkMark(QString::fromUtf8("喜欢"), QStringLiteral("like"),
        "QPushButton{background:#131a15;border:1px solid #4fc08a;border-radius:8px;padding:6px 14px;color:#4fc08a;}");
    skipBtn_  = mkMark(QString::fromUtf8("略过"), QStringLiteral("skip"),
        "QPushButton{background:#131a15;border:1px solid #e0705f;border-radius:8px;padding:6px 14px;color:#e0705f;}");
    lockBtn_  = mkMark(QString::fromUtf8("锁定"), QStringLiteral("lock"),
        "QPushButton{background:#131a15;border:1px solid #d9a94e;border-radius:8px;padding:6px 14px;color:#d9a94e;}");
    unlockBtn_ = mkMark(QString::fromUtf8("解除"), QString(),
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:8px;padding:6px 14px;color:#8da396;}");
    sbl->addWidget(selThumb_);
    sbl->addWidget(selName_, 1);
    sbl->addWidget(likeBtn_); sbl->addWidget(skipBtn_); sbl->addWidget(lockBtn_); sbl->addWidget(unlockBtn_);
    root->addWidget(selBar_);

    // 锁定区（顶部）
    lockFrame_ = new QFrame(this);
    lockFrame_->setFrameShape(QFrame::StyledPanel);
    lockFrame_->setStyleSheet(QStringLiteral("QFrame{background:#0e1410;border:none;border-radius:12px;}"));
    auto* ll = new QVBoxLayout(lockFrame_);
    ll->setContentsMargins(12, 10, 12, 10); ll->setSpacing(8);
    lockTitle_ = new QLabel(QString::fromUtf8("锁定区"), lockFrame_);
    lockTitle_->setStyleSheet(QStringLiteral("color:#d9a94e;font-weight:bold;font-size:13px;"));
    ll->addWidget(lockTitle_);
    lockList_ = new QListWidget(lockFrame_);
    lockList_->setViewMode(QListView::IconMode);
    lockList_->setResizeMode(QListView::Adjust);
    lockList_->setSpacing(6);
    lockList_->setFixedHeight(98);
    lockList_->setStyleSheet(QStringLiteral("QListWidget{background:transparent;border:none;}"
        "QListWidget::item:selected{background:transparent;}"));
    connect(lockList_, &QListWidget::clicked, this, [this](const QModelIndex& i){
        auto* it = lockList_->item(i.row());
        if (it) selectByThumb(it->data(Qt::UserRole).toString());
    });
    ll->addWidget(lockList_);
    root->addWidget(lockFrame_);

    // 网格（QListView 经代理过滤）
    view_ = new QListView(this);
    view_->setViewMode(QListView::IconMode);
    view_->setResizeMode(QListView::Adjust);
    view_->setSpacing(4);
    view_->setGridSize(QSize(232, 268));
    view_->setWordWrap(false);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setStyleSheet(QStringLiteral("QListView{background:#0c100d;border:none;"
        "border-radius:12px;} QListView::item:selected{background:transparent;}"));
    model_ = new CullModel(this);
    proxy_ = new CullProxy(this);
    proxy_->setSourceModel(model_);
    view_->setModel(proxy_);
    view_->setItemDelegate(new CullCardDelegate(view_));
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &CullPage::onSelectionChanged);

    gridStack_ = new QStackedWidget(this);
    auto* emptyHint = new QLabel(QString::fromUtf8(
        "尚无分析结果\n\n导入照片后点击「开始分析」\n基于本地缩略图打分 · 零云端 · 失败即放行"), this);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setStyleSheet(QStringLiteral("QLabel{color:#8da396;font-size:13px;background:transparent;}"));
    gridStack_->addWidget(emptyHint);   // 0
    gridStack_->addWidget(view_);       // 1
    gridStack_->setCurrentIndex(0);
    root->addWidget(gridStack_, 1);

    // ---- P25 B「对决模式」：同组两两 PK——引擎按分预选，人只做仲裁 ----
    auto* duelFrame = new QFrame(this);        // QWidget 不画 QSS 底，必须 QFrame
    duelFrame->setFrameShape(QFrame::StyledPanel);
    duelFrame->setStyleSheet(QStringLiteral(
        "QFrame{background:#0c100d;border:none;border-radius:12px;}"));
    duel_ = duelFrame;
    duel_->setFocusPolicy(Qt::StrongFocus);
    auto* dv = new QVBoxLayout(duel_);
    dv->setContentsMargins(12, 10, 12, 10);
    dv->setSpacing(10);
    auto* hrow = new QHBoxLayout;
    hrow->setSpacing(10);
    duelHead_ = new QLabel(duel_);
    duelHead_->setStyleSheet(QStringLiteral("color:#8da396;font-size:12px;border:none;background:transparent;"));
    duelBackBtn_ = new QPushButton(QString::fromUtf8("返回总览 Esc"), duel_);
    duelBackBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:8px;"
        "padding:5px 14px;color:#8da396;}"
        "QPushButton:hover{color:#f4f7f4;border-color:#4fc08a;}"));
    hrow->addWidget(duelHead_, 1);
    hrow->addWidget(duelBackBtn_);
    dv->addLayout(hrow);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(14);
    paneL_ = new DuelPane(duel_);
    paneR_ = new DuelPane(duel_);
    auto* pk = new QLabel(QString::fromUtf8("PK"), duel_);
    pk->setAlignment(Qt::AlignCenter);
    pk->setStyleSheet(QStringLiteral(
        "QLabel{color:#d9a94e;border:1.5px solid rgba(217,169,78,.45);border-radius:22px;"
        "font-family:Georgia,serif;font-size:14px;font-weight:bold;"
        "min-width:40px;min-height:40px;background:transparent;}"));
    mid->addWidget(paneL_, 1);
    mid->addWidget(pk, 0, Qt::AlignVCenter);
    mid->addWidget(paneR_, 1);
    dv->addLayout(mid, 1);

    auto* brow = new QHBoxLayout;
    brow->setSpacing(10);
    auto mkDuelBtn = [this](const QString& t, std::function<void()> fn) {
        auto* b = new QPushButton(t, duel_);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:8px;"
            "padding:6px 16px;color:#8da396;}"
            "QPushButton:hover{color:#f4f7f4;border-color:#4fc08a;}"));
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    brow->addWidget(mkDuelBtn(QString::fromUtf8("左留 ←"), [this]{ duelPick(0); }));
    brow->addWidget(mkDuelBtn(QString::fromUtf8("双弃 ↓"), [this]{ duelBoth(QStringLiteral("skip")); }));
    brow->addWidget(mkDuelBtn(QString::fromUtf8("双锁 S"), [this]{ duelBoth(QStringLiteral("lock")); }));
    brow->addWidget(mkDuelBtn(QString::fromUtf8("右留 →"), [this]{ duelPick(1); }));
    auto* hint = new QLabel(
        QString::fromUtf8("引擎按分预选（金框）· 败者自动标「略过」· 幸存者留用"), duel_);
    hint->setStyleSheet(QStringLiteral("color:#5a6b60;font-size:11px;border:none;background:transparent;"));
    brow->addStretch(1);
    brow->addWidget(hint);
    dv->addLayout(brow);

    gridStack_->addWidget(duel_);       // 2：对决舞台
    duel_->installEventFilter(this);
    connect(duelBackBtn_, &QPushButton::clicked, this, [this]{ duelExitMode(); });

    // 底部裁剪胶片条
    filmScroll_ = new QScrollArea(this);
    filmScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filmScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filmScroll_->setWidgetResizable(true);
    filmScroll_->setFixedHeight(104);
    filmScroll_->setStyleSheet(QStringLiteral("QScrollArea{background:#0c100d;border:none;"
        "border-radius:12px;}"));
    filmRow_ = new QWidget(filmScroll_);
    filmRow_->setLayout(new QHBoxLayout);
    filmRow_->layout()->setContentsMargins(10, 8, 10, 8);
    filmRow_->layout()->setSpacing(8);
    filmScroll_->setWidget(filmRow_);
    root->addWidget(filmScroll_);

    outH->addWidget(mainCol, 1);

    // 右栏：审美档案雷达
    auto* side = new QWidget(this);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(12);
    auto* rt = new QLabel(QString::fromUtf8("审美档案"), side);
    rt->setStyleSheet(QStringLiteral("color:#f4f7f4;font-weight:bold;font-size:14px;"));
    radar_ = new RadarWidget(side);
    radar_->setFixedSize(300, 320);
    radarNote_ = new QLabel(side);
    radarNote_->setWordWrap(true);
    radarNote_->setStyleSheet(QStringLiteral("color:#8da396;font-size:11px;"));
    radarCount_ = new QLabel(side);
    radarCount_->setStyleSheet(QStringLiteral("color:#d9a94e;font-size:11px;"));
    sv->addWidget(rt);
    sv->addWidget(radar_, 0, Qt::AlignHCenter);
    sv->addWidget(radarNote_);
    sv->addWidget(radarCount_);
    sv->addStretch(1);
    side->setFixedWidth(330);
    outH->addWidget(side, 0);

    connect(runBtn_, &QPushButton::clicked, this, &CullPage::onAnalyze);
    connect(markCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CullPage::onMarkFilterChanged);

    updateRadar();   // 初始：全库分布

    // 自测钩子：LA_CULL_SMOKE / LA_UI_SHOT → 同步跑前 80 张并直接出结果
    const bool shot = !qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
                   || !qEnvironmentVariableIsEmpty("LA_UI_SHOT")
                   || !qEnvironmentVariableIsEmpty("LA_CULL_DUEL_QA");
    if (shot) autoRun(80);
}

void CullPage::setKpi(int idx, const QString& v, const QString& label)
{
    if (idx < 0 || idx >= kpis_.size()) return;
    kpis_[idx]->setText(QString::fromUtf8(
        "<b style='font-size:15px;color:#f4f7f4;'>%1</b><br>"
        "<span style='font-size:11px;color:#8da396;'>%2</span>").arg(v, label));
}

void CullPage::buildChips(const QMap<QString,int>& counts)
{
    QHBoxLayout* cl = qobject_cast<QHBoxLayout*>(chipRow_->layout());
    if (!cl) return;
    while (QLayoutItem* it = cl->takeAt(0)) { delete it->widget(); delete it; }
    chips_.clear();
    auto mk = [this, cl](const QString& label, const QString& cat, int n){
        auto* b = new QPushButton(QString::fromUtf8("%1 (%2)").arg(label).arg(n), chipRow_);
        b->setProperty("cat", cat);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString::fromUtf8(
            "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:999px;"
            "padding:5px 14px;color:#8da396;font-size:12px;}"
            "QPushButton:hover{border-color:#4fc08a;color:#f4f7f4;}"));
        connect(b, &QPushButton::clicked, this, [this, cat]{ onChipClicked(cat); });
        chips_.append(b);
        cl->addWidget(b);
        return b;
    };
    mk(QString::fromUtf8("全部"), QString(), model_->rowCount());
    for (auto it = counts.begin(); it != counts.end(); ++it)
        mk(it.key(), it.key(), it.value());
    cl->addStretch(1);
}

void CullPage::onChipClicked(const QString& cat)
{
    activeCat_ = (activeCat_ == cat) ? QString() : cat;
    proxy_->setCategoryFilter(activeCat_);
    for (QPushButton* b : chips_) {
        const bool on = (b->property("cat").toString() == activeCat_);
        b->setStyleSheet(on
            ? QString::fromUtf8("QPushButton{background:#1a231d;border:1px solid #4fc08a;"
                "border-radius:999px;padding:5px 14px;color:#4fc08a;font-size:12px;font-weight:bold;}")
            : QString::fromUtf8("QPushButton{background:#131a15;border:1px solid #26312a;"
                "border-radius:999px;padding:5px 14px;color:#8da396;font-size:12px;}"));
    }
}

void CullPage::onMarkFilterChanged(int idx)
{
    if (!markCombo_) return;
    proxy_->setMarkFilter(markCombo_->itemData(idx).toString());
}

void CullPage::onSelectionChanged(const QModelIndex& cur, const QModelIndex&)
{
    if (!cur.isValid()) { selBar_->setVisible(false); return; }
    const QModelIndex src = proxy_->mapToSource(cur);
    if (!src.isValid()) { selBar_->setVisible(false); return; }
    const QString thumb = model_->data(model_->index(src.row(), 0), CullModel::ThumbRole).toString();
    QPixmap pm(thumb);
    if (!pm.isNull())
        selThumb_->setPixmap(pm.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else { selThumb_->clear(); selThumb_->setText(QString::fromUtf8("无")); }
    const QString mark = model_->data(model_->index(src.row(), 0), CullModel::MarkRole).toString();
    QString tag;
    if (mark == QStringLiteral("like"))      tag = QString::fromUtf8(" · 已喜欢");
    else if (mark == QStringLiteral("skip")) tag = QString::fromUtf8(" · 已略过");
    else if (mark == QStringLiteral("lock")) tag = QString::fromUtf8(" · 已锁定");
    selName_->setText(QFileInfo(thumb).fileName() + tag);
    selBar_->setVisible(true);
}

void CullPage::onMark(const QString& mark)
{
    const QModelIndex cur = view_->currentIndex();
    if (!cur.isValid()) return;
    const QModelIndex src = proxy_->mapToSource(cur);
    model_->setMark(src.row(), mark);
    onSelectionChanged(cur, QModelIndex());
    rebuildLockArea();
    rebuildFilmstrip();
    updateRadar();
}

void CullPage::rebuildLockArea()
{
    lockList_->clear();
    const QVector<QString> locked = model_->markedThumbs(QStringLiteral("lock"));
    lockCount_ = locked.size();
    lockTitle_->setText(QString::fromUtf8("锁定区（%1）").arg(lockCount_));
    if (locked.isEmpty()) {
        auto* hint = new QListWidgetItem(lockList_);
        hint->setText(QString::fromUtf8("锁定照片会出现在这里 · 选中卡片后点「锁定」"));
        hint->setForeground(QColor(0x5a, 0x6b, 0x60));
        hint->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const QString& t : locked) {
        QPixmap pm(t);
        auto* it = new QListWidgetItem(lockList_);
        if (!pm.isNull())
            it->setIcon(QIcon(pm.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        it->setData(Qt::UserRole, t);
        it->setToolTip(QFileInfo(t).fileName());
    }
}

void CullPage::rebuildFilmstrip()
{
    QHBoxLayout* fl = qobject_cast<QHBoxLayout*>(filmRow_->layout());
    if (!fl) return;
    while (QLayoutItem* it = fl->takeAt(0)) { delete it->widget(); delete it; }
    QVector<QString> keep = model_->markedThumbs(QStringLiteral("like"));
    keep += model_->markedThumbs(QStringLiteral("lock"));
    if (keep.isEmpty()) {
        auto* hint = new QLabel(QString::fromUtf8("保留作品会出现在这条胶片 · 选中后点 ♥/锁"),
                                filmRow_);
        hint->setStyleSheet(QStringLiteral("color:#5a6b60;font-size:11px;"));
        fl->addWidget(hint);
        return;
    }
    for (const QString& t : keep) {
        QPixmap pm(t);
        auto* b = new QToolButton(filmRow_);
        if (!pm.isNull()) {
            const QPixmap crop = pm.scaled(96, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            b->setIcon(QIcon(crop));
        }
        b->setIconSize(QSize(96, 64));
        b->setFixedSize(104, 76);
        b->setStyleSheet(Lumen::filmCard());
        b->setToolTip(QFileInfo(t).fileName());
        connect(b, &QToolButton::clicked, this, [this, t]{ selectByThumb(t); });
        fl->addWidget(b);
    }
    fl->addStretch(1);
}

void CullPage::updateRadar()
{
    QSet<QString> keep;
    for (const QString& t : model_->markedThumbs(QStringLiteral("like"))) keep.insert(t);
    for (const QString& t : model_->markedThumbs(QStringLiteral("lock"))) keep.insert(t);
    QVector<const Photo*> set;
    if (photos_) {
        if (keep.isEmpty()) {
            for (const Photo& ph : *photos_)
                if (!ph.thumbFile.isEmpty()) set.append(&ph);
        } else {
            for (const Photo& ph : *photos_) {
                if (ph.thumbFile.isEmpty()) continue;
                if (keep.contains(QDir(thumbsDir_).filePath(ph.thumbFile))) set.append(&ph);
            }
        }
    }
    const int n = set.size();
    double wide = 0, tele = 0, fast = 0, low = 0, portrait = 0, bw = 0;
    for (const Photo* p : set) {
        if (p->focal > 0) { if (p->focal < 35) wide++; if (p->focal > 100) tele++; }
        const double ap = parseAperture(p->ap);
        if (ap > 0 && ap <= 2.8) fast++;
        if (p->iso > 0 && p->iso >= 1600) low++;
        if (p->category == QString::fromUtf8("人像")) portrait++;
        if (p->category == QString::fromUtf8("黑白")) bw++;
    }
    QVector<double> vals(6, 0);
    if (n > 0) {
        vals[0] = wide / n * 100; vals[1] = tele / n * 100; vals[2] = fast / n * 100;
        vals[3] = low / n * 100;  vals[4] = portrait / n * 100; vals[5] = bw / n * 100;
    }
    const QStringList labels = {QString::fromUtf8("广角"), QString::fromUtf8("长焦"),
        QString::fromUtf8("大光圈"), QString::fromUtf8("弱光"),
        QString::fromUtf8("人像"), QString::fromUtf8("黑白")};
    const QString note = (n < 30)
        ? QString::fromUtf8("样本较少（%1 张），仅供参考；这只是设备与场景分布的客观统计，不等于读懂了你的审美。").arg(n)
        : QString::fromUtf8("基于 %1 张作品 · 设备/场景分布，非审美判定 · 零云端").arg(n);
    radar_->setData(vals, labels, n, note);
    radarNote_->setText(note);
    radarCount_->setText(QString::fromUtf8("样本 %1 张 · 仅本地可测维度").arg(n));
    lastRadarN_ = n;
}

void CullPage::selectByThumb(const QString& thumbPath)
{
    const int r = model_->rowOfThumb(thumbPath);
    if (r < 0) return;
    QModelIndex p = proxy_->mapFromSource(model_->index(r, 0));
    if (!p.isValid()) {
        // 被筛选隐藏 → 清除筛选以露出
        activeCat_.clear();
        proxy_->setCategoryFilter(QString());
        markCombo_->setCurrentIndex(0);
        proxy_->setMarkFilter(QString());
        onChipClicked(QString());   // 复位 chips 样式（cat="" 不匹配任何 active）
        p = proxy_->mapFromSource(model_->index(r, 0));
    }
    if (p.isValid()) { view_->setCurrentIndex(p); view_->scrollTo(p); }
}

void CullPage::onAnalyze()
{
    if (!photos_) return;
    QVector<CullInput> inputs;
    inputs.reserve(photos_->size());
    for (const Photo& ph : *photos_) {
        if (ph.thumbFile.isEmpty()) continue;
        CullInput in;
        in.thumbPath = QDir(thumbsDir_).filePath(ph.thumbFile);
        parseDim(ph.dim, in.origW, in.origH);
        in.isRaw = isRawSuffix(QFileInfo(ph.path).suffix());
        in.category = ph.category;
        inputs.append(in);
    }
    if (inputs.isEmpty()) {
        progress_->setText(QString::fromUtf8("无可用缩略图"));
        return;
    }
    runBtn_->setEnabled(false);
    progress_->setText(QString::fromUtf8("分析中 0/%1").arg(inputs.size()));

    CullOptions opts;
    opts.topN = topNSpin_->value();
    if (!worker_) worker_ = new CullWorker(this);
    worker_->setInputs(inputs, opts);
    connect(worker_, &CullWorker::progress, this, &CullPage::onProgress, Qt::UniqueConnection);
    connect(worker_, &CullWorker::finished, this, &CullPage::onFinished, Qt::UniqueConnection);
    worker_->start();
}

void CullPage::autoRun(int cap)
{
    if (!photos_) return;
    QVector<CullInput> inputs;
    inputs.reserve(photos_->size());
    for (const Photo& ph : *photos_) {
        if (ph.thumbFile.isEmpty()) continue;
        CullInput in;
        in.thumbPath = QDir(thumbsDir_).filePath(ph.thumbFile);
        parseDim(ph.dim, in.origW, in.origH);
        in.isRaw = isRawSuffix(QFileInfo(ph.path).suffix());
        in.category = ph.category;
        inputs.append(in);
    }
    if (inputs.isEmpty()) { progress_->setText(QString::fromUtf8("无可用缩略图")); return; }
    if (cap > 0 && inputs.size() > cap) inputs = inputs.mid(0, cap);
    CullOptions opts;
    opts.topN = topNSpin_->value();
    const CullReport rep = CullEngine::analyze(inputs, opts);
    onFinished(rep);
}

void CullPage::onProgress(int cur, int total, const QString&)
{
    progress_->setText(QString::fromUtf8("分析中 %1/%2").arg(cur).arg(total));
}

void CullPage::onFinished(const CullReport& rep)
{
    runBtn_->setEnabled(true);
    setKpi(0, QString::number(rep.total), QString::fromUtf8("已分析"));
    setKpi(1, QString::number(rep.candidateCount), QString::fromUtf8("候选"));
    setKpi(2, QString::number(rep.rejectedCount), QString::fromUtf8("废片"));
    setKpi(3, QString::number(rep.dupGroups.size()), QString::fromUtf8("去重组"));
    setKpi(4, QString::number(rep.blurryCount), QString::fromUtf8("模糊"));
    setKpi(5, QString::number(rep.exposureCount), QString::fromUtf8("曝光"));
    setKpi(6, QString::number(rep.unknownCount), QString::fromUtf8("解码失败"));

    model_->setItems(rep.topItems, thumbsDir_);
    catCounts_ = model_->categoryCounts();
    buildChips(catCounts_);
    // 复位筛选
    activeCat_.clear();
    proxy_->setCategoryFilter(QString());
    markCombo_->setCurrentIndex(0);
    proxy_->setMarkFilter(QString());

    rebuildLockArea();
    rebuildFilmstrip();
    updateRadar();

    gridStack_->setCurrentIndex(model_->rowCount() > 0 ? 1 : 0);

    // P25：对决素材组队（每次分析后刷新）
    lastReport_ = rep;
    buildDuelSession();

    if (!qEnvironmentVariableIsEmpty("LA_CULL_DUEL_QA")) {
        const QString png = qEnvironmentVariable("LA_CULL_DUEL_QA");
        // 构造期窗口未 show：布局未激活，grab()/render() 都拿不到子件 → 延迟到事件循环+show 后再抓
        QTimer::singleShot(600, this, [this, png] { runDuelQa(png); });
        return;
    }

    if (!qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")) {
        // 自测断言：标记 → 锁定区 / 胶片条 / 雷达 联动
        int markLock = 0, markLike = 0, markSkip = 0, filmBtns = 0;
        bool lockOk = false;
        if (model_->rowCount() >= 3) {
            model_->setMark(0, QStringLiteral("lock"));
            model_->setMark(1, QStringLiteral("like"));
            model_->setMark(2, QStringLiteral("skip"));
            rebuildLockArea();
            rebuildFilmstrip();
            updateRadar();
            markLock = model_->markedThumbs(QStringLiteral("lock")).size();
            markLike = model_->markedThumbs(QStringLiteral("like")).size();
            markSkip = model_->markedThumbs(QStringLiteral("skip")).size();
            lockOk = (lockCount_ == 1) && (lockList_->count() == 1);
            if (auto* fl2 = qobject_cast<QHBoxLayout*>(filmRow_->layout()))
                for (int i = 0; i < fl2->count(); ++i)
                    if (qobject_cast<QToolButton*>(fl2->itemAt(i)->widget())) ++filmBtns;
        }
        std::fprintf(stderr,
            "LA_CULL_SMOKE total=%d cand=%d rej=%d dup=%d blur=%d exp=%d unk=%d top=%d cats=%d radarN=%d\n",
            rep.total, rep.candidateCount, rep.rejectedCount, rep.dupGroups.size(),
            rep.blurryCount, rep.exposureCount, rep.unknownCount, rep.topItems.size(),
            catCounts_.size(), lastRadarN_);
        std::fprintf(stderr,
            "LA_CULL_SMOKE_MARKS lock=%d like=%d skip=%d lockOk=%d film=%d radarMarked=%d\n",
            markLock, markLike, markSkip, lockOk ? 1 : 0, filmBtns, lastRadarN_);
        std::fflush(stderr);
        QApplication::quit();
    } else {
        progress_->setText(QString::fromUtf8("完成 · 候选 %1 / 废片 %2 / 去重 %3 组")
            .arg(rep.candidateCount).arg(rep.rejectedCount).arg(rep.dupGroups.size()));
    }
}

// ---------------- P25 对决模式 ----------------

// 从报告去重组重建仲裁队：保留者（候选）+ 被拒相似项（dupItems）。
// 引擎已按分自动保留每组一张；对决让人重新裁决谁配得上留用。
void CullPage::buildDuelSession()
{
    duelGroups_.clear();
    duelIdx_ = 0;
    QHash<QString, CullItem> extra;
    for (const CullItem& it : lastReport_.dupItems) extra.insert(it.thumbPath, it);
    for (const CullDupGroup& dg : lastReport_.dupGroups) {
        CullItem kept;
        bool have = false;
        for (const CullItem& c : lastReport_.topItems) {
            if (c.thumbPath == dg.keptThumb) { kept = c; have = true; break; }
        }
        if (!have) {
            if (extra.contains(dg.keptThumb)) kept = extra.value(dg.keptThumb);
            else continue;                    // 组员缺失（理论上不发生）→ 跳过该组
        }
        DuelGroup g;
        g.members.append(kept);
        for (const QString& t : dg.similar)
            if (extra.contains(t)) g.members.append(extra.value(t));
        if (g.members.size() < 2) continue;   // 单员不成对决
        for (int i = 0; i < g.members.size(); ++i) g.alive.append(i);
        duelGroups_.append(g);
    }
    duelBtn_->setEnabled(!duelGroups_.isEmpty());
    if (!duelGroups_.isEmpty())
        duelBtn_->setText(QString::fromUtf8("开始对决（%1 组）").arg(duelGroups_.size()));
}

void CullPage::duelEnterMode()
{
    if (duelGroups_.isEmpty()) return;
    gridStack_->setCurrentIndex(2);
    duel_->setFocus();
    duelRender();
}

void CullPage::duelExitMode()
{
    gridStack_->setCurrentIndex(model_->rowCount() > 0 ? 1 : 0);
}

// 对决标记落库：被拒相似项不在模型 → 先升格入模再标记（总览/胶片条/雷达全联动）
void CullPage::markDuelItem(const CullItem& it, const QString& mark)
{
    int row = model_->rowOfThumb(it.thumbPath);
    if (row < 0) row = model_->upsertItem(it, thumbsDir_);
    if (row >= 0) model_->setMark(row, mark);
}

void CullPage::duelPick(int side)
{
    if (duelIdx_ >= duelGroups_.size()) return;
    DuelGroup& g = duelGroups_[duelIdx_];
    if (g.alive.size() < 2) return;
    const int loserVal = g.alive[1 - side];
    markDuelItem(g.members[loserVal], QStringLiteral("skip"));
    g.alive.removeOne(loserVal);
    duelAfterDecision();
}

void CullPage::duelBoth(const QString& mark)
{
    if (duelIdx_ >= duelGroups_.size()) return;
    DuelGroup& g = duelGroups_[duelIdx_];
    if (g.alive.size() < 2) return;
    markDuelItem(g.members[g.alive[0]], mark);
    markDuelItem(g.members[g.alive[1]], mark);
    g.alive.removeFirst();
    g.alive.removeFirst();
    duelAfterDecision();
}

void CullPage::duelAfterDecision()
{
    rebuildLockArea();
    rebuildFilmstrip();
    DuelGroup& g = duelGroups_[duelIdx_];
    if (g.alive.size() == 1) {                // 组内唯一幸存者 → 留用，进下一组
        markDuelItem(g.members[g.alive.first()], QStringLiteral("like"));
        updateRadar();
        ++duelIdx_;
    } else if (g.alive.isEmpty()) {           // 全员出局 → 下一组
        updateRadar();
        ++duelIdx_;
    }
    duelRender();
}

void CullPage::duelRender()
{
    if (duelIdx_ >= duelGroups_.size()) {
        paneL_->clear();
        paneR_->clear();
        duelHead_->setText(QString::fromUtf8("对决完成 · 共 %1 组 · 点「返回总览」查看战果")
                           .arg(duelGroups_.size()));
        return;
    }
    const DuelGroup& g = duelGroups_[duelIdx_];
    const CullItem& L = g.members[g.alive[0]];
    const CullItem& R = g.members[g.alive[1]];
    duelHead_->setText(QString::fromUtf8("第 %1 / %2 组 · 本组已决 %3 · 在场 %4")
        .arg(duelIdx_ + 1).arg(duelGroups_.size())
        .arg(g.members.size() - g.alive.size()).arg(g.alive.size()));
    auto setPane = [this](DuelPane* pane, const CullItem& it) {
        QPixmap pm(QDir(thumbsDir_).filePath(it.thumbPath));
        if (!pm.isNull())
            pm = pm.scaled(480, 340, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pane->setItem(it, pm);
    };
    setPane(paneL_, L);
    setPane(paneR_, R);
    paneL_->setFavorite(L.total > R.total);   // 引擎倾向：分高者金框
    paneR_->setFavorite(R.total > L.total);
}

// P25 对决键盘流：← 左留 / → 右留 / ↓ 双弃 / S 双锁 / Esc 返回总览
bool CullPage::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == duel_ && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        switch (ke->key()) {
            case Qt::Key_Left:  duelPick(0); return true;
            case Qt::Key_Right: duelPick(1); return true;
            case Qt::Key_Down:  duelBoth(QStringLiteral("skip")); return true;
            case Qt::Key_S:     duelBoth(QStringLiteral("lock")); return true;
            case Qt::Key_Escape: duelExitMode(); return true;
            default: break;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// P25 QA：离屏自测——组队（不足则用前 3 候选合成）→ 左留仲裁 → 标记落库 + 金像素实测
void CullPage::runDuelQa(const QString& png)
{
    if (duelGroups_.isEmpty() && model_->rowCount() >= 3) {
        DuelGroup g;
        for (int i = 0; i < 3 && i < model_->rowCount(); ++i)
            g.members.append(model_->itemAt(i));
        for (int i = 0; i < g.members.size(); ++i) g.alive.append(i);
        duelGroups_.append(g);
    }
    const int groups = duelGroups_.size();
    const int members0 = groups > 0 ? duelGroups_[0].members.size() : 0;
    duelEnterMode();
    const int skipBefore = model_->markedThumbs(QStringLiteral("skip")).size();
    duelPick(0);                              // 左留 → 右侧标 skip
    const int skipAfter = model_->markedThumbs(QStringLiteral("skip")).size();
    const bool markOk = (skipAfter == skipBefore + 1);
    duel_->resize(920, 580);
    if (QLayout* ly = duel_->layout())
        ly->activate();                        // 双保险：抓图前强推一次布局
    QCoreApplication::processEvents(QEventLoop::AllEvents, 150);
    QImage img(920, 580, QImage::Format_RGB32);
    img.fill(Qt::red);                        // QA 诊断底：若图发红说明 render 什么都没画
    {
        QPainter rp(&img);
        duel_->render(&rp, QPoint(0, 0), QRegion(duel_->rect()),
                      QWidget::DrawWindowBackground | QWidget::DrawChildren);
        rp.end();
    }
    const bool saveOk = img.save(png);
    std::fprintf(stderr, "  duelSize=920x580 saveOk=%d\n", int(saveOk));
    std::fflush(stderr);
    long gold = 0;                            // 金 #d9a94e：引擎倾向金框/徽章/分条
    for (int y = 0; y < img.height(); y += 2) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb px = row[x];
            if (qRed(px) > 170 && qGreen(px) > 120 && qGreen(px) < 205 && qBlue(px) < 130)
                ++gold;
        }
    }
    img.save(png);
    std::fprintf(stderr,
        "LA_CULL_DUEL_QA groups=%d members0=%d skipDelta=%d markOk=%d gold=%ld png=%s\n",
        groups, members0, skipAfter - skipBefore, int(markOk), gold, qPrintable(png));
    std::fflush(stderr);
    QApplication::quit();
}

#include "CullPage.moc"


#include "CullPage.h"
#include "../core/qaExit.h"   // 2.7.0 门禁自退契约（禁裸调 quit）
#include "../core/RawDecode.h"  // 2.29.0 RAW 判据唯一真源（禁在本文件另立表）
#include "../core/MotionKit.h"  // 2.34.0 动效引擎
#include "../core/MotionFX.h"   // 2.34.0 心形粒子爆开
#include "GalleryCardPainter.h" // 2.31.0 视频徽标统一绘制
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
// 2.31.0 QA 反例钩子：关闭视频徽标绘制，用于 LA_CULL_VIDEO_QA 反例验证。
static bool g_qaDisableVideoBadge = false;

// 2.29.0：原先此处有一份**独立的** RAW 扩展名表（16 种），与
// core/RawDecode.cpp 的 kRawExt[]（24 种）漂移 8 种 —— fff/iiq/k25/kdc/mos/ptx/srf/srw
// （Sigma / Phase One / Pentax / Mamiya / Ricoh / Sony）拿不到 RAW 豁免，
// 会被当普通照片走 Laplacian+曝光淘汰；而 RAW 豁免存在的理由正是「缩略图是 JPEG、
// 暗部 Laplacian 系统性失真」，故这些 RAW **必然被误杀**。
// 现统一走 RawDecode::isRawExt() 唯一真源（铁律 6）。视频判定由 mediaType 驱动。

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

// design-language-v2 T5：chip 族单源。Theme.h 无 chip() 工厂（pill() 是 QLabel 胶囊，
// 与本处可勾选 QPushButton 形状不合），按 brief Step 3 裁定的「单一本地函数」落地：
// buildChips（初建）与 onChipClicked（切态）两处共用，杜绝双份 CSS 漂移。
// 色彩全部走 Lumen token（spec §1.7），圆角/内距维持原值（归格属 T9/T10）。
static QString cullChipCss(bool checked)
{
    if (checked)
        return QString::fromUtf8(
            "QPushButton{background:%1;border:1px solid %2;border-radius:999px;"
            "padding:5px 14px;color:%3;%4font-weight:bold;}")
            .arg(Lumen::hex(Lumen::CARD_HI))
            .arg(Lumen::hex(Lumen::EMERALD))
            .arg(Lumen::hex(Lumen::EMERALD))
            .arg(Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：12px=T5 原位
    return QString::fromUtf8(
        "QPushButton{background:%1;border:1px solid %2;border-radius:999px;"
        "padding:5px 14px;color:%3;%4}"
        "QPushButton:hover{border-color:%5;color:%6;}")
        .arg(Lumen::hex(Lumen::CARD))
        .arg(Lumen::hex(Lumen::LINE))
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small))        // T8：12px=T5 原位
        .arg(Lumen::hex(Lumen::EMERALD))
        .arg(Lumen::hex(Lumen::INK));
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
        case MediaTypeRole: return r.item.mediaType;
        case DurRole:      return r.item.durSec;
        case IsVideoRole:  return r.item.isVideo;
        case PrivRole:     return r.item.priv;
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
    for (const Row& r : items_) {
        if (hidePrivate_ && r.item.priv) continue;   // 2.33.0 锁定项不计入 chips 数字
        if (!r.item.category.isEmpty()) m[r.item.category]++;
    }
    return m;
}

QVector<QString> CullModel::markedThumbs(const QString& mark) const
{
    QVector<QString> v;
    for (const Row& r : items_) {
        if (hidePrivate_ && r.item.priv) continue;   // 2.33.0 锁定项不进锁定区/胶片条
        if (r.mark == mark) v.append(r.item.thumbPath);
    }
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
    if (videoOnly_) {
        if (!idx.data(CullModel::IsVideoRole).toBool()) return false;
    }
    if (hidePrivate_) {   // 2.33.0 隐私门：锁定时私密卡片不进结果网格（与图库同口径）
        if (idx.data(CullModel::PrivRole).toBool()) return false;
    }
    return true;
}

// ---------------------------------------------------------------- 委托
CullCardDelegate::CullCardDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void CullPage::qaSetVideoBadgeDisabled(bool disabled)
{
    g_qaDisableVideoBadge = disabled;
}

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
    bool isVideo = false;
    double durSec = 0;
    if (!pm.isNull()) {
        const QPixmap scaled = pm.scaled(imgR.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p->drawPixmap(imgR.x() + (imgR.width() - scaled.width()) / 2,
                      imgR.y() + (imgR.height() - scaled.height()) / 2, scaled);
    } else {
        p->setPen(QColor(0x5a, 0x6b, 0x60));
        p->drawText(imgR, Qt::AlignCenter, QCoreApplication::translate("LightAlbum", "无缩略图"));
    }
    // 2.31.0 视频徽标：统一走 Lumen::paintVideoBadge，与图库页视觉一致。
    // QA 反例可通过 g_qaDisableVideoBadge 关闭绘制。
    isVideo = idx.data(CullModel::IsVideoRole).toBool();
    durSec  = idx.data(CullModel::DurRole).toDouble();
    if (isVideo && !g_qaDisableVideoBadge)
        Lumen::paintVideoBadge(p, r, durSec);

    const QString grade = idx.data(CullModel::GradeRole).toString();
    const double score = idx.data(CullModel::ScoreRole).toDouble();
    const QString status = idx.data(CullModel::StatusRole).toString();
    const QString reason = idx.data(CullModel::ReasonRole).toString();
    const QString mark = idx.data(CullModel::MarkRole).toString();

    QRect txtR(r.x() + 10, imgR.bottom() + 6, r.width() - 20, r.height() - imgR.height() - 12);
    p->setPen(QColor(0xf4, 0xf7, 0xf4));
    QFont f = Lumen::Type::head();   // T8：13pt 粗体分级行归 T3 head（12pt 粗）
    p->setFont(f);
    p->drawText(txtR, Qt::AlignLeft, QStringLiteral("%1  %2").arg(grade).arg(score, 0, 'f', 1));

    p->setPen(QColor(0x8d, 0xa3, 0x96));
    f = Lumen::Type::smallText(); p->setFont(f);   // T8：10pt 副文案归 T5
    QString sub;
    if (mark == QStringLiteral("like"))      sub = QCoreApplication::translate("LightAlbum", "♥ 喜欢");
    else if (mark == QStringLiteral("skip"))  sub = QCoreApplication::translate("LightAlbum", "已略过");
    else if (mark == QStringLiteral("lock"))  sub = QCoreApplication::translate("LightAlbum", "已锁定");
    else sub = (status == QStringLiteral("rejected")) ? reason
                                                   : QCoreApplication::translate("LightAlbum", "本地精选");
    p->drawText(txtR.adjusted(0, 18, 0, 0), Qt::AlignLeft | Qt::AlignTop, sub);

    // 标记徽标（单字，避免 emoji 字体缺失）：喜/略/锁
    if (!mark.isEmpty()) {
        QString ch, col;
        if (mark == QStringLiteral("like"))      { ch = QCoreApplication::translate("LightAlbum", "喜"); col = Lumen::hex(Lumen::EMERALD); }
        else if (mark == QStringLiteral("skip")) { ch = QCoreApplication::translate("LightAlbum", "略"); col = Lumen::hex(Lumen::CORAL); }
        else { ch = QCoreApplication::translate("LightAlbum", "锁"); col = Lumen::hex(Lumen::GOLD); }
        QRect br(imgR.x() + 6, imgR.y() + 6, 22, 22);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x06, 0x08, 0x06, 210));
        p->drawRoundedRect(br, 6, 6);
        p->setPen(QColor(col));
        QFont bf = Lumen::Type::head();   // T8：12pt 粗体徽标即 head 本尊
        p->setFont(bf);
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
    QFont f = Lumen::Type::smallText(); p.setFont(f);   // T8：轴标签 10pt 归 T5
    QFont vf = Lumen::Type::mono();                     // §2.2：雷达数值收编 mono()
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
            p.setFont(vf);
            p.drawText(QRectF(lx - 42, ly + 7, 84, 14), align | Qt::AlignVCenter,
                       QString::number(int(vals_[i])) + "%");
            p.setFont(f);
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
            p.drawText(img, Qt::AlignCenter, QCoreApplication::translate("LightAlbum", "无缩略图"));
        }

        if (fav_) {   // 引擎倾向：金底徽章（分高者）
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xd9, 0xa9, 0x4e));
            const QRectF b(r.x() + 10, r.y() + 10, 66, 22);
            p.drawRoundedRect(b, 7, 7);
            p.setPen(QColor(0x17, 0x13, 0x08));
            QFont f = Lumen::Type::smallText(); p.setFont(f);   // T8：10.5pt 金章归 T5（粗体随档归一）
            p.drawText(b, Qt::AlignCenter, QCoreApplication::translate("LightAlbum", "引擎倾向"));
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
        QFont f = Lumen::Type::head(); p.setFont(f);   // T8：13pt 粗体分级行归 T3 head
        p.drawText(QRect(r.x() + 12, r.bottom() - 28, r.width() - 24, 20), Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(it_.grade).arg(it_.total, 0, 'f', 1));
        p.setPen(QColor(0x8d, 0xa3, 0x96));
        f = Lumen::Type::body(); p.setFont(f);   // T8：9.5pt 即 body 本尊
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
    const QStringList labels = {QCoreApplication::translate("LightAlbum", "已分析"), QCoreApplication::translate("LightAlbum", "候选"),
                                QCoreApplication::translate("LightAlbum", "废片"), QCoreApplication::translate("LightAlbum", "去重组"),
                                QCoreApplication::translate("LightAlbum", "模糊"), QCoreApplication::translate("LightAlbum", "曝光"),
                                QCoreApplication::translate("LightAlbum", "解码失败")};
    for (const QString& l : labels) {
        auto* lb = new QLabel(QString::fromUtf8(
            "<b style='%4color:%1;'>—</b><br>"
            "<span style='%5color:%2;'>%3</span>")
            .arg(Lumen::hex(Lumen::INK))
            .arg(Lumen::hex(Lumen::MUTED))
            .arg(l)
            .arg(Lumen::Type::css(Lumen::Type::Tier::Head))    // T8：15px 主数值就近归 T3(16px)
            .arg(Lumen::Type::css(Lumen::Type::Tier::Small)),  // T8：11px 副标签归 T5(12px)
            kp);
        lb->setAlignment(Qt::AlignCenter);
        lb->setMinimumWidth(80);
        lb->setStyleSheet(QString::fromUtf8("QLabel{background:%1;border:none;"
            "border-radius:10px;padding:8px 14px;}").arg(Lumen::hex(Lumen::CARD)));
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

    runBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "开始分析"), bar);
    runBtn_->setStyleSheet(Lumen::btnGold());

    auto* stepWrap = new QWidget(bar);
    auto* sl = new QHBoxLayout(stepWrap);
    sl->setContentsMargins(0, 0, 0, 0); sl->setSpacing(6);
    auto* stepLbl = new QLabel(QCoreApplication::translate("LightAlbum", "目标张数"), bar);
    stepLbl->setStyleSheet(QStringLiteral("color:%1;").arg(Lumen::hex(Lumen::MUTED)));
    auto* minus = new QPushButton(QString::fromUtf8("−"), bar);
    auto* plus  = new QPushButton(QString::fromUtf8("+"), bar);
    topNSpin_ = new QSpinBox(bar);
    topNSpin_->setRange(5, 600);
    topNSpin_->setValue(60);
    topNSpin_->setSuffix(QCoreApplication::translate("LightAlbum", " 张"));
    topNSpin_->setStyleSheet(Lumen::input());
    minus->setFixedWidth(26); plus->setFixedWidth(26);
    minus->setStyleSheet(QString::fromUtf8("QPushButton{background:%1;border:1px solid %2;"
        "border-radius:7px;color:%3;}")
        .arg(Lumen::hex(Lumen::CARD))
        .arg(Lumen::hex(Lumen::LINE))
        .arg(Lumen::hex(Lumen::INK)));
    plus->setStyleSheet(minus->styleSheet());
    sl->addWidget(stepLbl); sl->addWidget(minus); sl->addWidget(topNSpin_); sl->addWidget(plus);
    connect(minus, &QPushButton::clicked, this, [this]{ topNSpin_->setValue(topNSpin_->value() - 10); });
    connect(plus,  &QPushButton::clicked, this, [this]{ topNSpin_->setValue(topNSpin_->value() + 10); });

    markCombo_ = new QComboBox(bar);
    markCombo_->addItem(QCoreApplication::translate("LightAlbum", "全部"), QString());
    markCombo_->addItem(QCoreApplication::translate("LightAlbum", "只看喜欢"), QStringLiteral("like"));
    markCombo_->addItem(QCoreApplication::translate("LightAlbum", "只看略过"), QStringLiteral("skip"));
    markCombo_->addItem(QCoreApplication::translate("LightAlbum", "只看锁定"), QStringLiteral("lock"));
    markCombo_->addItem(QCoreApplication::translate("LightAlbum", "只看视频"), QStringLiteral("__video__"));
    markCombo_->setStyleSheet(Lumen::input());

    progress_ = new QLabel(QCoreApplication::translate("LightAlbum", "基于本地缩略图 · 零云端 · 失败即放行"), bar);
    progress_->setStyleSheet(QStringLiteral("color:%1;").arg(Lumen::hex(Lumen::MUTED)));

    bl->addWidget(runBtn_);

    // P25 对决模式入口（分析完成后可点；空结果禁用）
    duelBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "开始对决"), bar);
    duelBtn_->setStyleSheet(Lumen::btnOutline(Lumen::GOLD));
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
    selBar_->setObjectName(QStringLiteral("cullSelBar"));
    selBar_->setStyleSheet(QStringLiteral("#cullSelBar{background:%1;border:none;"
        "border-radius:10px;}").arg(Lumen::hex(Lumen::INSET)));
    selBar_->setVisible(false);
    selThumb_ = new QLabel(selBar_);
    selThumb_->setFixedSize(56, 56);
    selThumb_->setStyleSheet(QStringLiteral("QLabel{background:#000;border-radius:8px;}"));
    selThumb_->setAlignment(Qt::AlignCenter);
    selName_ = new QLabel(selBar_);
    selName_->setStyleSheet(QStringLiteral("color:%1;%2")
        .arg(Lumen::hex(Lumen::INK))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：12px=T5 原位
    auto mkMark = [this](const QString& t, const QString& m, const QString& css){
        auto* b = new QPushButton(t, selBar_);
        b->setStyleSheet(css);
        connect(b, &QPushButton::clicked, this, [this, m]{ onMark(m); });
        return b;
    };
    // 描边按钮族单源（spec §4.1）：like/skip/lock 各按原 accent 走 btnOutline；
    // unlock 原是中性幽灵（LINE 边 + MUTED 字）→ btnGhost(MUTED)（btnOutline(LINE)
    // 会把文字洗成描边色不可读，保语义优先——brief Step 3「keep semantics」）
    likeBtn_  = mkMark(QCoreApplication::translate("LightAlbum", "喜欢"), QStringLiteral("like"),
        Lumen::btnOutline(Lumen::EMERALD));
    skipBtn_  = mkMark(QCoreApplication::translate("LightAlbum", "略过"), QStringLiteral("skip"),
        Lumen::btnOutline(Lumen::CORAL));
    lockBtn_  = mkMark(QCoreApplication::translate("LightAlbum", "锁定"), QStringLiteral("lock"),
        Lumen::btnOutline(Lumen::GOLD));
    unlockBtn_ = mkMark(QCoreApplication::translate("LightAlbum", "解除"), QString(),
        Lumen::btnGhost(Lumen::MUTED));
    sbl->addWidget(selThumb_);
    sbl->addWidget(selName_, 1);
    sbl->addWidget(likeBtn_); sbl->addWidget(skipBtn_); sbl->addWidget(lockBtn_); sbl->addWidget(unlockBtn_);
    root->addWidget(selBar_);

    // 锁定区（顶部）
    lockFrame_ = new QFrame(this);
    lockFrame_->setFrameShape(QFrame::StyledPanel);
    lockFrame_->setObjectName(QStringLiteral("cullLockFrame"));
    lockFrame_->setStyleSheet(QStringLiteral("#cullLockFrame{background:%1;border:none;border-radius:12px;}")
        .arg(Lumen::hex(Lumen::INSET)));
    auto* ll = new QVBoxLayout(lockFrame_);
    ll->setContentsMargins(12, 10, 12, 10); ll->setSpacing(8);
    lockTitle_ = new QLabel(QCoreApplication::translate("LightAlbum", "锁定区"), lockFrame_);
    lockTitle_->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;%2")
        .arg(Lumen::hex(Lumen::GOLD))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Body)));   // T8：13px=T4 原位（区块粗体留串）
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
    view_->setStyleSheet(QStringLiteral("QListView{background:%1;border:none;"
        "border-radius:12px;} QListView::item:selected{background:transparent;}")
        .arg(Lumen::hex(Lumen::PANEL)));
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
    emptyHint->setStyleSheet(QStringLiteral("QLabel{color:%1;%2background:transparent;}")
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Body)));   // T8：13px=T4 原位
    gridStack_->addWidget(emptyHint);   // 0
    gridStack_->addWidget(view_);       // 1
    gridStack_->setCurrentIndex(0);
    root->addWidget(gridStack_, 1);

    // ---- P25 B「对决模式」：同组两两 PK——引擎按分预选，人只做仲裁 ----
    auto* duelFrame = new QFrame(this);        // QWidget 不画 QSS 底，必须 QFrame
    duelFrame->setFrameShape(QFrame::StyledPanel);
    duelFrame->setObjectName(QStringLiteral("cullDuelFrame"));
    duelFrame->setStyleSheet(QStringLiteral(
        "#cullDuelFrame{background:%1;border:none;border-radius:12px;}")
        .arg(Lumen::hex(Lumen::PANEL)));
    duel_ = duelFrame;
    duel_->setFocusPolicy(Qt::StrongFocus);
    auto* dv = new QVBoxLayout(duel_);
    dv->setContentsMargins(12, 10, 12, 10);
    dv->setSpacing(10);
    auto* hrow = new QHBoxLayout;
    hrow->setSpacing(10);
    duelHead_ = new QLabel(duel_);
    duelHead_->setStyleSheet(QStringLiteral("color:%1;%2border:none;background:transparent;")
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：12px=T5 原位
    duelBackBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "返回总览 Esc"), duel_);
    duelBackBtn_->setStyleSheet(Lumen::btnGhost(Lumen::MUTED));
    hrow->addWidget(duelHead_, 1);
    hrow->addWidget(duelBackBtn_);
    dv->addLayout(hrow);

    auto* mid = new QHBoxLayout;
    mid->setSpacing(14);
    paneL_ = new DuelPane(duel_);
    paneR_ = new DuelPane(duel_);
    auto* pk = new QLabel(QString::fromUtf8("PK"), duel_);
    pk->setAlignment(Qt::AlignCenter);
    // 金边徽章保持 QLabel 配方（不重构成按钮）：色彩改走 GOLD token 引用——
    // 描边即 GOLD @45% alpha（= btnOutline(GOLD) 同族的金描边语义）；
    // 1.5px 描边 / 22px 圆角留给 T9；T8 已收编：Georgia 衬线泄漏降级
    // font-family 删除 + 14px → Type::css(Head)=16px（spec §2.3 编辑特权归位）。
    pk->setStyleSheet(QStringLiteral(
        "QLabel{color:%1;border:1.5px solid rgba(%2,%3,%4,.45);border-radius:22px;"
        "font-weight:bold;%5"
        "min-width:40px;min-height:40px;background:transparent;}")
        .arg(Lumen::hex(Lumen::GOLD))
        .arg(Lumen::GOLD.red())
        .arg(Lumen::GOLD.green())
        .arg(Lumen::GOLD.blue())
        .arg(Lumen::Type::css(Lumen::Type::Tier::Head)));   // T8：Georgia 14px → T3 head
    mid->addWidget(paneL_, 1);
    mid->addWidget(pk, 0, Qt::AlignVCenter);
    mid->addWidget(paneR_, 1);
    dv->addLayout(mid, 1);

    auto* brow = new QHBoxLayout;
    brow->setSpacing(10);
    auto mkDuelBtn = [this](const QString& t, std::function<void()> fn) {
        auto* b = new QPushButton(t, duel_);
        b->setStyleSheet(Lumen::btnGhost(Lumen::MUTED));
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    brow->addWidget(mkDuelBtn(QCoreApplication::translate("LightAlbum", "左留 ←"), [this]{ duelPick(0); }));
    brow->addWidget(mkDuelBtn(QCoreApplication::translate("LightAlbum", "双弃 ↓"), [this]{ duelBoth(QStringLiteral("skip")); }));
    brow->addWidget(mkDuelBtn(QCoreApplication::translate("LightAlbum", "双锁 S"), [this]{ duelBoth(QStringLiteral("lock")); }));
    brow->addWidget(mkDuelBtn(QCoreApplication::translate("LightAlbum", "右留 →"), [this]{ duelPick(1); }));
    auto* hint = new QLabel(
        QCoreApplication::translate("LightAlbum", "引擎按分预选（金框）· 败者自动标「略过」· 幸存者留用"), duel_);
    hint->setStyleSheet(QString::fromUtf8("color:%1;%2border:none;background:transparent;")
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：11px→T5(12px)
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
    filmScroll_->setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;"
        "border-radius:12px;}").arg(Lumen::hex(Lumen::PANEL)));
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
    auto* rt = new QLabel(QCoreApplication::translate("LightAlbum", "审美档案"), side);
    rt->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;%2")
        .arg(Lumen::hex(Lumen::INK))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Body)));   // T8：14px→T4(13px) 就近
    radar_ = new RadarWidget(side);
    radar_->setFixedSize(300, 320);
    radarNote_ = new QLabel(side);
    radarNote_->setWordWrap(true);
    radarNote_->setStyleSheet(QStringLiteral("color:%1;%2")
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：11px→T5(12px)
    radarCount_ = new QLabel(side);
    radarCount_->setStyleSheet(QStringLiteral("color:%1;%2")
        .arg(Lumen::hex(Lumen::GOLD))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：11px→T5(12px)
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
        "<b style='%5color:%1;'>%2</b><br>"
        "<span style='%6color:%3;'>%4</span>")
        .arg(Lumen::hex(Lumen::INK))
        .arg(v)
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(label)
        .arg(Lumen::Type::css(Lumen::Type::Tier::Head))    // T8：15px 主数值就近归 T3(16px)
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：11px→T5(12px)
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
        b->setStyleSheet(cullChipCss(false));
        connect(b, &QPushButton::clicked, this, [this, cat]{ onChipClicked(cat); });
        chips_.append(b);
        cl->addWidget(b);
        return b;
    };
    mk(QCoreApplication::translate("LightAlbum", "全部"), QString(), model_->rowCount());

    // 2.31.0 视频适配：若有视频项，在场景 chips 最前追加「视频」分类（按 isVideo 而非 category）
    int videoCnt = 0;
    for (int i = 0; i < model_->rowCount(); ++i)
        if (model_->data(model_->index(i, 0), CullModel::IsVideoRole).toBool())
            ++videoCnt;
    if (videoCnt > 0)
        mk(QCoreApplication::translate("LightAlbum", "视频"), QStringLiteral("__video__"), videoCnt);

    for (auto it = counts.begin(); it != counts.end(); ++it)
        mk(it.key(), it.key(), it.value());
    cl->addStretch(1);
}

void CullPage::onChipClicked(const QString& cat)
{
    activeCat_ = (activeCat_ == cat) ? QString() : cat;
    if (activeCat_ == QStringLiteral("__video__")) {
        proxy_->setCategoryFilter(QString());
        proxy_->setVideoOnly(true);
    } else {
        proxy_->setCategoryFilter(activeCat_);
        proxy_->setVideoOnly(false);
    }
    for (QPushButton* b : chips_)
        b->setStyleSheet(cullChipCss(b->property("cat").toString() == activeCat_));
}

void CullPage::onMarkFilterChanged(int idx)
{
    if (!markCombo_) return;
    const QString data = markCombo_->itemData(idx).toString();
    if (data == QStringLiteral("__video__")) {
        proxy_->setMarkFilter(QString());
        proxy_->setVideoOnly(true);
    } else {
        proxy_->setMarkFilter(data);
        proxy_->setVideoOnly(false);
    }
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
    else { selThumb_->clear(); selThumb_->setText(QCoreApplication::translate("LightAlbum", "无")); }
    const QString mark = model_->data(model_->index(src.row(), 0), CullModel::MarkRole).toString();
    QString tag;
    if (mark == QStringLiteral("like"))      tag = QCoreApplication::translate("LightAlbum", " · 已喜欢");
    else if (mark == QStringLiteral("skip")) tag = QCoreApplication::translate("LightAlbum", " · 已略过");
    else if (mark == QStringLiteral("lock")) tag = QCoreApplication::translate("LightAlbum", " · 已锁定");
    selName_->setText(QFileInfo(thumb).fileName() + tag);
    selBar_->setVisible(true);
}

void CullPage::onMark(const QString& mark)
{
    const QModelIndex cur = view_->currentIndex();
    if (!cur.isValid()) return;
    const QModelIndex src = proxy_->mapToSource(cur);
    model_->setMark(src.row(), mark);
    if (mark == QStringLiteral("like") && qEnvironmentVariableIsEmpty("LA_GATE")) {
        // 2.34 HeartBurst：心形粒子在卡片中心爆开（轨迹=引擎纯函数，瞬时层自毁）。
        const QRect vr = view_->visualRect(cur);
        MotionFx::heartBurst(view_->viewport(),
                             vr.center() - view_->viewport()->geometry().topLeft());
    }
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
    lockTitle_->setText(QCoreApplication::translate("LightAlbum", "锁定区（%1）").arg(lockCount_));
    if (locked.isEmpty()) {
        auto* hint = new QListWidgetItem(lockList_);
        hint->setText(QCoreApplication::translate("LightAlbum", "锁定照片会出现在这里 · 选中卡片后点「锁定」"));
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
        auto* hint = new QLabel(QCoreApplication::translate("LightAlbum", "保留作品会出现在这条胶片 · 选中后点 ♥/锁"),
                                filmRow_);
        hint->setStyleSheet(QString::fromUtf8("color:%1;%2")
            .arg(Lumen::hex(Lumen::MUTED))
            .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：11px→T5(12px)
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
            for (const Photo& ph : *photos_) {
                if (hidePrivate_ && ph.priv) continue;   // 2.33.0 隐私门
                if (!ph.thumbFile.isEmpty()) set.append(&ph);
            }
        } else {
            for (const Photo& ph : *photos_) {
                if (hidePrivate_ && ph.priv) continue;   // 2.33.0 隐私门
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
    const QStringList labels = {QCoreApplication::translate("LightAlbum", "广角"), QCoreApplication::translate("LightAlbum", "长焦"),
        QCoreApplication::translate("LightAlbum", "大光圈"), QCoreApplication::translate("LightAlbum", "弱光"),
        QCoreApplication::translate("LightAlbum", "人像"), QCoreApplication::translate("LightAlbum", "黑白")};
    const QString note = (n < 30)
        ? QCoreApplication::translate("LightAlbum", "样本较少（%1 张），仅供参考；这只是设备与场景分布的客观统计，不等于读懂了你的审美。").arg(n)
        : QCoreApplication::translate("LightAlbum", "基于 %1 张作品 · 设备/场景分布，非审美判定 · 零云端").arg(n);
    radar_->setData(vals, labels, n, note);
    radarNote_->setText(note);
    radarCount_->setText(QCoreApplication::translate("LightAlbum", "样本 %1 张 · 仅本地可测维度").arg(n));
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

// 2.33.0：分析输入唯一收集口（onAnalyze / autoRun 共用）。
// ★铁律 21：隐私过滤在**注入点**做 —— 锁定时私密照根本不进 CullEngine，
//   因此报告、KPI、雷达、对决组员全都不含私密内容，无须各处再各判一次。
QVector<CullInput> CullPage::collectInputs() const
{
    QVector<CullInput> inputs;
    if (!photos_) return inputs;
    inputs.reserve(photos_->size());
    for (const Photo& ph : *photos_) {
        if (hidePrivate_ && ph.priv) continue;   // P16 隐私门
        if (ph.thumbFile.isEmpty()) continue;
        CullInput in;
        in.thumbPath = QDir(thumbsDir_).filePath(ph.thumbFile);
        parseDim(ph.dim, in.origW, in.origH);
        // 2.29.0：isRaw 走唯一真源；isVideo 由 mediaType 驱动（不靠扩展名二次判定）
        in.isRaw   = RawDecode::isRawExt(ph.path);
        in.isVideo = (ph.mediaType == QLatin1String("video"));
        in.durSec  = ph.durSec;     // 2.31.0 视频时长透传
        in.category = ph.category;
        in.priv     = ph.priv;      // 2.33.0 透传：解锁态跑的报告，重新上锁后要能识别
        inputs.append(in);
    }
    return inputs;
}

void CullPage::setHidePrivate(bool hide)
{
    hidePrivate_ = hide;
    model_->setHidePrivate(hide);       // 锁定区 / 胶片条（markedThumbs）
    proxy_->setHidePrivate(hide);       // 结果网格
    updateRadar();                      // 样本数与分布随可见集合重算
}

void CullPage::onAnalyze()
{
    if (!photos_) return;
    const QVector<CullInput> inputs = collectInputs();
    if (inputs.isEmpty()) {
        progress_->setText(QCoreApplication::translate("LightAlbum", "无可用缩略图"));
        return;
    }
    runBtn_->setEnabled(false);
    progress_->setText(QCoreApplication::translate("LightAlbum", "分析中 0/%1").arg(inputs.size()));

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
    QVector<CullInput> inputs = collectInputs();
    if (inputs.isEmpty()) { progress_->setText(QCoreApplication::translate("LightAlbum", "无可用缩略图")); return; }
    if (cap > 0 && inputs.size() > cap) inputs = inputs.mid(0, cap);
    CullOptions opts;
    opts.topN = topNSpin_->value();
    const CullReport rep = CullEngine::analyze(inputs, opts);
    onFinished(rep);
}

void CullPage::onProgress(int cur, int total, const QString&)
{
    progress_->setText(QCoreApplication::translate("LightAlbum", "分析中 %1/%2").arg(cur).arg(total));
}

void CullPage::onFinished(const CullReport& rep)
{
    runBtn_->setEnabled(true);
    setKpi(0, QString::number(rep.total), QCoreApplication::translate("LightAlbum", "已分析"));
    setKpi(1, QString::number(rep.candidateCount), QCoreApplication::translate("LightAlbum", "候选"));
    setKpi(2, QString::number(rep.rejectedCount), QCoreApplication::translate("LightAlbum", "废片"));
    setKpi(3, QString::number(rep.dupGroups.size()), QCoreApplication::translate("LightAlbum", "去重组"));
    setKpi(4, QString::number(rep.blurryCount), QCoreApplication::translate("LightAlbum", "模糊"));
    setKpi(5, QString::number(rep.exposureCount), QCoreApplication::translate("LightAlbum", "曝光"));
    setKpi(6, QString::number(rep.unknownCount), QCoreApplication::translate("LightAlbum", "解码失败"));

    model_->setItems(rep.topItems, thumbsDir_);
    catCounts_ = model_->categoryCounts();
    buildChips(catCounts_);
    // 复位筛选
    activeCat_.clear();
    proxy_->setCategoryFilter(QString());
    proxy_->setVideoOnly(false);
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
        // 2.7.0：原为裸 QApplication::quit() —— 构造期调用时事件循环尚未
        // 进入，quit() 是 no-op，导致本门永久挂死（[KILL]，曾被误判为环境
        // 噪声）。收敛到 QaExit 契约：无条件排队，构造期/事件循环内都真退。
        QaExit::requestExit(0);
    } else {
        progress_->setText(QCoreApplication::translate("LightAlbum", "完成 · 候选 %1 / 废片 %2 / 去重 %3 组")
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
        // 2.33.0 隐私门：解锁时跑出的报告，重新上锁后对决组员必须剔掉私密项
        //   （否则私密缩略图绕过锁定继续显示在 PK 面板上）
        if (hidePrivate_) {
            for (int i = g.members.size() - 1; i >= 0; --i)
                if (g.members[i].priv) g.members.removeAt(i);
        }
        if (g.members.size() < 2) continue;   // 单员不成对决
        for (int i = 0; i < g.members.size(); ++i) g.alive.append(i);
        duelGroups_.append(g);
    }
    duelBtn_->setEnabled(!duelGroups_.isEmpty());
    if (!duelGroups_.isEmpty())
        duelBtn_->setText(QCoreApplication::translate("LightAlbum", "开始对决（%1 组）").arg(duelGroups_.size()));
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

// 反例钩子（铁律35）：LA_CULL_DUEL_QA_FORCE_BROKEN=1 让对决裁决根本不落标 —— 这正
//   是「选了胜者、忘了标败者」的真实缺陷形状（败者会留在候选里被继续带走）。
//   它只破坏数据通路，不碰 runDuelQa 里的判定式（铁律22）。
static bool duelMarkBrokenForQa()
{
    static const bool broken = !qEnvironmentVariableIsEmpty("LA_CULL_DUEL_QA_FORCE_BROKEN");
    return broken;
}

// 对决标记落库：被拒相似项不在模型 → 先升格入模再标记（总览/胶片条/雷达全联动）
void CullPage::markDuelItem(const CullItem& it, const QString& mark)
{
    if (duelMarkBrokenForQa()) return;
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
        duelHead_->setText(QCoreApplication::translate("LightAlbum", "对决完成 · 共 %1 组 · 点「返回总览」查看战果")
                           .arg(duelGroups_.size()));
        return;
    }
    const DuelGroup& g = duelGroups_[duelIdx_];
    const CullItem& L = g.members[g.alive[0]];
    const CullItem& R = g.members[g.alive[1]];
    duelHead_->setText(QCoreApplication::translate("LightAlbum", "第 %1 / %2 组 · 本组已决 %3 · 在场 %4")
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
    long gold = 0;                            // 金（Lumen::GOLD）：引擎倾向金框/徽章/分条
    for (int y = 0; y < img.height(); y += 2) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb px = row[x];
            if (qRed(px) > 170 && qGreen(px) > 120 && qGreen(px) < 205 && qBlue(px) < 130)
                ++gold;
        }
    }
    // ★判定必须存在：此前只打印数字并 requestExit(0)，而 runner 的 greenPat
    //   含 "OK"（子串、大小写不敏感），markOk=0 里的小写 ok 也算绿 → 门永远不可能
    //   红（铁律34：不可失败的断言即缺陷）。
    //   阈值口径与其它离屏像素门一致：对决面板必须真画出金框（gold>0）、仲裁必须
    //   真落一次 skip 标记、组队素材必须够两人对决、png 必须写成功。
    const bool pass = groups > 0 && members0 >= 2 && markOk && gold > 0 && saveOk;
    std::fprintf(stderr,
        "LA_CULL_DUEL_QA %s groups=%d members0=%d skipDelta=%d markOk=%d gold=%ld\n",
        pass ? "PASS" : "FAIL", groups, members0, skipAfter - skipBefore,
        int(markOk), gold);
    if (!pass)
        std::fprintf(stderr, "LA_CULL_DUEL_QA FAIL png=%s\n", qPrintable(png));
    std::fflush(stderr);
    QaExit::requestExit(0);   // 2.7.0：收敛到自退契约（红/绿由 FAIL 令牌判定）
}

#include "CullPage.moc"


#include "GalleryPage.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QStyledItemDelegate>

class GalleryCardDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return {300, 336};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& ix) const override
    {
        const QString title  = ix.data(GalleryModel::TitleRole).toString();
        const QString sub    = ix.data(GalleryModel::SubRole).toString();
        const QString orient = ix.data(GalleryModel::OrientRole).toString();
        const bool    miss   = ix.data(GalleryModel::MissingRole).toBool();
        const QPixmap pm = qobject_cast<const GalleryModel*>(ix.model())
                               ? static_cast<const GalleryModel*>(ix.model())->pixmapAt(ix.row())
                               : QPixmap();

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        // 卡片底（P12：圆角加大 + hover 翡翠描边 + 选中金边）
        QRectF card(opt.rect.adjusted(8, 8, -8, -8));
        const bool hov  = (opt.state & QStyle::State_MouseOver) != 0;
        const bool sel  = (opt.state & QStyle::State_Selected) != 0;
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x13, 0x1a, 0x15));
        p->drawRoundedRect(card, 12, 12);
        p->setBrush(Qt::NoBrush);
        if (sel) {
            QPen pen(QColor(0xd9, 0xa9, 0x4e), 2.2);
            p->setPen(pen);
            p->drawRoundedRect(card.adjusted(1, 1, -1, -1), 12, 12);
        } else if (hov) {
            QPen pen(QColor(0x4f, 0xc0, 0x8a, 170), 1.4);
            p->setPen(pen);
            p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        } else {
            QPen pen(QColor(0x26, 0x31, 0x2a), 1);
            p->setPen(pen);
            p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        }

        // 图区：等比居中；缺失/未加载显示占位
        QRectF img = card.adjusted(10, 10, -10, -46);
        if (!pm.isNull()) {
            QPixmap scaled = pm.scaled(int(img.width()), int(img.height()),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QRectF r(QPointF(img.center().x() - scaled.width() / 2.0,
                             img.center().y() - scaled.height() / 2.0),
                     scaled.size());
            p->drawPixmap(r.toRect(), scaled);
        } else if (miss) {
            p->setPen(QColor(0x8d, 0xa3, 0x96));
            p->drawText(img, Qt::AlignCenter, QStringLiteral("图片缺失"));
        } else {
            p->setPen(QColor(0x5a, 0x6b, 0x60));
            p->drawText(img, Qt::AlignCenter, QStringLiteral("…"));
        }

        // 横竖徽章（右上角小胶囊，金描边）
        if (orient == QLatin1String("landscape") || orient == QLatin1String("portrait")) {
            const QString b = orient == QLatin1String("landscape")
                                  ? QStringLiteral("横") : QStringLiteral("竖");
            QFont f = p->font(); f.setPointSizeF(8.5); f.setBold(true); p->setFont(f);
            QRectF br = card.adjusted(card.width() - 38, 14, -10, -card.height() + 34);
            p->setPen(QColor(0xd9, 0xa9, 0x4e));
            p->setBrush(QColor(0x0c, 0x10, 0x0d));
            p->drawRoundedRect(br, 7, 7);
            p->drawText(br, Qt::AlignCenter, b);
        }

        // 评级星（右上角下侧小圆点：金实心=已评，空圈=未达；rating=0 不画，数据诚实）
        const int rating = ix.data(GalleryModel::RatingRole).toInt();
        if (rating > 0) {
            const QPointF c0(card.right() - 14 - 4 * 9.0, card.bottom() - 44);
            for (int i = 0; i < 5; ++i) {
                const QPointF c(c0.x() + i * 9.0, c0.y());
                if (i < rating) { p->setPen(Qt::NoPen); p->setBrush(QColor(0xd9, 0xa9, 0x4e)); }
                else { p->setPen(QPen(QColor(0x5a, 0x6b, 0x60), 1)); p->setBrush(Qt::NoBrush); }
                p->drawEllipse(c, 2.6, 2.6);
            }
        }

        // 标题 + 副行
        p->setPen(QColor(0xf4, 0xf7, 0xf4));
        QFont f = p->font(); f.setPointSizeF(9.5); f.setBold(false); p->setFont(f);
        const QRectF tr(card.adjusted(10, card.height() - 40, -10, card.height() - 22));
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(title, Qt::ElideRight, int(tr.width())));
        if (!sub.isEmpty()) {
            p->setPen(QColor(0x8d, 0xa3, 0x96));
            QFont f2 = p->font(); f2.setPointSizeF(8.5); p->setFont(f2);
            const QRectF sr(card.adjusted(10, card.height() - 22, -10, -8));
            p->drawText(sr, Qt::AlignVCenter | Qt::AlignLeft,
                        p->fontMetrics().elidedText(sub, Qt::ElideRight, int(sr.width())));
        }
        p->restore();
    }
};

GalleryPage::GalleryPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), cache_(thumbsDir, this)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    count_ = new QLabel(this);
    count_->setStyleSheet(QString::fromUtf8("color:#8da396;padding:12px 20px 8px;"));
    lay->addWidget(count_);

    view_ = new QListView(this);
    view_->setViewMode(QListView::IconMode);
    view_->setResizeMode(QListView::Adjust);
    view_->setMovement(QListView::Static);
    view_->setUniformItemSizes(true);
    view_->setWrapping(true);
    view_->setFlow(QListView::LeftToRight);
    view_->setGridSize(QSize(308, 344));
    view_->setSpacing(0);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setMouseTracking(true);   // P12：委托 hover 翡翠描边需要鼠标追踪
    view_->setFrameShape(QFrame::NoFrame);
    view_->setStyleSheet(Lumen::scrollbar() +
        QStringLiteral("QListView{background:transparent;}"
                       "QListView::item:selected{background:transparent;}"));
    model_.setSource(photos);
    model_.setThumbCache(&cache_);
    view_->setModel(&model_);
    view_->setItemDelegate(new GalleryCardDelegate(view_));
    lay->addWidget(view_, 1);

    // 缩略图预取节流：滚动停止 120ms 后，对可见区间前后各扩 6/24 行触发后台加载
    prefetchTimer_ = new QTimer(this);
    prefetchTimer_->setSingleShot(true);
    prefetchTimer_->setInterval(120);
    connect(view_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            prefetchTimer_, qOverload<>(&QTimer::start));
    connect(prefetchTimer_, &QTimer::timeout, this, &GalleryPage::prefetch);

    applyFilter(QString());
}

void GalleryPage::applyFilter(const QString& q)
{
    model_.setFilter(q);
    const int n = model_.rowCount();
    const int total = photos_ ? photos_->size() : n;
    count_->setText(n == total
        ? QStringLiteral("显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QStringLiteral("显示 <b>%1</b> / %2 幅（已筛）").arg(n).arg(total));
}

void GalleryPage::prefetch()
{
    const QRect vp = view_->viewport()->rect();
    const QModelIndex first = view_->indexAt(vp.topLeft());
    if (!first.isValid()) return;
    const QModelIndex last = view_->indexAt(QPoint(vp.center().x(), vp.bottom()));
    const int a = qMax(0, first.row() - 6);
    const int b = qMin(model_.rowCount() - 1,
                       (last.isValid() ? last.row() : first.row() + 30) + 24);
    for (int r = a; r <= b; ++r)
        model_.pixmapAt(r);          // request 幂等：已缓存/在途直接返回
}

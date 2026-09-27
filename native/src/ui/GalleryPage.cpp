#include "GalleryPage.h"
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

        // 卡片底（与 web 版 .g-item：圆角 10、line 描边、选中金边）
        QRectF card(opt.rect.adjusted(8, 8, -8, -8));
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x18, 0x1d, 0x1a));
        p->drawRoundedRect(card, 10, 10);
        if (opt.state & QStyle::State_Selected) {
            QPen pen(QColor(0xc1, 0x9a, 0x4b), 1.6);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(card.adjusted(1, 1, -1, -1), 10, 10);
        } else {
            QPen pen(QColor(0x2a, 0x2f, 0x2c), 1);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
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
            p->setPen(QColor(0x9f, 0xa8, 0xa3));
            p->drawText(img, Qt::AlignCenter, QStringLiteral("图片缺失"));
        } else {
            p->setPen(QColor(0x4a, 0x55, 0x4f));
            p->drawText(img, Qt::AlignCenter, QStringLiteral("…"));
        }

        // 横竖徽章（右上角小胶囊，金描边）
        if (orient == QLatin1String("landscape") || orient == QLatin1String("portrait")) {
            const QString b = orient == QLatin1String("landscape")
                                  ? QStringLiteral("横") : QStringLiteral("竖");
            QFont f = p->font(); f.setPointSizeF(8.5); f.setBold(true); p->setFont(f);
            QRectF br = card.adjusted(card.width() - 38, 14, -10, -card.height() + 34);
            p->setPen(QColor(0xc1, 0x9a, 0x4b));
            p->setBrush(QColor(0x0b, 0x0e, 0x0c));
            p->drawRoundedRect(br, 7, 7);
            p->drawText(br, Qt::AlignCenter, b);
        }

        // 评级星（右上角下侧小圆点：金实心=已评，空圈=未达；rating=0 不画，数据诚实）
        const int rating = ix.data(GalleryModel::RatingRole).toInt();
        if (rating > 0) {
            const QPointF c0(card.right() - 14 - 4 * 9.0, card.bottom() - 44);
            for (int i = 0; i < 5; ++i) {
                const QPointF c(c0.x() + i * 9.0, c0.y());
                if (i < rating) { p->setPen(Qt::NoPen); p->setBrush(QColor(0xc1, 0x9a, 0x4b)); }
                else { p->setPen(QPen(QColor(0x4a, 0x55, 0x4f), 1)); p->setBrush(Qt::NoBrush); }
                p->drawEllipse(c, 2.6, 2.6);
            }
        }

        // 标题 + 副行
        p->setPen(QColor(0xf2, 0xf5, 0xf3));
        QFont f = p->font(); f.setPointSizeF(9.5); f.setBold(false); p->setFont(f);
        const QRectF tr(card.adjusted(10, card.height() - 40, -10, card.height() - 22));
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(title, Qt::ElideRight, int(tr.width())));
        if (!sub.isEmpty()) {
            p->setPen(QColor(0x9f, 0xa8, 0xa3));
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
    count_->setStyleSheet(QStringLiteral("color:#9fa8a3;padding:10px 18px 6px;"));
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
    view_->setFrameShape(QFrame::NoFrame);
    view_->setStyleSheet(QStringLiteral(
        "QListView{background:transparent;}"
        "QListView::item:selected{background:transparent;}"
        "QScrollBar:vertical{background:#0b0e0c;width:10px;margin:2px;}"
        "QScrollBar::handle:vertical{background:#2a2f2c;border-radius:5px;min-height:40px;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}"));
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

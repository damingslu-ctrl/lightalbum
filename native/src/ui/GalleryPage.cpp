#include "GalleryPage.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QPainter>
#include <QStyledItemDelegate>

// P16 分类固定次序（与 classifier_server.py LABELS 单一事实源一致；空分类不展示）
static QStringList catOrder()
{
    return {QStringLiteral("人像"), QStringLiteral("风光"), QStringLiteral("夜景·星空"),
            QStringLiteral("城市·建筑"), QStringLiteral("微距·花卉"), QStringLiteral("动物"),
            QStringLiteral("美食"), QStringLiteral("黑白")};
}

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

    // P16 分类 chips 条（buildChips 按数据填充；无分类数据时整条隐藏）
    chipsBar_ = new QWidget(this);
    chipsBar_->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* chipLay = new QHBoxLayout(chipsBar_);
    chipLay->setContentsMargins(20, 10, 20, 0);
    chipLay->setSpacing(8);
    lay->addWidget(chipsBar_);

    // P16 人物 chips 条（person 列有数据才显示）
    personBar_ = new QWidget(this);
    personBar_->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* personLay = new QHBoxLayout(personBar_);
    personLay->setContentsMargins(20, 8, 20, 0);
    personLay->setSpacing(8);
    lay->addWidget(personBar_);

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

    // P15：点击卡片 → 弹出灯箱并跳转修图（零云端；path 由 MainWindow 定位 Photo）。
    connect(view_, &QListView::clicked, this, [this](const QModelIndex& ix) {
        if (!ix.isValid()) return;
        const Photo* p = model_.photoAt(ix.row());
        if (p) emit photoActivated(p->path);
    });

    applyFilter(QString());
    buildChips();
}

// ---- P16 chips：按数据 + 隐私门状态重建（空分类不展示，全空隐藏整条）----
void GalleryPage::buildChips()
{
    // 清空旧 chips
    for (QPushButton* b : catChips_) b->deleteLater();
    catChips_.clear();
    for (QPushButton* b : personChips_) b->deleteLater();
    personChips_.clear();

    const QString chipCss = QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:999px;"
        "padding:4px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#f4f7f4;}"
        "QPushButton:checked{background:#d9a94e;border-color:#d9a94e;"
        "color:#0c100d;font-weight:bold;}");

    // 计数（与列表同口径：锁定时跳过私密）
    QHash<QString, int> catCnt;
    QSet<QString> persons;
    int nVisible = 0;
    for (const Photo& p : *photos_) {
        if (hidePrivate_ && p.priv) continue;
        ++nVisible;
        if (!p.category.isEmpty()) catCnt[p.category] += 1;
        for (const QString& n : p.person.split(',', Qt::SkipEmptyParts))
            persons.insert(n.trimmed());
    }

    auto* chipLay = static_cast<QHBoxLayout*>(chipsBar_->layout());
    auto addChip = [this, chipLay, chipCss](QVector<QPushButton*>& dst,
                                            const QString& tag, const QString& label) {
        auto* b = new QPushButton(label, chipsBar_);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(chipCss);
        b->setProperty("catTag", tag);
        connect(b, &QPushButton::clicked, this, [this, b] {
            const QString tag = b->property("catTag").toString();
            activeCat_ = (activeCat_ == tag) ? QString() : tag;
            model_.setCategoryFilter(activeCat_);
            restyleChips();
            updateCount();
        });
        chipLay->addWidget(b);
        dst.append(b);
    };

    // 全部
    {
        auto* b = new QPushButton(QStringLiteral("全部 %1").arg(nVisible), chipsBar_);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(chipCss);
        b->setProperty("catTag", QString());
        connect(b, &QPushButton::clicked, this, [this] {
            activeCat_.clear();
            model_.setCategoryFilter(QString());
            restyleChips();
            updateCount();
        });
        chipLay->addWidget(b);
        catChips_.append(b);
    }
    // 固定次序 8 类（空分类不展示）
    for (const QString& cat : catOrder()) {
        const int c = catCnt.value(cat, 0);
        if (c > 0) addChip(catChips_, cat,
                           QStringLiteral("%1 %2").arg(cat).arg(c));
    }
    // 其他（低置信度）与 未分类
    {
        const int c = catCnt.value(QStringLiteral("其他"), 0);
        if (c > 0) addChip(catChips_, QStringLiteral("其他"),
                           QStringLiteral("其他 %1").arg(c));
    }
    {
        int unc = 0;
        for (const Photo& p : *photos_)
            if (!(hidePrivate_ && p.priv) && p.category.isEmpty()) ++unc;
        if (unc > 0) addChip(catChips_, GalleryModel::kNoCategory,
                             QStringLiteral("未分类 %1").arg(unc));
    }
    chipLay->addStretch(1);
    chipsBar_->setVisible(catChips_.size() > 1);   // 只有「全部」= 无分类数据
    restyleChips();

    // 人物 chips（有数据才显示整条）
    auto* personLay = static_cast<QHBoxLayout*>(personBar_->layout());
    if (!persons.isEmpty()) {
        auto* allBtn = new QPushButton(QStringLiteral("全部人物"), personBar_);
        allBtn->setCheckable(true);
        allBtn->setCursor(Qt::PointingHandCursor);
        allBtn->setStyleSheet(chipCss);
        allBtn->setProperty("personTag", QString());
        connect(allBtn, &QPushButton::clicked, this, [this] {
            activePerson_.clear();
            model_.setPersonFilter(QString());
            restyleChips();
            updateCount();
        });
        personLay->addWidget(allBtn);
        personChips_.append(allBtn);
        QStringList sorted = QStringList(persons.cbegin(), persons.cend());
        std::sort(sorted.begin(), sorted.end());
        for (const QString& n : sorted) {
            auto* b = new QPushButton(n, personBar_);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(chipCss);
            b->setProperty("personTag", n);
            connect(b, &QPushButton::clicked, this, [this, b] {
                const QString n = b->property("personTag").toString();
                activePerson_ = (activePerson_ == n) ? QString() : n;
                model_.setPersonFilter(activePerson_);
                restyleChips();
                updateCount();
            });
            personLay->addWidget(b);
            personChips_.append(b);
        }
        personLay->addStretch(1);
        personBar_->setVisible(true);
    } else {
        personBar_->setVisible(false);
    }
    restyleChips();
}

void GalleryPage::restyleChips()
{
    for (QPushButton* b : catChips_) {
        const QString tag = b->property("catTag").toString();
        b->setChecked(activeCat_ == tag);
    }
    for (QPushButton* b : personChips_) {
        const QString tag = b->property("personTag").toString();
        b->setChecked(activePerson_ == tag);
    }
}

void GalleryPage::applyCategoryFilter(const QString& cat)
{
    activeCat_ = cat;
    model_.setCategoryFilter(cat);
    restyleChips();
    updateCount();
}

void GalleryPage::setHidePrivate(bool hide)
{
    hidePrivate_ = hide;
    model_.setHidePrivate(hide);
    buildChips();      // 计数随隐私门变化
    updateCount();
}

void GalleryPage::refresh()
{
    model_.rebuild();
    buildChips();
    updateCount();
}

void GalleryPage::updateCount()
{
    const int n = model_.rowCount();
    const int total = photos_ ? photos_->size() : n;
    QStringList notes;
    if (!searchText_.trimmed().isEmpty()) notes << QStringLiteral("已筛");
    if (!activeCat_.isEmpty())
        notes << (activeCat_ == GalleryModel::kNoCategory
                      ? QStringLiteral("未分类")
                      : QStringLiteral("分类：%1").arg(activeCat_));
    if (!activePerson_.isEmpty()) notes << QStringLiteral("人物：%1").arg(activePerson_);
    const QString suffix = notes.isEmpty()
        ? QString() : QStringLiteral("（%1）").arg(notes.join(QStringLiteral(" · ")));
    count_->setText(n == total && notes.isEmpty()
        ? QStringLiteral("显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QStringLiteral("显示 <b>%1</b> / %2 幅%3").arg(n).arg(total).arg(suffix));
}

void GalleryPage::applyFilter(const QString& q)
{
    searchText_ = q;
    model_.setFilter(q);
    updateCount();
}

// P15：地图区域/省点击 → 图库按大区/省筛选（pid 优先，否则大区）。
void GalleryPage::applyGeoFilter(const QString& regionId, const QString& provEn)
{
    model_.setRegionFilter(regionId);
    model_.setProvFilter(provEn);
    const int n = model_.rowCount();
    const int total = photos_ ? photos_->size() : n;
    count_->setText((regionId.isEmpty() && provEn.isEmpty())
        ? QStringLiteral("显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QStringLiteral("显示 <b>%1</b> / %2 幅（区域筛选）").arg(n).arg(total));
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

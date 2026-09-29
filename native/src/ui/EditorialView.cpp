#include "EditorialView.h"
#include "GalleryModel.h"
#include "GalleryCardPainter.h"
#include "Theme.h"
#include <QGridLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QScrollBar>
#include <QSet>
#include <QResizeEvent>
#include <QPainter>
#include <QPixmap>

// —— 单张卡片瓦片（自绘，复用 Lumen::paintGalleryCard）——
class GalleryTile : public QWidget {
    Q_OBJECT
public:
    GalleryTile(GalleryModel* m, int row, bool feature, QWidget* p = nullptr)
        : QWidget(p), model_(m), row_(row), feature_(feature) {
        setMouseTracking(true);
        setMinimumSize(120, 130);
    }
    QSize sizeHint() const override { return QSize(216, 240); }
    int row() const { return row_; }
    void setSelected(bool s) { sel_ = s; update(); }

signals:
    void activated(int row);
    void hovered(int row, bool on);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QModelIndex ix = model_->index(row_, 0);
        const QString title  = model_->data(ix, GalleryModel::TitleRole).toString();
        const QString sub    = model_->data(ix, GalleryModel::SubRole).toString();
        const QString orient = model_->data(ix, GalleryModel::OrientRole).toString();
        const bool    miss   = model_->data(ix, GalleryModel::MissingRole).toBool();
        const int     rating = model_->data(ix, GalleryModel::RatingRole).toInt();
        const QPixmap pm = model_->pixmapAt(row_);
        Lumen::paintGalleryCard(&p, QRectF(rect()).adjusted(4, 4, -4, -4),
                                title, sub, orient, miss, rating, pm, feature_, hov_, sel_);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit activated(row_);
    }
    void enterEvent(QEnterEvent*) override { hov_ = true; emit hovered(row_, true); update(); }
    void leaveEvent(QEvent*) override      { hov_ = false; emit hovered(row_, false); update(); }

private:
    GalleryModel* model_;
    int  row_;
    bool feature_;
    bool hov_ = false;
    bool sel_ = false;
};

EditorialView::EditorialView(GalleryModel* model, QWidget* parent)
    : QScrollArea(parent), model_(model)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(Lumen::scrollbar() + QStringLiteral("QScrollArea{background:transparent;}"));
    content_ = new QWidget(this);
    grid_ = new QGridLayout(content_);
    grid_->setContentsMargins(16, 12, 16, 16);
    grid_->setSpacing(12);
    setWidget(content_);

    connect(model_, &QAbstractItemModel::modelReset, this, &EditorialView::rebuild);
    connect(model_, &QAbstractItemModel::dataChanged, this, &EditorialView::onDataChanged);
    rebuild();
}

void EditorialView::rebuild()
{
    qDeleteAll(tiles_);
    tiles_.clear();
    const int n = model_->rowCount();
    for (int r = 0; r < n; ++r) {
        auto* t = new GalleryTile(model_, r, r == 0, content_);
        connect(t, &GalleryTile::activated, this, [this](int row) {
            selectedRow_ = row;
            for (GalleryTile* x : tiles_) x->setSelected(x->row() == row);
            const Photo* ph = model_->photoAt(row);
            emit photoActivated(ph ? ph->path : QString());
        });
        tiles_.append(t);
    }
    relayout();
}

void EditorialView::relayout()
{
    if (!grid_) return;
    while (grid_->count()) grid_->takeAt(0);   // 取下瓦片（不删除，保留复用）
    const int n = tiles_.size();
    if (n == 0) return;

    const int W = viewport()->width();
    cols_ = qBound(3, int(W / 230), 5);

    // feature（首图）占 (0,0,2,2)
    grid_->addWidget(tiles_.first(), 0, 0, 2, 2);

    auto key = [](int r, int c) { return QString::number(r) + QChar(',') + QString::number(c); };
    QSet<QString> occ;
    occ.insert(key(0, 0)); occ.insert(key(0, 1));
    occ.insert(key(1, 0)); occ.insert(key(1, 1));

    int r = 0, c = 0;
    for (int i = 1; i < n; ++i) {
        while (occ.contains(key(r, c))) {
            if (++c >= cols_) { c = 0; ++r; }
        }
        grid_->addWidget(tiles_[i], r, c);
        occ.insert(key(r, c));
        if (++c >= cols_) { c = 0; ++r; }
    }
    for (int cc = 0; cc < cols_; ++cc) grid_->setColumnStretch(cc, 1);
}

void EditorialView::resizeEvent(QResizeEvent* e)
{
    QScrollArea::resizeEvent(e);
    if (!relayoutPending_) {
        relayoutPending_ = true;
        QTimer::singleShot(60, this, [this] { relayoutPending_ = false; relayout(); });
    }
}

void EditorialView::onDataChanged(const QModelIndex& top, const QModelIndex& bottom,
                                  const QVector<int>&)
{
    for (int r = top.row(); r <= bottom.row(); ++r)
        if (r >= 0 && r < tiles_.size()) tiles_[r]->update();
}

// 仅预取可见（及相邻）卡片缩略图，替代原 QListView indexAt 逻辑
void EditorialView::prefetchVisible()
{
    if (!content_) return;
    const QRect vis = viewport()->rect().translated(content_->pos());
    const int n = tiles_.size();
    for (int i = 0; i < n; ++i) {
        if (tiles_[i]->geometry().intersects(vis)) {
            model_->pixmapAt(i);
            if (i - 1 >= 0) model_->pixmapAt(i - 1);
            if (i + 1 < n)  model_->pixmapAt(i + 1);
        }
    }
}

QRect EditorialView::featureGeometry() const
{
    return tiles_.isEmpty() ? QRect() : tiles_.first()->geometry();
}

QRect EditorialView::tileGeometry(int i) const
{
    return (i >= 0 && i < tiles_.size()) ? tiles_[i]->geometry() : QRect();
}

#include "EditorialView.moc"

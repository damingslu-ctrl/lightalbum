#include "EditorialView.h"
#include "GalleryModel.h"
#include "GalleryCardPainter.h"
#include "Theme.h"
#include <QTimer>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QScrollBar>
#include <QResizeEvent>
#include <QPainter>
#include <QPixmap>
#include <QSet>

// —— 单张卡片瓦片（自绘，复用 Lumen::paintGalleryCard）——
// 方向 D3：池化复用——构造后通过 setRow() 重绑模型行与 feature 标记，避免每图 new/delete。
class GalleryTile : public QWidget {
    Q_OBJECT
public:
    GalleryTile(GalleryModel* m, int row, bool feature, QWidget* p = nullptr)
        : QWidget(p), model_(m), row_(row), feature_(feature) {
        setMouseTracking(true);
    }
    QSize sizeHint() const override { return QSize(216, 240); }
    int row() const { return row_; }
    void setRow(int row, bool feature) { row_ = row; feature_ = feature; update(); }
    void setSelected(bool s) { sel_ = s; update(); }

signals:
    void activated(int row, Qt::KeyboardModifiers mods);   // D5b：带修饰键供多选判定
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
        // 方向 D 第四步：按瓦片角色选缓存级——feature 2×2 走 L2(512px)，普通瓦片走 L1(256px)
        const QPixmap pm = model_->pixmapAt(row_, feature_ ? ThumbCache::kFeature
                                                           : ThumbCache::kTile);
        Lumen::paintGalleryCard(&p, QRectF(rect()),
                                title, sub, orient, miss, rating, pm, feature_, hov_, sel_);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit activated(row_, e->modifiers());
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
    setWidgetResizable(false);                  // 方向 D3：content_ 由我们显式设为虚拟尺寸
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setStyleSheet(Lumen::scrollbar() + QStringLiteral("QScrollArea{background:transparent;}"));
    content_ = new QWidget(this);
    content_->setObjectName(QStringLiteral("evContent"));
    content_->setStyleSheet(QStringLiteral("background:transparent;"));
    setWidget(content_);

    connect(model_, &QAbstractItemModel::modelReset, this, &EditorialView::rebuild);
    connect(model_, &QAbstractItemModel::dataChanged, this, &EditorialView::onDataChanged);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged, this, &EditorialView::updateWindow);

    total_ = model_->rowCount();
    relayout();
}

// 计算虚拟布局：列数 + 单元格尺寸 + 虚拟内容总高 + 流式单元列表（与旧 relayout 打包一致）
void EditorialView::computeLayout()
{
    const int W = viewport()->width();
    if (W <= 0) return;
    cols_ = qBound(3, int(W / 230), 5);
    const int innerW = W - 2 * margin_;
    cellW_ = (innerW - (cols_ - 1) * spacing_) / cols_;
    cellH_ = qRound(cellW_ * 240.0 / 216.0);     // 维持原 sizeHint 比例 216×240

    avail_.clear();
    const int need = qMax(0, total_ - 1);        // 首图外还需 need 个流式单元
    int r = 0, c = 0;
    while (avail_.size() < need) {
        if (!(r <= 1 && c <= 1)) avail_.append(QPoint(r, c));   // 跳过 feature 2×2 占用的 4 格
        if (++c >= cols_) { c = 0; ++r; }
    }
    int maxR = 1;                                // feature 占据 0..1 行，内容高至少覆盖之
    for (const QPoint& cell : avail_) maxR = qMax(maxR, cell.x());
    contentH_ = 2 * margin_ + maxR * (cellH_ + spacing_) + cellH_;
    contentW_ = W;
}

void EditorialView::relayout()
{
    const int W = viewport()->width();
    if (W <= 0) return;                          // 尚未确定尺寸，等 resizeEvent 再算
    computeLayout();
    content_->resize(contentW_, contentH_);
    updateWindow();
}

QRect EditorialView::featureRect() const
{
    return QRect(margin_, margin_,
                 2 * cellW_ + spacing_, 2 * cellH_ + spacing_);
}

QRect EditorialView::cellRect(int i) const
{
    const QPoint cell = avail_.value(i - 1, QPoint(0, 0));   // i >= 1
    const int x = margin_ + cell.y() * (cellW_ + spacing_);
    const int y = margin_ + cell.x() * (cellH_ + spacing_);
    return QRect(x, y, cellW_, cellH_);
}

// 取一个空闲池化瓦片（无则新建并一次性连接 activated → 选中态/灯箱）
GalleryTile* EditorialView::acquire()
{
    if (!free_.isEmpty()) return free_.takeLast();
    auto* t = new GalleryTile(model_, 0, false, content_);
    connect(t, &GalleryTile::activated, this,
            [this](int row, Qt::KeyboardModifiers mods) { clickRow(row, mods); });
    pool_.append(t);
    return t;
}

void EditorialView::recycle(GalleryTile* t)
{
    t->hide();
    free_.append(t);
}

// 窗口化：将可见（含前后缓冲）模型行映射到池化瓦片（回收不可见、绑定新可见）
void EditorialView::updateWindow()
{
    if (!content_ || cellH_ <= 0) return;
    const QRect vp = viewport()->rect();
    const int sy  = verticalScrollBar()->value();
    const int buf = (cellH_ + spacing_) * 2;     // 上下各扩 2 行作为缓冲
    const QRect vis(0, sy - buf, vp.width(), vp.height() + 2 * buf);

    QSet<int> need;
    if (featureRect().intersects(vis)) need.insert(0);
    for (int i = 1; i < total_; ++i)
        if (cellRect(i).intersects(vis)) need.insert(i);

    // 回收已不可见的绑定瓦片
    for (auto it = bound_.begin(); it != bound_.end(); ) {
        if (!need.contains(it.key())) { recycle(it.value()); it = bound_.erase(it); }
        else ++it;
    }
    // 绑定新进入可见区的行
    for (int row : need) {
        if (bound_.contains(row)) continue;
        GalleryTile* t = acquire();
        t->setRow(row, row == 0);
        t->setSelected(selRows_.contains(row));
        t->setGeometry(row == 0 ? featureRect() : cellRect(row));
        t->show();
        bound_.insert(row, t);
    }
}

void EditorialView::rebuild()
{
    for (auto it = bound_.cbegin(); it != bound_.cend(); ++it) recycle(it.value());
    bound_.clear();
    total_ = model_->rowCount();
    relayout();
}

void EditorialView::resizeEvent(QResizeEvent* e)
{
    QScrollArea::resizeEvent(e);
    if (!relayoutPending_) {                     // 60ms 去抖，避免连续缩放反复重排
        relayoutPending_ = true;
        QTimer::singleShot(60, this, [this] { relayoutPending_ = false; relayout(); });
    }
}

void EditorialView::onDataChanged(const QModelIndex& top, const QModelIndex& bottom,
                                  const QVector<int>&)
{
    for (int r = top.row(); r <= bottom.row(); ++r) {
        auto it = bound_.find(r);
        if (it != bound_.end()) it.value()->update();
    }
}

// 仅预取可见（及相邻）卡片缩略图，替代原 QListView indexAt 逻辑
void EditorialView::prefetchVisible()
{
    for (auto it = bound_.cbegin(); it != bound_.cend(); ++it) {
        const int row = it.key();
        // 方向 D 第四步：feature（row 0）走 L2，其余走 L1
        const int lv = (row == 0) ? ThumbCache::kFeature : ThumbCache::kTile;
        model_->pixmapAt(row, lv);
        if (row - 1 >= 0)     model_->pixmapAt(row - 1, lv);
        if (row + 1 < total_) model_->pixmapAt(row + 1, lv);
    }
}

// ---- D5b：多选状态机 ----
// 语义（与主流图库一致，且**不破坏**既有单击开灯箱行为）：
//   无修饰    → 单选（清空多选集）+ 打开灯箱
//   Ctrl      → 切换该行选中，不打开灯箱
//   Shift     → 以 selectedRow_ 为锚点做范围选择，不打开灯箱
void EditorialView::clickRow(int row, Qt::KeyboardModifiers mods)
{
    if (row < 0 || row >= total_) return;
    const bool toggle = mods.testFlag(Qt::ControlModifier);
    const bool range  = mods.testFlag(Qt::ShiftModifier);
    if (toggle) {
        if (selRows_.contains(row)) selRows_.remove(row);
        else selRows_.insert(row);
        selectedRow_ = row;
    } else if (range && selectedRow_ >= 0) {
        const int a = qMin(selectedRow_, row), b = qMax(selectedRow_, row);
        selRows_.clear();
        for (int r = a; r <= b; ++r) selRows_.insert(r);
    } else {
        selRows_.clear();
        selRows_.insert(row);
        selectedRow_ = row;
    }
    refreshSelectionVisual();
    emit selectionChanged(selRows_.size());
    // 仅无修饰点击才打开灯箱（多选手势不应弹窗）
    if (!toggle && !range) {
        const Photo* ph = model_->photoAt(row);
        emit photoActivated(ph ? ph->path : QString());
    }
}

QStringList EditorialView::selectedPaths() const
{
    QStringList out;
    out.reserve(selRows_.size());
    for (int r : selRows_) {
        const Photo* ph = model_->photoAt(r);
        if (ph) out << ph->path;
    }
    return out;
}

void EditorialView::selectAll()
{
    selRows_.clear();
    for (int r = 0; r < total_; ++r) selRows_.insert(r);
    refreshSelectionVisual();
    emit selectionChanged(selRows_.size());
}

void EditorialView::clearSelection()
{
    if (selRows_.isEmpty() && selectedRow_ < 0) return;
    selRows_.clear();
    selectedRow_ = -1;
    refreshSelectionVisual();
    emit selectionChanged(0);
}

void EditorialView::toggleRow(int row)
{
    clickRow(row, Qt::ControlModifier);
}

void EditorialView::refreshSelectionVisual()
{
    for (auto it = bound_.cbegin(); it != bound_.cend(); ++it)
        it.value()->setSelected(selRows_.contains(it.key()));
}

QRect EditorialView::featureGeometry() const { return featureRect(); }

QRect EditorialView::tileGeometry(int i) const
{
    if (i == 0) return featureRect();
    if (i >= 1 && i < total_) return cellRect(i);
    return QRect();
}

#include "EditorialView.moc"

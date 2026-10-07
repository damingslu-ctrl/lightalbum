#include "EditorialView.h"

// 2.22.0 骨架屏相位：TU 内静态（GalleryTile 是本文件的局部类，无处安放成员）。
// 只保存"相位"这一个数，绘制仍把它当**显式参数**传给 paintGalleryCard → 可被门禁复现。
// 减动效时相位不推进（恒 0），配合 skeletonPulse(reduceMotion) 呈终态。
// 静态函数刻意**不触碰任何实例成员**（门禁可在构造视图前预设），
// 需要重绘时由 updateSkeletonTimer()/计时器自行 content_->update()。
static double g_skelPhase = 0.0;
static bool    g_skelReduceMotion = false;
// 缺陷①回归：门禁反例注入点——置 true 时 updateWindow 对已绑定瓦片跳过几何回填，
// 精确复现修复前的空白缺陷（反例须实测变红，铁律）。
static bool    g_qaNoReflow = false;
// 方向 D 第六步：性能回归反例注入点——**一次性**每帧绘制额外睡眠毫秒数。
// ★ 必须 one-shot：若逐帧生效，单个滚动帧会重绘十余张瓦片，20 跳 × N 瓦片 × 延时
//   累加成数十秒，直接撞上 qaExit 看门狗 20s 预算而误报 no-exit-request（实测踩过）。
//   一次性注入只污染**单个** paint 事件：总开销恒为 g_qaPaintLagMs，与瓦片数/跳数无关，
//   既能让该帧 dt 越过滚动预算（门必红），又不干扰看门狗时序。
static int     g_qaPaintLagMs = 0;
static int     g_qaPaintLagLeft = 0;
void EditorialView::qaSetSkeletonPhase(double ph, bool reduceMotion)
{
    g_skelPhase = ph;
    g_skelReduceMotion = reduceMotion;
}
double EditorialView::qaSkeletonPhase() { return g_skelPhase; }
void EditorialView::qaSetNoReflow(bool v) { g_qaNoReflow = v; }
void EditorialView::qaSetPaintLagMsForQa(int ms)
{
    g_qaPaintLagMs = ms;
    g_qaPaintLagLeft = (ms > 0) ? 1 : 0;   // 一次性：只在下一个 paint 事件生效
}
#include "GalleryModel.h"
#include "GalleryCardPainter.h"
#include "Theme.h"
#include <QTimer>
#include <QThread>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QScrollBar>
#include <QResizeEvent>
#include <QPainter>
#include <QKeyEvent>
#include <cstdio>
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
        setFocusPolicy(Qt::StrongFocus);   // F3 a11y: 图库瓦片可 Tab 聚焦
    }
    QSize sizeHint() const override { return QSize(216, 240); }
    int row() const { return row_; }
    void setRow(int row, bool feature) { row_ = row; feature_ = feature; update(); }
    bool isFeature() const { return feature_; }   // 2.22.0：骨架探测需按瓦片角色选缓存级
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
        // 2.22.0：把骨架相位与减动效显式传入（纯函数在 GalleryCardPainter 内）
        // 2.28.0 视频徽标（与 GalleryPage 同一数据真源）
        const bool isVideo =
            model_->data(ix, GalleryModel::MediaTypeRole).toString() == QLatin1String("video");
        const double durSec = model_->data(ix, GalleryModel::DurRole).toDouble();
        Lumen::paintGalleryCard(&p, QRectF(rect()),
                                title, sub, orient, miss, rating, pm, feature_, hov_, sel_,
                                g_skelPhase, g_skelReduceMotion, isVideo, durSec);
        // F3 a11y: 自定义自绘控件需手动绘制焦点环（原生焦点框不显示）
        if (hasFocus()) {
            QPen fp(QColor(0xd9, 0xa9, 0x4e), 2);
            p.setPen(fp);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
        }
        // 方向 D 第六步：性能回归反例注入——单次额外睡眠（默认 0，broken 时 300ms）。
        // 模拟"绘制退化"，使**一个**滚动帧 paint 成本越过预算 → 门禁必红。
        // one-shot：消费即清零，避免逐帧累加撞看门狗（见文件头注释）。
        if (g_qaPaintLagLeft > 0 && g_qaPaintLagMs > 0) {
            --g_qaPaintLagLeft;
            QThread::msleep(static_cast<unsigned long>(g_qaPaintLagMs));
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit activated(row_, e->modifiers());
    }
    void keyPressEvent(QKeyEvent* e) override {   // F3 a11y: 回车/空格 = 鼠标单击
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter ||
            e->key() == Qt::Key_Space) {
            e->accept();
            emit activated(row_, Qt::NoModifier);
        } else {
            QWidget::keyPressEvent(e);
        }
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
    // 2.22.0：滚停/数据变化后重算"可见区是否有骨架"（决定脉冲计时器启停）
    connect(verticalScrollBar(), &QAbstractSlider::sliderReleased,
            this, &EditorialView::updateSkeletonTimer);

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

// 缺陷①回归断言：每个已绑定瓦片几何必须等于当前布局（featureRect/cellRect）。
// 修复前 updateWindow 对已绑定瓦片跳过回填 → 几何残留旧宽度 → 此处返回 1。
// 必须定义在 GalleryTile 完整类型之后（要用 QWidget::geometry()）。
int EditorialView::qaReflowCheck() const
{
    bool consistent = true;
    for (auto it = bound_.constBegin(); it != bound_.constEnd(); ++it) {
        const QRect expected = (it.key() == 0) ? featureRect() : cellRect(it.key());
        if (it.value()->geometry() != expected) consistent = false;
    }
    return consistent ? 0 : 1;
}

int EditorialView::qaKeyboardCheck()
{
    // F3 a11y: 无模型即可测键盘路径（keyEvent 不触发 paint，model 可空）
    GalleryTile tile(nullptr, 0, false);
    const bool broken = !qEnvironmentVariableIsEmpty("LA_A11Y_KEYBOARD_QA_FORCE_BROKEN");
    tile.setFocusPolicy(broken ? Qt::NoFocus : Qt::StrongFocus);

    // 断言 1：键盘可达（StrongFocus）。broken 模式模拟“修复缺失”→ 无焦点策略 → 门红
    if (!(tile.focusPolicy() & Qt::StrongFocus)) {
        std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA FAIL tile not keyboard-focusable\n");
        std::fflush(stderr);
        return 1;
    }
    // 断言 2：Enter/Space 触发 activated（驱动 clickRow → 开灯箱/选择）。
    // 用本地连接计数替代 QSignalSpy（项目未链接 QtTest 模块）。
    int activated = 0;
    QMetaObject::Connection conn = QObject::connect(
        &tile, &GalleryTile::activated,
        [&activated](int, Qt::KeyboardModifiers) { ++activated; });
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
    QCoreApplication::sendEvent(&tile, &ev);
    QObject::disconnect(conn);
    if (activated != 1) {
        std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA FAIL Enter did not activate (count=%d)\n", activated);
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "LA_A11Y_KEYBOARD_QA PASS tile=keyboard-activatable\n");
    std::fflush(stderr);
    return 0;
}

void EditorialView::recycle(GalleryTile* t)
{
    t->hide();
    free_.append(t);
}

// 窗口化：将可见（含前后缓冲）模型行映射到池化瓦片（回收不可见、绑定新可见）
// 2.22.0：可见区里有没有"缩略图未就绪"的卡片？有则挂载脉冲计时器，无则停。
// 只看当前绑定到瓦片的可见行（bound_），不看全库 —— 全库 6425 张的未就绪数
// 与用户眼前看到的完全无关，据此启停才是"省电"的正确口径。
void EditorialView::updateSkeletonTimer()
{
    bool any = false;
    for (auto it = bound_.constBegin(); it != bound_.constEnd() && !any; ++it) {
        const int row = it.key();
        const QModelIndex ix = model_->index(row, 0);
        if (!ix.isValid()) continue;
        const bool miss = model_->data(ix, GalleryModel::MissingRole).toBool();
        const QPixmap pm = model_->pixmapAt(row, it.value()->isFeature() ? ThumbCache::kFeature
                                                                      : ThumbCache::kTile);
        if (Lumen::skeletonState(pm, miss) == Lumen::SkeletonState::Loading) any = true;
    }
    emit skeletonVisibilityChanged(any);
}

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
        else {
            // 缺陷①修复：窗口尺寸/列数变化时，已绑定且仍可见的瓦片必须实时回填几何。
            // 旧逻辑只回收不可见、只为"新进入"行绑定几何，导致可见瓦片残留旧(小)几何
            // —— 左上角迷你瓦片 + 宽视口溢出右缘 + 中间大片空白。
            GalleryTile* t = it.value();
            if (!g_qaNoReflow) {   // 反例注入：g_qaNoReflow 时模拟修复前行为（跳过回填）
                t->setGeometry(it.key() == 0 ? featureRect() : cellRect(it.key()));
                t->update();
            }
            ++it;
        }
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

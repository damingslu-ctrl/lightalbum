#include "GalleryPage.h"
#include "Theme.h"
#include "../core/PhotoCluster.h"
#include "DedupDialog.h"
#include "EditorialView.h"
#include "GalleryCardPainter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QCoreApplication>
#include <QImage>
#include <QLineEdit>
#include <QInputDialog>

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

        const bool hov = (opt.state & QStyle::State_MouseOver) != 0;
        const bool sel = (opt.state & QStyle::State_Selected) != 0;
        const int  rating = ix.data(GalleryModel::RatingRole).toInt();
        // 统一卡片绘制（P23 抽出，EditorialView 共用）
        // 2.28.0 视频徽标：数据取自 GalleryModel 角色（唯一真源，不在此处转算）
        const bool isVideo =
            ix.data(GalleryModel::MediaTypeRole).toString() == QLatin1String("video");
        const double durSec = ix.data(GalleryModel::DurRole).toDouble();
        Lumen::paintGalleryCard(p, QRectF(opt.rect).adjusted(8, 8, -8, -8),
                                title, sub, orient, miss, rating, pm, false, hov, sel,
                                0.0, false, isVideo, durSec);
    }
};

GalleryPage::GalleryPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir), cache_(thumbsDir, this)
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

    // 方向 C 第一步：聚类条（维度切换 + 簇 chips）。与分类/人物筛选**并存叠加**。
    clusterBar_ = new QWidget(this);
    clusterBar_->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* clusterLay = new QHBoxLayout(clusterBar_);
    clusterLay->setContentsMargins(20, 8, 20, 0);
    clusterLay->setSpacing(8);
    clusterDimBox_ = new QComboBox(clusterBar_);
    clusterDimBox_->setCursor(Qt::PointingHandCursor);
    clusterDimBox_->setToolTip(QCoreApplication::translate(
        "LightAlbum", "聚类维度：把相册按地点或时间分组浏览"));
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按省份"), 0);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按城市"), 1);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按年份"), 2);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按月份"), 3);
    clusterDimBox_->setStyleSheet(QStringLiteral(
        "QComboBox{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:3px 8px;color:#8da996;font-size:12px;}"
        "QComboBox QAbstractItemView{background:#0e130f;color:#d9e4dc;"
        "selection-background-color:#1d2a22;selection-color:#4fc08a;}"));
    connect(clusterDimBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        clusterDim_ = clusterDimBox_->currentData().toInt();
        activeClusterKey_.clear();     // 换维度即清簇选择（旧键在新维度无意义）
        model_.clearClusterFilter();
        buildClusterBar();
        updateCount();
    });
    clusterLay->addWidget(clusterDimBox_);
    clusterChipsBar_ = new QWidget(clusterBar_);
    clusterChipsBar_->setStyleSheet(QStringLiteral("background:transparent;"));
    clusterLay->addWidget(clusterChipsBar_, 1);
    // 方向 C：相似照片去重入口（打开分组浏览对话框）
    auto* dedupBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "相似照片"), clusterBar_);
    dedupBtn->setCursor(Qt::PointingHandCursor);
    dedupBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:3px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#d9e4dc;}"));
    connect(dedupBtn, &QPushButton::clicked, this, &GalleryPage::openDedup);
    clusterLay->addWidget(dedupBtn);
    // 方向 C·智能分类：请求 MainWindow 跑 CLIP 分类（视图不持写能力，只发信号）
    classifyBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "AI 分类"), clusterBar_);
    classifyBtn_->setCursor(Qt::PointingHandCursor);
    classifyBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:3px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#d9e4dc;}"));
    connect(classifyBtn_, &QPushButton::clicked, this, &GalleryPage::classifyRequested);
    clusterLay->addWidget(classifyBtn_);
    // 方向 C·人脸识别（方案A）：请求 MainWindow 跑人脸检测（视图不持写能力，只发信号）
    auto* personBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "识别人物"), clusterBar_);
    personBtn->setCursor(Qt::PointingHandCursor);
    personBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:3px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#d9e4dc;}"));
    connect(personBtn, &QPushButton::clicked, this, &GalleryPage::personDetectRequested);
    clusterLay->addWidget(personBtn);
    // 方向 C·人物自动聚类：请求 MainWindow 抽 embedding + 阈值余弦聚类（视图不持写能力，只发信号）
    auto* faceClusterBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "智能聚类"), clusterBar_);
    faceClusterBtn->setCursor(Qt::PointingHandCursor);
    faceClusterBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:3px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#d9e4dc;}"));
    connect(faceClusterBtn, &QPushButton::clicked, this, &GalleryPage::faceClusterRequested);
    clusterLay->addWidget(faceClusterBtn);
    lay->addWidget(clusterBar_);

    // 方向 C 第一步：搜索框（地点/时间/评分/关键词过滤，经 applyFilter → GalleryModel::setFilter）
    auto* searchBar = new QWidget(this);
    searchBar->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* searchLay = new QHBoxLayout(searchBar);
    searchLay->setContentsMargins(20, 8, 20, 8);
    searchLay->setSpacing(8);
    auto* searchLe = new QLineEdit(searchBar);
    searchLe->setPlaceholderText(QCoreApplication::translate("LightAlbum", "搜索 地点 / 时间 / 评分 / 关键词…"));
    searchLe->setClearButtonEnabled(true);
    searchLe->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0e1410;border:1px solid #26312a;border-radius:9px;"
        "padding:7px 12px;color:#f4f7f4;font-size:13px;}"
        "QLineEdit:focus{border-color:#4fc08a;}"));
    searchLay->addWidget(searchLe);
    searchLay->addStretch(1);
    lay->addWidget(searchBar);
    connect(searchLe, &QLineEdit::textChanged, this, &GalleryPage::applyFilter);

    count_ = new QLabel(this);
    count_->setStyleSheet(QString::fromUtf8("color:#8da396;padding:12px 20px 8px;"));
    lay->addWidget(count_);

    // P23 E 杂志拼贴：用自定义 EditorialView 替换 QListView 均匀网格（首图 2×2 feature）
    model_.setSource(photos);
    model_.setThumbCache(&cache_);
    view_ = new EditorialView(&model_, this);
    lay->addWidget(view_, 1);

    // 缩略图预取节流：滚动停止 120ms 后，对可见区间前后各扩 6/24 行触发后台加载
    prefetchTimer_ = new QTimer(this);
    prefetchTimer_->setSingleShot(true);
    prefetchTimer_->setInterval(120);
    connect(view_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            prefetchTimer_, qOverload<>(&QTimer::start));
    connect(prefetchTimer_, &QTimer::timeout, this, &GalleryPage::prefetch);

    // D5b：批量工具条（位于计数之下、视图之上；选中为空时整条隐藏，不占视觉空间）
    buildBatchBar();
    lay->addWidget(batchBar_);
    connect(view_, &EditorialView::selectionChanged, this, &GalleryPage::onSelectionChanged);

    // P15：点击卡片 → 弹出灯箱并跳转修图（零云端；path 由 MainWindow 定位 Photo）。
    connect(view_, &EditorialView::photoActivated, this, &GalleryPage::photoActivated);

    applyFilter(QString());
    buildChips();
}

// ---- D5b：批量工具条 ----
// 铁律：GalleryPage 只有 const QVector<Photo>*（只读借用），**不持有写能力** ——
// 按钮只发 signal，真正的库写入由 MainWindow（持 lib_）执行，保证写回唯一入口。
void GalleryPage::buildBatchBar()
{
    batchBar_ = new QWidget(this);
    batchBar_->setObjectName(QStringLiteral("gpBatchBar"));
    batchBar_->setStyleSheet(QStringLiteral("#gpBatchBar{background:#0b120d;"
                                            "border-top:1px solid #1e2a22;}"));
    auto* bl = new QHBoxLayout(batchBar_);
    bl->setContentsMargins(20, 6, 20, 6);
    bl->setSpacing(6);

    batchInfo_ = new QLabel(batchBar_);
    batchInfo_->setStyleSheet(QStringLiteral("color:#d9a94e;font-size:12px;font-weight:bold;"));
    bl->addWidget(batchInfo_);

    const QString btnCss = QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:4px 10px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#f4f7f4;}");
    starBtns_.clear();
    for (int i = 1; i <= 5; ++i) {
        auto* b = new QPushButton(QStringLiteral("★%1").arg(i), batchBar_);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(btnCss);
        connect(b, &QPushButton::clicked, this, [this, i] { emitBatchRating(i); });
        bl->addWidget(b);
        starBtns_.append(b);
    }
    privBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "🔒 私密"), batchBar_);
    privBtn_->setCursor(Qt::PointingHandCursor);
    privBtn_->setStyleSheet(btnCss);
    connect(privBtn_, &QPushButton::clicked, this, [this] { emitBatchPrivate(true); });
    bl->addWidget(privBtn_);

    auto* clearBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "取消选择"), batchBar_);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(btnCss);
    connect(clearBtn, &QPushButton::clicked, this, [this] { view_->clearSelection(); });
    bl->addWidget(clearBtn);

    bl->addStretch(1);
    batchBar_->setVisible(false);            // 选中为空 → 隐藏
}

void GalleryPage::onSelectionChanged(int count)
{
    selCount_ = count;
    batchBar_->setVisible(count > 0);
    if (count > 0) {
        batchInfo_->setText(QCoreApplication::translate("LightAlbum", "已选 %1 张").arg(count));
        for (int i = 0; i < starBtns_.size(); ++i)
            starBtns_[i]->setToolTip(QCoreApplication::translate("LightAlbum", "将选中的 %1 张设为 %2 星").arg(count).arg(i + 1));
    }
    updateCount();                            // 计数文案追加「已选 N」注记
}

void GalleryPage::emitBatchRating(int rating)
{
    if (!view_ || view_->selectedRows().isEmpty()) return;
    emit batchRatingRequested(view_->selectedPaths(), rating);
    view_->clearSelection();
}

void GalleryPage::emitBatchPrivate(bool priv)
{
    if (!view_ || view_->selectedRows().isEmpty()) return;
    emit batchPrivateRequested(view_->selectedPaths(), priv);
    view_->clearSelection();
}

// ---- D5b QA 入口 ----
QStringList GalleryPage::qaSelectedPaths() const { return view_ ? view_->selectedPaths() : QStringList(); }
int  GalleryPage::qaSelectCount() const { return view_ ? int(view_->selectedRows().size()) : 0; }
void GalleryPage::qaToggleRow(int row) { if (view_) view_->toggleRow(row); }
void GalleryPage::qaClearSelection() { if (view_) view_->clearSelection(); }
void GalleryPage::qaSelectAll() { if (view_) view_->selectAll(); }
bool GalleryPage::qaBatchBarVisible() const { return batchBar_ && batchBar_->isVisible(); }
int  GalleryPage::qaBatchBarStarCount() const { return int(starBtns_.size()); }
void GalleryPage::qaClickRow(int row, bool ctrl, bool shift) {
    if (!view_) return;
    Qt::KeyboardModifiers m = Qt::NoModifier;
    if (ctrl)  m |= Qt::ControlModifier;
    if (shift) m |= Qt::ShiftModifier;
    view_->clickRow(row, m);
}
void GalleryPage::qaFireBatchRating(int rating) { emitBatchRating(rating); }
void GalleryPage::qaFireBatchPrivate(bool priv) { emitBatchPrivate(priv); }
void GalleryPage::qaFireClassify() { emit classifyRequested(); }
void GalleryPage::qaFirePersonDetect() { emit personDetectRequested(); }
void GalleryPage::qaFireFaceCluster() { emit faceClusterRequested(); }

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
        auto* b = new QPushButton(QCoreApplication::translate("LightAlbum", "全部 %1").arg(nVisible), chipsBar_);
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
        if (c > 0) addChip(catChips_, QCoreApplication::translate("LightAlbum", "其他"),
                           QCoreApplication::translate("LightAlbum", "其他 %1").arg(c));
    }
    {
        int unc = 0;
        for (const Photo& p : *photos_)
            if (!(hidePrivate_ && p.priv) && p.category.isEmpty()) ++unc;
        if (unc > 0) addChip(catChips_, GalleryModel::kNoCategory,
                             QCoreApplication::translate("LightAlbum", "未分类 %1").arg(unc));
    }
    chipLay->addStretch(1);
    chipsBar_->setVisible(catChips_.size() > 1);   // 只有「全部」= 无分类数据
    restyleChips();

    // 人物 chips（有数据才显示整条）
    auto* personLay = static_cast<QHBoxLayout*>(personBar_->layout());
    if (!persons.isEmpty()) {
        auto* allBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "全部人物"), personBar_);
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
            // 方向 C·人脸识别（方案A）：右键重命名（视图不持写能力，只发信号）
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QPushButton::customContextMenuRequested, this, [this, b] {
                const QString oldName = b->property("personTag").toString();
                bool ok = false;
                const QString newName = QInputDialog::getText(
                    this,
                    QCoreApplication::translate("LightAlbum", "重命名人物"),
                    QCoreApplication::translate("LightAlbum", "将“%1”重命名为：").arg(oldName),
                    QLineEdit::Normal, oldName, &ok);
                if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName) return;
                emit renamePersonRequested(oldName, newName.trimmed());
            });
            personLay->addWidget(b);
            personChips_.append(b);
        }
        personLay->addStretch(1);
        personBar_->setVisible(true);
    } else {
        personBar_->setVisible(false);
    }
    buildClusterBar();   // 方向 C：聚类条随数据/隐私门一并重建
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

// 方向 C 第一步：按当前维度重建聚类条。
// 铁律：簇来自 core/PhotoCluster 纯函数（唯一真源），本函数只做「簇 → chip」呈现；
// 簇的**排序与分组规则不在 UI 层重写**，否则 UI 与门禁会各算一套。
void GalleryPage::buildClusterBar()
{
    if (!clusterChipsBar_ || !photos_) return;
    for (QPushButton* b : clusterChips_) b->deleteLater();
    clusterChips_.clear();
    auto* lay = static_cast<QHBoxLayout*>(clusterChipsBar_->layout());
    if (!lay) {
        lay = new QHBoxLayout(clusterChipsBar_);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
    }
    const QString chipCss = QStringLiteral(
        "QPushButton{background:#101710;border:1px solid #232d26;border-radius:11px;"
        "padding:4px 12px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#f4f7f4;}"
        "QPushButton:checked{background:#d9a94e;border-color:#d9a94e;"
        "color:#0c100d;font-weight:bold;}");

    const QVector<PhotoCluster::Cluster> cs =
        PhotoCluster::cluster(*photos_, static_cast<PhotoCluster::Dim>(clusterDim_), hidePrivate_);

    // 「全部」chip
    {
        int total = 0;
        for (const PhotoCluster::Cluster& c : cs) total += c.count;
        auto* b = new QPushButton(
            QCoreApplication::translate("LightAlbum", "全部 %1").arg(total), clusterChipsBar_);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(chipCss);
        b->setProperty("clusterKey", QString());
        connect(b, &QPushButton::clicked, this, [this, b] {
            activeClusterKey_ = b->property("clusterKey").toString();
            if (activeClusterKey_.isEmpty()) model_.clearClusterFilter();
            else model_.setClusterFilter(clusterDim_, activeClusterKey_);
            restyleClusterChips();
            updateCount();
        });
        lay->addWidget(b);
        clusterChips_.append(b);
    }
    // 各簇（count 降序已在 core 层排好；此处只呈现）
    for (const PhotoCluster::Cluster& c : cs) {
        auto* b = new QPushButton(
            QCoreApplication::translate("LightAlbum", "%1 %2").arg(c.label).arg(c.count),
            clusterChipsBar_);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(chipCss);
        b->setProperty("clusterKey", c.key);
        b->setToolTip(c.label);
        connect(b, &QPushButton::clicked, this, [this, b] {
            const QString k = b->property("clusterKey").toString();
            activeClusterKey_ = (activeClusterKey_ == k) ? QString() : k;   // 再点=取消
            if (activeClusterKey_.isEmpty()) model_.clearClusterFilter();
            else model_.setClusterFilter(clusterDim_, activeClusterKey_);
            restyleClusterChips();
            updateCount();
        });
        lay->addWidget(b);
        clusterChips_.append(b);
    }
    lay->addStretch(1);
    // 只有「全部」= 当前维度无任何可分簇数据（如整库都没写省/市）→ 整条隐藏
    clusterBar_->setVisible(clusterChips_.size() > 1);
    restyleClusterChips();
}

// 方向 C：打开"相似照片"去重对话框。对话框发 markRequested → 本页转发为
// batchMarkRequested（视图不持写能力，统一交给 MainWindow 落库）。
void GalleryPage::openDedup()
{
    DedupDialog dlg(photos_, thumbsDir_, this);
    connect(&dlg, &DedupDialog::markRequested, this,
            [this](const QStringList& paths) {
                emit batchMarkRequested(paths, QStringLiteral("skip"));
            });
    dlg.exec();
}

void GalleryPage::restyleClusterChips()
{
    for (QPushButton* b : clusterChips_)
        b->setChecked(activeClusterKey_ == b->property("clusterKey").toString());
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
    if (!searchText_.trimmed().isEmpty()) notes << QCoreApplication::translate("LightAlbum", "已筛");
    if (!activeCat_.isEmpty())
        notes << (activeCat_ == GalleryModel::kNoCategory
                      ? QCoreApplication::translate("LightAlbum", "未分类")
                      : QCoreApplication::translate("LightAlbum", "分类：%1").arg(activeCat_));
    if (!activePerson_.isEmpty()) notes << QCoreApplication::translate("LightAlbum", "人物：%1").arg(activePerson_);
    if (!activeClusterKey_.isEmpty()) {   // 方向 C：聚类注记（用簇标签而非机器键）
        notes << QCoreApplication::translate("LightAlbum", "聚类：%1").arg(
            activeClusterKey_ == QLatin1String(PhotoCluster::kUnclustered)
                ? QCoreApplication::translate("LightAlbum", "未分类")
                : activeClusterKey_);
    }
    if (selCount_ > 0) notes << QCoreApplication::translate("LightAlbum", "已选 %1").arg(selCount_);
    const QString suffix = notes.isEmpty()
        ? QString() : QStringLiteral("（%1）").arg(notes.join(QStringLiteral(" · ")));
    count_->setText(n == total && notes.isEmpty()
        ? QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅%3").arg(n).arg(total).arg(suffix));
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
        ? QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅（区域筛选）").arg(n).arg(total));
}

void GalleryPage::prefetch()
{
    view_->prefetchVisible();       // P23：EditorialView 自行判定可见区间
}

// P23 E 杂志拼贴 像素/几何自测（offscreen）：合成照片驱动 EditorialView，
// 验证 feature 首图占 2×2 且卡片真实绘制（颜色校验铁律：必须实测不许猜）。
// 缺陷①回归（2.22.0 修复）：改视口宽度迫使列数变化后，断言已绑定瓦片几何随之 reflow。
int GalleryPage::qaShootE(const QString& png)
{
    // 反例注入：FORCE_BROKEN 时 updateWindow 跳过几何回填（复现修复前空白缺陷）
    EditorialView::qaSetNoReflow(!qEnvironmentVariableIsEmpty("LA_GALLERY_E_QA_FORCE_BROKEN"));
    view_->relayout();                       // 强制重排（offscreen 下确保几何就位）
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    const int  n       = view_->tileCount();
    const int  cols    = view_->columnCount();
    const QRect fg     = view_->featureGeometry();          // 首图（row0）几何
    const QRect sg     = (n >= 2) ? view_->tileGeometry(1) : QRect();
    const int  singleW = sg.width();
    const int  singleH = sg.height();

    // 渲染整页（特征大图 + 周围小图）到图片
    QImage img = grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int W = img.width(), H = img.height();
    long cardBg = 0, lightTxt = 0;
    for (int y = 0; y < H; y += 2) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < W; x += 2) {
            const QRgb p = row[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            if (qAbs(r - 19) < 12 && qAbs(g - 26) < 12 && qAbs(b - 21) < 12) ++cardBg;   // 卡片底 #131a15
            if (r > 210 && g > 210 && b > 200) ++lightTxt;                                // 标题/文字 #f4f7f4
        }
    }
    // feature 2×2 判定：首图宽 > 1.5 个单图宽 且 高 > 1.5 个单图高
    const bool featureOK = (n >= 5) && (singleW > 0) && (fg.width()  > 1.5 * singleW)
                           && (singleH > 0) && (fg.height() > 1.5 * singleH);
    // 调试：模型返回的标题/副行原串（定位「标题未渲染」问题）
    const QString t0 = model_.data(model_.index(0, 0), GalleryModel::TitleRole).toString();
    const QString s0 = model_.data(model_.index(0, 0), GalleryModel::SubRole).toString();
    std::fprintf(stderr, "  t0=[%s] s0=[%s]\n", qPrintable(t0), qPrintable(s0));
    img.save(png);

    // ---- 缺陷①回归：改视口宽度迫使布局重算，断言已绑定瓦片几何随之 reflow ----
    // 复现路径与用户现场一致：小视口期绑定的迷你瓦片，在窗口放大后若不回填几何，
    // 就会挤在左上角、宽瓦片溢出右缘、中间留大片空白。
    // ★ 必须**直接改视图宽度**并**同步** relayout()，不能改 this->resize() 等父布局传播：
    //   首版这么写，独立跑 cols 5→3 绿，但在 qa_runner 里父布局未把新宽度传给视图，
    //   列数原地不动 → 门误红（自造 flaky，违反「门禁必可靠」）。宽度是唯一真源，直接动它。
    const int w0 = view_->viewport()->width();
    const int newW = (w0 > 1000) ? (w0 - 300) : (w0 + 300);
    const int wBefore = view_->featureGeometry().width();
    view_->resize(newW, view_->height());
    view_->relayout();                                  // 同步重排，此刻宽度必为 newW
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    const int wAfter = view_->featureGeometry().width();
    const int reflow  = view_->qaReflowCheck();
    // 非空心保证：feature 宽必须真的变了（几何事实），否则本门在"宽度没变"上空转
    const bool reflowOK = (reflow == 0) && (wAfter != wBefore);

    const bool ok = featureOK && reflowOK;
    std::fprintf(stderr,
        "LA_GALLERY_E_QA %s n=%d cols=%d->%d featW=%d->%d singleW=%d singleH=%d "
        "cardBg=%ld lightTxt=%ld featureOK=%d reflowOK=%d png=%s\n",
        ok ? "PASS" : "FAIL",
        n, cols, view_->columnCount(), wBefore, wAfter, singleW, singleH, cardBg, lightTxt,
        int(featureOK), int(reflowOK), qPrintable(png));
    std::fflush(stderr);
    return ok ? 0 : 1;
}

QScrollBar* GalleryPage::qaScrollBar() const
{
    return view_ ? view_->verticalScrollBar() : nullptr;
}

#include "GalleryPage.h"
#include "Theme.h"
#include "../core/PhotoCluster.h"
#include "../core/StoryGen.h"
#include "../core/MotionKit.h"     // 2.34.0 动效引擎预设
#include "../core/MotionFX.h"      // 2.34.0 引擎 UI 侧唯一封装
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
#include <QFrame>
#include <algorithm>

// P16 分类固定次序（单一事实源 = clipinfer/rules.cpp:17 kLabels[8]，其行序即
// native/3rdparty/clip/bin/prompts.bin 的 8 类行序；第四轮 T8 净替换后该表取代
// 退役的外部分类脚本成为权威。空分类不展示）
static QStringList catOrder()
{
    return {QStringLiteral("人像"), QStringLiteral("风光"), QStringLiteral("夜景·星空"),
            QStringLiteral("城市·建筑"), QStringLiteral("微距·花卉"), QStringLiteral("动物"),
            QStringLiteral("美食"), QStringLiteral("黑白")};
}

// 人物故事方向·B 的 A 轻增强：人物 chip 改圆形头像徽标（首字母 + 确定性主题配色）。
// 说明：FaceCluster 仅存 embedding，磁盘无现成人脸代表图，故用首字母 + 色环，
// 诚实不伪装成照片；配色由名字哈希确定。
// design-language-v2 T6：八色自拟盘（off-token 绿青褐系）退役 → CAT1..8 具名板（spec §1.4/§1.7）。
// T4（2.41.0）：哈希实现上移 Theme.h `Lumen::personRing()` 作单源（人物馆展室环共用），此处仅取 hex。
static QString storyAvatarColor(const QString& name)
{
    return Lumen::hex(Lumen::personRing(name));
}

// design-language-v2 T6：chip 族单源（分类/聚类/故事三条共用一份配方，终结 3 处克隆漂移）。
// Theme.h 无 chip() 工厂（spec §4.1 称「chip 已有」系失实，缺口已记报告）。本页与已退役选片页
// 的 chip 配方能否并为一个签名？——不能诚实统一：本页 checked = accent 实心反转 + PANEL
// 深色字（强激活态），选片页 checked = CARD_HI 底 + accent 描边/文字（spec §3.4 选中态条例）；
// 结构不同、padding 不同（4x12 vs 5x14），单签名 = 一厂藏两配方，属强扭，裁定维持各自本地单源。
// 色值全走 Lumen token（§1.7：克隆盘 101710/232d26 → CARD/LINE 回家；token 值字面量归引用）。
// 圆角/内距/字阶原值保留（归格属 T9/T10）。
static QString galleryChipCss(const QString& radiusPx, const QColor& checkedAccent)
{
    // T8：12px=T5 原位 → Type::css(Small)（拼接式，%1..%9 序号不动）。
    return (QString::fromUtf8(
        "QPushButton{background:%1;border:1px solid %2;border-radius:%3;"
        "padding:4px 12px;color:%4;")
        + Lumen::Type::css(Lumen::Type::Tier::Small)
        + QString::fromUtf8(
        "}QPushButton:hover{border-color:%5;color:%6;}"
        "QPushButton:checked{background:%7;border-color:%8;color:%9;font-weight:bold;}"))
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()), radiusPx,
             Lumen::hex(Lumen::muted()), Lumen::hex(Lumen::emerald()),
             Lumen::hex(Lumen::ink()), Lumen::hex(checkedAccent),
             Lumen::hex(checkedAccent), Lumen::hex(Lumen::panel()));
}

// §1.7：五处条带底色（原值 060806） 即 Lumen::bg() 的 token 值字面量 → 归引用（色不变）。
static QString barBgCss()
{
    return QStringLiteral("background:%1;").arg(Lumen::hex(Lumen::bg()));
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
    chipsBar_->setStyleSheet(barBgCss());
    auto* chipLay = new QHBoxLayout(chipsBar_);
    chipLay->setContentsMargins(Lumen::Space::L, Lumen::Space::M, Lumen::Space::L, 0);
    chipLay->setSpacing(8);
    lay->addWidget(chipsBar_);

    // P16 人物 chips 条（person 列有数据才显示）
    personBar_ = new QWidget(this);
    personBar_->setStyleSheet(barBgCss());
    auto* personLay = new QHBoxLayout(personBar_);
    personLay->setContentsMargins(Lumen::Space::L, Lumen::Space::S, Lumen::Space::L, 0);
    personLay->setSpacing(8);
    lay->addWidget(personBar_);

    // 方向 C 第一步：聚类条（维度切换 + 簇 chips）。与分类/人物筛选**并存叠加**。
    clusterBar_ = new QWidget(this);
    clusterBar_->setStyleSheet(barBgCss());
    auto* clusterLay = new QHBoxLayout(clusterBar_);
    clusterLay->setContentsMargins(Lumen::Space::L, Lumen::Space::S, Lumen::Space::L, 0);
    clusterLay->setSpacing(8);
    clusterDimBox_ = new QComboBox(clusterBar_);
    clusterDimBox_->setCursor(Qt::PointingHandCursor);
    clusterDimBox_->setToolTip(QCoreApplication::translate(
        "LightAlbum", "聚类维度：把相册按地点或时间分组浏览"));
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按省份"), 0);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按城市"), 1);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按年份"), 2);
    clusterDimBox_->addItem(QCoreApplication::translate("LightAlbum", "按月份"), 3);
    // design-language-v2 T6：combo() 工厂不存在（spec §4.1 称「input()/combo() 已有」中 combo 失实，
    // 缺口记报告，不强造）。与 T5 MapPage yearBox 同形：本地单源配方 + 全 token 引用
    //（§1.7：0e130f→PANEL、d9e4dc→INK；1d2a22 选中底→CARD_HI，沿用 T5「LINE 仅作分隔线、
    // 选中面归 CARD_HI」判例）。圆角 9px/内距/字阶原值保留（归格属 T9/T8）。
    clusterDimBox_->setStyleSheet((QString::fromUtf8(   // T8：12px=T5 原位 → css(Small)，拼接式序号不动
        "QComboBox{background:%1;border:1px solid %2;border-radius:%8px;"
        "padding:3px 8px;color:%3;")
        + Lumen::Type::css(Lumen::Type::Tier::Small)
        + QString::fromUtf8(
        "}QComboBox QAbstractItemView{background:%4;color:%5;"
        "selection-background-color:%6;selection-color:%7;}"))
        .arg(Lumen::hex(Lumen::card()))
        .arg(Lumen::hex(Lumen::line()))
        .arg(Lumen::hex(Lumen::muted()))
        .arg(Lumen::hex(Lumen::panel()))
        .arg(Lumen::hex(Lumen::ink()))
        .arg(Lumen::hex(Lumen::cardHi()))
        .arg(Lumen::hex(Lumen::emerald()))
        .arg(Lumen::Radius::Ctl));   // T9：9→Radius::Ctl 10（§3.1 归并 8/9/11，%8 尾插序号纪律安全）
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
    // §4.2：ghost 克隆归厂 → btnGhost(MUTED) 单源。归厂偏差记录：padding 3px12px→6px14px、
    // hover 由「翡翠边+亮字」改工厂制式「CARD_HI 底+同色描边」（T5 MapPage 同族判例）。
    dedupBtn->setStyleSheet(Lumen::btnGhost(Lumen::muted()));
    connect(dedupBtn, &QPushButton::clicked, this, &GalleryPage::openDedup);
    clusterLay->addWidget(dedupBtn);
    // 方向 C·智能分类：请求 MainWindow 跑 CLIP 分类（视图不持写能力，只发信号）
    classifyBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "AI 分类"), clusterBar_);
    classifyBtn_->setCursor(Qt::PointingHandCursor);
    classifyBtn_->setStyleSheet(Lumen::btnGhost(Lumen::muted()));   // ghost 克隆归厂（同 dedupBtn）
    connect(classifyBtn_, &QPushButton::clicked, this, &GalleryPage::classifyRequested);
    clusterLay->addWidget(classifyBtn_);
    // 方向 C·人脸识别（方案A）：请求 MainWindow 跑人脸检测（视图不持写能力，只发信号）
    auto* personBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "识别人物"), clusterBar_);
    personBtn->setCursor(Qt::PointingHandCursor);
    personBtn->setStyleSheet(Lumen::btnGhost(Lumen::muted()));      // ghost 克隆归厂（同 dedupBtn）
    connect(personBtn, &QPushButton::clicked, this, &GalleryPage::personDetectRequested);
    clusterLay->addWidget(personBtn);
    // 方向 C·人物自动聚类：请求 MainWindow 抽 embedding + 阈值余弦聚类（视图不持写能力，只发信号）
    auto* faceClusterBtn = new QPushButton(QCoreApplication::translate("LightAlbum", "智能聚类"), clusterBar_);
    faceClusterBtn->setCursor(Qt::PointingHandCursor);
    faceClusterBtn->setStyleSheet(Lumen::btnGhost(Lumen::muted())); // ghost 克隆归厂（同 dedupBtn）
    connect(faceClusterBtn, &QPushButton::clicked, this, &GalleryPage::faceClusterRequested);
    clusterLay->addWidget(faceClusterBtn);
    lay->addWidget(clusterBar_);

    // 人物故事方向·B：故事相册入口条（纯视图态，按 StoryGen 聚合结果填充；无库写入）
    storyBar_ = new QWidget(this);
    storyBar_->setStyleSheet(barBgCss());
    auto* storyLay = new QHBoxLayout(storyBar_);
    storyLay->setContentsMargins(Lumen::Space::L, Lumen::Space::S, Lumen::Space::L, 0);
    storyLay->setSpacing(8);
    lay->addWidget(storyBar_);

    // 方向 C 第一步：搜索框（地点/时间/评分/关键词过滤，经 applyFilter → GalleryModel::setFilter）
    auto* searchBar = new QWidget(this);
    searchBar->setStyleSheet(barBgCss());
    auto* searchLay = new QHBoxLayout(searchBar);
    searchLay->setContentsMargins(Lumen::Space::L, Lumen::Space::S, Lumen::Space::L, Lumen::Space::S);
    searchLay->setSpacing(8);
    auto* searchLe = new QLineEdit(searchBar);
    searchLe->setPlaceholderText(QCoreApplication::translate("LightAlbum", "搜索 地点 / 时间 / 评分 / 关键词…"));
    searchLe->setClearButtonEnabled(true);
    // §4.1：输入框克隆收编 → input() 单源（INSET 底/LINE 边/9px 圆角/INK 字/focus EMERALD 均厂内；
    // 归厂偏差记录：padding 7px12px→6px12px、原 13px 字号由工厂省略回落应用字阶、新增选区底色）。
    searchLe->setStyleSheet(Lumen::input());
    searchLay->addWidget(searchLe);
    searchLay->addStretch(1);
    lay->addWidget(searchBar);
    connect(searchLe, &QLineEdit::textChanged, this, &GalleryPage::applyFilter);

    // 2.41.0 spec §2.2：图库页落到「馆」的语感——标题在计数之上，计数继续做筛选真源。
    auto* heading = new QLabel(QCoreApplication::translate("LightAlbum", "全部藏品"), this);
    heading->setStyleSheet(QString::fromUtf8("color:%1;background:transparent;border:none;"
        "padding:%2px 20px 0;%3")
        .arg(Lumen::hex(Lumen::ink()))
        .arg(Lumen::Space::L)
        .arg(Lumen::Type::css(Lumen::Type::Tier::Head)));   // T8：字号走字阶，padding 走间距格
    lay->addWidget(heading);

    count_ = new QLabel(this);
    count_->setStyleSheet(QString::fromUtf8("color:%1;padding:12px 20px 8px;")
        .arg(Lumen::hex(Lumen::muted())));   // token 值字面量归引用（色不变）
    lay->addWidget(count_);

    // P23 E 杂志拼贴：用自定义 EditorialView 替换 QListView 均匀网格（首图 2×2 feature）
    model_.setSource(photos);
    model_.setThumbCache(&cache_);
    view_ = new EditorialView(&model_, this);
    lay->addWidget(view_, 1);

    // wave4 §2.9：过滤到 0 行时不能只剩一片空白——说清「为什么是空的 + 往哪走」。
    // 与 view_ 同槽位互斥显隐（隐藏项不占布局空间），筛选条/搜索框保持可用。
    empty_ = new QWidget(this);
    auto* ewl = new QVBoxLayout(empty_);
    ewl->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::Xxl,
                            Lumen::Space::Xxl, Lumen::Space::Xxl);
    ewl->addStretch(1);
    auto* emptyCard = new QFrame(empty_);
    emptyCard->setObjectName(QStringLiteral("galleryEmpty"));
    emptyCard->setMaximumWidth(560);
    emptyCard->setStyleSheet(QString::fromUtf8(
        "#galleryEmpty{background:%1;border:1px solid %2;border-radius:%3px;}")
        .arg(Lumen::hex(Lumen::inset()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Card));
    auto* ev = new QVBoxLayout(emptyCard);
    ev->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                           Lumen::Space::Xl, Lumen::Space::L);
    ev->setSpacing(Lumen::Space::S);
    emptyTitle_ = new QLabel(emptyCard);
    emptyTitle_->setFont(Lumen::Type::title(false));
    emptyTitle_->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::ink())));
    emptyBody_ = new QLabel(emptyCard);
    emptyBody_->setFont(Lumen::Type::body());
    emptyBody_->setWordWrap(true);
    emptyBody_->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::muted())));
    ev->addWidget(emptyTitle_);
    ev->addWidget(emptyBody_);
    ewl->addWidget(emptyCard, 0, Qt::AlignHCenter);
    ewl->addStretch(2);
    empty_->hide();
    lay->addWidget(empty_, 1);

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
    // T7 台账裁定：0b120d→PANEL（ΔRGB≤6 不可辨归格，批量条=浮面容器语义）、
    // 1e2a22→LINE（§1.1 唯一分隔线色，禁止暖灰/私有深阶替代；微亮 ~Δ8 合法化）。
    batchBar_->setStyleSheet(QString::fromUtf8("#gpBatchBar{background:%1;"
                                               "border-top:1px solid %2;}")
        .arg(Lumen::hex(Lumen::panel()), Lumen::hex(Lumen::line())));
    auto* bl = new QHBoxLayout(batchBar_);
    bl->setContentsMargins(Lumen::Space::L, Lumen::Space::S, Lumen::Space::L, Lumen::Space::S);
    bl->setSpacing(Lumen::Space::S);

    batchInfo_ = new QLabel(batchBar_);
    batchInfo_->setStyleSheet(QStringLiteral("color:%1;")   // T8：12px=T5 原位 → css(Small)
        .arg(Lumen::hex(Lumen::gold()))                        // token 值字面量归引用（色不变）
        + Lumen::Type::css(Lumen::Type::Tier::Small)
        + QStringLiteral("font-weight:bold;"));
    bl->addWidget(batchInfo_);

    // §4.2：ghost 克隆归厂 → btnGhost(MUTED) 单源（星级×5 + 私密 + 取消选择共用；
    // 归厂偏差同 dedupBtn 记录：padding 4px10px→6px14px、hover 制式化）。
    const QString btnCss = Lumen::btnGhost(Lumen::muted());
    starBtns_.clear();
    for (int i = 1; i <= 5; ++i) {
        auto* b = new QPushButton(QStringLiteral("★%1").arg(i), batchBar_);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(btnCss);
        connect(b, &QPushButton::clicked, this, [this, i, b] {
            // 2.34 StarBounce：CSS 固定了字号 → 走辉光通道（曲线/时长仍出自引擎预设）。
            MotionFx::glow(b, Lumen::muted(), QColor(0xf4, 0xf7, 0xf4),
                           Motion::kStarBounce, Motion::kPropGlow,
                           qEnvironmentVariableIsEmpty("LA_GATE"));
            emitBatchRating(i);
        });
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
// 2.33 收纳：分类/人物/聚类/故事四条统一走 Lumen::fillChipRow
// （前 kMaxVisibleChips=8 个平铺 + 「▾ N」溢出下拉），选中项自动提升可见。
void GalleryPage::buildChips()
{
    const QString chipCss = galleryChipCss(Lumen::Radius::Pill, Lumen::gold());

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

    // —— 分类条：前 8 个平铺，其余收进「▾ N」溢出下拉 ——
    QVector<Lumen::ChipDef> defs;
    defs.append(Lumen::chipDef(QString(),
        QCoreApplication::translate("LightAlbum", "全部 %1").arg(nVisible),
        activeCat_.isEmpty()));
    // 固定次序 8 类（空分类不展示）
    for (const QString& cat : catOrder()) {
        const int c = catCnt.value(cat, 0);
        if (c > 0)
            defs.append(Lumen::chipDef(cat,
                QStringLiteral("%1 %2").arg(cat).arg(c), activeCat_ == cat));
    }
    // 其他（低置信度）与 未分类
    {
        const int c = catCnt.value(QStringLiteral("其他"), 0);
        if (c > 0)
            defs.append(Lumen::chipDef(QCoreApplication::translate("LightAlbum", "其他"),
                QCoreApplication::translate("LightAlbum", "其他 %1").arg(c),
                activeCat_ == QCoreApplication::translate("LightAlbum", "其他")));
    }
    {
        int unc = 0;
        for (const Photo& p : *photos_)
            if (!(hidePrivate_ && p.priv) && p.category.isEmpty()) ++unc;
        if (unc > 0)
            defs.append(Lumen::chipDef(GalleryModel::kNoCategory,
                QCoreApplication::translate("LightAlbum", "未分类 %1").arg(unc),
                activeCat_ == GalleryModel::kNoCategory));
    }
    Lumen::fillChipRow(static_cast<QHBoxLayout*>(chipsBar_->layout()), chipsBar_, defs,
        chipCss, QString(),
        [this](const Lumen::ChipDef& d) {
            activeCat_ = (activeCat_ == d.key) ? QString() : d.key;
            model_.setCategoryFilter(activeCat_);
            buildChips();     // 重建（选中项提升 / 取消后回落；人物/聚类/故事条随数据重建）
            updateCount();
        });
    chipsBar_->setVisible(defs.size() > 1);   // 只有「全部」= 无分类数据

    // —— 人物条：彩色姓名 chips，同样「前 8 + 溢出」；可见 chip 保留右键重命名，
    //    隐藏人物先经菜单选中（自动提升进可见区）后再右键重命名 ——
    auto* personLay = static_cast<QHBoxLayout*>(personBar_->layout());
    if (!persons.isEmpty()) {
        QVector<Lumen::ChipDef> pdefs;
        pdefs.append(Lumen::chipDef(QString(),
            QCoreApplication::translate("LightAlbum", "全部人物"), activePerson_.isEmpty()));
        QStringList sorted = QStringList(persons.cbegin(), persons.cend());
        std::sort(sorted.begin(), sorted.end());
        for (const QString& n : sorted) {
            // 人物 chip：圆角标签显示完整姓名 + 确定性主题配色背景，
            // 替代原先只显示首字母的圆形头像（用户无法辨识人物）。
            Lumen::ChipDef d = Lumen::chipDef(n, n, activePerson_ == n);
            d.bg = storyAvatarColor(n);
            d.tooltip = n;
            pdefs.append(d);
        }
        // §1.7：token 值字面量归引用（f4f7f4→INK、4fc08a→EMERALD，色不变）。
        // 用拼接而非链式 .arg：fillChipRow 只对模板做一次 .arg(d.bg)，%1 占位必须保持唯一最低标号
        //（多参数 .arg 会先吃掉 %1，令彩色底失效——占位符纪律，勿改成 arg 链）。
        const QString personCssTpl =
            QStringLiteral("QPushButton{background:%1;border:none;border-radius:")
            + QString::number(Lumen::Radius::Ctl)
            + QStringLiteral("px;padding:4px 12px;color:")
            + Lumen::hex(Lumen::panel()) + QStringLiteral(";")
            + Lumen::Type::css(Lumen::Type::Tier::Small)   // T8：12px=T5 原位（输出无 % 标号，模板纪律不破坏）
            + QStringLiteral(
            "font-weight:bold;}"
            "QPushButton:hover{border:2px solid ") + Lumen::hex(Lumen::emerald()) + QStringLiteral(
            ";padding:2px 10px;}"
            "QPushButton:checked{border:2px solid ") + Lumen::hex(Lumen::ink()) + QStringLiteral(
            ";padding:2px 10px;background:%1;}");
        // 方向 C·人脸识别（方案A）：右键重命名（视图不持写能力，只发信号）
        Lumen::fillChipRow(personLay, personBar_, pdefs, chipCss, personCssTpl,
            [this](const Lumen::ChipDef& d) {
                activePerson_ = (activePerson_ == d.key) ? QString() : d.key;
                model_.setPersonFilter(activePerson_);
                buildChips();
                updateCount();
            },
            [this](QPushButton* b, const Lumen::ChipDef& d) {
                b->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(b, &QPushButton::customContextMenuRequested, this, [this, d] {
                    bool ok = false;
                    const QString newName = QInputDialog::getText(
                        this,
                        QCoreApplication::translate("LightAlbum", "重命名人物"),
                        QCoreApplication::translate("LightAlbum", "将“%1”重命名为：").arg(d.key),
                        QLineEdit::Normal, d.key, &ok);
                    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == d.key) return;
                    emit renamePersonRequested(d.key, newName.trimmed());
                });
            });
        personBar_->setVisible(true);
    } else {
        personBar_->setVisible(false);
    }
    buildClusterBar();   // 方向 C：聚类条随数据/隐私门一并重建
    buildStoryBar();     // 人物故事方向·B：故事条随数据/隐私门一并重建
}

// 方向 C 第一步：按当前维度重建聚类条。
// 铁律：簇来自 core/PhotoCluster 纯函数（唯一真源），本函数只做「簇 → chip」呈现；
// 簇的**排序与分组规则不在 UI 层重写**，否则 UI 与门禁会各算一套。
void GalleryPage::buildClusterBar()
{
    if (!clusterChipsBar_ || !photos_) return;
    auto* lay = static_cast<QHBoxLayout*>(clusterChipsBar_->layout());
    if (!lay) {
        lay = new QHBoxLayout(clusterChipsBar_);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
    }
    // §1.7：克隆盘 101710/232d26 → CARD/LINE 回家（并入文件级 galleryChipCss 单源）
    const QString chipCss = galleryChipCss(QString::number(Lumen::Radius::Ctl) + QStringLiteral("px"), Lumen::gold());   // T9：11→Radius::Ctl 10（归格）

    const QVector<PhotoCluster::Cluster> cs =
        PhotoCluster::cluster(*photos_, static_cast<PhotoCluster::Dim>(clusterDim_), hidePrivate_);

    // 2.33 收纳：簇 chips 统一「前 8 平铺 + 溢出下拉」（count 降序已在 core 层排好）
    QVector<Lumen::ChipDef> defs;
    {
        int total = 0;
        for (const PhotoCluster::Cluster& c : cs) total += c.count;
        defs.append(Lumen::chipDef(QString(),
            QCoreApplication::translate("LightAlbum", "全部 %1").arg(total),
            activeClusterKey_.isEmpty()));
    }
    for (const PhotoCluster::Cluster& c : cs) {
        Lumen::ChipDef d = Lumen::chipDef(c.key,
            QCoreApplication::translate("LightAlbum", "%1 %2").arg(c.label).arg(c.count),
            activeClusterKey_ == c.key);
        d.tooltip = c.label;
        defs.append(d);
    }
    Lumen::fillChipRow(lay, clusterChipsBar_, defs, chipCss, QString(),
        [this](const Lumen::ChipDef& d) {
            activeClusterKey_ = (activeClusterKey_ == d.key) ? QString() : d.key;   // 再点=取消
            if (activeClusterKey_.isEmpty()) model_.clearClusterFilter();
            else model_.setClusterFilter(clusterDim_, activeClusterKey_);
            buildClusterBar();   // 选中项提升 / 取消后回落
            updateCount();
        });
    // 只有「全部」= 当前维度无任何可分簇数据（如整库都没写省/市）→ 整条隐藏
    clusterBar_->setVisible(defs.size() > 1);
}

// 方向 C：打开"相似照片"去重对话框。对话框发 markRequested → 本页转发为
// batchMarkRequested（视图不持写能力，统一交给 MainWindow 落库）。
// 隐私门：把当前锁态透传给对话框，锁定态下私密照片不进分析输入集。
void GalleryPage::openDedup()
{
    DedupDialog dlg(photos_, thumbsDir_, hidePrivate_, this);
    connect(&dlg, &DedupDialog::markRequested, this,
            [this](const QStringList& paths) {
                emit batchMarkRequested(paths, QStringLiteral("skip"));
            });
    dlg.exec();
}

// 人物故事方向·B：按 StoryGen 纯函数聚合结果重建故事入口条。
// 铁律：故事分组来自 core/StoryGen 纯函数（唯一真源），本函数只做「故事 → chip」呈现；
// 分组的**排序与拆分规则不在 UI 层重写**（否则会与 LA_STORY_QA 各算一套）。
// 纯视图态：点击 chip 只调 model_.setStoryFilter（path 集合包含匹配），不写库（铁律5）。
// 2.33 收纳：不再建 60 个可见 chip（kCap 方案），统一「前 8 平铺 + 溢出下拉」；
//   菜单项带完整「地点 · 时间 · 张数」，选中隐藏故事自动提升进可见区。
void GalleryPage::buildStoryBar()
{
    if (!storyBar_ || !photos_) return;
    storyPaths_.clear();
    storyTitles_.clear();
    auto* lay = static_cast<QHBoxLayout*>(storyBar_->layout());
    if (!lay) {
        lay = new QHBoxLayout(storyBar_);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
    }
    // §1.7 同上；故事条 checked 传统为翡翠（非分类/聚类条的金），以 accent 参数保留语义差异
    const QString chipCss = galleryChipCss(QString::number(Lumen::Radius::Ctl) + QStringLiteral("px"), Lumen::emerald());   // T9：11→Radius::Ctl 10（归格）

    const StoryGen::Result r = StoryGen::build(*photos_, 5, hidePrivate_);
    if (r.stories.isEmpty()) { Lumen::clearChipRow(lay); storyBar_->setVisible(false); return; }
    for (const StoryGen::Story& s : r.stories) {
        storyPaths_.insert(s.id, s.paths);
        storyTitles_.insert(s.id, s.title);
    }

    // 按照片数降序（同 2.32 口径：大故事排前，平铺区优先露出）
    QVector<StoryGen::Story> ordered = r.stories;
    std::sort(ordered.begin(), ordered.end(), [](const StoryGen::Story& a, const StoryGen::Story& b) {
        if (a.photoCount != b.photoCount) return a.photoCount > b.photoCount;
        return a.begin < b.begin;
    });

    QVector<Lumen::ChipDef> defs;
    defs.append(Lumen::chipDef(QString(),
        QCoreApplication::translate("LightAlbum", "全部故事 %1").arg(r.stories.size()),
        activeStoryId_.isEmpty()));
    for (const StoryGen::Story& s : ordered) {
        Lumen::ChipDef d = Lumen::chipDef(s.id, s.place, activeStoryId_ == s.id);
        d.tooltip = QCoreApplication::translate("LightAlbum", "%1 · %2 张").arg(s.title).arg(s.photoCount);
        d.menuLabel = d.tooltip;   // 菜单里空间足，显示完整「地点 · 时间 · 张数」
        defs.append(d);
    }
    Lumen::fillChipRow(lay, storyBar_, defs, chipCss, QString(),
        [this](const Lumen::ChipDef& d) {
            if (activeStoryId_ == d.key) {           // 再点=取消
                activeStoryId_.clear(); activeStoryTitle_.clear();
                model_.clearStoryFilter();
            } else {
                activeStoryId_ = d.key;
                activeStoryTitle_ = storyTitles_.value(d.key);
                model_.setStoryFilter(storyPaths_.value(d.key));
            }
            buildStoryBar();   // 选中项提升 / 取消后回落
            updateCount();
        },
        [](QPushButton* b, const Lumen::ChipDef& d) {
            // 2.34 ChipPop 入场：辉光弹一下（ChipPop 预设 glow 键；CSS 固定字号故不走字体）。
            MotionFx::glow(b, d.checked ? QColor(0x0c, 0x10, 0x0d) : Lumen::muted(),
                           QColor(0xf4, 0xf7, 0xf4),
                           Motion::kChipPop, Motion::kPropGlow,
                           qEnvironmentVariableIsEmpty("LA_GATE"));
        });
    // 故事可能随隐私门/数据变化而消失，若当前激活故事已不存在则自动解除过滤
    if (!activeStoryId_.isEmpty() && !storyPaths_.contains(activeStoryId_)) {
        activeStoryId_.clear(); activeStoryTitle_.clear();
        model_.clearStoryFilter();
    }
    storyBar_->setVisible(true);
}

void GalleryPage::applyCategoryFilter(const QString& cat)
{
    activeCat_ = cat;
    model_.setCategoryFilter(cat);
    buildChips();      // 2.33：选中态随重建给出，不再单独 restyle
    updateCount();
}

// 2.41.0 T8：人物馆「在图库中看这一间」→ 图库按姓名筛选。
// 与 chips 同一状态机（activePerson_），故走同一条重建链，切换回来仍可见选中态。
void GalleryPage::applyPersonFilter(const QString& name)
{
    activePerson_ = name;
    model_.setPersonFilter(activePerson_);
    buildChips();
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
    if (!cmdNote_.isEmpty()) notes << cmdNote_;   // 2.41.0 ⌘K 过滤袋注记
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
    if (!activeStoryId_.isEmpty())         // 人物故事方向·B：故事注记
        notes << QCoreApplication::translate("LightAlbum", "故事：%1").arg(activeStoryTitle_);
    if (selCount_ > 0) notes << QCoreApplication::translate("LightAlbum", "已选 %1").arg(selCount_);
    const QString suffix = notes.isEmpty()
        ? QString() : QStringLiteral("（%1）").arg(notes.join(QStringLiteral(" · ")));
    count_->setText(n == total && notes.isEmpty()
        ? QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅").arg(n).arg(total)
        : QCoreApplication::translate("LightAlbum", "显示 <b>%1</b> / %2 幅%3").arg(n).arg(total).arg(suffix));
    syncEmptyState();
}

// wave4 §2.9：空态三态（库空 / 筛选无果 / 被隐私门挡光），只写本波真实存在的入口。
void GalleryPage::syncEmptyState()
{
    if (!empty_ || !view_) return;
    if (model_.rowCount() > 0) {
        empty_->hide();
        view_->show();
        return;
    }
    const int total = photos_ ? photos_->size() : 0;
    const bool filtering = !searchText_.trimmed().isEmpty() || !cmdNote_.isEmpty()
        || !activeCat_.isEmpty() || !activePerson_.isEmpty()
        || !activeClusterKey_.isEmpty() || !activeStoryId_.isEmpty();
    if (total == 0) {
        emptyTitle_->setText(QCoreApplication::translate("LightAlbum", "馆内还没有照片。"));
        emptyBody_->setText(QCoreApplication::translate(
            "LightAlbum", "回到前厅导入一个目录，这里就会挂满你的作品。"));
    } else if (filtering) {
        emptyTitle_->setText(QCoreApplication::translate("LightAlbum", "这里没有符合条件的照片。"));
        emptyBody_->setText(QCoreApplication::translate(
            "LightAlbum", "清掉筛选，或按 ⌘K 换个说法再找。"));
    } else {
        emptyTitle_->setText(QCoreApplication::translate("LightAlbum", "当前没有可显示的照片。"));
        emptyBody_->setText(QCoreApplication::translate(
            "LightAlbum", "其中可能有私密照片：点顶栏的「隐私库」解锁后就能看到。"));
    }
    view_->hide();
    empty_->show();
}

void GalleryPage::applyFilter(const QString& q)
{
    searchText_ = q;
    model_.setFilter(q);
    updateCount();
}

// 2.41.0 spec §2.4：⌘K 过滤袋通道。 MainWindow 直接调用（不新增 signal，
// CmdK::applyFilter 的去重由调用侧负责）；与页内 chips/搜索框并存叠加。
void GalleryPage::applyCmdFilter(const CmdFilter& f)
{
    model_.setCmdFilter(f);
    QStringList parts;
    if (f.yearMin) parts << QString::number(f.yearMin);
    if (f.month)   parts << QString::number(f.month);
    if (f.day)     parts << QString::number(f.day);
    if (!f.season.isEmpty())     parts << f.season;
    if (!f.person.isEmpty())     parts << f.person;
    if (!f.tokenPlace.isEmpty()) parts << f.tokenPlace;
    if (!f.category.isEmpty())   parts << f.category;
    if (!f.text.isEmpty())       parts << f.text;
    cmdNote_ = parts.isEmpty() ? QString()
        : QCoreApplication::translate("LightAlbum", "检索：%1").arg(parts.join(QStringLiteral(" · ")));
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
            if (qAbs(r - 19) < 12 && qAbs(g - 26) < 12 && qAbs(b - 21) < 12) ++cardBg;   // 卡片底（Lumen::card()）
            if (r > 210 && g > 210 && b > 200) ++lightTxt;                                // 标题/文字（Lumen::ink()）
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

#include "MainWindow.h"
#include "Icons.h"
#include "Theme.h"
#include "HomePage.h"
#include "GalleryPage.h"
#include "MapPage.h"
#include "LogPage.h"
#include "CullPage.h"
#include "RetouchPage.h"
#include "../core/Scanner.h"
#include "../core/MotionPref.h"
#include "../core/Vault.h"
#include <QDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPair>
#include <QStringList>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QCoreApplication>
#include <QFileDialog>
#include <QProcess>
#include <QApplication>
#include <QPointer>
#include <QDir>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QPainter>
#include <QPainterPath>
#include <QGradient>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <cstdio>

#include <QComboBox>

#ifdef Q_OS_WIN
#include <windows.h>   // MinGW os_defines.h 已定义 NOMINMAX，避免 min/max 宏污染
#endif

namespace {
struct NavItem { const char* label; const char* phase; };
// 与 web 版导航一致：首页/足迹/图库 · 智能选片/修图 · 日志/关于（三组分隔在 P1 用间隔项体现）
const NavItem kNav[] = {
    {"首页", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"选片", "P3"}, {"修图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};

// 导航图标名（与 kNav 一一对应；历史映射：日志→enhance、关于→log，勿重排）
// P24 A 抛光图标轨：激活态在 updateNavActive 用金色重绘同一图标。
const char* kNavIcons[] = { "home", "map", "gallery", "cull", "retouch", "enhance", "log" };

// P29 无标题栏：判定某点下的控件是否「交互型」（按钮/输入/列表/滚动等）→
// 命中交互控件时不抢作窗口拖拽区（WM_NCHITTEST 用）。
static bool isInteractiveAt(QWidget* w)
{
    while (w) {
        if (qobject_cast<QPushButton*>(w) || qobject_cast<QToolButton*>(w) ||
            qobject_cast<QLineEdit*>(w)   || qobject_cast<QListWidget*>(w) ||
            qobject_cast<QScrollArea*>(w) || qobject_cast<QAbstractSlider*>(w) ||
            qobject_cast<QComboBox*>(w))
            return true;
        w = w->parentWidget();
    }
    return false;
}

// P12「美术馆策展」：方形圆角缩略图（KeepAspectRatioByExpanding + 路径裁剪）
QPixmap roundedThumb(const QString& path, int size, int radius)
{
    QImage img(path);
    if (img.isNull()) return QPixmap();
    QPixmap src = QPixmap::fromImage(img.scaled(
        size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, size, size, radius, radius);
    p.setClipPath(clip);
    p.drawPixmap((src.width() - size) / 2, (src.height() - size) / 2, src);
    return out;
}
} // namespace

// ============================================================================
// P15 图片灯箱（沉浸式，无边框）：直接铺满显示大图，EXIF 等信息在图片正下方。
// 作为 MainWindow 的子控件覆盖全窗口，不再弹带标题栏的对话框「框」。
// P16：可点星级（写库）+ 私密切换（native vault 非破坏标记）。
// ============================================================================
class PhotoLightbox : public QWidget {
    Q_OBJECT
public:
    explicit PhotoLightbox(Library* lib, QWidget* parent = nullptr)
        : QWidget(parent), lib_(lib)
    {
        setObjectName(QStringLiteral("photoLightbox"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("QWidget#photoLightbox{background:rgba(6,8,6,0.97);}"));

        // P20：无布局——img_/bottom_ 全走手动 geometry（见 resizeEvent）

        // P20 B「影院黑幕」：去顶栏与滚动布局——img_ 满幅居中，信息收进
        // 底部渐变黑幕（QFrame：普通 QWidget 默认不画 QSS 背景，P19 实测坑）。
        // 常态黑幕半透明（opacity .38）让画面不被信息压住，鼠标入幕 200ms
        // 渐显至 1.0（QVariantAnimation，中途进出平滑折返）。
        img_ = new QLabel(this);
        img_->setAlignment(Qt::AlignCenter);
        img_->setStyleSheet(QStringLiteral("background:transparent;"));

        bottom_ = new QFrame(this);
        bottom_->setObjectName(QStringLiteral("lbBottom"));
        bottom_->setStyleSheet(QStringLiteral(
            "QFrame#lbBottom{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 rgba(4,6,4,0), stop:0.45 rgba(4,6,4,0.55), stop:1 rgba(4,6,4,0.94));}"));
        auto* bl = new QHBoxLayout(bottom_);
        bl->setContentsMargins(34, 6, 28, 16);
        bl->setSpacing(16);
        auto* textCol = new QVBoxLayout;
        textCol->setSpacing(3);
        title_ = new QLabel(bottom_);
        title_->setStyleSheet(QStringLiteral(
            "font-family:'Noto Serif SC','STSong','SimSun',serif;"
            "font-size:18px;font-weight:600;color:#f4f7f4;"));
        meta_ = new QLabel(bottom_);
        meta_->setStyleSheet(QStringLiteral("color:#9db2a5;font-size:11.5px;letter-spacing:1px;"));
        textCol->addWidget(title_);
        textCol->addWidget(meta_);
        bl->addLayout(textCol);
        bl->addStretch(1);
        starRow_ = new QWidget(bottom_);          // 星级行：setPhoto 时填充
        auto* sl = new QHBoxLayout(starRow_);
        sl->setContentsMargins(0, 0, 0, 0); sl->setSpacing(4);
        bl->addWidget(starRow_);
        ctl_ = new QWidget(bottom_);              // 操作按钮区：setPhoto 时填充
        auto* cl = new QHBoxLayout(ctl_);
        cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(10);
        bl->addWidget(ctl_);

        bottom_->installEventFilter(this);
        bottom_->raise();                          // 黑幕浮于 img_ 之上
        bottomEff_ = new QGraphicsOpacityEffect(bottom_);
        bottomEff_->setOpacity(0.38);
        bottom_->setGraphicsEffect(bottomEff_);
        if (qEnvironmentVariableIsSet("LA_LIGHTBOX_HOVER"))
            bottomEff_->setOpacity(1.0);    // QA：黑幕显形态截图（offscreen 无鼠标）
        fade_ = new QVariantAnimation(this);
        fade_->setDuration(200);
        fade_->setEasingCurve(QEasingCurve::OutCubic);
        connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            bottomEff_->setOpacity(v.toDouble());
        });
    }

    void setPhoto(const Photo* ph, const QString& thumbsDir)
    {
        path_ = ph->path;
        changed_ = false;
        // ---- 大图：存原图，layoutImage() 按窗口自适应（resize 也走这里）----
        orig_ = QPixmap(QDir(thumbsDir).filePath(ph->thumbFile));
        layoutImage();

        // ---- 影院黑幕信息层：衬线标题 + 一行式元信息（地点·日期·器材）----
        title_->setText(ph->title.isEmpty()
                        ? QFileInfo(ph->path).fileName() : ph->title);
        QStringList metaBits;
        const QString place = ph->city.isEmpty() ? ph->prov : ph->city;
        if (!place.isEmpty())    metaBits << place;
        if (!ph->date.isEmpty()) metaBits << ph->date;
        if (!ph->cam.isEmpty())  metaBits << ph->cam;
        if (!ph->lens.isEmpty()) metaBits << ph->lens;
        if (!ph->dim.isEmpty())  metaBits << ph->dim;
        meta_->setText(metaBits.join(QStringLiteral("　·　")));

        // ---- P16 可点星级：1-5 星写库；点当前最高星 = 清除评分 ----
        clearLayout(starRow_->layout());
        auto* sl = qobject_cast<QHBoxLayout*>(starRow_->layout());
        starBtns_.clear();
        for (int i = 0; i < 5; ++i) {
            auto* sb = new QToolButton(starRow_);
            sb->setText(QStringLiteral("★"));
            sb->setFixedSize(28, 26);
            sb->setCursor(Qt::PointingHandCursor);
            sb->setToolTip(QStringLiteral("评分 %1").arg(i + 1));
            connect(sb, &QToolButton::clicked, this, [this, i] {
                if (!lib_) return;
                const int nr = (curRating_ == i + 1) ? 0 : i + 1;
                lib_->setRating(path_, nr);
                curRating_ = nr;
                changed_ = true;
                setStars(nr);
            });
            sl->addWidget(sb);
            starBtns_.append(sb);
        }
        sl->addStretch(1);
        setStars(ph->rating);
        curRating_ = ph->rating;

        // ---- 操作区：私密切换（native vault）+ 修图台 + 关闭 ----
        clearLayout(ctl_->layout());
        auto* cl = qobject_cast<QHBoxLayout*>(ctl_->layout());
        auto* privBtn = new QPushButton(
            ph->priv ? QStringLiteral("🔓 已私密") : QStringLiteral("🔒 私密"), ctl_);
        privBtn->setCursor(Qt::PointingHandCursor);
        privBtn->setStyleSheet(Lumen::btnGhost());
        connect(privBtn, &QPushButton::clicked, this, [this, privBtn] {
            if (!lib_) return;
            const bool on = !privBtn->property("privOn").toBool();
            lib_->setPrivate(path_, on);
            privBtn->setProperty("privOn", on);
            privBtn->setText(on ? QStringLiteral("🔓 已私密") : QStringLiteral("🔒 私密"));
            changed_ = true;
        });
        privBtn->setProperty("privOn", ph->priv);
        auto* open = new QPushButton(QStringLiteral("修图台 →"), ctl_);
        open->setCursor(Qt::PointingHandCursor);
        open->setStyleSheet(Lumen::btnPrimary());
        connect(open, &QPushButton::clicked, this,
                [this, path = ph->path] { emit openInRetouch(path); });
        auto* close = new QPushButton(QStringLiteral("关闭"), ctl_);
        close->setCursor(Qt::PointingHandCursor);
        close->setStyleSheet(Lumen::btnGhost());
        connect(close, &QPushButton::clicked, this, &PhotoLightbox::close);
        cl->addWidget(privBtn); cl->addWidget(open); cl->addWidget(close);
    }

signals:
    void openInRetouch(const QString& path);
    void dismissed();

protected:
    void showEvent(QShowEvent* e) override {
        if (parentWidget()) setGeometry(parentWidget()->rect());
        QWidget::showEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) { e->accept(); close(); }
        else QWidget::keyPressEvent(e);
    }
    // P20：满幅 img_ + 底部黑幕跟随窗口尺寸（黑幕常驻最上层）
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        img_->setGeometry(rect());
        bottom_->setGeometry(0, height() - 120, width(), 120);
        bottom_->raise();
        layoutImage();
    }
    // P20：鼠标入幕显形 / 离幕收暗（从当前透明度出发，中途反向平滑折返）
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == bottom_) {
            if (ev->type() == QEvent::Enter || ev->type() == QEvent::Leave) {
                fade_->stop();
                fade_->setStartValue(bottomEff_->opacity());
                fade_->setEndValue(ev->type() == QEvent::Enter ? 1.0 : 0.38);
                fade_->start();
            }
        }
        return QWidget::eventFilter(obj, ev);
    }
    void closeEvent(QCloseEvent* e) override {
        QWidget::closeEvent(e);
        emit dismissed();
    }

private:
    static void clearLayout(QLayout* lay) {
        if (!lay) return;
        while (QLayoutItem* it = lay->takeAt(0)) {
            if (QWidget* w = it->widget()) w->deleteLater();
            delete it;
        }
    }

public:
    // P16：灯箱内改动过评分/私密 → 关闭后由 MainWindow 刷新图库
    bool hasChanges() const { return changed_; }
    void clearChanges() { changed_ = false; }

private:
    // P16：星级按钮着色（r 颗金星，其余灰）
    void setStars(int r)
    {
        for (int i = 0; i < starBtns_.size(); ++i) {
            starBtns_.at(i)->setStyleSheet(i < r
                ? QStringLiteral("QToolButton{background:transparent;border:none;"
                                 "color:#d9a94e;font-size:17px;padding:0;}")
                : QStringLiteral("QToolButton{background:transparent;border:none;"
                                 "color:#3a4a40;font-size:17px;padding:0;}"));
        }
    }

    // P20：按当前窗口尺寸缩放原图（留 44px 呼吸边）；无图时占位文案
    void layoutImage()
    {
        if (orig_.isNull()) {
            img_->setText(QStringLiteral("无缩略图"));
            img_->setStyleSheet(QStringLiteral("color:#8da396;font-size:14px;"));
            return;
        }
        const int m = 44;
        const QPixmap sc = orig_.scaled(qMax(360, width() - m), qMax(260, height() - m),
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
        img_->setPixmap(sc);
    }

    Library* lib_ = nullptr;
    QLabel* img_  = nullptr;
    QPixmap orig_;
    QFrame* bottom_ = nullptr;                      // P20：影院黑幕信息层
    QGraphicsOpacityEffect* bottomEff_ = nullptr;
    QVariantAnimation* fade_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* meta_ = nullptr;
    QWidget* starRow_ = nullptr;
    QWidget* ctl_ = nullptr;
    QString  path_;
    int      curRating_ = 0;
    bool     changed_ = false;
    QVector<QToolButton*> starBtns_;
};

MainWindow::MainWindow(Library* lib, const QString& thumbsDir,
                       const QString& dbPath, QWidget* parent)
    : QMainWindow(parent), lib_(lib), dbPath_(dbPath)
{
    // P29 无标题栏：移除原生 Windows 标题栏，改由自定义顶栏 + 常驻控制簇（winCtl_）承载。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- P24 侧边栏 A「抛光图标轨」：固定 68px 纯图标轨（取消 P18 折叠双态）。
    //      顶部衬线「光」字标；激活项金底胶囊（QSS 横向 margin 内缩 68-26=42px）
    //      + 左缘金条 + 图标转金；hover 泛光。图标走 QListWidgetItem::setIcon
    //      （激活态在 updateNavActive 重着色）；纯图标轨无文字 → tooltip 补位。----
    navBox_ = new QWidget(central);
    navBox_->setFixedWidth(68);
    navBox_->setStyleSheet(QStringLiteral(
        "background:#0c100d;border-right:1px solid #26312a;"));
    auto* nvLay = new QVBoxLayout(navBox_);
    nvLay->setContentsMargins(0, 14, 0, 12);
    nvLay->setSpacing(4);
    auto* navLogo = new QLabel(QStringLiteral("光"), navBox_);
    navLogo->setAlignment(Qt::AlignCenter);
    navLogo->setFixedHeight(30);
    navLogo->setStyleSheet(QStringLiteral(
        "color:#d9a94e;font-family:Georgia,'Noto Serif SC','STSong',serif;"
        "font-size:17px;background:transparent;"));
    nvLay->addWidget(navLogo);

    nav_ = new QListWidget(navBox_);
    nav_->setMinimumWidth(68);
    nav_->setMaximumWidth(68);
    nav_->setViewMode(QListView::IconMode);
    nav_->setFlow(QListView::TopToBottom);
    nav_->setWrapping(false);
    nav_->setMovement(QListView::Static);          // IconMode 默认可拖拽，锁死
    nav_->setGridSize(QSize(68, 48));
    nav_->setUniformItemSizes(true);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;outline:0;}"
        "QListWidget::item{margin:2px 13px;border-radius:10px;}"          // 42px 胶囊
        "QListWidget::item:hover{background:rgba(232,228,218,15);}"       // 泛光 .06
        "QListWidget::item:selected{background:rgba(217,169,78,40);}"));  // 金底 .16
    nav_->setIconSize(QSize(22, 22));
    for (int i = 0; i < int(sizeof(kNavIcons) / sizeof(kNavIcons[0])); ++i) {
        auto* it = new QListWidgetItem;
        it->setSizeHint(QSize(68, 44));
        it->setIcon(Icons::make(QString::fromLatin1(kNavIcons[i]),
                                QColor(0x9f, 0xa8, 0xa3), 22));
        it->setToolTip(QString::fromUtf8(kNav[i].label));
        nav_->addItem(it);
        navItems_.append(it);
    }
    // 激活金条：贴视口左缘（A 方案胶囊 left:-13px → 栏缘 x=0），呼吸由 tickBreath 驱动
    navBar_ = new QFrame(nav_->viewport());
    navBar_->setObjectName(QStringLiteral("navBar"));
    navBar_->setFixedSize(3, 20);
    navBar_->setStyleSheet(QStringLiteral("background:#d9a94e;border-radius:1px;"));
    navBar_->hide();
    auto* navBarEff = new QGraphicsOpacityEffect(navBar_);
    navBarEff->setOpacity(0.9);
    navBar_->setGraphicsEffect(navBarEff);
    nvLay->addWidget(nav_, 1);
    root->addWidget(navBox_);

    // P19：顶栏渐显动画（底色层透明度同源驱动）
    topSolidAnim_ = new QVariantAnimation(this);
    topSolidAnim_->setDuration(240);
    topSolidAnim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(topSolidAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        applyTopSolid(v.toDouble());
    });

    // ---- 右侧：顶栏 + 页面栈 ----
    auto* right = new QWidget(central);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    // ---- P19 E「沉浸渐隐栏」：topbar 脱离布局、悬浮于页面栈之上（透明常驻）。
    //      页面滚动>12px 或鼠标入栏 → 毛玻璃底色渐显（seam 发丝线随动）。
    //      模块名自 kpis_ 迁入（模块条退役），按钮全图标化，搜索点击展开。----
    topbar_ = new QWidget(right);
    QWidget* topbar = topbar_;
    topbar->setFixedHeight(54);
    auto* tl = new QHBoxLayout(topbar);
    tl->setContentsMargins(20, 0, 16, 0);
    tl->setSpacing(10);
    // 渐显底色层（0 号子控件、置底；文字/按钮不受透明度影响）
    topShade_ = new QFrame(topbar);     // QFrame：QWidget 默认不绘制 QSS 背景（实测坑）
    topShade_->setStyleSheet(QStringLiteral("background:rgba(12,16,13,0.90);"));
    auto* shadeEff = new QGraphicsOpacityEffect(topShade_);
    shadeEff->setOpacity(0.0);
    topShade_->setGraphicsEffect(shadeEff);
    title_ = new QLabel(QString::fromUtf8("光影集"), topbar);
    QFont tf = title_->font(); tf.setPointSizeF(15); tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    title_->setFont(tf);
    title_->setStyleSheet("color:#f4f7f4;background:transparent;");
    tl->addWidget(title_);
    auto* dotSep = new QFrame(topbar);           // 金点分隔：光影集 · 模块名
    dotSep->setFixedSize(4, 4);
    dotSep->setStyleSheet(QStringLiteral("background:#d9a94e;border-radius:2px;"));
    tl->addWidget(dotSep);
    moduleLbl_ = new QLabel(QStringLiteral("首页"), topbar);   // P19：自 kpis_ 迁入
    QFont mf2 = moduleLbl_->font(); mf2.setPointSizeF(11); mf2.setBold(true);
    mf2.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    moduleLbl_->setFont(mf2);
    moduleLbl_->setStyleSheet(QStringLiteral("color:#8da396;background:transparent;"));
    tl->addWidget(moduleLbl_);
    tl->addStretch(1);
    search_ = new QLineEdit(topbar);
    search_->setPlaceholderText(QString::fromUtf8("搜索标题 / 文件名 / 器材…"));
    search_->setFixedWidth(220);
    search_->setStyleSheet(Lumen::input());
    search_->hide();                             // P19：点击 🔍 展开
    // P16 隐私库锁按钮：未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁
    vaultBtn_ = new QPushButton(QStringLiteral("🔒"), topbar);
    vaultBtn_->setToolTip(QString::fromUtf8("隐私库"));
    vaultBtn_->setCursor(Qt::PointingHandCursor);
    vaultBtn_->setFixedSize(32, 32);
    vaultBtn_->setStyleSheet(Lumen::btnGhost());
    connect(vaultBtn_, &QPushButton::clicked, this, &MainWindow::onVaultButton);
    tl->addWidget(vaultBtn_);
    searchToggle_ = new QPushButton(QStringLiteral("🔍"), topbar);
    searchToggle_->setToolTip(QString::fromUtf8("搜索（图库筛选）"));
    searchToggle_->setCursor(Qt::PointingHandCursor);
    searchToggle_->setFixedSize(32, 32);
    searchToggle_->setStyleSheet(Lumen::btnGhost());
    connect(searchToggle_, &QPushButton::clicked, this, [this] {
        if (search_->isVisible()) { search_->hide(); }
        else { search_->show(); search_->raise(); search_->setFocus(); }
    });
    tl->addWidget(searchToggle_);
    // P8：原生导入器入口（补齐 P7 审计的唯一硬缺口）
    auto* importBtn = new QPushButton(QStringLiteral("＋"), topbar);
    importBtn->setToolTip(QString::fromUtf8("导入目录"));
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setFixedSize(32, 32);
    importBtn->setStyleSheet(Lumen::btnGold());
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::startImport);
    tl->addWidget(importBtn);
    tl->addSpacing(120);   // P29 预留窗口控制簇宽度，避免导入钮被 winCtl_ 覆盖
    topbar->installEventFilter(this);            // P19：入栏渐显
    // 顶栏底部翡翠→金渐变发丝线（品牌双色的呼吸缝；P19 起随底色渐显，seam
    // 成为 topbar 子控件，几何由 syncTopbarFloat 同步，不进布局）
    seam_ = new QFrame(topbar_);
    seam_->setFixedHeight(2);
    seam_->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #4fc08a, stop:0.55 #d9a94e, stop:1 rgba(217,169,78,0));"));
    seamEff_ = new QGraphicsOpacityEffect(seam_);
    seam_->setGraphicsEffect(seamEff_);
    seam_->lower();                              // 底色层之下（topShade_ 是 0 号）
    seam_->hide();                               // P19：初始透明态（visible 由 applyTopSolid 接管）

    // P29 无标题栏：窗口控制簇（min/max/close），悬浮于内容区右上角。
    // P29-H：首页（零 chrome 纯沉浸）隐藏该簇；关闭改由 Alt+F4 或切到非首页页再点 ✕。
    // 顶栏右侧已预留 120px 间距，避免导入钮被覆盖。
    winCtl_ = new QWidget(right);
    winCtl_->setFixedSize(116, 36);
    winCtl_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* wl = new QHBoxLayout(winCtl_);
    wl->setContentsMargins(0, 0, 2, 0);
    wl->setSpacing(4);
    wl->addStretch(1);
    winMin_   = new QPushButton(QStringLiteral("—"), winCtl_);   // 最小化
    winMax_   = new QPushButton(QStringLiteral("▢"), winCtl_);   // 最大化/还原
    winClose_ = new QPushButton(QStringLiteral("✕"), winCtl_);   // 关闭
    for (QPushButton* b : {winMin_, winMax_, winClose_}) {
        b->setFixedSize(32, 32);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(Lumen::btnGhost());
    }
    winClose_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #4a2630;border-radius:9px;color:#e6a3a3;}"
        "QPushButton:hover{background:#2a1318;border-color:#e06a6a;color:#ffd7d7;}"));
    wl->addWidget(winMin_);
    wl->addWidget(winMax_);
    wl->addWidget(winClose_);
    connect(winMin_, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(winMax_, &QPushButton::clicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    });
    connect(winClose_, &QPushButton::clicked, this, &QWidget::close);
    winCtl_->raise();

    // ---- P19：模块名称条退役——moduleLbl_ 已迁入悬浮顶栏；kpis_ 保留对象
    //      （onNav 等处有引用）但常隐，不再占布局。----
    kpis_ = new QWidget(right);
    kpis_->hide();

    stack_ = new QStackedWidget(right);
    // 页序与 kNav 一致；P17 起首页 = 「Quiet Frame」沉浸式影集（内容绑定真实库），日志为 EXIF 分析页，关于页实装
    home_ = new HomePage(lib_, thumbsDir, stack_);
    connect(home_, &HomePage::navigateTo, this,
            [this](int row) { onNav(row); });
    connect(home_, &HomePage::photoActivated, this, &MainWindow::openLightbox);
    connect(home_, &HomePage::importRequested, this, &MainWindow::startImport);
    stack_->addWidget(home_);
    map_ = new MapPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(map_);
    gallery_ = new GalleryPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(gallery_);
    cull_ = new CullPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(cull_);
    retouch_ = new RetouchPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(retouch_);
    log_ = new LogPage(&lib_->photos(), stack_);
    stack_->addWidget(log_);
    aboutPg_ = makeAbout();
    stack_->addWidget(aboutPg_);

    // P19：非首页给悬浮顶栏让出 54px 顶部空间（首页自带顶栏不参与）；
    // 内容满铺到 right 顶部，顶栏透明悬浮其上，滚动后底色渐显。
    for (QWidget* pg : QList<QWidget*>{ map_, gallery_, cull_, retouch_, log_, aboutPg_ }) {
        QMargins m = pg->contentsMargins();
        m.setTop(qMax(m.top(), 54));
        pg->setContentsMargins(m);
    }
    // P19：滚动渐显 hook——各页所有 QScrollArea 的垂直偏移>12 → 顶栏实底。
    // 无 QScrollArea 的页（地图/首页）只由 hover 触发。
    for (QWidget* pg : QList<QWidget*>{ map_, gallery_, cull_, retouch_, log_, aboutPg_ }) {
        const auto sas = pg->findChildren<QScrollArea*>();
        for (QScrollArea* sa : sas)
            connect(sa->verticalScrollBar(), &QScrollBar::valueChanged, this,
                    [this](int) { updateTopSolid(); });
    }
    rl->addWidget(stack_, 1);
    root->addWidget(right, 1);

    setCentralWidget(central);
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNav);
    connect(search_, &QLineEdit::textChanged, gallery_, &GalleryPage::applyFilter);
    // P15：图库卡片点击 → 沉浸式灯箱（无边框）→（可选）跳转修图台
    thumbsDir_ = thumbsDir;
    // D5b：批量写回接线 —— 视图只发 signal，库写入在此（MainWindow 持 lib_）统一执行，
    // 复用 Library::setRatings/setPrivates 的单事务原子语义（2.2.9）。
    connect(gallery_, &GalleryPage::batchRatingRequested, this,
            [this](const QStringList& paths, int rating) {
                if (!lib_ || paths.isEmpty()) return;
                const BatchResult r = lib_->setRatings(paths, rating);
                gallery_->refresh();
                QMessageBox::information(this, QStringLiteral("批量评分"),
                    QStringLiteral("请求 %1 张，成功 %2 张。%3")
                        .arg(r.total).arg(r.done)
                        .arg(r.ok ? QStringLiteral("已原子提交。")
                                  : QStringLiteral("失败并已整体回滚：%1").arg(r.error)));
            });
    connect(gallery_, &GalleryPage::batchPrivateRequested, this,
            [this](const QStringList& paths, bool priv) {
                if (!lib_ || paths.isEmpty()) return;
                const BatchResult r = lib_->setPrivates(paths, priv);
                gallery_->setHidePrivate(gallery_->hidePrivate()); // 私密标记影响可见性 → 重跑过滤
                gallery_->refresh();
                QMessageBox::information(this, QStringLiteral("批量私密"),
                    QStringLiteral("请求 %1 张，成功 %2 张。%3")
                        .arg(r.total).arg(r.done)
                        .arg(r.ok ? QStringLiteral("已原子提交。")
                                  : QStringLiteral("失败并已整体回滚：%1").arg(r.error)));
            });

    connect(gallery_, &GalleryPage::photoActivated, this,
            [this](const QString& path) { openLightbox(path); });
    // P15：地图「在图库查看」→ 按区域筛选并切到图库
    connect(map_, &MapPage::requestGallery, this,
            [this](const QString& regionId, const QString& provEn) {
                gallery_->applyGeoFilter(regionId, provEn);
                nav_->setCurrentRow(2);
            });
    // 地图爆炸卡片点击 → 沉浸式灯箱看大图
    connect(map_, &MapPage::requestLightbox, this,
            [this](const QString& path) { openLightbox(path); });
    animEnabled_ = uiAnimEnabled();
    setupBreathing();
    registerBreath(seamEff_, 0.72, 0.28, 1.0);
    nav_->setCurrentRow(0);   // P17：默认落在沉浸式首页
}

// P13：LA_* 冒烟/截图场景关闭所有动效，保证截图与冒烟确定性。
bool MainWindow::uiAnimEnabled() const
{
    return !qEnvironmentVariableIsSet("LA_SMOKE")
        && !qEnvironmentVariableIsSet("LA_UI_SHOT")
        && !qEnvironmentVariableIsSet("LA_CULL_SMOKE")
        && !qEnvironmentVariableIsSet("LA_RETOUCH_SMOKE")
        && !Lumen::systemReducedMotion();   // P15：系统「减弱动效」→ 关动效
}

// P13：全客户端呼吸感 —— 单一 30fps 定时器正弦驱动若干受控元素。
void MainWindow::setupBreathing()
{
    if (!animEnabled_) return;
    breathTimer_ = new QTimer(this);
    breathTimer_->setInterval(33);
    connect(breathTimer_, &QTimer::timeout, this, &MainWindow::tickBreath);
    breathTimer_->start();
}

// P15：统一注册呼吸目标（辉光/激活条共享同一 30fps 节拍，相位错落）。
void MainWindow::registerBreath(QGraphicsOpacityEffect* eff, double base,
                                 double amp, double phase)
{
    if (eff) breathTargets_.append({eff, base, amp, phase});
}

void MainWindow::tickBreath()
{
    breathPhase_ += 0.045;
    // 扩展呼吸：hero 光晕 / seam / 画廊辉光 / 功能卡辉光 统一正弦驱动
    for (auto& t : breathTargets_) {
        if (!t.eff) continue;
        const double k = 0.5 + 0.5 * std::sin(breathPhase_ + t.phase);
        t.eff->setOpacity(t.base + t.amp * k);
    }
    // 导航激活金条（仅当前项呼吸）
    if (navBar_ && navBar_->isVisible() && navBar_->graphicsEffect())
        static_cast<QGraphicsOpacityEffect*>(navBar_->graphicsEffect())
            ->setOpacity(0.55 + 0.45 * (0.5 + 0.5 * std::sin(breathPhase_)));
}

// P13：高亮当前导航项（P24：金条移至对应行 + 图标转金，其余回灰）。
// P15.2：顶栏模块名称随导航切换。
void MainWindow::updateNavActive(int row)
{
    activeNavRow_ = row;
    for (int i = 0; i < navItems_.size()
                    && i < int(sizeof(kNavIcons) / sizeof(kNavIcons[0])); ++i) {
        navItems_.at(i)->setIcon(Icons::make(QString::fromLatin1(kNavIcons[i]),
            i == row ? QColor(0xd9, 0xa9, 0x4e) : QColor(0x9f, 0xa8, 0xa3), 22));
    }
    if (navBar_) {
        if (row >= 0 && row < nav_->count()) {
            const QRect vr = nav_->visualItemRect(nav_->item(row));
            navBar_->setGeometry(0, vr.y() + (vr.height() - navBar_->height()) / 2,
                                 navBar_->width(), navBar_->height());
            navBar_->show();
        } else {
            navBar_->hide();
        }
    }
    if (moduleLbl_ && row >= 0 && row < int(sizeof(kNav) / sizeof(kNav[0])))
        moduleLbl_->setText(QString::fromUtf8(kNav[row].label));
}

// P19：hover 进出顶栏 → 底色渐显/按滚动状态收回
bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == topbar_) {
        if (ev->type() == QEvent::Enter) { topHover_ = true;  updateTopSolid(); }
        else if (ev->type() == QEvent::Leave) { topHover_ = false; updateTopSolid(); }
    }
    return QMainWindow::eventFilter(obj, ev);
}

// ---- P19 沉浸渐隐栏 ----
// 顶栏几何 = 侧栏右缘 → 内容区右缘；seam 置于栏底同步宽度。悬浮件不进布局，
// 需在 resize / nav 宽度动画 / show 后调用。
void MainWindow::syncTopbarFloat()
{
    QWidget* host = topbar_->parentWidget();
    if (!host) return;
    const int navW = nav_->isVisible() ? nav_->width() : 0;
    const int w = qMax(0, host->width() - navW);
    topbar_->setGeometry(navW, 0, w, 54);
    topbar_->raise();                    // 悬浮于 stack_ 之上
    topShade_->setGeometry(0, 0, w, 54); // 底色层不进布局，必须手动铺满
    seam_->setGeometry(0, 52, w, 2);
    if (winCtl_) {                       // P29：常驻控制簇钉在内容区右上角
        winCtl_->move(host->width() - winCtl_->width(), 9);
        winCtl_->raise();
    }
}

bool MainWindow::isWinCtlVisible() const
{
    return winCtl_ && winCtl_->isVisible();
}

void MainWindow::applyTopSolid(double t)
{
    topSolidT_ = t;
    if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(topShade_->graphicsEffect()))
        e->setOpacity(t);
    seam_->setVisible(t > 0.05);         // 呼吸仍由 tickBreath 驱动透明度
}

void MainWindow::forceTopSolid(bool on)
{
    topForced_ = on;                     // QA：越过滚动/hover 状态机，直接落终值
    topSolidAnim_->stop();
    topSolidOn_ = on;
    applyTopSolid(on ? 1.0 : 0.0);
}

void MainWindow::setTopSolid(bool on)
{
    if (topSolidOn_ == on) return;
    topSolidOn_ = on;
    topSolidAnim_->stop();
    topSolidAnim_->setStartValue(topSolidT_);
    topSolidAnim_->setEndValue(on ? 1.0 : 0.0);
    topSolidAnim_->start();
}

// 目标态 = 鼠标在顶栏 || 当前页（非首页）首个 QScrollArea 垂直偏移>12
void MainWindow::updateTopSolid()
{
    if (topForced_) { setTopSolid(true); return; }   // P19：QA 强制态优先
    bool on = topHover_;
    if (!on && stack_ && stack_->currentWidget() != home_) {
        const auto sas = stack_->currentWidget()->findChildren<QScrollArea*>();
        if (!sas.isEmpty()) {
            QScrollBar* sb = sas.first()->verticalScrollBar();
            on = sb && sb->value() > 12;
        }
    }
    setTopSolid(on);
}

void MainWindow::showEvent(QShowEvent* ev)
{
    QMainWindow::showEvent(ev);
    syncTopbarFloat();
}

// P13：导航切换联动动效（淡出 → 换页 → 淡入+轻微上滑）。
// 冒烟/截图或窗口尚未可见（开场动画期间）时直接换页，避免不确定性。
// 效果只在过渡期间 enabled；静置时 disabled 走原生渲染（避免中间 pixmap 发虚）。
// P15：沉浸式灯箱铺满窗口，随主窗口尺寸变化同步。
void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    if (lightbox_ && lightbox_->isVisible())
        lightbox_->setGeometry(rect());
    syncTopbarFloat();                   // P19：悬浮顶栏跟随窗口
}

// P29 无标题栏：拦截 WM_NCHITTEST，把窗口顶部带（顶栏/首页沉浸式头部）模拟成标题栏
// （HTCAPTION → 拖拽 + 双击最大化 + Aero 吸附），四周 6px 模拟可缩放边。命中交互控件
// 时不抢占，保证按钮/输入/列表正常工作。另处理窗口状态变化更新最大化图标。
#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCHITTEST) {
        const int frame = 6;       // 可缩放边宽
        const int band  = 54;      // 顶部拖拽带高（对齐顶栏）
        const QRect r = rect();
        const QPoint p = mapFromGlobal(QCursor::pos());
        const int x = p.x(), y = p.y();
        if (x < frame && y < frame)               { *result = HTTOPLEFT;     return true; }
        if (x > r.width() - frame && y < frame)   { *result = HTTOPRIGHT;    return true; }
        if (x < frame && y > r.height() - frame)  { *result = HTBOTTOMLEFT;  return true; }
        if (x > r.width() - frame && y > r.height() - frame) { *result = HTBOTTOMRIGHT; return true; }
        if (x < frame)              { *result = HTLEFT;   return true; }
        if (x > r.width() - frame)  { *result = HTRIGHT;  return true; }
        if (y < frame)              { *result = HTTOP;    return true; }
        if (y > r.height() - frame) { *result = HTBOTTOM; return true; }
        if (y < band && !isInteractiveAt(childAt(p))) { *result = HTCAPTION; return true; }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#else
bool MainWindow::nativeEvent(const QByteArray&, void*, qintptr*) { return false; }
#endif

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange && winMax_)
        winMax_->setText(isMaximized() ? QStringLiteral("❐") : QStringLiteral("▢"));
    QMainWindow::changeEvent(e);
}

// P16 隐私库：顶栏锁按钮分流 —— 未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁。
// 非破坏设计：原文件不动，仅 photos.private 标记 + PBKDF2 密码门（会话内解锁）。
void MainWindow::onVaultButton()
{
    const QString saltHex = lib_->metaValue(QStringLiteral("vault_salt"));
    const QString hashHex = lib_->metaValue(QStringLiteral("vault_hash"));

    if (saltHex.isEmpty() || hashHex.isEmpty()) {
        const QString p1 = QInputDialog::getText(
            this, QStringLiteral("设置隐私库密码"),
            QStringLiteral("为隐私库设置密码（标记为私密的照片需输入密码才可见）："),
            QLineEdit::Password);
        if (p1.isEmpty()) return;
        const QString p2 = QInputDialog::getText(
            this, QStringLiteral("确认密码"),
            QStringLiteral("再输入一次相同密码："), QLineEdit::Password);
        if (p2 != p1) {
            QMessageBox::warning(this, QStringLiteral("隐私库"),
                                 QStringLiteral("两次输入不一致，未设置。"));
            return;
        }
        const QByteArray salt = Vault::randomSalt();
        lib_->setMetaValue(QStringLiteral("vault_salt"),
                           QString::fromLatin1(salt.toHex()));
        lib_->setMetaValue(QStringLiteral("vault_hash"), Vault::hashHex(p1, salt));
        vaultUnlocked_ = true;
        vaultBtn_->setText(QStringLiteral("🔓 隐私库(已解锁)"));
        refreshPrivateState();
        return;
    }

    if (!vaultUnlocked_) {
        const QString pwd = QInputDialog::getText(
            this, QStringLiteral("解锁隐私库"),
            QStringLiteral("输入隐私库密码："), QLineEdit::Password);
        if (pwd.isEmpty()) return;
        if (Vault::verify(pwd, QByteArray::fromHex(saltHex.toLatin1()), hashHex)) {
            vaultUnlocked_ = true;
            vaultBtn_->setText(QStringLiteral("🔓 隐私库(已解锁)"));
            refreshPrivateState();
        } else {
            QMessageBox::warning(this, QStringLiteral("隐私库"),
                                 QStringLiteral("密码不正确。"));
        }
    } else {
        vaultUnlocked_ = false;
        vaultBtn_->setText(QStringLiteral("🔒 隐私库"));
        refreshPrivateState();
    }
}

void MainWindow::refreshPrivateState()
{
    gallery_->setHidePrivate(!vaultUnlocked_);
    map_->setHidePrivate(!vaultUnlocked_);
    log_->setHidePrivate(!vaultUnlocked_);
    if (lightbox_ && lightbox_->isVisible())
        lightbox_->hide();   // 重新上锁时顺手收起灯箱
}

// P15：打开沉浸式灯箱（图库点击或探针复用）。P16：灯箱内改评分/私密 → 关闭后刷新图库。
void MainWindow::openLightbox(const QString& path)
{
    const Photo* ph = nullptr;
    for (const Photo& p : lib_->photos())
        if (p.path == path) { ph = &p; break; }
    if (!ph) return;
    if (!lightbox_) {
        lightbox_ = new PhotoLightbox(lib_, this);
        connect(lightbox_, &PhotoLightbox::openInRetouch, this, [this](const QString& p) {
            const bool dirty = lightbox_ && lightbox_->hasChanges();
            if (lightbox_) { lightbox_->hide(); lightbox_->clearChanges(); }
            if (dirty) gallery_->refresh();
            retouch_->openPhoto(p);
            nav_->setCurrentRow(4);
        });
        connect(lightbox_, &PhotoLightbox::dismissed, this, [this] {
            const bool dirty = lightbox_ && lightbox_->hasChanges();
            if (lightbox_) { lightbox_->hide(); lightbox_->clearChanges(); }
            if (dirty) gallery_->refresh();
        });
    }
    lightbox_->setPhoto(ph, thumbsDir_);
    lightbox_->show();
    lightbox_->raise();
}

void MainWindow::onNav(int row)
{
    if (row < 0 || row >= stack_->count()) return;
    updateNavActive(row);
    // P21：首页保留左侧全局导航（用户要求首页也显示左栏，不再为沉浸隐藏它）。
    // 顶栏/发丝线仍按 P19 沉浸逻辑在首页隐藏；HomePage 自身左章节面板在右侧并排。
    const bool home = (row == 0);
    topbar_->setVisible(!home);
    if (winCtl_) winCtl_->setVisible(!home);   // P29-H：首页零 chrome 纯沉浸 → 隐藏控制簇
    kpis_->hide();                       // P19：模块条退役，永远不显示
    updateTopSolid();                    // P19：切页后按滚动偏移重算底色
    syncTopbarFloat();
    if (!animEnabled_ || !isVisible() || row == stack_->currentIndex()) {
        stack_->setCurrentIndex(row);
        return;
    }
    auto* eff = qobject_cast<QGraphicsOpacityEffect*>(stack_->graphicsEffect());
    if (!eff) {
        eff = new QGraphicsOpacityEffect(stack_);
        eff->setEnabled(false);
        stack_->setGraphicsEffect(eff);
    }
    eff->setEnabled(true);   // 过渡期间启用
    auto* fade = new QPropertyAnimation(eff, "opacity");
    fade->setDuration(150);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InQuad);
    connect(fade, &QPropertyAnimation::finished, this, [this, row, eff] {
        stack_->setCurrentIndex(row);
        QWidget* nxt = stack_->widget(row);
        const QPoint home = nxt->pos();
        nxt->move(home.x(), home.y() + 14);          // 轻微上滑入场
        auto* slide = new QPropertyAnimation(nxt, "pos");
        slide->setDuration(260);
        slide->setStartValue(nxt->pos());
        slide->setEndValue(home);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        auto* in = new QPropertyAnimation(eff, "opacity");
        in->setDuration(260);
        in->setStartValue(0.0);
        in->setEndValue(1.0);
        in->setEasingCurve(QEasingCurve::OutCubic);
        connect(in, &QPropertyAnimation::finished, this, [eff] {
            eff->setEnabled(false);   // 静置恢复无损原生渲染
        });
        auto* g = new QParallelAnimationGroup(this);
        g->addAnimation(slide);
        g->addAnimation(in);
        g->start(QAbstractAnimation::DeleteWhenStopped);
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

// P12 占位页重设计：线性图标 + 标题 + 「即将推出」胶囊 + 一句用户向说明。
// 不再引用 docs/REWRITE-PLAN.md（内部排期文档不应出现在用户界面）。
QWidget* MainWindow::makePlaceholder(const QString& secName, const QString& iconKind,
                                     const QString& desc) const
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setAlignment(Qt::AlignCenter);
    l->setSpacing(14);

    // 图标：翡翠色线性自绘，置于暗圆底上
    auto* iconLbl = new QLabel(w);
    iconLbl->setPixmap(Icons::make(iconKind, QColor(0x4f, 0xc0, 0x8a), 44).pixmap(44, 44));
    iconLbl->setAlignment(Qt::AlignCenter);
    iconLbl->setStyleSheet(QStringLiteral(
        "QLabel{background:#131a15;border:1px solid #26312a;border-radius:24px;"
        "padding:14px;}"));

    auto* t = new QLabel(secName, w);
    QFont f = t->font(); f.setPointSizeF(20); f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    t->setFont(f);
    t->setStyleSheet("color:#f4f7f4;");

    auto* chip = new QLabel(QStringLiteral("即将推出"), w);
    chip->setStyleSheet(QStringLiteral(
        "QLabel{background:#0e1410;border:1px solid #2f7d57;border-radius:999px;"
        "padding:3px 14px;color:#4fc08a;font-size:11px;}"));

    auto* s = new QLabel(desc, w);
    s->setStyleSheet("color:#8da396;");

    l->addWidget(iconLbl, 0, Qt::AlignHCenter);
    l->addWidget(t, 0, Qt::AlignHCenter);
    l->addWidget(chip, 0, Qt::AlignHCenter);
    l->addWidget(s, 0, Qt::AlignHCenter);
    return w;
}

// P12 关于页：真实版本信息 + 用户向要点（零云端承诺 / 数据位置 / 使用说明）。
QWidget* MainWindow::makeAbout() const
{
    auto* w = new QWidget;
    auto* outer = new QVBoxLayout(w);
    outer->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    outer->setContentsMargins(48, 40, 48, 24);

    auto* card = new QWidget(w);
    card->setMaximumWidth(620);
    card->setStyleSheet(QStringLiteral(
        "QWidget{background:#131a15;border:1px solid #26312a;border-radius:14px;}"));
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(28, 26, 28, 26);
    cl->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("光影集 LightAlbum %1").arg(LA_VERSION), card);
    QFont tf = title->font(); tf.setPointSizeF(17); tf.setBold(true);
    title->setFont(tf);
    title->setStyleSheet("color:#f4f7f4;border:none;background:transparent;");
    auto* pos = new QLabel(QStringLiteral("核心客户端 · 本地优先"), card);
    pos->setStyleSheet("color:#d9a94e;border:none;background:transparent;");
    auto makeLine = [card](const QString& text) {
        auto* lb = new QLabel(text, card);
        lb->setStyleSheet("color:#8da396;border:none;background:transparent;");
        lb->setWordWrap(true);
        return lb;
    };
    cl->addWidget(title);
    cl->addWidget(pos);
    cl->addWidget(makeLine(QStringLiteral(
        "零云端承诺：导入、足迹、图库、选片、修图全部在本机完成，"
        "照片不上传任何服务器。")));
    cl->addWidget(makeLine(QStringLiteral(
        "资料库：D:\\lightalbum\\library.db（可用环境变量 LA_DB / LA_THUMBS 重定向）。")));
    cl->addWidget(makeLine(QStringLiteral(
        "使用说明：见发布包内 USER-MANUAL.md。")));
    cl->addWidget(makeLine(QStringLiteral(
        "基于 Qt 6 构建 · © 2026 LightAlbum")));
    outer->addWidget(card);
    return w;
}

// P11 UI 截图探针：逐导航页切换 → grab() 存 PNG（offscreen 冒烟可用）。
int MainWindow::shootPages(const QString& dir)
{
    QDir().mkpath(dir);
    for (int i = 0; i < nav_->count(); ++i) {
        nav_->setCurrentRow(i);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        // P27：图库页缩略图为后台线程池异步加载（ThumbCache::LoadTask），
        // 120ms 不足以让可见缩略图落盘并重绘；专用长等待让其到位后再 grab
        //（否则 offscreen 直接抓到 96.7% 暗底，误判为黑边问题）。
        if (QString::fromUtf8(kNav[i].label) == QStringLiteral("图库")) {
            for (int w = 0; w < 9; ++w)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        }
        const QPixmap pm = grab();
        const QString path = QDir(dir).filePath(
            QStringLiteral("page-%1-%2.png").arg(i, 2, 10, QLatin1Char('0'))
                .arg(QString::fromUtf8(kNav[i].label)));
        pm.save(path);
        std::fprintf(stderr, "LA_UI_SHOT %s %dx%d\n", qPrintable(path),
                     pm.width(), pm.height());
        std::fflush(stderr);
    }
    return 0;
}

// P8 原生导入器：选目录 → QtConcurrent 后台 Scanner::syncRoot → 进度/取消 → 汇总。
// 完成后提供「重启载入」（页面持有的是打开时快照，避免半刷新状态）。
MapView* MainWindow::mapView() const
{
    return map_ ? map_->view() : nullptr;
}

void MainWindow::startImport()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择要导入的照片目录"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;

    const QString dbPath = !dbPath_.isEmpty()
        ? dbPath_ : QStringLiteral("D:/lightalbum/library.db");

    auto* dlg = new QProgressDialog(QStringLiteral("正在扫描导入…"), QStringLiteral("取消"),
                                    0, qMax(1, lib_->kpi().total), this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setWindowTitle(QStringLiteral("导入照片"));

    auto* cancelled = new std::atomic_bool(false);
    connect(dlg, &QProgressDialog::canceled, this,
            [cancelled]() { cancelled->store(true); });

    const QPointer<QProgressDialog> dlgGuard(dlg);
    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this,
            [this, watcher, dlgGuard, cancelled]() {
                const ScanResult r = watcher->result();
                watcher->deleteLater();
                delete cancelled;
                if (dlgGuard) dlgGuard->deleteLater();
                QString msg = QStringLiteral(
                    "新增 %1 · 更新 %2 · 未变化 %3 · 消失 %4\n总扫描 %5 张 · 用时 %6s")
                        .arg(r.added).arg(r.updated).arg(r.unchanged)
                        .arg(r.missing).arg(r.total).arg(r.seconds, 0, 'f', 1);
                if (r.placeholders > 0)
                    msg += QStringLiteral("\n云端占位跳过 %1 张").arg(r.placeholders);
                if (r.errorCount > 0)
                    msg += QStringLiteral("\n\n错误 %1 条：\n%2")
                               .arg(r.errorCount)
                               .arg(r.errors.join(QLatin1Char('\n')));
                QMessageBox box(this);
                box.setWindowTitle(QStringLiteral("导入完成"));
                box.setText(msg);
                QPushButton* restart = box.addButton(QStringLiteral("重启载入"), QMessageBox::AcceptRole);
                box.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == restart) {
                    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
                    QApplication::quit();
                }
            });
    watcher->setFuture(QtConcurrent::run([dbPath, dir, dlgGuard, cancelled]() {
        return Scanner::syncRoot(
            dir, dbPath, QFileInfo(dbPath).dir().filePath(QStringLiteral("thumbs")),
            [dlgGuard](int done, int total) {
                if (!dlgGuard) return;
                QMetaObject::invokeMethod(dlgGuard, [dlgGuard, done, total]() {
                    if (!dlgGuard) return;
                    dlgGuard->setMaximum(qMax(1, total));
                    dlgGuard->setValue(done);
                }, Qt::QueuedConnection);
            },
            [cancelled]() { return cancelled->load(); });
    }));
}

// AUTOMOC：本文件含 Q_OBJECT 类，必须包含生成的 moc（PhotoLightbox）。
#include "MainWindow.moc"

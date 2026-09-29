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

namespace {
struct NavItem { const char* label; const char* phase; };
// 与 web 版导航一致：首页/足迹/图库 · 智能选片/修图 · 日志/关于（三组分隔在 P1 用间隔项体现）
const NavItem kNav[] = {
    {"首页", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"选片", "P3"}, {"修图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};

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

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // 顶栏：标题 + 关闭
        auto* top = new QWidget(this);
        top->setStyleSheet(QStringLiteral(
            "QWidget{background:rgba(10,14,10,0.55);border-bottom:1px solid #1c261f;}"));
        auto* tl = new QHBoxLayout(top);
        tl->setContentsMargins(18, 10, 18, 10);
        auto* cap = new QLabel(QStringLiteral("作品预览"), top);
        cap->setStyleSheet(QStringLiteral("color:#8da396;font-size:13px;letter-spacing:2px;"));
        tl->addWidget(cap); tl->addStretch(1);
        auto* x = new QPushButton(QStringLiteral("✕ 关闭"), top);
        x->setCursor(Qt::PointingHandCursor);
        x->setStyleSheet(Lumen::btnGhost());
        tl->addWidget(x);
        root->addWidget(top);

        // 滚动内容：大图在上，信息在下
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;border:none;}"));
        auto* content = new QWidget(scroll);
        auto* cv = new QVBoxLayout(content);
        cv->setContentsMargins(28, 26, 28, 30);
        cv->setSpacing(18);
        cv->setAlignment(Qt::AlignHCenter);

        img_ = new QLabel(content);
        img_->setAlignment(Qt::AlignCenter);
        cv->addWidget(img_);

        info_ = new QWidget(content);
        info_->setMaximumWidth(780);
        auto* iv = new QVBoxLayout(info_);
        iv->setContentsMargins(0, 0, 0, 0);
        iv->setSpacing(12);
        cv->addWidget(info_, 0, Qt::AlignHCenter);
        cv->addStretch(1);
        scroll->setWidget(content);
        root->addWidget(scroll, 1);

        connect(x, &QPushButton::clicked, this, &PhotoLightbox::close);
    }

    void setPhoto(const Photo* ph, const QString& thumbsDir)
    {
        path_ = ph->path;
        changed_ = false;
        // ---- 大图（直接显示，按窗口尺寸自适应）----
        const QPixmap pm(QDir(thumbsDir).filePath(ph->thumbFile));
        if (!pm.isNull()) {
            const int pw  = parentWidget() ? parentWidget()->width()  : 1000;
            const int pht = parentWidget() ? parentWidget()->height() : 800;
            const int maxW = qMax(360, pw - 56);
            const int maxH = qMax(260, pht - 360);
            const QPixmap sc = pm.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            img_->setPixmap(sc);
            img_->setFixedSize(sc.size());
        } else {
            img_->setText(QStringLiteral("无缩略图"));
            img_->setStyleSheet(QStringLiteral("color:#8da396;font-size:14px;"));
            img_->setFixedSize(360, 200);
        }

        // ---- 信息（图片正下方）----
        clearLayout(info_->layout());
        auto* iv = qobject_cast<QVBoxLayout*>(info_->layout());
        if (!iv) { iv = new QVBoxLayout(info_); iv->setContentsMargins(0,0,0,0); iv->setSpacing(12); }

        auto* t = new QLabel(ph->title.isEmpty()
                                 ? QFileInfo(ph->path).fileName() : ph->title, info_);
        QFont tf = t->font(); tf.setPointSizeF(17); tf.setBold(true); t->setFont(tf);
        t->setStyleSheet(QStringLiteral("color:#f4f7f4;"));
        t->setWordWrap(true);
        iv->addWidget(t);

        auto addRow = [&](const QString& k, const QString& v) {
            auto* lb = new QLabel(
                QStringLiteral("<b style='color:#8da396;'>%1</b> &nbsp; %2")
                    .arg(k, v.isEmpty() ? QStringLiteral("—") : v), info_);
            lb->setStyleSheet(QStringLiteral("color:#f4f7f4;font-size:12px;"));
            lb->setWordWrap(true);
            iv->addWidget(lb);
        };
        addRow(QStringLiteral("日期"), ph->date);
        addRow(QStringLiteral("器材"), ph->cam);
        addRow(QStringLiteral("镜头"), ph->lens);
        addRow(QStringLiteral("大区 / 省"), ph->region + (ph->prov.isEmpty()
                    ? QString() : QStringLiteral(" / ") + ph->prov));
        addRow(QStringLiteral("城市"), ph->city);
        addRow(QStringLiteral("评分"), ph->rating > 0 ? QString::number(ph->rating) : QStringLiteral("—"));
        addRow(QStringLiteral("尺寸"), ph->dim);
        if (!ph->category.isEmpty())
            addRow(QStringLiteral("分类"), ph->category);
        iv->addStretch(1);

        // ---- P16 可点星级：1-5 星写库；点当前最高星 = 清除评分 ----
        auto* starRow = new QWidget(info_);
        auto* sl = new QHBoxLayout(starRow);
        sl->setContentsMargins(0, 4, 0, 0);
        sl->setSpacing(4);
        auto* starCap = new QLabel(QStringLiteral("评分"), starRow);
        starCap->setStyleSheet(QStringLiteral("color:#8da396;font-size:12px;"));
        sl->addWidget(starCap);
        starBtns_.clear();
        for (int i = 0; i < 5; ++i) {
            auto* sb = new QToolButton(starRow);
            sb->setText(QStringLiteral("★"));
            sb->setFixedSize(28, 26);
            sb->setCursor(Qt::PointingHandCursor);
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
        iv->addWidget(starRow);
        setStars(ph->rating);
        curRating_ = ph->rating;

        // ---- P16 私密切换（native vault：非破坏标记 + 密码门）----
        auto* privBtn = new QPushButton(
            ph->priv ? QStringLiteral("🔓 已设为私密（点击取消）")
                     : QStringLiteral("🔒 设为私密"), info_);
        privBtn->setCursor(Qt::PointingHandCursor);
        privBtn->setStyleSheet(Lumen::btnGhost());
        connect(privBtn, &QPushButton::clicked, this, [this, privBtn] {
            if (!lib_) return;
            const bool on = !privBtn->property("privOn").toBool();
            lib_->setPrivate(path_, on);
            privBtn->setProperty("privOn", on);
            privBtn->setText(on ? QStringLiteral("🔓 已设为私密（点击取消）")
                                : QStringLiteral("🔒 设为私密"));
            changed_ = true;
        });
        privBtn->setProperty("privOn", ph->priv);
        iv->addWidget(privBtn);

        iv->addStretch(1);

        auto* btnRow = new QWidget(info_);
        auto* bl = new QHBoxLayout(btnRow);
        bl->setContentsMargins(0, 8, 0, 0);
        bl->setSpacing(12);
        auto* open = new QPushButton(QStringLiteral("在修图台打开 →"), btnRow);
        open->setCursor(Qt::PointingHandCursor);
        open->setStyleSheet(Lumen::btnPrimary());
        connect(open, &QPushButton::clicked, this,
                [this, path = ph->path] { emit openInRetouch(path); });
        auto* close = new QPushButton(QStringLiteral("关闭"), btnRow);
        close->setCursor(Qt::PointingHandCursor);
        close->setStyleSheet(Lumen::btnGhost());
        connect(close, &QPushButton::clicked, this, &PhotoLightbox::close);
        bl->addWidget(open); bl->addWidget(close); bl->addStretch(1);
        iv->addWidget(btnRow);
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

    Library* lib_ = nullptr;
    QLabel* img_  = nullptr;
    QWidget* info_ = nullptr;
    QString  path_;
    int      curRating_ = 0;
    bool     changed_ = false;
    QVector<QToolButton*> starBtns_;
};

MainWindow::MainWindow(Library* lib, const QString& thumbsDir,
                       const QString& dbPath, QWidget* parent)
    : QMainWindow(parent), lib_(lib), dbPath_(dbPath)
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧图标栏（P12 修复：ListMode 下 68px 宽度挤不下「图标+文字」，
    //      全部标签被省略成 "…"。改 IconMode：图标在上 / 中文标签在下）----
    nav_ = new QListWidget(central);
    nav_->setFixedWidth(68);
    nav_->setViewMode(QListView::IconMode);
    nav_->setFlow(QListView::TopToBottom);
    nav_->setWrapping(false);
    nav_->setMovement(QListView::Static);          // IconMode 默认可拖拽，锁死
    nav_->setGridSize(QSize(68, 58));
    nav_->setUniformItemSizes(true);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#0c100d;border-right:1px solid #26312a;outline:0;padding-top:8px;}"
        "QListWidget::item{color:#8da396;font-size:11px;"
        "  margin:3px 6px;border-radius:10px;}"
        "QListWidget::item:hover{background:#1a231d;color:#f4f7f4;}"
        // 注意：选中态不能加全边框——QSS border 会让 IconMode 文本被省略成 "…"（实测）
        "QListWidget::item:selected{color:#f4f7f4;"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 rgba(79,192,138,88), stop:1 rgba(79,192,138,18));"
        "  border-left:2px solid #d9a94e;}"));
    nav_->setIconSize(QSize(22, 22));
    const char* kIcons[] = {"home", "map", "gallery", "cull", "retouch", "enhance", "log", "about"};
    for (int i = 0; i < int(sizeof(kNav) / sizeof(kNav[0])); ++i) {
        // P12：IconMode 委托文本在选中态被 QSS 省略成 "…"（实测），
        // 改用 setItemWidget 自绘单元格——图标 + 中文标签都是真 QLabel，永不省略。
        auto* it = new QListWidgetItem;
        it->setSizeHint(QSize(56, 52));
        nav_->addItem(it);
        auto* cell = new QWidget;
        auto* cv = new QVBoxLayout(cell);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(4);
        cv->setAlignment(Qt::AlignCenter);
        auto* ic = new QLabel(cell);
        ic->setPixmap(Icons::make(QString::fromLatin1(kIcons[i]),
                                  QColor(0x9f, 0xa8, 0xa3), 22).pixmap(22, 22));
        ic->setAlignment(Qt::AlignCenter);
        ic->setStyleSheet("background:transparent;");
        auto* tx = new QLabel(QString::fromUtf8(kNav[i].label), cell);
        tx->setAlignment(Qt::AlignCenter);
        tx->setStyleSheet("color:#9db2a5;font-size:11px;background:transparent;");
        cv->addWidget(ic);
        cv->addWidget(tx);
        nav_->setItemWidget(it, cell);
        navCells_.append(cell);
        // 激活金条（呼吸由 tickBreath 驱动），默认隐藏
        auto* bar = new QFrame(cell);
        bar->setObjectName(QStringLiteral("navBar"));
        bar->setFixedSize(2, 26);
        bar->setStyleSheet(QStringLiteral("background:#d9a94e;border-radius:1px;"));
        bar->move(3, (52 - 26) / 2);
        bar->hide();
        auto* be = new QGraphicsOpacityEffect(bar);
        be->setOpacity(0.9);
        bar->setGraphicsEffect(be);
        navBars_.append(bar);
    }
    root->addWidget(nav_);

    // ---- 右侧：顶栏 + 页面栈 ----
    auto* right = new QWidget(central);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    topbar_ = new QWidget(right);
    QWidget* topbar = topbar_;
    topbar->setObjectName("topbar");
    topbar->setStyleSheet(QStringLiteral(
        "#topbar{background:#0c100d;}"));
    auto* tl = new QHBoxLayout(topbar);
    tl->setContentsMargins(20, 12, 20, 12);
    title_ = new QLabel(QString::fromUtf8("光影集"), topbar);
    QFont tf = title_->font(); tf.setPointSizeF(15); tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    title_->setFont(tf);
    title_->setStyleSheet("color:#f4f7f4;");
    search_ = new QLineEdit(topbar);
    search_->setPlaceholderText(QString::fromUtf8("搜索标题 / 文件名 / 器材…"));
    search_->setFixedWidth(300);
    search_->setStyleSheet(Lumen::input());
    tl->addWidget(title_);
    tl->addStretch(1);
    // P16 隐私库锁按钮：未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁
    vaultBtn_ = new QPushButton(QStringLiteral("🔒 隐私库"), topbar);
    vaultBtn_->setCursor(Qt::PointingHandCursor);
    vaultBtn_->setStyleSheet(Lumen::btnGhost());
    connect(vaultBtn_, &QPushButton::clicked, this, &MainWindow::onVaultButton);
    tl->addWidget(vaultBtn_);
    // P8：原生导入器入口（补齐 P7 审计的唯一硬缺口）
    auto* importBtn = new QPushButton(QString::fromUtf8("导入目录"), topbar);
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setStyleSheet(Lumen::btnGold());
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::startImport);
    tl->addWidget(importBtn);
    tl->addWidget(search_);
    rl->addWidget(topbar);
    // 顶栏底部翡翠→金渐变发丝线（大胆点：品牌双色的呼吸缝）
    seam_ = new QFrame(right);
    seam_->setFixedHeight(2);
    seam_->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #4fc08a, stop:0.55 #d9a94e, stop:1 rgba(217,169,78,0));"));
    seamEff_ = new QGraphicsOpacityEffect(seam_);
    seam_->setGraphicsEffect(seamEff_);
    rl->addWidget(seam_);

    // ---- 模块名称条（P15.2：替代固定 KPI 条，随导航切换显示对应模块名）----
    kpis_ = new QWidget(right);
    kpis_->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* kl = new QHBoxLayout(kpis_);
    kl->setContentsMargins(20, 8, 20, 8);
    kl->setSpacing(10);
    auto* modBar = new QFrame(kpis_);
    modBar->setFixedSize(4, 18);
    modBar->setStyleSheet(QStringLiteral("background:#d9a94e;border-radius:2px;"));
    kl->addWidget(modBar);
    moduleLbl_ = new QLabel(QStringLiteral("首页"), kpis_);
    QFont mf = moduleLbl_->font(); mf.setPointSizeF(14); mf.setBold(true);
    mf.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    moduleLbl_->setFont(mf);
    moduleLbl_->setStyleSheet(QStringLiteral("color:#f4f7f4;"));
    kl->addWidget(moduleLbl_);
    kl->addStretch(1);
    rl->addWidget(kpis_);

    stack_ = new QStackedWidget(right);
    // 页序与 kNav 一致；P17 起首页 = 「Quiet Frame」沉浸式影集（内容绑定真实库），日志为 EXIF 分析页，关于页实装
    home_ = new HomePage(lib_, thumbsDir, stack_);
    connect(home_, &HomePage::navigateTo, this,
            [this](int row) { onNav(row); });
    connect(home_, &HomePage::photoActivated, this, &MainWindow::openLightbox);
    connect(home_, &HomePage::importRequested, this, &MainWindow::startImport);
    stack_->addWidget(home_);
    map_ = new MapPage(&lib_->photos(), stack_);
    stack_->addWidget(map_);
    gallery_ = new GalleryPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(gallery_);
    cull_ = new CullPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(cull_);
    retouch_ = new RetouchPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(retouch_);
    log_ = new LogPage(&lib_->photos(), stack_);
    stack_->addWidget(log_);
    stack_->addWidget(makeAbout());
    rl->addWidget(stack_, 1);
    root->addWidget(right, 1);

    setCentralWidget(central);
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNav);
    connect(search_, &QLineEdit::textChanged, gallery_, &GalleryPage::applyFilter);
    // P15：图库卡片点击 → 沉浸式灯箱（无边框）→（可选）跳转修图台
    thumbsDir_ = thumbsDir;
    connect(gallery_, &GalleryPage::photoActivated, this,
            [this](const QString& path) { openLightbox(path); });
    // P15：地图「在图库查看」→ 按区域筛选并切到图库
    connect(map_, &MapPage::requestGallery, this,
            [this](const QString& regionId, const QString& provEn) {
                gallery_->applyGeoFilter(regionId, provEn);
                nav_->setCurrentRow(2);
            });
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
    if (activeNavRow_ >= 0 && activeNavRow_ < navBars_.size()) {
        QFrame* bar = navBars_.at(activeNavRow_);
        if (bar->isVisible() && bar->graphicsEffect())
            static_cast<QGraphicsOpacityEffect*>(bar->graphicsEffect())
                ->setOpacity(0.55 + 0.45 * (0.5 + 0.5 * std::sin(breathPhase_)));
    }
}

// P13：高亮当前导航项（显示对应金条），并更新呼吸目标行。
// P15.2：顶栏模块名称随导航切换。
void MainWindow::updateNavActive(int row)
{
    activeNavRow_ = row;
    for (int i = 0; i < navBars_.size(); ++i)
        navBars_.at(i)->setVisible(i == row);
    if (moduleLbl_ && row >= 0 && row < int(sizeof(kNav) / sizeof(kNav[0])))
        moduleLbl_->setText(QString::fromUtf8(kNav[row].label));
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
    // P17：首页隐藏全局 chrome（左栏/顶栏/发丝线/模块条），交给沉浸式首页自身的顶栏导航；
    // 其他页恢复全局 chrome（经左栏「首页」项或各页返回路径均可回到首页）。
    const bool home = (row == 0);
    nav_->setVisible(!home);
    topbar_->setVisible(!home);
    seam_->setVisible(!home);
    kpis_->setVisible(!home);
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

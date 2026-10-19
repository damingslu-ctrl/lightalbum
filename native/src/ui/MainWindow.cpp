#include "MainWindow.h"
#include "Icons.h"
#include "Theme.h"
#include "DarkPalette.h"
#include "LobbyPage.h"      // 2.41.0 spec §2.2：前厅（接替已退役的 HomePage）
#include "GalleryPage.h"
#include "MapPage.h"
#include "LogPage.h"
#include "PeoplePage.h"     // 2.41.0 spec §2.3：人物馆（接管选片导航位）
#include "CmdKPanel.h"      // 2.41.0 spec §2.4：⌘K 检索面板
#include "RetouchPage.h"
#include "VideoLightbox.h"
#include "GazeLightbox.h"   // 2.41.0：原 PhotoLightbox 独立成类（T6 零行为迁出）
#include "../core/Scanner.h"
#include "../core/BrowseScope.h"   // 2.33.0 灯箱翻图作用域 + 隐私门（纯函数，与门禁同源）
#include "../core/ICloudSource.h"
#include "../core/qaExit.h"      // 2.7.0 门禁自退契约（禁裸调 quit）
#include "../core/Classifier.h"   // 方向 C·智能分类：CLIP 子进程门面（T8 净替换外部服务桥）
#include "../core/PersonDetector.h"  // 方向 C·人脸识别（方案A）：faceinfer 子进程编排层
#include "../core/FaceCluster.h"     // 方向 C·人物自动聚类：阈值余弦聚类纯函数
#include "FullCache.h"                // 2.22.0：原图字节预算 LRU（后台解码）
#include "../core/MotionPref.h"
#include "../core/MotionKit.h"        // 2.34.0 动效引擎：预设表/曲线唯一真源
#include "../core/MotionDriver.h"     // 2.34.0 唯一 Qt 驱动器
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
#include <QStyledItemDelegate>   // 2.41.0：本文件自持 NavGroupDelegate（此前间接吃到旧页头）
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMenu>
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

#include <QAction>          // 2.41.0 §2.6：四灯菜单项
#include <QActionGroup>     // 2.41.0 §2.6：四灯互斥
#include <QDate>            // 2.41.0 §2.2：前厅「往年今日」取当日
#include <QShortcut>        // 2.41.0 §2.4：Ctrl+K / Esc

#ifdef Q_OS_WIN
#include <windows.h>   // MinGW os_defines.h 已定义 NOMINMAX，避免 min/max 宏污染
#endif

namespace {
// T12 修复波第 2 轮 FIX-20：AI 成功文案的「%1 张」必须来自实际写回，而不是本轮尝试数。
// 跑前/跑后各数一次已标注张数做差，即本轮真正被写上的张数（一张没中也就不谎报 N 张）。
int countLabeled(const QVector<Photo>& photos, const QString Photo::*field)
{
    int n = 0;
    for (const Photo& p : photos)
        if (!(p.*field).isEmpty()) ++n;
    return n;
}

// 2.33 浮岛分组分割线 delegate：在分组首项（row 3=人物馆, row 5=日志）顶部画细线。
// 不加假 item，保持 QListWidget row 索引与 stack_ 索引一一对应。
class NavGroupDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& ix) const override
    {
        QStyledItemDelegate::paint(p, opt, ix);
        // 分组分割线：row 3（人物组首项）和 row 5（工具组首项）顶部
        if (ix.row() == 3 || ix.row() == 5) {
            p->save();
            QPen sep(QColor(0x1a, 0x23, 0x1d), 1);
            p->setPen(sep);
            const int mx = 16;   // 线不拉满宽度，左右各留 16px（浮岛呼吸感）
            p->drawLine(QPointF(opt.rect.left() + mx, opt.rect.top() + 1),
                        QPointF(opt.rect.right() - mx, opt.rect.top() + 1));
            p->restore();
        }
    }
};
struct NavItem { const char* label; const char* phase; };
// 2.41.0 spec §2.6：前厅/人物馆接管 0/3 两行（HomePage/CullPage 已于 T9 退役）。
// 分组：浏览(前厅/足迹/图库) | 人物·修图 | 工具(日志/关于)。
const NavItem kNav[] = {
    {"前厅", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"人物馆", "P3"}, {"修图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};

// 导航行 → 空间语义键（spec §1.3 六空间；Vault 不经导航，只由顶栏锁钮进）。
// 「跟随空间」模式下 resolveLamp 据此选灯，故这张表也是灯色的路由真源。
const Lumen::Hall kNavHall[] = {
    Lumen::Hall::Lobby,  Lumen::Hall::Gallery, Lumen::Hall::Gallery,
    Lumen::Hall::People, Lumen::Hall::Studio,
    Lumen::Hall::Common, Lumen::Hall::Common,
};

// 导航图标名（与 kNav 一一对应；历史映射：日志→enhance、关于→log，勿重排）
// P24 A 抛光图标轨：激活态在 updateNavActive 用金色重绘同一图标。
const char* kNavIcons[] = { "lobby", "map", "gallery", "people", "retouch", "enhance", "log" };

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
// 2.41.0 T8：暗色弹层菜单皮肤单源（导入菜单与灯钮菜单同一条 QSS）。
// 缺陷②原注：QMenu 是独立顶层弹出窗口，不继承应用级暗色 palette/QSS，落在系统
// 浅色原生菜单上会叠加全局浅色文字 → 白底白字。故逐实例设置（配色复用暗色弹层
// 色系：panel/ink/line/cardHi/emerald/muted，disabled 用 muted 满足 WCAG 对比）。
QString menuCss()
{
    return QString::fromUtf8(
        "QMenu{background:%1;color:%2;border:1px solid %3;}"
        "QMenu::item{background:transparent;color:%2;padding:6px 24px 6px 12px;}"
        "QMenu::item:selected{background:%4;color:%5;}"
        "QMenu::item:disabled{color:%6;}"
        "QMenu::separator{height:1px;background:%3;margin:4px 8px;}")
        .arg(Lumen::hex(Lumen::panel()), Lumen::hex(Lumen::ink()), Lumen::hex(Lumen::line()),
             Lumen::hex(Lumen::cardHi()), Lumen::hex(Lumen::emerald()),
             Lumen::hex(Lumen::muted()));
}
} // namespace


MainWindow::MainWindow(Library* lib, const QString& thumbsDir,
                       const QString& dbPath, QWidget* parent)
    : QMainWindow(parent), lib_(lib), dbPath_(dbPath), thumbsDir_(thumbsDir)
{
    // P29 无标题栏：移除原生 Windows 标题栏，改由自定义顶栏 + 常驻控制簇（winCtl_）承载。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    // 2.41.0 spec §2.6：跨帧三件只在此处造一次。rethemeAll 切灯会重建整帧，若跟着
    // 重造就会有两台 30fps 呼吸定时器 / 两份原图 LRU（灯箱持的是注入指针，换即悬垂）。
    topSolidAnim_ = new QVariantAnimation(this);
    topSolidAnim_->setDuration(240);
    topSolidAnim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(topSolidAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        applyTopSolid(v.toDouble());
    });
    fullCache_ = new FullCache(this);
    animEnabled_ = uiAnimEnabled();
    setupBreathing();

    buildUi();
    nav_->setCurrentRow(0);   // 默认落在前厅
}

// 2.41.0 spec §2.6：整帧建 UI。灯色在构造期就烘进各控件的 QSS（不是运行期可改的
// 属性），所以「切灯」唯一可靠的做法是重跑本函数——见 rethemeAll 的重建序。
// 前提：调用方已备好跨帧三件（topSolidAnim_ / fullCache_ / animEnabled_ + 呼吸定时器）。
void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 2.33 侧边栏「悬浮浮岛」：脱离左缘 12px、圆角 16px 浮岛，三组用
    //      细分割线分隔（浏览/编辑/工具），选中项金色光晕填充 + 图标转金。
    //      取消 P24 的左缘金条（浮岛无「左缘」语义），改用光晕呼吸。----
    auto* navWrap = new QWidget(central);           // 外层容器：12px 边距让浮岛「悬浮」
    navWrap->setFixedWidth(92);                      // 68 浮岛 + 12*2 边距
    navWrap->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* nwLay = new QVBoxLayout(navWrap);
    nwLay->setContentsMargins(Lumen::Space::M, Lumen::Space::L, Lumen::Space::M, Lumen::Space::L);
    nwLay->setSpacing(0);

    navBox_ = new QWidget(navWrap);
    navBox_->setFixedWidth(68);
    navBox_->setStyleSheet(QString::fromUtf8(
        "background:%1;border-radius:%3px;border:1px solid %2;")
        .arg(Lumen::hex(Lumen::panel()),          // §1.7：浮岛底 0a0e0b→PANEL（ΔRGB≤2 回家微亮）
             Lumen::hex(Lumen::cardHi()))        // token 值字面量归引用（色不变）
        .arg(Lumen::Radius::Card));             // T9 C3: 16px->Radius::Card 14
    auto* nvLay = new QVBoxLayout(navBox_);
    nvLay->setContentsMargins(0, Lumen::Space::L, 0, Lumen::Space::M);
    nvLay->setSpacing(4);
    auto* navLogo = new QLabel(QCoreApplication::translate("LightAlbum", "光"), navBox_);
    navLogo->setAlignment(Qt::AlignCenter);
    navLogo->setFixedHeight(30);
    navLogo->setStyleSheet((QString::fromUtf8(
        "color:%1;font-family:Georgia,'Noto Serif SC','STSong',serif;")
        + Lumen::Type::css(Lumen::Type::Tier::Title)   // T8：17px 衬线品牌字归 T2(20px)——衬线只住 T1/T2 §2.3
        + QStringLiteral("background:transparent;"))
        .arg(Lumen::hex(Lumen::gold())));
    nvLay->addWidget(navLogo);

    nav_ = new QListWidget(navBox_);
    nav_->setMinimumWidth(68);
    nav_->setMaximumWidth(68);
    nav_->setViewMode(QListView::IconMode);
    nav_->setFlow(QListView::TopToBottom);
    nav_->setWrapping(false);
    nav_->setMovement(QListView::Static);
    nav_->setGridSize(QSize(68, 44));
    nav_->setUniformItemSizes(true);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setFrameShape(QFrame::NoFrame);
    // 2.33 浮岛选中态：金色光晕（径向渐变）替代纯色胶囊，更有「打光」感
    nav_->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;outline:0;}"
        "QListWidget::item{margin:2px 10px;border-radius:%1px;}"
        "QListWidget::item:hover{background:rgba(79,192,138,18);}"
        "QListWidget::item:selected{background:qradialgradient(cx:0.5,cy:0.5,"
        "radius:0.6,fx:0.5,fy:0.5,stop:0 rgba(217,169,78,60),"
        "stop:1 rgba(217,169,78,8));}")
        .arg(Lumen::Radius::Card));   // T9 C3: 12px->Radius::Card 14（§3.1 归并 12/14/16）
    nav_->setIconSize(QSize(22, 22));
    // 三组：浏览(前厅/足迹/图库) | 人物·修图 | 工具(日志/关于)
    // 分割线由 delegate 在 row 3/5 顶部绘制（不加假 item，保持 row→stack 索引一致）
    for (int i = 0; i < int(sizeof(kNavIcons) / sizeof(kNavIcons[0])); ++i) {
        auto* it = new QListWidgetItem;
        it->setSizeHint(QSize(68, 42));
        it->setIcon(Icons::make(QString::fromLatin1(kNavIcons[i]),
                                 Lumen::muted(), 22));
        it->setToolTip(QString::fromUtf8(kNav[i].label));
        nav_->addItem(it);
        navItems_.append(it);
    }
    // 分组分割线 delegate：在 row 3（选片）和 row 5（日志）顶部画细线
    nav_->setItemDelegate(new NavGroupDelegate(nav_));
    // 浮岛模式取消左缘金条；选中态由 QSS 径向渐变光晕 + 图标转金表达
    navBar_ = nullptr;
    nvLay->addWidget(nav_, 1);
    nwLay->addWidget(navBox_);
    nwLay->addStretch(1);
    root->addWidget(navWrap);

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
    tl->setContentsMargins(Lumen::Space::L, 0, Lumen::Space::L, 0);
    tl->setSpacing(Lumen::Space::M);
    // 渐显底色层（0 号子控件、置底；文字/按钮不受透明度影响）
    topShade_ = new QFrame(topbar);     // QFrame：QWidget 默认不绘制 QSS 背景（实测坑）
    topShade_->setStyleSheet(QStringLiteral("background:rgba(12,16,13,0.90);"));
    auto* shadeEff = new QGraphicsOpacityEffect(topShade_);
    shadeEff->setOpacity(0.0);
    topShade_->setGraphicsEffect(shadeEff);
    title_ = new QLabel(QCoreApplication::translate("LightAlbum", "光影集"), topbar);
    title_->setFont(Lumen::Type::title(false));   // T8：15pt 粗体即 T2 Title 参照本尊（雅黑态；ls 2.0 随档归零 §2.4）
    title_->setStyleSheet(QString::fromUtf8("color:%1;background:transparent;")
        .arg(Lumen::hex(Lumen::ink())));
    tl->addWidget(title_);
    auto* dotSep = new QFrame(topbar);           // 金点分隔：光影集 · 模块名
    dotSep->setFixedSize(4, 4);
    dotSep->setStyleSheet(QString::fromUtf8("background:%1;border-radius:%2px;")
        .arg(Lumen::hex(Lumen::gold()))
        .arg(Lumen::Radius::Micro));   // T9 C3: 2px->Radius::Micro 4（4x4 金点，半径>高/2 截半仍正圆，视觉零位移）
    tl->addWidget(dotSep);
    moduleLbl_ = new QLabel(QCoreApplication::translate("LightAlbum", "前厅"), topbar);   // P19：自 kpis_ 迁入
    moduleLbl_->setFont(Lumen::Type::smallText());   // T8：11pt 粗→T5 9pt（粗体/ls 4.0 随档归一，漂移入 T12 清单）
    moduleLbl_->setStyleSheet(QString::fromUtf8("color:%1;background:transparent;")
        .arg(Lumen::hex(Lumen::muted())));
    tl->addWidget(moduleLbl_);
    tl->addStretch(1);
    // P16 隐私库锁按钮：未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁
    vaultBtn_ = new QPushButton(QStringLiteral("🔒"), topbar);
    vaultBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "隐私库"));
    vaultBtn_->setCursor(Qt::PointingHandCursor);
    vaultBtn_->setFixedSize(32, 32);
    vaultBtn_->setStyleSheet(Lumen::btnGhost());
    connect(vaultBtn_, &QPushButton::clicked, this, &MainWindow::onVaultButton);
    tl->addWidget(vaultBtn_);
    // 2.41.0 spec §2.6：四灯切换钮（Icons 自绘 lamp，InstantPopup 菜单）。
    // 为什么是 QToolButton 而不是第四个 ghost 按钮：4 灯 + 「跟随空间」是互斥选择，
    // 平铺要 5×32px，顶栏右端只预留了 120px 窗控宽度（与导入钮同判例）。
    lampBtn_ = new QToolButton(topbar);
    lampBtn_->setToolTip(QCoreApplication::translate("LightAlbum", "展厅灯光"));
    lampBtn_->setCursor(Qt::PointingHandCursor);
    lampBtn_->setFixedSize(32, 32);
    lampBtn_->setStyleSheet(Lumen::btnGhost(Lumen::gold(), "QToolButton", "0px"));
    lampBtn_->setPopupMode(QToolButton::InstantPopup);
    lampBtn_->setIcon(Icons::make(QStringLiteral("lamp"), Lumen::gold(), 18));
    lampBtn_->setMenu(buildLampMenu());
    tl->addWidget(lampBtn_);
    // 2.41.0 spec §2.6：🔍 不再展开顶栏搜索框（search_ 已删），改指 ⌘K 检索面板。
    searchToggle_ = new QPushButton(QStringLiteral("🔍"), topbar);
    searchToggle_->setToolTip(QCoreApplication::translate("LightAlbum", "检索（Ctrl+K）"));
    searchToggle_->setCursor(Qt::PointingHandCursor);
    searchToggle_->setFixedSize(32, 32);
    searchToggle_->setStyleSheet(Lumen::btnGhost());
    connect(searchToggle_, &QPushButton::clicked, this, [this] {
        if (cmdk_ && cmdk_->isVisible()) closeCmdK(); else openCmdK();
    });
    tl->addWidget(searchToggle_);
    // P8：原生导入器入口（补齐 P7 审计的唯一硬缺口）
    // 2.6.0：改为下拉菜单 —— 「导入目录 / 导入 iCloud 相册」双入口。
    // 为什么用 QToolButton+Menu 而不是两个并排按钮：顶栏右端只有 120px 预留
    // 窗口控制簇宽度（P29），两个 32px 钮会挤进 winCtl_ 底下；下拉零新增宽度。
    auto* importBtn = new QToolButton(topbar);
    importBtn->setText(QStringLiteral("＋"));
    importBtn->setToolTip(QCoreApplication::translate("LightAlbum", "导入照片"));
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setFixedSize(32, 32);
    importBtn->setStyleSheet(Lumen::btnGold());
    importBtn->setPopupMode(QToolButton::InstantPopup);
    importBtn->setMenu(buildImportMenu(importBtn));
    tl->addWidget(importBtn);
    tl->addSpacing(120);   // P29 预留窗口控制簇宽度，避免导入钮被 winCtl_ 覆盖
    topbar->installEventFilter(this);            // P19：入栏渐显
    // 顶栏底部翡翠→金渐变发丝线（品牌双色的呼吸缝；P19 起随底色渐显，seam
    // 成为 topbar 子控件，几何由 syncTopbarFloat 同步，不进布局）
    seam_ = new QFrame(topbar_);
    seam_->setFixedHeight(2);
    seam_->setStyleSheet(QString::fromUtf8(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 %1, stop:0.55 %2, stop:1 rgba(217,169,78,0));")
        .arg(Lumen::hex(Lumen::emerald()), Lumen::hex(Lumen::gold())));   // token 值字面量归引用（色不变）
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
    wl->setContentsMargins(0, 0, Lumen::Space::Xs, 0);
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
    // §1.7：破坏按钮私有盘（4a2630/e6a3a3 + hover 2a1318/e06a6a/ffd7d7）→ btnOutline(CORAL) 单源
    //（DedupDialog T6 同源化；hover 改由 CORAL.lighter(115) 表达，观感更收敛）
    winClose_->setStyleSheet(Lumen::btnOutline(Lumen::CORAL));
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
    // 页序与 kNav 一致；2.41.0 起 0=前厅（今日回看+近期入藏）、3=人物馆（一人一展室）
    lobby_ = new LobbyPage(&lib_->photos(), thumbsDir_, stack_);
    lobby_->setHidePrivate(!vaultUnlocked_);
    // 厅心/横条点击 → 凝视档。scope 不在页里算：MainWindow 复跑同一对纯函数
    // （铁律 20），命中「今日池」才给 Gaze，否则按「近期池」Browse。
    connect(lobby_, &LobbyPage::openGaze, this, [this](const QString& path) {
        const QVector<Photo>& all = lib_->photos();
        const bool hide = !vaultUnlocked_;
        const QVector<const Photo*> hero =
            LobbyPage::onThisDay(all, QDate::currentDate(), hide);
        bool inHero = false;
        for (const Photo* p : hero) if (p->path == path) { inHero = true; break; }
        if (inHero) {
            QStringList scope;
            for (const Photo* p : hero) scope << p->path;
            openGaze(path, scope);
        } else {
            QStringList scope;
            for (const Photo* p : LobbyPage::recent(all, hide)) scope << p->path;
            openLightbox(path, scope);
        }
    });
    connect(lobby_, &LobbyPage::navigateTo, this,
            [this](int row) { onNav(row); });
    connect(lobby_, &LobbyPage::importRequested, this, &MainWindow::startImport);
    // 2.6.0：空态 iCloud 入口 → 与顶栏菜单同一个 importICloud（唯一实现）
    connect(lobby_, &LobbyPage::icloudImportRequested, this, &MainWindow::importICloud);
    // 2.41.0 T8：玻璃指令栏点击 → ⌘K 开馆（与顶栏 🔍 / Ctrl+K 同一实现）
    connect(lobby_, &LobbyPage::commandRequested, this, &MainWindow::openCmdK);
    stack_->addWidget(lobby_);
    map_ = new MapPage(&lib_->photos(), thumbsDir_, stack_);
    stack_->addWidget(map_);
    gallery_ = new GalleryPage(&lib_->photos(), thumbsDir_, stack_);
    stack_->addWidget(gallery_);
    people_ = new PeoplePage(&lib_->photos(), thumbsDir_, stack_);
    people_->setHidePrivate(!vaultUnlocked_);
    // 展室缩略点击 → 凝视档，翻图作用域 = 那一间的全部路径（看得见什么就走什么）
    connect(people_, &PeoplePage::openGaze, this,
            [this](const QString& person, const QString& path) {
                openGaze(path, people_->roomPaths(person));
            });
    // 「在图库中看」→ 图库按姓名筛选并跳页（chips 与人物条同一状态机，非第二份过滤）
    connect(people_, &PeoplePage::filterGallery, this,
            [this](const QString& person) {
                gallery_->applyPersonFilter(person);
                nav_->setCurrentRow(2);
            });
    // 「智能聚类」→ 与图库页按钮同一个 runFaceCluster（唯一实现）
    connect(people_, &PeoplePage::clusteringRequested, this, [this] { runFaceCluster(); });
    stack_->addWidget(people_);
    retouch_ = new RetouchPage(&lib_->photos(), thumbsDir_, stack_);
    stack_->addWidget(retouch_);
    log_ = new LogPage(&lib_->photos(), stack_);
    stack_->addWidget(log_);
    aboutPg_ = makeAbout();
    stack_->addWidget(aboutPg_);

    // 2.41.0 spec §2.4：⌘K 面板是 central 的子控件——随整帧重建拿到新灯 QSS，
    // 不必单独 re-polish。默认隐藏，openCmdK 定位并置顶。
    cmdk_ = new CmdKPanel(&lib_->photos(), central);
    cmdk_->setHidePrivate(!vaultUnlocked_);
    connect(cmdk_, &CmdKPanel::applyFilter, this, [this](const CmdFilter& f) {
        gallery_->applyCmdFilter(f);
        closeCmdK();
        nav_->setCurrentRow(2);
    });
    connect(cmdk_, &CmdKPanel::openRoom, this, [this](const QString& name) {
        people_->focusRoom(name);
        closeCmdK();
        nav_->setCurrentRow(3);
    });
    // 检索即凝视：过滤袋仍只交给 GalleryModel（唯一筛选真源），取它的可见集当作用域
    connect(cmdk_, &CmdKPanel::openGazeFiltered, this, [this](const CmdFilter& f) {
        gallery_->applyCmdFilter(f);
        closeCmdK();
        nav_->setCurrentRow(2);
        const QStringList vis = gallery_->visiblePaths();
        if (!vis.isEmpty()) openGaze(vis.first(), vis);
    });
    connect(cmdk_, &CmdKPanel::lookBackToday, this, [this](const QDate& d) {
        closeCmdK();
        nav_->setCurrentRow(0);
        const QVector<const Photo*> hero =
            LobbyPage::onThisDay(lib_->photos(), d, !vaultUnlocked_);
        if (hero.isEmpty()) return;                 // 空态交前厅自己演，不硬开灯箱
        QStringList scope;
        for (const Photo* p : hero) scope << p->path;
        openGaze(scope.first(), scope);
    });

    // P19：非前厅给悬浮顶栏让出 54px 顶部空间（前厅零 chrome，自带让位不参与）；
    // 内容满铺到 right 顶部，顶栏透明悬浮其上，滚动后底色渐显。
    for (QWidget* pg : QList<QWidget*>{ map_, gallery_, people_, retouch_, log_, aboutPg_ }) {
        QMargins m = pg->contentsMargins();
        m.setTop(qMax(m.top(), 54));
        pg->setContentsMargins(m);
    }
    // P19：滚动渐显 hook——各页所有 QScrollArea 的垂直偏移>12 → 顶栏实底。
    // 无 QScrollArea 的页（地图/前厅）只由 hover 触发。
    for (QWidget* pg : QList<QWidget*>{ map_, gallery_, people_, retouch_, log_, aboutPg_ }) {
        const auto sas = pg->findChildren<QScrollArea*>();
        for (QScrollArea* sa : sas)
            connect(sa->verticalScrollBar(), &QScrollBar::valueChanged, this,
                    [this](int) { updateTopSolid(); });
    }
    rl->addWidget(stack_, 1);
    root->addWidget(right, 1);

    setCentralWidget(central);
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::onNav);
    // 2.33 美术升级：导航选中态图标点亮为金色——选中反馈不应只靠底色胶囊，
    // 图标本体变金才是「这里亮着」的视觉锚点（未选中回本灯雾灰）。
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::restyleNavIcons);
    restyleNavIcons(nav_->currentRow());

    // 2.41.0 spec §2.6：Ctrl+K 全局开馆（父=this，跨帧存活 → 重建后不会重复挂键）；
    // Esc 只在面板及其子树内生效，收起面板而不抢走灯箱/对话框的 Esc。
    auto* kOpen = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(kOpen, &QShortcut::activated, this, [this] {
        if (cmdk_ && cmdk_->isVisible()) closeCmdK(); else openCmdK();
    });
    auto* kEsc = new QShortcut(Qt::Key_Escape, cmdk_);
    kEsc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(kEsc, &QShortcut::activated, this, &MainWindow::closeCmdK);
    cmdk_->hide();
    // P15：图库卡片点击 → 沉浸式灯箱（无边框）→（可选）跳转修图台
    // D5b：批量写回接线 —— 视图只发 signal，库写入在此（MainWindow 持 lib_）统一执行，
    // 复用 Library::setRatings/setPrivates 的单事务原子语义（2.2.9）。
    connect(gallery_, &GalleryPage::batchRatingRequested, this,
            [this](const QStringList& paths, int rating) {
                if (!lib_ || paths.isEmpty()) return;
                const BatchResult r = lib_->setRatings(paths, rating);
                refreshExhibits();
                QMessageBox::information(this, QCoreApplication::translate("LightAlbum", "批量评分"),
                    QCoreApplication::translate("LightAlbum", "请求 %1 张，成功 %2 张。%3")
                        .arg(r.total).arg(r.done)
                        .arg(r.ok ? QCoreApplication::translate("LightAlbum", "已原子提交。")
                                  : QCoreApplication::translate("LightAlbum", "失败并已整体回滚：%1").arg(r.error)));
            });
    connect(gallery_, &GalleryPage::batchPrivateRequested, this,
            [this](const QStringList& paths, bool priv) {
                if (!lib_ || paths.isEmpty()) return;
                const BatchResult r = lib_->setPrivates(paths, priv);
                gallery_->setHidePrivate(gallery_->hidePrivate()); // 私密标记影响可见性 → 重跑过滤
                refreshExhibits();   // 前厅/人物馆的 usable 谓词同样吃 hidePrivate
                QMessageBox::information(this, QCoreApplication::translate("LightAlbum", "批量私密"),
                    QCoreApplication::translate("LightAlbum", "请求 %1 张，成功 %2 张。%3")
                        .arg(r.total).arg(r.done)
                        .arg(r.ok ? QCoreApplication::translate("LightAlbum", "已原子提交。")
                                  : QCoreApplication::translate("LightAlbum", "失败并已整体回滚：%1").arg(r.error)));
            });
    // 方向 C：相似照片去重"标记其余为待删" → 可逆 mark 落库（与评分/私密同构）
    connect(gallery_, &GalleryPage::batchMarkRequested, this,
            [this](const QStringList& paths, const QString& mark) {
                if (!lib_ || paths.isEmpty()) return;
                const BatchResult r = lib_->setMarks(paths, mark);
                refreshExhibits();
                Q_UNUSED(r);
            });
    // 方向 C·智能分类：AI 分类按钮 → 异步跑分类（探测与逐张推理都在 worker 线程；视图不持写能力，只发信号）
    connect(gallery_, &GalleryPage::classifyRequested, this, [this] { runClassify(); });
    // 方向 C·人脸识别（方案A）：识别人物按钮 → 探测 faceinfer.exe → 异步跑检测（视图不持写能力）
    connect(gallery_, &GalleryPage::personDetectRequested, this, [this] { runPersonDetect(); });
    connect(gallery_, &GalleryPage::faceClusterRequested, this, [this] { runFaceCluster(); });
    // 人物 chips 右键重命名 → 落库重命名 + 重建 chips（视图不持写能力，写回在 MainWindow）
    connect(gallery_, &GalleryPage::renamePersonRequested, this,
            [this](const QString& oldName, const QString& newName) {
                if (!lib_) return;
                lib_->renamePerson(oldName, newName);
                refreshExhibits();   // 人物 chips + 人物馆展室名 + ⌘K 人物词典同源，一处改名三处要跟
            });

    // 2.33.0：图库入口把「当前可见集合」交给灯箱作为翻图作用域 —— 用户正在看某个
    //   故事/人物/分类筛选时，←/→ 必须只在这个集合里走。
    connect(gallery_, &GalleryPage::photoActivated, this,
            [this](const QString& path) { openLightbox(path, gallery_->visiblePaths()); });
    connect(map_, &MapPage::requestGallery, this,
            [this](const QString& regionId, const QString& provEn) {
                gallery_->applyGeoFilter(regionId, provEn);
                nav_->setCurrentRow(2);
            });
    // 地图爆炸卡片点击 → 沉浸式灯箱看大图
    connect(map_, &MapPage::requestLightbox, this,
            [this](const QString& path) { openLightbox(path); });
    // 呼吸目标里 seam 发丝线属本帧（rethemeAll 会先清 breathTargets_ 再重建）
    registerBreath(seamEff_, 0.72, 0.28, 1.0);
}

// 2.41.0 spec §1.3：导航行 → 空间语义键。越界回 Gallery（lampFor 的默认档语义一致）。
Lumen::Hall MainWindow::currentHall() const
{
    const int n = int(sizeof(kNavHall) / sizeof(kNavHall[0]));
    const int row = stack_ ? stack_->currentIndex() : activeNavRow_;
    if (row < 0 || row >= n) return Lumen::Hall::Gallery;
    return kNavHall[row];
}

// 2.41.0 spec §2.6：激活项转本灯强调色、其余回本灯次文字色。
// 提为成员是因为这段循环原先在 ctor lambda 与 updateNavActive 各存一份——
// 切灯要求两处一起变色，重复体必须归一（棘轮之外的一条同源纪律）。
void MainWindow::restyleNavIcons(int row)
{
    const QColor active = Lumen::gold();
    const QColor idle   = Lumen::muted();
    const int n = int(sizeof(kNavIcons) / sizeof(kNavIcons[0]));
    for (int i = 0; i < navItems_.size() && i < n; ++i) {
        navItems_[i]->setIcon(Icons::make(QString::fromLatin1(kNavIcons[i]),
                                          i == row ? active : idle, 22));
    }
}

// 2.41.0 spec §2.6：一帧的 chrome 归位（激活态 + 顶栏可见性 + 悬浮几何）。
// onNav 的正常分支与 rethemeAll 共用此函数——rethemeAll 绝不能改走 onNav，
// 否则灯跟随会自我取消（见 rethemeAll 内的 blockSignals 注释）。
void MainWindow::applyRowChrome(int row)
{
    updateNavActive(row);
    // P21→2.41.0：前厅保留左侧全局导航（用户要求进门页也显示左栏，不再为沉浸隐藏它）。
    // 顶栏/发丝线仍按 P19 沉浸逻辑在前厅隐藏；厅心卡自身铺满内容区。
    const bool home = (row == 0);
    if (topbar_) topbar_->setVisible(!home);
    if (winCtl_) winCtl_->setVisible(!home);   // P29-H：前厅零 chrome 纯沉浸 → 隐藏控制簇
    if (kpis_)   kpis_->hide();                // P19：模块条退役，永远不显示
    if (!topbar_) return;
    updateTopSolid();                    // P19：切页后按滚动偏移重算底色
    syncTopbarFloat();
}

// 2.41.0 spec §2.6：切灯唯一路径 = 整帧重建。
// 顺序即安全，四步不可换：
//   1) 记页位 → 2) 摘旧帧句柄（呼吸定时器每 33ms 解引用 seamEff_/navBar_，旧帧
//      deleteLater 后就是悬垂指针 → 退出期 0xC0000005 的老判例）→
//   3) 交出并销毁旧帧 → 4) 建新帧后落回目标页、重算顶栏悬浮态。
// 调色板（QPalette）在 buildUi 之前换：各页构造期读 QSS 时拿到的已是新灯。
void MainWindow::rethemeAll(int row)
{
    if (row < 0) row = stack_ ? stack_->currentIndex() : 0;
    breathTargets_.clear();
    navItems_.clear();
    nav_ = nullptr; navBox_ = nullptr; navBar_ = nullptr;
    topbar_ = nullptr; topShade_ = nullptr; seam_ = nullptr; seamEff_ = nullptr;
    title_ = nullptr; moduleLbl_ = nullptr; kpis_ = nullptr; winCtl_ = nullptr;
    winMin_ = nullptr; winMax_ = nullptr; winClose_ = nullptr;
    searchToggle_ = nullptr; vaultBtn_ = nullptr; lampBtn_ = nullptr;
    lobby_ = nullptr; people_ = nullptr; gallery_ = nullptr; map_ = nullptr;
    retouch_ = nullptr; log_ = nullptr; aboutPg_ = nullptr; cmdk_ = nullptr;

    QWidget* old = takeCentralWidget();
    if (old) { old->hide(); old->deleteLater(); }
    Dark::apply(*qApp, Lumen::activeLamp());
    buildUi();

    // 落栈 + 归位。此处的 nav 必须静音：setCurrentRow → currentRowChanged → onNav
    // 会重入灯跟随分支，把 rethemeAll 刚设好的灯再翻回旧页的空间灯——每次跟随导航
    // 都被自己取消，困死在前厅（LA_UI_SHOT 实测：page-03-人物馆 拍出来是前厅）。
    // 转场动画同样不在此放：旧帧已被摘走，回调抓的是新帧指针，视觉上是二次入场。
    stack_->setCurrentIndex(row);
    nav_->blockSignals(true);
    nav_->setCurrentRow(row);
    nav_->blockSignals(false);
    // 悬浮 chrome 的几何读的是 host（顶栏父件）宽度，而新帧此刻还没被布局过：
    // host 仍是 Qt 给未布局子件的默认 100×30（LA_UI_SHOT 实测：切灯后进足迹/日志页，
    // winCtl 簇落在 x=-16 的左上角）。layout()->activate() 唤不动 QMainWindowLayout
    // 的中央件分配，主窗尺寸又没变、不会有 resizeEvent 补刀——只能等事件循环把布局
    // 落定后再补一次同步。QPointer + parentWidget 比对：期间若又切了一次灯，
    // 抓到的旧 host 已不是当前帧的顶栏父件，直接放弃。
    applyRowChrome(row);
    QWidget* host = topbar_ ? topbar_->parentWidget() : nullptr;
    QPointer<QWidget> hostP(host);
    QTimer::singleShot(0, this, [this, hostP] {
        if (hostP && topbar_ && hostP == topbar_->parentWidget()) syncTopbarFloat();
    });

    // spec §2.6：切灯配 slow 640「上光」包络（kLightFirst：glow 先到峰，画后进）。
    // QPointer 守护：640ms 内若再切一次灯，本帧已被摘走，回调触到旧 widget 即崩。
    if (animEnabled_ && isVisible()) {
        QWidget* host = centralWidget();
        auto* frameEff = new QGraphicsOpacityEffect(host);
        host->setGraphicsEffect(frameEff);
        QPointer<QWidget> hostP(host);
        QPointer<QGraphicsOpacityEffect> effP(frameEff);
        auto* drv = new Motion::Driver(Motion::kLightFirst, !animEnabled_, this);
        connect(drv, &Motion::Driver::valueFor, this, [hostP, effP](const QByteArray& prop, double v) {
            if (!hostP || !effP) return;
            if (prop == Motion::kPropOpacity) effP->setOpacity(v);   // glow/offsetY 不映射到整帧
        });
        connect(drv, &Motion::Driver::finishedSignal, this, [effP, drv] {
            if (effP) { effP->setOpacity(1.0); effP->setEnabled(false); }   // 静置回无损渲染
            drv->deleteLater();
        });
        drv->start();
    }
}

// 2.41.0 spec §2.6：四灯菜单 = 4 个互斥灯项 + 「跟随空间」勾选锁。
// 勾选项的真值只在 aboutToShow 时从 Lumen 读回（activeLamp/lampFollowSpace 是
// 唯一状态源），菜单自己不存第二份状态——否则手挑灯与空间跟随会互相漂移。
QMenu* MainWindow::buildLampMenu()
{
    // 父给灯按钮而非 MainWindow：rethemeAll 整帧重建时旧帧 deleteLater 会连带销毁本菜单，
    // 否则每次切灯泄漏一份（菜单内容随灯变，本就属于这一帧）。
    auto* m = new QMenu(lampBtn_);
    m->setStyleSheet(menuCss());
    auto* grp = new QActionGroup(m);
    grp->setExclusive(true);
    struct LampItem { Lumen::Lamp lamp; const char* name; };
    const LampItem kLamps[] = {
        { Lumen::Lamp::Nocturne, "L1 夜展" }, { Lumen::Lamp::Jade,   "L2 玉照" },
        { Lumen::Lamp::Gekkan,   "L3 素印" }, { Lumen::Lamp::Dusk,   "L4 暮金" },
    };
    for (const LampItem& it : kLamps) {
        QAction* a = m->addAction(Icons::make(QStringLiteral("lamp"),
                                              Lumen::lamp(it.lamp).gold, 16),
                                  QString::fromUtf8(it.name));
        a->setCheckable(true);
        a->setData(int(it.lamp));
        grp->addAction(a);
        connect(a, &QAction::triggered, this, [this, l = it.lamp] {
            if (Lumen::activeLamp() == l && Lumen::lampLocked()) return;   // 已是这盏灯，不重建
            Lumen::setActiveLamp(l);
            Lumen::setLampFollowSpace(false);   // 手挑即脱离跟随（锁语义 = manual）
            rethemeAll();
        });
    }
    m->addSeparator();
    QAction* follow = m->addAction(QCoreApplication::translate("LightAlbum", "跟随空间"));
    follow->setCheckable(true);
    connect(follow, &QAction::triggered, this, [this, follow] {
        const bool on = follow->isChecked();
        Lumen::setLampFollowSpace(on);
        if (on) Lumen::setActiveLamp(Lumen::lampFor(currentHall()));   // 立刻归位本空间灯
        rethemeAll();
    });
    connect(m, &QMenu::aboutToShow, this, [this, m, follow] {
        const bool on = Lumen::lampFollowSpace();
        const int cur = int(Lumen::activeLamp());
        const auto acts = m->actions();
        for (QAction* a : acts) {
            if (!a->isCheckable() || !a->data().isValid()) continue;
            a->setChecked(!on && a->data().toInt() == cur);   // 跟随态下 4 灯全不勾
        }
        follow->setChecked(on);
    });
    return m;
}

// 2.41.0 spec §2.4：⌘K 开馆。面板不自己定位（它是 central 的子控件、随帧重建），
// 由这里按当前窗口给一张居中卡：宽不超过 680，高取内容 sizeHint，落在顶栏带之下。
void MainWindow::openCmdK()
{
    if (!cmdk_) return;
    cmdk_->setHidePrivate(!vaultUnlocked_);   // 锁定态词典不含私密照人物名（spec §3 红线）
    cmdk_->openFor(currentHall());
    const int w = qMin(680, qMax(320, width() - 160));
    const int h = qMax(160, cmdk_->sizeHint().height());
    const int x = (width() - w) / 2;
    const int y = qMax(Lumen::Space::Xxl, (height() - h) / 3);
    cmdk_->setGeometry(x, y, w, h);
    cmdk_->show();
    cmdk_->raise();
    cmdk_->focusInput();
}

void MainWindow::closeCmdK()
{
    if (cmdk_) cmdk_->hide();
}

// 2.41.0 spec §2.6：库写回后的统一刷新汇合。评分/私密/mark 落库即同步内存副本
// （Library::applyBatch 在 COMMIT 之后写 photos_），故各页只需重跑各自的纯函数。
void MainWindow::refreshExhibits()
{
    if (gallery_) gallery_->refresh();
    if (lobby_)   lobby_->refresh();
    if (people_)  people_->refresh();
}

// ---- 2.41.0 §2.6 门禁探针（整帧重建是纯 UI 路径，五道新门都只测纯函数，够不着它）----
// 断言形态：探针返回**可数的结构事实**，不在门禁里做视觉判断。
CmdKPanel* MainWindow::qaCmdK() const { return cmdk_; }

int MainWindow::qaLampMenuActions()
{
    // 4 灯 + 分隔线 + 「跟随空间」= 6 项。少一项即菜单骨架断链。
    if (!lampBtn_ || !lampBtn_->menu()) return -1;
    return lampBtn_->menu()->actions().size();
}

void MainWindow::qaRetheme()
{
    // 直驱一次整帧重建：返回后句柄树应完好（nav_/stack_/lobby_/people_/cmdk_ 非空，
    // 且呼吸目标重新注册过）。门禁据此调用，再用 qaCmdK() 取换新后的面板指针。
    rethemeAll();
}

// P13：LA_* 冒烟/截图场景关闭所有动效，保证截图与冒烟确定性。
bool MainWindow::uiAnimEnabled() const
{
    return !qEnvironmentVariableIsSet("LA_SMOKE")
        && !qEnvironmentVariableIsSet("LA_UI_SHOT")
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
    restyleNavIcons(row);   // 2.41.0 §2.6：图标色取当前灯（此前三处各写一份 0xd9a94e/0x9fa8a3）
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

// 目标态 = 鼠标在顶栏 || 当前页（非前厅）首个 QScrollArea 垂直偏移>12
void MainWindow::updateTopSolid()
{
    if (topForced_) { setTopSolid(true); return; }   // P19：QA 强制态优先
    bool on = topHover_;
    if (!on && stack_ && stack_->currentWidget() != lobby_) {
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
            this, QCoreApplication::translate("LightAlbum", "设置隐私库密码"),
            QCoreApplication::translate("LightAlbum", "为隐私库设置密码（标记为私密的照片需输入密码才可见）："),
            QLineEdit::Password);
        if (p1.isEmpty()) return;
        const QString p2 = QInputDialog::getText(
            this, QCoreApplication::translate("LightAlbum", "确认密码"),
            QCoreApplication::translate("LightAlbum", "再输入一次相同密码："), QLineEdit::Password);
        if (p2 != p1) {
            QMessageBox::warning(this, QCoreApplication::translate("LightAlbum", "隐私库"),
                                 QCoreApplication::translate("LightAlbum", "两次输入不一致，未设置。"));
            return;
        }
        const QByteArray salt = Vault::randomSalt();
        lib_->setMetaValue(QStringLiteral("vault_salt"),
                           QString::fromLatin1(salt.toHex()));
        lib_->setMetaValue(QStringLiteral("vault_hash"), Vault::hashHex(p1, salt));
        vaultUnlocked_ = true;
        vaultBtn_->setText(QCoreApplication::translate("LightAlbum", "🔓 隐私库(已解锁)"));
        refreshPrivateState();
        return;
    }

    if (!vaultUnlocked_) {
        const QString pwd = QInputDialog::getText(
            this, QCoreApplication::translate("LightAlbum", "解锁隐私库"),
            QCoreApplication::translate("LightAlbum", "输入隐私库密码："), QLineEdit::Password);
        if (pwd.isEmpty()) return;
        if (Vault::verify(pwd, QByteArray::fromHex(saltHex.toLatin1()), hashHex)) {
            vaultUnlocked_ = true;
            vaultBtn_->setText(QCoreApplication::translate("LightAlbum", "🔓 隐私库(已解锁)"));
            refreshPrivateState();
        } else {
            QMessageBox::warning(this, QCoreApplication::translate("LightAlbum", "隐私库"),
                                 QCoreApplication::translate("LightAlbum", "密码不正确。"));
        }
    } else {
        vaultUnlocked_ = false;
        vaultBtn_->setText(QCoreApplication::translate("LightAlbum", "🔒 隐私库"));
        refreshPrivateState();
    }
}

void MainWindow::refreshPrivateState()
{
    // 2.41.0 §2.2/§2.3/§2.4：隐私门下发到三处新页面——前厅展陈、人物馆展室、⌘K 人物词典
    //   都直接吃 hidePrivate 谓词；漏一处就等于把私密照挂进展厅。
    const bool hide = !vaultUnlocked_;
    if (gallery_) gallery_->setHidePrivate(hide);
    if (map_) map_->setHidePrivate(hide);
    if (log_) log_->setHidePrivate(hide);
    if (people_) people_->setHidePrivate(hide);   // 2.33.0 补漏原话：选片页此前从不收到隐私门
    if (lobby_) lobby_->setHidePrivate(hide);
    if (cmdk_) cmdk_->setHidePrivate(hide);
    if (lightbox_ && lightbox_->isVisible())
        lightbox_->hide();   // 重新上锁时顺手收起灯箱
}

// 2.41.0 §2.5：灯箱只此一份实例（parent=this → 不随 rethemeAll 整帧重建而销毁）。
GazeLightbox* MainWindow::ensureLightbox()
{
    if (lightbox_) return lightbox_;
    lightbox_ = new GazeLightbox(lib_, this);
    lightbox_->setFullCache(fullCache_);   // 2.22.0：注入原图缓存（灯箱不自有）
    connect(lightbox_, &GazeLightbox::openInRetouch, this, [this](const QString& p) {
        const bool dirty = lightbox_ && lightbox_->hasChanges();
        if (lightbox_) { lightbox_->hide(); lightbox_->clearChanges(); }
        if (dirty) refreshExhibits();
        retouch_->openPhoto(p);
        nav_->setCurrentRow(4);
    });
    // 2.41.0 §2.5：凝视档 S/A/L 即写库（单张 len=1 语义）；空 mark = 撤销。
    // 写库即回 —— 不做批量缓冲；刷新走统一汇合（前厅展陈与人物馆展室同样看得见）。
    connect(lightbox_, &GazeLightbox::markRequested, this,
            [this](const QString& p, const QString& m) {
        lib_->setMark(p, m);
        refreshExhibits();
    });
    connect(lightbox_, &GazeLightbox::dismissed, this, [this](bool dirty) {
        if (lightbox_) { lightbox_->hide(); lightbox_->clearChanges(); }
        if (dirty) refreshExhibits();
    });
    return lightbox_;
}

// P15：打开沉浸式灯箱（图库点击或探针复用）。P16：灯箱内改评分/私密 → 关闭后刷新图库。
// 2.33.0：`scope` 传入调用方的可见集合（图库页在有筛选时），←/→ 就只在这个集合里走；
//   空 = 全库顺序（前厅/地图入口保持原语义）。隐私门据 vaultUnlocked_ 下发。
void MainWindow::openLightbox(const QString& path, const QStringList& scope)
{
    const Photo* ph = lib_->find(path);
    if (!ph) return;
    // ★2.30.0 视频走内嵌解码播放器（VideoLightbox）；照片仍走原 GazeLightbox。
    //   复用同一 openLightbox 入口，故前厅/图库/地图三处打开视频都自动生效。
    if (ph->mediaType == QLatin1String("video")) {
        if (!videoLightbox_) videoLightbox_ = new VideoLightbox(this);
        const QString poster = ph->thumbFile.isEmpty()
            ? QString() : QDir(thumbsDir_).filePath(ph->thumbFile);
        videoLightbox_->openVideo(ph->path, poster, ph->durSec);
        return;
    }
    auto* lb = ensureLightbox();
    lb->setScope(scope);
    lb->setHidePrivate(!vaultUnlocked_);
    // 2.41.0 §2.5：Browse 档零行为。startPath 留空 → 照片装载仍走下一行的 setPhoto，
    //   故这里是**零行为**的档位归位。
    lb->enter(GazeLightbox::Mode::Browse, QString());
    lb->setPhoto(ph, thumbsDir_);
    lb->show();
    lb->raise();
}

// 2.41.0 spec §2.5：凝视档入口。scope = 打开方给的**展陈连续作用域**
//   （前厅厅心九张 / 人物馆一间展室 / ⌘K 检索结果），←/→ 就在其中走。
// setPhoto 先于 enter：enter(Gaze) 用已载入的 orig_ 算主色光晕，且 path_ 已等于
//   startPath → 不会二次装载。视频无凝视档（无主色可取），退回 Browse 入口。
void MainWindow::openGaze(const QString& path, const QStringList& scope)
{
    const Photo* ph = lib_->find(path);
    if (!ph) return;
    if (ph->mediaType == QLatin1String("video")) { openLightbox(path, scope); return; }
    auto* lb = ensureLightbox();
    lb->setScope(scope);
    lb->setHidePrivate(!vaultUnlocked_);
    lb->setPhoto(ph, thumbsDir_);
    lb->enter(GazeLightbox::Mode::Gaze, path);
    lb->show();
    lb->raise();
}

void MainWindow::onNav(int row)
{
    if (row < 0 || row >= stack_->count()) return;
    // 2.41.0 spec §2.6：跟随模式在转场**之前**落灯。切灯 = 整帧重建，旧帧已被摘走，
    //   此时再挂转场动画会让回调命中已销毁控件，故重建后直接 return。重入的 onNav
    //   复查时 activeLamp 已等于目标灯 → 自然落入下方正常分支。
    if (Lumen::lampFollowSpace()) {
        const Lumen::Lamp want = Lumen::lampFor(kNavHall[row]);
        if (want != Lumen::activeLamp()) {
            Lumen::setActiveLamp(want);
            rethemeAll(row);   // 目标行必须显式带上：此刻栈还在旧页
            return;
        }
    }
    applyRowChrome(row);
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
    // 2.34：转场数值全部出自 MotionKit 预设（曲线/时长唯一真源）；
    // 入场位移方向随导航方向 = 视差感（向下切页从下方进，向上切页从上方进）。
    // 2.41.0 spec §3：预设随空间——进前厅=LightFirst(slow 640)、进人物馆=PushDoor(standard 360)、
    //   其余=NavFade。位移与否读预设表（不硬编码预设名）：PushDoor 无 offsetY 键 → 不预位移，
    //   否则收帧后控件停在偏移位回不了家。
    const QByteArray presetName = row == 0 ? Motion::kLightFirst
                                  : row == 3 ? Motion::kPushDoor
                                  : Motion::kNavFade;
    const Motion::Preset& p = Motion::preset(presetName);
    bool hasOffsetY = false;
    for (const auto& k : p.keys) hasOffsetY |= (k.prop == Motion::kPropOffsetY);
    const int prevRow = stack_->currentIndex();
    const double amp = (row > prevRow) ? 14.0 : -14.0;
    auto* fade = new QPropertyAnimation(eff, "opacity");
    fade->setDuration(int(p.durationMs * 0.35));
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InQuad);   // 与预设 exit 键同曲线（引擎表即用 InQuad）
    connect(fade, &QPropertyAnimation::finished, this,
            [this, row, eff, amp, hasOffsetY, presetName] {
        stack_->setCurrentIndex(row);
        QWidget* nxt = stack_->widget(row);
        const QPoint home = nxt->pos();
        if (hasOffsetY) nxt->move(home.x(), home.y() + amp);
        auto* drv = new Motion::Driver(presetName, !animEnabled_, this);
        connect(drv, &Motion::Driver::valueFor, this,
                [nxt, home, eff, hasOffsetY](const QByteArray& prop, double v) {
            if (prop == Motion::kPropOffsetY) {
                if (hasOffsetY) nxt->move(home.x(), int(home.y() + v));
            } else if (prop == Motion::kPropOpacity) eff->setOpacity(v);
        });
        connect(drv, &Motion::Driver::finishedSignal, this, [eff, drv] {
            eff->setEnabled(false);   // 静置恢复无损原生渲染
            drv->deleteLater();
        });
        drv->start();
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
    l->setSpacing(Lumen::Space::L);

    // 图标：翡翠色线性自绘，置于暗圆底上
    auto* iconLbl = new QLabel(w);
    iconLbl->setPixmap(Icons::make(iconKind, QColor(0x4f, 0xc0, 0x8a), 44).pixmap(44, 44));
    iconLbl->setAlignment(Qt::AlignCenter);
    iconLbl->setStyleSheet(QString::fromUtf8(
        "QLabel{background:%1;border:1px solid %2;border-radius:%3px;"
        "padding:14px;}")
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()))   // token 值字面量归引用（色不变）
        .arg(Lumen::Radius::Card));   // T9 C3: 24px->Radius::Card 14（§3.1 违反例裁定：MainWindow 24px 徽章→r-card）

    auto* t = new QLabel(secName, w);
    t->setFont(Lumen::Type::title(false));   // T8修订：占位题保持 sans（YaHei 15pt 粗体，同 LogPage:176）；Display/serif 仅首页 hero（§2.1）
    t->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::ink())));

    auto* chip = new QLabel(QCoreApplication::translate("LightAlbum", "即将推出"), w);
    chip->setStyleSheet(QString::fromUtf8(   // chip 工厂未出生（已知缺口）：本地 token-only 配方，逐值=INSET/EMERALD_DK/EMERALD
        "QLabel{background:%1;border:1px solid %2;border-radius:999px;"
        "padding:3px 14px;color:%3;}")
        .arg(Lumen::hex(Lumen::inset()), Lumen::hex(Lumen::EMERALD_DK),
             Lumen::hex(Lumen::emerald()))
        + Lumen::Type::css(Lumen::Type::Tier::Small));   // T8：11px→T5(12px)

    auto* s = new QLabel(desc, w);
    s->setStyleSheet(QString::fromUtf8("color:%1;").arg(Lumen::hex(Lumen::muted())));

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
    outer->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::Xxl, Lumen::Space::Xxl, Lumen::Space::Xl);

    auto* card = new QWidget(w);
    card->setMaximumWidth(620);
    card->setStyleSheet(QString::fromUtf8(
        "QWidget{background:%1;border:1px solid %2;border-radius:14px;}")
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line())));   // token 值字面量归引用（色不变）
    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(Lumen::Space::Xl, Lumen::Space::Xl, Lumen::Space::Xl, Lumen::Space::Xl);
    cl->setSpacing(12);

    auto* title = new QLabel(QCoreApplication::translate("LightAlbum", "光影集 LightAlbum %1").arg(LA_VERSION), card);
    title->setFont(Lumen::Type::title(false));   // T8：关于页主标题 17pt→T2 15pt 雅黑粗体（-2pt 入 T12 清单）
    title->setStyleSheet(QString::fromUtf8("color:%1;border:none;background:transparent;")
        .arg(Lumen::hex(Lumen::ink())));
    auto* pos = new QLabel(QCoreApplication::translate("LightAlbum", "核心客户端 · 本地优先"), card);
    pos->setStyleSheet(QString::fromUtf8("color:%1;border:none;background:transparent;")
        .arg(Lumen::hex(Lumen::gold())));
    auto makeLine = [card](const QString& text) {
        auto* lb = new QLabel(text, card);
        lb->setStyleSheet(QString::fromUtf8("color:%1;border:none;background:transparent;")
            .arg(Lumen::hex(Lumen::muted())));
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
    // 取证尺寸钉死：offscreen 虚拟屏只有 800x800，开场 showMaximized 后各页会被栈的
    // 最小宽推到不同尺寸（前厅 800、其余 1369），同一批快照互相不可比，且窄到低于
    // 布局最小宽时 QHBoxLayout 会压扁定尺寸子件——把「布局不够宽」误读成「新页坏图」。
    // 最大化态下 resize() 被 WM 几何忽略，必须先 showNormal 再钉尺寸。
    if (isMaximized()) showNormal();
    resize(1440, 900);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
    for (int i = 0; i < nav_->count(); ++i) {
        nav_->setCurrentRow(i);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 120);
        // P27：缩略图为后台线程池异步加载（ThumbCache::LoadTask），
        // 120ms 不足以让可见缩略图落盘并重绘；专用长等待让其到位后再 grab
        //（否则 offscreen 直接抓到 96.7% 暗底，误判为黑边问题）。
        // 2.41.0 §2.2/§2.3：前厅厅心/近期条与人物馆展室同样吃 ThumbCache，
        // 不一起等就会把「缩略图还没到」误判成「新页黑屏」。
        const QString label = QString::fromUtf8(kNav[i].label);
        if (label == QStringLiteral("图库") || label == QStringLiteral("前厅")
            || label == QStringLiteral("人物馆")) {
            for (int w = 0; w < 9; ++w)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        }
        const QPixmap pm = grab();
        const QString path = QDir(dir).filePath(
            QStringLiteral("page-%1-%2.png").arg(i, 2, 10, QLatin1Char('0'))
                .arg(label));
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

// 2.6.0：导入下拉菜单。普通目录沿用 P8 原生导入器；iCloud 走自动探测。
//
// ★ 懒构建（性能）：`detectRoot()` 会对候选目录做 `entryInfoList`（本机实测
// iCloud 目录 6778 个 QFileInfo）。若在顶栏构造期同步调用，等于把这笔 IO
// 压进 UI 启动主线程 —— LA_SCALE_PERF 的 firstMs 会从 ~40 抬到 ~100+。
// 故构造期只建骨架（零 IO），首次 `aboutToShow`（用户点开菜单、已在等）时才探测。
QMenu* MainWindow::buildImportMenu(QWidget* owner)
{
    // 父给持有它的按钮而非 MainWindow：rethemeAll 整帧重建时旧帧 deleteLater 连带销毁，
    // 否则每次切灯泄漏一份（与灯钮菜单同一条纪律）。
    QMenu* m = new QMenu(owner);
    m->setStyleSheet(menuCss());   // 2.41.0 T8：菜单皮肤归厂（与灯钮菜单同源）
    QAction* aDir = m->addAction(QCoreApplication::translate("LightAlbum", "导入目录…"));
    connect(aDir, &QAction::triggered, this, &MainWindow::startImport);

    QAction* aCloud = m->addAction(QCoreApplication::translate("LightAlbum", "导入 iCloud 相册"));
    connect(aCloud, &QAction::triggered, this, &MainWindow::importICloud);

    m->addSeparator();
    // 状态行占位文案，aboutToShow 时被真实探测结果替换
    QAction* aStatus = m->addAction(QCoreApplication::translate("LightAlbum", "正在检测 iCloud…"));
    aStatus->setEnabled(false);
    aCloud->setEnabled(false);          // 探测前先禁用，命中后才开

    connect(m, &QMenu::aboutToShow, this, [this, m, aCloud, aStatus]() {
        int n = 0;
        const QString root = ICloudSource::detectRoot(QString(), &n);
        if (root.isEmpty()) {
            // 探测不到就**禁用**并说明原因——绝不静默导入 0 张后报「完成」。
            aCloud->setEnabled(false);
            aStatus->setText(QCoreApplication::translate("LightAlbum", "未检测到 iCloud 本地相册（%1 个文件待同步）")
                                 .arg(n));
        } else {
            // 命中就报真实文件数，让用户点之前就知道规模。
            aCloud->setEnabled(true);
            aCloud->setToolTip(QCoreApplication::translate("LightAlbum", "iCloud 本地目录\n%1\n共 %2 个文件")
                                   .arg(root).arg(n));
            aStatus->setText(QCoreApplication::translate("LightAlbum", "已检测：%1 个文件").arg(n));
        }
    });
    return m;
}

// 2.6.0：导入 iCloud 相册 —— 探测根目录 → 确认 → 复用 syncRoot 全量通道。
// 写入路径**不新建**：与「导入目录」共用 runImportInto()，保证缩略图缓存键、
// 增量规则（size+mtime 未变则跳过）、missing 标记语义完全一致。
void MainWindow::importICloud()
{
    int n = 0;
    const QString root = ICloudSource::detectRoot(QString(), &n);
    if (root.isEmpty()) {
        QMessageBox box(this);
        box.setWindowTitle(QCoreApplication::translate("LightAlbum", "导入 iCloud 相册"));
        box.setText(QStringLiteral(
            "未检测到 iCloud 本地相册目录。\n\n"
            "请确认 iCloud for Windows 已安装并开启「照片」同步；"
            "若相册存放在非默认位置，可改用「导入目录…」手动选择。"));
        box.setIcon(QMessageBox::Information);
        box.exec();
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(QCoreApplication::translate("LightAlbum", "导入 iCloud 相册"));
    box.setText(QStringLiteral(
        "将导入 iCloud 本地相册：\n%1\n\n"
        "共扫描到 %2 个文件。\n\n"
        "云端未下载的占位文件会自动跳过；在 iCloud 里下载完成后重跑一次即可增量补齐。")
        .arg(root).arg(n));
    QPushButton* go = box.addButton(QCoreApplication::translate("LightAlbum", "开始导入"), QMessageBox::AcceptRole);
    QPushButton* pick = box.addButton(QCoreApplication::translate("LightAlbum", "改用手动选择…"),
                                      QMessageBox::ActionRole);
    box.addButton(QCoreApplication::translate("LightAlbum", "取消"), QMessageBox::RejectRole);
    box.setDefaultButton(go);
    box.exec();
    if (box.clickedButton() == go) {
        runImportInto(root, QCoreApplication::translate("LightAlbum", "导入 iCloud 相册"));
    } else if (box.clickedButton() == pick) {
        startImport();          // 手动选目录：复用 P8 原生路径
    }
}

void MainWindow::startImport()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QCoreApplication::translate("LightAlbum", "选择要导入的照片目录"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    runImportInto(dir, QCoreApplication::translate("LightAlbum", "导入照片"));
}

// 2.6.0：把 P8 的导入主体从 startImport 抽出，两个入口共用（唯一写入路径）。
void MainWindow::runImportInto(const QString& dir, const QString& title)
{
    const QString dbPath = !dbPath_.isEmpty()
        ? dbPath_ : QStringLiteral("D:/lightalbum/library.db");

    auto* dlg = new QProgressDialog(QCoreApplication::translate("LightAlbum", "正在扫描导入…"), QCoreApplication::translate("LightAlbum", "取消"),
                                    0, qMax(1, lib_->kpi().total), this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setWindowTitle(title);

    auto* cancelled = new std::atomic_bool(false);
    connect(dlg, &QProgressDialog::canceled, this,
            [cancelled]() { cancelled->store(true); });

    const QPointer<QProgressDialog> dlgGuard(dlg);
    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this,
            [this, watcher, dlgGuard, cancelled, title]() {
                const ScanResult r = watcher->result();
                watcher->deleteLater();
                delete cancelled;
                if (dlgGuard) dlgGuard->deleteLater();
                // 2.41.0 §2.2：馆务语感——「新增」是数据库词，进馆的说法是「入藏」。
                QString msg = QStringLiteral(
                    "%1 幅入藏 · 更新 %2 · 未变化 %3 · 消失 %4\n总扫描 %5 张 · 用时 %6s")
                        .arg(r.added).arg(r.updated).arg(r.unchanged)
                        .arg(r.missing).arg(r.total).arg(r.seconds, 0, 'f', 1);
                if (r.placeholders > 0)
                    msg += QCoreApplication::translate("LightAlbum", "\n云端占位跳过 %1 张").arg(r.placeholders);
                if (r.errorCount > 0)
                    msg += QCoreApplication::translate("LightAlbum", "\n\n错误 %1 条：\n%2")
                               .arg(r.errorCount)
                               .arg(r.errors.join(QLatin1Char('\n')));
                QMessageBox box(this);
                box.setWindowTitle(title + QCoreApplication::translate("LightAlbum", " · 完成"));
                box.setText(msg);
                QPushButton* restart = box.addButton(QCoreApplication::translate("LightAlbum", "重启载入"), QMessageBox::AcceptRole);
                box.addButton(QCoreApplication::translate("LightAlbum", "稍后"), QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == restart) {
                    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
                    QaExit::requestExit(0);   // 2.7.0：收敛到自退契约
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

// 方向 C·智能分类：收到图库「AI 分类」信号后，异步跑分类
// （QtConcurrent 不阻塞 GUI），带进度对话框与取消；完成后重建 chips。
// 引擎就绪探测 + 逐张分类全部在 worker 线程：AiSubprocessRunner 禁 GUI 线程调用
//（GUI 线程 = Q_ASSERT + Busy），故此处绝不在 GUI 线程 probe（旧实现在 GUI 线程
// 同步 QNAM 探活无害，换子进程底座后即为崩溃点——T4b runPersonDetect 判例，R-20）。
// worker 返回码 + engineMissing 旗标做三态判别（R-20）：「引擎未就绪」/「跑完」/「中断」，
// UI 只在「引擎未就绪」弹新引擎文案。
// 单写者（core/AiWriter.h）：worker 只碰快照与子进程，落库经 AiWriter 恒回 GUI 线程执行。
void MainWindow::runClassify()
{
    if (!lib_) return;
    // 1) 统计待分类数（未分类且已有缩略图；GUI 线程纯内存读快照，可）
    int pending = 0;
    for (const Photo& p : lib_->photos())
        if (p.category.isEmpty() && !p.thumbFile.isEmpty()) ++pending;
    if (pending == 0) {
        QMessageBox::information(this,
            QCoreApplication::translate("LightAlbum", "AI 分类"),
            QCoreApplication::translate("LightAlbum", "库内照片均已分类，无需再跑。"));
        return;
    }
    // FIX-20：跑前基线（已分类张数），finished 里做差得到本轮实际写回数。
    const int labeledBefore = countLabeled(lib_->photos(), &Photo::category);
    // 2) 进度对话框 + 后台跑（探测与分类全在 worker 线程）
    auto* dlg = new QProgressDialog(
        QCoreApplication::translate("LightAlbum", "AI 正在分类…"),
        QCoreApplication::translate("LightAlbum", "取消"), 0, pending, this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    auto* watcher = new QFutureWatcher<bool>(this);
    auto* cancelled = new std::atomic<bool>(false);
    auto* engineMissing = new std::atomic<bool>(false);   // R-20 三态旗标（worker 写、GUI 线程读）
    connect(dlg, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, dlg, watcher, cancelled, engineMissing, labeledBefore] {
                dlg->close();
                gallery_->refresh();   // 重建分类 chips + 计数（新分类生效）
                const bool ok = watcher->result();
                QString msg;
                if (engineMissing->load()) {
                    // 文案纪律（T12 修复波）：三条「AI 引擎未就绪」串一律不点名
                    // native/fetch_ai.ps1 / faceinfer / clipinfer —— 脚本与构建产物不在
                    // 发行包里（release.ps1 的 Copy-Set 目标无 .ps1，staging 树内 *.ps1=0），
                    // 对拿到 zip 的终端用户那是不可执行的指令。诚实版只说缺什么、
                    // 数据不出机，并把用户指向他真能点的按钮（各自尾部的「再点「…」」）。
                    msg = QCoreApplication::translate("LightAlbum",
                        "AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「AI 分类」。照片仅在本机处理，绝不外传。");
                } else {
                    // FIX-20：干净轮次报「本轮实际写回数」（跑前后已分类数做差），
                    // 不再把本轮尝试数 pending 当作写回数。
                    msg = ok ? QCoreApplication::translate("LightAlbum", "已为 %1 张照片写入场景分类。")
                                .arg(qMax(0, countLabeled(lib_->photos(), &Photo::category) - labeledBefore))
                             : QCoreApplication::translate("LightAlbum", "分类过程中断（可能已部分写入）。");
                }
                QMessageBox::information(this,
                    QCoreApplication::translate("LightAlbum", "AI 分类"), msg);
                watcher->deleteLater();
                delete cancelled;
                delete engineMissing;
                dlg->deleteLater();
            });
    const auto prog = [dlg](int done, int, const QString&) {
        QMetaObject::invokeMethod(dlg, "setValue", Qt::QueuedConnection, Q_ARG(int, done));
    };
    const auto cancelPred = [cancelled] { return cancelled->load(); };
    const QString td = thumbsDir_;
    // GUI 线程取快照：worker 只读此副本，绝不在 worker 线程碰 lib_->photos()
    //（Library 无锁，GalleryModel 以引用读 photos_）；落库经 AiWriter 恒回 GUI 线程。
    const QVector<Photo> snap = lib_->photos();
    watcher->setFuture(QtConcurrent::run([this, snap, td, prog, cancelPred, engineMissing] {
        // 探测在 worker 线程（子进程 runner 禁 GUI 线程调用）：未就绪 → 置旗标 + 返回
        // false → finished 弹「AI 引擎未就绪」文案。classify 内部亦先 probe，语义一致。
        if (!Classifier::probe()) { engineMissing->store(true); return false; }
        AiWriter writer(lib_, this);
        return Classifier::classify(snap, writer, td, 32, -1, prog, cancelPred);
    }));
}

// 方向 C·人脸识别（方案A）：收到图库「识别人物」信号后，异步跑人脸检测
// （QtConcurrent 不阻塞 GUI），带进度对话框与取消；完成后重建 chips。
// 引擎就绪探测 + 检测全部在 worker 线程：AiSubprocessRunner 禁 GUI 线程调用
// （GUI 线程 = Q_ASSERT + Busy），故此处绝不在 GUI 线程 probe（旧实现在 GUI 线程
// 同步 QNAM probe 无害，换子进程底座后即为崩溃点——本次接线修正）。
// 单写者（core/AiWriter.h）：worker 只碰快照与子进程，setPersons 落库经 AiWriter 恒回 GUI 线程。
void MainWindow::runPersonDetect()
{
    if (!lib_) return;
    // 1) 统计待检测数（person 为空；GUI 线程纯内存读快照，可）
    int pending = 0;
    for (const Photo& p : lib_->photos())
        if (p.person.isEmpty()) ++pending;
    if (pending == 0) {
        QMessageBox::information(this,
            QCoreApplication::translate("LightAlbum", "识别人物"),
            QCoreApplication::translate("LightAlbum", "库内照片均已标注人物，无需再跑。"));
        return;
    }
    // FIX-20：跑前基线（已标注人物张数），finished 里做差得到本轮实际写回数。
    const int labeledBefore = countLabeled(lib_->photos(), &Photo::person);
    // 2) 进度对话框 + 后台跑（探测与检测全在 worker 线程）
    auto* dlg = new QProgressDialog(
        QCoreApplication::translate("LightAlbum", "正在识别人物…"),
        QCoreApplication::translate("LightAlbum", "取消"), 0, pending, this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    auto* watcher = new QFutureWatcher<bool>(this);
    auto* cancelled = new std::atomic<bool>(false);
    auto* engineMissing = new std::atomic<bool>(false);   // R-20 三态旗标（worker 写、GUI 线程读）
    connect(dlg, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, dlg, watcher, cancelled, engineMissing, labeledBefore] {
                dlg->close();
                const bool ok = watcher->result();
                refreshExhibits();     // 重建人物 chips + 计数（与 runClassify 同款：
                                       // 「中断」也可能已部分写回，chips 必须与盘面一致）
                                       // 2.41.0 §2.3：person 是人物馆展室的聚合键 → 同源刷新
                QString msg;
                if (engineMissing->load()) {
                    // 三态与文案纪律同 runClassify（T12 修复波）：未就绪串不点名脚本/产物，
                    // 只说缺什么、数据不出机，并把用户指向他真能点的按钮。
                    msg = QCoreApplication::translate("LightAlbum",
                        "AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「识别人物」。照片仅在本机处理，绝不外传。");
                } else {
                    // FIX-20：干净轮次报「本轮实际写回数」——一张脸都没检出时这里是 0，
                    // 不再报 pending 冒充「已标注 N 张」。
                    msg = ok ? QCoreApplication::translate("LightAlbum", "已为 %1 张照片标注人物。")
                                .arg(qMax(0, countLabeled(lib_->photos(), &Photo::person) - labeledBefore))
                             : QCoreApplication::translate("LightAlbum", "识别过程中断（可能已部分写入）。");
                }
                QMessageBox::information(this,
                    QCoreApplication::translate("LightAlbum", "识别人物"), msg);
                watcher->deleteLater();
                delete cancelled;
                delete engineMissing;
                dlg->deleteLater();
            });
    const auto prog = [dlg](int done, int, const QString&) {
        QMetaObject::invokeMethod(dlg, "setValue", Qt::QueuedConnection, Q_ARG(int, done));
    };
    const auto cancelPred = [cancelled] { return cancelled->load(); };
    const QVector<Photo> snap = lib_->photos();   // GUI 线程快照，见 runClassify 同款约束
    watcher->setFuture(QtConcurrent::run([this, snap, prog, cancelPred, engineMissing] {
        // 探测在 worker 线程（子进程 runner 禁 GUI 线程调用）：未就绪 → 置旗标 + 返回
        // false → finished 弹「AI 引擎未就绪」文案。detect 内部亦先 probe，语义一致。
        // 旗标而非「返回值 false」决定文案：detect 的 false 现在还有第二义（FIX-14：
        // 逐张可见失败/取消中断），无旗标即如实落「中断」支，不再谎报「已标注 N 张」。
        if (!PersonDetector::probe()) { engineMissing->store(true); return false; }
        AiWriter writer(lib_, this);
        return PersonDetector::detect(snap, writer, -1, prog, cancelPred);
    }));
}

// 方向 C·人物自动聚类：收到图库「智能聚类」信号后，异步
// ① worker 抽取代表脸 embedding（经 AiWriter 投递 face_emb 落库）②③④ finished
// 处理器（GUI 线程）读回 embedding → FaceCluster 阈值余弦聚类 → applyClusters 写回 person
// → 刷新人物 chips。QtConcurrent 不阻塞 GUI，带进度对话框与取消。
// 引擎就绪探测 + 抽取全在 worker 线程：AiSubprocessRunner 禁 GUI 线程调用（旧实现在
// GUI 线程同步 QNAM probe，换子进程底座后即崩溃点——本次接线修正）。
// 三态判别（R-20，T12 修复波第 2 轮补齐）：worker 返回码 + engineMissing 旗标 ⇒
// 「引擎未就绪」/「跑完」/「中断」，返回码 false 不再单独等于未就绪。
// 单写者（core/AiWriter.h）：worker 只碰快照与子进程；读回/聚类/写库恒在 GUI 线程，
// 且 finished 晚于 detectEmbeddings 的投递入队，故能读到刚落库的 embedding。
// 起始编号：扫描既有 person token 中的「人物N」取最大 N，startIndex=N+1，避免与用户
// 已重命名的既有标签撞号（撞号会把两个人的照片混进同一人物 chips）。
void MainWindow::runFaceCluster()
{
    if (!lib_) return;
    // 1) 统计待聚类数（person 为空）
    int pending = 0;
    for (const Photo& p : lib_->photos())
        if (p.person.isEmpty()) ++pending;
    if (pending == 0) {
        QMessageBox::information(this,
            QCoreApplication::translate("LightAlbum", "智能聚类"),
            QCoreApplication::translate("LightAlbum",
                "库内照片均已标注人物，无需再聚类。要重新聚类请先清空人物标注。"));
        return;
    }
    // FIX-20：跑前基线（已标注人物张数），finished 里做差得到本轮实际聚类写回数。
    const int labeledBefore = countLabeled(lib_->photos(), &Photo::person);
    // 2) 起始编号：既有「人物N」的最大 N + 1
    int maxN = 0;
    for (const Photo& p : lib_->photos()) {
        for (const QString& t : p.person.split(',', Qt::SkipEmptyParts)) {
            const QString tt = t.trimmed();
            if (tt.startsWith(QStringLiteral("人物"))) {
                bool ok = false;
                const int n = tt.mid(2).toInt(&ok);
                if (ok && n > maxN) maxN = n;
            }
        }
    }
    const int startIndex = maxN + 1;
    // 4) 进度对话框 + 后台跑
    auto* dlg = new QProgressDialog(
        QCoreApplication::translate("LightAlbum", "正在聚类人物…"),
        QCoreApplication::translate("LightAlbum", "取消"), 0, pending, this);
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    auto* watcher = new QFutureWatcher<bool>(this);
    auto* cancelled = new std::atomic<bool>(false);
    auto* engineMissing = new std::atomic<bool>(false);   // R-20 三态旗标（worker 写、GUI 线程读）
    connect(dlg, &QProgressDialog::canceled, this, [cancelled] { cancelled->store(true); });
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, dlg, watcher, cancelled, engineMissing, labeledBefore, startIndex] {
                dlg->close();
                const bool extracted = watcher->result();
                // ② 读回 embedding → 阈值余弦聚类 → ③ 簇标签写回 person，全部在 GUI 线程。
                //   detectEmbeddings 的 face_emb 写回已经 AiWriter 投递到 GUI 事件队列，
                //   且这些投递在 finished 之前入队，故此刻能读到刚写入的 embedding；
                //   聚类是纯计算，applyClusters 恒在 GUI 线程（单写者，不覆盖非空）。
                if (extracted && !cancelled->load()) {
                    const QVector<FaceItem> embs = lib_->getFaceEmbeddings();
                    if (!embs.isEmpty()) {
                        QVector<ClusterInput> items;
                        items.reserve(embs.size());
                        for (const FaceItem& e : embs) items.append({e.path, e.emb});
                        const ClusterResult cr = FaceCluster::cluster(items, 0.4, startIndex);
                        QVector<ClusterItem> out;
                        out.reserve(embs.size());
                        for (const ClusterGroup& g : cr.groups)
                            for (const QString& p : g.paths)
                                out.append({p, g.label});
                        lib_->applyClusters(out);
                    }
                }
                refreshExhibits();   // 重建人物 chips + 计数（聚类标签生效；2.41.0 §2.3 展室同步）
                QString msg;
                if (engineMissing->load()) {
                    // 三态与文案纪律同 runClassify/runPersonDetect（T12 修复波第 2 轮）：
                    // 未就绪串不点名脚本/产物；「中断」串复用 .ts 里的既有译文，零新增翻译。
                    msg = QCoreApplication::translate("LightAlbum",
                        "AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「智能聚类」。照片仅在本机处理，绝不外传。");
                } else {
                    msg = extracted
                        ? QCoreApplication::translate("LightAlbum", "已聚类 %1 张照片为不同人物。")
                             .arg(qMax(0, countLabeled(lib_->photos(), &Photo::person) - labeledBefore))
                        : QCoreApplication::translate("LightAlbum", "聚类过程中断（可能已部分写入）。");
                }
                QMessageBox::information(this,
                    QCoreApplication::translate("LightAlbum", "智能聚类"), msg);
                watcher->deleteLater();
                delete cancelled;
                delete engineMissing;
                dlg->deleteLater();
            });
    const auto prog = [dlg](int done, int, const QString&) {
        QMetaObject::invokeMethod(dlg, "setValue", Qt::QueuedConnection, Q_ARG(int, done));
    };
    const auto cancelPred = [cancelled] { return cancelled->load(); };
    const QVector<Photo> snap = lib_->photos();   // GUI 线程快照，见 runClassify 同款约束
    watcher->setFuture(QtConcurrent::run([this, snap, prog, cancelPred, engineMissing] {
        // ① 抽代表脸 embedding 并经 AiWriter 投递 face_emb 写回（落库恒在 GUI 线程）
        //    探测/推理全在 worker 线程的子进程门面里（runner 禁 GUI 线程调用）。
        //    旗标与 runPersonDetect 同式：detectEmbeddings 的 false 现在有两义
        //    （FIX-14 逐张可见失败/取消中断 vs 未就绪），只有旗标能区分。
        if (!PersonDetector::probe()) { engineMissing->store(true); return false; }
        AiWriter writer(lib_, this);
        return PersonDetector::detectEmbeddings(snap, writer, -1, prog, cancelPred);
    }));
}

#include "MainWindow.h"
#include "Icons.h"
#include "GalleryPage.h"
#include "MapPage.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>

namespace {
struct NavItem { const char* label; const char* phase; };
// 与 web 版导航一致：首页/足迹/图库 · 智能选片/修图/AI美图 · 日志/关于（三组分隔在 P1 用间隔项体现）
const NavItem kNav[] = {
    {"首页", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"选片", "P3"}, {"修图", "P3"}, {"美图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};
} // namespace

MainWindow::MainWindow(const Library* lib, const QString& thumbsDir, QWidget* parent)
    : QMainWindow(parent), lib_(lib)
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧图标栏（web 版 #sidebar 的原生对应；P1 用双字短标签，图标随 P2 起补）----
    nav_ = new QListWidget(central);
    nav_->setFixedWidth(64);
    nav_->setViewMode(QListView::ListMode);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#0b0e0c;border-right:1px solid #2a2f2c;outline:0;padding-top:6px;}"
        "QListWidget::item{color:#9fa8a3;font-size:11px;height:52px;padding-left:4px;"
        "  border-bottom:1px solid rgba(255,255,255,.04);}"
        "QListWidget::item:selected{color:#f2f5f3;background:#1c2320;"
        "  border-left:2px solid #c19a4b;}"
        "QListWidget::item:selected .lbl{color:#f2f5f3;}"));
    nav_->setIconSize(QSize(22, 22));
    const char* kIcons[] = {"home", "map", "gallery", "cull", "retouch", "enhance", "log", "about"};
    for (int i = 0; i < int(sizeof(kNav) / sizeof(kNav[0])); ++i) {
        auto* it = new QListWidgetItem(QString::fromUtf8(kNav[i].label));
        it->setIcon(Icons::make(QString::fromLatin1(kIcons[i]), QColor(0x9f, 0xa8, 0xa3)));
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        nav_->addItem(it);
    }
    root->addWidget(nav_);

    // ---- 右侧：顶栏 + 页面栈 ----
    auto* right = new QWidget(central);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* topbar = new QWidget(right);
    topbar->setObjectName("topbar");
    topbar->setStyleSheet(QStringLiteral(
        "#topbar{background:#101513;border-bottom:1px solid #2a2f2c;}"));
    auto* tl = new QHBoxLayout(topbar);
    tl->setContentsMargins(18, 10, 18, 10);
    title_ = new QLabel(QStringLiteral("光影集"), topbar);
    QFont tf = title_->font(); tf.setPointSizeF(13); tf.setBold(true);
    title_->setFont(tf);
    title_->setStyleSheet("color:#f2f5f3;");
    search_ = new QLineEdit(topbar);
    search_->setPlaceholderText(QStringLiteral("搜索标题 / 文件名 / 器材…"));
    search_->setFixedWidth(300);
    search_->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#12181a;border:1px solid #2a2f2c;border-radius:8px;"
        "padding:6px 12px;color:#f2f5f3;}"));
    tl->addWidget(title_);
    tl->addStretch(1);
    tl->addWidget(search_);
    rl->addWidget(topbar);

    // ---- KPI 指标条（web 版 #kpiStrip 对应；收藏不进 P1：FAVS 属 web localStorage，原生侧不编造）----
    kpis_ = new QWidget(right);
    kpis_->setStyleSheet(QStringLiteral("background:#0b0e0c;"));
    auto* kl = new QHBoxLayout(kpis_);
    kl->setContentsMargins(18, 10, 18, 10);
    kl->setSpacing(14);
    const Kpi k = lib_->kpi();
    const QString yearTxt = k.yearCount > 0
        ? QStringLiteral("%1 跨 (%2\u2013%3)").arg(k.yearCount).arg(k.minYear).arg(k.maxYear)
        : QStringLiteral("\u2014");
    auto addKpi = [kl](const QString& v, const QString& label) {
        auto* lb = new QLabel(QStringLiteral(
            "<b style='font-size:16px;color:#f2f5f3;'>%1</b><br>"
            "<span style='font-size:11px;color:#9fa8a3;'>%2</span>").arg(v, label));
        lb->setStyleSheet(QStringLiteral(
            "QLabel{background:#181d1a;border:1px solid #2a2f2c;border-radius:10px;"
            "padding:8px 16px;}"));
        kl->addWidget(lb);
    };
    addKpi(QString::number(k.total), QStringLiteral("作品总数"));
    addKpi(yearTxt, QStringLiteral("拍摄年份"));
    addKpi(QString::number(k.provCount), QStringLiteral("足迹省份"));
    kl->addStretch(1);
    rl->addWidget(kpis_);

    stack_ = new QStackedWidget(right);
    // 页序与 kNav 一致；P1 只实装「图库」，其余诚实占位
    stack_->addWidget(makePlaceholder(QStringLiteral("首页"), QString::fromUtf8(kNav[0].phase)));
    map_ = new MapPage(stack_);
    stack_->addWidget(map_);
    gallery_ = new GalleryPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(gallery_);
    stack_->addWidget(makePlaceholder(QStringLiteral("智能选片"), QString::fromUtf8(kNav[3].phase)));
    stack_->addWidget(makePlaceholder(QStringLiteral("修图实验室"), QString::fromUtf8(kNav[4].phase)));
    stack_->addWidget(makePlaceholder(QStringLiteral("AI 美图"), QString::fromUtf8(kNav[5].phase)));
    stack_->addWidget(makePlaceholder(QStringLiteral("摄影日志"), QString::fromUtf8(kNav[6].phase)));
    stack_->addWidget(makePlaceholder(QStringLiteral("关于"), QString::fromUtf8(kNav[7].phase)));
    rl->addWidget(stack_, 1);
    root->addWidget(right, 1);

    setCentralWidget(central);
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(search_, &QLineEdit::textChanged, gallery_, &GalleryPage::applyFilter);
    nav_->setCurrentRow(2);   // 默认落在图库（P1 唯一实装页）
}

QWidget* MainWindow::makePlaceholder(const QString& secName, const QString& phase) const
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setAlignment(Qt::AlignCenter);
    auto* t = new QLabel(secName, w);
    QFont f = t->font(); f.setPointSizeF(20); f.setBold(true); t->setFont(f);
    t->setStyleSheet("color:#f2f5f3;");
    auto* s = new QLabel(QStringLiteral("该区段将在 %1 迁移 · 见 docs/REWRITE-PLAN.md").arg(phase), w);
    s->setStyleSheet("color:#9fa8a3;");
    l->addWidget(t, 0, Qt::AlignHCenter);
    l->addWidget(s, 0, Qt::AlignHCenter);
    return w;
}

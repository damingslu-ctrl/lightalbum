#include "MainWindow.h"
#include "Icons.h"
#include "Theme.h"
#include "GalleryPage.h"
#include "MapPage.h"
#include "CullPage.h"
#include "RetouchPage.h"
#include "BeautyPage.h"
#include "../core/Scanner.h"
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
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <atomic>
#include <cstdio>

namespace {
struct NavItem { const char* label; const char* phase; };
// 与 web 版导航一致：首页/足迹/图库 · 智能选片/修图/AI美图 · 日志/关于（三组分隔在 P1 用间隔项体现）
const NavItem kNav[] = {
    {"首页", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"选片", "P3"}, {"修图", "P3"}, {"美图", "P3"},
    {"日志", "P2"}, {"关于", "P3"},
};
} // namespace

MainWindow::MainWindow(const Library* lib, const QString& thumbsDir,
                       const QString& dbPath, QWidget* parent)
    : QMainWindow(parent), lib_(lib), dbPath_(dbPath)
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧图标栏（P11：胶囊选中态 + 翡翠渐变，圆角导航大胆化）----
    nav_ = new QListWidget(central);
    nav_->setFixedWidth(68);
    nav_->setViewMode(QListView::ListMode);
    nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#0c100d;border-right:1px solid #26312a;outline:0;padding-top:8px;}"
        "QListWidget::item{color:#8da396;font-size:11px;height:52px;"
        "  margin:3px 8px;border-radius:10px;}"
        "QListWidget::item:hover{background:#131a15;color:#f4f7f4;}"
        "QListWidget::item:selected{color:#f4f7f4;"
        "  background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 rgba(79,192,138,52), stop:1 rgba(79,192,138,10));"
        "  border-left:2px solid #d9a94e;}"
        "QListWidget::item:selected .lbl{color:#f4f7f4;}"));
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
    // P8：原生导入器入口（补齐 P7 审计的唯一硬缺口）
    auto* importBtn = new QPushButton(QString::fromUtf8("导入目录"), topbar);
    importBtn->setCursor(Qt::PointingHandCursor);
    importBtn->setStyleSheet(Lumen::btnGold());
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::startImport);
    tl->addWidget(importBtn);
    tl->addWidget(search_);
    rl->addWidget(topbar);
    // 顶栏底部翡翠→金渐变发丝线（大胆点：品牌双色的呼吸缝）
    auto* seam = new QFrame(right);
    seam->setFixedHeight(2);
    seam->setStyleSheet(QStringLiteral(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #4fc08a, stop:0.55 #d9a94e, stop:1 rgba(217,169,78,0));"));
    rl->addWidget(seam);

    // ---- KPI 指标条（P11：语义色竖条卡 —— 总数翡翠 / 年份金 / 足迹紫罗兰）----
    kpis_ = new QWidget(right);
    kpis_->setStyleSheet(QStringLiteral("background:#060806;"));
    auto* kl = new QHBoxLayout(kpis_);
    kl->setContentsMargins(20, 12, 20, 12);
    kl->setSpacing(14);
    const Kpi k = lib_->kpi();
    const QString yearTxt = k.yearCount > 0
        ? QStringLiteral("%1 跨 (%2\u2013%3)").arg(k.yearCount).arg(k.minYear).arg(k.maxYear)
        : QStringLiteral("\u2014");
    auto addKpi = [kl](const QString& v, const QString& label, const QColor& accent) {
        auto* lb = new QLabel(QStringLiteral(
            "<b style='font-size:18px;color:#f4f7f4;'>%1</b><br>"
            "<span style='font-size:11px;color:#8da396;'>%2</span>").arg(v, label));
        lb->setStyleSheet(Lumen::kpiCard(accent));
        kl->addWidget(lb);
    };
    addKpi(QString::number(k.total), QStringLiteral("作品总数"), Lumen::EMERALD);
    addKpi(yearTxt, QStringLiteral("拍摄年份"), Lumen::GOLD);
    addKpi(QString::number(k.provCount), QStringLiteral("足迹省份"), Lumen::VIOLET);
    kl->addStretch(1);
    rl->addWidget(kpis_);

    stack_ = new QStackedWidget(right);
    // 页序与 kNav 一致；P1 只实装「图库」，其余诚实占位
    stack_->addWidget(makePlaceholder(QStringLiteral("首页"), QString::fromUtf8(kNav[0].phase)));
    map_ = new MapPage(stack_);
    stack_->addWidget(map_);
    gallery_ = new GalleryPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(gallery_);
    cull_ = new CullPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(cull_);
    retouch_ = new RetouchPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(retouch_);
    beauty_ = new BeautyPage(&lib_->photos(), thumbsDir, stack_);
    stack_->addWidget(beauty_);
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
    l->setSpacing(10);
    auto* t = new QLabel(secName, w);
    QFont f = t->font(); f.setPointSizeF(22); f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    t->setFont(f);
    t->setStyleSheet("color:#f4f7f4;");
    auto* s = new QLabel(QString::fromUtf8("该区段将在 %1 迁移 · 见 docs/REWRITE-PLAN.md").arg(phase), w);
    s->setStyleSheet("color:#8da396;");
    l->addWidget(t, 0, Qt::AlignHCenter);
    l->addWidget(s, 0, Qt::AlignHCenter);
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

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
#include <QScrollArea>
#include <QToolButton>
#include <QPainter>
#include <QPainterPath>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <atomic>
#include <algorithm>
#include <cstdio>

namespace {
struct NavItem { const char* label; const char* phase; };
// 与 web 版导航一致：首页/足迹/图库 · 智能选片/修图/AI美图 · 日志/关于（三组分隔在 P1 用间隔项体现）
const NavItem kNav[] = {
    {"首页", "P1"}, {"足迹", "P2"}, {"图库", "P1"},
    {"选片", "P3"}, {"修图", "P3"}, {"美图", "P3"},
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

MainWindow::MainWindow(const Library* lib, const QString& thumbsDir,
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
        ? QStringLiteral("%1 年 (%2\u2013%3)").arg(k.yearCount).arg(k.minYear).arg(k.maxYear)
        : QStringLiteral("\u2014");
    auto addKpi = [kl](const QString& v, const QString& label, const QColor& accent) {
        auto* lb = new QLabel(QStringLiteral(
            "<b style='font-size:20px;color:#f4f7f4;'>%1</b><br>"
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
    // 页序与 kNav 一致；首页 = P12「美术馆策展」艺术仪表盘，日志为设计态占位，关于页实装
    stack_->addWidget(makeHome(thumbsDir));
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
    stack_->addWidget(makePlaceholder(QStringLiteral("摄影日志"), QStringLiteral("log"),
        QStringLiteral("拍摄时间线与器材使用统计")));
    stack_->addWidget(makeAbout());
    rl->addWidget(stack_, 1);
    root->addWidget(right, 1);

    setCentralWidget(central);
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(search_, &QLineEdit::textChanged, gallery_, &GalleryPage::applyFilter);
    nav_->setCurrentRow(2);   // 默认落在图库（P1 唯一实装页）
}

// P12「美术馆策展」首页：对角渐变 hero 大字 + 功能入口卡 + 最近作品胶片带。
// 大胆但有秩序：渐变只出现在 hero 与 hover 强语义处，照片仍是唯一主角。
QWidget* MainWindow::makeHome(const QString& thumbsDir) const
{
    auto* w = new QWidget;
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(16);

    // ---- Hero：对角翡翠→金低语渐变 + 大字标题 + 水印图标 ----
    auto* hero = new QWidget(w);
    hero->setObjectName("hero");
    hero->setStyleSheet(Lumen::heroBand());
    hero->setMinimumHeight(172);
    auto* hl = new QHBoxLayout(hero);
    hl->setContentsMargins(30, 22, 26, 22);
    hl->setSpacing(18);
    auto* hv = new QVBoxLayout;
    hv->setSpacing(7);
    auto* chipRow = new QHBoxLayout; chipRow->setSpacing(8);
    auto* chip1 = new QLabel(QStringLiteral("核心客户端 v%1").arg(LA_VERSION), hero);
    chip1->setStyleSheet(Lumen::pill(Lumen::EMERALD));
    auto* chip2 = new QLabel(QStringLiteral("零云端 · 本地优先"), hero);
    chip2->setStyleSheet(Lumen::pill(Lumen::GOLD));
    chipRow->addWidget(chip1); chipRow->addWidget(chip2); chipRow->addStretch(1);
    hv->addLayout(chipRow);
    auto* title = new QLabel(QStringLiteral("光影集"), hero);
    QFont tf = title->font(); tf.setPointSizeF(30); tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 9.0);
    title->setFont(tf);
    title->setStyleSheet("color:#f4f7f4;");
    hv->addWidget(title);
    auto* en = new QLabel(QStringLiteral("L I G H T A L B U M"), hero);
    QFont ef = en->font(); ef.setPointSizeF(8.5);
    ef.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    en->setFont(ef);
    en->setStyleSheet("color:#d9a94e;");
    hv->addWidget(en);
    auto* tag = new QLabel(
        QStringLiteral("每一次快门，都是一次驻足 —— 私人影像馆，全部在本机完成"), hero);
    tag->setStyleSheet("color:#8da396;font-size:12px;");
    hv->addWidget(tag);
    hl->addLayout(hv);
    hl->addStretch(1);
    auto* wm = new QLabel(hero);
    wm->setPixmap(Icons::make(QStringLiteral("gallery"),
                              QColor(0x4f, 0xc0, 0x8a, 36), 116).pixmap(116, 116));
    wm->setAlignment(Qt::AlignVCenter);
    hl->addWidget(wm);
    outer->addWidget(hero);

    // ---- 功能入口：四张语义色卡，hover 点亮，点击直达 ----
    struct Feat { const char* name; const char* desc; const char* icon;
                  int navRow; QColor accent; };
    const Feat feats[] = {
        {"图库", "浏览全部作品", "gallery", 2, Lumen::EMERALD},
        {"选片", "本地打分筛选", "cull",   3, Lumen::GOLD},
        {"修图", "流明引擎手动调", "retouch", 4, Lumen::EMERALD},
        {"美图", "一键风格化",   "enhance", 5, Lumen::VIOLET},
    };
    auto* featRow = new QWidget(w);
    auto* fl = new QHBoxLayout(featRow);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(14);
    for (const Feat& ft : feats) {
        auto* col = new QWidget(featRow);
        auto* cv = new QVBoxLayout(col);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(5);
        auto* btn = new QToolButton(col);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(Icons::make(QString::fromLatin1(ft.icon), ft.accent, 40));
        btn->setIconSize(QSize(40, 40));
        btn->setText(QString::fromUtf8(ft.name));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(96);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setStyleSheet(Lumen::featureCard(ft.accent));
        connect(btn, &QToolButton::clicked, this,
                [this, row = ft.navRow] { nav_->setCurrentRow(row); });
        auto* desc = new QLabel(QString::fromUtf8(ft.desc), col);
        desc->setAlignment(Qt::AlignCenter);
        desc->setStyleSheet("color:#8da396;font-size:10.5px;");
        cv->addWidget(btn, 1);
        cv->addWidget(desc);
        fl->addWidget(col, 1);
    }
    outer->addWidget(featRow);

    // ---- 最近作品胶片带：真实缩略图（按日期倒序取 12 张），金描边 hover ----
    auto* head = new QWidget(w);
    auto* hhl = new QHBoxLayout(head);
    hhl->setContentsMargins(0, 0, 0, 0);
    auto* sec = new QLabel(QStringLiteral("最近作品"), head);
    QFont sf = sec->font(); sf.setPointSizeF(13); sf.setBold(true);
    sf.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    sec->setFont(sf);
    sec->setStyleSheet("color:#f4f7f4;");
    auto* more = new QLabel(QStringLiteral(
        "<a href='#gallery' style='color:#d9a94e;text-decoration:none;'>在图库中查看全部 →</a>"), head);
    more->setTextFormat(Qt::RichText);
    connect(more, &QLabel::linkActivated, this,
            [this] { nav_->setCurrentRow(2); });
    hhl->addWidget(sec);
    hhl->addStretch(1);
    hhl->addWidget(more);
    outer->addWidget(head);

    auto* stripArea = new QScrollArea(w);
    stripArea->setWidgetResizable(true);
    stripArea->setFixedHeight(212);
    stripArea->setFrameShape(QFrame::NoFrame);
    stripArea->setStyleSheet(QStringLiteral(
        "QScrollArea{background:transparent;}"
        "QScrollBar:horizontal{background:transparent;height:8px;margin:2px;}"
        "QScrollBar::handle:horizontal{background:#24312a;border-radius:3px;min-width:40px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"));
    auto* strip = new QWidget(stripArea);
    auto* sl = new QHBoxLayout(strip);
    sl->setContentsMargins(0, 2, 0, 2);
    sl->setSpacing(12);

    QVector<const Photo*> recent;
    const QVector<Photo>& all = lib_->photos();
    for (const Photo& ph : all)
        if (!ph.thumbFile.isEmpty()) recent.append(&ph);
    std::stable_sort(recent.begin(), recent.end(),
                     [](const Photo* a, const Photo* b) { return a->date > b->date; });
    if (recent.size() > 12) recent.resize(12);
    for (const Photo* ph : recent) {
        const QPixmap pm = roundedThumb(
            QDir(thumbsDir).filePath(ph->thumbFile), 150, 10);
        if (pm.isNull()) continue;
        auto* card = new QToolButton(strip);
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setIcon(QIcon(pm));
        card->setIconSize(QSize(150, 150));
        card->setText(ph->date.isEmpty() ? QStringLiteral("—") : ph->date);
        card->setCursor(Qt::PointingHandCursor);
        card->setFixedSize(162, 198);
        card->setStyleSheet(Lumen::filmCard());
        connect(card, &QToolButton::clicked, this,
                [this] { nav_->setCurrentRow(2); });
        sl->addWidget(card);
    }
    sl->addStretch(1);
    stripArea->setWidget(strip);
    outer->addWidget(stripArea);

    if (recent.isEmpty()) {
        auto* hint = new QLabel(
            QStringLiteral("导入目录后，这里会展示你的最新作品。"), w);
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("color:#8da396;");
        outer->addWidget(hint);
    }
    outer->addStretch(1);
    return w;
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
        "零云端承诺：导入、足迹、图库、选片、修图、美图全部在本机完成，"
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

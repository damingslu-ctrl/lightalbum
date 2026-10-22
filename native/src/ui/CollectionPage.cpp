#include "CollectionPage.h"
#include "ThumbCache.h"
#include "Theme.h"
#include "../core/CurateEngine.h"   // templateLabelFor：静音行显示可读名（token 只是键）

#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// 「永久」阈值 = 2100-01-01。CurateScheduler 的静音哨兵远在此之上；这里不复制那个
// 哨兵值，只按语义分档（≥ 本阈值即无到期日），免得同一事实出现第二个真源。
constexpr qint64 kForeverSecs = Q_INT64_C(4102444800);

QString sourceLabel(const QString& src)
{
    if (src == QLatin1String("curate"))
        return QCoreApplication::translate("CollectionPage", "策展收入");
    if (src == QLatin1String("cmdk"))
        return QCoreApplication::translate("CollectionPage", "检索收入");
    return src;
}

// 静音行标题：token 只是键。能对上表 → 本地化可读名；对不上 → 如实显示原 token，不编名字。
QString muteTitle(const QString& token)
{
    Curate::TemplateId id;
    if (Curate::templateIdFromToken(token, &id)) {
        const QString label = Curate::templateLabelFor(id);
        if (!label.isEmpty()) return label;
    }
    return token;
}

QString feedbackLabel(const QString& f)
{
    if (f == QLatin1String("kept"))
        return QCoreApplication::translate("CollectionPage", "已收进");
    if (f == QLatin1String("swapped"))
        return QCoreApplication::translate("CollectionPage", "已换走");
    if (f == QLatin1String("muted"))
        return QCoreApplication::translate("CollectionPage", "已静音");
    return QString();
}

QString dateLabel(qint64 secs)
{
    if (secs <= 0) return QString();
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd"));
}

QLabel* metaLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setFont(Lumen::Type::monoMeta());
    l->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                         .arg(Lumen::hex(Lumen::muted())));
    return l;
}

QLabel* inkLabel(const QString& text, QWidget* parent, const QFont& f)
{
    auto* l = new QLabel(text, parent);
    l->setFont(f);
    l->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                         .arg(Lumen::hex(Lumen::ink())));
    return l;
}

// 行外壳：与人物馆展室卡同一语汇（card 底 + line 描边 + Card 圆角）。
QFrame* makeRowFrame(QWidget* parent)
{
    auto* row = new QFrame(parent);
    row->setObjectName(QStringLiteral("crow"));
    row->setStyleSheet(QStringLiteral(
        "QFrame#crow{background:%1;border:1px solid %2;border-radius:%3px;}")
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Card));
    return row;
}

QLabel* makeCover(QWidget* parent)
{
    auto* cover = new QLabel(parent);
    cover->setFixedSize(56, 56);
    cover->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;")
                             .arg(Lumen::hex(Lumen::inset())).arg(Lumen::Radius::Inset));
    return cover;
}

void styleList(QListWidget* l)
{
    l->setFrameShape(QFrame::NoFrame);
    l->setSelectionMode(QAbstractItemView::SingleSelection);
    l->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    l->setStyleSheet(QStringLiteral(
        "QListWidget{background:transparent;border:none;outline:none;}"
        "QListWidget::item{background:transparent;border:none;margin:0px 0px %1px 0px;}"
        "QListWidget::item:selected{background:transparent;}")
        .arg(Lumen::Space::S));
}

} // namespace

CollectionPage::CollectionPage(Library* lib, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), lib_(lib), thumbsDir_(thumbsDir)
{
    cache_ = new ThumbCache(thumbsDir_, this);
    setAutoFillBackground(true);
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Lumen::bg());
        setPalette(pal);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::Xl,
                             Lumen::Space::Xxl, Lumen::Space::Xl);
    root->setSpacing(Lumen::Space::L);

    root->addWidget(inkLabel(QCoreApplication::translate("CollectionPage", "典藏"),
                             this, Lumen::Type::display()));

    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    tabs_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane{border:none;background:transparent;}"
        "QTabBar::tab{background:transparent;color:%1;padding:%2px %3px;border:none;}"
        "QTabBar::tab:selected{color:%4;border-bottom:2px solid %4;}")
        .arg(Lumen::hex(Lumen::muted())).arg(Lumen::Space::S).arg(Lumen::Space::M)
        .arg(Lumen::hex(Lumen::gold())));
    root->addWidget(tabs_, 1);

    auto makeTab = [this](const QString& label, QListWidget** outList, QWidget** outEmpty,
                          const QString& emptyTitle, const QString& emptyHint) {
        auto* page = new QWidget(tabs_);
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(0, Lumen::Space::M, 0, 0);
        v->setSpacing(Lumen::Space::M);

        *outEmpty = emptyGuide(emptyTitle, emptyHint);
        v->addWidget(*outEmpty);
        *outList = new QListWidget(page);
        styleList(*outList);
        v->addWidget(*outList, 1);
        tabs_->addTab(page, label);
    };
    makeTab(QCoreApplication::translate("CollectionPage", "合集"), &albums_, &albumsEmpty_,
            QCoreApplication::translate("CollectionPage", "还没有合集"),
            QCoreApplication::translate("CollectionPage",
                "在前厅按『收进合集』，或在 ⌘K 里勾选照片后按『暂存为合集』。"));
    makeTab(QCoreApplication::translate("CollectionPage", "展览史"), &exhibits_, &exhibitsEmpty_,
            QCoreApplication::translate("CollectionPage", "还没有展览"),
            // 文案是承诺：CurateScheduler 的节流窗实测 >7 天，且非 force 调用只发生在冷启动
            // 一处，「每天会挑一展」不成立（C8 与 kWeekSec 同钉，漂边即红）。
            QCoreApplication::translate("CollectionPage",
                "前厅要距上一版超过 7 天才会自动再挑一展（重开应用时生效）；"
                "按『换一展』可随时换，记录都留在这里。"));
    makeTab(QCoreApplication::translate("CollectionPage", "安静下来的选题"), &mutes_, &mutesEmpty_,
            QCoreApplication::translate("CollectionPage", "没有被静音的选题"),
            QCoreApplication::translate("CollectionPage",
                "在前厅按『别再这类』，会让这类安静下来。"));

    // 双击与回车都走 itemActivated（键盘与鼠标同一入口，不另设第二套）。
    connect(albums_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        if (!it) return;
        const int i = it->data(Qt::UserRole).toInt();
        if (i < 0 || i >= albumRows_.size()) return;
        const QStringList paths = lib_->albumPaths(albumRows_[i].id);
        if (!paths.isEmpty()) emit openAlbum(paths);
    });
    connect(exhibits_, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) {
        if (!it) return;
        const int i = it->data(Qt::UserRole).toInt();
        if (i < 0 || i >= exhibitRows_.size()) return;
        const ExhibitRow& r = exhibitRows_[i];
        const QStringList paths = r.paths.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!paths.isEmpty()) emit openExhibit(paths, r.title, r.blurb);
    });

    refresh();
}

void CollectionPage::setHidePrivate(bool hide)
{
    if (hidePrivate_ == hide) return;
    hidePrivate_ = hide;
    refresh();
}

void CollectionPage::refresh()
{
    rebuildAlbums();
    rebuildExhibits();
    rebuildMutes();
}

int CollectionPage::qaTabCount() const { return tabs_ ? tabs_->count() : 0; }
int CollectionPage::qaAlbumRowCount() const { return albumRows_.size(); }
int CollectionPage::qaExhibitRowCount() const { return exhibitRows_.size(); }
int CollectionPage::qaMuteRowCount() const { return muteRows_.size(); }

QWidget* CollectionPage::emptyGuide(const QString& title, const QString& hint)
{
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("empty"));
    card->setStyleSheet(Lumen::kpiCard(Lumen::emerald()));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                          Lumen::Space::Xl, Lumen::Space::L);
    v->setSpacing(Lumen::Space::M);
    v->addWidget(inkLabel(title, card, Lumen::Type::title(false)));

    auto* sub = new QLabel(hint, card);
    sub->setFont(Lumen::Type::body());
    sub->setWordWrap(true);
    sub->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                           .arg(Lumen::hex(Lumen::muted())));
    v->addWidget(sub);
    return card;
}

void CollectionPage::rebuildAlbums()
{
    albumRows_ = lib_ ? lib_->listAlbums() : QVector<AlbumRow>();
    albums_->clear();
    for (int i = 0; i < albumRows_.size(); ++i) {
        QWidget* row = makeAlbumRow(i);
        auto* item = new QListWidgetItem(albums_);
        item->setSizeHint(QSize(0, 80));
        item->setData(Qt::UserRole, i);
        albums_->setItemWidget(item, row);
    }
    albumsEmpty_->setVisible(albumRows_.isEmpty());
    albums_->setVisible(!albumRows_.isEmpty());
}

void CollectionPage::rebuildExhibits()
{
    exhibitRows_ = lib_ ? lib_->loadExhibitHistory() : QVector<ExhibitRow>();
    exhibits_->clear();
    for (int i = 0; i < exhibitRows_.size(); ++i) {
        QWidget* row = makeExhibitRow(i);
        auto* item = new QListWidgetItem(exhibits_);
        item->setSizeHint(QSize(0, 76));
        item->setData(Qt::UserRole, i);
        exhibits_->setItemWidget(item, row);
    }
    exhibitsEmpty_->setVisible(exhibitRows_.isEmpty());
    exhibits_->setVisible(!exhibitRows_.isEmpty());
}

void CollectionPage::rebuildMutes()
{
    muteRows_ = lib_ ? lib_->listMutes(QDateTime::currentSecsSinceEpoch())
                     : QVector<Library::MuteRow>();
    mutes_->clear();
    for (int i = 0; i < muteRows_.size(); ++i) {
        QWidget* row = makeMuteRow(i);
        auto* item = new QListWidgetItem(mutes_);
        item->setSizeHint(QSize(0, 64));
        item->setData(Qt::UserRole, i);
        mutes_->setItemWidget(item, row);
    }
    mutesEmpty_->setVisible(muteRows_.isEmpty());
    mutes_->setVisible(!muteRows_.isEmpty());
}

QWidget* CollectionPage::makeAlbumRow(int i)
{
    const AlbumRow& a = albumRows_[i];
    QFrame* row = makeRowFrame(albums_);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(Lumen::Space::M, Lumen::Space::S,
                          Lumen::Space::M, Lumen::Space::S);
    h->setSpacing(Lumen::Space::M);

    auto* cover = makeCover(row);
    h->addWidget(cover);

    const QStringList paths = lib_ ? lib_->albumPaths(a.id) : QStringList();
    if (!paths.isEmpty()) {
        const Photo* ph = lib_->find(paths.first());
        if (ph && !ph->thumbFile.isEmpty()) {
            const QString tf = ph->thumbFile;
            const QPixmap pm = cache_->cached(tf, ThumbCache::kTile);
            if (!pm.isNull())
                cover->setPixmap(pm.scaled(56, 56, Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation));
            // ctx = cover：回调解引用谁就守护谁（行被 clear() 掉时 Qt 自动断连）。
            cache_->request(tf, cover, [cover](const QPixmap& p) {
                if (!p.isNull())
                    cover->setPixmap(p.scaled(56, 56, Qt::KeepAspectRatioByExpanding,
                                              Qt::SmoothTransformation));
            }, ThumbCache::kTile);
        }
    }

    auto* col = new QVBoxLayout();
    col->setSpacing(Lumen::Space::Xs);
    col->addWidget(inkLabel(a.name, row, Lumen::Type::label(true)));

    // spec §2.1：锁定态报**可见**张数（唯一显示的那一套数字；解锁态用库里计数）。
    int n = a.count;
    if (hidePrivate_ && lib_) {
        int vis = 0;
        for (const QString& p : paths) {
            const Photo* ph = lib_->find(p);
            if (ph && !ph->priv) ++vis;
        }
        n = vis;
    }
    QStringList bits;
    bits << QCoreApplication::translate("CollectionPage", "%1 张").arg(n);
    const QString src = sourceLabel(a.source);
    if (!src.isEmpty()) bits << src;
    const QString d = dateLabel(a.createdAt);
    if (!d.isEmpty()) bits << d;
    col->addWidget(metaLabel(bits.join(QStringLiteral(" · ")), row));
    h->addLayout(col, 1);
    return row;
}

QWidget* CollectionPage::makeExhibitRow(int i)
{
    const ExhibitRow& r = exhibitRows_[i];
    QFrame* row = makeRowFrame(exhibits_);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(Lumen::Space::M, Lumen::Space::S,
                          Lumen::Space::M, Lumen::Space::S);
    h->setSpacing(Lumen::Space::M);

    auto* col = new QVBoxLayout();
    col->setSpacing(Lumen::Space::Xs);
    const QString title = r.title.isEmpty()
        ? QCoreApplication::translate("CollectionPage", "未命名展") : r.title;
    col->addWidget(inkLabel(title, row, Lumen::Type::label(true)));
    if (!r.blurb.isEmpty()) {
        auto* blurb = new QLabel(r.blurb, row);
        blurb->setFont(Lumen::Type::body());
        blurb->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                                 .arg(Lumen::hex(Lumen::muted())));
        col->addWidget(blurb);
    }

    const int frames = r.paths.split(QLatin1Char(','), Qt::SkipEmptyParts).size();
    QStringList bits;
    const QString d = dateLabel(r.generatedAt);
    if (!d.isEmpty()) bits << d;
    bits << QCoreApplication::translate("CollectionPage", "%1 帧").arg(frames);
    col->addWidget(metaLabel(bits.join(QStringLiteral(" · ")), row));
    h->addLayout(col, 1);

    const QString fb = feedbackLabel(r.feedback);
    if (!fb.isEmpty()) {
        auto* badge = new QLabel(fb, row);
        badge->setFont(Lumen::Type::monoMeta());
        badge->setStyleSheet(QStringLiteral(
            "color:%1;background:%2;border:none;border-radius:%3;padding:2px 8px;")
            .arg(Lumen::hex(Lumen::gold()), Lumen::hex(Lumen::inset()),
                 Lumen::Radius::Pill));
        h->addWidget(badge, 0, Qt::AlignTop);
    }
    return row;
}

QWidget* CollectionPage::makeMuteRow(int i)
{
    const Library::MuteRow& m = muteRows_[i];
    QFrame* row = makeRowFrame(mutes_);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(Lumen::Space::M, Lumen::Space::S,
                          Lumen::Space::M, Lumen::Space::S);
    h->setSpacing(Lumen::Space::M);

    auto* col = new QVBoxLayout();
    col->setSpacing(Lumen::Space::Xs);
    col->addWidget(inkLabel(muteTitle(m.tmpl), row, Lumen::Type::label(false)));
    const QString until = m.until >= kForeverSecs
        ? QCoreApplication::translate("CollectionPage", "永久安静")
        : QCoreApplication::translate("CollectionPage", "安静至 %1")
              .arg(dateLabel(m.until));
    col->addWidget(metaLabel(until, row));
    h->addLayout(col, 1);

    auto* undo = new QPushButton(QCoreApplication::translate("CollectionPage", "唤醒"), row);
    undo->setStyleSheet(Lumen::btnGhost());
    undo->setCursor(Qt::PointingHandCursor);
    const QString tmpl = m.tmpl;
    connect(undo, &QPushButton::clicked, this, [this, tmpl] { emit unmuteRequested(tmpl); });
    h->addWidget(undo, 0, Qt::AlignVCenter);
    return row;
}

#include "PeoplePage.h"
#include "ThumbCache.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMouseEvent>
#include <QtGlobal>
#include <algorithm>
#include <functional>

namespace {

// 可点展室卡：整卡热区（点击 = 展开/收起该展室）。
class ClickFrame : public QFrame {
public:
    ClickFrame(std::function<void()> onActivate, QWidget* parent = nullptr)
        : QFrame(parent), onActivate_(std::move(onActivate))
    { setCursor(Qt::PointingHandCursor); }

protected:
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && onActivate_) onActivate_();
    }

private:
    std::function<void()> onActivate_;
};

QString yearSpan(const Room& r)
{
    if (r.yearMin <= 0 && r.yearMax <= 0) return QStringLiteral("—");
    if (r.yearMin == r.yearMax) return QString::number(r.yearMin);
    return QStringLiteral("%1–%2").arg(r.yearMin).arg(r.yearMax);
}

} // namespace

PeoplePage::PeoplePage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    cache_ = new ThumbCache(thumbsDir_, this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setAutoFillBackground(true);
    rebuild();
}

void PeoplePage::setHidePrivate(bool hide)
{
    if (hidePrivate_ == hide) return;
    hidePrivate_ = hide;
    refresh();
}

void PeoplePage::refresh() { rebuild(); }

// 2.41.0 T8：⌘K 人名高置信 → 直接推门进那间展室。rooms_ 由 buildRooms 纯函数聚合
// （与门禁同源，铁律 20），故按名定位不重算全库；找不到则保持原展开态重建（不猜房）。
void PeoplePage::focusRoom(const QString& name)
{
    for (int i = 0; i < rooms_.size(); ++i) {
        if (rooms_[i].name == name) { expanded_ = i; break; }
    }
    rebuild();
}

// 2.41.0 T8：凝视档翻图作用域 = 该展室全部照片（房内日期升序，与串列所见同序）。
QStringList PeoplePage::roomPaths(const QString& name) const
{
    for (const Room& r : rooms_) {
        if (r.name != name) continue;
        QStringList out;
        out.reserve(r.photos.size());
        for (const Photo* p : r.photos) out << p->path;
        return out;
    }
    return {};
}

QVector<Room> PeoplePage::buildRooms(const QVector<Photo>& in, bool hidePrivate)
{
    // 反例臂：LA_PEOPLE_QA_FORCE_BROKEN=1 时忽略 hidePrivate（私密照涌入展室）→ 门禁 C 组翻红。
    const bool forceBroken = !qEnvironmentVariableIsEmpty("LA_PEOPLE_QA_FORCE_BROKEN");

    QHash<QString, int> index;            // 姓名 → rooms 下标（保插入序，排序在最后统一做）
    QVector<Room> rooms;
    for (const Photo& p : in) {
        if (p.missing || p.thumbFile.isEmpty()) continue;              // usable：前两条
        if (p.priv && hidePrivate && !forceBroken) continue;           // usable：私密门
        const QStringList names = p.person.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& raw : names) {
            const QString n = raw.trimmed();
            if (n.isEmpty()) continue;                                  // 空姓名防御（"a, ,b" 段）
            int i = index.value(n, -1);
            if (i < 0) {
                Room r;
                r.name = n;
                rooms << r;
                i = rooms.size() - 1;
                index.insert(n, i);
            }
            rooms[i].photos << &p;                                      // 一照多人各入其房
        }
    }

    for (Room& r : rooms) {
        // 输入向量是日期倒序（Library 载入序）→ 房内须升序，故显式 stable_sort（保同日原序）。
        std::stable_sort(r.photos.begin(), r.photos.end(),
                         [](const Photo* a, const Photo* b) { return a->date < b->date; });
        r.count = r.photos.size();
        r.yearMin = 0;
        r.yearMax = 0;
        for (const Photo* p : r.photos) {
            const int y = p->year > 0 ? p->year : p->date.left(4).toInt();
            if (y <= 0) continue;                                       // 年份未知不进跨度（不编造）
            if (r.yearMin == 0 || y < r.yearMin) r.yearMin = y;
            if (y > r.yearMax) r.yearMax = y;
        }
        r.ring = Lumen::personRing(r.name);
    }

    std::stable_sort(rooms.begin(), rooms.end(), [](const Room& a, const Room& b) {
        if (a.count != b.count) return a.count > b.count;                // 人多的展室在前
        return a.name < b.name;                                          // 同级按展签升序
    });
    return rooms;
}

void PeoplePage::rebuild()
{
    delete content_;
    content_ = new QWidget(this);
    content_->setAutoFillBackground(true);
    {
        QPalette pal = content_->palette();
        pal.setColor(QPalette::Window, Lumen::bg());
        content_->setPalette(pal);
    }
    auto* root = static_cast<QVBoxLayout*>(layout());
    while (root->count() > 0) delete root->takeAt(0);
    root->addWidget(content_);

    auto* col = new QVBoxLayout(content_);
    col->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::Xl,
                            Lumen::Space::Xxl, Lumen::Space::Xl);
    col->setSpacing(Lumen::Space::L);

    auto* title = new QLabel(QCoreApplication::translate("LightAlbum", "人物馆"), content_);
    title->setFont(Lumen::Type::display());
    title->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::ink())));
    col->addWidget(title);

    rooms_ = photos_ ? buildRooms(*photos_, hidePrivate_) : QVector<Room>();
    if (rooms_.isEmpty()) {
        col->addWidget(makeEmptyCard(), 0, Qt::AlignTop);
        col->addStretch(1);
        content_->show();
        return;
    }

    auto* grid = new QGridLayout();
    grid->setSpacing(Lumen::Space::M);
    const int cols = 4;
    for (int i = 0; i < rooms_.size(); ++i)
        grid->addWidget(makeRoomBlock(i), i / cols, i % cols, Qt::AlignTop);
    for (int c = 0; c < cols; ++c) grid->setColumnStretch(c, 1);
    col->addLayout(grid);
    col->addStretch(1);
    content_->show();
}

QWidget* PeoplePage::makeRoomBlock(int idx)
{
    const Room& r = rooms_[idx];
    auto* box = new QWidget(content_);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(Lumen::Space::S);

    auto* card = new ClickFrame([this, idx]() {
        expanded_ = (expanded_ == idx) ? -1 : idx;
        rebuild();
    }, content_);
    card->setObjectName(QStringLiteral("room"));
    card->setStyleSheet(QStringLiteral(
        "QFrame#room{background:%1;border:1px solid %2;border-radius:%3px;}")
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Card));
    Lumen::Elev::apply(card, Lumen::Elev::E2);
    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(Lumen::Space::M, Lumen::Space::M,
                          Lumen::Space::M, Lumen::Space::M);
    h->setSpacing(Lumen::Space::M);

    auto* cover = new QLabel(card);
    cover->setFixedSize(72, 72);
    cover->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;")
        .arg(Lumen::hex(Lumen::inset())).arg(Lumen::Radius::Inset));
    if (!r.photos.isEmpty()) {
        const QString tf = r.photos.first()->thumbFile;
        const QPixmap pm = cache_->cached(tf, ThumbCache::kTile);
        if (!pm.isNull())
            cover->setPixmap(pm.scaled(72, 72, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation));
        cache_->request(tf, this, [cover](const QPixmap& p) {
            if (!p.isNull())
                cover->setPixmap(p.scaled(72, 72, Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation));
        }, ThumbCache::kTile);
    }
    h->addWidget(cover);

    auto* info = new QVBoxLayout();
    info->setSpacing(Lumen::Space::Xs);
    auto* nameRow = new QHBoxLayout();
    nameRow->setSpacing(Lumen::Space::S);
    auto* dot = new QLabel(card);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:%2;")
        .arg(Lumen::hex(r.ring), Lumen::Radius::Pill));
    nameRow->addWidget(dot);
    auto* name = new QLabel(r.name, card);
    name->setFont(Lumen::Type::label(true));
    name->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::ink())));
    nameRow->addWidget(name);
    nameRow->addStretch(1);
    info->addLayout(nameRow);

    auto* meta = new QLabel(yearSpan(r), card);
    meta->setFont(Lumen::Type::monoMeta());
    meta->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::muted())));
    info->addWidget(meta);

    auto* cnt = new QLabel(
        QCoreApplication::translate("LightAlbum", "%1 张").arg(r.count), card);
    cnt->setFont(Lumen::Type::smallText());
    cnt->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::faint())));
    info->addWidget(cnt);
    h->addLayout(info, 1);

    v->addWidget(card);
    if (expanded_ == idx) v->addWidget(makeThumbRow(r));
    return box;
}

QWidget* PeoplePage::makeThumbRow(const Room& r)
{
    auto* strip = new QFrame(content_);
    strip->setObjectName(QStringLiteral("strip"));
    strip->setStyleSheet(QStringLiteral(
        "QFrame#strip{background:%1;border:1px solid %2;border-radius:%3px;"
        "padding:%4px;}")
        .arg(Lumen::hex(Lumen::panel()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Card).arg(Lumen::Space::Xs));
    auto* h = new QHBoxLayout(strip);
    h->setContentsMargins(Lumen::Space::S, Lumen::Space::S,
                          Lumen::Space::S, Lumen::Space::S);
    h->setSpacing(Lumen::Space::S);

    const QString person = r.name;
    const int shown = qMin(r.photos.size(), 12);
    for (int i = 0; i < shown; ++i) {
        const Photo* p = r.photos[i];
        auto* btn = new QToolButton(strip);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIconSize(QSize(88, 88));
        btn->setText(p->date.left(10));
        btn->setToolTip(person);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{background:%1;border:1px solid %2;border-radius:%3px;"
            "color:%4;padding:%5px;}")
            .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::line()))
            .arg(Lumen::Radius::Ctl).arg(Lumen::hex(Lumen::muted()))
            .arg(Lumen::Space::Xs));
        const QString path = p->path;
        const QString tf = p->thumbFile;
        const QPixmap pm = cache_->cached(tf, ThumbCache::kTile);
        if (!pm.isNull()) btn->setIcon(QIcon(pm));
        cache_->request(tf, this, [btn](const QPixmap& loaded) {
            if (!loaded.isNull()) btn->setIcon(QIcon(loaded));
        }, ThumbCache::kTile);
        connect(btn, &QToolButton::clicked, this, [this, person, path]() {
            emit openGaze(person, path);
        });
        h->addWidget(btn);
    }
    if (r.photos.size() > shown) {
        auto* more = new QLabel(QStringLiteral("+%1").arg(r.photos.size() - shown), strip);
        more->setFont(Lumen::Type::monoMeta());
        more->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::muted())));
        h->addWidget(more);
    }

    auto* filterBtn = new QToolButton(strip);
    filterBtn->setText(QCoreApplication::translate("LightAlbum", "在图库筛选"));
    filterBtn->setCursor(Qt::PointingHandCursor);
    filterBtn->setStyleSheet(Lumen::btnGhost());
    connect(filterBtn, &QToolButton::clicked, this, [this, person]() {
        emit filterGallery(person);
    });
    h->addWidget(filterBtn);
    h->addStretch(1);
    return strip;
}

QWidget* PeoplePage::makeEmptyCard()
{
    auto* card = new QFrame(content_);
    card->setObjectName(QStringLiteral("empty"));
    card->setStyleSheet(Lumen::kpiCard(Lumen::emerald()));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                          Lumen::Space::Xl, Lumen::Space::L);
    v->setSpacing(Lumen::Space::M);

    auto* title = new QLabel(
        QCoreApplication::translate("LightAlbum", "馆内布展中"), card);
    title->setFont(Lumen::Type::title(false));
    title->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::ink())));
    v->addWidget(title);

    auto* sub = new QLabel(
        QCoreApplication::translate("LightAlbum", "识别照片中的人物，即可为他们各开一间展室。"), card);
    sub->setFont(Lumen::Type::body());
    sub->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::muted())));
    v->addWidget(sub);

    auto* row = new QHBoxLayout();
    auto* btn = new QPushButton(
        QCoreApplication::translate("LightAlbum", "识别人物"), card);
    btn->setStyleSheet(Lumen::btnPrimary());
    connect(btn, &QPushButton::clicked, this, &PeoplePage::clusteringRequested);
    row->addWidget(btn);
    row->addStretch(1);
    v->addLayout(row);
    return card;
}

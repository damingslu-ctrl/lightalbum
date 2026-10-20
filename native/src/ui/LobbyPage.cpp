#include "LobbyPage.h"
#include "ThumbCache.h"
#include "Theme.h"
#include "Icons.h"
#include "../core/CmdFilter.h"   // seasonIndexOf：四季表唯一真源
#include "../core/CurateEngine.h"  // templateIdToken：静音意图的模板 token 唯一真源
#include "../core/MotionDriver.h"  // kExhibitBreath 的唯一 Qt 驱动器
#include "../core/MotionKit.h"
#include "../core/MotionPref.h"    // Lumen::systemReducedMotion

#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtGlobal>
#include <algorithm>

namespace {

// 缩略卡：圆角裁剪 + 悬停描边 + 点击 → onActivated()。
// 用 QPainter 自绘而非 QLabel：圆角裁剪需要 clipPath（QLabel 的圆角会露出直角底）。
class ThumbCard : public QFrame {
public:
    ThumbCard(const QPixmap& pm, const QString& caption, int side,
              std::function<void()> onActivated, QWidget* parent = nullptr)
        : QFrame(parent), pm_(pm), caption_(caption), onActivated_(std::move(onActivated))
    {
        setFixedSize(side, side + (caption_.isEmpty() ? 0 : 22));
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    void setPixmap(const QPixmap& pm) { pm_ = pm; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF img(0, 0, width(), width());
        QPainterPath clip;
        clip.addRoundedRect(img, Lumen::Radius::Card, Lumen::Radius::Card);
        p.setClipPath(clip);
        if (!pm_.isNull()) {
            const QPixmap scaled = pm_.scaled(img.size().toSize(),
                                              Qt::KeepAspectRatioByExpanding,
                                              Qt::SmoothTransformation);
            p.drawPixmap(img.toRect(), scaled,
                         QRect((scaled.width() - img.width()) / 2,
                               (scaled.height() - img.height()) / 2,
                               int(img.width()), int(img.height())));
        } else {
            p.fillRect(img, Lumen::inset());
        }
        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(hover_ ? Lumen::gold() : Lumen::line(), 1));
        p.drawRoundedRect(img.adjusted(0.5, 0.5, -0.5, -0.5),
                          Lumen::Radius::Card, Lumen::Radius::Card);
        if (!caption_.isEmpty()) {
            p.setFont(Lumen::Type::smallText());
            p.setPen(Lumen::muted());
            p.drawText(QRectF(2, width() + 2, width() - 4, 18),
                       Qt::AlignLeft | Qt::AlignVCenter, caption_);
        }
    }
    void enterEvent(QEnterEvent*) override { hover_ = true;  update(); }
    void leaveEvent(QEvent*)      override { hover_ = false; update(); }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && onActivated_) onActivated_();
    }

private:
    QPixmap pm_;
    QString caption_;
    bool hover_ = false;
    std::function<void()> onActivated_;
};

// 今日一展卡：E4 开馆层的底与 1px 金描边由 Lumen::Elev 的 QSS 给出（本类先落基类
// paintEvent），这里只叠「呼吸微光」那一层——α 上限引用 Elev::kInnerGlowMax（V4 §五
// 光晕纪律 ≤10%），数值由 Motion::Driver(kExhibitBreath) 推入，本类不含任何曲线/时长知识。
class ExhibitCard : public QFrame {
public:
    explicit ExhibitCard(QWidget* parent = nullptr) : QFrame(parent) {}

    void setBreath(double v)
    {
        breath_ = qBound(0.0, v, 1.0);
        setProperty("laBreath", breath_);   // 落点即门禁读数：不为测试另设探针
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QFrame::paintEvent(e);
        if (breath_ <= 0.0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor g = Lumen::gold();
        g.setAlpha(int(255.0 * Lumen::Elev::kInnerGlowMax * breath_));
        p.setPen(QPen(g, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(1.0, 1.0, width() - 2.0, height() - 2.0),
                          Lumen::Radius::Feature, Lumen::Radius::Feature);
    }

private:
    double breath_ = 0.0;
};

} // namespace

namespace {

// 可用性铁律（铁律 21 注入点过滤）：缺失 / 无缩略图 / 私密（且当前要求隐藏）一律不进展。
// forceBroken 仅供门禁反例臂（LA_LOBBY_QA_FORCE_BROKEN）模拟「漏掉私密过滤」的失效态。
bool usable(const Photo& p, bool hidePrivate, bool forceBroken)
{
    if (p.missing) return false;
    if (p.thumbFile.isEmpty()) return false;
    if (p.priv && hidePrivate && !forceBroken) return false;
    return true;
}

// 日期解析链与 StoryGen.cpp 同源：主格式 "yyyy-MM-dd"，退化 "yyyy:MM:dd"，
// 再退化完整时间串。解不出 = 无效日期（不参与任何入展判定，不编造年份）。
QDate parsePhotoDate(const Photo& p)
{
    const QString s = p.date.trimmed();
    if (s.isEmpty()) return QDate();
    QDate d = QDate::fromString(s.left(10), QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid()) d = QDate::fromString(s.left(10), QStringLiteral("yyyy:MM:dd"));
    if (!d.isValid()) d = QDate::fromString(s, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return d;
}

// 四季划分委派 core/CmdFilter.h::seasonIndexOf（3-5 春 / 6-8 夏 / 9-11 秋 / 12-2 冬）。
// 本地包装保留调用点不动；调用点只在 QDate::isValid() 之后取月份，故 -1 分支不可达。
int seasonOf(int month) { return seasonIndexOf(month); }

} // namespace

LobbyPage::LobbyPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    cache_ = new ThumbCache(thumbsDir_, this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setAutoFillBackground(true);
    rebuild();
}

void LobbyPage::setHidePrivate(bool hide)
{
    if (hidePrivate_ == hide) return;
    hidePrivate_ = hide;
    refresh();
}

void LobbyPage::refresh() { rebuild(); }

QString LobbyPage::buildCardText(const Curate::Exhibit& e)
{
    if (!e.valid) return QString();
    const QString title = e.theme.title.trimmed();
    if (title.isEmpty()) return QString();          // 缺源不编造（R7）：无展名即无文案
    const QString blurb = e.theme.blurb.trimmed();
    return blurb.isEmpty() ? title : title + QStringLiteral(" · ") + blurb;
}

void LobbyPage::setExhibit(const Curate::Exhibit& e)
{
    // 同展不重建：换一次展就重放一遍呼吸、重排一次缩略图请求，refresh 汇合时会抖。
    // 身份 = 有效位 + 模板 + 文案 + 帧序 + 主图（任一项变了都必须重建）。
    const bool sameExhibit = e.valid == exhibit_.valid
                             && e.theme.id == exhibit_.theme.id
                             && e.theme.primaryPath == exhibit_.theme.primaryPath
                             && e.theme.paths == exhibit_.theme.paths
                             && buildCardText(e) == buildCardText(exhibit_);
    exhibit_ = e;
    if (!sameExhibit) refresh();
}

QVector<const Photo*> LobbyPage::onThisDay(const QVector<Photo>& in, const QDate& today,
                                           bool hidePrivate, int maxN)
{
    QVector<const Photo*> out;
    if (!today.isValid() || maxN <= 0) return out;

    // 反例臂：LA_LOBBY_QA_FORCE_BROKEN=1 时跳过私密过滤（门禁 E 组据此翻红）。
    const bool forceBroken = !qEnvironmentVariableIsEmpty("LA_LOBBY_QA_FORCE_BROKEN");

    // 一次遍历分桶（日期只解一次），三个池天然为递增超集：
    // 当日 ⊆ 同月 ⊆ 同季。
    struct Cand { const Photo* p; QDate d; };
    QVector<Cand> dayPool, monthPool, seasonPool;
    const int m = today.month(), dd = today.day();
    const int sea = seasonOf(m);
    const bool leapFold = (m == 2 && dd == 28);   // 平年 2-28 并入 2-29（闰日照不落空）
    for (const Photo& p : in) {
        if (!usable(p, hidePrivate, forceBroken)) continue;
        const QDate pd = parsePhotoDate(p);
        if (!pd.isValid()) continue;
        if (pd.month() == m) {
            if (pd.day() == dd || (leapFold && pd.day() == 29))
                dayPool << Cand{&p, pd};
            monthPool << Cand{&p, pd};
        } else if (seasonOf(pd.month()) == sea) {
            seasonPool << Cand{&p, pd};
        }
    }

    // 回退链：取第一个「够 3 张」的池；全都不够则给同季（稀薄结果，可为空，
    // 绝不回填假数据——空态由 UI 演成「今日无展 · 明日再会」）。
    const QVector<Cand>* pool = &seasonPool;
    if (dayPool.size() >= 3)        pool = &dayPool;
    else if (monthPool.size() >= 3) pool = &monthPool;

    QVector<Cand> ranked = *pool;
    std::stable_sort(ranked.begin(), ranked.end(), [&today](const Cand& a, const Cand& b) {
        const int da = qAbs(a.d.year() - today.year());
        const int db = qAbs(b.d.year() - today.year());
        if (da != db) return da < db;              // 近年在先
        if (a.p->rating != b.p->rating) return a.p->rating > b.p->rating;   // 同级按评分
        return a.p->date > b.p->date;              // 再同级按日期串倒序（ISO 串序即时间序）
    });

    out.reserve(qMin(ranked.size(), maxN));
    for (const Cand& c : ranked) {
        if (out.size() >= maxN) break;
        out << c.p;
    }
    return out;
}

QVector<const Photo*> LobbyPage::recent(const QVector<Photo>& in,
                                        bool hidePrivate, int maxN)
{
    // in 已按 date ISO 串倒序（Library 载入后即此序）；此处只做过滤 + 头取，
    // 不重排——重排会引入第二份「排序真源」，与库序漂移。
    QVector<const Photo*> out;
    if (maxN <= 0) return out;
    for (const Photo& p : in) {
        if (out.size() >= maxN) break;
        if (usable(p, hidePrivate, false)) out << &p;
    }
    return out;
}

void LobbyPage::rebuild()
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
    while (root->count() > 0) {
        QLayoutItem* it = root->takeAt(0);
        delete it;
    }
    root->addWidget(content_);

    auto* col = new QVBoxLayout(content_);
    col->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::Xl,
                            Lumen::Space::Xxl, Lumen::Space::Xl);
    col->setSpacing(Lumen::Space::Xl);

    if (photos_ && !photos_->isEmpty()) {
        curateCard_ = makeCurateCard();      // 无展（冷启动/缺源/频控）→ 整卡不出现
        if (curateCard_) col->addWidget(curateCard_);
        heroCard_ = makeHeroCard();
        col->addWidget(heroCard_);
        recentStrip_ = makeThumbStrip();
        col->addWidget(recentStrip_);
    } else {
        emptyGuide_ = makeEmptyGuide();
        col->addWidget(emptyGuide_, 0, Qt::AlignTop);
    }
    col->addWidget(makeCommandBar());
    col->addStretch(1);
    content_->show();
}

QWidget* LobbyPage::makeCurateCard()
{
    // 三重「不出现」门槛（spec §2.4 / 铁律 21）：无效展、文案拼不出、主图不在库——
    // 任一条成立就返回 nullptr，rebuild() 不 addWidget，厅里就不会立起一块空壳。
    if (!exhibit_.valid) return nullptr;
    const QString copy = buildCardText(exhibit_);
    if (copy.isEmpty()) return nullptr;

    const Curate::Theme& th = exhibit_.theme;
    const Photo* primary = nullptr;
    if (photos_) {
        for (const Photo& p : *photos_) {
            if (p.path == th.primaryPath) { primary = &p; break; }
        }
    }
    if (!primary) return nullptr;

    auto* card = new ExhibitCard(content_);
    card->setObjectName(QStringLiteral("curateCard"));
    card->setAccessibleName(copy);        // 读屏与门禁读同一句（buildCardText 唯一真源）
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QStringLiteral("QWidget#curateCard{border-radius:%1px;}")
                            .arg(Lumen::Radius::Feature));
    Lumen::Elev::apply(card, Lumen::Elev::E4);   // 全项目第一张 E4「开馆」卡：金描边 + 内发光

    auto* h = new QHBoxLayout(card);
    h->setContentsMargins(Lumen::Space::Xl, Lumen::Space::Xl,
                          Lumen::Space::Xl, Lumen::Space::Xl);
    h->setSpacing(Lumen::Space::Xl);

    auto* v = new QVBoxLayout();
    v->setSpacing(Lumen::Space::S);

    auto* kicker = new QLabel(
        QCoreApplication::translate("LightAlbum", "今日一展 · 馆长选题"), card);
    kicker->setFont(Lumen::Type::label());
    kicker->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                              .arg(Lumen::hex(Lumen::gold())));
    v->addWidget(kicker);

    auto* title = new QLabel(th.title, card);
    title->setObjectName(QStringLiteral("curateTitle"));
    title->setFont(Lumen::Type::display());
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                              .arg(Lumen::hex(Lumen::ink())));
    v->addWidget(title);

    if (!th.blurb.trimmed().isEmpty()) {
        auto* blurb = new QLabel(th.blurb, card);
        blurb->setObjectName(QStringLiteral("curateBlurb"));
        blurb->setFont(Lumen::Type::body());
        blurb->setWordWrap(true);
        blurb->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                                  .arg(Lumen::hex(Lumen::muted())));
        v->addWidget(blurb);
    }

    // 三手势 = 三个「意图」，本类绝不写库；落库由 MainWindow 接线（spec §2.1）。
    auto gesture = [card](const char* name, const char* iconKind, const char* text) {
        auto* b = new QPushButton(QCoreApplication::translate("LightAlbum", text), card);
        b->setObjectName(QString::fromLatin1(name));
        b->setIcon(Icons::make(QLatin1String(iconKind), Lumen::gold(), 24));
        b->setStyleSheet(Lumen::btnGhost(Lumen::gold()));
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto* acts = new QHBoxLayout();
    acts->setSpacing(Lumen::Space::M);
    auto* keep = gesture("curateKeep", "star", "收进合集");
    auto* swap = gesture("curateSwap", "swap", "换一展");
    auto* mute = gesture("curateMute", "reject", "别再这类");
    acts->addWidget(keep);
    acts->addWidget(swap);
    acts->addWidget(mute);
    acts->addStretch(1);
    v->addSpacing(Lumen::Space::S);
    v->addLayout(acts);
    connect(keep, &QPushButton::clicked, this, [this, th]() {
        emit keepExhibit(th.paths, exhibit_.theme.title);
    });
    connect(swap, &QPushButton::clicked, this, &LobbyPage::swapExhibit);
    connect(mute, &QPushButton::clicked, this, [this, th]() {
        emit muteTemplate(QString::fromLatin1(Curate::templateIdToken(th.id)));
    });

    v->addStretch(1);
    h->addLayout(v, 1);

    // 主图即「开展」入口：点它进凝视策展序列（整展帧序，不是单张）。
    QWidget* hero = makeThumbCard(
        *primary, 168, true, [this, th]() {
            emit openCuration(th.paths, exhibit_.theme.title, exhibit_.theme.blurb);
        });
    hero->setObjectName(QStringLiteral("curateHero"));
    h->addWidget(hero, 0, Qt::AlignTop);

    // 呼吸微光一次成展只播一遍（不是常驻循环）：数值全出自引擎预设，UI 零曲线知识。
    // LA_GATE / 截图 / 系统减动效 → Driver 走终帧支路，卡片静置即终值态，像素门确定性。
    const bool anim = qEnvironmentVariableIsEmpty("LA_GATE")
                      && qEnvironmentVariableIsEmpty("LA_UI_SHOT")
                      && !Lumen::systemReducedMotion();
    auto* drv = new Motion::Driver(Motion::kExhibitBreath, !anim, card);
    connect(drv, &Motion::Driver::valueFor, card, [card](const QByteArray& prop, double v) {
        if (prop == Motion::kPropGlow) card->setBreath(v);
    });
    drv->start();
    return card;
}

QWidget* LobbyPage::makeHeroCard()
{
    auto* card = new QFrame(content_);
    card->setObjectName(QStringLiteral("hero"));
    card->setStyleSheet(Lumen::heroBand());
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                          Lumen::Space::Xl, Lumen::Space::Xl);
    v->setSpacing(Lumen::Space::M);

    const QDate today = QDate::currentDate();
    auto* title = new QLabel(QCoreApplication::translate("LightAlbum", "今日展"), card);
    title->setFont(Lumen::Type::display());
    title->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::ink())));
    v->addWidget(title);

    // 一排 7 幅是硬最小宽算出来的：7*148 + 6*12 + 卡内边距 48 + 页边距 64 + 导航 74
    // ≈ 1300。再多一幅就把窗口最小宽顶过 1440，窄屏（1366 笔电）下 QHBoxLayout 会把
    // 定尺寸卡压扁、日期图注互相叠字（LA_UI_SHOT 800 宽实测）。
    const QVector<const Photo*> picks = onThisDay(*photos_, today, hidePrivate_, 7);
    if (picks.isEmpty()) {
        auto* none = new QLabel(
            QCoreApplication::translate("LightAlbum", "今日无展 · 明日再会"), card);
        none->setFont(Lumen::Type::body());
        none->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::muted())));
        v->addWidget(none);
        return card;
    }

    auto* row = new QHBoxLayout();
    row->setSpacing(Lumen::Space::M);
    for (const Photo* p : picks) {
        row->addWidget(makeThumbCard(*p, 148, true));
    }
    row->addStretch(1);
    v->addLayout(row);
    return card;
}

QWidget* LobbyPage::makeThumbStrip()
{
    auto* wrap = new QWidget(content_);
    auto* v = new QVBoxLayout(wrap);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(Lumen::Space::M);

    auto* head = new QLabel(QCoreApplication::translate("LightAlbum", "最新到馆"), wrap);
    head->setFont(Lumen::Type::title(false));
    head->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::ink())));
    v->addWidget(head);

    // 同一条排宽度纪律：8*116 + 7*12 + 页边距 64 ≈ 1076，不成为约束项。
    const QVector<const Photo*> rows = recent(*photos_, hidePrivate_, 8);
    if (rows.isEmpty()) {
        auto* none = new QLabel(
            QCoreApplication::translate("LightAlbum", "暂无新到馆藏"), wrap);
        none->setFont(Lumen::Type::body());
        none->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(Lumen::hex(Lumen::muted())));
        v->addWidget(none);
        return wrap;
    }

    auto* row = new QHBoxLayout();
    row->setSpacing(Lumen::Space::M);
    for (const Photo* p : rows) {
        row->addWidget(makeThumbCard(*p, 116, false));
    }
    row->addStretch(1);
    v->addLayout(row);
    return wrap;
}

QWidget* LobbyPage::makeThumbCard(const Photo& p, int sidePx, bool feature,
                                  std::function<void()> onActivated)
{
    const QString path = p.path;
    const QString caption = p.date.left(10);
    const QString thumb = p.thumbFile;
    const int lv = feature ? ThumbCache::kFeature : ThumbCache::kTile;

    // 默认意图 = 单点开凝视；今日一展卡传入自己的回调（整展序列），不再另造一套缩略卡。
    if (!onActivated) onActivated = [this, path]() { emit openGaze(path); };

    // 命中即同步给图；未命中由后台加载完成后回调填充（ctx=this 生命周期守卫）。
    QPixmap pm = cache_->cached(thumb, lv);
    auto* card = new ThumbCard(pm, caption, sidePx, std::move(onActivated), content_);
    cache_->request(thumb, this, [card](const QPixmap& loaded) {
        if (!loaded.isNull()) card->setPixmap(loaded);
    }, lv);
    return card;
}

QWidget* LobbyPage::makeCommandBar()
{
    // 2.41.0 T8：玻璃指令栏 = ⌘K 入口（不能只当装饰）。整条做成 QPushButton，
    // 子 QLabel 全部透传鼠标，点击才落在按钮本体上而不是落在标签上没反应。
    auto* bar = new QPushButton(content_);
    bar->setObjectName(QStringLiteral("cmdbar"));
    bar->setCursor(Qt::PointingHandCursor);
    bar->setStyleSheet(QStringLiteral(
        "QPushButton#cmdbar{background:%1;border:1px solid %2;"
        "border-radius:%3px;text-align:left;}")
        .arg(Lumen::hex(Lumen::panel()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Feature));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(Lumen::Space::L, Lumen::Space::S,
                          Lumen::Space::L, Lumen::Space::S);
    h->setSpacing(Lumen::Space::M);

    auto* hint = new QLabel(
        QCoreApplication::translate("LightAlbum", "搜索、跳转、问问馆藏…"), bar);
    hint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hint->setFont(Lumen::Type::body());
    hint->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::faint())));
    h->addWidget(hint);
    h->addStretch(1);

    auto* key = new QLabel(QStringLiteral("Ctrl+K"), bar);
    key->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    key->setFont(Lumen::Type::monoMeta());
    key->setStyleSheet(QStringLiteral(
        "background:%1;border:1px solid %2;border-radius:%3px;"
        "padding:2px 8px;color:%4;")
        .arg(Lumen::hex(Lumen::inset()), Lumen::hex(Lumen::line()))
        .arg(Lumen::Radius::Inset)
        .arg(Lumen::hex(Lumen::muted())));
    h->addWidget(key);
    // QPushButton::minimumSizeHint() 出自样式而非子布局：无文字按钮只给到 19px，
    // 会把内嵌布局的 34px 裁掉（LA_UI_SHOT 实测图注被拦腰切）。把布局最小高交还按钮。
    bar->setMinimumHeight(h->totalMinimumSize().height());
    connect(bar, &QPushButton::clicked, this, &LobbyPage::commandRequested);
    return bar;
}

QWidget* LobbyPage::makeEmptyGuide()
{
    auto* card = new QFrame(content_);
    card->setObjectName(QStringLiteral("empty"));
    card->setStyleSheet(Lumen::kpiCard(Lumen::emerald()));
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                          Lumen::Space::Xl, Lumen::Space::L);
    v->setSpacing(Lumen::Space::M);

    auto* title = new QLabel(
        QCoreApplication::translate("LightAlbum", "馆内尚无藏品"), card);
    title->setFont(Lumen::Type::title(false));
    title->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::ink())));
    v->addWidget(title);

    auto* sub = new QLabel(
        QCoreApplication::translate("LightAlbum", "导入一个目录或 iCloud 相册，开始你的第一场展览。"), card);
    sub->setFont(Lumen::Type::body());
    sub->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::muted())));
    v->addWidget(sub);

    auto* row = new QHBoxLayout();
    row->setSpacing(Lumen::Space::M);
    auto* dirBtn = new QPushButton(
        QCoreApplication::translate("LightAlbum", "导入目录"), card);
    dirBtn->setStyleSheet(Lumen::btnPrimary());
    connect(dirBtn, &QPushButton::clicked, this, &LobbyPage::importRequested);
    row->addWidget(dirBtn);

    auto* icloudBtn = new QPushButton(
        QCoreApplication::translate("LightAlbum", "导入 iCloud 相册"), card);
    icloudBtn->setStyleSheet(Lumen::btnGhost());
    connect(icloudBtn, &QPushButton::clicked, this, &LobbyPage::icloudImportRequested);
    row->addWidget(icloudBtn);

    row->addStretch(1);
    v->addLayout(row);
    return card;
}

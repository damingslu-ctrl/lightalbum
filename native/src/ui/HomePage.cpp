// ============================================================================
// P17 沉浸式首页「Quiet Frame」实现。
// 版式语言来自用户参考视频（黑底 / 衬线大标题 / 左侧模糊渐变章节面板 /
// 全出血横滑轮播 / 底部分段进度），内容全部绑定本机真实库（数据诚实铁律）。
// ============================================================================
#include "HomePage.h"
#include "Theme.h"
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QMouseEvent>
#include <QTimer>
#include <QFrame>
#include <QKeyEvent>
#include <QPainter>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <cstdio>

namespace {

// ---- 小工具 ---------------------------------------------------------------
QString pad2(int n) { return QString::number(n).rightJustified(2, QLatin1Char('0')); }

QString titleCase(const QString& s)
{
    if (s.isEmpty()) return s;
    return s.at(0).toUpper() + s.mid(1);
}

// 省英文名瘦身："Guangdong Province" → "Guangdong"；"Beijing Municipality" → "Beijing"
QString provShort(const QString& prov)
{
    QString s = prov;
    if (s.endsWith(QStringLiteral(" Province"), Qt::CaseInsensitive))
        s.chop(QString(QStringLiteral(" Province")).size());
    if (s.endsWith(QStringLiteral(" Municipality"), Qt::CaseInsensitive))
        s.chop(QString(QStringLiteral(" Municipality")).size());
    return s;
}

// 封面铺满裁切（KeepAspectRatioByExpanding 居中）
QPixmap coverScaled(const QString& path, const QSize& sz)
{
    QImage img(path);
    if (img.isNull() || sz.isEmpty()) return QPixmap();
    QPixmap scaled = QPixmap::fromImage(
        img.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    QPixmap out(sz);
    out.fill(QColor(0x06, 0x08, 0x06));
    QPainter p(&out);
    p.drawPixmap((sz.width() - scaled.width()) / 2,
                 (sz.height() - scaled.height()) / 2, scaled);
    return out;
}

// 细箭头（左右切图 / 章节翻页共用气质）
QToolButton* makeArrowBtn(QWidget* parent, const QString& ch)
{
    auto* b = new QToolButton(parent);
    b->setText(ch);
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedSize(34, 34);
    b->setStyleSheet(QStringLiteral(
        "QToolButton{background:rgba(6,8,6,120);border:none;border-radius:17px;"
        "color:rgba(244,247,244,190);font-size:26px;font-weight:bold;}"
        "QToolButton:hover{background:rgba(6,8,6,190);color:#d9a94e;}"));
    return b;
}

QToolButton* makeTextBtn(QWidget* parent, const QString& text, int px)
{
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:none;color:#b9b3a6;"
        "font-size:%1px;}"
        "QToolButton:hover{color:#d9a94e;}"
        "QToolButton:disabled{color:#4a534b;}").arg(px));
    return b;
}

// ============================================================================
// 左侧章节面板：当前章封面 → 两级缩放模糊打底 + 纵向压暗 scrim + 右缘发丝线。
// ============================================================================
class ChapterPanel : public QWidget {
public:
    explicit ChapterPanel(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedWidth(300);
    }
    void setBackdrop(const QString& thumbPath)
    {
        small_ = QImage();
        if (!thumbPath.isEmpty() && QFileInfo::exists(thumbPath)) {
            QImage img(thumbPath);
            if (!img.isNull()) {
                // 两级缩放 = 廉价高斯：缩到高 24px，paint 时再平滑放大，色彩保留、细节化开
                small_ = img.scaledToHeight(24, Qt::SmoothTransformation);
            }
        }
        update();
    }
protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        update();   // paintEvent 按当前尺寸重画，无需重解码
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x08, 0x0b, 0x09));
        if (!small_.isNull()) {
            const QSize sz = size();
            QPixmap scaled = QPixmap::fromImage(
                small_.scaled(sz, Qt::KeepAspectRatioByExpanding,
                              Qt::SmoothTransformation));
            p.setOpacity(0.62);
            p.drawPixmap((sz.width() - scaled.width()) / 2,
                         (sz.height() - scaled.height()) / 2, scaled);
            p.setOpacity(1.0);
        }
        // 纵向 scrim：上部轻（留出照片氛围），下部深（保证底部文字可读）
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0.0, QColor(6, 8, 6, 70));
        g.setColorAt(0.55, QColor(6, 8, 6, 125));
        g.setColorAt(1.0, QColor(6, 8, 6, 215));
        p.fillRect(rect(), g);
        p.fillRect(width() - 1, 0, 1, height(), QColor(0x26, 0x31, 0x2a));
    }
private:
    QImage small_;   // 24px 高的低清底图（paint 时按面板尺寸放大 = 模糊）
};

// ============================================================================
// 主区轮播「Stage」：全出血双缓冲横滑 + 细箭头 + 01/N 计数 + 城市角标。
// ============================================================================
class Stage : public QWidget {
    Q_OBJECT
public:
    explicit Stage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("homeStage"));
        imgA_ = new QLabel(this); imgB_ = new QLabel(this);
        for (QLabel* l : {imgA_, imgB_}) {
            l->setAlignment(Qt::AlignCenter);
            l->setAttribute(Qt::WA_TransparentForMouseEvents);   // 点击穿透到 Stage
            l->setGraphicsEffect(new QGraphicsOpacityEffect(l));
            l->hide();
        }
        prevBtn_ = makeArrowBtn(this, QStringLiteral("\u2039"));
        nextBtn_ = makeArrowBtn(this, QStringLiteral("\u203A"));
        connect(prevBtn_, &QToolButton::clicked, this, [this] { prev(); });
        connect(nextBtn_, &QToolButton::clicked, this, [this] { next(); });
        counter_ = new QLabel(this);
        counter_->setStyleSheet(QStringLiteral(
            "QLabel{background:rgba(6,8,6,120);border-radius:9px;padding:2px 9px;"
            "color:rgba(244,247,244,225);font-size:11px;letter-spacing:1px;}"));
        tag_ = new QLabel(this);
        tag_->setStyleSheet(QStringLiteral(
            "QLabel{background:rgba(6,8,6,120);border-radius:8px;padding:2px 8px;"
            "color:rgba(244,247,244,215);font-size:10px;letter-spacing:3px;}"));
    }

    void setPhotos(const QVector<const Photo*>& photos, const QString& thumbsDir)
    {
        photos_ = photos;
        thumbsDir_ = thumbsDir;
        idx_ = pickStartIndex();
        shown_ = 0;
        imgA_->hide(); imgB_->hide();
        showIndex(idx_, 1, false);
    }
    void setAnimations(bool on) { anim_ = on; }
    void setTag(const QString& t) { tag_->setText(t.toUpper()); }
    int  count() const { return photos_.size(); }
    const Photo* current() const
    { return photos_.isEmpty() ? nullptr : photos_.at(idx_); }

    void next() { step(1); }
    void prev() { step(-1); }

signals:
    void indexChanged(int idx, int total);
    void activated(const QString& path);

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        relayout();
        rerenderCurrent();
    }
    void showEvent(QShowEvent* e) override
    {
        QWidget::showEvent(e);
        // 首次 resizeEvent 发生在 show 之前（子标签 isVisible()==false 无法据其判断），
        // 尺寸此后不再变化 → 必须在 show 时补一次全尺寸重渲，否则小图一直挂着。
        rerenderCurrent();
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        // 点图（避开箭头区域）→ 打开灯箱；箭头自身处理点击不会进到这里
        if (const Photo* p = current()) emit activated(p->path);
        QWidget::mousePressEvent(e);
    }
private:
    // 开场起点：日期倒序的前 24 张候选里挑第一张「中央区亮像素占比 ≥ 30%」的
    // （均值会被暗房亮屏/小亮斑骗过，占比更接近观感）；全暗则诚实回退第 0 张。
    // 只挑起点，不改序列与计数（01/N 仍是真实章内序）。
    int pickStartIndex() const
    {
        const bool dbg = qEnvironmentVariableIsSet("LA_HOME_DEBUG");
        const int n = qMin(24, photos_.size());
        for (int i = 0; i < n; ++i) {
            QImage img(QDir(thumbsDir_).filePath(photos_.at(i)->thumbFile));
            if (img.isNull()) continue;
            img = img.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (img.isNull()) continue;
            const QRect core = img.rect().adjusted(8, 8, -8, -8);   // 中央 1/2 区
            if (core.isEmpty()) continue;
            long long bright = 0, cnt = 0;
            for (int y = core.top(); y <= core.bottom(); ++y) {
                const QRgb* ln = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = core.left(); x <= core.right(); ++x) {
                    if (qGray(ln[x]) >= 90) ++bright;
                    ++cnt;
                }
            }
            const int frac = cnt > 0 ? int(bright * 100 / cnt) : 0;
            if (dbg)
                std::fprintf(stderr, "LA_HOME_DEBUG cand=%d frac=%d thumb=%s\n",
                             i, frac, qPrintable(photos_.at(i)->thumbFile));
            if (frac >= 30) return i;
        }
        return 0;
    }
    void step(int dir)
    {
        if (photos_.isEmpty()) return;
        const int n = photos_.size();
        idx_ = (idx_ + dir + n) % n;
        showIndex(idx_, dir, anim_);
    }
    QRect imgRect() const { return rect(); }
    void relayout()
    {
        const QRect r = imgRect();
        imgA_->setGeometry(r); imgB_->setGeometry(r);
        prevBtn_->move(14, (height() - prevBtn_->height()) / 2);
        nextBtn_->move(width() - 14 - nextBtn_->width(),
                       (height() - nextBtn_->height()) / 2);
        counter_->adjustSize();
        counter_->move(20, height() - counter_->height() - 16);
        tag_->adjustSize();
        tag_->move(width() - tag_->width() - 20, height() - tag_->height() - 16);
    }
    void rerenderCurrent()
    {
        if (photos_.isEmpty()) return;
        QLabel* cur = (shown_ == 0) ? imgA_ : imgB_;
        // 不以 isVisible() 为门槛：首次 resizeEvent 发生在 show 之前，
        // 此时子标签不可见但正需要全尺寸重渲（小图挂到 show 后的根因）。
        cur->setPixmap(coverScaled(
            QDir(thumbsDir_).filePath(photos_.at(idx_)->thumbFile), size()));
        cur->setGeometry(imgRect());
    }
    void showIndex(int idx, int dir, bool animate)
    {
        if (idx < 0 || idx >= photos_.size()) {
            counter_->setText(QString());
            tag_->setText(QString());
            return;
        }
        QLabel* incoming = (shown_ == 0) ? imgB_ : imgA_;
        QLabel* outgoing = (shown_ == 0) ? imgA_ : imgB_;
        QGraphicsOpacityEffect* ie =
            qobject_cast<QGraphicsOpacityEffect*>(incoming->graphicsEffect());
        incoming->setPixmap(coverScaled(
            QDir(thumbsDir_).filePath(photos_.at(idx)->thumbFile), size()));
        incoming->setGeometry(imgRect());
        incoming->show();
        incoming->raise();
        // HUD（箭头/计数/角标）永远压在照片层之上
        prevBtn_->raise();
        nextBtn_->raise();
        counter_->raise();
        tag_->raise();
        counter_->setText(QStringLiteral("%1 / %2")
                              .arg(pad2(idx + 1), pad2(photos_.size())));
        emit indexChanged(idx, photos_.size());
        if (!animate) {
            if (ie) ie->setOpacity(1.0);
            outgoing->hide();
            shown_ = (shown_ == 0) ? 1 : 0;
            return;
        }
        // 横滑：入图从 dir 侧滑入并盖住静置的出图（只动 pos，出图不必动）
        const QRect r = imgRect();
        const int w = r.width();
        const QPoint dst = r.topLeft();
        incoming->move(dir > 0 ? dst.x() + w : dst.x() - w, dst.y());
        auto* slide = new QPropertyAnimation(incoming, "pos");
        slide->setDuration(420);
        slide->setStartValue(incoming->pos());
        slide->setEndValue(dst);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        connect(slide, &QPropertyAnimation::finished, this, [outgoing] {
            outgoing->hide();
        });
        slide->start(QAbstractAnimation::DeleteWhenStopped);
        shown_ = (shown_ == 0) ? 1 : 0;
    }
    QVector<const Photo*> photos_;
    QString  thumbsDir_;
    QLabel *imgA_ = nullptr, *imgB_ = nullptr;
    QToolButton *prevBtn_ = nullptr, *nextBtn_ = nullptr;
    QLabel *counter_ = nullptr, *tag_ = nullptr;
    int idx_ = 0;
    int shown_ = 0;      // 双缓冲：0 → 用 A 显 / B 入
    bool anim_ = true;
};

// ============================================================================
// 底部分段进度条：≤48 张逐张分段（当前金色），更多则连续比例填充（诚实比例）。
// ============================================================================
class SegBar : public QWidget {
public:
    explicit SegBar(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(20); }
    void set(int count, int idx) { n_ = count; i_ = idx; update(); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        if (n_ <= 0) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const int m = 22;
        const int w = width() - m * 2;
        const int y = (height() - 3) / 2;
        const QColor gold(0xd9, 0xa9, 0x4e);
        const QColor dim(0x33, 0x3a, 0x33);
        if (n_ <= 48) {
            const int gap = 4;
            const double sw = double(w - (n_ - 1) * gap) / n_;
            double x = m;
            for (int i = 0; i < n_; ++i) {
                p.fillRect(QRectF(x, y, sw, 3), i == i_ ? gold : dim);
                x += sw + gap;
            }
        } else {
            p.fillRect(m, y, w, 3, dim);
            const int fill = int(double(w) * (i_ + 1) / n_);
            p.fillRect(m, y, fill, 3, gold);
        }
    }
private:
    int n_ = 0, i_ = 0;
};

} // namespace

// ============================================================================
// 章节（纯逻辑）：定位照片按 city→prov→region 兜底分组，按数量取前 N 章。
// 跳过 priv / missing / 无缩略图 / 无定位；计数与年份全为真实值。
// ============================================================================
QVector<HomePage::Chapter> HomePage::buildChapters(const QVector<Photo>& photos,
                                                   int maxChapters)
{
    QHash<QString, int> indexOf;         // key → 章节序（保持首见顺序做稳定 tie-break）
    QVector<Chapter> order;
    for (const Photo& p : photos) {
        if (p.priv || p.missing || p.thumbFile.isEmpty()) continue;
        const QString key = !p.city.isEmpty() ? QStringLiteral("c:") + p.city
                          : !p.prov.isEmpty() ? QStringLiteral("p:") + p.prov
                          : !p.region.isEmpty() ? QStringLiteral("r:") + p.region
                          : QString();
        if (key.isEmpty()) continue;     // 无定位 → 不进章节（诚实，不猜）
        int row = indexOf.value(key, -1);
        if (row < 0) {
            row = order.size();
            indexOf.insert(key, row);
            Chapter c;
            c.city = p.city; c.prov = p.prov; c.region = p.region;
            order.append(c);
        }
        order[row].photos.append(&p);
    }

    QVector<int> bySize;
    for (int i = 0; i < order.size(); ++i) bySize.append(i);
    std::stable_sort(bySize.begin(), bySize.end(),
                     [&order](int a, int b) {
                         return order.at(a).photos.size() > order.at(b).photos.size();
                     });
    if (bySize.size() > maxChapters) bySize.resize(maxChapters);

    QVector<Chapter> out;
    for (int k = 0; k < bySize.size(); ++k) {
        Chapter c = order.at(bySize.at(k));
        c.startNo = k + 1;
        // 章内按日期倒序（最新在前 = 封面）
        std::stable_sort(c.photos.begin(), c.photos.end(),
                         [](const Photo* a, const Photo* b) { return a->date > b->date; });
        // 年份范围 + 总量（含未定位同章照片不存在——组键即定位，天然一致）
        c.yearMin = 9999; c.yearMax = 0;
        QHash<QString, int> catCnt;
        for (const Photo* ph : c.photos) {
            if (ph->year > 0) {
                c.yearMin = std::min(c.yearMin, ph->year);
                c.yearMax = std::max(c.yearMax, ph->year);
            }
            if (!ph->category.isEmpty()) catCnt.insert(ph->category, catCnt.value(ph->category) + 1);
        }
        if (c.yearMin == 9999) c.yearMin = c.yearMax = 0;
        // 关键词：真实 CLIP 分类 top ≤3
        QVector<QPair<QString, int>> cats;
        for (auto it = catCnt.cbegin(); it != catCnt.cend(); ++it) cats.append({it.key(), it.value()});
        std::stable_sort(cats.begin(), cats.end(),
                         [](const QPair<QString,int>& a, const QPair<QString,int>& b) {
                             return a.second > b.second;
                         });
        for (int i = 0; i < cats.size() && i < 3; ++i) c.keywords.append(cats.at(i).first);

        // 展示串（兜底链：city → prov 瘦身 → region 首字母大写；绝不编造）
        const QString ps = provShort(c.prov);
        if (!c.city.isEmpty())             c.title = c.city;
        else if (!ps.isEmpty())            c.title = ps;
        else                               c.title = titleCase(c.region);

        QStringList caps;
        caps << QStringLiteral("CHAPTER %1").arg(pad2(c.startNo));
        if (!c.prov.isEmpty()) caps << c.prov.toUpper();
        else if (!c.region.isEmpty()) caps << c.region.toUpper();
        c.capsLine = caps.join(QStringLiteral(" · "));

        c.subtitle = c.yearMax > 0
            ? (c.yearMin == c.yearMax
                   ? QStringLiteral("%1 · %2 幅").arg(c.yearMin).arg(c.photos.size())
                   : QStringLiteral("%1 — %2 · %3 幅").arg(c.yearMin).arg(c.yearMax).arg(c.photos.size()))
            : QStringLiteral("%1 幅").arg(c.photos.size());

        const QString metaPlace = !c.city.isEmpty() ? c.city
                                : !ps.isEmpty()     ? ps
                                                    : c.region;
        c.metaLine = QStringLiteral("%1 · %2 · ROLL %3")
                         .arg(c.yearMax > 0 ? QString::number(c.yearMax) : QStringLiteral("—"),
                              metaPlace.toUpper(), pad2(c.startNo));
        out.append(c);
    }
    return out;
}

// ============================================================================
// HomePage 本体
// ============================================================================
HomePage::HomePage(Library* lib, const QString& thumbsDir, QWidget* parent)
    : QWidget(parent), lib_(lib), thumbsDir_(thumbsDir)
{
    setObjectName(QStringLiteral("homePage"));
    setStyleSheet(QStringLiteral("QWidget#homePage{background:#060806;}"));
    setFocusPolicy(Qt::StrongFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    chapters_ = buildChapters(lib_ ? lib_->photos() : QVector<Photo>(), 12);

    // ---- 顶部条（衬线 logo + 文字导航 + 品牌圆章）----
    topBar_ = new QWidget(this);
    topBar_->setObjectName(QStringLiteral("homeTopBar"));
    topBar_->setFixedHeight(56);
    topBar_->setStyleSheet(QStringLiteral(
        "#homeTopBar{background:#060806;border-bottom:1px solid #161d18;}"));
    auto* tl = new QHBoxLayout(topBar_);
    tl->setContentsMargins(24, 0, 20, 0);
    tl->setSpacing(16);
    {
        auto* logoBox = new QWidget(topBar_);
        auto* lv = new QVBoxLayout(logoBox);
        lv->setContentsMargins(0, 6, 0, 6);
        lv->setSpacing(0);
        auto* logo = new QLabel(QStringLiteral("光影集"), logoBox);
        logo->setFont(Lumen::serifFont(14, QFont::DemiBold, 3.0));
        logo->setStyleSheet(QStringLiteral("color:#f4f7f4;background:transparent;"));
        auto* en = new QLabel(QStringLiteral("QUIET FRAME · LIGHTALBUM"), logoBox);
        en->setFont(Lumen::serifFont(6.8, QFont::Normal, 2.4));
        en->setStyleSheet(QStringLiteral("color:#d9a94e;background:transparent;"));
        lv->addWidget(logo);
        lv->addWidget(en);
        tl->addWidget(logoBox);
    }
    tl->addStretch(1);
    {
        static const char* kHomeNav[] = {"首页", "足迹", "图库", "选片", "修图", "日志", "关于"};
        for (int i = 0; i < int(sizeof(kHomeNav) / sizeof(kHomeNav[0])); ++i) {
            auto* b = makeTextBtn(topBar_, QString::fromUtf8(kHomeNav[i]), 12);
            connect(b, &QToolButton::clicked, this, [this, i] {
                if (i != 0) emit navigateTo(i);
            });
            tl->addWidget(b);
            navTexts_.append(b);
        }
        // 首页永远处于当前态（其他页由全局 chrome 接管导航）
        navTexts_.at(0)->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:none;color:#d9a94e;"
            "font-size:12px;font-weight:bold;}"));
    }
    {
        auto* avatar = new QLabel(QStringLiteral("光"), topBar_);
        avatar->setFixedSize(30, 30);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFont(Lumen::serifFont(12, QFont::DemiBold));
        avatar->setStyleSheet(QStringLiteral(
            "QLabel{background:#1a231d;border:1px solid rgba(217,169,78,140);"
            "border-radius:15px;color:#d9a94e;}"));
        tl->addWidget(avatar);
    }
        root->addWidget(topBar_);
        topBar_->hide();   // P29-H 纯沉浸：首页顶栏字标/导航条整体隐藏，仅留左栏 + 翻页箭头

    if (chapters_.isEmpty()) {
        // 空库（无可定位作品）：诚实的空态 + 导入入口
        auto* empty = new QWidget(this);
        auto* ev = new QVBoxLayout(empty);
        ev->setAlignment(Qt::AlignCenter);
        ev->setSpacing(14);
        auto* t = new QLabel(QStringLiteral("光影集"), empty);
        t->setFont(Lumen::serifFont(30, QFont::DemiBold, 8.0));
        t->setStyleSheet(QStringLiteral("color:#f4f7f4;"));
        t->setAlignment(Qt::AlignCenter);
        auto* s = new QLabel(
            QStringLiteral("导入目录后，首页会按城市分章、全屏轮播你的作品。\n"
                           "（当前库中没有可定位的可见照片）"), empty);
        s->setStyleSheet(QStringLiteral("color:#8da396;"));
        s->setAlignment(Qt::AlignCenter);
        auto* imp = new QPushButton(QStringLiteral("导入目录"), empty);
        imp->setCursor(Qt::PointingHandCursor);
        imp->setStyleSheet(Lumen::btnGold());
        connect(imp, &QPushButton::clicked, this, &HomePage::importRequested);
        ev->addWidget(t);
        ev->addWidget(s);
        ev->addWidget(imp, 0, Qt::AlignHCenter);
        root->addWidget(empty, 1);
        return;
    }

    // ---- 主体：左章节面板 + 右全出血轮播 ----
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    panel_ = new ChapterPanel(this);
    body->addWidget(panel_);

    {
        // 面板内容（margins 由外层 VBox 撑）
        auto* pv = new QVBoxLayout(panel_);
        pv->setContentsMargins(30, 26, 24, 18);
        pv->setSpacing(0);
        auto* index = new QLabel(QStringLiteral("←  INDEX"), panel_);
        index->setFont(Lumen::serifFont(9, QFont::Normal, 3.0));
        index->setStyleSheet(QStringLiteral("color:#b9b3a6;background:transparent;"));
        pv->addWidget(index);
        pv->addSpacing(60);

        capsLbl_ = new QLabel(panel_);
        capsLbl_->setFont(Lumen::serifFont(8.5, QFont::Normal, 2.2));
        capsLbl_->setStyleSheet(QStringLiteral("color:#cfc9bd;background:transparent;"));
        capsLbl_->setWordWrap(true);
        pv->addWidget(capsLbl_);
        pv->addSpacing(10);

        titleLbl_ = new QLabel(panel_);
        titleLbl_->setFont(Lumen::serifFont(34, QFont::DemiBold, 1.0));
        titleLbl_->setStyleSheet(QStringLiteral("color:#f4f7f4;background:transparent;"));
        titleLbl_->setWordWrap(true);
        pv->addWidget(titleLbl_);
        pv->addSpacing(14);

        goldRule_ = new QFrame(panel_);
        goldRule_->setFixedSize(34, 2);
        goldRule_->setStyleSheet(QStringLiteral("background:#d9a94e;border:none;"));
        pv->addWidget(goldRule_);
        pv->addSpacing(12);

        subtitleLbl_ = new QLabel(panel_);
        subtitleLbl_->setStyleSheet(QStringLiteral(
            "color:#e8e4da;background:transparent;font-size:12px;"));
        pv->addWidget(subtitleLbl_);
        pv->addSpacing(6);
        keywordLbl_ = new QLabel(panel_);
        keywordLbl_->setStyleSheet(QStringLiteral(
            "color:#98a29a;background:transparent;font-size:11px;"));
        keywordLbl_->setWordWrap(true);
        pv->addWidget(keywordLbl_);

        pv->addStretch(1);

        grid_ = new QWidget(panel_);
        pv->addWidget(grid_);
        pv->addSpacing(10);
        coverLbl_ = new QLabel(panel_);
        coverLbl_->setStyleSheet(QStringLiteral(
            "color:#6d7a6f;background:transparent;font-size:9px;"));
        pv->addWidget(coverLbl_);
        pv->addSpacing(6);
        metaLbl_ = new QLabel(panel_);
        metaLbl_->setFont(Lumen::serifFont(9, QFont::Normal, 2.0));
        metaLbl_->setStyleSheet(QStringLiteral("color:#8d8878;background:transparent;"));
        pv->addWidget(metaLbl_);
        pv->addSpacing(14);
        auto* fRow = new QHBoxLayout;
        fRow->setSpacing(12);
        prevChapBtn_ = makeTextBtn(panel_, QStringLiteral("←  上一章"), 11);
        nextChapBtn_ = makeTextBtn(panel_, QStringLiteral("下一章  →"), 11);
        connect(prevChapBtn_, &QToolButton::clicked, this,
                [this] { setChapter((chapterIdx_ - 1 + chapters_.size()) % chapters_.size()); });
        connect(nextChapBtn_, &QToolButton::clicked, this,
                [this] { setChapter((chapterIdx_ + 1) % chapters_.size()); });
        fRow->addWidget(prevChapBtn_);
        fRow->addStretch(1);
        fRow->addWidget(nextChapBtn_);
        pv->addLayout(fRow);
    }

    stage_ = new Stage(this);
    body->addWidget(stage_, 1);
    root->addLayout(body, 1);

    // ---- 底部分段进度 ----
    segBar_ = new SegBar(this);
    root->addWidget(segBar_);

    // ---- 章节号网格（每章一个号牌，当前金色）----
    {
        auto* gl = new QGridLayout(grid_);
        gl->setContentsMargins(0, 0, 0, 0);
        gl->setHorizontalSpacing(6);
        gl->setVerticalSpacing(6);
        for (int i = 0; i < chapters_.size(); ++i) {
            auto* b = new QToolButton(grid_);
            b->setText(pad2(i + 1));
            b->setFixedSize(32, 22);
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QToolButton::clicked, this, [this, i] { setChapter(i); });
            gl->addWidget(b, i / 6, i % 6);
            gridBtns_.append(b);
        }
    }

    auto* stage = static_cast<Stage*>(stage_);
    connect(stage, &Stage::indexChanged, this, &HomePage::stageChanged);
    connect(stage, &Stage::activated, this, [this](const QString& path) {
        emit photoActivated(path);
    });

    autoTimer_ = new QTimer(this);
    autoTimer_->setInterval(5000);
    connect(autoTimer_, &QTimer::timeout, this, [stage] { stage->next(); });

    setChapter(0, false);
}

void HomePage::setAnimations(bool on)
{
    anim_ = on;
    if (stage_) static_cast<Stage*>(stage_)->setAnimations(on);
    if (!on && autoTimer_) autoTimer_->stop();
}

void HomePage::setChapter(int idx, bool animateStage)
{
    if (chapters_.isEmpty()) return;
    idx = qBound(0, idx, chapters_.size() - 1);
    chapterIdx_ = idx;
    const Chapter& c = chapters_.at(idx);
    stageChanged(0, 0);   // 占位：进度条先清，等 Stage 信号覆盖
    static_cast<Stage*>(stage_)->setAnimations(anim_);
    static_cast<Stage*>(stage_)->setPhotos(c.photos, thumbsDir_);
    refreshPanel();
}

void HomePage::refreshPanel()
{
    if (chapters_.isEmpty()) return;
    const Chapter& c = chapters_.at(chapterIdx_);

    capsLbl_->setText(c.capsLine);
    titleLbl_->setText(c.title);
    subtitleLbl_->setText(c.subtitle);
    keywordLbl_->setText(c.keywords.isEmpty()
        ? QString() : c.keywords.join(QStringLiteral(" · ")));
    metaLbl_->setText(c.metaLine);
    // 定位覆盖诚实标注：可见且带定位的照片比例（全库口径）
    {
        int located = 0, usable = 0;
        if (lib_) {
            for (const Photo& p : lib_->photos()) {
                if (p.priv || p.missing || p.thumbFile.isEmpty()) continue;
                ++usable;
                if (!p.city.isEmpty() || !p.prov.isEmpty() || !p.region.isEmpty())
                    ++located;
            }
        }
        coverLbl_->setText(QStringLiteral("定位覆盖 %1 / %2 幅 · 共 %3 章")
                               .arg(located).arg(usable).arg(chapters_.size()));
    }
    // 封面打底：与 Stage 同一张开场照（亮度择优后的起点，非盲取最新一张）
    QString cover;
    if (stage_) {
        if (const Photo* cur = static_cast<Stage*>(stage_)->current())
            cover = QDir(thumbsDir_).filePath(cur->thumbFile);
        static_cast<Stage*>(stage_)->setTag(c.city.isEmpty() ? c.title : c.city);
    }
    static_cast<ChapterPanel*>(panel_)->setBackdrop(cover);

    // 网格高亮
    for (int i = 0; i < gridBtns_.size(); ++i) {
        gridBtns_.at(i)->setStyleSheet(i == chapterIdx_
            ? QStringLiteral("QToolButton{background:rgba(217,169,78,30);"
                             "border:1px solid #d9a94e;border-radius:4px;"
                             "color:#d9a94e;font-size:10px;}")
            : QStringLiteral("QToolButton{background:transparent;border:none;"
                             "border-radius:4px;color:#7d887f;font-size:10px;}"
                             "QToolButton:hover{color:#f4f7f4;}"));
    }
    prevChapBtn_->setEnabled(chapters_.size() > 1);
    nextChapBtn_->setEnabled(chapters_.size() > 1);
}

void HomePage::stageChanged(int idx, int total)
{
    static_cast<SegBar*>(segBar_)->set(total, idx);
}

void HomePage::rebuild()
{
    // 保留给未来「导入完成后的整体刷新」；当前页持有库快照，构造期一次性建好。
}

void HomePage::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
}

void HomePage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (autoTimer_ && anim_ && chapters_.size() > 0 && stage_
        && static_cast<Stage*>(stage_)->count() > 1)
        autoTimer_->start();
}

void HomePage::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if (autoTimer_) autoTimer_->stop();
}

void HomePage::keyPressEvent(QKeyEvent* e)
{
    auto* stage = stage_ ? static_cast<Stage*>(stage_) : nullptr;
    switch (e->key()) {
    case Qt::Key_Left:  if (stage) stage->prev(); e->accept(); return;
    case Qt::Key_Right: if (stage) stage->next(); e->accept(); return;
    case Qt::Key_Up:
        if (!chapters_.isEmpty())
            setChapter((chapterIdx_ - 1 + chapters_.size()) % chapters_.size());
        e->accept(); return;
    case Qt::Key_Down:
        if (!chapters_.isEmpty())
            setChapter((chapterIdx_ + 1) % chapters_.size());
        e->accept(); return;
    default:
        QWidget::keyPressEvent(e);
    }
}

#include "HomePage.moc"

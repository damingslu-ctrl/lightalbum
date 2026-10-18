#include "GazeLightbox.h"
#include "../core/Library.h"
#include "../core/MotionKit.h"     // 2.41.0 §2.5 换展动画（kLightbox）
#include "../core/MotionDriver.h"  // 2.41.0 §2.5 唯一 Qt 驱动器
#include "../core/MotionPref.h"    // Lumen::systemReducedMotion
#include "FullCache.h"
#include <QLabel>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QVariantAnimation>
#include <QPropertyAnimation>
#include <QPointer>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QShowEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QLayout>
#include <algorithm>

// T6 零行为迁出：以下全部定义自 MainWindow.cpp（P15 起）逐字搬运，仅类名改
// PhotoLightbox → GazeLightbox。

namespace {
// 2.41.0 §2.5 换展淡入时长（spec 明文 120ms const）。
constexpr int kExhibitFadeMs = 120;
// 主色采样：缩略图边长（spec 明文 64px）与饱和度门槛（spec 明文 >0.25）。
constexpr int    kDominantSamplePx  = 64;
constexpr double kDominantSatMin    = 0.25;
constexpr double kDominantFallbackL = 0.90;   // 低饱和回退 = emerald 亮度 -10%
} // namespace

// 2.41.0 §2.5 主色纯函数（铁律 20：产品光晕与 LA_GAZE_QA 同一实现）。
// 反例臂（铁律 35）：LA_GAZE_QA_FORCE_BROKEN=1 时**旁路低饱和回退** —— 无达标样本
// 时不再落 emerald 暗化，而把全样本中位数原样返回（灰图 → 灰，非主色）→ B 组必红。
QColor GazeLightbox::dominantColor(const QImage& thumb)
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_GAZE_QA_FORCE_BROKEN");
    auto fallback = []() {
        QColor e = Lumen::emerald();
        int h = 0, s = 0, l = 0, a = 0;
        e.getHsl(&h, &s, &l, &a);
        return QColor::fromHsl(h, s, int(l * kDominantFallbackL + 0.5), a);
    };
    if (thumb.isNull()) return fallback();

    const QImage im = thumb.scaled(kDominantSamplePx, kDominantSamplePx,
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_ARGB32);
    const int w = im.width(), hgt = im.height();
    if (w <= 0 || hgt <= 0) return fallback();
    const double cx = w / 2.0, cy = hgt / 2.0;
    const double innerR = qMin(w, hgt) / 4.0;    // 中心 1/2 半径

    QVector<QColor> samples;
    samples.reserve(w * hgt);
    for (int y = 0; y < hgt; ++y) {
        for (int x = 0; x < w; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const bool inner = (dx * dx + dy * dy) <= innerR * innerR;
            if (!inner && ((x % 2) != 0 || (y % 2) != 0)) continue;   // 外圈每 2px 采样
            samples.append(QColor::fromRgba(im.pixel(x, y)));
        }
    }
    if (samples.isEmpty()) return broken ? QColor(Qt::black) : fallback();

    // 达标样本：HSL 饱和度 > 0.25（qFuzzyCompare 排序，中位数取样）。
    QVector<QColor> hot;
    for (const QColor& c : samples)
        if (c.toHsl().saturationF() > kDominantSatMin) hot.append(c);
    if (hot.isEmpty())
        return broken ? samples.at(samples.size() / 2) : fallback();

    std::sort(hot.begin(), hot.end(), [](const QColor& a, const QColor& b) {
        const QColor ha = a.toHsl(), hb = b.toHsl();
        if (!qFuzzyCompare(ha.saturationF() + 1.0, hb.saturationF() + 1.0))
            return ha.saturationF() < hb.saturationF();
        if (!qFuzzyCompare(ha.lightnessF()  + 1.0, hb.lightnessF()  + 1.0))
            return ha.lightnessF() < hb.lightnessF();
        if (!qFuzzyCompare(ha.hueF()        + 1.0, hb.hueF()        + 1.0))
            return ha.hueF() < hb.hueF();
        return false;
    });
    return hot.at(hot.size() / 2);
}

void GazeLightbox::setFullCache(FullCache* fc) { full_ = fc; }
FullCache* GazeLightbox::fullCache() const { return full_; }

GazeLightbox::GazeLightbox(Library* lib, QWidget* parent)
    : QWidget(parent), lib_(lib)
{
    setObjectName(QStringLiteral("photoLightbox"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QWidget#photoLightbox{background:rgba(6,8,6,0.97);}"));
    setFocusPolicy(Qt::StrongFocus);   // F3 a11y：灯箱可键盘聚焦（←/→ 翻图）

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
    bl->setContentsMargins(Lumen::Space::Xxl, Lumen::Space::S, Lumen::Space::Xl, Lumen::Space::L);
    bl->setSpacing(16);
    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(Lumen::Space::Xs);
    title_ = new QLabel(bottom_);
    title_->setStyleSheet((QString::fromUtf8(
        "font-family:'Noto Serif SC','STSong','SimSun',serif;")
        + Lumen::Type::css(Lumen::Type::Tier::Title)   // T8：18px 衬线标题归 T2(20px)；衬线=编辑特权 §2.3
        + QString::fromUtf8(
        "font-weight:bold;color:%1;"))                 // T8 §2.4：600 归一 bold 三档
        .arg(Lumen::hex(Lumen::ink())));
    meta_ = new QLabel(bottom_);
    meta_->setStyleSheet(QString::fromUtf8("color:%1;")
        .arg(Lumen::hex(Lumen::muted()))   // §1.7：9db2a5→MUTED（次文字归级）
        + Lumen::Type::css(Lumen::Type::Tier::Small)   // T8：11.5px→T5(12px)
        + QStringLiteral("letter-spacing:1px;"));      // §2.4：ls 1–3px 属 T5 小号合法场景，保留
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
    cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(Lumen::Space::M);
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

void GazeLightbox::setPhoto(const Photo* ph, const QString& thumbsDir)
{
    const QString prevPath = path_;          // 换展判据（Gaze 档动画用）
    path_ = ph->path;
    thumbsDir_ = thumbsDir;   // F3 a11y：翻图复用
    changed_ = false;
    // 2.22.0 原图：先立即用已有缩略图出图（**不阻塞、秒出**），
    // 再异步解码原图，解完淡入替换。解码失败（HEIC 样本实测 3 张里 2 张
    // decoded=NULL）就保持缩略图 —— 诚实降级，绝不空白。
    orig_ = QPixmap(QDir(thumbsDir).filePath(ph->thumbFile));
    hasFull_ = false;
    layoutImage();
    // 2.41.0 §2.5：凝视档的环境光晕按**缩略图**主色算（不落库、不写设置）。
    if (mode_ == Mode::Gaze)
        applyGlow(dominantColor(orig_.toImage()));
    if (full_) {
        QPointer<GazeLightbox> self(this);
        full_->request(ph->path, this, [self](const QImage& full) {
            if (self.isNull()) return;              // 生命周期守卫
            if (full.isNull()) return;              // 失败：保持缩略图（诚实降级）
            self->orig_ = QPixmap::fromImage(full);
            self->hasFull_ = true;
            self->fadeSwapIn();                     // 淡入替换，不闪
        });
    }

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
        sb->setToolTip(QCoreApplication::translate("LightAlbum", "评分 %1").arg(i + 1));
        connect(sb, &QToolButton::clicked, this, [this, i] {
            if (!lib_) return;
            const int nr = (curRating_ == i + 1) ? 0 : i + 1;
            lib_->setRating(path_, nr);
            curRating_ = nr;
            changed_ = true;
            setStars(nr);
            emit ratingChanged(path_, nr);   // 2.41.0 §2.5：观测信号（T8 可改读型刷新）
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
        ph->priv ? QCoreApplication::translate("LightAlbum", "🔓 已私密") : QCoreApplication::translate("LightAlbum", "🔒 私密"), ctl_);
    privBtn->setCursor(Qt::PointingHandCursor);
    privBtn->setStyleSheet(Lumen::btnGhost());
    connect(privBtn, &QPushButton::clicked, this, [this, privBtn] {
        if (!lib_) return;
        const bool on = !privBtn->property("privOn").toBool();
        lib_->setPrivate(path_, on);
        privBtn->setProperty("privOn", on);
        privBtn->setText(on ? QCoreApplication::translate("LightAlbum", "🔓 已私密") : QCoreApplication::translate("LightAlbum", "🔒 私密"));
        changed_ = true;
    });
    privBtn->setProperty("privOn", ph->priv);
    auto* open = new QPushButton(QCoreApplication::translate("LightAlbum", "修图台 →"), ctl_);
    open->setCursor(Qt::PointingHandCursor);
    open->setStyleSheet(Lumen::btnPrimary());
    connect(open, &QPushButton::clicked, this,
            [this, path = ph->path] { emit openInRetouch(path); });
    auto* close = new QPushButton(QCoreApplication::translate("LightAlbum", "关闭"), ctl_);
    close->setCursor(Qt::PointingHandCursor);
    close->setStyleSheet(Lumen::btnGhost());
    connect(close, &QPushButton::clicked, this, &GazeLightbox::close);
    cl->addWidget(privBtn); cl->addWidget(open); cl->addWidget(close);

    // 2.41.0 §2.5：凝视档「换展」——只在真的换了一张时才动（首装不闪）。
    if (mode_ == Mode::Gaze && !prevPath.isEmpty() && prevPath != path_)
        startExhibitAnim();
}

// 2.41.0 §2.5 入馆：Browse 零行为；Gaze 叠环境光晕（无边框形态本就成立）。
void GazeLightbox::enter(Mode m, const QString& startPath)
{
    // QA 取证通道（与 LA_LIGHTBOX_HOVER 同一既有先例）：offscreen 截图探针没有快捷键，
    //   让 LA_LIGHTBOX_GAZE 把入馆档位直接抬到「凝视」，好让光晕/雾条形态可被 grab 取证。
    if (qEnvironmentVariableIsSet("LA_LIGHTBOX_GAZE")) m = Mode::Gaze;
    const bool wasGaze = (mode_ == Mode::Gaze);
    mode_ = m;
    if (m == Mode::Gaze && !wasGaze) {
        // 主色由 setPhoto 算（此处仅在已有图时补算一次，防 enter 先于 setPhoto）
        if (!orig_.isNull()) applyGlow(dominantColor(orig_.toImage()));
    } else if (m == Mode::Browse && wasGaze) {
        // 退回浏览：黑幕复位，光晕撤场
        setStyleSheet(QStringLiteral("QWidget#photoLightbox{background:rgba(6,8,6,0.97);}"));
    }
    if (!startPath.isEmpty() && lib_) {
        const Photo* ph = lib_->find(startPath);
        if (ph && startPath != path_) setPhoto(ph, thumbsDir_);
    }
}

void GazeLightbox::showEvent(QShowEvent* e) {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    QWidget::showEvent(e);
    setFocus();   // F3 a11y：灯箱获得键盘焦点后支持 ←/→ 翻图
}

void GazeLightbox::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { e->accept(); close(); }
    else if (e->key() == Qt::Key_Left)  { navigate(-1); e->accept(); }
    else if (e->key() == Qt::Key_Right) { navigate( 1); e->accept(); }
    // 2.41.0 §2.5 凝视键位（只在 Gaze 档生效，Browse 档保持零行为）：
    //   S=喜欢 / A|Delete=略过 / L=封存。**每次按键即发** —— 写库即回，不做批量缓冲。
    else if (mode_ == Mode::Gaze && e->key() == Qt::Key_S) { handleGazeMark(QStringLiteral("like")); e->accept(); }
    else if (mode_ == Mode::Gaze
             && (e->key() == Qt::Key_A || e->key() == Qt::Key_Delete)) { handleGazeMark(QStringLiteral("skip")); e->accept(); }
    else if (mode_ == Mode::Gaze && e->key() == Qt::Key_L) { handleGazeMark(QStringLiteral("lock")); e->accept(); }
    else QWidget::keyPressEvent(e);
}

// 2.41.0 §2.5：S/A/L 即写标。同键在 toast 期内再按 = 撤销（发空 mark → 主窗 setMark(path,"")）。
void GazeLightbox::handleGazeMark(const QString& mark)
{
    if (path_.isEmpty()) return;
    const bool undo = (toastTimer_ && toastTimer_->isActive()
                       && mark == pendingMark_ && path_ == pendingPath_);
    if (undo) {
        pendingMark_.clear();
        pendingPath_.clear();
        if (toastTimer_) toastTimer_->stop();
        emit markRequested(path_, QString());
        showToast(QCoreApplication::translate("LightAlbum", "已撤销"));
    } else {
        pendingMark_ = mark;
        pendingPath_ = path_;
        emit markRequested(path_, mark);
        const QString label = mark == QLatin1String("like")   ? QCoreApplication::translate("LightAlbum", "喜欢")
                            : mark == QLatin1String("skip")   ? QCoreApplication::translate("LightAlbum", "略过")
                                                              : QCoreApplication::translate("LightAlbum", "封存");
        showToast(QCoreApplication::translate("LightAlbum", "已标记 %1 · 再按同键撤销").arg(label));
    }
}

// 2.41.0 §2.5：底部 toast（3s 自动收起；收起时关掉撤销窗口）。
void GazeLightbox::showToast(const QString& text)
{
    if (!toast_) {
        toast_ = new QLabel(this);
        toast_->setObjectName(QStringLiteral("gazeToast"));
        toast_->setAlignment(Qt::AlignCenter);
        toast_->setStyleSheet(QString::fromUtf8(
            "QLabel#gazeToast{background:rgba(6,8,6,0.86);"
            "border:1px solid %1;color:%2;border-radius:%3;padding:8px 16px;}")
            .arg(Lumen::hex(Lumen::line()))
            .arg(Lumen::hex(Lumen::ink()))
            .arg(Lumen::Radius::Pill)
            + Lumen::Type::css(Lumen::Type::Tier::Small));
        toastTimer_ = new QTimer(this);
        toastTimer_->setSingleShot(true);
        connect(toastTimer_, &QTimer::timeout, this, [this] {
            pendingMark_.clear();
            pendingPath_.clear();
            if (toast_) toast_->hide();
        });
    }
    toast_->setText(text);
    toast_->adjustSize();
    toast_->move((width() - toast_->width()) / 2, height() - 172);
    toast_->raise();
    toast_->show();
    toastTimer_->start(3000);   // spec §2.5：3s
}

// 2.41.0 §2.5：环境光晕 = 影院黑幕换成「主色四缘淡晕」（Lumen::glowCss，alpha ≤10%）。
void GazeLightbox::applyGlow(const QColor& c)
{
    setStyleSheet(Lumen::glowCss(c, 1.0));
}

// 2.41.0 §2.5：换展动画 —— 旧画淡出 120ms（const）+ 新画 kLightbox 入画，≤2 并发。
void GazeLightbox::startExhibitAnim()
{
    // ≤2 并发：起新前若已有 2 个活跃 Driver，停最旧。
    // Motion::Driver 无 stop() —— 删 Driver 即销毁其子 QVariantAnimation（唯一停法）。
    while (drivers_.size() >= 2) {
        Motion::Driver* old = drivers_.takeFirst();
        if (old) old->deleteLater();
    }

    // 新画入画：淡入 120ms（const 时长）。
    auto* eff = new QGraphicsOpacityEffect(img_);
    eff->setOpacity(0.0);
    img_->setGraphicsEffect(eff);
    auto* in = new QPropertyAnimation(eff, "opacity", this);
    in->setDuration(kExhibitFadeMs);
    in->setStartValue(0.0);
    in->setEndValue(1.0);
    in->setEasingCurve(QEasingCurve::OutCubic);
    connect(in, &QPropertyAnimation::finished, this, [this] {
        if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(img_->graphicsEffect()))
            e->setEnabled(false);   // 静置恢复无损原生渲染
    });
    in->start(QAbstractAnimation::DeleteWhenStopped);

    auto* drv = new Motion::Driver(Motion::kLightbox, Lumen::systemReducedMotion(), this);
    connect(drv, &Motion::Driver::valueFor, this, [this](const QByteArray& prop, double v) {
        if (prop == Motion::kPropOffsetX) img_->move(int(-v * 28), 0);   // 横滑轻推入画
    });
    connect(drv, &Motion::Driver::finishedSignal, this, [this, drv] {
        img_->move(0, 0);
        drivers_.removeOne(drv);
        drv->deleteLater();
    });
    drivers_.append(drv);
    drv->start();
}

// P20：满幅 img_ + 底部黑幕跟随窗口尺寸（黑幕常驻最上层）
void GazeLightbox::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    img_->setGeometry(rect());
    bottom_->setGeometry(0, height() - 120, width(), 120);
    bottom_->raise();
    layoutImage();
}

// P20：鼠标入幕显形 / 离幕收暗（从当前透明度出发，中途反向平滑折返）
bool GazeLightbox::eventFilter(QObject* obj, QEvent* ev) {
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

void GazeLightbox::closeEvent(QCloseEvent* e) {
    QWidget::closeEvent(e);
    // 2.41.0 §2.5：dirty 随关灯上报（凝视档 S/A/L 已即写库，故此处只管评分/私密）。
    emit dismissed(changed_);
}

void GazeLightbox::clearLayout(QLayout* lay) {
    if (!lay) return;
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
}

// P16：灯箱内改动过评分/私密 → 关闭后由 MainWindow 刷新图库
bool GazeLightbox::hasChanges() const { return changed_; }
void GazeLightbox::clearChanges() { changed_ = false; }

void GazeLightbox::setScope(const QStringList& paths)
{
    slots_.clear();
    slotPos_.clear();
    if (!lib_) return;
    auto add = [this](const Photo& p) {
        BrowseSlot s;
        s.path = p.path;
        s.priv = p.priv;
        slotPos_.insert(s.path, int(slots_.size()));
        slots_.append(s);
    };
    const QVector<Photo>& ps = lib_->photos();
    if (paths.isEmpty()) {
        slots_.reserve(ps.size());
        for (const Photo& p : ps) add(p);
    } else {
        slots_.reserve(paths.size());
        for (const QString& path : paths) {
            const Photo* ph = lib_->find(path);
            if (ph) add(*ph);      // 作用域里的路径必须真实存在于库
        }
    }
}

// P16 隐私门：由 MainWindow 依 vaultUnlocked_ 下发（锁定态翻图须跳过私密照）
void GazeLightbox::setHidePrivate(bool hide) { hidePrivate_ = hide; }

void GazeLightbox::navigate(int delta) {
    if (!lib_) return;
    // 判定本体在 BrowseScope::nextVisible（纯函数，与 LA_PRIVACY_QA 同源）
    const int nx = BrowseScope::nextVisible(
        slots_, slotPos_.value(path_, -1), delta, hidePrivate_);
    if (nx < 0) return;
    const Photo* ph = lib_->find(slots_[nx].path);
    if (!ph) return;
    setPhoto(ph, thumbsDir_);
    setFocus();
}

// P16：星级按钮着色（r 颗金星，其余灰）
void GazeLightbox::setStars(int r)
{
    for (int i = 0; i < starBtns_.size(); ++i) {
        starBtns_.at(i)->setStyleSheet(i < r
            ? QString::fromUtf8("QToolButton{background:transparent;border:none;"
                                "color:%1;%2padding:0;}")
              .arg(Lumen::hex(Lumen::gold()))
              .arg(Lumen::Type::css(Lumen::Type::Tier::Head))   // T8：17px 星级就近归 T3(16px)
            : QString::fromUtf8("QToolButton{background:transparent;border:none;"
                                "color:%1;%2padding:0;}")
              .arg(Lumen::hex(Lumen::faint()))   // §1.7：淡出态 3a4a40→FAINT
              .arg(Lumen::Type::css(Lumen::Type::Tier::Head)));
    }
}

// 2.22.0：原图解完后淡入替换，避免"啪"地闪一下。
// 用 opacity 渐入 120ms；不缩放不动几何，纯视觉淡入（最省、最不晕）。
void GazeLightbox::fadeSwapIn()
{
    layoutImage();
    auto* eff = new QGraphicsOpacityEffect(img_);
    eff->setOpacity(0.0);
    img_->setGraphicsEffect(eff);
    auto* a = new QPropertyAnimation(eff, "opacity", this);
    a->setDuration(120);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    connect(a, &QPropertyAnimation::finished, this, [this] {
        if (auto* e = qobject_cast<QGraphicsOpacityEffect*>(img_->graphicsEffect())) {
            e->setEnabled(false);   // 静置恢复无损原生渲染
        }
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

// —— QA 钩子（2.22.0）——
// 当前是否已换成原图（门禁判别器：原图分辨率必远大于 512px 缩略图）
bool GazeLightbox::qaHasFull() const { return hasFull_; }
// 当前显示图的边长（供门禁与缩略图档位对比）
QSize GazeLightbox::qaDisplaySize() const { return orig_.size(); }
FullCache* GazeLightbox::qaFullCache() const { return full_; }

void GazeLightbox::layoutImage()
{
    if (orig_.isNull()) {
        img_->setText(QCoreApplication::translate("LightAlbum", "无缩略图"));
        img_->setStyleSheet(QString::fromUtf8("color:%1;")
            .arg(Lumen::hex(Lumen::muted()))
            + Lumen::Type::css(Lumen::Type::Tier::Body));   // T8：14px 占位文字就近归 T4(13px)
        return;
    }
    const int m = 44;
    const QPixmap sc = orig_.scaled(qMax(360, width() - m), qMax(260, height() - m),
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img_->setPixmap(sc);
}

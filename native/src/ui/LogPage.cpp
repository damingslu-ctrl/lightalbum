#include "LogPage.h"
#include <QCoreApplication>
#include "Theme.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QVector>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <cmath>

// ———————————————————————————————————————————————————————————————————————
// 解析助手（与 web renderStats 口径一致；缺失 EXIF 一律跳过，不硬塞末档）
// ———————————————————————————————————————————————————————————————————————
static double parseApertureNum(const QString& s)
{
    static const QRegularExpression re("(\\d+(?:\\.\\d+)?)");
    const QRegularExpressionMatch m = re.match(s);
    return m.hasMatch() ? m.captured(1).toDouble() : NAN;
}

static double parseShutterSec(const QString& s)
{
    QString t = s.trimmed();
    if (t.isEmpty()) return NAN;
    if (t.contains('/')) {
        QString den = t.section('/', 1);
        den.remove(QRegularExpression("[^0-9.]"));
        const double d = den.toDouble();
        return d > 0 ? 1.0 / d : NAN;
    }
    if (t.contains('s', Qt::CaseInsensitive)) {
        const QString num = t.left(t.indexOf('s', Qt::CaseInsensitive)).trimmed();
        return num.toDouble();   // 秒（1s / 20s / 0.5s）
    }
    return t.toDouble();          // 纯数字 → 秒
}

// ———————————————————————————————————————————————————————————————————————
// BarChart
// ———————————————————————————————————————————————————————————————————————
BarChart::BarChart(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(150);
}

void BarChart::setData(const QStringList& labels, const QList<int>& values, const QColor& color)
{
    labels_ = labels; values_ = values; color_ = color;
    update();
}

void BarChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Qt::transparent);
    const int n = values_.size();
    if (n == 0) return;
    const int pad = 30, baseY = height() - 16, plotH = baseY - 12;
    const int maxV = *std::max_element(values_.begin(), values_.end());
    const int max = maxV < 1 ? 1 : maxV;
    const int gap = 8;
    const int bw = (width() - pad * 2 - gap * (n - 1)) / n;
    if (bw <= 0) return;

    p.setPen(QPen(Lumen::LINE, 1));
    p.drawLine(pad, baseY, width() - pad, baseY);

    QFont vf = p.font(); vf.setPointSizeF(10); vf.setBold(true);
    vf.setFamily(QStringLiteral("Consolas, 'SFMono-Regular', monospace"));   // P30-C 等宽数字
    QFont lf = p.font(); lf.setPointSizeF(9.5);
    for (int i = 0; i < n; ++i) {
        const int v = values_[i];
        const int h = int(double(v) / double(max) * double(plotH));
        const int x = pad + i * (bw + gap);
        const int y = baseY - h;
        p.setPen(Qt::NoPen);
        p.setBrush(color_);
        if (h > 1) p.drawRoundedRect(QRect(x, y, bw, h), 4, 4);
        else p.drawRect(QRect(x, baseY - 1, bw, 1));   // 0 值细线占位
        // 数值（等宽）
        p.setFont(vf); p.setPen(Lumen::INK);
        p.drawText(QRect(x, y - 16, bw, 14), Qt::AlignCenter | Qt::AlignBottom, QString::number(v));
        // 横轴标签
        p.setFont(lf); p.setPen(Lumen::MUTED);
        p.drawText(QRect(x, baseY + 2, bw, 12), Qt::AlignCenter | Qt::AlignTop, labels_[i]);
    }
}

// ———————————————————————————————————————————————————————————————————————
// PieChart
// ———————————————————————————————————————————————————————————————————————
PieChart::PieChart(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(150, 150);
}

void PieChart::setData(const QStringList& keys, const QList<int>& values, const QStringList& colors)
{
    keys_ = keys; values_ = values; colors_ = colors;
    update();
}

void PieChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Qt::transparent);
    const int total = std::accumulate(values_.begin(), values_.end(), 0);
    if (total <= 0) return;
    const int S = qMin(width(), height());
    const int R = S / 2 - 4;
    const QPointF c(width() / 2.0, height() / 2.0);
    double ang = -M_PI / 2.0;
    for (int i = 0; i < values_.size(); ++i) {
        if (values_[i] <= 0) continue;
        const double a2 = ang + double(values_[i]) / double(total) * 2.0 * M_PI;
        const double large = (double(values_[i]) / double(total)) > 0.5 ? 1.0 : 0.0;
        QPainterPath path;
        path.moveTo(c);
        path.arcTo(QRectF(c.x() - R, c.y() - R, 2 * R, 2 * R), -90.0 + ang * 180.0 / M_PI,
                   (a2 - ang) * 180.0 / M_PI);
        path.closeSubpath();
        QColor col = (i < colors_.size()) ? QColor(colors_[i]) : Lumen::EMERALD;
        p.setBrush(col); p.setPen(QPen(Lumen::BG, 1.5));
        p.drawPath(path);
        ang = a2;
    }
}

// ———————————————————————————————————————————————————————————————————————
// LogPage
// ———————————————————————————————————————————————————————————————————————
LogPage::LogPage(const QVector<Photo>* photos, QWidget* parent)
    : QWidget(parent), photos_(photos)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // P30-C：淡网格底
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{background:"
        "repeating-linear-gradient(0deg, rgba(255,255,255,0.02) 0 1px, transparent 1px 26px),"
        "repeating-linear-gradient(90deg, rgba(255,255,255,0.02) 0 1px, transparent 1px 26px),"
        "#060806;border:none;}"));
    outer->addWidget(scroll);

    auto* root = new QWidget(scroll);
    auto* lay = new QVBoxLayout(root);
    lay->setContentsMargins(22, 20, 22, 24);
    lay->setSpacing(12);

    // —— 区块标题 ——
    auto* head = new QWidget(root);
    auto* hl = new QVBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    auto* title = new QLabel(QCoreApplication::translate("LightAlbum", "摄影日志"), head);
    QFont tf = title->font(); tf.setPointSizeF(20); tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 4.0);
    title->setFont(tf);
    title->setStyleSheet("color:#f4f7f4;");
    auto* sub = new QLabel(QString::fromUtf8(
        "基于全部作品的 EXIF 信息，统计你的拍摄参数偏好与创作节律。数据会替你说话。"), head);
    sub->setStyleSheet("color:#8da396;font-size:12px;");
    sub->setWordWrap(true);
    hl->addWidget(title);
    hl->addWidget(sub);
    lay->addWidget(head);

    // —— P30-C 顶栏：筛选 chips（年份 / 机身）+ 导出 CSV ——
    // 2.33 收纳：不再横向滚动，统一「前 8 平铺 + ▾ 溢出下拉」（Lumen::fillChipRow）
    toolbar_ = new QWidget(root);
    auto* tlb = new QHBoxLayout(toolbar_);
    tlb->setContentsMargins(0, 0, 0, 0);
    tlb->setSpacing(8);

    QSet<int> yrs; QSet<QString> cams;
    if (photos_) for (const Photo& p : *photos_) {
        if (p.year > 0) yrs.insert(p.year);
        if (!p.cam.isEmpty()) cams.insert(p.cam);
    }
    for (int y : yrs) yList_.append(QString::number(y));
    for (const QString& c : cams) cList_.append(c);
    std::sort(yList_.begin(), yList_.end());

    chipsHost_ = new QWidget(toolbar_);
    auto* chipsLay = new QHBoxLayout(chipsHost_);
    chipsLay->setContentsMargins(0, 0, 0, 0);
    chipsLay->setSpacing(8);
    tlb->addWidget(chipsHost_, 1);

    exportBtn_ = new QPushButton(QCoreApplication::translate("LightAlbum", "导出 CSV"), toolbar_);
    exportBtn_->setCursor(Qt::PointingHandCursor);
    exportBtn_->setFixedHeight(28);
    exportBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;color:#d9a94e;border:1px solid #c19a4b;"
        "border-radius:8px;padding:0 14px;font-size:12px;}"
        "QPushButton:hover{background:rgba(217,169,78,0.12);}"));
    tlb->addWidget(exportBtn_);

    rebuildFilterChips();
    connect(exportBtn_, &QPushButton::clicked, this, &LogPage::exportCsv);
    lay->addWidget(toolbar_);

    // —— KPI 条 ——
    kpiRow_ = new QWidget(root);
    auto* khl = new QHBoxLayout(kpiRow_);
    khl->setContentsMargins(0, 0, 0, 0);
    khl->setSpacing(8);
    for (int i = 0; i < 6; ++i) {
        auto* lb = new QLabel(kpiRow_);
        khl->addWidget(lb);
    }
    khl->addStretch(1);
    lay->addWidget(kpiRow_);

    // —— 洞察卡（等宽终端风）——
    insight_ = new QLabel(root);
    insight_->setWordWrap(true);
    insight_->setStyleSheet(QStringLiteral(
        "QLabel{background:#0f1512;border:none;border-radius:8px;"
        "padding:12px 16px;color:#cfe6d8;font-family:Consolas,'SFMono-Regular',monospace;"
        "font-size:12px;line-height:1.6;}"));
    lay->addWidget(insight_);

    // —— 图表网格（3 列 × 2 行）——
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    auto makeCard = [&](const QString& t, const QString& hint, QWidget* chart, QWidget* legend = nullptr) {
        auto* card = new QWidget(root);
        // ID 选择器只命中卡片自身；QWidget 类型选择器会级联到全部子控件，
        // 令标题/提示/图表各自长出一个底+描边框——正是「框中框」根因。
        card->setObjectName(QStringLiteral("logChartCard"));
        card->setStyleSheet(QStringLiteral(
            "#logChartCard{background:#0f1512;border:none;border-radius:8px;}"));
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 10, 12, 10);
        cl->setSpacing(6);
        auto* h = new QLabel(t, card);
        QFont hf = h->font(); hf.setPointSizeF(13); hf.setBold(true);
        hf.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
        h->setFont(hf); h->setStyleSheet("color:#f4f7f4;");
        auto* hi = new QLabel(hint, card);
        hi->setStyleSheet("color:#8da396;font-size:11px;");
        cl->addWidget(h);
        cl->addWidget(hi);
        cl->addWidget(chart);
        if (legend) cl->addWidget(legend);
        return card;
    };

    apChart_   = new BarChart(root);
    shChart_   = new BarChart(root);
    isoChart_  = new BarChart(root);
    focChart_  = new BarChart(root);
    gearChart_ = new PieChart(root);
    gearLegend_= new QLabel(root);
    gearLegend_->setWordWrap(true);
    gearLegend_->setStyleSheet(QString::fromUtf8("color:%1;font-size:11px;")
        .arg(Lumen::hex(Lumen::MUTED)));
    gearLegend_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    monthChart_= new BarChart(root);

    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "光圈分布"),   QCoreApplication::translate("LightAlbum", "常用景深与通光量"),    apChart_), 0, 0);
    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "快门速度"),   QCoreApplication::translate("LightAlbum", "从高速定格到长曝"),    shChart_), 0, 1);
    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "感光度 ISO"), QCoreApplication::translate("LightAlbum", "画质与噪点的权衡"),    isoChart_), 0, 2);
    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "镜头焦距"),   QCoreApplication::translate("LightAlbum", "广角到长焦的取舍"),    focChart_), 1, 0);
    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "器材使用"),   QCoreApplication::translate("LightAlbum", "机身 / 镜头出勤率"),   gearChart_, gearLegend_), 1, 1);
    grid->addWidget(makeCard(QCoreApplication::translate("LightAlbum", "按月的拍摄活跃度"), QCoreApplication::translate("LightAlbum", "一年里的创作节律"), monthChart_), 1, 2);

    lay->addLayout(grid);

    // —— 页脚快照 ——
    foot_ = new QLabel(root);
    foot_->setStyleSheet(QString::fromUtf8(
        "color:%1;font-family:Consolas,'SFMono-Regular',monospace;font-size:11px;")
        .arg(Lumen::hex(Lumen::MUTED)));
    lay->addWidget(foot_);

    lay->addStretch(1);
    scroll->setWidget(root);

    recompute();
}

// 2.33 收纳：年份/机身筛选条（前 8 平铺 + ▾ 溢出下拉）。
// key 编码：空 = 全部；"y:2024" = 年份；"c:机身名" = 机身（mid(2) 取余串，冒号无歧义）。
// 单选语义与 2.32 一致：点年份/机身即选中，点「全部」清除；选中态随重建给出。
void LogPage::rebuildFilterChips()
{
    if (!chipsHost_) return;
    auto* lay = static_cast<QHBoxLayout*>(chipsHost_->layout());
    if (!lay) return;
    const QString chipCss = QStringLiteral(
        "QPushButton{background:#0f1512;color:#8da396;border:1px solid #26312a;"
        "border-radius:999px;padding:0 14px;font-size:12px;min-height:24px;}"
        "QPushButton:hover{color:#f4f7f4;border-color:#3a4a40;}"
        "QPushButton:checked{background:#4fc08a;color:#06130c;border:1px solid #4fc08a;"
        "border-radius:999px;padding:0 14px;font-size:12px;font-weight:700;}");

    QVector<Lumen::ChipDef> defs;
    defs.append(Lumen::chipDef(QString(),
        QCoreApplication::translate("LightAlbum", "全部"),
        filtYear_ == 0 && filtCam_.isEmpty()));
    for (const QString& y : yList_)
        defs.append(Lumen::chipDef(QStringLiteral("y:%1").arg(y), y, filtYear_ == y.toInt()));
    for (const QString& c : cList_)
        defs.append(Lumen::chipDef(QStringLiteral("c:%1").arg(c), c, filtCam_ == c));

    Lumen::fillChipRow(lay, chipsHost_, defs, chipCss, QString(),
        [this](const Lumen::ChipDef& d) {
            if (d.key.isEmpty())                       { filtYear_ = 0; filtCam_.clear(); }
            else if (d.key.startsWith(QLatin1String("y:"))) { filtYear_ = d.key.mid(2).toInt(); filtCam_.clear(); }
            else                                       { filtYear_ = 0; filtCam_ = d.key.mid(2); }
            rebuildFilterChips();   // 选中态随重建给出；选中隐藏项自动提升
            recompute();
        });
}

void LogPage::setHidePrivate(bool hide)
{
    if (hidePrivate_ == hide) return;
    hidePrivate_ = hide;
    recompute();
}

void LogPage::recompute()
{
    // 仅纳入「未锁定隐私」的照片
    const QVector<Photo>& ps = photos_ ? *photos_ : QVector<Photo>();
    QVector<const Photo*> inc;
    inc.reserve(ps.size());
    for (const Photo& p : ps)
        if (!(hidePrivate_ && p.priv)) inc.append(&p);

    // P30-C 顶栏筛选（单一维度：年份 / 机身）
    if (filtYear_ > 0 || !filtCam_.isEmpty()) {
        QVector<const Photo*> f2; f2.reserve(inc.size());
        for (const Photo* p : inc) {
            if (filtYear_ > 0 && p->year != filtYear_) continue;
            if (!filtCam_.isEmpty() && p->cam != filtCam_) continue;
            f2.append(p);
        }
        inc = f2;
    }

    // —— 桶 ——
    QMap<QString, int> apB, shB, isoB, focB;
    apB[QString::fromUtf8("f/1.4-2")] = 0; apB[QString::fromUtf8("f/2.8-4")] = 0;
    apB[QString::fromUtf8("f/5.6-8")] = 0; apB[QString::fromUtf8("f/9-11")] = 0;
    apB[QString::fromUtf8("f/13-16")] = 0;
    shB[QString::fromUtf8("≥1/500")] = 0; shB[QString::fromUtf8("1/125-1/250")] = 0;
    shB[QString::fromUtf8("1/30-1/60")] = 0; shB[QString::fromUtf8("1/4-1s")] = 0;
    shB[QString::fromUtf8(">10s")] = 0;
    isoB[QString::fromUtf8("≤200")] = 0; isoB[QString::fromUtf8("400-800")] = 0;
    isoB[QString::fromUtf8("1600-3200")] = 0; isoB[QString::fromUtf8("≥4000")] = 0;
    focB[QString::fromUtf8("≤24")] = 0; focB[QString::fromUtf8("35-50")] = 0;
    focB[QString::fromUtf8("70-135")] = 0; focB[QString::fromUtf8("≥200")] = 0;

    QMap<QString, int> gear;
    int months[12] = {0};
    QSet<QString> regions;
    QSet<QString> cams;
    QMap<QString, int> camCount;
    int total = 0, favN = 0;
    QSet<int> years;

    for (const Photo* p : inc) {
        ++total;
        if (p->rating >= 4) ++favN;          // 收藏代理：4~5 星视为收藏
        if (!p->region.isEmpty()) regions.insert(p->region);
        if (!p->cam.isEmpty()) { cams.insert(p->cam); camCount[p->cam]++; }
        if (p->year > 0) years.insert(p->year);

        const double a = parseApertureNum(p->ap);
        if (std::isfinite(a)) {
            if (a < 2.1) apB[QString::fromUtf8("f/1.4-2")]++;
            else if (a < 4.1) apB[QString::fromUtf8("f/2.8-4")]++;
            else if (a < 8.1) apB[QString::fromUtf8("f/5.6-8")]++;
            else if (a < 11.1) apB[QString::fromUtf8("f/9-11")]++;
            else apB[QString::fromUtf8("f/13-16")]++;
        }
        // 快门
        const QString st = p->sh.trimmed();
        const bool isFrac = st.contains('/');
        const bool hasS = st.contains('s', Qt::CaseInsensitive);
        const double sec = parseShutterSec(st);
        if (std::isfinite(sec) && sec > 0) {
            if (!isFrac && hasS) {
                if (sec > 10) shB[QString::fromUtf8(">10s")]++;
                else if (sec >= 1) shB[QString::fromUtf8("1/4-1s")]++;
                else shB[QString::fromUtf8("1/30-1/60")]++;
            } else {
                const double den = isFrac ? (1.0 / sec) : sec;
                if (den >= 500) shB[QString::fromUtf8("≥1/500")]++;
                else if (den >= 125) shB[QString::fromUtf8("1/125-1/250")]++;
                else shB[QString::fromUtf8("1/30-1/60")]++;
            }
        }
        // ISO
        if (p->iso > 0) {
            if (p->iso <= 200) isoB[QString::fromUtf8("≤200")]++;
            else if (p->iso <= 800) isoB[QString::fromUtf8("400-800")]++;
            else if (p->iso <= 3200) isoB[QString::fromUtf8("1600-3200")]++;
            else isoB[QString::fromUtf8("≥4000")]++;
        }
        // 焦距
        if (p->focal > 0) {
            if (p->focal <= 24) focB[QString::fromUtf8("≤24")]++;
            else if (p->focal <= 50) focB[QString::fromUtf8("35-50")]++;
            else if (p->focal <= 135) focB[QString::fromUtf8("70-135")]++;
            else focB[QString::fromUtf8("≥200")]++;
        }
        // 器材
        const QString key = p->cam + QString::fromUtf8(" · ") + p->lens;
        gear[key]++;
        // 月度
        if (p->date.length() >= 7) {
            const int m = p->date.mid(5, 2).toInt();
            if (m >= 1 && m <= 12) months[m - 1]++;
        }
    }

    // P30-C 技术调色板
    const QColor em(0x4f, 0xc0, 0x8a), gold(0xc1, 0x9a, 0x4b), violet(0x9a, 0x8f, 0xd0),
                 coral(0xe0, 0x7a, 0x5f), green(0x5a, 0x8f, 0x6b);

    apChart_->setData(apB.keys(), apB.values(), em);
    shChart_->setData(shB.keys(), shB.values(), gold);
    isoChart_->setData(isoB.keys(), isoB.values(), violet);
    focChart_->setData(focB.keys(), focB.values(), coral);
    monthChart_->setData({QString::fromUtf8("1"),QString::fromUtf8("2"),QString::fromUtf8("3"),
                           QString::fromUtf8("4"),QString::fromUtf8("5"),QString::fromUtf8("6"),
                           QString::fromUtf8("7"),QString::fromUtf8("8"),QString::fromUtf8("9"),
                           QString::fromUtf8("10"),QString::fromUtf8("11"),QString::fromUtf8("12")},
                         QList<int>(std::begin(months), std::end(months)), green);

    // 器材：取前 6 + 其他
    QVector<QPair<QString, int>> gv;
    for (auto it = gear.begin(); it != gear.end(); ++it)
        gv.append({it.key(), it.value()});
    std::sort(gv.begin(), gv.end(),
              [](const QPair<QString, int>& a, const QPair<QString, int>& b) { return a.second > b.second; });
    const QStringList palette = {QStringLiteral("#c19a4b"), QStringLiteral("#3f6098"),
                                 QStringLiteral("#4b3f7a"), QStringLiteral("#c76b3a"),
                                 QStringLiteral("#5a8f6b"), QStringLiteral("#b06a8a")};
    QStringList gk, gc; QList<int> gv_;
    int other = 0;
    for (int i = 0; i < gv.size(); ++i) {
        if (i < 6) { gk.append(gv[i].first); gc.append(palette[i % palette.size()]); gv_.append(gv[i].second); }
        else other += gv[i].second;
    }
    if (other > 0) { gk.append(QCoreApplication::translate("LightAlbum", "其他")); gc.append(QStringLiteral("#5a6b60")); gv_.append(other); }
    gearChart_->setData(gk, gv_, gc);
    QString legendHtml;
    for (int i = 0; i < gk.size(); ++i)
        legendHtml += QStringLiteral("<span style='margin-right:12px;'><i style='display:inline-block;"
                                     "width:9px;height:9px;border-radius:2px;background:%1;margin-right:5px;'></i>"
                                     "%2 (%3)</span>").arg(gc[i], gk[i], QString::number(gv_[i]));
    gearLegend_->setText(legendHtml);

    // —— KPI（P30-C：紧凑等宽数字）——
    const QString topCam = camCount.isEmpty() ? QStringLiteral("—") : camCount.begin().key();
    const int topCamN = camCount.isEmpty() ? 0 : camCount.begin().value();
    const auto topFocIt = std::max_element(focB.begin(), focB.end(),
        [](const int& a, const int& b) { return a < b; });
    const QString topFoc = (topFocIt != focB.end() && *topFocIt > 0)
        ? focB.key(*topFocIt) : QStringLiteral("—");
    const int topFocN = (topFocIt != focB.end()) ? *topFocIt : 0;
    const int yrCount = years.size();
    const int perYr = yrCount ? int(double(total) / double(yrCount)) : 0;
    const int widePct = total ? int(std::round(double(focB[QString::fromUtf8("≤24")]) / double(total) * 100.0)) : 0;

    const QStringList kpiV = {
        QString::number(total),
        QString::number(regions.size()),
        QString::number(cams.size()),
        QString::number(favN),
        topCam + QStringLiteral(" · ") + QString::number(topCamN) + QCoreApplication::translate("LightAlbum", "幅"),
        topFoc + QStringLiteral(" · ") + QString::number(topFocN) + QCoreApplication::translate("LightAlbum", "幅")
    };
    const QStringList kpiL = {
        QString::fromUtf8("作品总数"), QString::fromUtf8("足迹地区"),
        QCoreApplication::translate("LightAlbum", "使用机身"), QCoreApplication::translate("LightAlbum", "已收藏"),
        QCoreApplication::translate("LightAlbum", "最常机身"), QCoreApplication::translate("LightAlbum", "最常焦段")
    };
    QList<QLabel*> kpiLbls = kpiRow_->findChildren<QLabel*>();
    for (int i = 0; i < 6 && i < kpiLbls.size(); ++i) {
        kpiLbls[i]->setText(QStringLiteral(
            "<b style='font-family:Consolas,monospace;font-size:16px;color:#7fe6ac;'>%1</b><br>"
            "<span style='font-size:11px;color:%3;'>%2</span>")
            .arg(kpiV[i], kpiL[i], Lumen::hex(Lumen::MUTED)));
        kpiLbls[i]->setStyleSheet(QStringLiteral(
            "QLabel{background:#0f1512;border:none;border-radius:8px;"
            "padding:9px 10px;}"));
    }

    // —— 洞察（终端风）——
    insight_->setText(QString::fromUtf8(
        "> 最常机身 %1（%2 幅），广角（≤24mm）占全部作品 %3%；足迹横跨 %4 个地区，平均每年约 %5 幅。")
        .arg(topCam, QString::number(topCamN), QString::number(widePct),
             QString::number(regions.size()), QString::number(perYr)));

    // —— 页脚快照 ——
    QString scopeStr = QStringLiteral("all");
    if (filtYear_ > 0) scopeStr = QString::number(filtYear_);
    else if (!filtCam_.isEmpty()) scopeStr = filtCam_;
    foot_->setText(QString::fromUtf8("SNAPSHOT @ %1  ·  scope: %2  ·  %3 frames")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")),
             scopeStr, QString::number(total)));
}

void LogPage::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(this,
        QCoreApplication::translate("LightAlbum", "导出摄影日志 CSV"), QStringLiteral("lightalbum-log.csv"),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QStringLiteral("title,date,camera,lens,aperture,shutter,iso,focal,rating,region,private\n");
    if (!photos_) return;
    for (const Photo& p : *photos_) {
        if (hidePrivate_ && p.priv) continue;
        if (filtYear_ > 0 && p.year != filtYear_) continue;
        if (!filtCam_.isEmpty() && p.cam != filtCam_) continue;
        const QString title = QFileInfo(p.path).fileName();
        const QStringList row = { title, p.date, p.cam, p.lens, p.ap, p.sh,
                                  QString::number(p.iso), QString::number(p.focal),
                                  QString::number(p.rating), p.region,
                                  p.priv ? QStringLiteral("1") : QStringLiteral("0") };
        QStringList q;
        for (QString s : row) {
            if (s.contains('"') || s.contains(','))
                s = QStringLiteral("\"") + s.replace(QStringLiteral("\""), QStringLiteral("\"\"")) + QStringLiteral("\"");
            q.append(s);
        }
        ts << q.join(',') << QStringLiteral("\n");
    }
}

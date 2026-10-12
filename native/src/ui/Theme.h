#pragma once
// ============================================================================
// 「曜石·流明」设计系统 —— LightAlbum native 2.0（P11 全量 UI 重设计）
//
// 设计原则（UI Designer 约定）：
//   1. 色彩纪律：中性炭黑阶 + 翡翠（主操作）+ 琥珀金（品牌强调）+ 紫罗兰（AI 专属）
//      + 珊瑚（危险/警示）——语义色各司其职，绝不混用。
//   2. 大胆但有秩序：渐变只出现在「选中 / 主按钮 / 进度」三类强语义处，
//      大面积表面保持安静的深色，让照片做主角。
//   3. 所有中文 UI 字面量必须 QString::fromUtf8（Latin-1 通道必乱码，P8/P10 实证）。
//   4. 对比度：正文 INK/BG ≈ 16:1，次文 MUTED/BG ≥ 5.5:1（WCAG AA）。
// ============================================================================
#include <QColor>
#include <QFont>
#include <QString>
#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMenu>
#include <QPainter>
#include <QVector>
#include <QPoint>
#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>
#include <QWidget>
#include <functional>

// 统一选项条需要的最小 Qt 声明（fillChipRow 只做构建，不持状态）

namespace Lumen {

// ---- 色彩 token（曜石阶） ---------------------------------------------------
inline constexpr QColor BG      = QColor(0x06, 0x08, 0x06);   // 页面底（更深曜石）
inline constexpr QColor PANEL   = QColor(0x0c, 0x10, 0x0d);   // 顶栏/工具栏
inline constexpr QColor CARD    = QColor(0x13, 0x1a, 0x15);   // 卡片
inline constexpr QColor CARD_HI = QColor(0x1a, 0x23, 0x1d);   // 卡片 hover/选中
inline constexpr QColor INSET   = QColor(0x0e, 0x14, 0x10);   // 内嵌控件/输入框
inline constexpr QColor LINE    = QColor(0x26, 0x31, 0x2a);   // 描边
inline constexpr QColor INK     = QColor(0xf4, 0xf7, 0xf4);   // 主文字
inline constexpr QColor MUTED   = QColor(0x8d, 0xa3, 0x96);   // 次文字
inline constexpr QColor FAINT   = QColor(0x5a, 0x6b, 0x60);   // 弱文字/禁用

inline constexpr QColor EMERALD    = QColor(0x4f, 0xc0, 0x8a); // 翡翠主色（提亮）
inline constexpr QColor EMERALD_HI = QColor(0x6a, 0xdb, 0xa6); // 翡翠亮端
inline constexpr QColor EMERALD_DK = QColor(0x2f, 0x7d, 0x57); // 翡翠暗端
inline constexpr QColor GOLD       = QColor(0xd9, 0xa9, 0x4e); // 琥珀金
inline constexpr QColor GOLD_HI    = QColor(0xf0, 0xd0, 0x8a); // 金亮端
inline constexpr QColor GOLD_DK    = QColor(0xb0, 0x86, 0x3a); // 金暗端
inline constexpr QColor VIOLET     = QColor(0xa8, 0x8b, 0xe5); // AI 智能体专属
inline constexpr QColor VIOLET_DK  = QColor(0x6d, 0x54, 0xa8);
inline constexpr QColor CORAL      = QColor(0xe0, 0x70, 0x5f); // 危险/警示
inline constexpr QColor DISABLED   = QColor(0x50, 0x58, 0x52);

// ---- v2 扩展 token（spec 2026-10-12 §1）—— 曜石基底之上只加语义角色，不改旧值 ----
inline constexpr QColor SKY      = QColor(0x5b, 0x9c, 0xd6); // CAT2 数据分类·天青
inline constexpr QColor ROSE     = QColor(0xcf, 0x86, 0xa8); // CAT6 数据分类·洋红
inline constexpr QColor TEAL     = QColor(0x57, 0xb6, 0xa4); // CAT7 数据分类·蓝绿
inline constexpr QColor OLIVE    = QColor(0x9f, 0xb7, 0x5a); // CAT8 数据分类·橄榄
inline constexpr QColor CORAL_DK = QColor(0xa0, 0x4a, 0x3e); // 危险描边暗端
inline constexpr QColor INK_IMG   = QColor(0xe8, 0xe4, 0xda); // 照片叠加层·主文字
inline constexpr QColor MUTED_IMG = QColor(0x98, 0xa2, 0x9a); // 照片叠加层·次文字
inline constexpr QColor FAINT_IMG = QColor(0x6d, 0x7a, 0x6f); // 照片叠加层·弱文字
// 数据分类色板：CAT 板是数据编码语言，不受 VIOLET=AI 专属的控制面职责约束（spec §1.4 裁定）
inline constexpr QColor CAT1 = EMERALD, CAT2 = SKY, CAT3 = GOLD, CAT4 = VIOLET,
                        CAT5 = CORAL,  CAT6 = ROSE, CAT7 = TEAL, CAT8 = OLIVE;
inline QColor glowJade() { QColor c = EMERALD_HI; c.setAlpha(115); return c; } // 45%
inline QColor glowGold() { QColor c = GOLD_HI;   c.setAlpha(102); return c; } // 40%

inline QString hex(const QColor& c) { return c.name(); }

// ---- 字体：中文优先雅黑（不写死则 offscreen/默认字体中文发虚） ---------------
// P16.6 视觉QA：prefer 为显式从文件加载的 CJK 家族名（offscreen 字体库缺枚举时用），
// 为空则回退系统「Microsoft YaHei UI」。两者都走雅黑家族，真实窗口外观不变。
inline void applyAppFont(QApplication& app, const QString& prefer = QString())
{
    const QString fam = prefer.isEmpty() ? QString::fromUtf8("Microsoft YaHei UI") : prefer;
    QFont f(fam);
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setPointSizeF(9.5);
    app.setFont(f);
}

// ---- P17「Quiet Frame」衬线字族：拉丁走 Georgia，中文走显式加载的衬线 CJK ----
// （NotoSerifSC / 华文宋体 / 宋体，由 main.cpp 从字体文件加载后 setSerifFamily 注入；
//   未注入则中文兜底雅黑——真实窗口仍可读，仅失去衬线气质。）
inline QStringList& serifFamilies()
{
    static QStringList s = {QStringLiteral("Georgia"),
                            QStringLiteral("Microsoft YaHei UI")};
    return s;
}
inline void setSerifFamily(const QString& cjkSerif)
{
    if (cjkSerif.isEmpty()) return;
    QStringList& s = serifFamilies();
    s.removeOne(cjkSerif);
    s.insert(1, cjkSerif);   // Georgia 之后（拉丁衬线优先），雅黑之前（兜底）
}
inline QFont serifFont(qreal pointSize, QFont::Weight weight = QFont::Normal,
                       qreal letterSpacing = 0.0)
{
    QFont f;
    f.setFamilies(serifFamilies());
    f.setPointSizeF(pointSize);
    f.setWeight(weight);
    if (letterSpacing != 0.0)
        f.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

namespace Radius { // px，白名单与 qa/palette_baseline.json radiusWhitelist 同步
    inline constexpr int Micro = 4, Inset = 6, Ctl = 10, Card = 14, Feature = 20;
    inline const QString Pill = QStringLiteral("999px");
}
namespace Space { // 4px 基网格六级
    inline constexpr int Xs = 4, S = 8, M = 12, L = 16, Xl = 24, Xxl = 32;
}
namespace Type {
    enum class Tier { Display, Title, Head, Body, Small };
    inline QFont yahei(qreal pt, QFont::Weight w = QFont::Normal, qreal ls = 0.0) {
        QFont f(QString::fromUtf8("Microsoft YaHei UI"));
        f.setPointSizeF(pt); f.setWeight(w); f.setStyleStrategy(QFont::PreferAntialias);
        if (ls != 0.0) f.setLetterSpacing(QFont::AbsoluteSpacing, ls);
        return f;
    }
    inline QFont display()               { return serifFont(20, QFont::Bold); }
    inline QFont title(bool serif = true){ return serif ? serifFont(15, QFont::Bold) : yahei(15, QFont::Bold); }
    inline QFont head()  { return yahei(12, QFont::Bold); }
    inline QFont body()  { return yahei(9.5); }
    inline QFont smallText(){ return yahei(9); }   // small 撞 Windows RPC 宏，命名 smallText
    inline QFont mono(qreal pt = 9.5) { QFont f(QStringLiteral("Consolas")); f.setPointSizeF(pt); return f; }
    inline int px(qreal pt) { return int(pt * 4.0 / 3.0 + 0.5); }
    inline QString css(Tier t) {
        static const qreal pts[] = {20, 15, 12, 9.5, 9};
        return QString("font-size:%1px;").arg(px(pts[int(t)]));
    }
}
namespace Elev {
    enum Level { E1, E2 };
    inline void apply(QWidget* w, Level lv) {
        auto* sh = new QGraphicsDropShadowEffect(w);
        sh->setColor(QColor(0, 0, 0, lv == E1 ? 128 : 158));
        sh->setOffset(QPointF(0, lv == E1 ? 6 : 14));
        sh->setBlurRadius(lv == E1 ? 20 : 36);
        w->setGraphicsEffect(sh);
    }
}

// ---- 组件样式工厂（QSS 片段；只覆盖强语义组件，表面色走 QPalette） -----------

// 主按钮：翡翠渐变（大胆点一：唯一的「执行」色）
inline QString btnPrimary()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%1, stop:1 #%2);border:none;border-radius:9px;"
        "padding:8px 18px;color:#06130c;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%3, stop:1 #%1);}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}")
        .arg(hex(EMERALD_HI), hex(EMERALD_DK), hex(EMERALD));
}

// 品牌按钮：金渐变（强调/品牌动作，如「开始选片」）
inline QString btnGold()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%1, stop:1 #%2);border:none;border-radius:9px;"
        "padding:8px 18px;color:#1a1204;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%3, stop:1 #%1);}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}")
        .arg(hex(GOLD_HI), hex(GOLD_DK), hex(GOLD));
}

// AI 按钮：紫罗兰渐变（AI 智能体专属语义，大胆点二）
inline QString btnAi()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%1, stop:1 #%2);border:none;border-radius:9px;"
        "padding:8px 18px;color:#f2eefc;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%3, stop:1 #%1);}"
        "QPushButton:disabled{background:#2a332d;color:#5a6b60;}")
        .arg(hex(VIOLET), hex(VIOLET_DK), QColor(0xbd, 0xa4, 0xf0).name());
}

// 幽灵按钮：描边 + hover 微亮（工具/次要动作）
inline QString btnGhost(const QColor& tint = GOLD)
{
    return QString::fromUtf8(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:9px;"
        "padding:6px 14px;color:#%1;}"
        "QPushButton:hover{background:#1a231d;border-color:#%2;}"
        "QPushButton:disabled{color:#5a6b60;border-color:#1c2420;}")
        .arg(hex(tint), hex(tint), hex(tint));
}

// AI 动作专用第四形态（VIOLET 职责锁死：非 AI 禁用，spec §1.3）
inline QString btnViolet()
{
    QColor hover = VIOLET.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%1, stop:1 #%2);border:none;border-radius:9px;"
        "padding:8px 16px;color:#f4f7f4;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #%3, stop:1 #%1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(VIOLET), hex(VIOLET_DK), hex(hover), hex(DISABLED), hex(FAINT));
}

inline QString btnOutline(const QColor& accent)
{
    QColor hi = accent.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:%1;border:1px solid %2;border-radius:9px;"
        "padding:6px 14px;color:%2;}"
        "QPushButton:hover{border-color:%3;color:%3;background:%4;}"
        "QPushButton:disabled{color:%5;border-color:%6;}")
        .arg(hex(CARD), hex(accent), hex(hi), hex(CARD_HI), hex(FAINT), hex(LINE));
}

// KPI 卡片：左侧语义色竖条 + 渐变高光底（大胆点三）
inline QString kpiCard(const QColor& accent)
{
    return QString::fromUtf8(
        "QLabel{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 rgba(%1,46), stop:1 rgba(0,0,0,0)) #131a15;"
        "border:1px solid #26312a;border-left:3px solid #%2;"
        "border-radius:12px;padding:10px 18px;}")
        .arg(QString::number(accent.red()) + "," + QString::number(accent.green())
             + "," + QString::number(accent.blue()), hex(accent));
}

// ---- P12「美术馆策展」组件 --------------------------------------------------

// Hero 横幅：对角翡翠→金低语渐变（大面积但克制，文字仍是主角）
inline QString heroBand()
{
    return QString::fromUtf8(
        "#hero{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #14281d, stop:0.45 #0d130f, stop:1 #1c1508);"
        "border:1px solid #26312a;border-radius:16px;}");
}

// 胶囊 chip：描边 + 语义色文字
inline QString pill(const QColor& accent)
{
    return QString::fromUtf8(
        "QLabel{background:#0e1410;border:1px solid rgba(%1,120);"
        "border-radius:999px;padding:3px 12px;color:#%2;font-size:11px;}")
        .arg(QString::number(accent.red()) + "," + QString::number(accent.green())
             + "," + QString::number(accent.blue()), hex(accent));
}

// 功能入口卡：图标 + 名称，hover 时语义色描边点亮
inline QString featureCard(const QColor& accent)
{
    return QString::fromUtf8(
        "QToolButton{background:#131a15;border:1px solid #26312a;border-radius:14px;"
        "padding:12px 8px;color:#f4f7f4;font-weight:bold;font-size:13px;}"
        "QToolButton:hover{background:#1a231d;border:1px solid #%1;}"
        "QToolButton:pressed{background:#0e1410;}")
        .arg(hex(accent));
}

// 最近作品胶片卡：圆角图 + 底部日期
inline QString filmCard()
{
    return QString::fromUtf8(
        "QToolButton{background:#131a15;border:1px solid #26312a;border-radius:12px;"
        "padding:5px;color:#8da396;font-size:10.5px;}"
        "QToolButton:hover{border:1px solid #d9a94e;background:#1a231d;}");
}

// 输入框 / 下拉：胶囊内嵌
inline QString input()
{
    return QString::fromUtf8(
        "QLineEdit,QComboBox{background:#0e1410;border:1px solid #26312a;"
        "border-radius:9px;padding:6px 12px;color:#f4f7f4;"
        "selection-background-color:#2f7d57;}"
        "QLineEdit:focus,QComboBox:focus{border-color:#4fc08a;}");
}

// 键盘焦点环（WCAG 2.1：所有可交互控件须有可见焦点指示）。
// 描边画在控件外侧、不挤占布局；统一翡翠色作为全局 focus 语义色。
// 注：Qt 样式表支持 outline / outline-offset（自 Qt 4.4 起），offscreen 截图不显，
//     真实桌面键盘 Tab 聚焦即见。
inline QString focusRing(const QColor& c = EMERALD_HI)
{
    return QString::fromUtf8(
        "QPushButton:focus,QToolButton:focus,QAbstractSpinBox:focus,"
        "QComboBox:focus,QSlider:focus{"
        "outline:2px solid #%1;outline-offset:2px;}")
        .arg(hex(c));
}

// 滑杆：翡翠轨道 + 发光手柄
inline QString slider(const QColor& handle = EMERALD)
{
    return QString::fromUtf8(
        "QSlider::groove:horizontal{background:#0e1410;height:4px;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#%1;width:12px;height:12px;"
        "border-radius:6px;margin:-4px 0;}"
        "QSlider::sub-page:horizontal{background:rgba(%2,%3,%4,110);border-radius:2px;}")
        .arg(hex(handle), QString::number(handle.red()),
             QString::number(handle.green()), QString::number(handle.blue()));
}

// 进度条：翡翠渐变 chunk
inline QString progress()
{
    return QString::fromUtf8(
        "QProgressBar{background:#0e1410;border:1px solid #26312a;"
        "border-radius:6px;height:8px;text-align:center;}"
        "QProgressBar::chunk{border-radius:6px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #%1, stop:1 #%2);}")
        .arg(hex(EMERALD), hex(EMERALD_HI));
}

// 全局滚动条：细翡翠
inline QString scrollbar()
{
    return QString::fromUtf8(
        "QScrollBar:vertical{background:transparent;width:10px;margin:2px;}"
        "QScrollBar::handle:vertical{background:#24312a;border-radius:4px;min-height:30px;}"
        "QScrollBar::handle:vertical:hover{background:#2f7d57;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar:horizontal{background:transparent;height:10px;margin:2px;}"
        "QScrollBar::handle:horizontal{background:#24312a;border-radius:4px;min-width:30px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}");
}

// ---- 2.33 统一选项条（全客户端「选项 chips 条」共用构建器） -----------------
// 逻辑：最多前 kMaxVisibleChips 个选项平铺，其余收进「▾ N」溢出下拉按钮；
//   点击溢出按钮弹菜单勾选。被选中的隐藏项会自动提升进可见区——筛选状态
//   必须看得见（选中态不可见 = 骗用户，违反诚实铁律）。
// 分工：本函数只做「构建」，不持任何状态；选中态由调用方在 ChipDef.checked
//   给出，点击语义由调用方 onClick 完成（含重建整条，让提升逻辑生效）。
inline constexpr int kMaxVisibleChips = 8;

struct ChipDef {
    QString key;         // 稳定标识（空 = 「全部」类清除筛选项；语义由调用方定义）
    QString label;       // chip 平铺文字
    QString menuLabel;   // 溢出菜单项文字（空则用 label；可放完整「地点 · 时间 · 张数」）
    QString tooltip;     // 悬停提示（chip 与菜单项共用）
    QString bg;          // 非空 → 用 coloredCssTpl 上彩色底（如人物姓名 chip）
    bool    checked = false;
};

inline ChipDef chipDef(const QString& key, const QString& label, bool checked = false)
{
    ChipDef d;
    d.key = key;
    d.label = label;
    d.checked = checked;
    return d;
}

// 彩色 chip 在溢出菜单里的视觉延续：前置色点图标
inline QPixmap chipDotIcon(const QString& color)
{
    QPixmap pm(16, 16);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(color));
    p.drawEllipse(3, 3, 4, 4);
    return pm;
}

// 清空一行 chips（按钮与 stretch 一并移除；rebuild 统一入口）
inline void clearChipRow(QHBoxLayout* lay)
{
    if (!lay) return;
    while (QLayoutItem* it = lay->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
}

// 构建选项条。onClick：与点击该选项等效的动作（toggle/写筛选由调用方完成，
//   并自行重建整条）。onChipCreated：可选，可见 chip 构建后的钩子
//   （如人物条挂右键重命名；隐藏项先经菜单选中提升后同样可右键）。
inline void fillChipRow(QHBoxLayout* lay, QWidget* host,
                        const QVector<ChipDef>& chips,
                        const QString& chipCss,        // 普通样式（含 :hover/:checked 态）
                        const QString& coloredCssTpl,  // 含 %1 的彩色样式；空 = 全用 chipCss
                        const std::function<void(const ChipDef&)>& onClick,
                        const std::function<void(QPushButton*, const ChipDef&)>& onChipCreated = {},
                        int maxVisible = kMaxVisibleChips)
{
    clearChipRow(lay);
    QVector<ChipDef> list = chips;

    // 选中项落在隐藏区 → 与可见区末位交换（保证当前筛选状态可见）
    if (list.size() > maxVisible) {
        for (int i = maxVisible; i < list.size(); ++i) {
            if (list[i].checked) {
                if (!list[maxVisible - 1].checked) std::swap(list[i], list[maxVisible - 1]);
                break;
            }
        }
    }

    const int shown = qMin(maxVisible, list.size());
    for (int i = 0; i < shown; ++i) {
        const ChipDef d = list[i];
        auto* b = new QPushButton(d.label, host);
        b->setCheckable(true);
        b->setChecked(d.checked);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(d.bg.isEmpty() ? chipCss : coloredCssTpl.arg(d.bg));
        if (!d.tooltip.isEmpty()) b->setToolTip(d.tooltip);
        QObject::connect(b, &QPushButton::clicked, host, [d, onClick] { onClick(d); });
        if (onChipCreated) onChipCreated(b, d);
        lay->addWidget(b);
    }

    const int hidden = list.size() - shown;
    if (hidden > 0) {
        auto* more = new QPushButton(
            QCoreApplication::translate("LightAlbum", "▾ %1").arg(hidden), host);
        more->setCursor(Qt::PointingHandCursor);
        more->setStyleSheet(chipCss);
        more->setToolTip(QCoreApplication::translate("LightAlbum", "展开其余 %1 个选项").arg(hidden));
        auto* menu = new QMenu(more);
        menu->setStyleSheet(QString::fromUtf8(
            "QMenu{background:#0e130f;color:#d9e4dc;border:1px solid #26312a;"
            "border-radius:8px;padding:4px;}"
            "QMenu::item{padding:5px 18px;border-radius:5px;font-size:12px;}"
            "QMenu::item:selected{background:#1d2a22;color:#4fc08a;}"
            "QMenu::item:disabled{color:#5a6b60;}"));
        const int menuCap = 120;   // 超大库保护：菜单最多列 120 项（QMenu 自带滚动，别塞爆）
        int listed = 0;
        for (int i = shown; i < list.size() && listed < menuCap; ++i, ++listed) {
            const ChipDef d = list[i];
            QString text = d.menuLabel.isEmpty() ? d.label : d.menuLabel;
            if (d.checked) text = QCoreApplication::translate("LightAlbum", "✓ %1").arg(text);
            QAction* a = menu->addAction(text);
            if (!d.bg.isEmpty()) a->setIcon(chipDotIcon(d.bg));
            if (!d.tooltip.isEmpty()) a->setToolTip(d.tooltip);
            QObject::connect(a, &QAction::triggered, host, [d, onClick] { onClick(d); });
        }
        if (listed < hidden) {
            menu->addAction(QCoreApplication::translate(
                "LightAlbum", "… 其余 %1 个未列出").arg(hidden - listed))->setEnabled(false);
        }
        QObject::connect(more, &QPushButton::clicked, host, [more, menu] {
            menu->exec(more->mapToGlobal(QPoint(0, more->height() + 4)));
        });
        lay->addWidget(more);
    }
    lay->addStretch(1);
}

} // namespace Lumen

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

} // namespace Lumen

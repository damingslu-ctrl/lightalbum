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
#include <QSettings>
#include <QWidget>
#include <functional>

// 统一选项条需要的最小 Qt 声明（fillChipRow 只做构建，不持状态）

namespace Lumen {

// ---- 四灯系统（2.41.0 spec §2.1）：唯一运行时可变 token 源 -------------------
// 颜色仍以 hex() 烘进构造期 QSS，故「切灯 = 整帧重建」（MainWindow::rethemeAll），
// 不存在热刷；表面角色一律走下方 11 个访问器（读活跃灯表）。
enum class Lamp : int { Nocturne = 1, Jade = 2, Gekkan = 3, Dusk = 4 };   // L1..L4
// 空间枚举取名 Hall：Space 已被 4px 间距命名空间占用（Lumen::Space::M 等 139 处引用）。
enum class Hall : int { Lobby, Gallery, People, Studio, Vault, Common, Collection };

struct LampTokens { QColor bg, surface, inset, ink, muted, line, gold, emerald,
                    canvasBg, canvasLine, mask, focusRing; };

// 四组定值（V4 全文；LA_LAMP_QA A 组按此逐列断言，两两任列相异）。
inline const LampTokens& lamp(Lamp l)   // 表只读；非法值回退 L1
{
    static const LampTokens nocturne {   // L1 夜展
        QColor(0x14, 0x1B, 0x16), QColor(0x1D, 0x2A, 0x22),
        QColor(0x0E, 0x14, 0x10), QColor(0xE8, 0xE4, 0xD8),
        QColor(0x8D, 0xA3, 0x96), QColor(0x26, 0x31, 0x2A),
        QColor(0xD8, 0xA6, 0x57), QColor(0x4F, 0xC0, 0x8A),
        QColor(0x0B, 0x10, 0x0D), QColor(0x1A, 0x23, 0x1D),
        QColor(0x07, 0x0A, 0x08), QColor(0x4F, 0xC0, 0x8A) };
    static const LampTokens jade {       // L2 玉照
        QColor(0xF2, 0xEF, 0xE6), QColor(0xFF, 0xFF, 0xFF),
        QColor(0xE9, 0xE5, 0xD9), QColor(0x2B, 0x2B, 0x28),
        QColor(0x6D, 0x67, 0x5C), QColor(0xE2, 0xDE, 0xD3),
        QColor(0xB9, 0x8A, 0x2F), QColor(0x3F, 0x6B, 0x52),
        QColor(0xE7, 0xE3, 0xD7), QColor(0xD6, 0xD1, 0xC4),
        QColor(0x1C, 0x1A, 0x16), QColor(0x3F, 0x6B, 0x52) };
    static const LampTokens gekkan {     // L3 素印（灰阶，唯一彩点 = gold==emerald）
        QColor(0xFA, 0xFA, 0xFA), QColor(0xFF, 0xFF, 0xFF),
        QColor(0xEF, 0xEF, 0xEC), QColor(0x11, 0x11, 0x11),
        QColor(0x8A, 0x8A, 0x85), QColor(0xE4, 0xE4, 0xE0),
        QColor(0xB9, 0x8A, 0x2F), QColor(0xB9, 0x8A, 0x2F),
        QColor(0xEF, 0xEF, 0xEC), QColor(0xDD, 0xDD, 0xD8),
        QColor(0x11, 0x11, 0x11), QColor(0xB9, 0x8A, 0x2F) };
    static const LampTokens dusk {       // L4 暮金
        QColor(0x24, 0x1A, 0x10), QColor(0x33, 0x24, 0x1A),
        QColor(0x2A, 0x1E, 0x14), QColor(0xF2, 0xE4, 0xCE),
        QColor(0xA8, 0x89, 0x6B), QColor(0x4A, 0x33, 0x20),
        QColor(0xE2, 0xA9, 0x5C), QColor(0xB9, 0x8A, 0x2F),
        QColor(0x1A, 0x12, 0x0C), QColor(0x3A, 0x28, 0x19),
        QColor(0x0D, 0x09, 0x06), QColor(0xE2, 0xA9, 0x5C) };
    switch (l) {
    case Lamp::Jade:   return jade;
    case Lamp::Gekkan: return gekkan;
    case Lamp::Dusk:   return dusk;
    default:           return nocturne;
    }
}

// 跟随空间的默认灯：前厅走夜展，画廊/人物馆/修图/暗箱走玉照；
// Common（日志/关于等未定义页）留在夜展基调，不跳亮底；典藏页跟画廊同语汇。
inline Lamp lampFor(Hall h)
{
    static const Lamp follow[7] = { Lamp::Nocturne, Lamp::Jade, Lamp::Jade,
                                    Lamp::Jade,     Lamp::Jade, Lamp::Nocturne,
                                    Lamp::Jade };   // Collection：亮底画廊语汇
    const int i = int(h);
    return (i >= 0 && i < int(sizeof(follow) / sizeof(follow[0]))) ? follow[i]
                                                                  : Lamp::Nocturne;
}

// QSettings 持久化：lamp/active（int 1..4）、lamp/mode（"follow"/"manual"）。
inline Lamp activeLamp()
{
    switch (QSettings().value(QStringLiteral("lamp/active"), 1).toInt()) {
    case 2:  return Lamp::Jade;
    case 3:  return Lamp::Gekkan;
    case 4:  return Lamp::Dusk;
    default: return Lamp::Nocturne;
    }
}
inline void setActiveLamp(Lamp l) { QSettings().setValue(QStringLiteral("lamp/active"), int(l)); }
inline bool lampFollowSpace()
{
    return QSettings().value(QStringLiteral("lamp/mode"),
                             QStringLiteral("follow")).toString()
           == QLatin1String("follow");
}
inline void setLampFollowSpace(bool on)
{
    QSettings().setValue(QStringLiteral("lamp/mode"),
                         on ? QStringLiteral("follow") : QStringLiteral("manual"));
}
// 锁 = 手动灯（封存 lampFor）；与 lamp/mode 同一存储，不引第二份状态。
inline bool lampLocked()                { return !lampFollowSpace(); }
inline void setLampLocked(bool locked)  { setLampFollowSpace(!locked); }

// 装配处统一取灯：follow 下 lampFor(hall) 优先，manual（锁）下恒 activeLamp。
inline Lamp resolveLamp(Hall h) { return lampFollowSpace() ? lampFor(h) : activeLamp(); }

inline QString lampCssVars(Lamp l)   // 壳层 :root 变量段（前厅/⌘K 消费）
{
    const LampTokens& t = lamp(l);
    return QStringLiteral("--la-bg:%1;--la-surface:%2;--la-inset:%3;--la-ink:%4;"
                          "--la-muted:%5;--la-line:%6;--la-gold:%7;--la-emerald:%8;")
        .arg(t.bg.name(), t.surface.name(), t.inset.name(), t.ink.name(),
             t.muted.name(), t.line.name(), t.gold.name(), t.emerald.name());
}

// ---- 表面角色访问器（11 个；旧 constexpr BG/PANEL/… 的全部替代面） -----------
// 派生规则（计划裁决 3）：PANEL→inset 列；CARD→surface 列；CARD_HI→surface 向 ink
// 混 8%；FAINT→muted 向 bg 混 50%。
inline QColor bg()      { return lamp(activeLamp()).bg; }
inline QColor panel()   { return lamp(activeLamp()).inset; }
inline QColor card()    { return lamp(activeLamp()).surface; }
inline QColor cardHi()
{
    const LampTokens& t = lamp(activeLamp());
    QColor c = t.surface;
    c.setRed  (c.red()   + (t.ink.red()   - c.red())   * 8 / 100);
    c.setGreen(c.green() + (t.ink.green() - c.green()) * 8 / 100);
    c.setBlue (c.blue()  + (t.ink.blue()  - c.blue())  * 8 / 100);
    return c;
}
inline QColor inset()   { return lamp(activeLamp()).inset; }
inline QColor ink()     { return lamp(activeLamp()).ink; }
inline QColor muted()   { return lamp(activeLamp()).muted; }
inline QColor faint()
{
    const LampTokens& t = lamp(activeLamp());
    return QColor((t.muted.red()   + t.bg.red())   / 2,
                  (t.muted.green() + t.bg.green()) / 2,
                  (t.muted.blue()  + t.bg.blue())  / 2);
}
inline QColor line()    { return lamp(activeLamp()).line; }
inline QColor gold()    { return lamp(activeLamp()).gold; }
inline QColor emerald() { return lamp(activeLamp()).emerald; }

// ---- 画布层角色（T12）：编辑画布/对比/视频/日志底与焦点环，随灯换表不再写死深色 ----
inline QColor canvasBg()       { return lamp(activeLamp()).canvasBg; }
inline QColor canvasLine()     { return lamp(activeLamp()).canvasLine; }
inline QColor maskColor()      { return lamp(activeLamp()).mask; }
inline QColor focusRingColor() { return lamp(activeLamp()).focusRing; }

// ---- 品牌色（本轮冻结，不随灯） ---------------------------------------------
inline constexpr QColor EMERALD_HI = QColor(0x6a, 0xdb, 0xa6); // 翡翠亮端
inline constexpr QColor EMERALD_DK = QColor(0x2f, 0x7d, 0x57); // 翡翠暗端
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
// T7 长尾归零新增（spec §1.5 照片板梯阶补全；§1 erratum 提案，待 T12 文档收编）：
inline constexpr QColor SOFT_IMG  = QColor(0xb9, 0xb3, 0xa6); // 照片叠加层·暖灰中阶（Quiet Frame 导航文字/INDEX/caps 梯阶收编，INK_IMG↔MUTED_IMG 之间）
inline constexpr QColor GOLD_WASH = QColor(0x26, 0x20, 0x12); // 金强调按钮 :checked 底 wash（GOLD 描边+文字之下的暖金暗衬，非表面非描线，无现成 token 之家）
// 数据分类色板：CAT 板是数据编码语言，不受 VIOLET=AI 专属的控制面职责约束（spec §1.4 裁定）
// CAT1/CAT3 原引 EMERALD/GOLD 常量；两者已 lamp-aware 化，数据编码本轮冻结 → 落 L1 同值字面。
inline constexpr QColor CAT1 = QColor(0x4f, 0xc0, 0x8a), CAT2 = SKY,
                        CAT3 = QColor(0xd9, 0xa9, 0x4e), CAT4 = VIOLET,
                        CAT5 = CORAL,  CAT6 = ROSE, CAT7 = TEAL, CAT8 = OLIVE;
inline QColor glowJade() { QColor c = EMERALD_HI; c.setAlpha(115); return c; } // 45%
inline QColor glowGold() { QColor c = GOLD_HI;   c.setAlpha(102); return c; } // 40%

// 2.41.0 §2.5 凝视模式「环境光晕」：以主色在窗口四缘铺一层极淡径向渐变 —— 中心
// 仍是影院黑幕（照片压得住），越靠边越泛主色。strength∈[0,1]，实际 alpha 硬顶在
// 10%（25/255）以内（spec 明文 ≤10%）；吸顶取整后仍 ≤ 25。
inline QString glowCss(const QColor& c, double strength = 1.0)
{
    QColor edge = c;
    edge.setAlpha(int(qBound(0.0, strength, 1.0) * 25.0 + 0.5));
    // 注意：QColor::name() 丢 alpha，须显式 HexArgb（#aarrggbb）。
    return QStringLiteral(
        "QWidget#photoLightbox{background:qradialgradient("
        "cx:0.5,cy:0.5,radius:0.78,fx:0.5,fy:0.5,"
        "stop:0 rgba(6,8,6,0.97),stop:1 %1);}")
        .arg(edge.name(QColor::HexArgb));
}

// 人物色环单源：FNV-1a 稳定哈希 → CAT1..8 具名板。
// GalleryPage 的人物 chip 底色与 PeoplePage 的展室环共用同一份映射（此前 GalleryPage 本地一份）；
// 哈希仅用于「稳定选色」（同姓名恒同色），不代表任何人物语义。
inline QColor personRing(const QString& name)
{
    const QByteArray s = name.toUtf8();
    uint h = 2166136261u;
    for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; }
    static const QColor pal[] = { CAT1, CAT2, CAT3, CAT4, CAT5, CAT6, CAT7, CAT8 };
    return pal[h % (sizeof(pal) / sizeof(pal[0]))];
}

// 注意：返回值含前导 '#'（QColor::name()）。QSS 模板占位符前不得再写字面 #，
// 否则拼出 "##rrggbb"，Qt 样式表解析器会整条丢弃该声明（Task 4 offscreen 实测）。
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
    // 2.41.0 spec §3：七档字阶（+Label 展签体、+MonoMeta 馆务元信息）。
    // 既有符号名全部保值，调用点零破坏；css() 的档位序与 pts[] 一一对应。
    enum class Tier { Display, Title, Label, Head, Body, Small, MonoMeta };
    inline QFont yahei(qreal pt, QFont::Weight w = QFont::Normal, qreal ls = 0.0) {
        QFont f(QString::fromUtf8("Microsoft YaHei UI"));
        f.setPointSizeF(pt); f.setWeight(w); f.setStyleStrategy(QFont::PreferAntialias);
        if (ls != 0.0) f.setLetterSpacing(QFont::AbsoluteSpacing, ls);
        return f;
    }
    inline QFont display()               { return serifFont(20, QFont::Bold); }
    inline QFont title(bool serif = true){ return serif ? serifFont(15, QFont::Bold) : yahei(15, QFont::Bold); }
    // 展签体（人物名/年份牌/合集题/⌘K 展题）：17px/500，衬线仅 L1/L4 允许。
    inline QFont label(bool serifAllowed = true)
    {
        const bool serif = serifAllowed
            && (activeLamp() == Lamp::Nocturne || activeLamp() == Lamp::Dusk);
        return serif ? serifFont(12.75, QFont::DemiBold, 1.0)
                     : yahei(12.75, QFont::DemiBold);
    }
    inline QFont head()  { return yahei(12, QFont::Bold); }
    inline QFont body()  { return yahei(9.5); }
    inline QFont smallText(){ return yahei(9); }   // small 撞 Windows RPC 宏，命名 smallText
    inline QFont mono(qreal pt = 9.5) { QFont f(QStringLiteral("Consolas")); f.setPointSizeF(pt); return f; }
    inline QFont monoMeta() { return mono(8.25); }  // 馆务 Mono-Meta 11px
    inline int px(qreal pt) { return int(pt * 4.0 / 3.0 + 0.5); }
    inline QString css(Tier t) {
        static const qreal pts[] = {20, 15, 12.75, 12, 9.5, 9, 8.25};
        return QString("font-size:%1px;").arg(px(pts[int(t)]));
    }
}
namespace Elev {
    // 四层高度（2.42.0 spec §0.1 R4：在 v2 阴影基座上再加一层「光」）。
    // 显式取 1..4 —— 0 留作「未设层」，且层级值本身可判别（门禁 Elev_E4_exists）。
    enum Level { E1 = 1, E2 = 2, E3 = 3, E4 = 4 };

    // 阴影三列唯一真源（纯函数；apply 只做接线，门禁逐层读回比对）。
    // 逐层严格递增：任何一层塌回上一层，V_elev_shadow_monotone / _apply_* 即红。
    inline int shadowAlpha(Level lv) { return lv == E1 ? 128 : lv == E2 ? 158 : lv == E3 ? 176 : 190; }
    inline qreal shadowDy(Level lv)  { return lv == E1 ? 6 : lv == E2 ? 14 : lv == E3 ? 22 : 30; }
    inline qreal shadowBlur(Level lv){ return lv == E1 ? 20 : lv == E2 ? 36 : lv == E3 ? 54 : 72; }

    // V4 §五「光晕 α≤10%」的落地形式：金按 ≤10% 合成进宿主底色后**不透明**输出。
    // 不用透明外圈 —— Qt QSS 只有一层 background，透明外圈会露出父层（卡片边缘发黑）。
    inline constexpr double kInnerGlowMax = 0.10;
    inline QColor innerGlow(const QColor& base, double ratio = kInnerGlowMax)
    {
        const QColor g = gold();
        const double t = qBound(0.0, ratio, kInnerGlowMax);
        auto mix = [t](int b, int hi) { return qRound(b + (hi - b) * t); };
        return QColor(mix(base.red(), g.red()), mix(base.green(), g.green()),
                      mix(base.blue(), g.blue()));
    }

    // E4 开馆层 = 1px 金描边（走活跃灯 token）+ 内发光径向。E1..E3 返回空串 = 不开馆。
    // selector 必须限定到宿主自己：Qt 会把无选择器的声明下发给全部后代控件。
    inline QString openCss(Level lv, const QString& selector, const QColor& base)
    {
        if (lv != E4) return QString();
        return QStringLiteral("%1{border:1px solid %2;"
                              "background:qradialgradient("
                              "cx:0.5,cy:0.5,radius:1.0,fx:0.5,fy:0.5,"
                              "stop:0 %3,stop:0.58 %3,stop:1 %4);}")
            .arg(selector, hex(gold()), hex(base), hex(innerGlow(base)));
    }

    inline QString selectorFor(const QWidget* w)
    {
        const QString name = w->objectName();
        return name.isEmpty()
            ? QStringLiteral("*")            // 裸控件（无子树）才允许退化形式
            : QString::fromLatin1(w->metaObject()->className()) + QStringLiteral("#") + name;
    }

    inline void apply(QWidget* w, Level lv) {
        auto* sh = new QGraphicsDropShadowEffect(w);
        sh->setColor(QColor(0, 0, 0, shadowAlpha(lv)));
        sh->setOffset(QPointF(0, shadowDy(lv)));
        sh->setBlurRadius(shadowBlur(lv));
        w->setGraphicsEffect(sh);
        switch (lv) {
        case E1: case E2: case E3:
            break;                              // 只加压舱阴影；E3 聚光不开馆，不动宿主 QSS
        case E4: {                              // 开馆：追加（不覆盖）宿主自己的样式表
            const QString css = openCss(lv, selectorFor(w), card());
            if (css.isEmpty() || w->property("laElevOpen").toString() == css) break;   // 幂等
            const QString own = w->styleSheet();
            w->setStyleSheet(own.isEmpty()
                                 ? css
                                 : own + (own.endsWith(QLatin1Char(';')) ? QString()
                                                                         : QStringLiteral(";")) + css);
            w->setProperty("laElevOpen", css);
            break;
        }
        }
    }
}

// ---- 组件样式工厂（QSS 片段；只覆盖强语义组件，表面色走 QPalette） -----------

// 主按钮：翡翠渐变（大胆点一：唯一的「执行」色）
inline QString btnPrimary()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %1, stop:1 %2);border:none;border-radius:10px;"
        "padding:8px 18px;color:#06130c;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %3, stop:1 %1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(EMERALD_HI), hex(EMERALD_DK), hex(emerald()), hex(inset()), hex(faint()));
}

// 品牌按钮：金渐变（强调/品牌动作，如「开始选片」）
inline QString btnGold()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %1, stop:1 %2);border:none;border-radius:10px;"
        "padding:8px 18px;color:#1a1204;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %3, stop:1 %1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(GOLD_HI), hex(GOLD_DK), hex(gold()), hex(inset()), hex(faint()));
}

// AI 按钮：紫罗兰渐变（AI 智能体专属语义，大胆点二）
inline QString btnAi()
{
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %1, stop:1 %2);border:none;border-radius:10px;"
        "padding:8px 18px;color:#f2eefc;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %3, stop:1 %1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(VIOLET), hex(VIOLET_DK), QColor(0xbd, 0xa4, 0xf0).name(),
             hex(inset()), hex(faint()));
}

// 幽灵按钮：描边 + hover 微亮（工具/次要动作）。
// cls 参数：顶栏灯钮是 QToolButton，而本函数选择器若写死 QPushButton，整条 QSS 挂上去
// 不生效（实测：灯钮吃到的是全局样式，换灯时描边/图标色不跟）。padding 同理要能归零——
// 32×32 定尺寸图标钮上 14px 横向 padding 会把 18px 图标挤裁。
inline QString btnGhost(const QColor& tint = gold(), const char* cls = "QPushButton",
                        const char* pad = "6px 14px")
{
    return QString::fromUtf8(
        "%1{background:%4;border:1px solid %5;border-radius:10px;"   // T9: 9->Radius::Ctl 10 (btnGhost)
        "padding:%8;color:%2;}"
        "%1:hover{background:%6;border-color:%3;}"
        "%1:disabled{color:%7;border-color:%5;}")
        .arg(QString::fromLatin1(cls))          // %1 选择器（三处）
        .arg(hex(tint))                         // %2 字/图标色
        .arg(hex(tint))                         // %3 hover 描边
        .arg(hex(card()))                       // %4 底色
        .arg(hex(line()))                       // %5 描边（含 disabled）
        .arg(hex(cardHi()))                     // %6 hover 底色
        .arg(hex(faint()))                      // %7 disabled 字色
        .arg(QString::fromLatin1(pad));         // %8 内边距
}

// AI 动作专用第四形态（VIOLET 职责锁死：非 AI 禁用，spec §1.3）
inline QString btnViolet()
{
    QColor hover = VIOLET.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %1, stop:1 %2);border:none;border-radius:10px;"
        "padding:8px 16px;color:%6;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 %3, stop:1 %1);}"
        "QPushButton:disabled{background:%4;color:%5;}")
        .arg(hex(VIOLET), hex(VIOLET_DK), hex(hover), hex(DISABLED), hex(faint()), hex(ink()));
}

inline QString btnOutline(const QColor& accent)
{
    QColor hi = accent.lighter(115);
    return QString::fromUtf8(
        "QPushButton{background:%1;border:1px solid %2;border-radius:10px;"   // T9: 9->Radius::Ctl 10 (btnOutline)
        "padding:6px 14px;color:%2;}"
        "QPushButton:hover{border-color:%3;color:%3;background:%4;}"
        "QPushButton:disabled{color:%5;border-color:%6;}")
        .arg(hex(card()), hex(accent), hex(hi), hex(cardHi()), hex(faint()), hex(line()));
}

// KPI 卡片：左侧语义色竖条 + 渐变高光底（大胆点三）
inline QString kpiCard(const QColor& accent)
{
    return QString::fromUtf8(
        "QLabel{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 rgba(%1,46), stop:1 rgba(0,0,0,0)) %3;"
        "border:1px solid %4;border-left:3px solid %2;"
        "border-radius:14px;padding:10px 18px;}")   // T9: 12->Radius::Card 14 (kpiCard)
        .arg(QString::number(accent.red()) + "," + QString::number(accent.green())
             + "," + QString::number(accent.blue()),
             hex(accent), hex(card()), hex(line()));
}

// ---- P12「美术馆策展」组件 --------------------------------------------------

// Hero 横幅：对角翡翠→金低语渐变（大面积但克制，文字仍是主角）
inline QString heroBand()
{
    return QString::fromUtf8(
        "#hero{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #14281d, stop:0.45 #0d130f, stop:1 #1c1508);"
        "border:1px solid %1;border-radius:20px;}")   // T9: 16->Radius::Feature 20 (heroBand=首页英雄件 §3.1)
        .arg(hex(line()));
}

// 胶囊 chip：描边 + 语义色文字
inline QString pill(const QColor& accent)
{
    return QString::fromUtf8(
        "QLabel{background:%3;border:1px solid rgba(%1,120);"
        "border-radius:999px;padding:3px 12px;color:%2;font-size:11px;}")
        .arg(QString::number(accent.red()) + "," + QString::number(accent.green())
             + "," + QString::number(accent.blue()), hex(accent), hex(inset()));
}

// 功能入口卡：图标 + 名称，hover 时语义色描边点亮
inline QString featureCard(const QColor& accent)
{
    return QString::fromUtf8(
        "QToolButton{background:%2;border:1px solid %3;border-radius:14px;"
        "padding:12px 8px;color:%4;font-weight:bold;font-size:13px;}"
        "QToolButton:hover{background:%5;border:1px solid %1;}"
        "QToolButton:pressed{background:%6;}")
        .arg(hex(accent), hex(card()), hex(line()), hex(ink()), hex(cardHi()), hex(inset()));
}

// 最近作品胶片卡：圆角图 + 底部日期
inline QString filmCard()
{
    return QString::fromUtf8(
        "QToolButton{background:%1;border:1px solid %2;border-radius:14px;"   // T9: 12->Radius::Card 14 (filmCard)
        "padding:5px;color:%3;font-size:10.5px;}"
        "QToolButton:hover{border:1px solid %4;background:%5;}")
        .arg(hex(card()), hex(line()), hex(muted()), hex(gold()), hex(cardHi()));
}

// 输入框 / 下拉：胶囊内嵌
inline QString input()
{
    return QString::fromUtf8(
        "QLineEdit,QComboBox{background:%1;border:1px solid %2;"
        "border-radius:10px;padding:6px 12px;color:%3;"   // T9: 9->Radius::Ctl 10 (input，brief 裁定 Theme.h 9px 族归 Ctl)
        "selection-background-color:#2f7d57;}"
        "QLineEdit:focus,QComboBox:focus{border-color:%4;}")
        .arg(hex(inset()), hex(line()), hex(ink()), hex(emerald()));
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
        "outline:2px solid %1;outline-offset:2px;}")
        .arg(hex(c));
}

// 滑杆：翡翠轨道 + 发光手柄
inline QString slider(const QColor& handle = emerald())
{
    return QString::fromUtf8(
        "QSlider::groove:horizontal{background:%5;height:4px;border-radius:6px;}"   // T9: 2->Radius::Inset 6（§3.1 r-inset 用途明列滑轨；4px 高轨半径截半仍胶囊，视觉零位移）
        "QSlider::handle:horizontal{background:%1;width:12px;height:12px;"
        "border-radius:6px;margin:-4px 0;}"
        "QSlider::sub-page:horizontal{background:rgba(%2,%3,%4,110);border-radius:6px;}")   // T9: 2->Radius::Inset 6（与 groove 同档嵌套等径）
        .arg(hex(handle), QString::number(handle.red()),
             QString::number(handle.green()), QString::number(handle.blue()),
             hex(inset()));
}

// 进度条：翡翠渐变 chunk
inline QString progress()
{
    return QString::fromUtf8(
        "QProgressBar{background:%3;border:1px solid %4;"
        "border-radius:6px;height:8px;text-align:center;}"
        "QProgressBar::chunk{border-radius:6px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %1, stop:1 %2);}")
        .arg(hex(emerald()), hex(EMERALD_HI), hex(inset()), hex(line()));
}

// 全局滚动条：细翡翠
inline QString scrollbar()
{
    // 终审 M2（spec §1.7）：把手 #24312a 归并 LINE token（#26312a，近值归一是裁定本意）。
    // 陷阱注记：hex() 自带前导 '#'，模板 %N 前不得再写字面 #，否则 ##rrggbb 整条声明被静默丢弃。
    return QString::fromUtf8(
        "QScrollBar:vertical{background:transparent;width:10px;margin:2px;}"
        "QScrollBar::handle:vertical{background:%1;border-radius:4px;min-height:30px;}"
        "QScrollBar::handle:vertical:hover{background:#2f7d57;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar:horizontal{background:transparent;height:10px;margin:2px;}"
        "QScrollBar::handle:horizontal{background:%1;border-radius:4px;min-width:30px;}"
        "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}")
        .arg(hex(line()));
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
            "QMenu{background:%1;color:%2;border:1px solid %3;"
            "border-radius:10px;padding:4px;}"   // T9: 8->Radius::Ctl 10（QMenu 框；嵌套律 框10=项6+padding4 命中）
            "QMenu::item{padding:5px 18px;border-radius:6px;font-size:12px;}"   // T9: 5->Radius::Inset 6（§3.1 归并 5/6/7；font-size 串为豁免文件原位 T8 形态，不动）
            "QMenu::item:selected{background:%4;color:%5;}"
            "QMenu::item:disabled{color:%6;}")
            .arg(hex(panel()), hex(ink()), hex(line()),
                 hex(card()), hex(emerald()), hex(faint())));
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

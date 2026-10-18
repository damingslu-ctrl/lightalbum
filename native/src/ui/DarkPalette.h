#pragma once
// 「曜石·流明」深色主题 P11 → 2.41.0 四灯泛化：
//   apply() 的角色映射不变，值改读活跃灯的 8 列 token 表（ui/Theme.h Lumen::lamp）。
//   下面 8 枚 constexpr 不再是「实际发色」，而是 LA_A11Y_CONTRAST_QA 的 15 对对比度
//   审计夹具（棘轮豁免文件，保留原值 = 老审计可复现），删除即门禁红。
#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QColor>

namespace Dark {
inline constexpr QColor Bg      = QColor(0x06, 0x08, 0x06);   // 页面底
inline constexpr QColor Card    = QColor(0x13, 0x1a, 0x15);   // 卡片
inline constexpr QColor Inset   = QColor(0x0e, 0x14, 0x10);   // 内嵌控件
inline constexpr QColor Ink     = QColor(0xf4, 0xf7, 0xf4);   // 主文字
inline constexpr QColor Muted   = QColor(0x8d, 0xa3, 0x96);   // 次文字
inline constexpr QColor Line    = QColor(0x26, 0x31, 0x2a);   // 描边
inline constexpr QColor Gold    = QColor(0xd9, 0xa9, 0x4e);   // 强调（金）
inline constexpr QColor Emerald = QColor(0x4f, 0xc0, 0x8a);   // 主操作（翡翠）

inline void apply(QApplication& app, Lumen::Lamp l = Lumen::activeLamp())
{
    const Lumen::LampTokens& t = Lumen::lamp(l);
    QPalette pal;
    pal.setColor(QPalette::Window,          t.bg);
    pal.setColor(QPalette::WindowText,      t.ink);
    pal.setColor(QPalette::Base,            t.inset);
    pal.setColor(QPalette::AlternateBase,   t.surface);
    pal.setColor(QPalette::Text,            t.ink);
    pal.setColor(QPalette::Button,          t.surface);
    pal.setColor(QPalette::ButtonText,      t.ink);
    pal.setColor(QPalette::ToolTipBase,     t.surface);
    pal.setColor(QPalette::ToolTipText,     t.ink);
    pal.setColor(QPalette::BrightText,      t.gold);
    pal.setColor(QPalette::Highlight,       t.gold);
    pal.setColor(QPalette::HighlightedText, t.bg);
    pal.setColor(QPalette::PlaceholderText, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::Text,      t.muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    app.setPalette(pal);
}
} // namespace Dark

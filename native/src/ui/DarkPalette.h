#pragma once
// 「曜石·流明」深色主题 P11（与 ui/Theme.h token 同步：charcoal + 翡翠 + 金）
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

inline void apply(QApplication& app)
{
    QPalette pal;
    pal.setColor(QPalette::Window,          Bg);
    pal.setColor(QPalette::WindowText,      Ink);
    pal.setColor(QPalette::Base,            Inset);
    pal.setColor(QPalette::AlternateBase,   Card);
    pal.setColor(QPalette::Text,            Ink);
    pal.setColor(QPalette::Button,          Card);
    pal.setColor(QPalette::ButtonText,      Ink);
    pal.setColor(QPalette::ToolTipBase,     Card);
    pal.setColor(QPalette::ToolTipText,     Ink);
    pal.setColor(QPalette::BrightText,      Gold);
    pal.setColor(QPalette::Highlight,       Gold);
    pal.setColor(QPalette::HighlightedText, Bg);
    pal.setColor(QPalette::PlaceholderText, Muted);
    pal.setColor(QPalette::Disabled, QPalette::Text,      Muted);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, Muted);
    app.setPalette(pal);
}
} // namespace Dark

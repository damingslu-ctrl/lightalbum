#pragma once
// 「雾隐·墨韵」深色主题（与 web 版 body.dark 色板一致：charcoal + 金强调 + 翡翠语义）
#include <QApplication>
#include <QPalette>
#include <QColor>

namespace Dark {
inline constexpr QColor Bg      = QColor(0x0b, 0x0e, 0x0c);   // 页面底
inline constexpr QColor Card    = QColor(0x18, 0x1d, 0x1a);   // 卡片
inline constexpr QColor Inset   = QColor(0x12, 0x18, 0x14);   // 内嵌控件
inline constexpr QColor Ink     = QColor(0xf2, 0xf5, 0xf3);   // 主文字
inline constexpr QColor Muted   = QColor(0x9f, 0xa8, 0xa3);   // 次文字
inline constexpr QColor Line    = QColor(0x2a, 0x2f, 0x2c);   // 描边
inline constexpr QColor Gold    = QColor(0xc1, 0x9a, 0x4b);   // 强调（金）
inline constexpr QColor Emerald = QColor(0x5a, 0x8f, 0x6b);   // AI/成功语义（翡翠，唯一语义色）

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

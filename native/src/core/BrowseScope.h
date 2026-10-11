// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 2.33.0 灯箱翻图作用域（隐私门补漏）。
//
// 为什么要抽成纯函数：←/→ 的「下一张可见者」同时牵住两条不变量 ——
//   ① 不得越出调用方给的作用域（筛选/故事/人物里就该在这几张之间走）；
//   ② 锁定态不得步进私密照片（vault 锁着时连按 → 此前能翻进私密区）。
// 若把这两条写在 QWidget 里，门禁就只能靠构造 MainWindow 才能断言，且实现与
// 断言各写一套（铁律 20：必须同源）。故抽出槽位序列 → 下标 的唯一真源，
// PhotoLightbox::navigate() 直接调用本函数，LA_PRIVACY_QA 也直接调用本函数。
// 命名注记：形参不可叫 slots —— Qt 在 !QT_NO_KEYWORDS 下把 slots 定义成空宏，
//   会把 `slots.size()` 展开成 `.size()` 而编译失败（本文件实测踩过）。
#include <QString>
#include <QVector>

// 一个可翻到的位置：只带判定所需的最小信息（路径 + 是否私密）。
struct BrowseSlot {
    QString path;
    bool    priv = false;
};

namespace BrowseScope {

// 从 cur 起按 delta（±1）方向找第一个可见槽位。
// 端点不回绕；cur 越界/为负、或方向上没有可见者 → -1（调用方保持当前图不动）。
// hidePrivate=true 时私密槽视为不可见（数据诚实：priv 为真就是真，不猜）。
int nextVisible(const QVector<BrowseSlot>& seq, int cur, int delta, bool hidePrivate);

} // namespace BrowseScope

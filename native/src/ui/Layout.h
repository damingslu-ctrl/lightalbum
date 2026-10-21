#pragma once
// 波4 §3.3：网格列数唯一真源（EditorialView / PeoplePage / CollectionPage 三处共用）。
// 纯算术：零 Qt、零状态；resize 防抖归各页，工具函数不许长状态。
// 分母取 targetPx + 12（卡片间距）：960/242=3、1440/242=5.95→5、2560 封顶 5
// （spec 修订 R11：原 int(W/230) 会在 960 得 4，与 G 组断言自相矛盾）。
inline int gridCols(int widthPx, int targetPx = 230, int lo = 3, int hi = 5)
{
    if (targetPx <= 0) targetPx = 230;
    const int pitch = targetPx + 12;
    const int n = (widthPx > 0 ? widthPx : 0) / pitch;
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
}

# tools/ — 取证与标定探针（非产品代码，不参与 CMake 构建）

这些是**离线取证工具**，不进 exe、不被任何门禁调用。它们的存在意义是：
门禁阈值必须由**实测**标定（本项目铁律：颜色/像素/性能支持必须实测，禁猜）。
阈值改动时用它们复标，而不是拍脑袋。

## retouch_probe.cpp — 修图 12 算子阈值标定探针

编译（需 MinGW bin 在 PATH，Qt 6.8.3 mingw_64）：

    export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"
    QTDIR="D:/qt/6.8.3/mingw_64"
    g++ -std=c++17 -O2 -o build/retouch_probe.exe tools/retouch_probe.cpp \
        src/core/RetouchEngine.cpp -I src \
        -I "$QTDIR/include" -I "$QTDIR/include/QtCore" -I "$QTDIR/include/QtGui" \
        -L "$QTDIR/lib" -lQt6Core -lQt6Gui

运行（Qt bin 必须在 PATH，否则 rc=127）：

    cd build && export PATH="D:/qt/6.8.3/mingw_64/bin:$PATH" \
        && export QT_QPA_PLATFORM=offscreen && ./retouch_probe.exe

输出 20 组算子的实测 delta（dLuma / dStd / dSat / dLocal / dRGB 三通道和），
以及 identity 逐位一致性、暗角与高光/阴影的空间分布。
`LA_RETOUCH_OP_QA` 的每个阈值都取自这里的实测值再留余量。

它同时是 `recommend()` 符号矛盾的发现工具：`highlights=-0.30` 实测 **+42.90 提亮**，
与其「压高光」文案相反 —— 该缺陷于 2.24.0 修复（统一为「正值=增强同名效果」）。

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

## raw_probe.cpp — RAW 解码链路取证探针（2.28.0）

链接**真实的** `src/core/RawDecode.cpp`（非副本），断言 RAW 支持的真实行为。
不进 exe、不被门禁调用。

编译（需 MinGW bin 在 PATH，Qt 6.8.3 mingw_64；LibRaw 须已 fetch 并随构建树就位）：

    export PATH="/d/qt/Tools/mingw1310_64/bin:$PATH"
    QTDIR="D:/qt/6.8.3/mingw_64"
    g++ -std=c++17 -O1 -o build/raw_probe.exe tools/raw_probe.cpp \
        src/core/RawDecode.cpp -I src \
        -I "$QTDIR/include" -I "$QTDIR/include/QtCore" -I "$QTDIR/include/QtGui" \
        -L "$QTDIR/lib" -lQt6Core -lQt6Gui

运行（Qt bin 必须在 PATH；参数 = 一个真实 RAW + 一个普通 JPEG 作负例）：

    cd build && export PATH="D:/qt/6.8.3/mingw_64/bin:$PATH" \
        && export QT_QPA_PLATFORM=offscreen \
        && ./raw_probe.exe /d/la_deps/rawqa/sample1.dng /d/la_deps/rawqa/notraw.jpg

实测读数（Canon EOS 350D DNG，LibRaw 0.22.2）：解码 **609ms**，
PPM **1737x1157**，Qt 读出非 null；负例（非 RAW）正确失败。

**本探针的存在理由**：中间格式选 PPM 而非 TIFF 是**实测**定的，不是拍的 ——
本机 Qt 插件只有 qgif/qico/qjpeg/qsvg，**无 qtiff.dll**，实测 TIFF 读出 `null / 0x0`；
而 PPM 属 Qt **内建** handler（`pbm,pgm,ppm,xbm,xpm`），不依赖插件部署。
改动中间格式前请先用本探针复测。

## raw_counterexample2.sh — RAW 门禁反例验证（2.28.0）

验证 `LA_RAW_QA` 的反例**真能转红**（铁律 22/23）。跑法：

    bash tools/raw_counterexample2.sh 0   # 正确实现 -> 绿
    bash tools/raw_counterexample2.sh 1   # 缺陷实现 -> 红

注入的缺陷形态 = 「`rc==0` 即认为成功 + 产物名按『替换扩展名』猜测（而非追加）」
—— 这正是真实会犯的错，且**只有在合法 DNG 上才暴露**。

实测结论（关键，别推翻）：**两种模式下 `dcraw_emu` 的 rc 都是 0**，
差别只在 `derived=x.dng.ppm exists=1` vs `derived=x.ppm exists=0`。
即 **exit code 不携带任何信息，产物校验是唯一判据** —— 这正是
`RawDecode::decodeToTempPpm` 绝不判 exit code 的原因。

★ 历史上还有一版 v1 反例注入「只信 exit code」，**已作废**：实测畸形输入时
`dcraw_emu` rc=2，两种实现都失败 → 红绿不分、无法判别。
教训（铁律 23）：**反例必须先实测被注入点的真实行为**，否则测的是不存在的缺陷。

# -*- coding: utf-8 -*-
# P29 无标题栏：CRLF 安全补丁（读-替换-断言 count==1-写回）
import os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
H = os.path.join(ROOT, "src", "ui", "MainWindow.h")
C = os.path.join(ROOT, "src", "ui", "MainWindow.cpp")

def load(p):
    with open(p, "rb") as f:
        data = f.read()
    assert b"\r\n" in data, "源文件不是 CRLF：" + p
    return data.decode("utf-8").replace("\r\n", "\n")

def save(p, s):
    with open(p, "wb") as f:
        f.write(s.replace("\n", "\r\n").encode("utf-8"))

def rep(s, old, new, label):
    n = s.count(old)
    if n != 1:
        raise SystemExit("FAIL [%s] count=%d (期望 1)" % (label, n))
    return s.replace(old, new, 1)

# ---------------- MainWindow.h ----------------
h = load(H)

h = rep(h,
    "    void resizeEvent(QResizeEvent* e) override;\n",
    "    void resizeEvent(QResizeEvent* e) override;\n"
    "    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;\n"
    "    void changeEvent(QEvent* e) override;\n",
    "H1 nativeEvent/changeEvent 声明")

h = rep(h,
    "    QWidget*        topbar_ = nullptr;      // P17：顶栏句柄（首页隐藏全局 chrome）\n",
    "    QWidget*        topbar_ = nullptr;      // P17：顶栏句柄（首页隐藏全局 chrome）\n"
    "    // P29 无标题栏：常驻窗口控制簇（min/max/close），独立于 topbar_，首页隐藏顶栏时仍可用。\n"
    "    QWidget*        winCtl_   = nullptr;\n"
    "    QPushButton*    winMin_   = nullptr;\n"
    "    QPushButton*    winMax_   = nullptr;\n"
    "    QPushButton*    winClose_ = nullptr;\n",
    "H2 winCtl_ 成员")

save(H, h)
print("MainWindow.h patched OK")

# ---------------- MainWindow.cpp ----------------
c = load(C)

# C1 增加 windows.h（仅 Windows）与 QComboBox
c = rep(c,
    "#include <cstdio>\n\nnamespace {\n",
    "#include <cstdio>\n\n"
    "#include <QComboBox>\n\n"
    "#ifdef Q_OS_WIN\n"
    "#include <windows.h>   // MinGW os_defines.h 已定义 NOMINMAX，避免 min/max 宏污染\n"
    "#endif\n\n"
    "namespace {\n",
    "C1 头文件")

# C2 命名空间内静态辅助：判定交互控件
c = rep(c,
    "const char* kNavIcons[] = { \"home\", \"map\", \"gallery\", \"cull\", \"retouch\", \"enhance\", \"log\" };\n",
    "const char* kNavIcons[] = { \"home\", \"map\", \"gallery\", \"cull\", \"retouch\", \"enhance\", \"log\" };\n\n"
    "// P29 无标题栏：判定某点下的控件是否「交互型」（按钮/输入/列表/滚动等）→\n"
    "// 命中交互控件时不抢作窗口拖拽区（WM_NCHITTEST 用）。\n"
    "static bool isInteractiveAt(QWidget* w)\n"
    "{\n"
    "    while (w) {\n"
    "        if (qobject_cast<QPushButton*>(w) || qobject_cast<QToolButton*>(w) ||\n"
    "            qobject_cast<QLineEdit*>(w)   || qobject_cast<QListWidget*>(w) ||\n"
    "            qobject_cast<QScrollArea*>(w) || qobject_cast<QAbstractSlider*>(w) ||\n"
    "            qobject_cast<QComboBox*>(w))\n"
    "            return true;\n"
    "        w = w->parentWidget();\n"
    "    }\n"
    "    return false;\n"
    "}\n",
    "C2 isInteractiveAt")

# C3 ctor 内设置无边框
c = rep(c,
    "    : QMainWindow(parent), lib_(lib), dbPath_(dbPath)\n"
    "{\n"
    "    auto* central = new QWidget(this);\n",
    "    : QMainWindow(parent), lib_(lib), dbPath_(dbPath)\n"
    "{\n"
    "    // P29 无标题栏：移除原生 Windows 标题栏，改由自定义顶栏 + 常驻控制簇（winCtl_）承载。\n"
    "    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);\n"
    "    auto* central = new QWidget(this);\n",
    "C3 setWindowFlags")

# C4 顶栏右侧预留控制簇宽度
c = rep(c,
    "    tl->addWidget(importBtn);\n",
    "    tl->addWidget(importBtn);\n"
    "    tl->addSpacing(120);   // P29 预留窗口控制簇宽度，避免导入钮被 winCtl_ 覆盖\n",
    "C4 顶栏右间距")

# C5 创建 winCtl_ 控制簇（seam 之后）
c = rep(c,
    "    seam_->lower();                              // 底色层之下（topShade_ 是 0 号）\n"
    "    seam_->hide();                               // P19：初始透明态（visible 由 applyTopSolid 接管）\n"
    "\n"
    "    // ---- P19：模块名称条退役——moduleLbl_ 已迁入悬浮顶栏；kpis_ 保留对象\n",
    "    seam_->lower();                              // 底色层之下（topShade_ 是 0 号）\n"
    "    seam_->hide();                               // P19：初始透明态（visible 由 applyTopSolid 接管）\n"
    "\n"
    "    // P29 无标题栏：常驻窗口控制簇（min/max/close）。独立父于 topbar_，首页隐藏顶栏时仍可用；\n"
    "    // 顶栏右侧已预留 120px 间距，避免导入钮被覆盖。悬浮于内容区右上角。\n"
    "    winCtl_ = new QWidget(right);\n"
    "    winCtl_->setFixedSize(116, 36);\n"
    "    winCtl_->setStyleSheet(QStringLiteral(\"background:transparent;\"));\n"
    "    auto* wl = new QHBoxLayout(winCtl_);\n"
    "    wl->setContentsMargins(0, 0, 2, 0);\n"
    "    wl->setSpacing(4);\n"
    "    wl->addStretch(1);\n"
    "    winMin_   = new QPushButton(QStringLiteral(\"—\"), winCtl_);   // 最小化\n"
    "    winMax_   = new QPushButton(QStringLiteral(\"▢\"), winCtl_);   // 最大化/还原\n"
    "    winClose_ = new QPushButton(QStringLiteral(\"✕\"), winCtl_);   // 关闭\n"
    "    for (QPushButton* b : {winMin_, winMax_, winClose_}) {\n"
    "        b->setFixedSize(32, 32);\n"
    "        b->setCursor(Qt::PointingHandCursor);\n"
    "        b->setStyleSheet(Lumen::btnGhost());\n"
    "    }\n"
    "    winClose_->setStyleSheet(QStringLiteral(\n"
    "        \"QPushButton{background:#131a15;border:1px solid #4a2630;border-radius:9px;color:#e6a3a3;}\"\n"
    "        \"QPushButton:hover{background:#2a1318;border-color:#e06a6a;color:#ffd7d7;}\"));\n"
    "    wl->addWidget(winMin_);\n"
    "    wl->addWidget(winMax_);\n"
    "    wl->addWidget(winClose_);\n"
    "    connect(winMin_, &QPushButton::clicked, this, &QWidget::showMinimized);\n"
    "    connect(winMax_, &QPushButton::clicked, this, [this] {\n"
    "        if (isMaximized()) showNormal(); else showMaximized();\n"
    "    });\n"
    "    connect(winClose_, &QPushButton::clicked, this, &QWidget::close);\n"
    "    winCtl_->raise();\n"
    "\n"
    "    // ---- P19：模块名称条退役——moduleLbl_ 已迁入悬浮顶栏；kpis_ 保留对象\n",
    "C5 winCtl_ 创建")

# C6 syncTopbarFloat 内钉住控制簇
c = rep(c,
    "    topbar_->setGeometry(navW, 0, w, 54);\n"
    "    topbar_->raise();                    // 悬浮于 stack_ 之上\n"
    "    topShade_->setGeometry(0, 0, w, 54); // 底色层不进布局，必须手动铺满\n"
    "    seam_->setGeometry(0, 52, w, 2);\n"
    "}\n",
    "    topbar_->setGeometry(navW, 0, w, 54);\n"
    "    topbar_->raise();                    // 悬浮于 stack_ 之上\n"
    "    topShade_->setGeometry(0, 0, w, 54); // 底色层不进布局，必须手动铺满\n"
    "    seam_->setGeometry(0, 52, w, 2);\n"
    "    if (winCtl_) {                       // P29：常驻控制簇钉在内容区右上角\n"
    "        winCtl_->move(host->width() - winCtl_->width(), 9);\n"
    "        winCtl_->raise();\n"
    "    }\n"
    "}\n",
    "C6 winCtl_ 定位")

# C7 新增 nativeEvent + changeEvent（resizeEvent 之后）
c = rep(c,
    "    if (lightbox_ && lightbox_->isVisible())\n"
    "        lightbox_->setGeometry(rect());\n"
    "    syncTopbarFloat();                   // P19：悬浮顶栏跟随窗口\n"
    "}\n"
    "\n"
    "// P16 隐私库：顶栏锁按钮分流 —— 未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁。\n",
    "    if (lightbox_ && lightbox_->isVisible())\n"
    "        lightbox_->setGeometry(rect());\n"
    "    syncTopbarFloat();                   // P19：悬浮顶栏跟随窗口\n"
    "}\n"
    "\n"
    "// P29 无标题栏：拦截 WM_NCHITTEST，把窗口顶部带（顶栏/首页沉浸式头部）模拟成标题栏\n"
    "// （HTCAPTION → 拖拽 + 双击最大化 + Aero 吸附），四周 6px 模拟可缩放边。命中交互控件\n"
    "// 时不抢占，保证按钮/输入/列表正常工作。另处理窗口状态变化更新最大化图标。\n"
    "#ifdef Q_OS_WIN\n"
    "bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)\n"
    "{\n"
    "    Q_UNUSED(eventType);\n"
    "    MSG* msg = static_cast<MSG*>(message);\n"
    "    if (msg->message == WM_NCHITTEST) {\n"
    "        const int frame = 6;       // 可缩放边宽\n"
    "        const int band  = 54;      // 顶部拖拽带高（对齐顶栏）\n"
    "        const QRect r = rect();\n"
    "        const QPoint p = mapFromGlobal(QCursor::pos());\n"
    "        const int x = p.x(), y = p.y();\n"
    "        if (x < frame && y < frame)               { *result = HTTOPLEFT;     return true; }\n"
    "        if (x > r.width() - frame && y < frame)   { *result = HTTOPRIGHT;    return true; }\n"
    "        if (x < frame && y > r.height() - frame)  { *result = HTBOTTOMLEFT;  return true; }\n"
    "        if (x > r.width() - frame && y > r.height() - frame) { *result = HTBOTTOMRIGHT; return true; }\n"
    "        if (x < frame)              { *result = HTLEFT;   return true; }\n"
    "        if (x > r.width() - frame)  { *result = HTRIGHT;  return true; }\n"
    "        if (y < frame)              { *result = HTTOP;    return true; }\n"
    "        if (y > r.height() - frame) { *result = HTBOTTOM; return true; }\n"
    "        if (y < band && !isInteractiveAt(childAt(p))) { *result = HTCAPTION; return true; }\n"
    "    }\n"
    "    return QMainWindow::nativeEvent(eventType, message, result);\n"
    "}\n"
    "#else\n"
    "bool MainWindow::nativeEvent(const QByteArray&, void*, qintptr*) { return false; }\n"
    "#endif\n"
    "\n"
    "void MainWindow::changeEvent(QEvent* e)\n"
    "{\n"
    "    if (e->type() == QEvent::WindowStateChange && winMax_)\n"
    "        winMax_->setText(isMaximized() ? QStringLiteral(\"❐\") : QStringLiteral(\"▢\"));\n"
    "    QMainWindow::changeEvent(e);\n"
    "}\n"
    "\n"
    "// P16 隐私库：顶栏锁按钮分流 —— 未初始化 → 设密码；锁定 → 输密码解锁；已解锁 → 重新上锁。\n",
    "C7 nativeEvent/changeEvent")

save(C, c)
print("MainWindow.cpp patched OK")
print("ALL P29 PATCHES APPLIED")

// LightAlbum Native — P1 入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 冒烟：LA_SMOKE=1 + QT_QPA_PLATFORM=offscreen → 载入真库后自动退出并打 LA_P1_SMOKE_OK
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <cstdio>
#include <cmath>
#include "core/Library.h"
#include "core/LumenEngine.h"
#include "core/LumenAgent.h"
#include "core/GeoIndex.h"
#include "core/RtHistory.h"
#include "core/Scanner.h"
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/GalleryPage.h"
#include "ui/GalleryModel.h"
#include "ui/GalleryCardPainter.h"
#include "ui/RetouchPage.h"
#include "ui/HomePage.h"
#include "ui/DarkPalette.h"
#include "ui/Theme.h"
#include "ui/Splash.h"
#include "ui/LogPage.h"
#include "core/MotionPref.h"
#include "core/Vault.h"
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>

namespace {
// P9 AI 修图智能体冒烟：合成「偏暗偏暖发灰」样张 → 测量 → 决策 → 执行。
// 真实路径（默认 Ollama 127.0.0.1:11434，机器没装则自动落兜底）+ 强制兜底路径（端口 9）。
int runAgentSmoke()
{
    // 合成样张：暗 + 暖（R>B）+ 低对比
    QImage img(256, 256, QImage::Format_RGB32);
    for (int y = 0; y < 256; ++y) {
        QRgb* ln = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < 256; ++x)
            ln[x] = qRgb(52 + x / 16, 44 + y / 20, 30);   // 暗、轻微暖调、平缓（发灰）
    }
    int pass = 1;

    // ① 真实决策（Ollama 在线 → ollama；未装 → 自动 local 兜底，两者都算结构合法）
    const AgentContext ctx = LumenAgent::contextFrom(img, 3200, 0.0, QString());
    const AgentPlan real = LumenAgent::decide(ctx);
    bool realOk = real.ok && !real.params.isEmpty();
    for (auto it = real.params.cbegin(); it != real.params.cend(); ++it)
        if (it.value() < -1.0 || it.value() > 1.0) realOk = false;
    const QImage outReal = LumenEngine::retouchWithPlan(img, real.params);
    realOk = realOk && !outReal.isNull();
    std::fprintf(stderr, "AGENT real src=%s model=%s intent=%s params=%d out=%d reason=%s\n",
                 qPrintable(real.source), qPrintable(real.model),
                 qPrintable(real.intent), real.params.size(), outReal.isNull() ? 0 : 1,
                 qPrintable(real.reason));
    if (!realOk) pass = 0;

    // ② 强制兜底：LLM 指向关闭端口 → 秒退到本地规则（350ms 探测）
    const QByteArray savedUrl = qgetenv("LA_LLM_URL");
    qputenv("LA_LLM_URL", "http://127.0.0.1:9");
    const AgentPlan fb = LumenAgent::decide(LumenAgent::contextFrom(img, 3200, 0.0, QStringLiteral("通透")));
    qputenv("LA_LLM_URL", savedUrl);
    const bool fbOk = fb.ok && fb.source == QStringLiteral("local")
                   && !fb.params.isEmpty()
                   && (fb.reason.contains(QStringLiteral("未连接"))
                       || fb.reason.contains(QStringLiteral("兜底")));
    std::fprintf(stderr, "AGENT fallback src=%s intent=%s brightness=%.2f warmth=%.2f denoise=%.2f\n",
                 qPrintable(fb.source), qPrintable(fb.intent),
                 fb.params.value(QStringLiteral("brightness"), 0),
                 fb.params.value(QStringLiteral("warmth"), 0),
                 fb.params.value(QStringLiteral("denoise"), 0));
    if (!fbOk) pass = 0;

    // ③ 兜底计划方向断言：暗样张应正提亮；ISO3200 应有降噪； goal 通透 → clarity ≥ 0
    if (fb.params.value(QStringLiteral("brightness"), 0) <= 0) pass = 0;
    if (fb.params.value(QStringLiteral("denoise"), 0) < 0.30) pass = 0;
    if (fb.params.value(QStringLiteral("clarity"), 0) < 0.10) pass = 0;

    // ④ clampPlan 白名单：未知键丢弃、越界截断
    QMap<QString, double> dirty;
    dirty.insert(QStringLiteral("brightness"), 5.0);
    dirty.insert(QStringLiteral("evil"), 1.0);
    const QMap<QString, double> cl = LumenAgent::clampPlan(dirty);
    const bool clampOk = cl.size() == 1 && cl.value(QStringLiteral("brightness")) == 1.0;
    if (!clampOk) pass = 0;

    std::fprintf(stderr, "LA_AGENT_SMOKE %s real=%s fb=%s clamp=%d\n",
                 pass ? "PASS" : "FAIL", realOk ? "1" : "0", fbOk ? "1" : "0", clampOk ? 1 : 0);
    std::fflush(stderr);
    return pass ? 0 : 1;
}
} // namespace（P9 agent 冒烟）

namespace {
// P10 离线逆地理编码冒烟：已知点断言（pip / 城市兜底 / 海上诚实空）+ 真库回填统计。
int runGeoSmoke()
{
    GeoIndex& gi = GeoIndex::instance();
    const bool loaded = gi.ensureLoaded();
    int pass = loaded ? 1 : 0;
    // 数据完整性自检：中国城市省名不得残留英文（P-geo-prov-cn 债务清偿验证）
    {
        const QVector<QString> bad = gi.verifyChinaProvinces();
        for (const QString& b : bad)
            std::fprintf(stderr, "GEO badprov %s\n", qPrintable(b));
        if (!bad.isEmpty()) pass = 0;
    }
    struct P { double lat, lon; const char* r; const char* pv; const char* ct; };
    static const P pts[] = {
        {64.15, -21.94,  "iceland", "Capital Region",   "雷克雅未克"},
        // DataV 34 省切换后，多边形主路径返回中文省名（北京市/广东省/辽宁省）。
        // kCities 英文兜底债务已清偿（本迭代）：所有 china 城市 prov 改为精确中文省名，
        // 并由 verifyChinaProvinces() 在门禁内逐条校验（含"无 ASCII 字母 + 在 34 省白名单"）。
        // 以下期望对齐设计内中文真值（主路径命中多边形）。
        {39.90, 116.41,  "china",   "北京市", "北京"},
        {23.13, 113.26,  "china",   "广东省", "广州"},
        {38.91, 121.61,  "china",   "辽宁省", "大连"},   // 主路径命中辽宁省多边形
        {30.57, 104.07,  "china",   "四川省", "成都"},   // 主路径命中四川省多边形
        {15.46, 111.13,  "",        "",                 ""},       // 南海海域 → 诚实空
    };
    for (const P& p : pts) {
        const GeoResult g = gi.resolve(p.lat, p.lon);
        // 中文字面量必须 fromUtf8（QLatin1String 按 Latin-1 解，中文必错——PathGuess 同款教训）
        const bool ok = g.region == QString::fromUtf8(p.r)
                     && g.prov == QString::fromUtf8(p.pv)
                     && g.city == QString::fromUtf8(p.ct);
        std::fprintf(stderr, "GEO %s lat=%.2f lon=%.2f -> r=%s pv=%s ct=%s\n",
                     ok ? "ok" : "FAIL", p.lat, p.lon,
                     qPrintable(g.region), qPrintable(g.prov), qPrintable(g.city));
        if (!ok) pass = 0;
    }
    // 真库回填统计（读时回填，KPI 省份数应显著 > 0）
    int enriched = 0, total = 0, provCount = 0;
    if (qEnvironmentVariableIsSet("LA_DB")) {
        Library lib;
        QString err;
        if (lib.open(qEnvironmentVariable("LA_DB"), &err)) {
            for (const Photo& p : lib.photos()) {
                ++total;
                if (!p.region.isEmpty()) ++enriched;
            }
            provCount = lib.kpi().provCount;
        } else {
            std::fprintf(stderr, "GEO db open fail: %s\n", qPrintable(err));
        }
    }
    std::fprintf(stderr, "GEO realdb total=%d enriched=%d provCount=%d\n",
                 total, enriched, provCount);
    if (total > 0 && (enriched * 100 / total) < 40) pass = 0;   // 真库点亮 ≥40%
    std::fprintf(stderr, "LA_GEO_SMOKE %s loaded=%d\n", pass ? "PASS" : "FAIL", loaded ? 1 : 0);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 B：撤销/重做栈纯状态机断言（直接驱动 RtHistory，无需 UI / 库）。
int runRetouchUndoQa()
{
    RtHistory h;
    RtEditState a, b, c, d;
    a.cur[QStringLiteral("brightness")] = 0.0; a.rot = 0; a.angle = 0.0;
    b.cur[QStringLiteral("brightness")] = 0.3; b.rot = 0; b.angle = 0.0;
    c.cur[QStringLiteral("brightness")] = 0.3; c.rot = 1; c.angle = 5.0;
    c.cropNorm = QRectF(0.1, 0.1, 0.5, 0.5);
    d.cur[QStringLiteral("brightness")] = 0.5; d.rot = 2; d.angle = 0.0;

    h.clear(a);
    int marks = 0, checks = 0;
    auto chk = [&](bool ok){ ++checks; if (ok) ++marks; };

    chk(!h.canUndo() && !h.canRedo());                  // 基线：无撤销/重做
    h.push(b);
    chk(h.canUndo() && !h.canRedo());                  // 一次编辑后可撤不可重
    h.push(c);
    chk(h.canUndo() && h.pos() == 2 && h.size() == 3); // 两步后位于栈顶
    chk(h.undo() == b && h.pos() == 1 && h.canRedo()); // undo -> b
    chk(h.undo() == a && h.pos() == 0 && !h.canUndo());// undo -> a（栈底）
    chk(h.redo() == b && h.pos() == 1);                // redo -> b
    h.push(d);                                          // 在 b 之后 push -> 截断 c
    chk(!h.canRedo() && h.size() == 3 && h.pos() == 2);// push 截断 redo 尾
    const int before = h.size();
    h.push(d);                                          // 无变化不记
    chk(h.size() == before);
    chk(h.current() == d && h.current().rot == 2);      // 当前态等于 d

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_RETOUCH_UNDO_QA %s marks=%d checks=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 C 第一步：相册内搜索过滤断言（直接驱动 GalleryModel，无需 UI / 库）。
int runGallerySearchQa()
{
    // 重要：main() 入口已构造 QApplication（line ~417），QA 派发在其后。
    // 此处**禁止**再建 QCoreApplication —— 双 QApplication 退场会 SIGSEGV（实测 EXIT=139）。
    // GalleryModel 仅做同步数据运算（setSource/setFilter/rebuild/rowCount），复用既有的 QApplication 即可。

    auto mk = [](const QString& title, const QString& cam, const QString& region,
                 const QString& prov, const QString& city, const QString& date, int rating) {
        Photo p;
        p.title = title; p.cam = cam; p.region = region; p.prov = prov;
        p.city = city; p.date = date; p.rating = rating; p.thumbFile = QStringLiteral("t");
        return p;
    };
    QVector<Photo> phs;
    phs.append(mk(QStringLiteral("黄山日出"), QStringLiteral("Canon EOS R5"),
                  QStringLiteral("china"), QStringLiteral("安徽省"), QStringLiteral("黄山市"),
                  QStringLiteral("2023-05-01"), 5));
    phs.append(mk(QStringLiteral("东京夜景"), QStringLiteral("Sony A7"),
                  QStringLiteral("japan"), QStringLiteral("Tokyo"), QStringLiteral("东京"),
                  QStringLiteral("2022-11-20"), 4));
    phs.append(mk(QStringLiteral("家猫"), QStringLiteral("iPhone"),
                  QStringLiteral("china"), QStringLiteral("北京市"), QStringLiteral("北京"),
                  QStringLiteral("2024-01-12"), 3));
    phs.append(mk(QStringLiteral("冰岛风光"), QStringLiteral("Nikon"),
                  QStringLiteral("iceland"), QStringLiteral("Capital Region"), QStringLiteral("雷克雅未克"),
                  QStringLiteral("2021-07-10"), 5));

    GalleryModel m;
    m.setSource(&phs);

    int marks = 0, checks = 0;
    auto chk = [&](bool ok){ ++checks; if (ok) ++marks; };
    auto cnt = [&](){ return m.rowCount(); };

    m.setFilter(QStringLiteral("北京"));   chk(cnt() == 1);   // 北京市/北京 命中 1
    m.setFilter(QStringLiteral("china"));  chk(cnt() == 2);   // region 命中 2
    m.setFilter(QStringLiteral("2023"));    chk(cnt() == 1);   // 年份 1
    m.setFilter(QStringLiteral("5"));       chk(cnt() == 2);   // 裸串"5"命中 2 张（黄山/冰岛均含 5；其余日期/器材不含裸 5）
    m.setFilter(QStringLiteral("Canon"));   chk(cnt() == 1);   // 器材 1
    m.setFilter(QStringLiteral("夜景"));    chk(cnt() == 1);   // 标题关键词 1
    m.setFilter(QString());                 chk(cnt() == 4);   // 清空恢复全部

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_GALLERY_SEARCH_QA %s marks=%d checks=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 D 第一步：真实库回归门禁（规模化与流畅性）。
// 加载真实库（env LA_SCALE_DB 优先 → LA_DB → 从 exe 目录向上搜索 _scale_fixture.db），
// 断言 open 成功、照片数 ≥ LA_SCALE_MIN（默认 1000，镜像真库规模）、kpi 已填充；
// 构造 GalleryPage（真实图库网格）+ MapView（真实 GPS 足迹）offscreen 不崩、首屏渲染不超时。
// 重要：main() 已建 QApplication（line ~417），此处严禁再建 QApplication —— 双实例退场 SIGSEGV。
//       本函数复用既有 QApplication，自建 Library 打开真实库（Library 仅为 QSqlDatabase 包装，
//       不依赖 main 的 lib）；构造页面仅需 QApplication 已存在即可。
int runScaleSmoke()
{
    QString db = qEnvironmentVariable("LA_SCALE_DB");
    if (db.isEmpty()) db = qEnvironmentVariable("LA_DB");
    if (db.isEmpty()) {
        // 默认：从可执行文件目录向上搜索合成 scale fixture（提交于仓库根，镜像真库 6425 规模）
        QString dir = QCoreApplication::applicationDirPath();
        for (int i = 0; i < 6; ++i) {
            const QString cand = dir + QStringLiteral("/_scale_fixture.db");
            if (QFileInfo::exists(cand)) { db = cand; break; }
            const QString up = QDir(dir).filePath(QStringLiteral(".."));
            if (up == dir) break;
            dir = QDir::cleanPath(up);
        }
    }
    const int minCount = qEnvironmentVariable("LA_SCALE_MIN", QStringLiteral("1000")).toInt();
    const qint64 mapBudgetMs = qEnvironmentVariable("LA_SCALE_MAP_MS", QStringLiteral("15000")).toLongLong();

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_SMOKE FAIL open=%s (%s)\n",
                     qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const Kpi& k = lib.kpi();
    const int n = int(phs.size());

    bool ok = true;
    QStringList fails;
    if (k.total != n) { ok = false; fails << "kpi.total!=n"; }
    if (n < minCount) { ok = false; fails << QStringLiteral("photos=%1<min=%2").arg(n).arg(minCount); }
    if (k.yearCount < 1) { ok = false; fails << "yearCount<1"; }
    if (k.provCount < 1) { ok = false; fails << "provCount<1"; }

    // 关键页面 1：真实图库网格（EditorialView 窗口化，仅可见行出绘制，构造/首屏不崩）
    int galleryCtrls = -1;
    {
        GalleryPage page(&phs, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        galleryCtrls = page.findChildren<QWidget*>().size();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    // 关键页面 2：足迹地图（注入真实 GPS 预投影点）。测量单次首屏 paint 周期。
    int gpsCount = 0;
    qint64 paintMs = -1;
    {
        MapView view(nullptr);
        QVector<QPointF> pts;
        pts.reserve(n);
        for (const Photo& p : phs) {
            if (!p.hasGps) continue;
            const double x = (p.lo + 180.0) / 360.0 * MapView::MAP_W;
            const double y = (90.0 - p.la) / 180.0 * MapView::MAP_H;
            pts.append(QPointF(x, y));
        }
        gpsCount = pts.size();
        view.setGpsPoints(pts);
        view.setGpsCount(gpsCount);
        view.resize(1200, 800);
        view.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100); // 初始 show
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        view.update();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2000); // 单次 paint 周期
        paintMs = QDateTime::currentMSecsSinceEpoch() - t0;
        std::fprintf(stderr, "LA_SCALE_MAP mapMs=%lld gps=%d\n", paintMs, gpsCount);
        std::fflush(stderr);
    }

    if (galleryCtrls < 0) { ok = false; fails << "gallery_construct"; }
    if (paintMs < 0 || paintMs > mapBudgetMs) { ok = false; fails << QStringLiteral("mapPaint=%1>budget=%2").arg(paintMs).arg(mapBudgetMs); }

    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_SMOKE PASS photos=%d years=%d provs=%d span=%d-%d gps=%d galleryCtrls=%d mapMs=%lld db=%s\n",
            n, k.yearCount, k.provCount, k.minYear, k.maxYear, gpsCount, galleryCtrls, paintMs,
            qPrintable(QFileInfo(db).fileName()));
    } else {
        std::fprintf(stderr, "LA_SCALE_SMOKE FAIL reason=%s photos=%d gps=%d mapMs=%lld\n",
                     qPrintable(fails.join(QChar(';'))), n, gpsCount, paintMs);
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 D 第二步：规模化流畅性基线门禁（先测再改：量化首屏/滚动，守护回归）。
// 复用 runScaleSmoke 的库定位逻辑；构造 GalleryPage（EditorialView 窗口化网格），
// 量化 ① 首屏耗时（构造→首绘）② N 次滚动跳变帧 paint 均值/峰值（保持 <120ms 节流，
// 隔离纯 paint，不触发缩略图预取）。预算经 env 覆盖（默认宽松防 CI 误红），实测后校准。
// 重要：复用 main() 既有 QApplication，禁新建 QCoreApplication（双实例退场 SIGSEGV）。
int runScalePerf()
{
    QString db = qEnvironmentVariable("LA_SCALE_DB");
    if (db.isEmpty()) db = qEnvironmentVariable("LA_DB");
    if (db.isEmpty()) {
        QString dir = QCoreApplication::applicationDirPath();
        for (int i = 0; i < 6; ++i) {
            const QString cand = dir + QStringLiteral("/_scale_fixture.db");
            if (QFileInfo::exists(cand)) { db = cand; break; }
            const QString up = QDir(dir).filePath(QStringLiteral(".."));
            if (up == dir) break;
            dir = QDir::cleanPath(up);
        }
    }
    const qint64 firstBudgetMs  = qEnvironmentVariable("LA_SCALE_PERF_FIRST",  QStringLiteral("3000")).toLongLong();
    const qint64 scrollBudgetMs = qEnvironmentVariable("LA_SCALE_PERF_SCROLL", QStringLiteral("50")).toLongLong();
    const int    jumps          = qEnvironmentVariable("LA_SCALE_PERF_JUMPS",  QStringLiteral("20")).toInt();

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_PERF FAIL open=%s (%s)\n", qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const int n = int(phs.size());

    int galleryCtrls = -1;
    qint64 firstMs = -1, scrollAvgMs = -1, scrollMaxMs = -1;
    {
        const qint64 tA = QDateTime::currentMSecsSinceEpoch();
        GalleryPage page(&phs, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400); // 首屏 + 布局 + 可见区预取
        const qint64 tB = QDateTime::currentMSecsSinceEpoch();
        firstMs = tB - tA;
        galleryCtrls = page.findChildren<QWidget*>().size();

        // 滚动跳变帧 paint 测量：设 scrollbar 值 → update → 短 processEvents
        // （<120ms 节流，隔离纯 paint，不触发缩略图预取）。
        QScrollBar* sb = page.qaScrollBar();
        if (sb && sb->maximum() > 0 && jumps > 0) {
            qint64 sum = 0, mx = 0;
            for (int j = 1; j <= jumps; ++j) {
                const int v = int(sb->maximum() * j / (jumps + 1));
                const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
                sb->setValue(v);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t0;
                sum += dt; if (dt > mx) mx = dt;
            }
            scrollAvgMs = sum / jumps;
            scrollMaxMs = mx;
        } else {
            scrollAvgMs = 0; scrollMaxMs = 0;   // 内容未超出视口：无需滚动，成本记 0
        }
        std::fprintf(stderr,
            "LA_SCALE_PERF firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d jumps=%d\n",
            firstMs, scrollAvgMs, scrollMaxMs, galleryCtrls, jumps);
        std::fflush(stderr);
    }

    bool ok = (n >= 1000)
            && (firstMs >= 0 && firstMs <= firstBudgetMs)
            && (scrollMaxMs >= 0 && scrollMaxMs <= scrollBudgetMs);
    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_PERF PASS photos=%d firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d db=%s\n",
            n, firstMs, scrollAvgMs, scrollMaxMs, galleryCtrls, qPrintable(QFileInfo(db).fileName()));
    } else {
        QStringList fails;
        if (n < 1000) fails << QStringLiteral("photos=%1<1000").arg(n);
        if (firstMs < 0 || firstMs > firstBudgetMs) fails << QStringLiteral("firstMs=%1>budget=%2").arg(firstMs).arg(firstBudgetMs);
        if (scrollMaxMs < 0 || scrollMaxMs > scrollBudgetMs) fails << QStringLiteral("scrollMaxMs=%1>budget=%2").arg(scrollMaxMs).arg(scrollBudgetMs);
        std::fprintf(stderr, "LA_SCALE_PERF FAIL reason=%s photos=%d firstMs=%lld scrollMaxMs=%lld\n",
                     qPrintable(fails.join(QChar(';'))), n, firstMs, scrollMaxMs);
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}
} // namespace（P10 geo 冒烟）

namespace {
// P5 金样对拍冒烟：读 _gold_lumen/{in,gold,steps}_*.png/json，跑 C++ auto 管线，
// 输出 mean-abs-diff + steps 序列一致性（PIL 滤波器非字节级可复刻 → 阈值容差）。
int runLumenSmoke()
{
    const QString dir = qEnvironmentVariable("LA_GOLD_DIR",
        QStringLiteral("D:/Test/workbuddy/2026-09-18-09-00-13/_gold_lumen"));
    const double kMadLimit = 8.0;
    int n = 0, stepsOk = 0;
    double madSum = 0, madMax = 0;
    for (int i = 0; i < 8; ++i) {
        const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
        if (!QFileInfo::exists(inP)) break;
        const QImage in(inP);
        const QImage gold(QDir(dir).filePath(QStringLiteral("gold_%1.png").arg(i)));
        QFile mf(QDir(dir).filePath(QStringLiteral("steps_%1.json").arg(i)));
        if (in.isNull() || gold.isNull() || !mf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject meta = QJsonDocument::fromJson(mf.readAll()).object();
        const QJsonArray keysArr = meta.value(QStringLiteral("keys")).toArray();
        QStringList goldKeys;
        for (const QJsonValue& kv : keysArr) goldKeys.append(kv.toString());

        LumenReport rep;
        const QImage out = LumenEngine::autoPipeline(in, LumenPreset::byName(QStringLiteral("auto")), &rep);

        // mean abs diff（同尺寸前提；尺寸不一致直接 FAIL）
        double mad = 999;
        if (out.size() == gold.size()) {
            QImage a = out.convertToFormat(QImage::Format_RGB32);
            QImage b = gold.convertToFormat(QImage::Format_RGB32);
            long long sum = 0;
            for (int y = 0; y < a.height(); ++y) {
                const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    sum += std::abs(qRed(pa[x]) - qRed(pb[x]))
                         + std::abs(qGreen(pa[x]) - qGreen(pb[x]))
                         + std::abs(qBlue(pa[x]) - qBlue(pb[x]));
                }
            }
            mad = double(sum) / (a.width() * a.height() * 3.0);
        }
        QStringList keys;
        for (const LumenStep& s : rep.steps) keys.append(s.key);
        const bool sOk = (keys == goldKeys);
        ++n;
        stepsOk += sOk ? 1 : 0;
        madSum += mad;
        madMax = std::max(madMax, mad);
        std::fprintf(stderr, "LUMEN %d mad=%.3f steps_ok=%d keys=[%s] gold=[%s]\n",
                     i, mad, sOk ? 1 : 0, qPrintable(keys.join(',')),
                     qPrintable(goldKeys.join(',')));
    }
    const double avg = n ? madSum / n : 999;
    bool pass = n > 0 && stepsOk == n && madMax <= kMadLimit;
    std::fprintf(stderr, "LA_LUMEN_SMOKE_SUBSET n=%d madAvg=%.3f madMax=%.3f stepsOk=%d/%d %s\n",
                 n, avg, madMax, stepsOk, n, (stepsOk == n && madMax <= kMadLimit) ? "PASS" : "FAIL");

    // ---- P5-2 显式算子逐算子对拍（隔离比对）----
    static const char* kOps[] = {"film", "bw", "glow", "redeye", "vignette", "hdr", "structure",
                                "dodge", "dehaze", "chromatic", "portrait", "lens"};
    auto madOf = [&](const QImage& out, const QImage& gold) -> double {
        if (out.size() != gold.size()) return 999.0;
        QImage a = out.convertToFormat(QImage::Format_RGB32);
        QImage b = gold.convertToFormat(QImage::Format_RGB32);
        long long sum = 0;
        for (int y = 0; y < a.height(); ++y) {
            const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x)
                sum += std::abs(qRed(pa[x]) - qRed(pb[x]))
                     + std::abs(qGreen(pa[x]) - qGreen(pb[x]))
                     + std::abs(qBlue(pa[x]) - qBlue(pb[x]));
        }
        return double(sum) / (a.width() * a.height() * 3.0);
    };
    for (int i = 0; i < n; ++i) {
        for (const char* op : kOps) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(
                QStringLiteral("op_%1_%2.png").arg(QString::fromLatin1(op)).arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            QImage out;
            const QString opName = QString::fromLatin1(op);
            if (opName == QStringLiteral("film"))        out = LumenEngine::filmStyle(in, false);
            else if (opName == QStringLiteral("bw"))     out = LumenEngine::blackWhite(in);
            else if (opName == QStringLiteral("glow"))   out = LumenEngine::glowStyle(in);
            else if (opName == QStringLiteral("redeye")) out = LumenEngine::redeyeRemove(in);
            else if (opName == QStringLiteral("vignette")) out = LumenEngine::vignetteCorrect(in);
            else if (opName == QStringLiteral("hdr"))    out = LumenEngine::toneMapHdr(in);
            else if (opName == QStringLiteral("structure")) out = LumenEngine::structure(in);
            else if (opName == QStringLiteral("dodge"))     out = LumenEngine::dodge(in);
            else if (opName == QStringLiteral("dehaze"))    out = LumenEngine::dehaze(in);
            else if (opName == QStringLiteral("chromatic")) out = LumenEngine::chromaticCorrect(in);
            else if (opName == QStringLiteral("portrait"))  out = LumenEngine::portraitBeautify(in);
            else                                             out = LumenEngine::lensCorrect(in);
            const double mad = madOf(out, gold);
            std::fprintf(stderr, "LUMEN_OP %d %s mad=%.3f\n", i, op, mad);
            if (opName == QStringLiteral("lens") && qEnvironmentVariableIsSet("LA_LUMEN_DEBUG"))
                out.save(QDir(dir).filePath(QStringLiteral("cpp_lens_%1.png").arg(i)));
            if (mad > kMadLimit) pass = false;
        }
    }

    // ---- P5-4 retouch_with_plan 对拍（固定 13 项参数字典）----
    {
        QMap<QString,double> pp;
        pp[QStringLiteral("warmth")] = 0.3;     pp[QStringLiteral("brightness")] = 0.2;
        pp[QStringLiteral("contrast")] = 0.25;  pp[QStringLiteral("highlights")] = -0.3;
        pp[QStringLiteral("shadows")] = 0.4;    pp[QStringLiteral("whites")] = 0.1;
        pp[QStringLiteral("blacks")] = -0.2;    pp[QStringLiteral("saturation")] = 0.3;
        pp[QStringLiteral("vibrance")] = 0.4;   pp[QStringLiteral("clarity")] = 0.3;
        pp[QStringLiteral("tint")] = 0.2;       pp[QStringLiteral("denoise")] = 0.2;
        pp[QStringLiteral("sharpness")] = 0.3;
        static const double kPlanLimit = 4.0;
        for (int i = 0; i < n; ++i) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(QStringLiteral("op_plan_%1.png").arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            const QImage cpp = LumenEngine::retouchWithPlan(in, pp);
            const double mad = madOf(cpp, gold);
            if (qEnvironmentVariableIsSet("LA_LUMEN_DEBUG"))
                cpp.save(QDir(dir).filePath(QStringLiteral("cpp_plan_%1.png").arg(i)));
            std::fprintf(stderr, "LUMEN_PLAN %d mad=%.3f\n", i, mad);
            if (mad > kPlanLimit) pass = false;
        }
    }

    // ---- P5-4 水平校正对拍（含角度自检）----
    {
        static const double kLevelLimit = 6.0;
        for (int i = 0; i < 4; ++i) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_level_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(QStringLiteral("op_level_%1.png").arg(i));
            const QString mP = QDir(dir).filePath(QStringLiteral("level_meta_%1.json").arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            QFile mf(mP);
            double goldAngle = 0.0;
            if (mf.open(QIODevice::ReadOnly)) {
                goldAngle = QJsonDocument::fromJson(mf.readAll())
                                .object().value(QStringLiteral("angle")).toDouble();
                mf.close();
            }
            const double cppAngle = LumenEngine::levelDetectAngle(in);
            const double angErr = std::fabs(cppAngle - goldAngle);
            const double mad = madOf(LumenEngine::levelCorrect(in), gold);
            std::fprintf(stderr, "LUMEN_LEVEL %d cppAngle=%.2f goldAngle=%.2f angErr=%.2f mad=%.3f\n",
                         i, cppAngle, goldAngle, angErr, mad);
            if (angErr > 1.0 || mad > kLevelLimit) pass = false;
        }
    }
    std::fprintf(stderr, "LA_LUMEN_SMOKE %s\n", pass ? "PASS" : "FAIL");
    std::fflush(stderr);
    return pass ? 0 : 1;
}
} // namespace


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LightAlbum"));
    app.setOrganizationName(QStringLiteral("lightalbum"));
    Dark::apply(app);

    // P16.6 视觉QA：offscreen 字体库不枚举 CJK → 中文豆腐块。显式从文件加载中文字体，
    // 字体引擎直接持有字形（不依赖系统字体枚举），offscreen 截图即等于真实窗口效果。
    QString cjkFamily;
    {
        static const QStringList cands = {
            QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
            QStringLiteral("C:/Windows/Fonts/NotoSansSC-VF.ttf"),
            QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
            QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        };
        for (const QString& p : cands) {
            if (!QFileInfo::exists(p)) continue;
            const int id = QFontDatabase::addApplicationFont(p);
            if (id < 0) continue;
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) { cjkFamily = fams.first(); break; }
        }
        if (cjkFamily.isEmpty())
            std::fprintf(stderr, "LA_FONT no CJK font loaded (fallback to system)\n");
        else
            std::fprintf(stderr, "LA_FONT loaded CJK family=%s\n", qPrintable(cjkFamily));
        std::fflush(stderr);
    }
    Lumen::applyAppFont(app, cjkFamily);   // P11：中文优先（显式 CJK 家族或系统雅黑）

    // P17「Quiet Frame」衬线中文：大标题/章节名的书卷气来自显式加载的衬线 CJK 字体，
    // 拉丁字形仍由 Georgia 优先（families 兜底链在 Lumen::serifFamilies）。
    {
        static const QStringList serifCands = {
            QStringLiteral("C:/Windows/Fonts/NotoSerifSC-VF.ttf"),
            QStringLiteral("C:/Windows/Fonts/STSONG.TTF"),
            QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        };
        QString serifFamily;
        for (const QString& p : serifCands) {
            if (!QFileInfo::exists(p)) continue;
            const int id = QFontDatabase::addApplicationFont(p);
            if (id < 0) continue;
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) { serifFamily = fams.first(); break; }
        }
        Lumen::setSerifFamily(serifFamily);
        if (serifFamily.isEmpty())
            std::fprintf(stderr, "LA_FONT no serif CJK loaded (titles fall back to sans)\n");
        else
            std::fprintf(stderr, "LA_FONT loaded serif CJK family=%s\n", qPrintable(serifFamily));
        std::fflush(stderr);
    }
    app.setStyleSheet(Lumen::focusRing());   // P16.1：全局键盘焦点环（WCAG 可达性）

    // P5 金样对拍冒烟（无需库文件，最先跑）
    if (qEnvironmentVariableIsSet("LA_LUMEN_SMOKE"))
        return runLumenSmoke();

    // P9 AI 修图智能体冒烟（测量 → 决策 → retouchWithPlan；不依赖库文件）
    if (qEnvironmentVariableIsSet("LA_AGENT_SMOKE"))
        return runAgentSmoke();

    // P10 离线逆地理冒烟（已知点 + 真库回填统计）
    if (qEnvironmentVariableIsSet("LA_GEO_SMOKE"))
        return runGeoSmoke();

    // 方向 B：撤销/重做栈纯状态机断言（RtHistory 直接驱动）
    if (qEnvironmentVariableIsSet("LA_RETOUCH_UNDO_QA"))
        return runRetouchUndoQa();

    // 方向 C 第一步：相册内搜索过滤断言（GalleryModel 直接驱动）
    if (qEnvironmentVariableIsSet("LA_GALLERY_SEARCH_QA"))
        return runGallerySearchQa();

    // 方向 D 第一步：真实库回归门禁（规模化与流畅性，默认可跑 6425 张合成 fixture）
    if (qEnvironmentVariableIsSet("LA_SCALE_SMOKE"))
        return runScaleSmoke();

    // 方向 D 第二步：规模化流畅性基线门禁（量化首屏/滚动，守护回归）
    if (qEnvironmentVariableIsSet("LA_SCALE_PERF"))
        return runScalePerf();

    // P8 原生导入器冒烟：扫描 LA_SCAN_ROOT 入库到 LA_SCAN_DB（独立于正式库），
    // 打印增量结果后退出；同一命令重跑应 added=0 / unchanged=N（增量幂等）。
    if (qEnvironmentVariableIsSet("LA_SCAN_SMOKE")) {
        const ScanResult r = Scanner::syncRoot(
            qEnvironmentVariable("LA_SCAN_ROOT"),
            qEnvironmentVariable("LA_SCAN_DB"),
            qEnvironmentVariable("LA_SCAN_THUMBS"));
        std::fprintf(stderr,
                     "LA_SCAN_SMOKE added=%d updated=%d unchanged=%d missing=%d "
                     "placeholders=%d total=%d errors=%d secs=%.2f %s\n",
                     r.added, r.updated, r.unchanged, r.missing, r.placeholders,
                     r.total, r.errorCount, r.seconds,
                     r.errorCount == 0 ? "PASS" : "FAIL");
        std::fflush(stderr);
        return r.errorCount == 0 ? 0 : 1;
    }

    // P30-C 日志页控制台风格冒烟：合成 EXIF 数据集 → 构造 LogPage（含 recompute）→
    // 断言 KPI 已填充、状态变更可重算、图表 paintEvent 在 offscreen 下不崩（不抓图，避开 PNG 挂）。
    if (qEnvironmentVariableIsSet("LA_LOG_SMOKE")) {
        QVector<Photo> phs;
        const QStringList cams   = {QStringLiteral("Canon EOS R5"), QStringLiteral("Sony A7IV")};
        const QStringList lenses = {QStringLiteral("RF24-70mm F2.8"), QStringLiteral("FE 35mm F1.8")};
        for (int i = 0; i < 40; ++i) {
            Photo p;
            p.path   = QStringLiteral("synthetic/%1.jpg").arg(i);
            p.date   = QStringLiteral("2023-%1-15").arg((i % 12) + 1, 2, 10, QChar('0'));
            p.year   = 2023 + (i % 2);
            p.cam    = cams[i % 2];
            p.lens   = lenses[i % 2];
            p.ap     = (i % 3 == 0) ? QStringLiteral("f/2.8") : QStringLiteral("f/8");
            p.sh     = (i % 2) ? QStringLiteral("1/250") : QStringLiteral("1/2s");
            p.iso    = (i % 2) ? 100 : 1600;
            p.focal  = (i % 4 == 0) ? 16 : (i % 4 == 1 ? 35 : (i % 4 == 2 ? 85 : 200));
            p.rating = (i % 5 == 0) ? 5 : 0;
            p.region = QStringLiteral("china");
            if (i % 7 == 0) p.ap.clear();           // 缺失 EXIF：须跳过不硬塞
            if (i % 11 == 0) { p.iso = 0; p.focal = 0; }
            phs.append(p);
        }
        LogPage page(&phs, nullptr);
        page.setHidePrivate(false);                 // 触发状态变更重算（与 ctor 同路径）
        page.show();                                // offscreen：触发 paintEvent，不抓图
        QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
        const QList<QLabel*> allLbls = page.findChildren<QLabel*>();
        int totalKpi = -1; bool foundKpi = false;
        for (QLabel* l : allLbls) {
            const QString t = l->text();
            if (t.contains(QStringLiteral("作品总数"))) {
                const QRegularExpression re(QStringLiteral("<b[^>]*>(\\d+)</b>"));
                const QRegularExpressionMatch m = re.match(t);
                if (m.hasMatch()) { totalKpi = m.captured(1).toInt(); foundKpi = true; }
                break;
            }
        }
        const bool ok = foundKpi && totalKpi == 40 && allLbls.size() >= 10;
        std::fprintf(stderr, "LA_LOG_SMOKE_%s totalKpi=%d labels=%d\n",
                     ok ? "OK" : "FAIL", totalKpi, allLbls.size());
        // 横向溢出测量（P29-H 反馈修正：chips 内滚后整页不应被撑宽）
        QScrollArea* outer = nullptr;
        for (QScrollArea* s : page.findChildren<QScrollArea*>())
            if (s->parent() == &page) { outer = s; break; }
        QWidget* rootW = outer ? outer->widget() : nullptr;
        const int rootMin = rootW ? rootW->minimumSizeHint().width() : -1;
        const int pageMin = page.minimumSizeHint().width();
        std::fprintf(stderr, "LA_LOG_OVERFLOW rootMin=%d pageMin=%d\n", rootMin, pageMin);
        std::fflush(stderr);
        return ok ? 0 : 1;
    }

    Library lib;
    const QString dbPath  = qEnvironmentVariable("LA_DB", QStringLiteral("D:/lightalbum/library.db"));
    const QString thumbsDir = qEnvironmentVariable("LA_THUMBS", QStringLiteral("D:/lightalbum/thumbs"));
    QString err;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_DB_OPEN_FAIL %s (%s)\n",
                     qPrintable(dbPath), qPrintable(err));
        std::fflush(stderr);
        if (qEnvironmentVariableIsSet("LA_SMOKE")) return 2;
        return 1;
    }

    // P17 首页章节分组冒烟（纯逻辑，无 GUI）：章节数>0、章内非空、按数量降序、
    // 展示字段非空、关键词 ⊆ 真实分类；打印定位覆盖率后按结果退出。
    if (qEnvironmentVariableIsSet("LA_HOME_SMOKE")) {
        const auto& phs = lib.photos();
        const auto chs = HomePage::buildChapters(phs, 12);
        int located = 0, usable = 0;
        for (const Photo& p : phs) {
            if (p.priv || p.missing || p.thumbFile.isEmpty()) continue;
            ++usable;
            if (!p.city.isEmpty() || !p.prov.isEmpty() || !p.region.isEmpty()) ++located;
        }
        bool ok = !chs.isEmpty() && located > 0;
        int prevCount = 1 << 30;
        for (const auto& c : chs) {
            if (c.photos.isEmpty() || c.title.isEmpty()
                || c.capsLine.isEmpty() || c.subtitle.isEmpty()
                || (int)c.photos.size() > prevCount)
                ok = false;
            prevCount = int(c.photos.size());
        }
        std::fprintf(stderr, "LA_HOME_SMOKE_%s chapters=%d located=%d usable=%d "
                             "first=\"%s\"(%d 幅) kw=%d\n",
                     ok ? "OK" : "FAIL", chs.size(), located, usable,
                     chs.isEmpty() ? qPrintable(QString())
                                   : qPrintable(chs.first().title),
                     chs.isEmpty() ? 0 : chs.first().photos.size(),
                     chs.isEmpty() ? 0 : chs.first().keywords.size());
        std::fflush(stderr);
        return ok ? 0 : 1;
    }

    // P16 写回链路自测：LA_RW_SMOKE=1（必须指向临时库副本，绝不碰真库）。
    // 评分写→内存同步→SQL 读回；私密标记往返；meta KV 往返；vault 口令哈希/校验。
    if (qEnvironmentVariableIsSet("LA_RW_SMOKE")) {
        const auto& phs = lib.photos();
        if (phs.isEmpty()) {
            std::fprintf(stderr, "LA_RW_SMOKE_FAIL no photos\n");
            return 3;
        }
        const QString p0 = phs.first().path;
        bool r1 = lib.setRating(p0, 4);
        bool r1m = phs.first().rating == 4;
        bool r2 = lib.setPrivate(p0, true) && phs.first().priv;
        bool r3 = lib.setPrivate(p0, false) && !phs.first().priv;
        bool r4 = lib.setMetaValue(QStringLiteral("rw_smoke_key"),
                                   QStringLiteral("v123"));
        bool r5 = lib.metaValue(QStringLiteral("rw_smoke_key"))
                    == QStringLiteral("v123");
        const QByteArray salt = Vault::randomSalt();
        const QString h = Vault::hashHex(QStringLiteral("口令abc123"), salt);
        bool r6 = !h.isEmpty()
            && Vault::verify(QStringLiteral("口令abc123"), salt, h)
            && !Vault::verify(QStringLiteral("错密码"), salt, h);
        lib.setRating(p0, 0);   // 还原
        std::fprintf(stderr,
            "LA_RW_SMOKE_%s rating=%d ratingMem=%d priv=%d unpriv=%d meta=%d metaGet=%d vault=%d\n",
            (r1 && r1m && r2 && r3 && r4 && r5 && r6) ? "OK" : "FAIL",
            int(r1), int(r1m), int(r2), int(r3), int(r4), int(r5), int(r6));
        std::fflush(stderr);
        return (r1 && r1m && r2 && r3 && r4 && r5 && r6) ? 0 : 4;
    }

    MainWindow win(&lib, thumbsDir, dbPath);
    win.setWindowTitle(QStringLiteral("光影集 LightAlbum " LA_VERSION));
    win.resize(1440, 940);

    // P13 开场动画：仅在真实启动（非冒烟/截图）时播放，保证 CI 确定性。
    const bool animOk = !qEnvironmentVariableIsSet("LA_SMOKE")
        && !qEnvironmentVariableIsSet("LA_UI_SHOT")
        && !qEnvironmentVariableIsSet("LA_LIGHTBOX_SHOT")
        && !qEnvironmentVariableIsSet("LA_CULL_SMOKE")
        && !qEnvironmentVariableIsSet("LA_RETOUCH_SMOKE")
        && !qEnvironmentVariableIsSet("LA_RETOUCH_A_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_A_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_REGION_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_CLICK_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_BURST_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_COUNTRY_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_PERF")
        && !qEnvironmentVariableIsSet("LA_GALLERY_E_QA")
        && !qEnvironmentVariableIsSet("LA_CULL_DUEL_QA")
        && !qEnvironmentVariableIsSet("LA_GALLERY_FILL_QA")
        && !qEnvironmentVariableIsSet("LA_SCALE_PERF")
        ;

    QRect avail = QApplication::primaryScreen()->availableGeometry();
    QRect fullRect((avail.width() - 1440) / 2, (avail.height() - 940) / 2, 1440, 940);

    // P13 开场动画探针：LA_SPLASH_SHOT=<dir> 时抓 3 帧（开合/停留/成缝）存 PNG 后退出。
    if (qEnvironmentVariableIsSet("LA_SPLASH_SHOT")) {
        const QString dir = qEnvironmentVariable("LA_SPLASH_SHOT");
        QDir().mkpath(dir);
        SplashWindow sp(fullRect);
        QTimer::singleShot(250, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-1-open.png")));
            sp.grabIris().save(QDir(dir).filePath(QStringLiteral("splash-4-iris.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT open+iris saved\n"); std::fflush(stderr);
        });
        QTimer::singleShot(1600, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-2-hold.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT hold saved\n"); std::fflush(stderr);
        });
        QTimer::singleShot(2450, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-3-slit.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT slit saved\n"); std::fflush(stderr);
            QApplication::quit();
        });
        sp.play();
        return app.exec();
    }

    if (animOk) {
        // 系统「减弱动效」：静态收尾 splash，客户端立即全尺寸显示（无展开动画）。
        if (Lumen::systemReducedMotion()) {
            SplashWindow splash(fullRect);
            QObject::connect(&splash, &SplashWindow::finished, [&]() {
                splash.close();
                win.setGeometry(fullRect);
                win.show();
            });
            splash.playStatic();
            return app.exec();
        }
        // 开场：splash 播放 → 收缩成缝 → 客户端从缝中向上下展开
        SplashWindow splash(fullRect);
        QObject::connect(&splash, &SplashWindow::finished, [&]() {
            splash.close();
            // 先以「缝」形态出现，再向上下展开（避免 show() 全尺寸单帧闪现）
            const QRect slit(fullRect.x(), fullRect.center().y() - 1,
                             fullRect.width(), 2);
            win.setGeometry(slit);
            win.show();
            auto* a = new QPropertyAnimation(&win, "geometry");
            a->setDuration(Lumen::Motion::kExpandMs);
            a->setStartValue(slit);
            a->setEndValue(fullRect);
            a->setEasingCurve(QEasingCurve::OutCubic);
            a->start(QAbstractAnimation::DeleteWhenStopped);
        });
        splash.play();
        return app.exec();
    }

    // 非动画（冒烟/截图）：直接显示并保留原有门控
    win.setGeometry(fullRect);
    win.show();

    // P15 沉浸式灯箱探针：LA_LIGHTBOX_SHOT=<dir> 时打开首张照片的灯箱 → grab 存 PNG 后退出。
    if (qEnvironmentVariableIsSet("LA_LIGHTBOX_SHOT")) {
        const QString dir = qEnvironmentVariable("LA_LIGHTBOX_SHOT");
        QDir().mkpath(dir);
        const auto& phs = lib.photos();
        if (!phs.isEmpty()) {
            win.openLightbox(phs.first().path);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            const QString path = QDir(dir).filePath(QStringLiteral("lightbox.png"));
            win.grab().save(path);
            std::fprintf(stderr, "LA_LIGHTBOX_SHOT %s\n", qPrintable(path));
            std::fflush(stderr);
        }
        return 0;
    }

    // A 现状墨韵·抛光 像素自测：LA_MAP_A_QA=<png> 时离屏渲染地图世界层并统计翡翠像素。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_A_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_A_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootA(p);
        std::fprintf(stderr, "LA_MAP_A_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 下钻区/省视图像素自测：LA_MAP_REGION_QA=<png>[,<regionId>] 时强制 enterRegion 后
    // 离屏渲染该大区省视图。默认 regionId=china（验证 DataV 34 省落地）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_REGION_QA")) {
        const QString v = qEnvironmentVariable("LA_MAP_REGION_QA");
        QString p = v, id = QStringLiteral("china");
        const int idx = v.indexOf(QLatin1Char(','));
        if (idx >= 0) { p = v.left(idx); id = v.mid(idx + 1); }
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootRegion(p, id);
        std::fprintf(stderr, "LA_MAP_REGION_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 点击链路自测：LA_MAP_CLICK_QA=1 时合成鼠标事件，实测世界锚点/省面两级命中。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_CLICK_QA")) {
        if (MapView* mv = win.mapView()) mv->qaClickProbe();
        std::fprintf(stderr, "LA_MAP_CLICK_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 爆炸弹层像素自测：LA_MAP_BURST_QA=<png> 时进省视图 + 合成缩略图 →
    // 离屏渲染终态并分色统计（连线/卡片/冲击波落地必须实测）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_BURST_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_BURST_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootBurst(p);
        std::fprintf(stderr, "LA_MAP_BURST_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 帧耗时自测：LA_MAP_PERF=1 时逐层级渲染 20 帧打印平均毫秒/帧。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_PERF")) {
        if (MapView* mv = win.mapView()) mv->qaPerf();
        std::fprintf(stderr, "LA_MAP_PERF_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 国家视图像素自测：LA_MAP_COUNTRY_QA=<png>[,<国家名>] 时 enterCountry →
    // 离屏渲染国界轮廓 + 石板色统计（默认 India，bbox 中心必在国内）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_QA")) {
        const QString v = qEnvironmentVariable("LA_MAP_COUNTRY_QA");
        QString p = v, nm = QStringLiteral("India");
        const int idx = v.indexOf(QLatin1Char(','));
        if (idx >= 0) { p = v.left(idx); nm = v.mid(idx + 1); }
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootCountry(p, nm);
        std::fprintf(stderr, "LA_MAP_COUNTRY_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // P23 E 杂志拼贴 像素/几何自测：LA_GALLERY_E_QA=<png> 时合成 12 张照片驱动 EditorialView，
    // 验证 feature 首图占 2×2 + 卡片真实绘制，并抓整页 PNG（颜色校验铁律：必须实测不许猜）。
    if (!qEnvironmentVariableIsEmpty("LA_GALLERY_E_QA")) {
        const QString p = qEnvironmentVariable("LA_GALLERY_E_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        QVector<Photo> synth;
        for (int i = 0; i < 12; ++i) {
            Photo ph;
            ph.path  = QStringLiteral("synthetic/%1.jpg").arg(i);
            ph.title = QStringLiteral("作品 %1").arg(i + 1);
            ph.date  = QStringLiteral("2024-05-%1").arg((i % 9) + 1, 2, 10, QChar('0'));
            ph.orient = (i % 2) ? QStringLiteral("portrait") : QStringLiteral("landscape");
            ph.rating = (i % 4 == 0) ? 4 : (i % 3 == 0 ? 3 : 0);
            ph.year  = 2024;
            synth.append(ph);
        }
        GalleryPage page(&synth, QDir::temp().filePath(QStringLiteral("la_e_qa_thumbs")), nullptr);
        page.resize(1200, 820);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        page.qaShootE(p);
        std::fprintf(stderr, "LA_GALLERY_E_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // P27 图库铺满验证：LA_GALLERY_FILL_QA=<png> 时取真库首张存在的照片，
    // 直接用 Lumen::paintGalleryCard 渲染 feature 大图 + single 小图两种卡片，
    // 验证真实照片铺满卡片（无黑边 / 无 letterbox）——绕过异步 ThumbCache 直接测绘制逻辑
    //（颜色校验铁律：必须实测不许猜）。
    if (!qEnvironmentVariableIsEmpty("LA_GALLERY_FILL_QA")) {
        const QString p = qEnvironmentVariable("LA_GALLERY_FILL_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        // 取真库首张磁盘上存在的照片（绕过异步 ThumbCache，直接验证绘制逻辑）
        QString photoPath;
        for (const Photo& ph : lib.photos()) {
            if (QFile::exists(ph.path)) { photoPath = ph.path; break; }
        }
        QPixmap pm;
        if (!photoPath.isEmpty()) pm.load(photoPath);
        const bool loaded = !pm.isNull();
        auto renderCard = [&](int W, int H, bool feature) -> QImage {
            QImage img(W, H, QImage::Format_RGB32);
            img.fill(QColor(0x13, 0x1a, 0x15));
            QPainter painter(&img);
            Lumen::paintGalleryCard(&painter, QRectF(0, 0, W, H),
                QStringLiteral("测试作品标题"),
                QStringLiteral("2024-05-01 · 风光 · 云南"),
                feature ? QStringLiteral("landscape") : QStringLiteral("portrait"),
                false, 4, pm, feature, false, false);
            painter.end();
            return img;
        };
        const QImage feat = renderCard(400, 400, true);
        const QImage sing = renderCard(240, 240, false);
        const QString dir = QFileInfo(p).absolutePath();
        feat.save(p);   // 主交付：feature 大图铺满样张
        sing.save(QDir(dir).filePath(QStringLiteral("gallery_fill_single.png")));
        auto measure = [](const QImage& img, long& cardBg, long& photo, long& litext) {
            const int W = img.width(), H = img.height();
            cardBg = photo = litext = 0;
            for (int y = 0; y < H; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < W; ++x) {
                    const QRgb v = row[x];
                    const int r = qRed(v), g = qGreen(v), b = qBlue(v);
                    if (qAbs(r - 19) < 12 && qAbs(g - 26) < 12 && qAbs(b - 21) < 12) ++cardBg;
                    else if (r > 200 && g > 200 && b > 190) ++litext;
                    else ++photo;
                }
            }
        };
        long fcb, fph, flt, scb, sph, slt;
        measure(feat, fcb, fph, flt);
        measure(sing, scb, sph, slt);
        const long fAll = feat.width() * feat.height();
        const long sAll = sing.width() * sing.height();
        const double fCbFrac = double(fcb) / fAll, sCbFrac = double(scb) / sAll;
        const double fPhFrac = double(fph) / fAll, sPhFrac = double(sph) / sAll;
        // 关键断言：卡片底（黑边 / letterbox）占比极小 → 真实照片铺满、无黑边。
        // （photo 占比阈值不可靠：亮调照片天空/高光会被误计入 litext，故仅以 cardBg 占比判定）
        const bool ok = loaded && fCbFrac < 0.02 && sCbFrac < 0.02;
        std::fprintf(stderr,
            "LA_GALLERY_FILL_QA loaded=%d photo=%s featCbFrac=%.4f singCbFrac=%.4f "
            "featPhFrac=%.4f singPhFrac=%.4f featLitext=%ld ok=%d\n",
            int(loaded), qPrintable(photoPath), fCbFrac, sCbFrac, fPhFrac, sPhFrac, flt, int(ok));
        std::fflush(stderr);
        return ok ? 0 : 5;
    }

    // P30-C / P29-H 诊断：离屏渲染日志页/首页到 PNG（QWidget::render 直绘 QImage，
    // 避开 shootPages 的 grab 挂起族）。仅供目视诊断，不进正式回归门。
    if (!qEnvironmentVariableIsEmpty("LA_LOG_SHOT")) {
        const QString p = qEnvironmentVariable("LA_LOG_SHOT");
        QDir().mkpath(QFileInfo(p).absolutePath());
        LogPage page(&lib.photos(), nullptr);
        page.resize(1440, 940);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        QImage img(page.size(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        page.render(&painter);
        painter.end();
        img.save(p);
        std::fprintf(stderr, "LA_LOG_SHOT_DONE %s\n", qPrintable(p));
        std::fflush(stderr);
        return 0;
    }
    if (!qEnvironmentVariableIsEmpty("LA_HOME_SHOT")) {
        const QString p = qEnvironmentVariable("LA_HOME_SHOT");
        QDir().mkpath(QFileInfo(p).absolutePath());
        win.setGeometry(80, 80, 1440, 940);
        win.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 600);
        QImage img(win.size(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        win.render(&painter);
        painter.end();
        img.save(p);
        std::fprintf(stderr, "LA_HOME_SHOT_DONE %s\n", qPrintable(p));
        std::fflush(stderr);
        return 0;
    }

    // P26 修图页 A 经典工作室 像素/几何自测：LA_RETOUCH_A_QA=<png> 时合成样张驱动独立 RetouchPage，
    // 验证 6 模式轨 + 12 滑杆 + 右栈 6 页 + 金像素，并抓整页 PNG。
    if (!qEnvironmentVariableIsEmpty("LA_RETOUCH_A_QA")) {
        const QString p = qEnvironmentVariable("LA_RETOUCH_A_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        QVector<Photo> synth;
        for (int i = 0; i < 6; ++i) {
            Photo ph;
            ph.path  = QStringLiteral("synthetic/%1.jpg").arg(i);
            ph.title = QStringLiteral("样张 %1").arg(i + 1);
            synth.append(ph);
        }
        RetouchPage page(&synth, QDir::temp().filePath(QStringLiteral("la_a_qa_thumbs")), nullptr);
        page.resize(1240, 820);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        page.qaShootA(p);
        std::fprintf(stderr, "LA_RETOUCH_A_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // P11 UI 截图探针：LA_UI_SHOT=<dir> 时逐页 widget.grab() 存 PNG（offscreen 可用，
    // 用于颜色/乱码真实验证——颜色校验铁律：必须实测不许猜）。
    if (qEnvironmentVariableIsSet("LA_UI_SHOT")) {
        if (qEnvironmentVariableIsSet("LA_UI_SHOT_SOLID"))
            win.forceTopSolid(true); // P19：顶栏实底态截图（强制态，抗 onNav 重算）
        return win.shootPages(qEnvironmentVariable("LA_UI_SHOT"));
    }

    // 冒烟探针门控：载入真库 + 页面栈就位后自动退出（offscreen/CI 可用）。
    // 选片自测（LA_CULL_SMOKE）/ 修图自测（LA_RETOUCH_SMOKE）
    // 由各自页面自行驱动退出，这里不抢跑。
    if (qEnvironmentVariableIsSet("LA_SMOKE") && qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        // P29-H：首页（零 chrome 纯沉浸）应隐藏窗口控制簇——同步打印，避开 600ms 自退定时器可能的挂起
        const bool winCtlHiddenOnHome = !win.isWinCtlVisible();
        std::fprintf(stderr, "LA_SMOKE_WINCTL home=%d\n", int(winCtlHiddenOnHome));
        std::fflush(stderr);
        QTimer::singleShot(600, &app, [&lib, &win]{
            const Kpi& k = lib.kpi();
            std::fprintf(stderr, "LA_P1_SMOKE_OK photos=%d years=%d provs=%d span=%d-%d\n",
                         k.total, k.yearCount, k.provCount, k.minYear, k.maxYear);
            std::fflush(stderr);
            QApplication::quit();
        });
    }
    // P25 对决 QA 由 CullPage 内 QTimer::singleShot(600) 在事件循环内完成并 quit
    return app.exec();
}

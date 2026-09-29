// LightAlbum Native — P1 入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 冒烟：LA_SMOKE=1 + QT_QPA_PLATFORM=offscreen → 载入真库后自动退出并打 LA_P1_SMOKE_OK
#include <QApplication>
#include <QScreen>
#include <QTimer>
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
#include "core/Scanner.h"
#include "ui/MainWindow.h"
#include "ui/DarkPalette.h"
#include "ui/Theme.h"
#include "ui/Splash.h"
#include "core/MotionPref.h"
#include "core/Vault.h"
#include <QPropertyAnimation>

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
    struct P { double lat, lon; const char* r; const char* pv; const char* ct; };
    static const P pts[] = {
        {64.15, -21.94,  "iceland", "Capital Region",   "雷克雅未克"},
        {39.90, 116.41,  "china",   "Beijing Municipality", "北京"},
        {23.13, 113.26,  "china",   "Guangzhou Province", "广州"},
        {38.91, 121.61,  "china",   "Liaoning Province", "大连"},   // 沿海缝隙 → 城市兜底
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
        ;

    QRect avail = QApplication::primaryScreen()->availableGeometry();
    QRect fullRect((avail.width() - 1440) / 2, (avail.height() - 940) / 2, 1440, 940);

    // P13 开场动画探针：LA_SPLASH_SHOT=<dir> 时抓 3 帧（完整/收缩中/成缝）存 PNG 后退出。
    if (qEnvironmentVariableIsSet("LA_SPLASH_SHOT")) {
        const QString dir = qEnvironmentVariable("LA_SPLASH_SHOT");
        QDir().mkpath(dir);
        SplashWindow sp(fullRect);
        QTimer::singleShot(900, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-1-full.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT full saved\n"); std::fflush(stderr);
        });
        QTimer::singleShot(1850, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-2-contract.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT contract saved\n"); std::fflush(stderr);
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

    // P11 UI 截图探针：LA_UI_SHOT=<dir> 时逐页 widget.grab() 存 PNG（offscreen 可用，
    // 用于颜色/乱码真实验证——颜色校验铁律：必须实测不许猜）。
    if (qEnvironmentVariableIsSet("LA_UI_SHOT"))
        return win.shootPages(qEnvironmentVariable("LA_UI_SHOT"));

    // 冒烟探针门控：载入真库 + 页面栈就位后自动退出（offscreen/CI 可用）。
    // 选片自测（LA_CULL_SMOKE）/ 修图自测（LA_RETOUCH_SMOKE）
    // 由各自页面自行驱动退出，这里不抢跑。
    if (qEnvironmentVariableIsSet("LA_SMOKE") && qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        QTimer::singleShot(600, &app, [&lib]{
            const Kpi& k = lib.kpi();
            std::fprintf(stderr, "LA_P1_SMOKE_OK photos=%d years=%d provs=%d span=%d-%d\n",
                         k.total, k.yearCount, k.provCount, k.minYear, k.maxYear);
            std::fflush(stderr);
            QApplication::quit();
        });
    }
    return app.exec();
}

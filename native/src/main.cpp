// LightAlbum Native — P1 入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 冒烟：LA_SMOKE=1 + QT_QPA_PLATFORM=offscreen → 载入真库后自动退出并打 LA_P1_SMOKE_OK
#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <cstdio>
#include <cmath>
#include "core/Library.h"
#include "core/LumenEngine.h"
#include "ui/MainWindow.h"
#include "ui/DarkPalette.h"

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

    // P5 金样对拍冒烟（无需库文件，最先跑）
    if (qEnvironmentVariableIsSet("LA_LUMEN_SMOKE"))
        return runLumenSmoke();

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

    MainWindow win(&lib, thumbsDir);
    win.setWindowTitle(QStringLiteral("光影集 LightAlbum " LA_VERSION));
    win.resize(1440, 940);
    win.show();

    // 冒烟探针门控：载入真库 + 页面栈就位后自动退出（offscreen/CI 可用）。
    // 选片自测（LA_CULL_SMOKE）/ 修图自测（LA_RETOUCH_SMOKE）/ 美图自测（LA_BEAUTY_SMOKE）
    // 由各自页面自行驱动退出，这里不抢跑。
    if (qEnvironmentVariableIsSet("LA_SMOKE") && qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_BEAUTY_SMOKE")) {
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

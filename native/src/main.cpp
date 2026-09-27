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
    const bool pass = n > 0 && stepsOk == n && madMax <= kMadLimit;
    std::fprintf(stderr, "LA_LUMEN_SMOKE n=%d madAvg=%.3f madMax=%.3f stepsOk=%d/%d %s\n",
                 n, avg, madMax, stepsOk, n, pass ? "PASS" : "FAIL");
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
    win.setWindowTitle(QStringLiteral("光影集 LightAlbum (native)"));
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

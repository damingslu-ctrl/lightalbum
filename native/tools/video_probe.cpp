#include <QGuiApplication>
#include <QImageReader>
#include <QImage>
#include <QFileInfo>
#include <cstdio>
#include "core/VideoDecode.h"

static int fails = 0;
static void ck(bool c, const char* w) {
    printf("%-46s %s\n", w, c ? "PASS" : "FAIL");
    if (!c) ++fails;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QString mp4 = QString::fromLocal8Bit(argv[1]);
    const QString jpg = QString::fromLocal8Bit(argv[2]);

    ck(VideoDecode::isVideoExt(QStringLiteral("a.mp4")), "isVideoExt .mp4");
    ck(VideoDecode::isVideoExt(QStringLiteral("A.MOV")), "isVideoExt upper .MOV");
    ck(VideoDecode::isVideoExt(QStringLiteral("x.mkv")), "isVideoExt .mkv");
    ck(!VideoDecode::isVideoExt(QStringLiteral("a.dng")), "isVideoExt rejects .dng");
    ck(!VideoDecode::isVideoExt(QStringLiteral("a.jpg")), "isVideoExt rejects .jpg");

    ck(VideoDecode::needsExternalDecode(QStringLiteral("a.mp4"), true), "needs mp4 true");
    ck(!VideoDecode::needsExternalDecode(QStringLiteral("a.mp4"), false), "needs mp4 no-decoder false");
    ck(!VideoDecode::needsExternalDecode(QStringLiteral("a.json"), true), "needs json false");
    ck(!VideoDecode::needsExternalDecode(QStringLiteral("a.ini"), true), "needs ini false");

    const bool avail = VideoDecode::available();
    printf("toolDir = %s\n", qPrintable(VideoDecode::toolDir()));
    ck(avail, "available()");
    if (!avail) { printf("SKIP\n"); return fails ? 1 : 0; }

    const VideoDecode::VideoProbe p = VideoDecode::probe(mp4);
    printf("probe: dur=%.3f w=%d h=%d codec=%s valid=%d\n",
           p.durationSec, p.width, p.height, qPrintable(p.codec), p.valid() ? 1 : 0);
    ck(p.durationSec > 9.5 && p.durationSec < 10.5, "probe duration ~10s");
    ck(p.width == 640 && p.height == 480, "probe dimensions 640x480");
    ck(p.codec == QLatin1String("h264"), "probe codec h264");

    const QString poster = VideoDecode::extractPoster(mp4);
    printf("extractPoster -> %s\n", qPrintable(QFileInfo(poster).fileName()));
    ck(!poster.isEmpty(), "poster produced");
    ck(!poster.isEmpty() && QFileInfo::exists(poster), "poster exists");
    if (!poster.isEmpty()) {
        QImageReader r(poster); QImage im = r.read();
        printf("poster read %dx%d null=%d\n", im.width(), im.height(), im.isNull()?1:0);
        ck(!im.isNull(), "Qt reads poster");
        QFile::remove(poster);
    }

    ck(VideoDecode::extractPoster(jpg).isEmpty(), "extractPoster on non-video fails");

    printf("=== %s ===\n", fails ? "FAIL" : "ALL PASS");
    return fails ? 1 : 0;
}

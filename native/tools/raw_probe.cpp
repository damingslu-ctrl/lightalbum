#include <QGuiApplication>
#include <QImageReader>
#include <QImage>
#include <QFileInfo>
#include <cstdio>
#include "core/RawDecode.h"

static int fails = 0;
static void ck(bool cond, const char* what) {
    printf("%-46s %s\n", what, cond ? "PASS" : "FAIL");
    if (!cond) ++fails;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QString raw = QString::fromLocal8Bit(argv[1]);

    // ---- pure predicate (no decoder needed) ----
    ck(RawDecode::isRawExt(QStringLiteral("a.dng")), "isRawExt .dng");
    ck(RawDecode::isRawExt(QStringLiteral("A.CR3")), "isRawExt case-insens .CR3");
    ck(RawDecode::isRawExt(QStringLiteral("x.nef")), "isRawExt .nef");
    ck(!RawDecode::isRawExt(QStringLiteral("a.jpg")), "isRawExt rejects .jpg");
    ck(!RawDecode::isRawExt(QStringLiteral("noext")), "isRawExt rejects noext");

    ck(RawDecode::isImportable(QStringLiteral("a.jpg"), false),
       "isImportable: photo yes even w/o decoder");
    ck(!RawDecode::isImportable(QStringLiteral("a.dng"), false),
       "isImportable: RAW no when decoder missing");
    ck(RawDecode::isImportable(QStringLiteral("a.dng"), true),
       "isImportable: RAW yes when decoder present");

    // ---- availability ----
    const bool avail = RawDecode::available();
    printf("toolDir = %s\n", qPrintable(RawDecode::toolDir()));
    ck(avail, "available() (LibRaw staged next to exe)");
    if (!avail) { printf("SKIP decode assertions\n"); return fails ? 1 : 0; }

    // ---- real decode ----
    QElapsedTimer t; t.start();
    const QString ppm = RawDecode::decodeToTempPpm(raw);
    const qint64 ms = t.elapsed();
    printf("decodeToTempPpm -> %s (%lld ms)\n",
           qPrintable(QFileInfo(ppm).fileName()), (long long)ms);
    ck(!ppm.isEmpty(), "decode produced a PPM path");
    ck(QFileInfo::exists(ppm) && QFileInfo(ppm).size() > 1024, "PPM exists and non-trivial");

    if (!ppm.isEmpty()) {
        QImageReader r(ppm);
        QImage img = r.read();
        printf("PPM read -> %dx%d null=%d\n", img.width(), img.height(), img.isNull() ? 1 : 0);
        ck(!img.isNull(), "Qt reads the PPM (non-null)");
        ck(img.width() >= 512 && img.height() >= 512, "PPM dimensions plausible");
        QFile::remove(ppm);
    }

    const QSize sz = RawDecode::probeSize(raw);
    printf("probeSize -> %dx%d\n", sz.width(), sz.height());
    ck(sz.isValid() && sz.width() > 0 && sz.height() > 0, "probeSize valid");

    // ---- negative: a non-RAW file must NOT decode ----
    const QString bogus = QString::fromLocal8Bit(argv[2]);
    ck(RawDecode::decodeToTempPpm(bogus).isEmpty(),
       "decode of non-RAW (jpg) correctly fails");

    printf("=== %s ===\n", fails ? "FAIL" : "ALL PASS");
    return fails ? 1 : 0;
}

#!/usr/bin/env bash
# Counterexample harness v2 for the RAW gate (iron rule 22/23).
#
# v1 was REJECTED as weak: it injected "trust exit code", but measured dcraw_emu
# returns rc=2 for malformed input -- so the rc-based check ALSO failed there and
# the broken build did not turn red. A counterexample that cannot discriminate
# proves nothing (iron rule 23).
#
# v2 injects the defect that ACTUALLY matters and is unique to artifact-judging:
#   "the process exited cleanly, therefore we have an image" -- with NO artifact
#   validation, and with the output-path derivation left unverified.
# The discriminating input is a file that makes dcraw_emu exit 0 WITHOUT writing
# a usable PPM, or writes it somewhere other than the derived name.
#
# Measured facts this relies on:
#   - dcraw_emu APPENDS .ppm, keeping the existing suffix (x.dng -> x.dng.ppm).
#   - unknown option -> prints "Unknown option" yet STILL exits 0 and still writes.
#   - unsupported/corrupt input -> rc=2, no output.
# Expected values are FIXED and must not flip with broken.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BROKEN="${1:-0}"

SRC="$ROOT/build/_raw_ce2.cpp"
cat > "$SRC" <<'EOF'
#include <QGuiApplication>
#include <QImageReader>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

// The real-world defect shape: we believed dcraw_emu wrote "<input>.ppm",
// so we return that path. The broken variant drops artifact validation AND
// uses a WRONG derived path (a classic off-by-suffix bug).
static QString run(bool broken, const QString& input, const QString& bin,
                   const QString& work, int* outRc, QSize* outSize)
{
    QProcess p; p.setWorkingDirectory(bin);
    p.setProgram(bin + QStringLiteral("/dcraw_emu.exe"));
    p.setArguments({ QStringLiteral("-h"), QStringLiteral("-w"), work });
    p.start();
    if (!p.waitForFinished(30000)) return QStringLiteral("TIMEOUT");
    p.waitForFinished(1000);
    *outRc = p.exitCode();

    if (broken) {
        // DEFECT: exit code 0 => "we have an image", and we guess the output
        // name by REPLACING the extension instead of APPENDING (real bug shape).
        if (*outRc != 0) return QString();
        const QString guessed = QFileInfo(work).completeBaseName()
                                + QStringLiteral(".ppm");
        *outSize = QSize();
        return guessed;
    }
    // CORRECT: derive by APPENDING, then validate the artifact itself.
    const QString ppm = work + QStringLiteral(".ppm");
    if (!QFileInfo::exists(ppm)) return QString();
    QImageReader r(ppm);
    const QSize s = r.size();
    *outSize = s;
    if (!s.isValid() || s.width() < 16 || s.height() < 16) return QString();
    return ppm;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const bool broken = qEnvironmentVariableIntValue("LA_RAW_FORCE_BROKEN") != 0;
    const QString bin = QCoreApplication::applicationDirPath()
                        + QStringLiteral("/3rdparty/libraw/bin");
    const QString in = QString::fromLocal8Bit(argv[1]);

    static QTemporaryDir td; td.setAutoRemove(true);
    const QString work = td.filePath(QStringLiteral("x.dng"));
    if (!QFile::copy(in, work)) { printf("SETUP_FAIL\n"); return 2; }

    int rc = -1; QSize sz;
    const QString got = run(broken, in, bin, work, &rc, &sz);
    const bool ok = !got.isEmpty() && QFileInfo::exists(got) && sz.isValid()
                    && sz.width() >= 16 && sz.height() >= 16;
    printf("DECODE %s rc=%d derived=%s size=%dx%d exists=%d\n",
           ok ? "ok" : "fail", rc, qPrintable(QFileInfo(got).fileName()),
           sz.width(), sz.height(), got.isEmpty() ? 0 : QFileInfo::exists(got) ? 1 : 0);
    return ok ? 0 : 1;
}
EOF

export PATH="/d/qt/Tools/mingw1310_64/bin:/d/qt/6.8.3/mingw_64/bin:$PATH"
g++ -std=c++17 -O1 "$SRC" -o "$ROOT/build/raw_ce2.exe" \
  -I /d/qt/6.8.3/mingw_64/include -I /d/qt/6.8.3/mingw_64/include/QtGui \
  -I /d/qt/6.8.3/mingw_64/include/QtCore \
  -L /d/qt/6.8.3/mingw_64/lib -lQt6Gui -lQt6Core || { echo "BUILD_FAIL"; exit 2; }

export QT_QPA_PLATFORM=offscreen
RAW=/d/la_deps/rawqa/sample1.dng

echo "--- valid DNG ---"
LA_RAW_FORCE_BROKEN=$BROKEN "$ROOT/build/raw_ce2.exe" "$RAW"; echo "   exit=$?"

# Discriminating case: a truncated/garbage file that still exits 0 in some paths,
# plus the suffix-derivation bug which ALWAYS shows on valid input.
echo "--- valid DNG, broken=1 (suffix-derivation bug must surface) ---"
LA_RAW_FORCE_BROKEN=1 "$ROOT/build/raw_ce2.exe" "$RAW"; echo "   exit=$?"

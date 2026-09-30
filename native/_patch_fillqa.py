#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CRLF-safe patch: add LA_GALLERY_FILL_QA gate to main.cpp (P27 cover-fill proof)."""
import io, sys

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\native\src\main.cpp"

def to_lf(b): return b.replace(b"\r\n", b"\n")
def to_crlf(s): return s.replace("\n", "\r\n")

with io.open(SRC, "rb") as f:
    raw = f.read()
text = to_lf(raw).decode("utf-8")

# --- 1) include GalleryCardPainter.h right after GalleryPage.h ---
anchor_inc = (
    '#include "ui/GalleryPage.h"\n'
    '#include "ui/RetouchPage.h"\n'
)
repl_inc = (
    '#include "ui/GalleryPage.h"\n'
    '#include "ui/GalleryCardPainter.h"\n'
    '#include "ui/RetouchPage.h"\n'
)
assert text.count(anchor_inc) == 1, "include anchor count=%d" % text.count(anchor_inc)
text = text.replace(anchor_inc, repl_inc)

# --- 2) add to animOk exclusion list ---
anchor_anim = (
    '        && !qEnvironmentVariableIsSet("LA_GALLERY_E_QA")\n'
    '        && !qEnvironmentVariableIsSet("LA_CULL_DUEL_QA")\n'
)
repl_anim = (
    '        && !qEnvironmentVariableIsSet("LA_GALLERY_E_QA")\n'
    '        && !qEnvironmentVariableIsSet("LA_CULL_DUEL_QA")\n'
    '        && !qEnvironmentVariableIsSet("LA_GALLERY_FILL_QA")\n'
)
assert text.count(anchor_anim) == 1, "anim anchor count=%d" % text.count(anchor_anim)
text = text.replace(anchor_anim, repl_anim)

# --- 3) insert the gate block right after LA_GALLERY_E_QA gate ends ---
anchor_gate = (
    '        std::fprintf(stderr, "LA_GALLERY_E_QA_DONE\\n");\n'
    '        std::fflush(stderr);\n'
    '        return 0;\n'
    '    }\n'
    '\n'
    '    // P26 修图页 A 经典工作室 像素/几何自测：LA_RETOUCH_A_QA=<png> 时合成样张驱动独立 RetouchPage，\n'
)
gate_block = (
    '        std::fprintf(stderr, "LA_GALLERY_E_QA_DONE\\n");\n'
    '        std::fflush(stderr);\n'
    '        return 0;\n'
    '    }\n'
    '\n'
    '    // P27 图库铺满验证：LA_GALLERY_FILL_QA=<png> 时取真库首张存在的照片，\n'
    '    // 直接用 Lumen::paintGalleryCard 渲染 feature 大图 + single 小图两种卡片，\n'
    '    // 验证真实照片铺满卡片（无黑边 / 无 letterbox）——绕过异步 ThumbCache 直接测绘制逻辑\n'
    '    //（颜色校验铁律：必须实测不许猜）。\n'
    '    if (!qEnvironmentVariableIsEmpty("LA_GALLERY_FILL_QA")) {\n'
    '        const QString p = qEnvironmentVariable("LA_GALLERY_FILL_QA");\n'
    '        QDir().mkpath(QFileInfo(p).absolutePath());\n'
    '        // 取真库首张磁盘上存在的照片（绕过异步 ThumbCache，直接验证绘制逻辑）\n'
    '        QString photoPath;\n'
    '        for (const Photo& ph : lib.photos()) {\n'
    '            if (QFile::exists(ph.path)) { photoPath = ph.path; break; }\n'
    '        }\n'
    '        QPixmap pm;\n'
    '        if (!photoPath.isEmpty()) pm.load(photoPath);\n'
    '        const bool loaded = !pm.isNull();\n'
    '        auto renderCard = [&](int W, int H, bool feature) -> QImage {\n'
    '            QImage img(W, H, QImage::Format_RGB32);\n'
    '            img.fill(QColor(0x13, 0x1a, 0x15));\n'
    '            QPainter painter(&img);\n'
    '            Lumen::paintGalleryCard(&painter, QRectF(0, 0, W, H),\n'
    '                QStringLiteral("测试作品标题"),\n'
    '                QStringLiteral("2024-05-01 · 风光 · 云南"),\n'
    '                feature ? QStringLiteral("landscape") : QStringLiteral("portrait"),\n'
    '                false, 4, pm, feature, false, false);\n'
    '            painter.end();\n'
    '            return img;\n'
    '        };\n'
    '        const QImage feat = renderCard(400, 400, true);\n'
    '        const QImage sing = renderCard(240, 240, false);\n'
    '        const QString dir = QFileInfo(p).absolutePath();\n'
    '        feat.save(p);   // 主交付：feature 大图铺满样张\n'
    '        sing.save(QDir(dir).filePath(QStringLiteral("gallery_fill_single.png")));\n'
    '        auto measure = [](const QImage& img, long& cardBg, long& photo, long& litext) {\n'
    '            const int W = img.width(), H = img.height();\n'
    '            cardBg = photo = litext = 0;\n'
    '            for (int y = 0; y < H; ++y) {\n'
    '                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));\n'
    '                for (int x = 0; x < W; ++x) {\n'
    '                    const QRgb v = row[x];\n'
    '                    const int r = qRed(v), g = qGreen(v), b = qBlue(v);\n'
    '                    if (qAbs(r - 19) < 12 && qAbs(g - 26) < 12 && qAbs(b - 21) < 12) ++cardBg;\n'
    '                    else if (r > 200 && g > 200 && b > 190) ++litext;\n'
    '                    else ++photo;\n'
    '                }\n'
    '            }\n'
    '        };\n'
    '        long fcb, fph, flt, scb, sph, slt;\n'
    '        measure(feat, fcb, fph, flt);\n'
    '        measure(sing, scb, sph, slt);\n'
    '        const long fAll = feat.width() * feat.height();\n'
    '        const long sAll = sing.width() * sing.height();\n'
    '        const double fCbFrac = double(fcb) / fAll, sCbFrac = double(scb) / sAll;\n'
    '        const double fPhFrac = double(fph) / fAll, sPhFrac = double(sph) / sAll;\n'
    '        // cover-fill 断言：真实照片铺满 → 卡片底（黑边）占比极小、照片占比极大\n'
    '        const bool ok = loaded && fCbFrac < 0.02 && sCbFrac < 0.02\n'
    '                              && fPhFrac > 0.90 && sPhFrac > 0.90;\n'
    '        std::fprintf(stderr,\n'
    '            "LA_GALLERY_FILL_QA loaded=%d photo=%s featCbFrac=%.4f singCbFrac=%.4f "\n'
    '            "featPhFrac=%.4f singPhFrac=%.4f featLitext=%ld ok=%d\\n",\n'
    '            int(loaded), qPrintable(photoPath), fCbFrac, sCbFrac, fPhFrac, sPhFrac, flt, int(ok));\n'
    '        std::fflush(stderr);\n'
    '        return ok ? 0 : 5;\n'
    '    }\n'
    '\n'
    '    // P26 修图页 A 经典工作室 像素/几何自测：LA_RETOUCH_A_QA=<png> 时合成样张驱动独立 RetouchPage，\n'
)
assert text.count(anchor_gate) == 1, "gate anchor count=%d" % text.count(anchor_gate)
text = text.replace(anchor_gate, gate_block)

with io.open(SRC, "wb") as f:
    f.write(to_crlf(text).encode("utf-8"))
print("OK main.cpp patched: include + animOk + LA_GALLERY_FILL_QA gate")

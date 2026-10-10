// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// EraseBridge <-> inpaint.exe / inpaint_stub.exe 帧协议（唯一真源，两端共用）。
// 纯 ASCII 行 + 二进制体；无随机、宿主无关。详见 spec §2.2。
#include <QIODevice>
#include <QImage>
#include <QByteArray>
#include <QString>
#include <cstring>

namespace LaEraseProto {

// base 归一到 RGB888 后逐像素抽 R,G,B（跳 alpha，行无 padding）。
inline QByteArray packRgb(const QImage& img)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    QByteArray out;
    out.resize(rgb.width() * rgb.height() * 3);
    uchar* d = reinterpret_cast<uchar*>(out.data());
    for (int y = 0; y < rgb.height(); ++y) {
        const uchar* line = rgb.constScanLine(y);
        memcpy(d, line, size_t(rgb.width()) * 3);
        d += size_t(rgb.width()) * 3;
    }
    return out;
}

// mask 取 alpha 通道（ARGB32），255=消除区。
inline QByteArray packMaskAlpha(const QImage& maskIn)
{
    const QImage mask = maskIn.convertToFormat(QImage::Format_ARGB32);
    QByteArray out;
    out.resize(mask.width() * mask.height());
    uchar* d = reinterpret_cast<uchar*>(out.data());
    for (int y = 0; y < mask.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        for (int x = 0; x < mask.width(); ++x) *d++ = uchar(qAlpha(line[x]));
    }
    return out;
}

// 写请求到 io（子进程 stdin）。id 正整数。
inline bool writeRequest(QIODevice* io, quint32 id, const QImage& base, const QImage& mask, QString* err)
{
    const QImage rgbImg = base.convertToFormat(QImage::Format_RGB888);
    const QImage maskImg = mask.convertToFormat(QImage::Format_ARGB32);
    if (maskImg.width() != rgbImg.width() || maskImg.height() != rgbImg.height()) {
        if (err) *err = QStringLiteral("mask size != base");
        return false;
    }
    const QByteArray rgb = packRgb(rgbImg);
    const QByteArray m   = packMaskAlpha(maskImg);
    const int w = rgbImg.width(), h = rgbImg.height();
    QByteArray hdr = QStringLiteral("TASK %1 %2 %3 %4 %5\n")
        .arg(id).arg(w).arg(h).arg(rgb.size()).arg(m.size()).toUtf8();
    if (io->write(hdr) != hdr.size()) { if (err) *err = QStringLiteral("write hdr"); return false; }
    if (io->write(rgb) != rgb.size()) { if (err) *err = QStringLiteral("write rgb"); return false; }
    if (io->write(m)   != m.size())   { if (err) *err = QStringLiteral("write mask"); return false; }
    // Qt6：QIODevice 无 flush()；此处 io 为 QProcess（stdin 管道），
    // QProcess::write() 内部已 waitForBytesWritten()，无需也不能再 flush。
    return true;
}

// 读响应：成功填 outImage(QImage)。hdr/body 任何不一致 → false + err。
inline bool readResponse(QIODevice* io, quint32 expectId, QImage& outImage, QString* err)
{
    const QByteArray hdr = io->readLine();
    const QList<QByteArray> tk = hdr.trimmed().split(' ');
    if (tk.isEmpty()) { if (err) *err = QStringLiteral("empty resp"); return false; }
    if (tk[0] == "ERR") {
        if (err) *err = QStringLiteral("child ERR: ") + QString::fromUtf8(hdr.trimmed());
        return false;
    }
    // OK <id> <w> <h> <no>
    if (tk.size() != 5 || tk[0] != "OK") { if (err) *err = QStringLiteral("bad resp hdr"); return false; }
    const quint32 id = tk[1].toUInt();
    const int w = tk[2].toInt(), h = tk[3].toInt();
    const int no = tk[4].toInt();
    if (id != expectId) { if (err) *err = QStringLiteral("resp id mismatch"); return false; }
    if (no != w * h * 3) { if (err) *err = QStringLiteral("resp size mismatch"); return false; }
    QByteArray rgb = io->read(no);
    if (rgb.size() != no) { if (err) *err = QStringLiteral("short rgb"); return false; }
    QImage img(w, h, QImage::Format_RGB888);
    const uchar* s = reinterpret_cast<const uchar*>(rgb.constData());
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        memcpy(line, s + size_t(y) * w * 3, size_t(w) * 3);
    }
    outImage = img;
    return true;
}

} // namespace LaEraseProto

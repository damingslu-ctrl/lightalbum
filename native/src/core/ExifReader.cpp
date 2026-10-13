// P8-2 EXIF 解析实现 —— 移植自 client/lightalbum/core/exif.py
// 真实照片的 EXIF 很脏（"0/0"、空串、越界），所有有理数解析都做零值保护。
#include "ExifReader.h"
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStringList>
#include <cstring>
#include <cmath>

namespace {

// ---------------- TIFF 解析底座 ----------------
struct Tiff {
    const uchar* d = nullptr;
    qsizetype    n = 0;
    bool         le = true;   // little-endian（II）
    bool ok = false;
};

bool inRange(const Tiff& t, qsizetype off, qsizetype len)
{
    return t.ok && off >= 0 && len >= 0 && off + len <= t.n;
}

quint16 u16(const Tiff& t, qsizetype off)
{
    if (!inRange(t, off, 2)) return 0;
    return t.le ? quint16(quint16(t.d[off]) | (quint16(t.d[off + 1]) << 8))
                : quint16((quint16(t.d[off]) << 8) | quint16(t.d[off + 1]));
}

quint32 u32(const Tiff& t, qsizetype off)
{
    if (!inRange(t, off, 4)) return 0;
    if (t.le)
        return quint32(t.d[off]) | (quint32(t.d[off + 1]) << 8)
             | (quint32(t.d[off + 2]) << 16) | (quint32(t.d[off + 3]) << 24);
    return (quint32(t.d[off]) << 24) | (quint32(t.d[off + 1]) << 16)
         | (quint32(t.d[off + 2]) << 8) | quint32(t.d[off + 3]);
}

// type 尺寸表（BYTE,ASCII,SHORT,LONG,RATIONAL,SBYTE,UNDEF,SSHORT,SLONG,SRATIONAL,FLOAT,DOUBLE）
int typeSize(int ty)
{
    static const int k[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
    return (ty >= 1 && ty <= 12) ? k[ty] : 0;
}

// IFD 条目数据定位：<=4 字节内联（entry+8），否则 4 字节为偏移
qsizetype entryDataOff(const Tiff& t, qsizetype entry, int ty, quint32 count)
{
    const qsizetype total = qsizetype(typeSize(ty)) * qsizetype(count);
    if (total <= 4) return entry + 8;
    return qsizetype(u32(t, entry + 8));
}

// ASCII：剥 \x00 与首尾空白（Python _clean）
QString asciiAt(const Tiff& t, qsizetype off, qsizetype len)
{
    if (!inRange(t, off, len) || len <= 0) return QString();
    QByteArray raw(reinterpret_cast<const char*>(t.d + off), int(len));
    return QString::fromUtf8(raw.replace(char(0), "")).trimmed();
}

// 有理数 → double；den==0 视为无效（Python _rational_to_float 的零值保护）
// num 按 int32 解释以支持 SRATIONAL 负值。
double rationalVal(const Tiff& t, qsizetype off, bool* okOut = nullptr)
{
    if (!inRange(t, off, 8)) { if (okOut) *okOut = false; return 0.0; }
    const quint32 num = u32(t, off);
    const quint32 den = u32(t, off + 4);
    if (den == 0) { if (okOut) *okOut = false; return 0.0; }
    if (okOut) *okOut = true;
    return double(qint32(num)) / double(qint32(den));
}

// 在一个 IFD 里按 tag 取条目偏移（找不到返回 -1）
qsizetype findTag(const Tiff& t, qsizetype ifdOff, quint16 tag)
{
    if (!inRange(t, ifdOff, 2)) return -1;
    const quint16 cnt = u16(t, ifdOff);
    for (quint16 i = 0; i < cnt; ++i) {
        const qsizetype e = ifdOff + 2 + qsizetype(i) * 12;
        if (!inRange(t, e, 12)) return -1;
        if (u16(t, e) == tag) return e;
    }
    return -1;
}

// 条目的完整取值信息
struct Entry {
    qsizetype e = -1;      // 条目偏移
    int  ty = 0;
    quint32 count = 0;
    qsizetype off = -1;    // 数据偏移
};
Entry getEntry(const Tiff& t, qsizetype ifdOff, quint16 tag)
{
    Entry r;
    r.e = findTag(t, ifdOff, tag);
    if (r.e < 0) return r;
    r.ty = int(u16(t, r.e + 2));
    r.count = u32(t, r.e + 4);
    r.off = entryDataOff(t, r.e, r.ty, r.count);
    return r;
}

// GPS DMS（3 个 RATIONAL）+ N/S/E/W → 带符号十进制度（round 6）
bool dmsToDeg(const Tiff& t, qsizetype off, quint32 count, const QString& ref, double* out)
{
    if (count < 3) return false;
    bool ok1 = false, ok2 = false, ok3 = false;
    const double d = rationalVal(t, off, &ok1);
    const double m = rationalVal(t, off + 8, &ok2);
    const double s = rationalVal(t, off + 16, &ok3);
    if (!ok1) return false;
    double deg = d + (ok2 ? m : 0.0) / 60.0 + (ok3 ? s : 0.0) / 3600.0;
    if (ref == QLatin1String("S") || ref == QLatin1String("W")) deg = -deg;
    *out = std::round(deg * 1e6) / 1e6;
    return true;
}

// ---------------- JPEG APP1 / PNG eXIf 提取 ----------------
QByteArray extractJpegExif(const QByteArray& f)
{
    if (f.size() < 4 || uchar(f[0]) != 0xFF || uchar(f[1]) != 0xD8) return {};
    qsizetype off = 2;
    while (off + 4 <= f.size()) {
        if (uchar(f[off]) != 0xFF) return {};
        const uchar marker = uchar(f[off + 1]);
        if (marker == 0xFF) { ++off; continue; }        // 填充字节
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            off += 2;
            continue;
        }
        if (marker == 0xDA) break;                      // SOS：后面不会再有 APP1
        const int len = (uchar(f[off + 2]) << 8) | uchar(f[off + 3]);
        if (len < 2) return {};
        if (marker == 0xE1 && off + 4 + 6 <= f.size()
            && std::memcmp(f.constData() + off + 4, "Exif\0\0", 6) == 0)
            return f.mid(off + 10, len - 8);            // 去掉 "Exif\0\0"
        off += 2 + len;
    }
    return {};
}

QByteArray extractPngExif(const QByteArray& f)
{
    if (f.size() < 16) return {};
    static const uchar kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (std::memcmp(f.constData(), kSig, 8) != 0) return {};
    qsizetype off = 8;
    while (off + 12 <= f.size()) {
        const quint32 len = (quint32(uchar(f[off])) << 24) | (quint32(uchar(f[off + 1])) << 16)
                          | (quint32(uchar(f[off + 2])) << 8) | quint32(uchar(f[off + 3]));
        if (len > quint32(f.size())) return {};
        const char* ty = f.constData() + off + 4;
        if (std::memcmp(ty, "eXIf", 4) == 0 && len >= 8)
            return f.mid(off + 8, qsizetype(len));
        if (std::memcmp(ty, "IEND", 4) == 0) break;
        off += 12 + qsizetype(len);                     // len + type + data + crc
    }
    return {};
}

// ---------------- WebP（RIFF）EXIF chunk 提取（2.38.0 R2） ----------------
QByteArray extractWebpExif(const QByteArray& f)
{
    if (f.size() < 12 || std::memcmp(f.constData(), "RIFF", 4) != 0
        || std::memcmp(f.constData() + 8, "WEBP", 4) != 0) return {};
    qsizetype off = 12;
    while (off + 8 <= f.size()) {
        const quint32 len = quint32(uchar(f[off + 4])) | (quint32(uchar(f[off + 5])) << 8)
                          | (quint32(uchar(f[off + 6])) << 16) | (quint32(uchar(f[off + 7])) << 24);
        if (len > quint32(f.size())) return {};               // 防呆：尺寸越界即放弃
        if (std::memcmp(f.constData() + off, "EXIF", 4) == 0) {
            QByteArray payload = f.mid(off + 8, qsizetype(len));
            if (payload.size() >= 6 && std::memcmp(payload.constData(), "Exif\0\0", 6) == 0)
                payload.remove(0, 6);                          // 兼容带 "Exif\0\0" 前缀的写入器
            return payload;
        }
        off += 8 + qsizetype(len) + (len & 1u);                // chunk 2 字节对齐
    }
    return {};
}

// ---------------- HEIC（ISOBMFF）EXIF 提取：meta/iinf/iloc 最小 walker（2.38.0 R2） ----------------
// 防呆：盒尺寸越界 / 子盒超出父盒 / extent 越界 -> 返回空 -> 上层走 mtime 回退（诚实降级）。
quint64 exifBe(const uchar* p, int n)
{
    quint64 v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

QByteArray extractHeicExif(const QByteArray& f)
{
    const uchar* d = reinterpret_cast<const uchar*>(f.constData());
    const qint64 n = f.size();
    qint64 metaOff = -1, metaSize = 0, off = 0;
    while (off + 8 <= n) {                                     // 顶层盒扫描
        qint64 size = qint64(exifBe(d + off, 4));
        const char* ty = reinterpret_cast<const char*>(d + off + 4);
        qint64 hdr = 8;
        if (size == 1) {                                       // 64 位 largesize
            if (off + 16 > n) return {};
            size = qint64(exifBe(d + off + 8, 8));
            hdr = 16;
        } else if (size == 0) {
            size = n - off;                                    // 到文件尾
        }
        if (size < hdr || off + size > n) return {};           // 防呆：越界即放弃
        if (std::memcmp(ty, "meta", 4) == 0) { metaOff = off; metaSize = size; break; }
        off += size;
    }
    if (metaOff < 0) return {};
    qint64 p = metaOff + 8 + 4;                                // meta FullBox：version/flags 4 字节
    const qint64 metaEnd = metaOff + metaSize;
    qint64 exifItemId = -1, ilocOff = -1, ilocSize = 0;
    while (p + 8 <= metaEnd) {                                 // 子盒 walker（限于 meta 内）
        const qint64 size = qint64(exifBe(d + p, 4));
        const char* ty = reinterpret_cast<const char*>(d + p + 4);
        if (size < 8 || p + size > metaEnd) break;             // 防呆：越界/自指 -> 放弃
        if (std::memcmp(ty, "iinf", 4) == 0) {
            const qint64 iinfEnd = p + size;
            const int ver = d[p + 8];                          // iinf FullBox version
            qint64 q = p + 8 + 4;
            if (q > iinfEnd) return {};
            q += (ver == 0) ? 2 : 4;                           // entry_count（v0: u16 / v1+: u32）
            while (q + 8 <= iinfEnd) {                         // infe 条目扫描
                const qint64 isz = qint64(exifBe(d + q, 4));
                const char* ity = reinterpret_cast<const char*>(d + q + 4);
                if (isz < 8 || q + isz > iinfEnd) break;
                if (std::memcmp(ity, "infe", 4) == 0) {
                    const int iver = d[q + 8];                 // infe FullBox version
                    qint64 r = q + 8 + 4;
                    qint64 itemId = 0;
                    if (iver == 2 && r + 8 <= q + isz) { itemId = qint64(exifBe(d + r, 2)); r += 4; }
                    else if (iver >= 3 && r + 10 <= q + isz) { itemId = qint64(exifBe(d + r, 4)); r += 6; }
                    else { q += isz; continue; }
                    if (r + 4 <= q + isz
                        && std::memcmp(reinterpret_cast<const char*>(d + r), "Exif", 4) == 0)
                        exifItemId = itemId;
                }
                q += isz;
            }
        } else if (std::memcmp(ty, "iloc", 4) == 0) {
            ilocOff = p; ilocSize = size;
        }
        p += size;
    }
    if (exifItemId < 0 || ilocOff < 0) return {};
    const qint64 ilocEnd = ilocOff + ilocSize;
    // 前导体最少 18 字节（8 盒头 + 4 ver/flags + 2 size/period + ≤4 itemCount）；
    // 截断盒必须在此兜住，否则下面对 d[ilocOff+8]/d[q]/d[q+1] 的裸读可越过缓冲尾（不可信输入）。
    if (ilocOff + 18 > ilocEnd) return {};
    const int iver = d[ilocOff + 8];
    qint64 q = ilocOff + 8 + 4;
    const int b1 = d[q], b2 = d[q + 1]; q += 2;
    const int offSize = b1 >> 4, lenSize = b1 & 0xF, baseOffSize = b2 >> 4, indexSize = b2 & 0xF;
    if ((offSize != 4 && offSize != 8) || (lenSize != 4 && lenSize != 8)) return {};
    qint64 itemCount = qint64(exifBe(d + q, iver == 2 ? 4 : 2));
    q += (iver == 2) ? 4 : 2;
    for (qint64 i = 0; i < itemCount; ++i) {
        if (q + (iver < 2 ? 2 : 4) > ilocEnd) return {};
        const qint64 itemId = (iver < 2) ? qint64(exifBe(d + q, 2)) : qint64(exifBe(d + q, 4));
        q += (iver < 2) ? 2 : 4;
        int cm = 0;
        if (iver >= 1) { if (q + 2 > ilocEnd) return {}; cm = int(exifBe(d + q, 2)) & 0xF; q += 2; }
        q += 2;                                                // data_reference_index
        qint64 base = 0;
        if (baseOffSize > 0) { if (q + baseOffSize > ilocEnd) return {}; base = qint64(exifBe(d + q, baseOffSize)); }
        if (base < 0) return {};                               // 8 字节高位→负 qint64：显式钳位，不依赖 Qt mid 负位行为
        q += baseOffSize;
        if (q + 2 > ilocEnd) return {};
        const qint64 extCount = qint64(exifBe(d + q, 2)); q += 2;
        for (qint64 e = 0; e < extCount; ++e) {
            q += indexSize;                                    // extent_index（v1/2 且 index_size>0；v0 恒 0）
            if (q + offSize + lenSize > ilocEnd) return {};
            const qint64 extOff = qint64(exifBe(d + q, offSize)); q += offSize;
            const qint64 extLen = qint64(exifBe(d + q, lenSize)); q += lenSize;
            if (itemId == exifItemId && cm == 0 && e == 0) {   // construction_method 0 = 文件内偏移
                if (extOff <= 0 || extLen <= 0 || base + extOff + extLen > n) return {};
                const QByteArray payload = f.mid(qsizetype(base + extOff), qsizetype(extLen));
                if (payload.size() < 4) return {};
                const qint64 ti = qint64(exifBe(reinterpret_cast<const uchar*>(payload.constData()), 4));
                const qint64 start = 4 + ti;                   // payload 前 4 字节 = 到 TIFF 头偏移
                if (start < 0 || start > payload.size()) return {};
                return payload.mid(qsizetype(start));
            }
        }
    }
    return {};
}

// ---------------- 语义字段（exif.py 的 _fmt_* / _parse_dt / 映射表）----------------
QString wbLabel(int v)
{
    switch (v) {
    case 0: return QStringLiteral("自动");
    case 1: return QStringLiteral("手动");
    default: return QString();
    }
}

QString meterLabel(int v)
{
    switch (v) {
    case 0: return QStringLiteral("未知");
    case 1: return QStringLiteral("平均测光");
    case 2: return QStringLiteral("中央重点");
    case 3: return QStringLiteral("点测光");
    case 4: return QStringLiteral("多点");
    case 5: return QStringLiteral("评价测光");
    case 6: return QStringLiteral("局部");
    default: return QString();
    }
}

// '2023:02:11 20:14:03' / '2023-02-11T20:14:03' -> [date, time]
QStringList parseDt(const QString& raw)
{
    static const QRegularExpression re(
        QStringLiteral("^(\\d{4})[:\\-/](\\d{1,2})[:\\-/](\\d{1,2})[ T]?"
                       "(\\d{1,2})?:?(\\d{1,2})?:?(\\d{1,2})?"));
    const QRegularExpressionMatch m = re.match(raw.trimmed());
    if (!m.hasMatch()) return { QString(), QString() };
    const int y = m.captured(1).toInt();
    const int mo = m.captured(2).toInt();
    const int d = m.captured(3).toInt();
    const int hh = m.captured(4).isEmpty() ? 0 : m.captured(4).toInt();
    const int mm = m.captured(5).isEmpty() ? 0 : m.captured(5).toInt();
    const int ss = m.captured(6).isEmpty() ? 0 : m.captured(6).toInt();
    if (!QDate(y, mo, d).isValid()) return { QString(), QString() };
    const QString date = QStringLiteral("%1-%2-%3")
        .arg(m.captured(1), QString::number(mo).rightJustified(2, QLatin1Char('0')),
             QString::number(d).rightJustified(2, QLatin1Char('0')));
    const QString tm = (hh || mm || ss)
        ? QStringLiteral("%1:%2").arg(QString::number(hh).rightJustified(2, QLatin1Char('0')),
                                      QString::number(mm).rightJustified(2, QLatin1Char('0')))
        : QString();
    return { date, tm };
}

// 无拍摄时间就用文件修改时间兜底（宁可有个大概日期，也不要空）
void fillMtimeFallback(ExifData& out, const QFileInfo& fi)
{
    if (!out.d.isEmpty()) return;
    const QDateTime mt = fi.lastModified();
    if (!mt.isValid()) return;
    out.d = mt.date().toString(QStringLiteral("yyyy-MM-dd"));
    if (out.tm.isEmpty())
        out.tm = mt.time().toString(QStringLiteral("HH:mm"));
}

} // namespace

namespace ExifReader {

// >=1s 显示 '15s' / '1.3s'；<1s 显示 '1/125'（exif.py _fmt_shutter）
QString fmtShutter(double t)
{
    if (t <= 0) return QString();
    if (t >= 10) return QStringLiteral("%1s").arg(QString::number(std::round(t)));
    if (t >= 1)  return QStringLiteral("%1s").arg(QString::number(std::round(t * 10.0) / 10.0));
    const double inv = 1.0 / t;
    if (inv >= 2) return QStringLiteral("1/%1").arg(QString::number(std::round(inv)));
    return QStringLiteral("%1s").arg(QString::number(std::round(t * 100.0) / 100.0));
}

// f/1.8 这样显示；小于 10 保留 1 位小数，否则取整（exif.py _fmt_aperture）
QString fmtAperture(double f)
{
    if (f <= 0) return QString();
    if (f < 10) return QStringLiteral("f/%1").arg(QString::number(std::round(f * 10.0) / 10.0));
    return QStringLiteral("f/%1").arg(QString::number(std::round(f)));
}

ExifData readExif(const QString& path)
{
    ExifData out;
    QFileInfo fi(path);
    if (path.isEmpty() || !fi.isFile()) return out;

    // ---- 尺寸（QImageReader；与 Pillow 同样对打不开的格式返回空）
    {
        QImageReader r(path);
        const QSize sz = r.size();
        if (sz.isValid() && sz.width() > 0 && sz.height() > 0)
            out.dim = QStringLiteral("%1%2%3").arg(sz.width())
                          .arg(QChar(0x00D7))                  // ×
                          .arg(sz.height());
    }

    // ---- 取 TIFF blob：按**文件签名**识别容器（piexif.load 同款语义，
    //      而非扩展名 —— 现实中存在 .png 扩展名的 JPEG（实测对拍抓到），
    //      PIL/piexif 均按内容打开，这里保持一致）
    QByteArray blob;
    {
        QFile f(path);
        QByteArray head;
        if (f.open(QIODevice::ReadOnly)) head = f.read(16);
        const bool jpegSig = head.size() >= 2 && uchar(head[0]) == 0xFF && uchar(head[1]) == 0xD8;
        static const uchar kPng[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        const bool pngSig = head.size() >= 8
            && std::memcmp(head.constData(), kPng, 8) == 0;
        const bool tiffSig = head.size() >= 4
            && ((head[0] == 'I' && head[1] == 'I')
                || (head[0] == 'M' && head[1] == 'M'));
        const bool webpSig = head.size() >= 12
            && std::memcmp(head.constData(), "RIFF", 4) == 0
            && std::memcmp(head.constData() + 8, "WEBP", 4) == 0;
        const bool heicSig = head.size() >= 8
            && std::memcmp(head.constData() + 4, "ftyp", 4) == 0;
        if (jpegSig) {
            f.seek(0);                                         // head 已消费 16 字节
            blob = extractJpegExif(f.read(1024 * 1024));       // APP1 必在头部 1MB 内
        } else if (pngSig) {
            f.seek(0);
            QByteArray all = (f.size() < qint64(8) * 1024 * 1024)
                ? f.readAll() : f.read(qint64(8) * 1024 * 1024);
            blob = extractPngExif(all);
        } else if (webpSig) {
            f.seek(0);
            QByteArray all = (f.size() < qint64(8) * 1024 * 1024)
                ? f.readAll() : f.read(qint64(8) * 1024 * 1024);
            blob = extractWebpExif(all);
        } else if (heicSig && f.size() < qint64(64) * 1024 * 1024) {
            f.seek(0);
            blob = extractHeicExif(f.readAll());               // 盒链读取总量上限 64MB
        } else if (tiffSig && f.size() < qint64(64) * 1024 * 1024) {
            f.seek(0);
            blob = f.readAll();
        }
    }

    Tiff t;
    t.d = reinterpret_cast<const uchar*>(blob.constData());
    t.n = blob.size();
    if (t.n >= 4 && t.d[0] == 'I' && t.d[1] == 'I') { t.le = true;  t.ok = true; }
    else if (t.n >= 4 && t.d[0] == 'M' && t.d[1] == 'M') { t.le = false; t.ok = true; }
    if (!t.ok || u16(t, 2) != 42) {
        fillMtimeFallback(out, fi);
        return out;
    }

    const qsizetype ifd0 = qsizetype(u32(t, 4));

    // ---- IFD0：Make/Model/Orientation/DateTime + 子 IFD 指针
    QString make, model, dt0th;
    if (Entry e = getEntry(t, ifd0, 0x010F); e.e >= 0)
        make = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
    if (Entry e = getEntry(t, ifd0, 0x0110); e.e >= 0)
        model = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
    if (Entry e = getEntry(t, ifd0, 0x0112); e.e >= 0 && inRange(t, e.off, 2))
        out.orient = int(u16(t, e.off));
    if (Entry e = getEntry(t, ifd0, 0x0132); e.e >= 0)
        dt0th = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));

    // ---- Exif 子 IFD
    double fNumber = 0, exposureTime = 0, focalLen = 0, bias = 0;
    bool hasAp = false, hasSh = false, hasFocal = false, hasBias = false;
    QString dto, lensModel, lensMake;
    int iso = 0, wbRaw = -1, meterRaw = -1;
    if (Entry ep = getEntry(t, ifd0, 0x8769); ep.e >= 0) {
        const qsizetype exifIfd = qsizetype(u32(t, ep.e + 8));
        auto numAt = [&](quint16 tag, bool* ok) -> double {
            Entry e = getEntry(t, exifIfd, tag);
            if (e.e < 0) { if (ok) *ok = false; return 0.0; }
            return rationalVal(t, e.off, ok);
        };
        exposureTime = numAt(0x829A, &hasSh);
        fNumber      = numAt(0x829D, &hasAp);
        focalLen     = numAt(0x920A, &hasFocal);
        bias         = numAt(0x9204, &hasBias);
        if (Entry e = getEntry(t, exifIfd, 0x8827); e.e >= 0 && inRange(t, e.off, 2))
            iso = (e.ty == 3) ? int(u16(t, e.off)) : int(u32(t, e.off));
        if (Entry e = getEntry(t, exifIfd, 0xA403); e.e >= 0 && inRange(t, e.off, 2))
            wbRaw = (e.ty == 3) ? int(u16(t, e.off)) : int(u32(t, e.off));
        if (Entry e = getEntry(t, exifIfd, 0x9207); e.e >= 0 && inRange(t, e.off, 2))
            meterRaw = (e.ty == 3) ? int(u16(t, e.off)) : int(u32(t, e.off));
        if (Entry e = getEntry(t, exifIfd, 0x9003); e.e >= 0)
            dto = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
        if (Entry e = getEntry(t, exifIfd, 0xA434); e.e >= 0)
            lensModel = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
        if (Entry e = getEntry(t, exifIfd, 0xA433); e.e >= 0)
            lensMake = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
    }

    // ---- GPS 子 IFD
    if (Entry ep = getEntry(t, ifd0, 0x8825); ep.e >= 0) {
        const qsizetype gpsIfd = qsizetype(u32(t, ep.e + 8));
        QString latRef, lonRef;
        if (Entry e = getEntry(t, gpsIfd, 1); e.e >= 0)
            latRef = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
        if (Entry e = getEntry(t, gpsIfd, 3); e.e >= 0)
            lonRef = asciiAt(t, e.off, qsizetype(e.count) * typeSize(e.ty));
        double la = 0, lo = 0;
        bool okLa = false, okLo = false;
        if (Entry e = getEntry(t, gpsIfd, 2); e.e >= 0)
            okLa = dmsToDeg(t, e.off, e.count, latRef, &la);
        if (Entry e = getEntry(t, gpsIfd, 4); e.e >= 0)
            okLo = dmsToDeg(t, e.off, e.count, lonRef, &lo);
        if (okLa && okLo && (la != 0.0 || lo != 0.0)) {
            out.hasGps = true;
            out.la = la;
            out.lo = lo;
            bool okA = false;
            Entry e6 = getEntry(t, gpsIfd, 6);
            const double a = (e6.e >= 0) ? rationalVal(t, e6.off, &okA) : 0.0;
            if (okA) {
                int refByte = 0;
                if (Entry e5 = getEntry(t, gpsIfd, 5); e5.e >= 0 && inRange(t, e5.off, 1))
                    refByte = int(t.d[e5.off]);
                out.hasAlt = true;
                out.alt = int(std::round(refByte == 1 ? -a : a));
            }
        }
    }

    // ---- 组装（Python 组装语义逐条对齐）
    const QStringList mw = make.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!model.isEmpty() && !make.isEmpty() && !mw.isEmpty()
        && model.toUpper().startsWith(mw[0].toUpper()))
        out.cam = model;
    else
        out.cam = (make + QLatin1Char(' ') + model).trimmed();

    out.lens = lensModel.isEmpty() ? lensMake : lensModel;
    if (hasFocal && focalLen > 0) out.focal = std::round(focalLen * 10.0) / 10.0;
    out.ap = hasAp ? fmtAperture(fNumber) : QString();
    out.sh = hasSh ? fmtShutter(exposureTime) : QString();
    out.iso = iso;
    QStringList dt = parseDt(dto);
    if (dt[0].isEmpty()) dt = parseDt(dt0th);
    out.d = dt[0];
    out.tm = dt[1];
    out.evc = hasBias ? std::round(bias * 100.0) / 100.0 : 0.0;
    out.wb = wbLabel(wbRaw);
    out.meter = meterLabel(meterRaw);

    fillMtimeFallback(out, fi);
    return out;
}

} // namespace ExifReader

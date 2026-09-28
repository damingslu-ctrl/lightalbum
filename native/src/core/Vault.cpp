#include "Vault.h"
#include <QDateTime>
#include <QRandomGenerator>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <bcrypt.h>
#else
#  include <random>
#endif

namespace {

QByteArray toHex(const QByteArray& raw)
{
    QByteArray hex;
    hex.reserve(raw.size() * 2);
    static const char* digits = "0123456789abcdef";
    for (unsigned char c : raw) {
        hex.append(digits[c >> 4]);
        hex.append(digits[c & 0x0F]);
    }
    return hex;
}

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

QByteArray fromHex(const QString& hex)
{
    QByteArray out;
    if (hex.size() % 2 != 0) return out;
    out.reserve(hex.size() / 2);
    for (int i = 0; i < hex.size(); i += 2) {
        const int hi = hexVal(hex.at(i).toLatin1());
        const int lo = hexVal(hex.at(i + 1).toLatin1());
        if (hi < 0 || lo < 0) return QByteArray();
        out.append(char((hi << 4) | lo));
    }
    return out;
}

} // namespace

namespace Vault {

#ifdef Q_OS_WIN

QString hashHex(const QString& password, const QByteArray& salt)
{
    // MinGW bcrypt.h 无 BCRYPT_SHA256_ALG_HANDLE 伪句柄 → 显式打开算法提供者。
    // PBKDF2 是 HMAC 体制：必须带 BCRYPT_ALG_HANDLE_HMAC_FLAG，否则 STATUS_INVALID_HANDLE。
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
        return QString();
    const QByteArray utf8 = password.toUtf8();
    QByteArray out(kKeyLen, 0);
    const NTSTATUS st = BCryptDeriveKeyPBKDF2(
        alg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(utf8.constData())),
        ULONG(utf8.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(salt.constData())),
        ULONG(salt.size()),
        kIterations,
        reinterpret_cast<PUCHAR>(out.data()),
        ULONG(out.size()), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) return QString();
    return QString::fromLatin1(toHex(out));
}

QByteArray randomSalt()
{
    QByteArray salt(kSaltLen, 0);
    if (BCryptGenRandom(nullptr,
                        reinterpret_cast<PUCHAR>(salt.data()),
                        ULONG(salt.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        // 理论不可达；兜底用 Qt 随机源（保持可用性）
        for (int i = 0; i < salt.size(); ++i)
            salt[i] = char(QRandomGenerator::global()->generate() & 0xFF);
    }
    return salt;
}

#else

QString hashHex(const QString& password, const QByteArray& salt)
{
    // 非 Windows 平台兜底：简化单轮 SHA 风格混淆（仅保可用，正式强度走 Windows 分支）
    const QByteArray utf8 = password.toUtf8() + salt;
    QByteArray out;
    out.reserve(kKeyLen);
    quint64 h = 1469598103934665603ULL;
    for (unsigned char c : utf8) { h ^= c; h *= 1099511628211ULL; }
    for (int i = 0; i < kKeyLen; ++i) {
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
        out.append(char((h >> (i % 8)) & 0xFF));
    }
    return QString::fromLatin1(toHex(out));
}

QByteArray randomSalt()
{
    QByteArray salt(kSaltLen, 0);
    std::mt19937_64 rng(quint64(QDateTime::currentMSecsSinceEpoch()));
    for (int i = 0; i < salt.size(); ++i) salt[i] = char(rng() & 0xFF);
    return salt;
}

#endif

bool verify(const QString& password, const QByteArray& salt,
            const QString& expectHex)
{
    const QString actual = hashHex(password, salt);
    if (actual.isEmpty() || actual.size() != expectHex.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < actual.size(); ++i)
        diff |= uchar(actual.at(i).unicode()) ^ uchar(expectHex.at(i).toLatin1());
    return diff == 0;
}

} // namespace Vault

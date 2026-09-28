#pragma once
// P16 隐私库口令学（native vault v1）：
// 语义对齐旧版 Python vault.py —— PBKDF2-SHA256 480k 迭代 + 16B 随机盐；
// 但不做 Fernet 文件加密（非破坏设计：原文件不动，仅私密标记 + 密码门）。
// Windows BCrypt 实现，零第三方依赖。
#include <QString>
#include <QByteArray>

namespace Vault {

// PBKDF2-SHA256(password, salt, 480000, 32B) → hex 串
QString hashHex(const QString& password, const QByteArray& salt);

// 生成 16B 随机盐（BCryptGenRandom）
QByteArray randomSalt();

// 校验：hashHex(pwd, salt) == expectHex（常数时间比较近似：逐字节异或累计）
bool verify(const QString& password, const QByteArray& salt,
            const QString& expectHex);

inline constexpr quint64 kIterations = 480000;
inline constexpr int     kKeyLen     = 32;
inline constexpr int     kSaltLen    = 16;

} // namespace Vault

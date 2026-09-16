// ============================================================================
// 阶段 B · B1 · 密码哈希实现
//
// 实现要点:
//   1. salt = QByteArray::number(QRandomGenerator::securelySeeded().bounded())
//      实际取 16 字节随机(QRandomGenerator 不够安全,生产场景换 libsodium)
//   2. hash = SHA256(salt_bytes + utf8(password_bytes)) → hex
//   3. 验证 = 重算一遍比较
//
// 安全注:B1 阶段用 QRandomGenerator;B6 阶段评估升级到 OS CSPRNG。
//   详见 docs/优化与改善计划.html "B6 工程加密"。
//
// 关联:PasswordHasher.h
// ============================================================================

#include "auth/PasswordHasher.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QByteArray>

namespace qwhmi::auth {

namespace {
constexpr int kSaltBytes = 16;  // 128 bit salt
}

QString generateSalt() {
    QByteArray salt;
    salt.reserve(kSaltBytes);
    // QRandomGenerator::global() 在 Qt 6 由 OS CSPRNG 种子(Win: BCryptGenRandom)
    // 单进程取 16 字节足够;不追求密码学最强(QRandomGenerator 不是密码学随机数)
    // B6 阶段会替换;目前 B1 阶段先用 Qt 内置 + BCryptGenRandom
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < kSaltBytes; ++i) {
        salt.append(static_cast<char>(rng->bounded(256)));
    }
    return QString::fromLatin1(salt.toHex());
}

QString hashPassword(const QString& plain, const QString& saltHex) {
    if (plain.isEmpty() || saltHex.length() != kSaltBytes * 2) {
        return {};
    }
    QByteArray combined;
    combined.append(QByteArray::fromHex(saltHex.toLatin1()));
    combined.append(plain.toUtf8());
    const QByteArray digest = QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

bool verifyPassword(const QString& plain, const QString& saltHex, const QString& expectedHashHex) {
    if (expectedHashHex.length() != 64) {  // SHA256 = 32 bytes = 64 hex
        return false;
    }
    const QString actual = hashPassword(plain, saltHex);
    if (actual.isEmpty()) {
        return false;
    }
    // 常数时间比较?简化版先直比,B6 再升级
    return actual == expectedHashHex;
}

} // namespace qwhmi::auth
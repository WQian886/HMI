// ============================================================================
// 阶段 B · B1 · 密码哈希工具
//
// 文件职责:声明 hashPassword / verifyPassword / generateSalt 三个 API。
//
// 设计:
//   - 算法:SHA256(salt_bytes + utf8_password_bytes) → hex
//   - salt:每次新建用户时随机生成 16 字节(避免彩虹表)
//   - 验证:重算一遍比对 hex(常数时间?B6 工程加密阶段再升级)
//
// 为什么 SHA256 而不是 bcrypt/argon2:Qt6 标准库自带 QCryptographicHash,
//   无需引第三方依赖,符合 B1 阶段"最小骨架"原则。
//   B6 阶段会评估升级到 scrypt 或 argon2id(那是工程加密的范围)。
//
// 关联:docs/优化与改善计划.html "阶段 B1",User.h
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::auth {

// 生成 16 字节随机盐,返回 hex(32 字符)
QString generateSalt();

// 用给定 salt 对明文密码做 SHA256,返回 hex(64 字符)
// 输入:plain(明文) + saltHex(32 字符 hex)
// 输出:64 字符 hex;输入非法返空字符串
QString hashPassword(const QString& plain, const QString& saltHex);

// 校验:用相同 salt 重算并比对 hash
// 输入:plain + saltHex + expectedHashHex
// 输出:true = 匹配,false = 不匹配或参数非法
bool verifyPassword(const QString& plain, const QString& saltHex, const QString& expectedHashHex);

} // namespace qwhmi::auth
// ============================================================================
// 阶段 B · B1 · 用户数据模型
//
// 文件职责:User POD(纯数据)+ 拷贝构造 / 比较运算。
//   不含 Q_OBJECT(B1.1 骨架阶段不接 signal,后续 UserManager 加)。
//
// 字段说明:
//   - name        登录名(唯一,大小写不敏感比较)
//   - role        角色(4 选 1)
//   - passwordHash  SHA256(salt + password) 的十六进制
//   - salt        16 字节随机盐(hex 编码存)
//   - createdAt   ISO8601 字符串(QT 时间)
//   - disabled    软删除标记(B5 审计要保留历史)
//
// 安全:不存明文密码;hash + salt 都必须非空(由 PasswordHasher 保证)。
//
// 关联:docs/优化与改善计划.html "阶段 B1",PasswordHasher.h
// ============================================================================

#pragma once

#include "auth/Role.h"

#include <QString>

namespace qwhmi::auth {

struct User {
    QString    name;
    Role       role = Role::Viewer;
    QString    passwordHash;  // SHA256 hex
    QString    salt;          // 16-byte random hex
    QString    createdAt;     // ISO8601
    bool       disabled = false;

    // 名字比较(大小写不敏感 + 去前后空格)
    static bool namesEqual(const QString& a, const QString& b);
};

} // namespace qwhmi::auth
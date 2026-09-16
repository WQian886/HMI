// ============================================================================
// 阶段 B · B1.3 · 用户 JSON 存读
//
// 文件职责:把 UserManager 持久化到 users.json,放在 QStandardPaths::AppLocalDataLocation。
//
// 为什么单独一个类(不复用 Project 那套):
//   - Project 存的是"工程文件"(变量/报警/动作/通讯),JSON 结构复杂且带 schema_version
//   - 用户列表是另一码事,字段少且简单,手写 JSON 即可,复用会引入噪声
//
// 字段:name / role / passwordHash / salt / createdAt / disabled
//
// 文件位置(QStandardPaths::AppLocalDataLocation):
//   %LOCALAPPDATA%/QianWei/HMI/users.json
//
// 关联:UserManager.h,PasswordHasher.h
// ============================================================================

#pragma once

#include "auth/User.h"

#include <QList>
#include <QString>

namespace qwhmi::auth {

class UserManager;

class UserStore {
public:
    // 默认路径:QStandardPaths::AppLocalDataLocation + "/users.json"
    static QString defaultFilePath();

    // 把 UserManager 里所有用户写到文件。失败返 false(并 qCWarning)
    static bool save(const UserManager& mgr, const QString& filePath = {});

    // 从文件读用户,逐个 addUser 到 mgr(失败用户跳过,不 abort 整个加载)
    // 返回成功加载的用户数
    static int load(UserManager& mgr, const QString& filePath = {});

    // 仅辅助测试:序列化/反序列化一对 User(直接对 JSON 操作)
    // 不读不写文件
    static QString toJsonString(const QList<User>& users);
    static QList<User> fromJsonString(const QString& json, bool* ok = nullptr);
};

} // namespace qwhmi::auth
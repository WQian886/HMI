// ============================================================================
// 阶段 B · B1.3 · UserStore 实现
//
// 关联:UserStore.h
// ============================================================================

#include "auth/UserStore.h"
#include "auth/UserManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "logging/Logging.h"

namespace qwhmi::auth {

QString UserStore::defaultFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return dir + QStringLiteral("/users.json");
}

QString UserStore::toJsonString(const QList<User>& users) {
    QJsonArray arr;
    for (const User& u : users) {
        QJsonObject obj;
        obj["name"]         = u.name;
        obj["role"]         = roleToString(u.role);
        obj["passwordHash"] = u.passwordHash;
        obj["salt"]         = u.salt;
        obj["createdAt"]    = u.createdAt;
        obj["disabled"]     = u.disabled;
        arr.append(obj);
    }
    QJsonObject root;
    root["schema_version"] = 1;
    root["users"]          = arr;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QList<User> UserStore::fromJsonString(const QString& json, bool* ok) {
    QList<User> out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok) *ok = false;
        qCWarning(hmiApp) << "[UserStore] JSON 解析失败:" << err.errorString();
        return out;
    }
    const QJsonObject root = doc.object();
    const int schema = root.value("schema_version").toInt(0);
    if (schema != 1) {
        if (ok) *ok = false;
        qCWarning(hmiApp) << "[UserStore] 不支持的 schema_version:" << schema;
        return out;
    }
    const QJsonArray arr = root.value("users").toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject obj = v.toObject();
        User u;
        u.name         = obj.value("name").toString();
        u.role         = roleFromString(obj.value("role").toString());
        u.passwordHash = obj.value("passwordHash").toString();
        u.salt         = obj.value("salt").toString();
        u.createdAt    = obj.value("createdAt").toString();
        u.disabled     = obj.value("disabled").toBool(false);
        if (u.name.isEmpty() || u.passwordHash.isEmpty() || u.salt.isEmpty()) {
            qCWarning(hmiApp) << "[UserStore] 跳过不完整用户记录";
            continue;
        }
        out.append(u);
    }
    if (ok) *ok = true;
    return out;
}

bool UserStore::save(const UserManager& mgr, const QString& filePath) {
    const QString path = filePath.isEmpty() ? defaultFilePath() : filePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString json = toJsonString(mgr.listUsers());
    // QSaveFile = 原子写(临时文件 + rename,防崩了留半截)
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(hmiApp) << "[UserStore] save 打开失败:" << path << f.errorString();
        return false;
    }
    f.write(json.toUtf8());
    if (!f.commit()) {
        qCWarning(hmiApp) << "[UserStore] save commit 失败:" << path << f.errorString();
        return false;
    }
    qCInfo(hmiApp) << "[UserStore] 已保存" << mgr.count() << "用户到" << path;
    return true;
}

int UserStore::load(UserManager& mgr, const QString& filePath) {
    const QString path = filePath.isEmpty() ? defaultFilePath() : filePath;
    QFile f(path);
    if (!f.exists()) {
        qCInfo(hmiApp) << "[UserStore] 文件不存在(首次启动):" << path;
        return 0;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(hmiApp) << "[UserStore] load 打开失败:" << path << f.errorString();
        return 0;
    }
    const QString json = QString::fromUtf8(f.readAll());
    f.close();
    bool ok = false;
    const QList<User> users = fromJsonString(json, &ok);
    if (!ok) return 0;
    int loaded = 0;
    for (const User& u : users) {
        mgr.appendLoadedUser(u);
        ++loaded;
    }
    qCInfo(hmiApp) << "[UserStore] 已加载" << loaded << "用户从" << path;
    return loaded;
}

} // namespace qwhmi::auth
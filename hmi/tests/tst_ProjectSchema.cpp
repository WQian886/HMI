// ============================================================================
// 阶段 C · C.0 · Project schema 演进单测
//
// 验证 C.0 schema_version 迁移框架:
//   1. migrateV1ToV2 把 communications 单 PLC 提到 plcs[0] 多 PLC 数组
//   2. migrateV1ToV2 schema_version "1.0" → 数字 2
//   3. migrateV1ToV2 保留 modbus 子对象(timeout/max_retries/poll_interval_ms)
//   4. migrateV1ToV2 modbus 子对象缺失时用默认值
//   5. migrateTo 链式调用(预留未来 v2→v3)
//   6. migrateTo 未知版本返回 false
//
// 关联:Project.h, Project.cpp
// ============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include "Project.h"
#include "VariableManager.h"
#include "AlarmManager.h"
#include "ActionManager.h"
#include "PollScheduler.h"

class TestProjectSchema : public QObject {
    Q_OBJECT
private slots:
    // v1 → v2 单步迁移
    void migrateV1ToV2_addsPlcsField();
    void migrateV1ToV2_convertsSchemaVersionToNumber();
    void migrateV1ToV2_removesCommunicationsField();
    void migrateV1ToV2_preservesModbusSubObject();
    void migrateV1ToV2_usesDefaultsWhenModbusMissing();
    void migrateV1ToV2_handlesMissingCommunicationsGracefully();

    // migrateTo 链式
    void migrateTo_chainsFromV1ToV2();
    void migrateTo_returnsFalseOnUnknownVersion();

    // saveToFile 写新格式
    void saveToFile_writesSchemaVersion2();
    void saveToFile_writesPlcsArrayNotCommunications();
};

namespace {
// 构造一个标准 v1 JSON 字符串(模拟老 default.json)
QJsonObject makeV1Json() {
    QJsonObject root;
    root["schema_version"] = "1.0";
    root["project_name"]   = "Old Project";
    root["created_at"]     = "2026-08-11T14:36:05";
    root["updated_at"]     = "2026-08-11T14:36:05";
    QJsonObject comm;
    comm["protocol"] = "modbus_tcp";
    comm["host"]     = "192.168.1.10";
    comm["port"]     = 502;
    QJsonObject modbus;
    modbus["timeout_ms"]       = 3000;
    modbus["max_retries"]      = 5;
    modbus["poll_interval_ms"] = 100;
    comm["modbus"] = modbus;
    root["communications"] = comm;
    QJsonArray vars;
    QJsonObject v;
    v["name"] = "Speed";
    v["address"] = 2;
    v["data_type"] = "UInt16";
    v["access"] = "ReadWrite";
    vars.append(v);
    root["variables"] = vars;
    return root;
}
} // namespace

void TestProjectSchema::migrateV1ToV2_addsPlcsField() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root.contains("plcs"));
    QJsonArray plcsArr = root["plcs"].toArray();
    QCOMPARE(plcsArr.size(), 1);
    QJsonObject plc0 = plcsArr[0].toObject();
    QCOMPARE(plc0["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));
    QCOMPARE(plc0["host"].toString(), QStringLiteral("192.168.1.10"));
    QCOMPARE(plc0["port"].toInt(), 502);
}

void TestProjectSchema::migrateV1ToV2_convertsSchemaVersionToNumber() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(root["schema_version"].isString());   // 迁移前是字符串
    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root["schema_version"].isDouble());   // 迁移后是数字
    QCOMPARE(root["schema_version"].toInt(), 2);
}

void TestProjectSchema::migrateV1ToV2_removesCommunicationsField() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();
    QVERIFY(root.contains("communications"));     // 迁移前有

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(!root.contains("communications"));    // 迁移后无(冗余,信息已搬到 plcs[0])
}

void TestProjectSchema::migrateV1ToV2_preservesModbusSubObject() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(p.migrateV1ToV2(root));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QVERIFY(plc0.contains("modbus"));
    QJsonObject modbus = plc0["modbus"].toObject();
    QCOMPARE(modbus["timeout_ms"].toInt(), 3000);            // 保留
    QCOMPARE(modbus["max_retries"].toInt(), 5);               // 保留
    QCOMPARE(modbus["poll_interval_ms"].toInt(), 100);        // 保留
}

void TestProjectSchema::migrateV1ToV2_usesDefaultsWhenModbusMissing() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();
    QJsonObject comm = root["communications"].toObject();
    comm.remove("modbus");                        // 故意删 modbus 子对象
    root["communications"] = comm;

    QVERIFY(p.migrateV1ToV2(root));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QVERIFY(plc0.contains("modbus"));             // 应该用默认补回来
    QJsonObject modbus = plc0["modbus"].toObject();
    QCOMPARE(modbus["timeout_ms"].toInt(), 3000);
    QCOMPARE(modbus["max_retries"].toInt(), 5);
    QCOMPARE(modbus["poll_interval_ms"].toInt(), 100);
}

void TestProjectSchema::migrateV1ToV2_handlesMissingCommunicationsGracefully() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root;
    root["schema_version"] = "1.0";
    // 没有 communications 字段(最老的格式)
    // 不应该崩,用默认值

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root.contains("plcs"));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));   // 默认
    QCOMPARE(plc0["host"].toString(), QStringLiteral("127.0.0.1"));         // 默认
    QCOMPARE(plc0["port"].toInt(), 502);                                    // 默认
}

void TestProjectSchema::migrateTo_chainsFromV1ToV2() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    // migrateTo(fromVersion=1, toVersion=2) 应该依次调 v1→v2
    QVERIFY(p.migrateTo(root, 1, 2));
    QCOMPARE(root["schema_version"].toInt(), 2);
    QVERIFY(root.contains("plcs"));
    QVERIFY(!root.contains("communications"));
}

void TestProjectSchema::migrateTo_returnsFalseOnUnknownVersion() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    // migrateTo(fromVersion=99, toVersion=100) — 没有对应迁移函数 → 返 false
    QVERIFY(!p.migrateTo(root, 99, 100));
    // 失败时 root 不变(不丢数据)
    QCOMPARE(root["schema_version"].toString(), QStringLiteral("1.0"));
}

void TestProjectSchema::saveToFile_writesSchemaVersion2() {
    // saveToFile 需要真实的 managers(检查 !varMgr_ / !alarmMgr_ / !actionMgr_)
    // 所以这 2 个用例建最简实例验证写文件行为
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    // PollScheduler 构造需要 varMgr + ModbusMaster* — 我们不用它的 tick,只用来满足 Project 构造
    // 给 null ModbusMaster 让 PollScheduler 不启动(默认不调用 start())
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    // 用固定路径(避免 QTemporaryFile 和 QSaveFile 冲突)
    const QString path = "E:/qianwie/hmi/hmi/tests/build/tst_saveToFile_writesSchemaVersion2.json";
    QFile::remove(path);  // 清掉旧文件

    QVERIFY(p.saveToFile(path));

    // 读回 + 验证
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QVERIFY(doc.isObject());
    QJsonObject root = doc.object();
    QCOMPARE(root["schema_version"].toInt(), 2);          // ← 关键:数字不是字符串
    QVERIFY(root.contains("plcs"));
    QVERIFY(!root.contains("communications"));             // ← 不再写老字段

    QFile::remove(path);  // 清理
}

void TestProjectSchema::saveToFile_writesPlcsArrayNotCommunications() {
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    const QString path = "E:/qianwie/hmi/hmi/tests/build/tst_saveToFile_writesPlcsArrayNotCommunications.json";
    QFile::remove(path);

    QVERIFY(p.saveToFile(path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QVERIFY(root.contains("plcs"));
    QJsonArray plcsArr = root["plcs"].toArray();
    QCOMPARE(plcsArr.size(), 1);
    QJsonObject plc0 = plcsArr[0].toObject();
    QCOMPARE(plc0["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));

    QFile::remove(path);
}

QTEST_MAIN(TestProjectSchema)
#include "tst_ProjectSchema.moc"

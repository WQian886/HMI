// ============================================================================
// 阶段 A · A5 单元测试 · JsonSerializer
//
// 测试目标:JsonSerializer::toJson / fromJsonVariable 对 Variable 的双向转换。
//
// 为什么需要:JSON 是工程文件格式,序列化/反序列化错一个字段 = 工程加载失败
//   或部分丢失。阶段 6 loadFromFile 严重依赖。
//
// 覆盖用例(4 个):
//   variable_toJson              含 range 的完整序列化(所有字段)
//   variable_toJson_noRange      无 range 不写 range 字段(节省 JSON 体积)
//   variable_fromJson_roundtrip  反序列化所有字段
//   variable_fromJson_badType    非法 data_type 字符串 → 返 nullptr
//
// 注意:enum 路径(alarmLevelToString / directionToString 等)需要 Alarm.h,
//   留给阶段 F 报警一起补。当前只覆盖 Variable 路径。
//
// 详见:docs/阶段A_工程化基线.md + JsonSerializer.h
// ============================================================================

#include <QtTest>
#include "JsonSerializer.h"
#include "Variable.h"
#include "VariableManager.h"

class tst_Serializer : public QObject {
    Q_OBJECT
private slots:
    void variable_toJson();
    void variable_toJson_noRange();
    void variable_fromJson_roundtrip();
    void variable_fromJson_badType_returnsNull();
};

void tst_Serializer::variable_toJson() {
    Variable v("Speed", 0x0001, Variable::DataType::Int16, Variable::Access::ReadWrite);
    v.setRange(QVariant::fromValue<qint16>(0), QVariant::fromValue<qint16>(3000));
    v.setDeadband(0.5);
    v.setPollIntervalMs(500);

    const QJsonObject obj = JsonSerializer::toJson(v);
    QCOMPARE(obj.value("name").toString(), QString("Speed"));
    QCOMPARE(obj.value("address").toInt(), 0x0001);
    QCOMPARE(obj.value("data_type").toString(), QString("Int16"));
    QCOMPARE(obj.value("access").toString(), QString("ReadWrite"));
    QCOMPARE(obj.value("deadband").toDouble(), 0.5);
    QCOMPARE(obj.value("poll_interval_ms").toInt(), 500);
    QVERIFY(obj.contains("range"));
    QCOMPARE(obj.value("range").toObject().value("min").toInt(), 0);
    QCOMPARE(obj.value("range").toObject().value("max").toInt(), 3000);
}

void tst_Serializer::variable_toJson_noRange() {
    Variable v("Raw", 0x0002, Variable::DataType::UInt16, Variable::Access::ReadOnly);
    const QJsonObject obj = JsonSerializer::toJson(v);
    QVERIFY(!obj.contains("range"));
}

void tst_Serializer::variable_fromJson_roundtrip() {
    QJsonObject obj;
    obj["name"]             = "Speed";
    obj["address"]          = 0x0001;
    obj["data_type"]        = "Int16";
    obj["access"]           = "ReadWrite";
    obj["deadband"]         = 0.5;
    obj["poll_interval_ms"] = 500;
    QJsonObject range;
    range["min"] = 0;
    range["max"] = 3000;
    obj["range"] = range;

    VariableManager mgr;
    Variable* v2 = JsonSerializer::fromJsonVariable(obj, &mgr);
    QVERIFY(v2 != nullptr);
    QCOMPARE(v2->name(), QString("Speed"));
    QCOMPARE(v2->address(), quint16(0x0001));
    QVERIFY(v2->dataType() == Variable::DataType::Int16);
    QVERIFY(v2->access() == Variable::Access::ReadWrite);
    QCOMPARE(v2->pollIntervalMs(), 500);
    QVERIFY(v2->hasRange());
}

void tst_Serializer::variable_fromJson_badType_returnsNull() {
    QJsonObject obj;
    obj["name"]      = "X";
    obj["address"]   = 0;
    obj["data_type"] = "NonExistent";
    obj["access"]    = "ReadOnly";

    VariableManager mgr;
    Variable* v = JsonSerializer::fromJsonVariable(obj, &mgr);
    QCOMPARE(v, nullptr);
}

QTEST_MAIN(tst_Serializer)
#include "tst_Serializer.moc"

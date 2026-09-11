// ============================================================================
// 阶段 A · A5 单元测试 · Variable
//
// 测试目标:Variable 数据模型的 setValue / deadband / range / quality。
//
// 为什么需要:Variable 是整个数据层基础,改一行就有连锁影响(UI、报警、
//   报表、轮询都依赖)。把行为锁在测试里 = 改动时不会无声破坏。
//
// 覆盖用例(10 个):
//   construction()              构造默认值(name/address/quality=Bad/dirty=false)
//   setValue_accepted()         正常赋值 → Accepted + emit valueChanged
//   setValue_noChange()         同值 → NoChange(不 emit)
//   setValue_deadband_filtered  |新-旧| < deadband → NoChange
//   setValue_deadband_passed    |新-旧| > deadband → Accepted
//   setValue_typeMismatch       类型不匹配 → Rejected
//   setValue_rangeCheck_rejected 超出 [min,max] → Rejected
//   setValue_rangeCheck_skipped checkRange=false 跳过范围(PLC 原始值可能超)
//   markDirty_clearDirty        dirty 标记双向
//   quality_noSpam              同 quality 不 emit(避免信号风暴)
//
// 怎么跑:
//   cd tests
//   cmake -B build -G "Visual Studio 17 2022" -A x64
//   cmake --build build --config Debug
//   ctest --test-dir build -C Debug --output-on-failure
//
// 详见:docs/阶段A_工程化基线.md "A5 单元测试" + Variable.h
// ============================================================================

#include <QtTest>
#include "Variable.h"

class tst_Variable : public QObject {
    Q_OBJECT
private slots:
    void construction();
    void setValue_accepted();
    void setValue_noChange();
    void setValue_deadband_filtered();
    void setValue_deadband_passed();
    void setValue_typeMismatch();
    void setValue_rangeCheck_rejected();
    void setValue_rangeCheck_skipped();
    void markDirty_clearDirty();
    void quality_noSpam();
};

void tst_Variable::construction() {
    Variable v("Speed", 0x0001, Variable::DataType::Int16, Variable::Access::ReadWrite);
    QCOMPARE(v.name(), QString("Speed"));
    QCOMPARE(v.address(), quint16(0x0001));
    QVERIFY(v.dataType() == Variable::DataType::Int16);
    QVERIFY(v.access() == Variable::Access::ReadWrite);
    QCOMPARE(v.pollIntervalMs(), 1000);
    QCOMPARE(v.deadband(), 0.0);
    QVERIFY(v.quality() == Variable::Quality::Bad);
    QVERIFY(!v.isDirty());
}

void tst_Variable::setValue_accepted() {
    Variable v("X", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QSignalSpy spy(&v, &Variable::valueChanged);
    auto r = v.setValue(QVariant::fromValue<qint16>(42));
    QCOMPARE(r, Variable::SetValueResult::Accepted);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(v.value().toInt(), 42);
}

void tst_Variable::setValue_noChange() {
    Variable v("X", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    v.setValue(QVariant::fromValue<qint16>(42));
    QSignalSpy spy(&v, &Variable::valueChanged);
    auto r = v.setValue(QVariant::fromValue<qint16>(42));
    QCOMPARE(r, Variable::SetValueResult::NoChange);
    QCOMPARE(spy.count(), 0);
}

void tst_Variable::setValue_deadband_filtered() {
    Variable v("Temp", 0, Variable::DataType::Float, Variable::Access::ReadOnly);
    v.setValue(QVariant::fromValue<float>(50.0f));
    v.setDeadband(0.5);
    QSignalSpy spy(&v, &Variable::valueChanged);
    auto r = v.setValue(QVariant::fromValue<float>(50.3f));
    QCOMPARE(r, Variable::SetValueResult::NoChange);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(v.value().toFloat(), 50.0f);
}

void tst_Variable::setValue_deadband_passed() {
    Variable v("Temp", 0, Variable::DataType::Float, Variable::Access::ReadOnly);
    v.setValue(QVariant::fromValue<float>(50.0f));
    v.setDeadband(0.5);
    QSignalSpy spy(&v, &Variable::valueChanged);
    auto r = v.setValue(QVariant::fromValue<float>(51.0f));
    QCOMPARE(r, Variable::SetValueResult::Accepted);
    QCOMPARE(spy.count(), 1);
}

void tst_Variable::setValue_typeMismatch() {
    Variable v("X", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QSignalSpy spy(&v, &Variable::valueChanged);
    auto r = v.setValue(QVariant::fromValue<float>(3.14f));
    QCOMPARE(r, Variable::SetValueResult::Rejected);
    QCOMPARE(spy.count(), 0);
}

void tst_Variable::setValue_rangeCheck_rejected() {
    Variable v("Speed", 0, Variable::DataType::Int16, Variable::Access::ReadWrite);
    v.setRange(QVariant::fromValue<qint16>(0), QVariant::fromValue<qint16>(1000));
    auto r = v.setValue(QVariant::fromValue<qint16>(1500));
    QCOMPARE(r, Variable::SetValueResult::Rejected);
}

void tst_Variable::setValue_rangeCheck_skipped() {
    Variable v("Raw", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    v.setRange(QVariant::fromValue<qint16>(0), QVariant::fromValue<qint16>(1000));
    auto r = v.setValue(QVariant::fromValue<qint16>(2000), false);
    QCOMPARE(r, Variable::SetValueResult::Accepted);
    QCOMPARE(v.value().toInt(), 2000);
}

void tst_Variable::markDirty_clearDirty() {
    Variable v("X", 0, Variable::DataType::Int16, Variable::Access::ReadWrite);
    QVERIFY(!v.isDirty());
    v.markDirty();
    QVERIFY(v.isDirty());
    v.clearDirty();
    QVERIFY(!v.isDirty());
}

void tst_Variable::quality_noSpam() {
    Variable v("X", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    v.setQuality(Variable::Quality::Good);
    QSignalSpy spy(&v, &Variable::qualityChanged);
    v.setQuality(Variable::Quality::Good);
    QCOMPARE(spy.count(), 0);
    v.setQuality(Variable::Quality::Bad);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(tst_Variable)
#include "tst_Variable.moc"

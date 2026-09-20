// ============================================================================
// 阶段 A · A5 单元测试 · Variable
//
// 测试目标:Variable 数据模型的 setValue / deadband / range / quality。
//
// 为什么需要:Variable 是整个数据层基础,改一行就有连锁影响(UI、报警、
//   报表、轮询都依赖)。把行为锁在测试里 = 改动时不会无声破坏。
//
// 覆盖用例(12 个):原有 10 + D.5 别名 2 个
//   ...
//   alias_addAndLookup           D.5:addAlias 后 aliases_ 列表 + displayName 用首个别名
//   alias_removeAndEdgeCases     D.5:removeAlias + 空/重复/==name 都跳过
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
    void alias_addAndLookup();         // D.5
    void alias_removeAndEdgeCases();   // D.5
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

// ★ D.5:别名增 + displayName 用首个别名
void tst_Variable::alias_addAndLookup() {
    Variable v("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly);
    QCOMPARE(v.displayName(), QString("Speed"));   // 无别名 → 用 name_

    v.addAlias("转速");
    QCOMPARE(v.aliases().size(), 1);
    QCOMPARE(v.displayName(), QString("转速"));     // 首个别名 = 转速

    v.addAlias("RPM");                              // 多个别名
    QCOMPARE(v.aliases().size(), 2);
    QCOMPARE(v.displayName(), QString("转速"));     // 仍是第一个
}

// ★ D.5:别名删除 + 边界(空字符串 / ==name / 重复添加都跳过)
void tst_Variable::alias_removeAndEdgeCases() {
    Variable v("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly);

    v.addAlias("");        // 空字符串 → 跳过
    v.addAlias("Speed");   // == name_ → 跳过(避免重名)
    QCOMPARE(v.aliases().size(), 0);

    v.addAlias("转速");
    v.addAlias("转速");    // 重复 → 跳过
    QCOMPARE(v.aliases().size(), 1);

    v.removeAlias("转速");
    QCOMPARE(v.aliases().size(), 0);
    v.removeAlias("nonexistent");  // 删除不存在 → 不崩
}

QTEST_MAIN(tst_Variable)
#include "tst_Variable.moc"

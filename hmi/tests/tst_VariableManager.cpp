// ============================================================================
// 阶段 A · A5 单元测试 · VariableManager
//
// 测试目标:VariableManager 的 add/find/remove + 同名替换时 emit variableReplaced。
//
// 为什么需要:同 name 替换(loadFromFile 重建 Variable 的核心机制)的正确性
//   关系到多个上层(ThresholdAlarmChecker / ActionManager / Binding)能否收到
//   "指针失效"通知,从而避免 use-after-free。
//
// 覆盖用例(4 个):
//   empty()                     空 manager
//   add_find_remove             基本 CRUD
//   replace_sameName_emits      同名替换 emit variableReplaced(old, new)
//   duplicate_null_or_empty     空指针 / 空 name 拒绝
//
// 详见:docs/阶段A_工程化基线.md + VariableManager.h
// ============================================================================

#include <QtTest>
#include "VariableManager.h"
#include "Variable.h"

class tst_VariableManager : public QObject {
    Q_OBJECT
private slots:
    void empty();
    void add_find_remove();
    void replace_sameName_emits();
    void duplicate_null_or_empty();
};

void tst_VariableManager::empty() {
    VariableManager mgr;
    QCOMPARE(mgr.count(), 0);
    QVERIFY(mgr.allVariables().isEmpty());
    QVERIFY(!mgr.removeVariable("NotExists"));
    QCOMPARE(mgr.findVariable("X"), nullptr);
}

void tst_VariableManager::add_find_remove() {
    VariableManager mgr;
    auto* v = new Variable("X", 0x0001, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QVERIFY(mgr.addVariable(v));
    QCOMPARE(mgr.count(), 1);
    QCOMPARE(mgr.findVariable("X"), v);
    QVERIFY(mgr.removeVariable("X"));
    QCOMPARE(mgr.count(), 0);
}

void tst_VariableManager::replace_sameName_emits() {
    VariableManager mgr;
    auto* v1 = new Variable("X", 0x0001, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QVERIFY(mgr.addVariable(v1));

    QSignalSpy spy(&mgr, &VariableManager::variableReplaced);

    auto* v2 = new Variable("X", 0x0002, Variable::DataType::Float, Variable::Access::ReadWrite);
    QVERIFY(mgr.addVariable(v2));

    QCOMPARE(mgr.count(), 1);
    QCOMPARE(mgr.findVariable("X"), v2);
    QCOMPARE(spy.count(), 1);
}

void tst_VariableManager::duplicate_null_or_empty() {
    VariableManager mgr;
    QVERIFY(!mgr.addVariable(nullptr));
    auto* v = new Variable("", 0, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QVERIFY(!mgr.addVariable(v));
    delete v;
}

QTEST_MAIN(tst_VariableManager)
#include "tst_VariableManager.moc"

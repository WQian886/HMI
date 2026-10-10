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
    void findByAlias_returnsCorrectVariable();   // D.5

    // ---- ★ 改善清单 #6/#7:删变量必须广播 ----
    void remove_emitsVariableReplaced_withNullNew();
    void clear_emitsForEveryVariable();
    void remove_ofMissingName_doesNotEmit();
};

// 一个"持有指针的人":模拟 WriteScheduler / TrendWidget 这类
// 【把 Variable* 存起来、稍后再解引用】的监听者。
// ★ 为什么不用 QSignalSpy 就完事
//   QSignalSpy 只能证明"信号发了",证明不了"发的是对的参数"。
//   实测的真 bug 正是:removeVariable 一个信号都不发 —— Spy 看得见。
//   而这个探针模拟的是真实后果(手里那个指针变成悬空),对错都能断出来。
namespace {
class PointerHolder : public QObject {
public:
    explicit PointerHolder(VariableManager* mgr) {
        connect(mgr, &VariableManager::variableReplaced,
                this, [this](Variable* oldVar, Variable* newVar) {
            if (held_ == oldVar) held_ = newVar;
        });
    }
    void    grab(Variable* v) { held_ = v; }
    // ★ 延迟解引用 —— 这正是 onTick / onWriteReply 的形状
    bool    stillAlive() const { return held_ && !held_->isDerived(); }
    Variable* held_ = nullptr;
};
}   // namespace

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

// ★ D.5:按别名反查 — 不同 var 的别名不能重,空 alias 返 nullptr
void tst_VariableManager::findByAlias_returnsCorrectVariable() {
    VariableManager mgr;
    auto* speed = new Variable("Speed", 0, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    speed->addAlias("转速");
    speed->addAlias("RPM");
    auto* temp = new Variable("Temp", 1, Variable::DataType::Float, Variable::Access::ReadOnly, &mgr);
    temp->addAlias("温度");

    QVERIFY(mgr.addVariable(speed));
    QVERIFY(mgr.addVariable(temp));

    QCOMPARE(mgr.findByAlias("转速"), speed);
    QCOMPARE(mgr.findByAlias("RPM"), speed);
    QCOMPARE(mgr.findByAlias("温度"), temp);
    QCOMPARE(mgr.findByAlias("Speed"), nullptr);  // 别名不匹配 name_
    QCOMPARE(mgr.findByAlias(""), nullptr);
    QCOMPARE(mgr.findByAlias("nonexistent"), nullptr);
}

// ============================================================================
// ★ 改善清单 #6 / #7:删变量必须广播 variableReplaced(old, nullptr)
//
// 【为什么这条是真 bug,不是洁癖】
//   manager 删掉变量时,那些【把 Variable* 存起来、稍后再解引用】的模块
//   —— WriteScheduler::pending_、TrendWidget —— 手里的指针当场变悬空。
//   WriteScheduler::onWriteReply 会 `v->clearDirty()`:
//   写请求在途(几十~几百毫秒)时用户在界面上删了这个变量,
//   PLC 回包一到就是 use-after-free —— 崩,或者更糟:写进已释放内存。
//
// 【为什么它一直没被发现】
//   同名【替换】会 emit、clear() 也会 emit,唯独 removeVariable() 不发。
//   三个入口长得一样、两个会通知一个不会,读代码的人极容易以为都会。
//
//   ★ 更正(2026-10-10 当天晚些时候):原来这里写的是
//     "只有【用户在界面上删变量】才碰得到" —— 【那句话是错的】。
//     全仓查过:removeVariable() 除了本文件这些用例,没有任何生产调用方,
//     hmi.ui 里也没有删变量的入口。别照着这句话去找触发路径。
//     生产里真正删光变量的是 clear()(换工程),而它早就 emit 了。
//
// 【对照】clear() 早就做对了(2026-08-07),而且写清了理由。
//   removeVariable() 当时只是漏了 —— 现在补上,两个入口语义一致。
// ============================================================================

void tst_VariableManager::remove_emitsVariableReplaced_withNullNew() {
    VariableManager mgr;
    auto* v = new Variable("X", 0x0001, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QVERIFY(mgr.addVariable(v));

    PointerHolder holder(&mgr);
    holder.grab(v);

    QSignalSpy spy(&mgr, &VariableManager::variableReplaced);
    QVERIFY(mgr.removeVariable("X"));

    // ★ 参数必须逐个对:old 是被删的那个、new 是 nullptr。
    //   只断"发了"不够 —— 广播成 (nullptr, nullptr) 同样满足 count()==1。
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Variable*>(), v);
    QCOMPARE(spy.at(0).at(1).value<Variable*>(), nullptr);

    // ★ 真实后果:持有者必须被置空,而不是留一个悬空指针。
    QVERIFY2(holder.held_ == nullptr,
             "删变量后监听者手里还是悬空指针 —— onWriteReply 一到就是 use-after-free");
}

void tst_VariableManager::clear_emitsForEveryVariable() {
    VariableManager mgr;
    auto* a = new Variable("A", 1, Variable::DataType::Int16, Variable::Access::ReadOnly);
    auto* b = new Variable("B", 2, Variable::DataType::Int16, Variable::Access::ReadOnly);
    QVERIFY(mgr.addVariable(a));
    QVERIFY(mgr.addVariable(b));

    QSignalSpy spy(&mgr, &VariableManager::variableReplaced);
    mgr.clear();

    // ★ 一条都不能漏:漏掉的那一个,持有者就永远握着悬空指针。
    QCOMPARE(spy.count(), 2);
    for (int i = 0; i < spy.count(); ++i) {
        QVERIFY(spy.at(i).at(0).value<Variable*>() != nullptr);
        QCOMPARE(spy.at(i).at(1).value<Variable*>(), nullptr);
    }
}

void tst_VariableManager::remove_ofMissingName_doesNotEmit() {
    VariableManager mgr;
    QVERIFY(mgr.addVariable(new Variable("X", 1, Variable::DataType::Int16,
                                          Variable::Access::ReadOnly)));
    QSignalSpy spy(&mgr, &VariableManager::variableReplaced);
    QVERIFY(!mgr.removeVariable("NotExists"));
    // ★ 删一个不存在的东西,不能凭空广播一个 nullptr-old。
    //   否则监听者会把"本来就跟它无关"的指针也置空 ——
    //   而 onVariableReplaced 是按 oldVar 匹配的,所以这不至于崩,
    //   但它会让广播失去"谁被动了"这个信息,排查时极其误导。
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(tst_VariableManager)
#include "tst_VariableManager.moc"

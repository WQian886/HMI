// ============================================================================
// 阶段 E · E.4d-follow-8 · tst_GeometryUndo —— 对齐/等尺寸的 Undo
//
// 【这次修的什么】EditorMainWindow 的 9 个动作(6 对齐 + 3 等尺寸)原来
//   直接调 setPosition/setSize,零 Undo 包装 —— 用户按了左对齐,
//   Ctrl+Z 撤不回来。排版是反复试错的操作,没有撤销等于每按一次就永久改一次。
//
// 【测什么】三条,按重要性排:
//   ① undo 之后【所有】控件回到原位(不是"大部分"—— 半撤销比不撤销更迷惑)
//   ② 几何没变时不产生 Undo 步骤(否则 Ctrl+Z 会有"按了没反应"的空步骤)
//   ③ 连续同类动作合并成一条,不同类不合并
//
// 【为什么每个动作都要单独测 9 遍】merge 依赖 actionTag,快照/push 是
//   9 份各写各的代码 —— 只要有一处漏了快照或 push,那一个动作就撤不回来,
//   而这从代码上看不出来。参数化循环跑一遍能保证 9 个都被覆盖。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QUndoStack>
#include "editor/EditorMainWindow.h"
#include "editor/GeometryChangeCommand.h"
#include "widgets/LabelControl.h"
#include "widgets/RectangleControl.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestGeometryUndo : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ===== ① undo 完整性 =====
    void alignLeft_undoRestoresAll();
    void alignRight_undoRestoresAll();
    void alignTop_undoRestoresAll();
    void alignBottom_undoRestoresAll();
    void alignCenterH_undoRestoresAll();
    void alignCenterV_undoRestoresAll();
    void sameWidth_undoRestoresAll();
    void sameHeight_undoRestoresAll();
    void sameSize_undoRestoresAll();
    void undo_redo_roundTrips();
    void undo_multipleSteps_unwindsInOrder();

    // ===== ② 不产生空步骤 =====
    void noGeometryChange_pushesNothing();     // ★ 用户最烦的失败模式
    void singleSelection_pushesNothing();
    void undoStack_isEmptyAfterNoOpAlign();

    // ===== ③ 合并语义 =====
    void sameActionTwice_mergesIntoOne();
    void differentActions_doNotMerge();
    void merge_undoRestoresToTheEarliestState();   // ★ 合并方向写反的锁

    // ===== 命令类本身 =====
    void command_undoRedo_isIdempotentAcrossRepeats();
    void command_ignoresNullTargets();
};

// ===== 工具 =====

namespace {

// ★ 建一个 3 选中的窗口。3 个而不是 2 个:2 个控件时"一半撤销"和"全撤销"
//   肉眼可能分不清,3 个才看得出是不是全退了。
//
//   【存指针不存引用】EditorMainWindow 是 QMainWindow 派生,Qt 禁拷贝,
//   所以 Fixture 里只能存指针 —— 写成 EditorMainWindow& 会让 Fixture
//   变成不可拷贝的类型,`Fixture f{w, ...}` 直接编译失败(试图拷贝构造)。
struct Fixture {
    EditorMainWindow* w;
    LabelControl* a;
    LabelControl* b;
    LabelControl* c;
    QList<QPointF> origPos;
    QList<QSizeF>  origSize;
};

Fixture makeFixture(EditorMainWindow* w) {
    Fixture f{w, new LabelControl, new LabelControl, new LabelControl, {}, {}};
    f.a->setSize({50, 40}); f.a->setPosition({100, 100});
    f.b->setSize({80, 60}); f.b->setPosition({300, 200});
    f.c->setSize({40, 90}); f.c->setPosition({150, 350});
    w->addControl(f.a);
    w->addControl(f.b);
    w->addControl(f.c);

    f.origPos = {f.a->position(), f.b->position(), f.c->position()};
    f.origSize = {f.a->size(), f.b->size(), f.c->size()};

    // ★ 用 setSelected 逐个选,不能用 scene->setSelectedItems(那不是
    //   QGraphicsScene 的 API)。顺序按加入顺序:对齐的参考基准是
    //   targets_.first(),若真机上 Qt 给的 selectedItems() 顺序不保证
    //   (见 tst_EditorMainWindow.cpp:264 的注释),那 9 个动作的"参考是谁"
    //   本身就不确定 —— 但那是另一个问题(见下面的参考基准断言)。
    //   本文件只关心 undo 能不能完整还原,所以额外用 origPos 逐控件比对,
    //   不依赖谁当参考。
    w->setTarget(f.a);
    f.a->setSelected(true);
    f.b->setSelected(true);
    f.c->setSelected(true);
    // ★ 这里不能用 QVERIFY —— 它展开成 `return;`,而本函数返回 Fixture,
    //   编译期就报"函数必须返回值"。多选是否建起来由调用方断言。
    return f;
}

void checkAllRestored(const Fixture& f, const char* what) {
    // ★ 先确认 fixture 真的造出了多选,否则下面"全部还原"的断言是空的
    //   (targets_ 只有 1 个时,9 个动作本来就 return,撤销也无从谈起)
    QVERIFY2(f.w->hasMultipleSelection(), "fixture 没造出多选,断言无效");
    const QList<ControlBase*> cs = {f.a, f.b, f.c};
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(cs[i]->position() == f.origPos[i],
                 qPrintable(QStringLiteral("%1: 控件 %2 的位置没退回去(%3,%4),"
                                           "期望(%5,%6) —— 半撤销比不撤销更迷惑")
                                .arg(QLatin1String(what)).arg(i)
                                .arg(cs[i]->position().x()).arg(cs[i]->position().y())
                                .arg(f.origPos[i].x()).arg(f.origPos[i].y())));
        QVERIFY2(cs[i]->size() == f.origSize[i],
                 qPrintable(QStringLiteral("%1: 控件 %2 的尺寸没退回去").arg(QLatin1String(what)).arg(i)));
    }
}

} // namespace

// ===== ① 9 个动作的 undo 完整性 =====

void TestGeometryUndo::alignLeft_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignLeft();
    QVERIFY(w.undoStack()->count() > 0);
    w.undoStack()->undo();
    checkAllRestored(f, "alignLeft");
}
void TestGeometryUndo::alignRight_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignRight();
    w.undoStack()->undo();
    checkAllRestored(f, "alignRight");
}
void TestGeometryUndo::alignTop_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignTop();
    w.undoStack()->undo();
    checkAllRestored(f, "alignTop");
}
void TestGeometryUndo::alignBottom_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignBottom();
    w.undoStack()->undo();
    checkAllRestored(f, "alignBottom");
}
void TestGeometryUndo::alignCenterH_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignCenterH();
    w.undoStack()->undo();
    checkAllRestored(f, "alignCenterH");
}
void TestGeometryUndo::alignCenterV_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignCenterV();
    w.undoStack()->undo();
    checkAllRestored(f, "alignCenterV");
}
void TestGeometryUndo::sameWidth_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onSameWidth();
    w.undoStack()->undo();
    checkAllRestored(f, "sameWidth");
}
void TestGeometryUndo::sameHeight_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onSameHeight();
    w.undoStack()->undo();
    checkAllRestored(f, "sameHeight");
}
void TestGeometryUndo::sameSize_undoRestoresAll() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onSameSize();
    w.undoStack()->undo();
    checkAllRestored(f, "sameSize");
}

// ★ undo 再 redo 必须精确回到【对齐后】的状态,而不是"再执行一次对齐"
//   —— 后者对幂等操作看起来一样,但一旦对齐逻辑改了就对不上了
void TestGeometryUndo::undo_redo_roundTrips() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignLeft();
    const QList<QPointF> afterAlign = {f.a->position(), f.b->position(), f.c->position()};

    w.undoStack()->undo();
    checkAllRestored(f, "roundTrip-undo");

    w.undoStack()->redo();
    const QList<ControlBase*> cs = {f.a, f.b, f.c};
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(cs[i]->position() == afterAlign[i],
                 "redo 之后的位置跟对齐完那一刻不一致");
    }
}

void TestGeometryUndo::undo_multipleSteps_unwindsInOrder() {
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignLeft();
    w.onSameWidth();
    w.onAlignTop();
    QCOMPARE(w.undoStack()->count(), 3);

    w.undoStack()->undo();   // 撤顶对齐
    w.undoStack()->undo();   // 撤等宽
    w.undoStack()->undo();   // 撤左对齐
    checkAllRestored(f, "multiStep");
}

// ===== ② 空步骤 =====

// ★★ 本文件最要紧的一条。
//   3 个控件本来就在同一 x,按左对齐什么都不会发生。
//   如果这时候还往栈里 push 一条命令,用户按 Ctrl+Z 会发现:
//   画面一动不动,但栈里明明还有内容 —— 那种"按了没反应"比不能撤销更招人烦,
//   因为用户会以为程序卡了。所以 pushGeometryCommand 里比了前后快照,
//   没变化就不 push。
void TestGeometryUndo::noGeometryChange_pushesNothing() {
    EditorMainWindow w;
    LabelControl* a = new LabelControl;
    LabelControl* b = new LabelControl;
    a->setPosition({100, 100});
    b->setPosition({100, 300});   // ★ x 跟 a 一样 → 左对齐不会有任何变化
    w.addControl(a);
    w.addControl(b);
    w.setTarget(a);
    a->setSelected(true);
    b->setSelected(true);

    QCOMPARE(w.undoStack()->count(), 0);
    w.onAlignLeft();
    QVERIFY2(w.undoStack()->count() == 0,
             "几何没变却压了一条空命令 → Ctrl+Z 会变成'按了没反应'");
}

// 单选时动作本来就该直接返回,不该产生任何 Undo 步骤
void TestGeometryUndo::singleSelection_pushesNothing() {
    EditorMainWindow w;
    LabelControl* a = new LabelControl;
    a->setPosition({100, 100});
    w.addControl(a);
    w.setTarget(a);
    a->setSelected(true);

    w.onAlignLeft();
    w.onSameWidth();
    w.onSameSize();
    QCOMPARE(w.undoStack()->count(), 0);
}

void TestGeometryUndo::undoStack_isEmptyAfterNoOpAlign() {
    // 同上,换个角度验一遍:先来一次【有变化的】对齐,再夹一次无变化的,
    // 栈里应该只有 1 条,而不是 2 条
    EditorMainWindow w;
    auto f = makeFixture(&w);
    w.onAlignLeft();
    QCOMPARE(w.undoStack()->count(), 1);

    w.onAlignLeft();   // 已经对齐了,这次无变化
    QCOMPARE(w.undoStack()->count(), 1);
}

// ===== ③ 合并语义 =====

void TestGeometryUndo::sameActionTwice_mergesIntoOne() {
    EditorMainWindow w;
    makeFixture(&w);
    w.onAlignLeft();
    const int n1 = w.undoStack()->count();
    w.onAlignLeft();   // 第二次已经没变化了,不会 push —— 这条验的是"不会多出步骤"
    QCOMPARE(w.undoStack()->count(), n1);
}

// ★ 连续两个【不同】动作不能合成一步。
//   如果合成了,用户按左对齐再按等大,Ctrl+Z 一次会把两个操作一起退掉,
//   中间那个"只有对齐没等大"的状态就再也回不去了。
void TestGeometryUndo::differentActions_doNotMerge() {
    EditorMainWindow w;
    makeFixture(&w);
    w.onAlignLeft();
    w.onSameWidth();
    QCOMPARE(w.undoStack()->count(), 2);
}

// ★★ 合并方向写反的锁。
//   连续两次同类操作(这里直接构造命令,绕开"第二次无变化"的短路)合并后,
//   Ctrl+Z 一次必须退回到【最早那次之前】,而不是只退掉最后一次。
//   写反的表现:退一次之后右边还有 5 个没对齐,用户以为撤销没生效。
void TestGeometryUndo::merge_undoRestoresToTheEarliestState() {
    LabelControl a, b, c;
    a.setPosition({10, 10});
    b.setPosition({50, 20});
    c.setPosition({90, 30});
    ControlBase* cs[] = {&a, &b, &c};

    QHash<ControlBase*, QRectF> state0, state1, state2;
    for (int i = 0; i < 3; ++i) state0.insert(cs[i], QRectF(cs[i]->position(), cs[i]->size()));
    // 第一次动作:全移到 x=0
    for (auto* x : cs) x->setPosition(QPointF(0, x->position().y()));
    for (int i = 0; i < 3; ++i) state1.insert(cs[i], QRectF(cs[i]->position(), cs[i]->size()));
    // 第二次动作:全移到 x=0,y=0
    for (auto* x : cs) x->setPosition(QPointF(0, 0));
    for (int i = 0; i < 3; ++i) state2.insert(cs[i], QRectF(cs[i]->position(), cs[i]->size()));

    QUndoStack stack;
    stack.push(new GeometryChangeCommand(state0, state1, QStringLiteral("左对齐")));
    stack.push(new GeometryChangeCommand(state1, state2, QStringLiteral("左对齐")));

    QCOMPARE(stack.count(), 1);   // ★ 合并成一条了

    stack.undo();
    // ★ 必须退到 state0(最早),不是 state1(最后一次之前)
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(cs[i]->position() == state0[cs[i]].topLeft(),
                 qPrintable(QStringLiteral("控件 %1 退回到了 %2,应是最初的 %3 —— "
                                           "合并方向写反了(拿了最新的 old 覆盖)")
                                .arg(i).arg(cs[i]->position().x())
                                .arg(state0[cs[i]].topLeft().x())));
    }
}

// ===== 命令类本身 =====

// ★ 同一个命令对象被反复 undo/redo 不得自我污染。
//   这条锁的是"oldRects_/newRects_ 构造后不可变"这个设计约束 ——
//   如果有人改成持有"当前矩形"的可变字段,这里就会露馅。
void TestGeometryUndo::command_undoRedo_isIdempotentAcrossRepeats() {
    RectangleControl r;
    r.setPosition({10, 20});
    r.setSize({30, 40});

    QHash<ControlBase*, QRectF> before, after;
    before.insert(&r, QRectF(10, 20, 30, 40));
    after.insert(&r, QRectF(99, 88, 77, 66));

    GeometryChangeCommand cmd(before, after, QStringLiteral("test"));
    for (int i = 0; i < 5; ++i) {
        cmd.redo();
        QVERIFY(r.position() == QPointF(99, 88));
        QVERIFY(r.size() == QSizeF(77, 66));
        cmd.undo();
        QVERIFY(r.position() == QPointF(10, 20));
        QVERIFY(r.size() == QSizeF(30, 40));
    }
}

// ★ hash 里混了 nullptr 不该崩。控件被删掉后 Undo 栈里还留着它的记录是可能的
//   (用户删控件,再撤销删除 —— 顺序反过来时快照里就有失效指针)
void TestGeometryUndo::command_ignoresNullTargets() {
    QHash<ControlBase*, QRectF> before, after;
    before.insert(nullptr, QRectF(0, 0, 1, 1));
    after.insert(nullptr, QRectF(5, 5, 1, 1));
    RectangleControl r;
    before.insert(&r, QRectF(0, 0, 10, 10));
    after.insert(&r, QRectF(50, 50, 10, 10));

    GeometryChangeCommand cmd(before, after, QStringLiteral("test"));
    cmd.redo();   // 不该崩
    QVERIFY(r.position() == QPointF(50, 50));
    cmd.undo();
    QVERIFY(r.position() == QPointF(0, 0));
}

QTEST_MAIN(TestGeometryUndo)
#include "tst_GeometryUndo.moc"

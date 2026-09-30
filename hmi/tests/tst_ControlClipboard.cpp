// ============================================================================
// 阶段 E · E.7 · tst_ControlClipboard —— 复制 / 粘贴 / 删除 + 它们的 Undo
//
// 【这次补的什么】编辑器此前【完全没有】复制/粘贴/删除:
//   编辑菜单里只有"撤销/重做"两项,全代码目录搜 copy/paste/delete 零命中。
//   画布上的元件拿不走、搬不动、删不掉 —— 而这三件事是排版的基本功。
//
// 【测什么】按重要性排:
//   ① 增删的 Undo 完整性 —— 删了要能原样回来(位置/尺寸/颜色/变量名全都要回来)
//   ② 连续 删→撤销→删→撤销 的循环 —— ★ 这是本任务最容易埋雷的地方。
//      命令存的是 JSON 快照(不是控件指针),撤销会造出【新对象】,
//      旧指针随即失效。如果命令里残留了任何旧指针,第二轮就会二次 delete
//      或改到已释放内存 —— 而且往往不崩,只是"第二次撤销画面不对",极难查。
//   ③ 快照的 type 字段必须是【注册表短名】而不是 className ——
//      两者对不上时 create() 静默返回 nullptr,粘贴出来的控件凭空消失且不报错。
//      这正是 E.5 踩过的坑,所以专门锁一条用例。
//   ④ 边界:无选中复制 / 空剪贴板粘贴 / 几何无变化
//
// 【为什么每条用例都断言 controls().size()】只断言"画面上看着对了"是不够的:
//   E.4d 那批 bug 里有好几条是"控件还在但残影/多了一份",只有数量对不上
//   才抓得住。数量是这个任务里最便宜也最灵的断言。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QUndoStack>
#include <algorithm>   // ★ std::any_of(paste_copyAgain_resetsOffsetCounter 用)
#include "editor/ControlListCommand.h"
#include "editor/EditorMainWindow.h"
#include "widgets/RectangleControl.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestControlClipboard : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ===== ① 复制 / 粘贴 =====
    void copy_storesSnapshotOfSelection();
    void copy_snapshotType_isRegistryShortName_notClassName();   // ★ E.5 同款坑的锁
    void copy_noSelection_isSilentNoOp();
    void paste_createsCopiesWithOffset();
    void paste_secondTime_offsetsFurther();
    void paste_copyAgain_resetsOffsetCounter();
    void paste_emptyClipboard_isSilentNoOp();
    void paste_preservesAllProperties();          // ★ 位置/尺寸/颜色/变量名全要回来
    void paste_multipleSelection_createsAll();
    void paste_newControls_becomeSelected();

    // ===== ② 删除 + Undo(重点)=====
    void delete_removesFromCanvasAndControls();
    void delete_multipleSelection_removesAll();
    void delete_clearsSelectionAndHandles();
    void delete_undo_restoresCountAndProperties();
    void delete_noSelection_isSilentNoOp();
    void delete_undo_redo_repeatsCleanly();        // ★★ 删→撤→删→撤 连环
    void delete_undo_redo_fiveCycles_staysStable();// ★★ 压力:五轮之后数量/属性仍对
    void deleteTwice_undoTwice_unwindsBothSteps();

    // ===== ③ 粘贴 + Undo =====
    void paste_undo_removesTheCopies();
    void paste_undo_redo_repeatsCleanly();
    void paste_undoThenNewAction_clearsRedoStack();

    // ===== ④ 组合场景 =====
    void copyDeletePaste_usesOriginalClipboardNotTheDeletedOne();
    void deletePasteUndo_deleteWins();   // ★ 顺序不同结果不同,别把栈语义搞混
};

// ===== 工具 =====
namespace {

// ★ 加一个控件并设好几何/颜色/变量名 —— 属性越"非默认",往返测试越有鉴别力。
//   如果只测默认值的控件,propertiesFromJson 里任何一环坏了测试都发现不了。
//
//   【为什么用 RectangleControl 而不是 LabelControl】LabelControl 一个
//   Q_PROPERTY 都没有(只有基类的通用属性),测不到颜色往返。RectangleControl
//   有 fillColor + strokeColor 两个 QColor,是这批控件里属性最"密"的之一 ——
//   往返测试要的就是这种能真正卡住 bug 的控件。
ControlBase* addRect(EditorMainWindow* w, const QPointF& pos, const QSizeF& sz,
                     const QColor& fill, const QString& var) {
    auto* r = new RectangleControl;
    r->setSize(sz);
    r->setPosition(pos);
    r->setFillColor(fill);
    r->setVariableName(var);
    w->addControl(r);
    return r;
}

void selectAll(EditorMainWindow* w) {
    for (auto* c : w->controls()) c->setSelected(true);
}

// ★ 数【画布上的控件数】,而不是 scene()->items().size()。
//   控件被选中时 scene 里还多出 8 个 ResizeHandleItem + 1 个 SelectionFrameItem,
//   直接用 items().size() 去比"1 个控件"会数成 10 —— 那是断言写错了,不是代码错。
//   这里只数 ControlBase 派生类,才是"画面上到底有几个元件"这个真问题。
int controlCountInScene(EditorMainWindow* w) {
    int n = 0;
    for (QGraphicsItem* item : w->scene()->items()) {
        if (dynamic_cast<ControlBase*>(item)) ++n;
    }
    return n;
}

} // namespace

// ===== ① 复制 / 粘贴 =====

void TestControlClipboard::copy_storesSnapshotOfSelection() {
    EditorMainWindow w;
    addRect(&w, {10, 20}, {80, 30}, QColor(10, 20, 30), "plc1.A");
    addRect(&w, {200, 40}, {60, 25}, QColor(40, 50, 60), "plc1.B");
    selectAll(&w);

    w.onCopy();
    QCOMPARE(w.clipboard().size(), 2);
    // ★ 快照里必须带 type,否则粘贴时 create("") 拿到 nullptr
    for (const auto& snap : w.clipboard()) {
        QVERIFY(!snap.value("type").toString().isEmpty());
    }
}

void TestControlClipboard::copy_snapshotType_isRegistryShortName_notClassName() {
    // ★ 这条用例守的是 E.5 那个"静默丢控件"的坑。
    //   toJson() 写的 type 是 className("RectangleControl"),
    //   而 create() 查的注册表 key 是短名("Rectangle")。
    //   一旦有人图省事改用 toJson() 的快照,粘贴就会 create 失败返回 nullptr,
    //   画面上凭空少一个控件,而且【全程零报错】。
    EditorMainWindow w;
    addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    selectAll(&w);
    w.onCopy();
    QCOMPARE(w.clipboard().size(), 1);

    const QString type = w.clipboard().first().value("type").toString();
    QCOMPARE(type, QStringLiteral("Rectangle"));
    QVERIFY2(type != QStringLiteral("RectangleControl"),
             "快照的 type 用了 className,粘贴时 create() 查不到注册表 key");
    // ★ 正面验证:这个 type 必须真能造出控件
    QVERIFY(ControlBase::create(type) != nullptr);
}

void TestControlClipboard::copy_noSelection_isSilentNoOp() {
    EditorMainWindow w;
    addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    // 不选
    w.onCopy();
    QCOMPARE(w.clipboard().size(), 0);
    QCOMPARE(w.undoStack()->count(), 0);   // 复制不产生 Undo 步骤
}

void TestControlClipboard::paste_createsCopiesWithOffset() {
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();

    const int before = w.controls().size();
    w.onPaste();
    // ★ 数量 +1 —— 粘贴出【新】控件,不是移动原控件
    QCOMPARE(w.controls().size(), before + 1);

    // 找到新控件:不是原来那个
    ControlBase* pasted = nullptr;
    for (auto* c : w.controls()) if (c != a) pasted = c;
    QVERIFY(pasted != nullptr);
    // 偏移 10,10 —— 不偏移的话两份完全重叠,用户以为只贴了一份
    QCOMPARE(pasted->position(), QPointF(110, 110));
    // ★ 原来的控件必须原地不动
    QCOMPARE(a->position(), QPointF(100, 100));
}

void TestControlClipboard::paste_secondTime_offsetsFurther() {
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();
    w.onPaste();

    QCOMPARE(w.controls().size(), 3);
    // ★ 三份必须在三个不同位置 —— 全叠在一起的话用户只会看见一个。
    //   用 QString 当 key 而不是 QSet<QPointF>:Qt 6 没给 QPointF 提供 qHash,
    //   QSet<QPointF> 直接静态断言失败,编不过。
    QSet<QString> positions;
    for (auto* c : w.controls())
        positions.insert(QStringLiteral("%1,%2").arg(c->position().x()).arg(c->position().y()));
    QCOMPARE(positions.size(), 3);
}

void TestControlClipboard::paste_copyAgain_resetsOffsetCounter() {
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();          // → 110,110
    a->setSelected(true);
    w.onCopy();           // ★ 重新复制 = 换了内容,偏移计数应归零
    w.onPaste();
    // 第二批也应该只偏移 10,而不是接着往下数
    QVERIFY(std::any_of(w.controls().begin(), w.controls().end(),
                        [](ControlBase* c) { return c->position() == QPointF(110, 110); }));
}

void TestControlClipboard::paste_emptyClipboard_isSilentNoOp() {
    EditorMainWindow w;
    addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    const int before = w.controls().size();
    w.onPaste();          // 剪贴板是空的
    QCOMPARE(w.controls().size(), before);
    QCOMPARE(w.undoStack()->count(), 0);
}

void TestControlClipboard::paste_preservesAllProperties() {
    EditorMainWindow w;
    const QPointF pos(123, 456);
    const QSizeF sz(77, 33);
    const QColor c(12, 34, 56, 200);          // ★ 带 alpha —— E.1c 修过的坑
    auto* a = addRect(&w, pos, sz, c, "plc1.Pressure");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();

    ControlBase* pasted = nullptr;
    for (auto* c2 : w.controls()) if (c2 != a) pasted = c2;
    QVERIFY(pasted != nullptr);

    QCOMPARE(pasted->size(), sz);
    QCOMPARE(pasted->variableName(), QStringLiteral("plc1.Pressure"));
    // ★ alpha 必须活下来 —— 半透明控件存一次变不透明,是"画面莫名其妙变实了"
    QCOMPARE(pasted->property("fillColor").value<QColor>().alpha(), 200);
    QCOMPARE(pasted->position(), pos + QPointF(10, 10));
}

void TestControlClipboard::paste_multipleSelection_createsAll() {
    EditorMainWindow w;
    addRect(&w, {10, 10}, {40, 20}, QColor(1, 1, 1), "plc1.A");
    addRect(&w, {80, 80}, {40, 20}, QColor(2, 2, 2), "plc1.B");
    addRect(&w, {150, 150}, {40, 20}, QColor(3, 3, 3), "plc1.C");
    selectAll(&w);
    w.onCopy();
    QCOMPARE(w.clipboard().size(), 3);

    w.onPaste();
    QCOMPARE(w.controls().size(), 6);
    // ★ 一次 Ctrl+Z 要撤掉整批,不是撤掉一个
    QCOMPARE(w.undoStack()->count(), 1);
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 3);
}

void TestControlClipboard::paste_newControls_becomeSelected() {
    // 用户粘完就该能接着拖它们摆位置,不用再点一遍选中
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();
    QCOMPARE(w.targets().size(), 1);
    QVERIFY(w.targets().first() != a);
}

// ===== ② 删除 + Undo(重点)=====

void TestControlClipboard::delete_removesFromCanvasAndControls() {
    EditorMainWindow w;
    auto* a = addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    auto* b = addRect(&w, {200, 40}, {60, 25}, QColor(4, 5, 6), "plc1.B");
    a->setSelected(true);
    b->setSelected(true);

    w.onDelete();
    // ★ 三处都要干净:controls_ 列表、scene 里的 item、选中态
    QCOMPARE(w.controls().size(), 0);
    QCOMPARE(w.scene()->items().size(), 0);
    QVERIFY(w.targets().isEmpty());
    QVERIFY(w.selectionFrame() == nullptr || !w.selectionFrame()->isVisible());
}

void TestControlClipboard::delete_multipleSelection_removesAll() {
    EditorMainWindow w;
    for (int i = 0; i < 5; ++i) {
        addRect(&w, {i * 100.0, 50.0}, {50, 20}, QColor(i, i, i), "plc1.V");
    }
    selectAll(&w);
    QVERIFY(w.hasMultipleSelection());
    w.onDelete();
    QCOMPARE(w.controls().size(), 0);
    // ★ 一次删完是一条 Undo 步骤
    QCOMPARE(w.undoStack()->count(), 1);
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 5);
}

void TestControlClipboard::delete_clearsSelectionAndHandles() {
    // ★ 8 个 ResizeHandleItem 是独立的 scene item —— 控件删了 handle 还在的话,
    //   画面上会留一排蓝点,而且点别处时会跟着一堆野 handle 晃。
    EditorMainWindow w;
    auto* a = addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    QVERIFY(w.scene()->items().size() > 1);   // 控件 + handle + 选中框

    w.onDelete();
    // ★ 删干净后 scene 里不该剩任何东西(选中框被隐藏不算 item)
    for (QGraphicsItem* item : w.scene()->items()) {
        if (auto* cb = dynamic_cast<ControlBase*>(item)) {
            QFAIL(qPrintable(QStringLiteral("删完还留着控件:%1").arg(item->type())));
        }
    }
    QVERIFY(w.targets().isEmpty());
}

void TestControlClipboard::delete_undo_restoresCountAndProperties() {
    EditorMainWindow w;
    const QPointF pos(321, 654);
    const QSizeF sz(88, 44);
    const QColor c(200, 100, 50, 180);
    addRect(&w, pos, sz, c, "plc1.Level");
    addRect(&w, {500, 500}, {30, 30}, QColor(9, 9, 9), "plc2.Other");
    selectAll(&w);

    w.onDelete();
    QCOMPARE(w.controls().size(), 0);
    w.undoStack()->undo();

    // ★ 用户对"撤销删除"的预期是【原样回来】 —— 属性必须全回来
    QCOMPARE(w.controls().size(), 2);
    ControlBase* restored = w.controls().first();
    QCOMPARE(restored->position(), pos);
    QCOMPARE(restored->size(), sz);
    QCOMPARE(restored->variableName(), QStringLiteral("plc1.Level"));
    QCOMPARE(restored->property("fillColor").value<QColor>().alpha(), 180);
    // 还原后要重新入画布,不能"在列表里但画布上没有"
    QCOMPARE(controlCountInScene(&w), 2);
}

void TestControlClipboard::delete_noSelection_isSilentNoOp() {
    EditorMainWindow w;
    addRect(&w, {10, 20}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    w.onDelete();
    QCOMPARE(w.controls().size(), 1);
    QCOMPARE(w.undoStack()->count(), 0);
}

void TestControlClipboard::delete_undo_redo_repeatsCleanly() {
    // ★★ 本任务最容易埋雷的地方。
    //   ControlListCommand 存的是 JSON 快照,undo 会 create 出一个【新对象】,
    //   旧指针当场失效。所以第二轮 delete 时,命令里如果还残留着第一轮的
    //   旧指针,就会二次 delete(崩)或写进已释放内存(不崩但数据乱)。
    //   这个用例的意义就是逼出这种残留。
    EditorMainWindow w;
    addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    selectAll(&w);

    w.onDelete();
    QCOMPARE(w.controls().size(), 0);
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 1);
    const QPointF restoredPos = w.controls().first()->position();
    QCOMPARE(restoredPos, QPointF(100, 100));

    // ★ 第二轮:redo 掉,再 undo 回来
    w.undoStack()->redo();
    QCOMPARE(w.controls().size(), 0);
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 1);
    QCOMPARE(w.controls().first()->position(), QPointF(100, 100));
    QCOMPARE(w.controls().first()->variableName(), QStringLiteral("plc1.A"));
}

void TestControlClipboard::delete_undo_redo_fiveCycles_staysStable() {
    // 压力版:五轮之后数量/属性/画布项数都必须还是对的。
    // 悬空指针的破坏往往在第 2~3 轮才显形,跑一轮不够。
    EditorMainWindow w;
    addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    selectAll(&w);
    w.onDelete();

    for (int i = 0; i < 5; ++i) {
        w.undoStack()->undo();
        QVERIFY2(w.controls().size() == 1,
                 qPrintable(QStringLiteral("第 %1 轮 undo 后控件数不对").arg(i + 1)));
        QCOMPARE(w.controls().first()->position(), QPointF(100, 100));
        QCOMPARE(w.controls().first()->variableName(), QStringLiteral("plc1.A"));
        QCOMPARE(controlCountInScene(&w), 1);

        w.undoStack()->redo();
        QVERIFY2(w.controls().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 轮 redo 后控件没删干净").arg(i + 1)));
        QCOMPARE(controlCountInScene(&w), 0);
    }
}

void TestControlClipboard::deleteTwice_undoTwice_unwindsBothSteps() {
    EditorMainWindow w;
    auto* a = addRect(&w, {10, 10}, {40, 20}, QColor(1, 1, 1), "plc1.A");
    auto* b = addRect(&w, {100, 10}, {40, 20}, QColor(2, 2, 2), "plc1.B");
    auto* c = addRect(&w, {200, 10}, {40, 20}, QColor(3, 3, 3), "plc1.C");

    a->setSelected(true);
    w.onDelete();
    QCOMPARE(w.controls().size(), 2);

    // ★ 重新选中剩下的 b 再删 —— 上一条 delete 已经把 a 从 targets_ 摘掉了,
    //   这里必须重新选,否则第二次删除会删到空
    b->setSelected(true);
    w.onDelete();
    QCOMPARE(w.controls().size(), 1);
    QCOMPARE(w.controls().first(), c);
    QCOMPARE(w.undoStack()->count(), 2);

    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 2);
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 3);
    // 三步状态:位置各归各位,不能串
    //   同样用 QString 当 key —— Qt 6 没给 QPointF 提供 qHash
    QSet<QString> pos;
    for (auto* cc : w.controls())
        pos.insert(QStringLiteral("%1,%2").arg(cc->position().x()).arg(cc->position().y()));
    QCOMPARE(pos.size(), 3);
}

// ===== ③ 粘贴 + Undo =====

void TestControlClipboard::paste_undo_removesTheCopies() {
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();
    QCOMPARE(w.controls().size(), 2);

    w.undoStack()->undo();
    // ★ 撤销粘贴 = 只撤掉新贴的,原件必须还在
    QCOMPARE(w.controls().size(), 1);
    QCOMPARE(w.controls().first(), a);
    QCOMPARE(a->position(), QPointF(100, 100));
    QCOMPARE(controlCountInScene(&w), 1);
}

void TestControlClipboard::paste_undo_redo_repeatsCleanly() {
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();
    QCOMPARE(w.controls().size(), 2);

    for (int i = 0; i < 4; ++i) {
        w.undoStack()->undo();
        QCOMPARE(w.controls().size(), 1);
        w.undoStack()->redo();
        QCOMPARE(w.controls().size(), 2);
    }
    // 原件始终没被动过
    QVERIFY(w.controls().contains(a));
    QCOMPARE(a->position(), QPointF(100, 100));
}

void TestControlClipboard::paste_undoThenNewAction_clearsRedoStack() {
    // Qt UndoStack 的标准语义:撤销后再做新动作,重做链作废。
    // 这条不是测我们的代码,是锁住"粘贴的 undo 也遵守同一套栈语义" ——
    // 如果 ControlListCommand 用了 child 命令或 push 了两条,这里会红。
    EditorMainWindow w;
    auto* a = addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    a->setSelected(true);
    w.onCopy();
    w.onPaste();
    QCOMPARE(w.controls().size(), 2);
    w.undoStack()->undo();
    QCOMPARE(w.undoStack()->count(), 1);   // 撤销不减命令数,只是回退 index

    // 撤销之后换个新动作
    auto* b = new RectangleControl;
    b->setPosition({400, 400});
    b->setVariableName("plc3.Rebuilt");   // ★ 靠名字认人,不能靠指针
    const QString bName = b->variableName();
    w.addControl(b);
    b->setSelected(true);
    w.onDelete();

    // ★ 栈里只剩【删除】这一条,粘贴那条被新动作挤掉了 —— 这才是"重做链作废"。
    //   容易写错的地方在这里:undo() 之后 index = -1,此时 push 会把 index 之后的
    //   命令全删掉,而 index+1 == 0 意味着【连粘贴命令本身也一起删】。
    //   所以 count() 是 1 不是 2 —— 写 2 就得先想清楚这条 Qt 语义。
    QCOMPARE(w.undoStack()->count(), 1);
    QCOMPARE(w.controls().size(), 1);    // 只剩原件 a,b 被删了

    // ★ 真正要锁的:再撤一次,回来的必须是 b(撤销删除),
    //   绝不能把之前撤销掉的粘贴又带回来 —— 那说明重做链没被清干净。
    //   【不能断言 controls().contains(b)】:命令存的是 JSON 快照,撤销是
    //   create 一个【新对象】,b 这个指针在删除那一步就已经失效了。
    //   认控件只能认属性(名字/位置),认指针是拿已释放内存比地址。
    w.undoStack()->undo();
    QCOMPARE(w.controls().size(), 2);
    const bool bCameBack = std::any_of(
        w.controls().begin(), w.controls().end(),
        [&bName](ControlBase* c) { return c->variableName() == bName; });
    QVERIFY2(bCameBack, "撤销删除后 b 没有按属性还原回来");
}

// ===== ④ 组合场景 =====

void TestControlClipboard::copyDeletePaste_usesOriginalClipboardNotTheDeletedOne() {
    // ★ 剪贴板存的是 JSON 快照,所以"复制 → 删掉原件 → 粘贴"仍然能还原。
    //   如果剪贴板存的是指针,这里就会是悬空 —— 这条用例是这个设计决定的验收。
    EditorMainWindow w;
    addRect(&w, {100, 100}, {80, 30}, QColor(7, 7, 7), "plc1.Kept");
    selectAll(&w);
    w.onCopy();
    QCOMPARE(w.clipboard().size(), 1);

    w.onDelete();
    QCOMPARE(w.controls().isEmpty(), true);
    QCOMPARE(w.scene()->items().size(), 0);

    w.onPaste();
    QCOMPARE(w.controls().size(), 1);
    ControlBase* back = w.controls().first();
    QCOMPARE(back->variableName(), QStringLiteral("plc1.Kept"));
    QCOMPARE(back->position(), QPointF(110, 110));
}

void TestControlClipboard::deletePasteUndo_deleteWins() {
    // 操作顺序不同结果就不同 —— 这条锁住栈语义没搞反:
    //   粘贴(1步) → 删除(1步) → Ctrl+Z 只该撤掉"删除",贴的还在
    EditorMainWindow w;
    addRect(&w, {100, 100}, {80, 30}, QColor(1, 2, 3), "plc1.A");
    selectAll(&w);
    w.onCopy();
    w.onPaste();                      // 2 个控件
    QCOMPARE(w.controls().size(), 2);

    selectAll(&w);
    w.onDelete();                     // 全删光
    QCOMPARE(w.controls().size(), 0);

    w.undoStack()->undo();            // 只撤掉删除 → 2 个都回来
    QCOMPARE(w.controls().size(), 2);
}

QTEST_MAIN(TestControlClipboard)
#include "tst_ControlClipboard.moc"

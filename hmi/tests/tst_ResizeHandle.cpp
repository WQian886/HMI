// ============================================================================
// tst_ResizeHandle — E.4b 8 handle 缩放 + Shift 等比 + 最小尺寸 6 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>

#include "widgets/AlarmListControl.h"
#include "widgets/BarControl.h"
#include "widgets/ButtonControl.h"
#include "widgets/GaugeControl.h"
#include "widgets/GroupControl.h"
#include "widgets/ImageControl.h"
#include "widgets/InputEditControl.h"
#include "widgets/LabelControl.h"
#include "widgets/LedControl.h"
#include "widgets/LineControl.h"
#include "widgets/NumberDisplay.h"
#include "widgets/RectangleControl.h"
#include "widgets/SliderControl.h"
#include "widgets/TableControl.h"
#include "widgets/TextDisplay.h"
#include "widgets/ToggleControl.h"
#include "widgets/TrendControl.h"
#include "widgets/base/ControlBase.h"
#include "editor/ResizeHandleItem.h"
#include "editor/EditorMainWindow.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestResizeHandle : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // 6 用例
    void constructor_8handlesPerItem();        // 选中 → 8 个 handle
    void dragCornerBR_resizesCorrectly();       // 拖 BR → 两方向变大
    void dragEdgeL_onlyChangesWidth();          // 拖 L 边 → 只改宽
    void shiftDrag_maintainsAspect();           // Shift + 拖角 → 保持纵横比
    void minSizeConstraint_enforces20x20();     // 拖太小 → 限制 20
    void handlesRemovedOnDeselect();            // 取消选中 → handle 删掉
};

// 注册所有控件(EditorMainWindow + tests 需要)
namespace {
void registerAllForTest() {
    using namespace qwhmi::widgets;
    delete new LabelControl;       delete new NumberDisplay;    delete new TextDisplay;
    delete new BarControl;         delete new GaugeControl;     delete new LedControl;
    delete new ImageControl;       delete new RectangleControl; delete new LineControl;
    delete new GroupControl;       delete new ButtonControl;    delete new ToggleControl;
    delete new SliderControl;      delete new InputEditControl;
    delete new TrendControl;       delete new TableControl;     delete new AlarmListControl;
}
}

void TestResizeHandle::initTestCase() {
    registerAllForTest();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInfo() << "[tst_ResizeHandle] init";
}
void TestResizeHandle::cleanupTestCase() {
    qInfo() << "[tst_ResizeHandle] cleanup";
}

// 工具:数 child 中的 ResizeHandleItem 数量
static int countHandleChildren(ControlBase* c) {
    int n = 0;
    for (QGraphicsItem* ch : c->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(ch)) ++n;
    }
    return n;
}

// 工具:按 HandleType 找对应 handle
static ResizeHandleItem* findHandle(ControlBase* c, HandleType type) {
    for (QGraphicsItem* ch : c->childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(ch);
        if (h && h->handleType() == type) return h;
    }
    return nullptr;
}

// ===== 1. constructor_8handlesPerItem =====
void TestResizeHandle::constructor_8handlesPerItem() {
    EditorMainWindow emw;
    auto* bar = new BarControl;
    emw.handlePaletteDrop("Bar", QPointF(0, 0));   // 加一个 BarControl
    QCOMPARE(emw.controls().size(), 1);

    // 选中前:无 handle
    QCOMPARE(countHandleChildren(emw.controls().first()), 0);

    // 选中后:8 handle
    emw.setTarget(emw.controls().first());
    QCOMPARE(countHandleChildren(emw.controls().first()), 8);
}

// ===== 2. dragCornerBR_resizesCorrectly =====
void TestResizeHandle::dragCornerBR_resizesCorrectly() {
    BarControl bar;
    bar.setSize(QSizeF(100, 50));
    ResizeHandleItem::installHandlesFor(&bar);   // ★ 直接装,绕过 EditorMainWindow

    auto* h = findHandle(&bar, HandleType::BottomRight);
    QVERIFY(h != nullptr);

    // ★ 用 public 测试入口(绕过 protected QGraphicsItem::mouse*Event)
    QPointF pressPos = bar.scenePos() + QPointF(100, 50);    // BR handle 在 scene 的位置
    h->handleMousePress(pressPos);

    QPointF movePos = pressPos + QPointF(30, 20);
    h->handleMouseMove(movePos, Qt::NoModifier);

    QCOMPARE(bar.size().width(),  100 + 30);    // w 增 30
    QCOMPARE(bar.size().height(),  50 + 20);    // h 增 20
    QCOMPARE(bar.pos(), QPointF(0, 0));         // pos 不变(BR 角)

    h->handleMouseRelease(movePos);
}

// ===== 3. dragEdgeL_onlyChangesWidth =====
void TestResizeHandle::dragEdgeL_onlyChangesWidth() {
    BarControl bar;
    bar.setSize(QSizeF(100, 50));
    bar.setPos(QPointF(10, 20));
    ResizeHandleItem::installHandlesFor(&bar);

    auto* h = findHandle(&bar, HandleType::Left);
    QVERIFY(h != nullptr);

    QPointF pressPos = bar.scenePos() + QPointF(0, 25);    // L 边中点
    h->handleMousePress(pressPos);

    QPointF movePos = pressPos + QPointF(20, 0);
    h->handleMouseMove(movePos, Qt::NoModifier);

    QCOMPARE(bar.size().width(),  100 - 20);
    QCOMPARE(bar.size().height(),  50);
    QCOMPARE(bar.pos().x(),       10 + 20);

    h->handleMouseRelease(movePos);
}

// ===== 4. shiftDrag_maintainsAspect =====
void TestResizeHandle::shiftDrag_maintainsAspect() {
    BarControl bar;
    bar.setSize(QSizeF(100, 50));   // aspect = 2.0
    ResizeHandleItem::installHandlesFor(&bar);

    auto* h = findHandle(&bar, HandleType::BottomRight);
    QVERIFY(h != nullptr);

    QPointF pressPos = bar.scenePos() + QPointF(100, 50);
    h->handleMousePress(pressPos);

    QPointF movePos = pressPos + QPointF(40, 5);   // delta.x > delta.y
    h->handleMouseMove(movePos, Qt::ShiftModifier);   // ★ Shift

    // ★ Shift:以较大变化(width +40)为主轴,height 按 2.0 比例算 = 20
    QCOMPARE(bar.size().width(),  100 + 40);
    QCOMPARE(bar.size().height(), (100 + 40) / 2.0);

    h->handleMouseRelease(movePos);
}

// ===== 5. minSizeConstraint_enforces20x20 =====
void TestResizeHandle::minSizeConstraint_enforces20x20() {
    BarControl bar;
    bar.setSize(QSizeF(100, 50));
    ResizeHandleItem::installHandlesFor(&bar);

    auto* h = findHandle(&bar, HandleType::BottomRight);
    QVERIFY(h != nullptr);

    QPointF pressPos = bar.scenePos() + QPointF(100, 50);
    h->handleMousePress(pressPos);

    // ★ 拖反向(让控件变很小)
    QPointF movePos = pressPos + QPointF(-1000, -1000);
    h->handleMouseMove(movePos, Qt::NoModifier);

    QVERIFY(bar.size().width()  >= ResizeHandleItem::MIN_SIZE);
    QVERIFY(bar.size().height() >= ResizeHandleItem::MIN_SIZE);
    QCOMPARE(bar.size().width(),  ResizeHandleItem::MIN_SIZE);
    QCOMPARE(bar.size().height(), ResizeHandleItem::MIN_SIZE);

    h->handleMouseRelease(movePos);
}

// ===== 6. handlesRemovedOnDeselect =====
void TestResizeHandle::handlesRemovedOnDeselect() {
    EditorMainWindow emw;
    emw.handlePaletteDrop("Bar", QPointF(0, 0));
    emw.setTarget(emw.controls().first());
    QCOMPARE(countHandleChildren(emw.controls().first()), 8);

    emw.setTarget(nullptr);          // 取消选中
    QCOMPARE(countHandleChildren(emw.controls().first()), 0);
}

QTEST_MAIN(TestResizeHandle)
#include "tst_ResizeHandle.moc"

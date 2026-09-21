// ============================================================================
// tst_ResizeHandle — E.4b 8 handle 缩放 + Shift 等比 + 最小尺寸 6 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsProxyWidget>
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

    // 7 用例
    void constructor_8handlesPerItem();        // 选中 → 8 个 handle
    void dragCornerBR_resizesCorrectly();       // 拖 BR → 两方向变大
    void dragEdgeL_onlyChangesWidth();          // 拖 L 边 → 只改宽
    void shiftDrag_maintainsAspect();           // Shift + 拖角 → 保持纵横比
    void minSizeConstraint_enforces20x20();     // 拖太小 → 限制 20
    void handlesRemovedOnDeselect();            // 取消选中 → handle 删掉
    void proxyWidgetSyncs_resizeWithControl();   // TrendControl/AlarmListControl 缩放跟 widget
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

// ★ 工具:把 TrendControl 内部 proxy widget 找出来,验证它的 minSize 跟 ControlBase 一致
namespace {
QWidget* findProxyWidget(ControlBase* c) {
    for (QGraphicsItem* ch : c->childItems()) {
        if (auto* proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch)) {
            return proxy->widget();      // ★ QGraphicsProxyWidget::widget() 拿到被包的真实 QWidget
        }
    }
    return nullptr;
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

// ===== 7. proxyWidgetSyncs_resizeWithControl =====
//   修复:TrendControl/AlarmListControl 用 QGraphicsProxyWidget 包真实 widget,
//   缩放后内部 widget 也要跟变(否则 visible 区域错位)
void TestResizeHandle::proxyWidgetSyncs_resizeWithControl() {
    TrendControl trend;
    trend.setSize(QSizeF(280, 100));
    ResizeHandleItem::installHandlesFor(&trend);

    // ★ 缩放前:proxy widget 的 minSize 应是 (280, 100)
    QWidget* proxy = findProxyWidget(&trend);
    QVERIFY(proxy != nullptr);
    QCOMPARE(proxy->minimumSize().width(),  280);
    QCOMPARE(proxy->minimumSize().height(), 100);

    // ★ 模拟拖 BR handle (size +30, +20)
    auto* h = findHandle(&trend, HandleType::BottomRight);
    QVERIFY(h != nullptr);
    h->handleMousePress(trend.scenePos() + QPointF(280, 100));
    h->handleMouseMove(trend.scenePos() + QPointF(310, 120), Qt::NoModifier);

    // ★ ControlBase size 应是 310x120
    QCOMPARE(trend.size().width(),  310);
    QCOMPARE(trend.size().height(), 120);
    // ★ 修复点:proxy widget minSize 也跟变(否则 visible 区域错位)
    QCOMPARE(proxy->minimumSize().width(),  310);
    QCOMPARE(proxy->minimumSize().height(), 120);
}

QTEST_MAIN(TestResizeHandle)
#include "tst_ResizeHandle.moc"

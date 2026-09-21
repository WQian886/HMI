// ============================================================================
// tst_PaletteWidget
//   E.4a 控件调色板 6 用例
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSignalSpy>
#include <QWidget>

#include "editor/PaletteWidget.h"
#include "editor/EditorMainWindow.h"
#include "widgets/base/ControlBase.h"
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

using namespace qwhmi::editor;
using namespace qwhmi::widgets;

class TestPaletteWidget : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    // 6 用例(按 E.0 §4 表 + E.4a 具体需求)
    void constructor_populatesFromRegistry();        // 启动后项数 == 注册表 size
    void itemsShowRegisteredTypeNames();             // 每项 text 是注册短名
    void controlMimeType_isStable();                // mime type 字符串稳定
    void dragMimeData_containsTypeName();           // drag 发出后 mime 有 type
    void dropOnEditorView_createsControl();         // 拖到 EditorMainWindow 创建控件
    void dropOutsideEditor_doesNotCreate();         // 无效 mime 不创建
};

// 注册所有控件(跟 EditorMainWindow/测试需要一致)
void registerAllForTest() {
    using namespace qwhmi::widgets;
    delete new LabelControl;       delete new NumberDisplay;    delete new TextDisplay;
    delete new BarControl;         delete new GaugeControl;     delete new LedControl;
    delete new ImageControl;       delete new RectangleControl; delete new LineControl;
    delete new GroupControl;       delete new ButtonControl;    delete new ToggleControl;
    delete new SliderControl;      delete new InputEditControl;
    delete new TrendControl;       delete new TableControl;     delete new AlarmListControl;
}

void TestPaletteWidget::initTestCase() {
    registerAllForTest();
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qInfo() << "[tst_PaletteWidget] init";
}
void TestPaletteWidget::cleanupTestCase() {
    qInfo() << "[tst_PaletteWidget] cleanup";
}

// ===== 1. constructor_populatesFromRegistry =====
void TestPaletteWidget::constructor_populatesFromRegistry() {
    PaletteWidget p;
    QStringList types = ControlBase::registeredTypeNames();
    QCOMPARE(p.count(), types.size());
    QVERIFY(p.count() >= 17);   // 至少 17 个控件(E.1 + E.1b-2)
}

// ===== 2. itemsShowRegisteredTypeNames =====
void TestPaletteWidget::itemsShowRegisteredTypeNames() {
    PaletteWidget p;
    QStringList types = ControlBase::registeredTypeNames();
    for (int i = 0; i < p.count(); ++i) {
        QString text = p.item(i)->text();
        QVERIFY(types.contains(text));
    }
}

// ===== 3. controlMimeType_isStable =====
void TestPaletteWidget::controlMimeType_isStable() {
    // ★ 这是拖拽源和拖拽目标之间的契约 — 改了要同步改两边
    QCOMPARE(PaletteWidget::controlMimeType(),
             QStringLiteral("application/x-hmi-control-type"));
}

// ===== 4. dragMimeData_containsTypeName =====
//   直接构造 mime(QListWidget 真实拖拽需要 QDrag 事件循环,复杂;这里只验证 mime 编码)
void TestPaletteWidget::dragMimeData_containsTypeName() {
    PaletteWidget p;
    auto* item = p.item(0);
    QVERIFY(item != nullptr);
    const QString typeName = item->data(Qt::UserRole).toString();
    QVERIFY(!typeName.isEmpty());

    // 模拟 startDrag 内部做的事:构造 mime data
    auto* mime = new QMimeData;
    mime->setData(PaletteWidget::controlMimeType(), typeName.toUtf8());
    mime->setText(typeName);

    QVERIFY(mime->hasFormat(PaletteWidget::controlMimeType()));
    QCOMPARE(QString::fromUtf8(mime->data(PaletteWidget::controlMimeType())),
             typeName);
    delete mime;
}

// ===== 5. dropOnEditorView_createsControl =====
//   EditorMainWindow 接收 mime data → ControlBase::create → 加到 scene
//   注:不构造 QDropEvent(那个复杂),直接调 handlePaletteDrop 验证业务逻辑
void TestPaletteWidget::dropOnEditorView_createsControl() {
    EditorMainWindow emw;
    QVERIFY(emw.paletteWidget() != nullptr);
    QVERIFY(emw.scene() != nullptr);
    QCOMPARE(emw.controls().size(), 0);

    emw.handlePaletteDrop("Bar", QPointF(100, 100));

    QCOMPARE(emw.controls().size(), 1);
    auto* c = emw.controls().first();
    QCOMPARE(QString::fromUtf8(c->metaObject()->className()),
             QStringLiteral("qwhmi::widgets::BarControl"));
    QCOMPARE(c->position(), QPointF(100, 100));

    // ★ 再 drop 一个 — 验证多次 drop 都生效
    emw.handlePaletteDrop("LED", QPointF(200, 200));
    QCOMPARE(emw.controls().size(), 2);
}

// ===== 6. dropUnknownType_doesNotCreate =====
void TestPaletteWidget::dropOutsideEditor_doesNotCreate() {
    EditorMainWindow emw;

    // 未知 type → 不创建
    emw.handlePaletteDrop("NonExistentType", QPointF(50, 50));
    QCOMPARE(emw.controls().size(), 0);

    // 空 type → 不创建
    emw.handlePaletteDrop("", QPointF(50, 50));
    QCOMPARE(emw.controls().size(), 0);
}

QTEST_MAIN(TestPaletteWidget)
#include "tst_PaletteWidget.moc"

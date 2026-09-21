// ============================================================================
// tst_HandleVisualAlignment — 验证 handle 视觉边界与控件边界对齐情况
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>

#include "widgets/AlarmListControl.h"
#include "widgets/TrendControl.h"
#include "widgets/TableControl.h"
#include "widgets/BarControl.h"
#include "widgets/base/ControlBase.h"
#include "editor/ResizeHandleItem.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestHandleVisualAlignment : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_HandleVisualAlignment] init"; }
    void cleanupTestCase() { qInfo() << "[tst_HandleVisualAlignment] cleanup"; }

    void verify_handle_visual_offset_on_alarmlist();   // 量化视觉偏移
    void verify_handle_rect_in_local_coords();         // handle rect 本地坐标
    void verify_handle_scenePos_after_layout();        // handle 在 scene 实际位置
    void real_mouse_drag_BR_shrinks_correctly();       // ★ 真实 QGraphicsView 拖动 BR handle 缩小
    void hit_test_handle_corners_BR_TL_B_T_R();        // ★ hit test 各角/边 handle
};

// 工具：拿 handle 的"父坐标系 rect"（即它在 target 本地的实际占用范围）
static QRectF handleLocalRect(ResizeHandleItem* h) {
    return h->rect().translated(h->pos());
}

void TestHandleVisualAlignment::verify_handle_rect_in_local_coords() {
    // ★ QGraphicsRectItem::rect() 是本地坐标:(-4,-4,8,8),中心是 (0,0)
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    ResizeHandleItem::installHandlesFor(&a);

    int idx = 0;
    for (QGraphicsItem* ch : a.childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(ch);
        if (!h) continue;
        // ★ handle 的本地 rect = (-H/2, -H/2, H, H) = (-4, -4, 8, 8)
        QCOMPARE(h->rect(), QRectF(-4, -4, 8, 8));
        qDebug() << "[handle rect] type=" << static_cast<int>(h->handleType())
                 << "local rect =" << h->rect()
                 << "pos =" << h->pos();
        ++idx;
    }
    QCOMPARE(idx, 8);
}

void TestHandleVisualAlignment::verify_handle_visual_offset_on_alarmlist() {
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(0, 0));
    ResizeHandleItem::installHandlesFor(&a);

    qDebug() << "========== Handle 视觉边界 vs 控件边界 ==========";
    qDebug() << "控件 size = (200, 100),控件右边界=200, 底边界=100";

    int idx = 0;
    for (QGraphicsItem* ch : a.childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(ch);
        if (!h) continue;

        const QRectF localRect = handleLocalRect(h);
        const HandleType t = h->handleType();

        QStringList label;
        qreal left = localRect.left(), right = localRect.right();
        qreal top  = localRect.top(),  bot   = localRect.bottom();

        // 控件边界: x ∈ [0, 200], y ∈ [0, 100]
        qreal controlLeft = 0, controlRight = 200, controlTop = 0, controlBot = 100;

        qDebug() << "  [" << static_cast<int>(t) << "]"
                 << "local rect =" << localRect
                 << "控件右边对其:" << (right == controlRight ? "✅" : "❌ 偏移" + QString::number(right - controlRight))
                 << "控件底边对其:" << (bot   == controlBot   ? "✅" : "❌ 偏移" + QString::number(bot   - controlBot));

        ++idx;
    }
    QCOMPARE(idx, 8);
}

void TestHandleVisualAlignment::verify_handle_scenePos_after_layout() {
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(50, 50));
    ResizeHandleItem::installHandlesFor(&a);

    qDebug() << "========== Handle 在 scene 的实际位置 ==========";
    qDebug() << "控件 scenePos = (50, 50), size = (200, 100)";

    int idx = 0;
    for (QGraphicsItem* ch : a.childItems()) {
        auto* h = qgraphicsitem_cast<ResizeHandleItem*>(ch);
        if (!h) continue;
        const HandleType t = h->handleType();
        qDebug() << "  [" << static_cast<int>(t) << "]"
                 << "scenePos =" << h->scenePos()
                 << "(handle 中心) 范围 = ("
                 << h->scenePos().x() - 4 << "," << h->scenePos().y() - 4 << ") - ("
                 << h->scenePos().x() + 4 << "," << h->scenePos().y() + 4 << ")";
        ++idx;
    }
    QCOMPARE(idx, 8);
}

// 工具：模拟鼠标按下并拖动（在 scene 内）
static void simulateMouseDrag(QGraphicsView* view, QPointF sceneFrom, QPointF sceneTo) {
    // 把 scene 坐标转换成 view 坐标
    QPoint viewFrom = view->mapFromScene(sceneFrom);
    QPoint viewTo   = view->mapFromScene(sceneTo);

    QWidget* vp = view->viewport();
    QPoint globalFrom = vp->mapToGlobal(viewFrom);
    QPoint globalTo   = vp->mapToGlobal(viewTo);

    // 模拟 mouse press
    QMouseEvent press(QEvent::MouseButtonPress, viewFrom, vp->mapToGlobal(viewFrom),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &press);

    // 模拟 mouse move（多个 step）
    const int steps = 5;
    for (int i = 1; i <= steps; ++i) {
        QPoint p(viewFrom.x() + (viewTo.x() - viewFrom.x()) * i / steps,
                 viewFrom.y() + (viewTo.y() - viewFrom.y()) * i / steps);
        QMouseEvent mv(QEvent::MouseMove, p, vp->mapToGlobal(p),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &mv);
    }

    // 模拟 mouse release
    QMouseEvent rel(QEvent::MouseButtonRelease, viewTo, vp->mapToGlobal(viewTo),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &rel);
}

void TestHandleVisualAlignment::hit_test_handle_corners_BR_TL_B_T_R() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(0, 0));
    scene.addItem(&a);
    ResizeHandleItem::installHandlesFor(&a);

    view.resize(400, 300);
    view.show();
    QTest::qWait(50);

    // 检查每个 handle 是否能被 hit test 命中（中心点 + 角点）
    struct Probe { const char* name; HandleType t; QPointF sceneCenter; };
    Probe probes[] = {
        {"TL", HandleType::TopLeft,     QPointF(0,   0)},
        {"T",  HandleType::Top,         QPointF(100, 0)},
        {"TR", HandleType::TopRight,    QPointF(200, 0)},
        {"L",  HandleType::Left,        QPointF(0,   50)},
        {"R",  HandleType::Right,       QPointF(200, 50)},
        {"BL", HandleType::BottomLeft,  QPointF(0,   100)},
        {"B",  HandleType::Bottom,      QPointF(100, 100)},
        {"BR", HandleType::BottomRight, QPointF(200, 100)},
    };
    for (const Probe& p : probes) {
        QList<QGraphicsItem*> hits = scene.items(p.sceneCenter);
        qDebug() << "[hit test]" << p.name
                 << "center scene pos =" << p.sceneCenter
                 << "items count =" << hits.size();
        if (hits.size() > 0) {
            ResizeHandleItem* h = qgraphicsitem_cast<ResizeHandleItem*>(hits.first());
            if (h) {
                qDebug() << "    topmost handle type =" << static_cast<int>(h->handleType());
            } else {
                qDebug() << "    topmost IS NOT a handle! class hint =" << hits.first()->type();
            }
        }
    }
}

void TestHandleVisualAlignment::real_mouse_drag_BR_shrinks_correctly() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    view.setRenderHint(QPainter::Antialiasing);

    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(0, 0));
    scene.addItem(&a);
    ResizeHandleItem::installHandlesFor(&a);

    view.resize(600, 400);
    view.show();
    QTest::qWait(50);

    qDebug() << "[real drag] before: size =" << a.size() << " pos =" << a.pos();

    // ★ 真实拖动 BR handle：scene (200, 100) → (170, 80) = 缩小 30x20
    simulateMouseDrag(&view, QPointF(200, 100), QPointF(170, 80));

    qDebug() << "[real drag] after: size =" << a.size() << " pos =" << a.pos();

    // ★ Bug 复现：用户说"拖 BR 不能缩小"，验证 size 是否真的缩小了
    QVERIFY2(a.size().width() < 200,
             "Bug 复现:BR handle 缩小方向失效,size.width() 没减小");
    QVERIFY2(a.size().height() < 100,
             "Bug 复现:BR handle 缩小方向失效,size.height() 没减小");
}

QTEST_MAIN(TestHandleVisualAlignment)
#include "tst_HandleVisualAlignment.moc"

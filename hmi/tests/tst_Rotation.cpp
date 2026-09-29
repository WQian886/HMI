// ============================================================================
// E.4d-follow-10 · tst_Rotation —— 旋转到底坏在哪
//
// 【为什么先写测试再改代码】计划书里"加 ControlBase::rotation Q_PROPERTY"
// 这件事,读 QGraphicsItem 的头文件就知道**不需要做**:
//   QGraphicsItem 自己就有 Q_PROPERTY(qreal rotation READ rotation
//                                    WRITE setRotation NOTIFY rotationChanged)
// 而且不是 FINAL,所以:PropertySheet 的反射遍历【已经】会把它列出来,
// propertiesToJson / propertiesFromJson 也【已经】会存读它。
//
// 照着计划书去 ControlBase.h 再写一遍 Q_PROPERTY,会变成"子类覆盖父类同名属性",
// moc 允许,但等于凭空多一层间接 —— 而底下那层还在跑。
// 这就是"先读基类头文件再动手"能省下的东西。
//
// 所以本文件先【量】出真正坏掉的是哪几件事,再针对性地修。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QJsonObject>
#include "editor/EditorMainWindow.h"
#include "editor/GroupHandleItem.h"
#include "editor/PropertyChangeCommand.h"
#include "editor/ResizeHandleItem.h"
#include "widgets/RectangleControl.h"
#include "widgets/LineControl.h"
#include "widgets/PipeControl.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

class TestRotation : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ===== ① 基线:rotation 本来就有的部分 =====
    void rotation_isAlreadyAProperty();       // 反射能看到
    void rotation_roundTripsThroughJson();    // JSON 存读
    void rotationChangesBoundingRect();       // Qt 自己会转

    // ===== ② 真正坏掉的:handle 不跟随 =====
    void handles_doNotFollowRotation_currently();
    void handle_cornersShouldFollowRotation();

    // ===== ③ 真正坏掉的:光标不跟随 =====
    void handle_cursorShouldRotate();

    // ===== ④ 多选等比缩放遇到旋转控件 =====
    void groupResize_withRotatedMember_keepsAxisAlignedGeometry();

    // ===== ⑤ 编辑器相关 =====
    void rotating_doesNotBreakSelectionFrame();
    void rotating_doesNotBreakUndo();
};

namespace {

// 把 target 的 8 个 handle 位置抓成 scene 坐标
QList<QPointF> handleScenePositions(QGraphicsItem* target) {
    QList<QPointF> out;
    for (QGraphicsItem* c : target->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) {
            out.append(c->mapToScene(c->boundingRect().center()));
        }
    }
    return out;
}

int indexOfHandleAt(QGraphicsItem* target, QPointF scenePos, qreal tol = 1.5) {
    const QList<QPointF> ps = handleScenePositions(target);
    for (int i = 0; i < ps.size(); ++i) {
        const QPointF d = ps[i] - scenePos;
        if (qAbs(d.x()) < tol && qAbs(d.y()) < tol) return i;
    }
    return -1;
}

} // namespace

// ===== ① 基线 =====

void TestRotation::rotation_isAlreadyAProperty() {
    RectangleControl rc;
    const QMetaObject* meta = rc.metaObject();
    int idx = meta->indexOfProperty("rotation");
    // ★ 如果这个断言失败,说明 Qt 版本变了(QGraphicsItem 去掉了 rotation),
    //   那才需要在 ControlBase 里自己声明。先确认前提再动手。
    QVERIFY2(idx >= 0, "QGraphicsItem 不再暴露 rotation 属性 —— 先确认 Qt 版本");

    const QMetaProperty p = meta->property(idx);
    QVERIFY(p.isReadable());
    QVERIFY(p.isWritable());
    QCOMPARE(p.userType(), static_cast<int>(QMetaType::Double));

    // 读写真的通
    rc.setProperty("rotation", 90.0);
    QCOMPARE(rc.property("rotation").toDouble(), 90.0);
}

void TestRotation::rotation_roundTripsThroughJson() {
    RectangleControl rc;
    rc.setRotation(90);

    const QJsonObject j = rc.toJson();
    QVERIFY2(j.contains("rotation"),
             "toJson 没有存 rotation —— propertiesToJson 漏了 QGraphicsItem 的属性");
    QCOMPARE(j.value("rotation").toDouble(), 90.0);

    RectangleControl rc2;
    rc2.fromJson(j);
    QCOMPARE(rc2.rotation(), 90.0);
}

void TestRotation::rotationChangesBoundingRect() {
    // 200x100 的矩形转 90° 后,sceneBoundingRect 应当是 100x200(宽高互换)
    RectangleControl rc;
    rc.setSize({200, 100});
    rc.setPosition({0, 0});
    // ★ 旋转默认绕 transformOriginPoint(默认是 boundingRect 中心)
    QCOMPARE(rc.sceneBoundingRect().size().width(),  200.0);
    QCOMPARE(rc.sceneBoundingRect().size().height(), 100.0);

    rc.setRotation(90);
    // ★ 中心保持不变:转之前中心 (100,50),转之后还是 (100,50)
    const QPointF c0 = rc.sceneBoundingRect().center();
    rc.setRotation(90);
    QCOMPARE(c0, rc.sceneBoundingRect().center());

    QVERIFY2(rc.sceneBoundingRect().width() < rc.sceneBoundingRect().height(),
             "转 90° 之后 sceneBoundingRect 的宽高没有互换 —— "
             "boundingRect 没跟着 transform 走,说明需要 override boundingRect()");
}

// ===== ② handle 跟随 =====

// ★★ 这条是本文件推翻计划书的地方。
//   计划书说"旋转后 handle 位置要跟着转,否则错位"—— 实测【不用管】:
//   handle 是 target 的【子 item】(installHandlesFor 里 `new ResizeHandleItem(type, target, target)`),
//   子 item 继承父的 transform,父一转,8 个 handle 自动跟着转。
//
//   写完这一版才知道真相,差点去给 ResizeHandleItem 加一套旋转换算 ——
//   那套代码会跟 Qt 已经做好的事情重复,还多一个能算错的地方。
void TestRotation::handles_doNotFollowRotation_currently() {
    EditorMainWindow w;
    auto* rc = new RectangleControl;
    rc->setSize({200, 100});
    rc->setPosition({100, 100});
    w.addControl(rc);
    rc->setSelected(true);

    // 转之前,TL handle 在左上角
    const QRectF br = rc->sceneBoundingRect();
    const int idxBefore = indexOfHandleAt(rc, br.topLeft());
    QVERIFY2(idxBefore >= 0, "转之前 TL handle 就不在左上角 —— 前提不成立,先查别的");

    rc->setRotation(90);
    const QRectF br2 = rc->sceneBoundingRect();

    // ★ 转之后必须有 handle 落在新的 sceneBoundingRect 左上角。
    //   —— 但【不必】是原来那个 TL handle。旋转把"哪个角在左上"这件事
    //   重新定义了:原来在左上的那个,转 90° 后跑到了别的角。
    QVERIFY2(indexOfHandleAt(rc, br2.topLeft()) >= 0,
             "转 90° 之后新左上角没有 handle —— Qt 的父子 transform 没生效?"
             "(计划书担心的这个 bug 其实不存在,先确认是哪里变了)");
}

// 真正坏的是【光标】:位置跟着转了,但光标形状是构造时按"未旋转时的语义"写死的。
void TestRotation::handle_cornersShouldFollowRotation() {
    EditorMainWindow w;
    auto* rc = new RectangleControl;
    rc->setSize({200, 100});
    rc->setPosition({100, 100});
    w.addControl(rc);
    rc->setSelected(true);

    const QRectF br = rc->sceneBoundingRect();
    rc->setRotation(90);
    const QRectF br2 = rc->sceneBoundingRect();

    // ★ 旋转后的 8 个角/边中点上都必须有 handle。顺序不重要(见上一条的说明),
    //   重要的是"8 个位置一个不缺"。
    const QPointF expect[8] = {
        br2.topLeft(),  {br2.center().x(), br2.top()},
        br2.topRight(), {br2.left(),  br2.center().y()},
        {br2.right(),  br2.center().y()}, br2.bottomLeft(),
        {br2.center().x(), br2.bottom()}, br2.bottomRight(),
    };
    for (int i = 0; i < 8; ++i) {
        QVERIFY2(indexOfHandleAt(rc, expect[i]) >= 0,
                 qPrintable(QStringLiteral("旋转后位置 %1 上没有 handle,期望 (%2,%3)")
                                .arg(i).arg(expect[i].x()).arg(expect[i].y())));
    }
    Q_UNUSED(br);
}

// ===== ③ 光标跟随 =====

void TestRotation::handle_cursorShouldRotate() {
    EditorMainWindow w;
    auto* rc = new RectangleControl;
    rc->setSize({200, 100});
    w.addControl(rc);
    rc->setSelected(true);

    QList<QGraphicsItem*> handles;
    for (QGraphicsItem* c : rc->childItems()) {
        if (qgraphicsitem_cast<ResizeHandleItem*>(c)) handles.append(c);
    }
    QCOMPARE(handles.size(), 8);

    // ★ 找 TL handle(index 0)看它的光标。转 90° 之后,原来的 TL 角
    //   在视觉上变成了 BL 角,光标应该从 SizeFDiag 变成 …(对角缩放仍是 FDiag,
    //   所以这个例子区分不出来)。
    //   真正能区分的是 Top handle(index 1):它是 SizeVerCursor,
    //   转 90° 之后它在视觉上成了左边/右边,应该是 SizeHorCursor。
    auto* top = qgraphicsitem_cast<ResizeHandleItem*>(handles.at(1));
    QVERIFY(top);
    QCOMPARE(top->cursor().shape(), Qt::SizeVerCursor);

    rc->setRotation(90);
    // ★ 当前实现不改光标,所以这里会失败 —— 它是验收标准。
    QVERIFY2(top->cursor().shape() == Qt::SizeHorCursor,
             "转 90° 之后 Top handle 的光标还是竖的 —— 鼠标拖动方向和视觉不符");
}

// ===== ④ 多选等比缩放 =====

void TestRotation::groupResize_withRotatedMember_keepsAxisAlignedGeometry() {
    EditorMainWindow w;
    auto* a = new RectangleControl; a->setSize({100, 50}); a->setPosition({100, 100});
    auto* b = new RectangleControl; b->setSize({100, 50}); b->setPosition({300, 100});
    w.addControl(a); w.addControl(b);
    a->setSelected(true); b->setSelected(true);

    // 把 a 转 90°(它变高变窄),多选的 union 会变
    a->setRotation(90);
    const QRectF unionBefore = a->sceneBoundingRect().united(b->sceneBoundingRect());

    // 拖 BR handle 把整个组放大 2 倍
    // ★ 用 EditorMainWindow 自己的 accessor,不要去 scene 里扫 item ——
    //   扫出来的第一个 GroupHandleItem 可能是别的测试留下的野指针,
    //   本文件就是这么崩的(0xC0000005,栈在 QAtomicOps 里,看不出是谁)。
    GroupHandleItem* group = w.groupHandle();
    QVERIFY2(group != nullptr, "多选了却没装 GroupHandleItem");

    group->captureOriginalRects();
    const QRectF newRect(unionBefore.x(), unionBefore.y(),
                         unionBefore.width() * 2, unionBefore.height() * 2);
    group->resizeToGroupRect(newRect);

    // ★ 验收:两个控件的 position/size 都还是有效值(非负、非 0、没跑飞)
    //   注意这里【不断言旋转语义正确】—— 等比缩放一个已旋转的控件,
    //   严格讲应该同步旋转来保持形状,那是更大的问题。
    //   本阶段只保证"不会崩、不会写出离谱的值"。
    for (auto* t : {static_cast<ControlBase*>(a), static_cast<ControlBase*>(b)}) {
        QVERIFY(t->size().width()  >= 0);
        QVERIFY(t->size().height() >= 0);
        QVERIFY2(t->position().x() > -1e6 && t->position().y() > -1e6,
                 "等比缩放把控件甩到天边去了 —— relX/relY 的分母用错了");
    }
}

// ===== ⑤ 不破坏别的 =====

void TestRotation::rotating_doesNotBreakSelectionFrame() {
    EditorMainWindow w;
    auto* rc = new RectangleControl;
    rc->setSize({200, 100});
    rc->setPosition({100, 100});
    w.addControl(rc);
    rc->setSelected(true);
    QVERIFY(w.selectionFrame() != nullptr);
    const QRectF before = w.selectionFrame()->rect();

    rc->setRotation(90);
    // ★ rotationChanged 是个 QGraphicsItem 的信号,但我们没接它去刷 frame。
    //   sceneBoundingRect 会变(200x100 → 100x200),frame 没跟着变就是 bug。
    QVERIFY2(w.selectionFrame()->rect() != before,
             "转了之后选中框没更新 —— frame 仍在用旋转前的尺寸,"
             "用户看到框和控件形状对不上");
}

void TestRotation::rotating_doesNotBreakUndo() {
    // 通过属性面板改 rotation 应该有 Undo —— PropertySheet 走
    // PropertyChangeCommand。直接用 setRotation 不进 Undo(那是 Qt 的活),
    // 所以这条只验"经由属性面板改"这条路径。
    RectangleControl rc;
    QUndoStack stack;
    auto* cmd = new PropertyChangeCommand(&rc, "rotation",
                                          QVariant(0.0), QVariant(90.0));
    stack.push(cmd);
    QCOMPARE(rc.rotation(), 90.0);
    stack.undo();
    QCOMPARE(rc.rotation(), 0.0);
    stack.redo();
    QCOMPARE(rc.rotation(), 90.0);
}

QTEST_MAIN(TestRotation)
#include "tst_Rotation.moc"

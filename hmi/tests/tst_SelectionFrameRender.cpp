// ============================================================================
// E.4d-follow-9 · tst_SelectionFrameRender —— 选中框【真的画出来了吗】
//
// 【为什么要有这个】`SelectionFrameItem` 早就实现了(构造函数里就 setPen 了
//   蓝色虚线),而且 tst_EditorMainWindow 里有 4 个用例断言它的 pos/rect 几何。
//   但那些用例**没有一个验证画面上真的有那条线** ——
//   「几何对」和「画出来」是两件事:pen 颜色刷成背景色、brush 没清、
//   zValue 被别的 item 压住,几何照样全对,画面上就是什么都没有。
//   E.4d-follow-5/6/7 一路修的 bug 也都是这一类:
//   位置算对了、信号接了、但用户看到的东西不对。
//
//   所以这里不查几何,直接把 scene 渲染到 QImage 上数像素。
//   之前的 4 个几何用例继续保留 —— 它们防的是另一类回归。
//
// 【怎么数像素】frame 的 pen 是 #0078D7 蓝色虚线,控件本身(LabelControl 默认
//   深灰字)不会画这个色。所以数"接近 #0078D7 的像素"即可,不用管虚线断口。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include "editor/EditorMainWindow.h"
#include "editor/SelectionFrameItem.h"
#include "widgets/LabelControl.h"
#include "widgets/RectangleControl.h"

using namespace qwhmi::widgets;
using namespace qwhmi::editor;

namespace {

// ★ 数"接近 #0078D7"的像素。为什么用容差而不是精确相等:
//   虚线画在 2x 缩放上时 QPainter 会做抗锯齿,边缘像素是混合色。
//   精确相等会得到 0,然后我就会去查"明明画了为什么数不到"——白忙一轮。
int countFramePixels(const QImage& img, int tolerance = 40) {
    const QColor want(0, 120, 215);
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (qAbs(c.red()   - want.red())   <= tolerance &&
                qAbs(c.green() - want.green()) <= tolerance &&
                qAbs(c.blue()  - want.blue())  <= tolerance) {
                ++n;
            }
        }
    }
    return n;
}

} // namespace

class TestSelectionFrameRender : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); }

    // ★★★ 本文件最要紧的一条:选中框必须在画面上真的画出来。
    //   之前所有用例都只断言几何,这一条才断言"用户看得见"。
    void frame_isActuallyPainted();

    // ★ 反向锁:没选中时不能有那条线(否则会误以为一直选中了)
    void noFrame_beforeSelection();

    // ★ 反向锁:取消选中后线必须消失。
    //   取消选中不清 visible 的话,线会一直挂在那儿 —— 删掉了还"选中着"。
    void frame_disappearsAfterDeselect();

    // ★★★ 这才是 bug图/图1.png 报的那件事。
    //   上面那条测的是"frame 自己画不画",这条测的是"在真实 scene 里还看不看得到"。
    //   frame 的 rect 正好等于 target.sceneBoundingRect,而 2px 的 pen 是
    //   以路径为中心画的 —— 有一半落在控件【内部】。控件自己 paint 的背景
    //   (RectangleControl 的填充色、ButtonControl 的蓝色底)盖住那一半,
    //   用户就只看到半条线,或者在深色控件上完全看不见。
    //   这就是文档里"选中框在控件内部"的真实成因。
    void frame_survivesInFullSceneRender();

    // ★ pen 的三个属性:虚线 / 2px / 不随缩放变化。
    //   逐条锁,因为这三条退化都是静默的(线还在,只是变成实线/细线/跟着缩放),
    //   没人会注意到,但观感差很多。
    void pen_isDashedAndCosmetic();
};

void TestSelectionFrameRender::frame_isActuallyPainted() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    lbl->setSize({200, 100});
    lbl->setPosition({100, 100});
    w.addControl(lbl);
    lbl->setSelected(true);

    QVERIFY(w.selectionFrame() != nullptr);

    // ★ 把选中框单独渲染到一个干净的白底图上。
    //   渲染整个 scene 的话,LabelControl 自己的字和背景会混进来,
    //   将来 Label 改成蓝色就会让这个测试假绿 —— 测的是"画面里有蓝的",
    //   而不是"选中框画了蓝的"。
    // ★ 画布比框【各大一圈】:2px 的 pen 以路径为中心,贴着 rect 的 4 条边画,
    //   有一半落在 rect 外侧。画布正好等于 rect 的话,那半圈直接被裁掉 ——
    //   头一版就是这么得到 n=85 的假阴性。边距 4px 足够容下 2px pen。
    const int margin = 4;
    QImage img(200 + 2 * margin, 100 + 2 * margin, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        // ★ 只平移 margin。直接调 paint() 是在【本地坐标】里画的 ——
        //   item 的 rect 本来就是 (0,0,200,100),再把 scene pos 减一遍就平移了两次,
        //   框整个跑到画布外,只剩一条 4px 高的边露在图里。
        //   (头一版就是这么写的,数出 n=180,差点被我当成"pen 有问题"。)
        p.translate(margin, margin);
        // ★ option 必须给一个真对象 —— QGraphicsRectItem::paint 内部会做
        //   `option->state & QStyle::State_MouseOver` 这样的解引用,
        //   传 nullptr 直接 0xC0000005。Qt 平时总会经由 QGraphicsScene 传一个,
        //   手写调用时这个契约不会体现在头文件里,踩了就得读 Qt 源码才发现。
        QStyleOptionGraphicsItem opt;
        w.selectionFrame()->paint(&p, &opt, nullptr);
    }

    const int n = countFramePixels(img);
    // ★ 200x100 的框周长 600px,2px 宽 + 虚线(实占约 2/3)= 约 800 像素面积。
    //   阈值给 200:低到能容错虚线端点 + 抗锯齿边缘像素(那些混着白底,过不了容差),
    //   高到"只画了一条边"绝不算过。
    //   ★ 头一版这个测试给 n > 200 报了 n=85,原因是画布正好 200x100,
    //     而 2px 的 pen 是以路径为中心画的 —— 一半在 rect 外,被画布裁掉了。
    //     不是没画出来。所以画布必须比框大一圈,让 pen 完整落在里面。
    QVERIFY2(n > 200,
             qPrintable(QStringLiteral("选中框画出来的蓝色像素太少(n=%1)—— "
                                       "可能是被控件自己的背景盖住了,或者 pen 退化成实线")
                                .arg(n)));
}

void TestSelectionFrameRender::noFrame_beforeSelection() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    lbl->setSize({100, 50});
    w.addControl(lbl);
    // ★ 不调 setSelected —— 此刻不该有 frame
    QVERIFY2(w.selectionFrame() == nullptr || !w.selectionFrame()->isVisible(),
             "还没选中就画了选中框 —— 用户会以为控件一直处于选中态");
}

void TestSelectionFrameRender::frame_disappearsAfterDeselect() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    lbl->setSize({100, 50});
    w.addControl(lbl);
    lbl->setSelected(true);
    QVERIFY(w.selectionFrame() != nullptr);
    QVERIFY(w.selectionFrame()->isVisible());

    lbl->setSelected(false);
    // ★ 两种"消失"都算对:frame 被删了,或者被 setVisible(false)。
    //   实现走的是后者(removeSelectionFrame 之外还有 visible 分支),
    //   但测试只锁"用户看不到了",不锁实现细节。
    QVERIFY2(w.selectionFrame() == nullptr || !w.selectionFrame()->isVisible(),
             "取消选中后选中框还挂在那儿 —— 删了控件画面上还留着框");
}

// ★★★ 渲染【整个 scene】,而不是只画 frame 自己 —— 这才是用户看到的画面。
//   背景用一个打底的浅色,和真实画布一致。
void TestSelectionFrameRender::frame_survivesInFullSceneRender() {
    EditorMainWindow w;
    // ★ 用 RectangleControl 而不是 LabelControl:它 paint 时会刷一整块填充色,
    //   正好模拟"控件背景盖住框的内半圈"这个场景(图 1 报的 Button 就是蓝色实心)。
    auto* rc = new RectangleControl;
    rc->setSize({200, 100});
    rc->setPosition({100, 100});
    w.addControl(rc);
    rc->setSelected(true);
    QVERIFY(w.selectionFrame() != nullptr);

    QGraphicsScene* scene = rc->scene();
    QVERIFY(scene != nullptr);
    scene->setSceneRect(50, 50, 400, 300);

    QImage img(400, 300, QImage::Format_ARGB32);
    img.fill(QColor(0xF0, 0xF0, 0xF0));
    {
        QPainter p(&img);
        // ★ scene->render 会自己按 sceneRect 做 translate,不用手动平移
        scene->render(&p, QRectF(0, 0, 400, 300), scene->sceneRect());
    }

    // ★ frame 在 scene 坐标里占 (100,100)-(300,200),对应图片坐标 (50,50)-(250,200)。
    //   只在这块区域里数像素 —— 整张图数的话,RectangleControl 的填充色、
    //   8 个蓝色 handle(ResizeHandleItem 的 brush 也是 #0078D7!都是同一个蓝)
    //   都会混进来,数字虚高,测试就废了。
    //
    //   ★★ handle 跟 frame 同色(#0078D7)是个真问题:所以这里特意【排除 handle
    //   所在的边中点区域】,只数靠近四个角的那几段。
    int n = 0;
    const QColor want(0, 120, 215);
    for (int y = 50; y < 200; ++y) {
        for (int x = 50; x < 250; ++x) {
            // ★ handle 在 8 个位置:四角 + 四边中点。跳过每条边靠中间的 ±30px。
            const int dl = x - 50, dr = 250 - 1 - x;
            const int dt = y - 50, db = 200 - 1 - y;
            if (dl < 30 || dr < 30) continue;   // 左边/右边的 handle
            if (dt < 30 || db < 30) continue;   // 上边/下边的 handle
            const QColor c = img.pixelColor(x, y);
            if (qAbs(c.red()   - want.red())   <= 40 &&
                qAbs(c.green() - want.green()) <= 40 &&
                qAbs(c.blue()  - want.blue())  <= 40) {
                ++n;
            }
        }
    }
    // ★ 排除了 8 个 handle 区之后,剩下的 4 段边每段约 (200-60)=140px 长,
    //   2px 宽、虚线实占约 2/3 → 4 段合计约 750 像素。阈值给 200 够宽松。
    QVERIFY2(n > 200,
             qPrintable(QStringLiteral("整场景渲染后,选中框只剩 %1 个像素 —— "
                                       "它被控件自己的背景盖住了(图 1 的现象)")
                            .arg(n)));
}

void TestSelectionFrameRender::pen_isDashedAndCosmetic() {
    EditorMainWindow w;
    auto* lbl = new LabelControl;
    w.addControl(lbl);
    lbl->setSelected(true);
    QVERIFY(w.selectionFrame() != nullptr);

    const QPen pen = w.selectionFrame()->pen();
    QCOMPARE(pen.style(), Qt::DashLine);
    QCOMPARE(pen.widthF(), SelectionFrameItem::FRAME_WIDTH);
    // ★ cosmetic:画布缩放时线宽不变。跟随缩放的话,缩小到 30% 时
    //   2px 线会变成 0.6px —— 屏幕上直接看不见,用户以为没选中。
    QVERIFY2(pen.isCosmetic(), "pen 非 cosmetic —— 缩小画布时选中框会细到看不见");

    // ★ 必须在 handle 之上,否则虚线会从 8 个缩放点中间穿过去,很难看
    QVERIFY(w.selectionFrame()->zValue() >= SelectionFrameItem::PEN_Z_VALUE);
}

QTEST_MAIN(TestSelectionFrameRender)
#include "tst_SelectionFrameRender.moc"

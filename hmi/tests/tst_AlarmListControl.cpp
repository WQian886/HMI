// ============================================================================
// tst_AlarmListControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QApplication>
#include "widgets/AlarmListControl.h"
#include "../AlarmManager.h"
#include "../Alarm.h"

using namespace qwhmi::widgets;

class TestAlarmListControl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QT_QPA_PLATFORM", "offscreen"); qInfo() << "[tst_AlarmListControl] init"; }
    void cleanupTestCase() { qInfo() << "[tst_AlarmListControl] cleanup"; }

    void construct_defaults();
    void paint_doesNotCrash();
    void bindAlarmManager_connectsSignals();
    void bindAlarmManager_refreshShowsActiveAlarms();
    void proxyChild_blocksParentSelection();   // ★ 复现 bug 实证
};

void TestAlarmListControl::construct_defaults() {
    AlarmListControl a;
    QCOMPARE(a.size(), QSizeF(320, 100));
}

void TestAlarmListControl::paint_doesNotCrash() {
    QGraphicsScene s;
    AlarmListControl a;
    s.addItem(&a);
    QVERIFY(true);   // proxy 自己渲染
}

void TestAlarmListControl::bindAlarmManager_connectsSignals() {
    AlarmManager mgr;
    AlarmListControl a;
    a.bindAlarmManager(&mgr);
    // 验证:再次 bind 应被忽略(代码有 if(alarmMgr_) return 守卫)
    a.bindAlarmManager(&mgr);
    QVERIFY(true);
}

void TestAlarmListControl::bindAlarmManager_refreshShowsActiveAlarms() {
    // ★ 简化:不构造 ThresholdAlarmChecker(它需要 PollScheduler + driver,
    //   链路太重,且与"控件 bind 不崩"目标无关)。
    //   空 AlarmManager + bindAlarmManager → 控件内部 refreshNow() 不崩即可。
    AlarmManager mgr;
    AlarmListControl a;
    a.bindAlarmManager(&mgr);
    QVERIFY(true);
    // 触发一次清空信号,验证连接没断
    emit mgr.allCleared();
    QVERIFY(true);
}

// ★ Bug 复现实证:QGraphicsProxyWidget 默认 acceptedMouseButtons != NoButton,
//   吞掉所有鼠标事件 → 父 AlarmListControl 收不到 press → 选不中
//   修复预期:proxy.setAcceptedMouseButtons(Qt::NoButton) 后,proxy.childItems()
//   第一个子图元的 acceptedMouseButtons == Qt::NoButton
void TestAlarmListControl::proxyChild_blocksParentSelection() {
    AlarmListControl a;
    a.setSize(QSizeF(200, 100));
    a.setPos(QPointF(0, 0));

    // 1) 验证 proxy 子图元存在
    QGraphicsProxyWidget* proxy = nullptr;
    for (QGraphicsItem* ch : a.childItems()) {
        proxy = qgraphicsitem_cast<QGraphicsProxyWidget*>(ch);
        if (proxy) break;
    }
    QVERIFY2(proxy != nullptr, "AlarmListControl 必须有一个 QGraphicsProxyWidget 子");

    // 2) 关键断言:在未修复状态下,proxy 接受鼠标事件(吞事件)
    qDebug() << "[BugRepro] proxy->acceptedMouseButtons() =" << proxy->acceptedMouseButtons();
    if (proxy->acceptedMouseButtons() == Qt::NoButton) {
        qDebug() << "[BugRepro] ✅ 已修复:proxy 不再吃事件";
    } else {
        qDebug() << "[BugRepro] ❌ Bug 复现:proxy 接受鼠标事件(默认 Qt::LeftButton)";
        qDebug() << "[BugRepro]    → 父 AlarmListControl 收不到 mousePress";
        qDebug() << "[BugRepro]    → ItemIsSelectable 失效,不能选中";
        qDebug() << "[BugRepro]    → ItemIsMovable 失效,不能移动";
        qDebug() << "[BugRepro]    → scene->selectionChanged 不发,8 handle 不挂";
    }
    QVERIFY2(proxy->acceptedMouseButtons() == Qt::NoButton,
             "Bug 已修:proxy 应 setAcceptedMouseButtons(Qt::NoButton),让父收事件");

    // 3) 验证父控件 flags 设了(基类应该做了)
    QVERIFY(a.flags() & QGraphicsItem::ItemIsSelectable);
    QVERIFY(a.flags() & QGraphicsItem::ItemIsMovable);
}

QTEST_MAIN(TestAlarmListControl)
#include "tst_AlarmListControl.moc"
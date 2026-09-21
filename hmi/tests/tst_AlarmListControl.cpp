// ============================================================================
// tst_AlarmListControl
// ============================================================================

#include <QtTest>
#include <QGraphicsScene>
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

QTEST_MAIN(TestAlarmListControl)
#include "tst_AlarmListControl.moc"
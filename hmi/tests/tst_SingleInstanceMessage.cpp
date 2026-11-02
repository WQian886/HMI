// ============================================================================
// G-25(2026-10-30) · 单实例消息【只处理一次】 —— 回归测试
//
//  【这条缺陷是什么】
//    体检 P1-7:共享内存里的消息【读完不清零、不记已读】。
//    主实例每 500ms 轮询一次,每次都把同一份内容反序列化出来再 emit 一次
//    messageReceived —— 于是:
//
//      · main.cpp 那个槽每 500ms 把主窗口强行拉到前台一次
//        (SetWindowPos TOPMOST → NOTOPMOST → show → raise → activateWindow)
//        ⇒ 操作员点别的窗口会被抢回去
//      · 每 tick 打 6~8 行日志 ⇒ 一天约 60 万行
//
//    而双击第二个 hmi.exe 只应该发生【一次】。
//
//  【这几条用例分别钉住哪一段】
//    1. messageIsHandledExactlyOnce
//       ★★ 这就是那条缺陷本身:一条消息 ⇒ messageReceived 只发一次。
//       发完之后【再轮询 5 次】,一次都不许多。
//    2. secondMessageIsAlsoHandled
//       ★ 反向的那一半:修复不能是"干脆不处理了"。
//         第二条消息(seq 变了)必须照样送到。
//    3. manyMessages_allHandled
//       连发 5 条 ⇒ 收到 5 条,一条不多一条不少、顺序不变。
//    4. noMessage_noSignal
//       没有消息时轮询多少次都不许发信号。
//    5. primaryDoesNotSendToItself
//       主实例自己不许给自己发消息(老规矩,顺手钉住)。
//
//  【为什么不用 QSignalSpy::wait()】
//    主实例里有个 500ms 的定时器在调 checkIncomingMessages。
//    测试【故意不跑事件循环】,所以定时器一次都不会转 ——
//    消息只可能由测试自己显式调 checkIncomingMessages() 送来。
//    ⇒ 结果是确定的,不靠 sleep,也不靠运气。
//
//  【负向对照】
//    把 SingleInstance.cpp 里那句 "seq == lastSeq 就跳过" 去掉,
//    第 1/2/3 条必须立刻变红。
//
//  关联:src/instance/SingleInstance.h / .cpp、main.cpp(messageReceived 那个槽)
// ============================================================================

#include <QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "instance/SingleInstance.h"

using namespace qwhmi::instance;

class tst_SingleInstanceMessage : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void messageIsHandledExactlyOnce();
    void secondMessageIsAlsoHandled();
    void manyMessages_allHandled();
    void noMessage_noSignal();
    void primaryDoesNotSendToItself();
};

void tst_SingleInstanceMessage::initTestCase() {
    // 这个测试碰的是真共享内存。跑之前先把环境变量摘掉 ——
    // 万一机器上有个 hmi.exe 正好占着默认键,结论会读成假的。
    qunsetenv("HMI_INSTANCE_KEY");
}

void tst_SingleInstanceMessage::cleanupTestCase() {
    qunsetenv("HMI_INSTANCE_KEY");
}

// 每条用例一把【独一无二的】键 —— 免得跟机器上跑着的 hmi.exe 撞上,
// 撞上了结论会读成假的。
static QString uniqueKey(const char* suffix) {
    return QStringLiteral("tst-single-msg-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QString::fromLatin1(suffix));
}

// ---------------------------------------------------------------------------
// 1. ★★ 一条消息只处理一次
// ---------------------------------------------------------------------------
void tst_SingleInstanceMessage::messageIsHandledExactlyOnce() {
    const QString key = uniqueKey("once");

    SingleInstance primary;
    QVERIFY2(primary.tryBecomePrimary(key), "主实例拿不到身份");

    QSignalSpy spy(&primary, &SingleInstance::messageReceived);

    SingleInstance secondary;
    QVERIFY2(!secondary.tryBecomePrimary(key), "同一把键的第二个实例不该是主实例");
    QVERIFY(secondary.sendMessageToPrimary({QStringLiteral("--project"),
                                             QStringLiteral("x.json")}));

    primary.checkIncomingMessages();
    QCOMPARE(spy.count(), 1);
    // ★ 用 takeFirst 把这条【取走】—— 不取走的话,后面那句
    //   "再轮询 5 次,一条都不许多"就变成在数【它自己】,永远 ≥1。
    QCOMPARE(spy.takeFirst().at(0).toStringList(),
             QStringList({QStringLiteral("--project"), QStringLiteral("x.json")}));

    // ★★★ 这 5 次就是原缺陷的现场 ——
    //   修复前,每一次轮询都会把同一条消息【重放】一遍,
    //   窗口就被抢 5 次焦点、日志里就多 5 轮。
    for (int i = 0; i < 5; ++i) {
        primary.checkIncomingMessages();
    }
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// 2. ★ 反向的那一半:第二条消息必须照样送到
//
//    修复"只处理一次"最省事的错法是"干脆不处理了" ——
//    那条用例会把这种情况抓住。
// ---------------------------------------------------------------------------
void tst_SingleInstanceMessage::secondMessageIsAlsoHandled() {
    const QString key = uniqueKey("second");

    SingleInstance primary;
    QVERIFY(primary.tryBecomePrimary(key));
    QSignalSpy spy(&primary, &SingleInstance::messageReceived);

    SingleInstance secondary;
    QVERIFY(!secondary.tryBecomePrimary(key));

    QVERIFY(secondary.sendMessageToPrimary({QStringLiteral("--project"),
                                             QStringLiteral("x.json")}));
    primary.checkIncomingMessages();
    QCOMPARE(spy.count(), 1);

    // ★ 第二条:序号变了 ⇒ 必须放行
    QVERIFY(secondary.sendMessageToPrimary({QStringLiteral("--project"),
                                             QStringLiteral("y.json")}));
    primary.checkIncomingMessages();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toStringList(),
             QStringList({QStringLiteral("--project"), QStringLiteral("y.json")}));

    // 再轮询:还是只有 2 条
    primary.checkIncomingMessages();
    primary.checkIncomingMessages();
    QCOMPARE(spy.count(), 2);
}

// ---------------------------------------------------------------------------
// 3. 连发 5 条 ⇒ 收到 5 条,顺序不变
// ---------------------------------------------------------------------------
void tst_SingleInstanceMessage::manyMessages_allHandled() {
    const QString key = uniqueKey("many");

    SingleInstance primary;
    QVERIFY(primary.tryBecomePrimary(key));
    QSignalSpy spy(&primary, &SingleInstance::messageReceived);

    SingleInstance secondary;
    QVERIFY(!secondary.tryBecomePrimary(key));

    for (int i = 0; i < 5; ++i) {
        QVERIFY(secondary.sendMessageToPrimary(
            {QStringLiteral("--project"), QStringLiteral("p%1.json").arg(i)}));
        primary.checkIncomingMessages();
    }
    QCOMPARE(spy.count(), 5);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(spy.at(i).at(0).toStringList().value(1),
                 QStringLiteral("p%1.json").arg(i));
    }

    // 全部发完之后再轮询 5 次:一条都不许多
    for (int i = 0; i < 5; ++i) {
        primary.checkIncomingMessages();
    }
    QCOMPARE(spy.count(), 5);
}

// ---------------------------------------------------------------------------
// 4. 没有消息时,轮询多少次都不许发信号
// ---------------------------------------------------------------------------
void tst_SingleInstanceMessage::noMessage_noSignal() {
    const QString key = uniqueKey("empty");

    SingleInstance primary;
    QVERIFY(primary.tryBecomePrimary(key));
    QSignalSpy spy(&primary, &SingleInstance::messageReceived);

    for (int i = 0; i < 10; ++i) {
        primary.checkIncomingMessages();
    }
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// 5. 主实例自己不许给自己发消息
// ---------------------------------------------------------------------------
void tst_SingleInstanceMessage::primaryDoesNotSendToItself() {
    const QString key = uniqueKey("self");

    SingleInstance primary;
    QVERIFY(primary.tryBecomePrimary(key));

    QVERIFY2(!primary.sendMessageToPrimary({QStringLiteral("--project")}),
             "主实例不许给自己发消息");
}

QTEST_GUILESS_MAIN(tst_SingleInstanceMessage)
#include "tst_SingleInstanceMessage.moc"

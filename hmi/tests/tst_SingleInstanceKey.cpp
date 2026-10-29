// ============================================================================
// 2026-10-21 · 单实例键可指定 —— 回归测试
//
//  【这条测试是为了什么】
//    2026-10-21 要开真正的 24h 长跑,但机器上还挂着一个上一轮的 hmi.exe
//    (PID 42428,从 2026-10-20 17:05:40 跑到现在,从 17:05:41 起就再没
//     跟 PLC 说过一句话 —— 第一批读请求失败之后变量卡在在途态了)。
//
//    用户的要求是"不想影响那个长跑"。而单实例锁用的是一把【固定的】
//    共享内存键,第二个实例一启动就退出 ——
//    于是脚本只能"拒绝开跑",或者"把上一个杀掉"。
//    两条都不是好答案。
//
//    改法:实例键可以从环境变量 HMI_INSTANCE_KEY 取。
//    键不同 → 两把锁 → 两个实例各跑各的,谁也不动谁。
//    键相同 → 行为和以前完全一样(第二个照样被挡住)。
//
//  【这几条用例分别钉住哪一段】
//    1. defaultKeyUnchanged
//       默认键必须还是 "hmi-single-instance-v1"。
//       改它 = 所有已经在跑的实例之间互相不认识了 —— 看着像"支持多开",
//       实际上是"单实例没了"。这条守住它。
//    2. keyComesFromEnvironment
//       设了环境变量就用它,没设就用默认键。
//    3. sameKeyStillCollides
//       ★ 单实例的【本职】不能因为加了新出口就失效。
//       同一个键的第二个实例必须不是 primary。
//    4. differentKeysCoexist
//       ★ 新出口必须真的能共存 —— 不同键的两个实例都是 primary。
//       这条是整个"不影响上一轮长跑"方案的地基。
//
//  关联:src/instance/SingleInstance.h / .cpp、main.cpp(第 3 步)
// ============================================================================

#include <QtTest>
#include <QCoreApplication>
#include <QProcessEnvironment>

#include "instance/SingleInstance.h"

using namespace qwhmi::instance;

class tst_SingleInstanceKey : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void defaultKeyUnchanged();
    void keyComesFromEnvironment();
    void sameKeyStillCollides();
    void differentKeysCoexist();
};

static QString uniqueKey(const char* suffix) {
    return QStringLiteral("tst-single-instance-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QString::fromLatin1(suffix));
}

void tst_SingleInstanceKey::initTestCase() {
    // 这个测试碰的是真共享内存。跑之前先确认没人在用同一把键 ——
    // 万一别的进程(比如一个还活着的 hmi.exe)正好占着,结论会读成假的。
    qunsetenv("HMI_INSTANCE_KEY");
}

void tst_SingleInstanceKey::cleanupTestCase() {
    qunsetenv("HMI_INSTANCE_KEY");
}

// ---------------------------------------------------------------------------
// 1. 默认键不许变
// ---------------------------------------------------------------------------
void tst_SingleInstanceKey::defaultKeyUnchanged() {
    QCOMPARE(QString::fromLatin1(SingleInstance::kDefaultKey),
             QStringLiteral("hmi-single-instance-v1"));
}

// ---------------------------------------------------------------------------
// 2. 键从环境变量来
// ---------------------------------------------------------------------------
void tst_SingleInstanceKey::keyComesFromEnvironment() {
    qunsetenv("HMI_INSTANCE_KEY");
    QCOMPARE(SingleInstance::keyFromEnvironment(),
             QString::fromLatin1(SingleInstance::kDefaultKey));

    qputenv("HMI_INSTANCE_KEY", "soak-run-2026-10-21");
    QCOMPARE(SingleInstance::keyFromEnvironment(),
             QStringLiteral("soak-run-2026-10-21"));

    // 设成空串 = 没设 —— 不能变成一把【空键】,
    // 空键会让所有实例共用同一个(无名)共享内存,正好和"每个实例一把"相反。
    qputenv("HMI_INSTANCE_KEY", "");
    QCOMPARE(SingleInstance::keyFromEnvironment(),
             QString::fromLatin1(SingleInstance::kDefaultKey));

    qunsetenv("HMI_INSTANCE_KEY");
}

// ---------------------------------------------------------------------------
// 3. 同一个键:第二个实例照样不是 primary(单实例的本职不能丢)
// ---------------------------------------------------------------------------
void tst_SingleInstanceKey::sameKeyStillCollides() {
    const QString key = uniqueKey("same");

    SingleInstance first;
    QVERIFY2(first.tryBecomePrimary(key),
             "第一个实例拿不到主身份 —— 这把键是不是被别的进程占了?");

    SingleInstance second;
    QVERIFY2(!second.tryBecomePrimary(key),
             "同一个键的第二个实例成了 primary —— 单实例机制被绕过了。"
             "这条一红,加 --instance-key 就等于'取消了单实例',那不能上线。");
    QVERIFY(!second.isPrimary());
}

// ---------------------------------------------------------------------------
// 4. 不同的键:两个实例可以共存 ★ 这条是整个方案的地基 ★
//
//    场景:长跑实例的键 = soak-xxx,机器上那个旧实例的键 = 默认键。
//    两把锁互不相干 → 两个进程各跑各的 → 谁也不用被结束掉。
// ---------------------------------------------------------------------------
void tst_SingleInstanceKey::differentKeysCoexist() {
    const QString keyA = uniqueKey("a");
    const QString keyB = uniqueKey("b");
    QVERIFY(keyA != keyB);

    SingleInstance instA;
    SingleInstance instB;

    QVERIFY2(instA.tryBecomePrimary(keyA), "A 拿不到主身份");
    QVERIFY2(instB.tryBecomePrimary(keyB),
             "B 用了不同的键却拿不到主身份 —— "
             "那么'换一个键就能共存'这个方案根本不成立,"
             "整个'不动上一轮长跑'的想法就得另想办法。");

    QVERIFY(instA.isPrimary());
    QVERIFY(instB.isPrimary());
}

QTEST_MAIN(tst_SingleInstanceKey)
#include "tst_SingleInstanceKey.moc"

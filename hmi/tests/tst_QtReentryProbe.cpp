// ============================================================================
// tst_QtReentryProbe —— ★基线★:Qt 6.8.3 的自定义消息处理器到底会不会被重入?
//
// ============================================================================
//  【为什么要钉这个】
//    G-19 一开始的结论是:
//
//      messageHandler ──lock(mutex)──▶ openNewLogFile()
//                                              └─▶ qWarning("failed to open")
//                                                        └─▶ messageHandler(重入)
//                                                              └─▶ lock(mutex) ★死锁
//
//    并按这个结论修了半天(还加了一层"重入保护")。
//    ★ 实测推翻了这个结论 ★ —— 本文件就是那次实测。
//
//    实测结果(Qt 6.8.3):Qt 【不会】重入自定义消息处理器。
//      在 handler 内部再打一条日志,Qt 会把它交给【默认 handler】,
//      自定义 handler 一次都不会被重入(entered=1 / reentered=0)。
//    ⇒ 那条死锁链不成立,G-19 从 P0 降级为 P2(真正的缺陷是"每条日志都重试
//      一次打开 + 日志静默全丢",见 tst_LoggingFileUnavailable.cpp)。
//
//  【为什么必须留成【断言】而不是一次性探针】
//    这条语义是 Logging.cpp 里【那把非递归 QMutex + 重入保护】的定位依据。
//    哪天升级 Qt 改了它(变成会重入),这里会先红,提醒我们重新审视
//    那把锁和那道保护 —— 而不是等到现场发现程序不动了。
//    ★ 绿色的测试不一定有效,能咬自己一口的才有效 ★
//
//  【失败时该做什么】
//    如果本文件变红,说明 Qt 改了重入语义。此时:
//      · Logging.cpp 里 messageHandler 的"重入保护"从【保险】变成【必需】;
//      · handler 内部【绝不能】再用 qWarning/qDebug 报错
//        (那会真的二次 lock ⇒ 死锁),fprintf 反而是对的;
//      · G-19 的级别要按"会死锁"重新评。
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>

#include <cstdio>

#include "stderr_capture.h"

namespace {

int g_depth     = 0;    // 当前嵌套深度
int g_entered   = 0;    // 探针 handler 被进入的总次数
int g_reentered = 0;    // 其中属于【重入】的次数

void probeHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    ++g_entered;
    if (g_depth > 0) ++g_reentered;
    fprintf(stderr, "PROBE-ENTER depth=%d type=%d msg=[%s]\n",
            g_depth, int(type), qUtf8Printable(msg));
    fflush(stderr);

    if (g_depth == 0) {
        // ★ 关键:在 handler 内部再打一条日志,看会不会再进来一次。
        ++g_depth;
        qWarning("PROBE-INNER-这条是从 handler 内部打出来的");
        --g_depth;
    }
}

} // namespace

class tst_QtReentryProbe : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void qtDoesNotReenterCustomHandler();
};

void tst_QtReentryProbe::initTestCase() {
    qInstallMessageHandler(probeHandler);
}

void tst_QtReentryProbe::qtDoesNotReenterCustomHandler() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    StderrCapture cap;
    QVERIFY2(cap.start(tmp.path() + "/stderr.txt"),
             "重定向 stderr 失败 —— 本用例验证不到任何东西");

    qWarning("PROBE-OUTER-外层这条");

    const QString err = cap.stop();

    // ---- ① 自定义 handler 【只被进入了一次】----
    //   这就是本文件的核心断言:Qt 不重入自定义 handler。
    QVERIFY2(g_entered == 1,
             qPrintable(QString(
                 "★ 自定义 handler 被进入了 %1 次(期望 1 次)★\n"
                 "  实测(Qt 6.8.3):handler 内部再打一条日志,Qt 会把它交给\n"
                 "  【默认 handler】,自定义 handler 不会被重入。\n"
                 "  现在被重入了 ⇒ Qt 改了这条语义 ⇒\n"
                 "  Logging.cpp 里那把非递归 QMutex 和重入保护的定位要重审,\n"
                 "  而且 handler 内部【绝不能】再用 qWarning/qDebug 报错。")
                 .arg(g_entered)));

    QVERIFY2(g_reentered == 0,
             qPrintable(QString("重入了 %1 次 —— 见上面那条说明。").arg(g_reentered)));

    // ---- ② 内层那条消息【没有】被丢掉,它出现在 stderr 上 ----
    //   少了这条,"没重入"有两种解释:Qt 不重入,或者 Qt 把内层消息吞了。
    //   而"吞了"会让人以为错误处理路径的报错不见了 —— 那是更糟的情况。
    QVERIFY2(err.contains("PROBE-INNER"),
             "内层消息没有出现在 stderr 上 —— 它被丢掉了?\n"
             "  (Qt 的默认 handler 会把它原样打出来,没有时间戳、没有级别、没有类别。)");

    // ---- ③ 内层那条走的是【默认 handler】:所以它没有我们的前缀 ----
    //   探针 handler 自己打的行带 "PROBE-ENTER" 前缀;
    //   内层那条【没有】这个前缀 ⇒ 它不是探针 handler 打的 ⇒ 是默认 handler 打的。
    //   ★ 这正是 Logging.cpp 里"错误不要用 qWarning 报"的真正理由:
    //     出来的是裸文本,跟别的日志行长得不一样,还不受级别过滤控制。
    const int innerLines = countOccurrences(err, "PROBE-INNER");
    QVERIFY2(innerLines == 1,
             qPrintable(QString("内层消息出现了 %1 次,期望 1 次。").arg(innerLines)));

    const bool innerHasPrefix = err.contains("PROBE-ENTER depth=1")
                             || err.contains("PROBE-ENTER depth=0 msg=[PROBE-INNER");
    QVERIFY2(!innerHasPrefix,
             "内层消息带着探针的前缀 ⇒ 它【是】被重入进来的 ⇒ "
             "Qt 改了重入语义,见上面那条说明。");
}

QTEST_MAIN(tst_QtReentryProbe)
#include "tst_QtReentryProbe.moc"

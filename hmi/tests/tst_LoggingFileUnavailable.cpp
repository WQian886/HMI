// ============================================================================
// tst_LoggingFileUnavailable —— 钉住 G-19:日志目录打不开 ⇒ 每条日志都重试一次
//
// ============================================================================
//  【这条缺陷是什么】
//    Logging.cpp 的 messageHandler 原来长这样:
//
//        if (!g_state.currentFile || today != g_state.currentDate) {
//            ...
//            g_state.currentFile = openNewLogFile();      // ← 打不开就是 nullptr
//        }
//
//    打不开时 currentFile 一直是 nullptr ⇒ 下一条消息又满足
//    "!currentFile" ⇒ 又去打开一次。于是【每一条日志】都要:
//        mkpath 一次 + open 一次(失败)+ 报错一次。
//
//  【后果】
//    · stderr 被刷爆(每条日志一行报错,一天几万行)
//    · CPU 白烧(每条日志一次 mkdir + 一次 open)
//    · 而日志文件一个字也没写进去 —— 这才是要紧的:
//      【日志没了,而且没有任何一条说"日志为什么没了"】
//    · 触发条件里有"人能主动造成的":磁盘满 / 目录无写权限 /
//      hmi.json 里 logging.directory 配错(指到一个不存在的盘)。
//
//  【★ 一条被推翻的结论,记在这里免得下一个人再走一遍】★
//    最初的结论是"qWarning 会重入 messageHandler ⇒ 二次 lock 同一把
//    非递归 QMutex ⇒ 同线程死锁 ⇒ 整个进程永久挂死",
//    并按这个结论修了半天(还加了一层"重入保护")。
//    ★ 实测推翻了它 ★(探针见 tst_QtReentryProbe.cpp):
//      Qt 6.8.3 【不会】重入自定义消息处理器 ——
//      在 handler 内部再打一条日志,Qt 会把它交给【默认 handler】,
//      自定义 handler 一次都不会被重入(实测 entered=1 / reentered=0)。
//    ⇒ 那条死锁链不成立。日志那一行的"重入保护"现在只是【保险】,
//      不是修复 —— 它的注释里也这么写着。
//
//  【本文件钉的是什么】
//    ① 打不开的时候,只【试一次】,不是每条消息试一次。
//    ② 打不开这件事【要说出来】,而且只说一次(不能刷屏),
//       而且说的话要能被认出来(固定格式)。
//    ③ 目录修好之后,日志必须能【自动恢复】。
//
//  【负向对照(必须做,否则这个测试可能是坏的)】
//    把 messageHandler 里的 fileBroken 那道闸去掉
//    ⇒ 每条消息都重试 ⇒ openAttempts 从 1 变成 101
//    ⇒ 本文件【立刻红】,而且失败信息直接写着"试了 100 次"。
//    (这个对照比"程序卡住"那种好得多:精确、快、能指出是哪条路不对。)
//
//  【为什么单独一个文件,不并进 tst_LoggingCategory】
//    tst_LoggingCategory 测的是【级别过滤】,前提是日志能正常写进文件。
//    本文件测的是【日志根本写不进去】的那条路 ——
//    两条路共用同一个 handler,但状态互斥(一个要文件能开,一个要文件开不了)。
//    放一起会让"打不开"污染"应当消失"那几条断言的前提。
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

#include <windows.h>          // CreateFileW —— 共享读正在被写的日志文件

#include "logging/Logging.h"
#include "stderr_capture.h"   // 抓 stderr 数报错行数(同目录的共享小工具)

using namespace qwhmi::logging;

class tst_LoggingFileUnavailable : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- 核心:打不开的时候只试一次,不是每条消息试一次 ----
    void logFileUnopenable_openIsAttemptedOnlyOnce();

    // ---- 后面那 100 条消息不许再去试 ----
    void logFileUnopenable_mustNotRetryForever();

    // ---- 打不开这件事必须【说出来】,而且只说一次 ----
    void logFileUnopenable_isReportedOnceInAUniformFormat();

    // ---- 目录修好之后必须能自动恢复 ----
    void afterDirectoryFixed_loggingWorksAgain();

private:
    QTemporaryDir m_dir;
    QString        m_badDir;     // 故意开不了的目录(<tmp>/blocker/logs,blocker 是个文件)
    QString        m_goodDir;    // 正常目录

    QString readLog() const;
    static void applyDir(const QString& dir);
};

QString tst_LoggingFileUnavailable::readLog() const {
    // 抄 tst_LoggingCategory 的读法(那边有长注释解释为什么必须这么读):
    //   · handler 一直开着当前日志文件,Windows 上 QFile 独占 ⇒ 普通 open 会被拒;
    //     QFile 没有"共享读"开关,所以走 Win32 CreateFileW + FILE_SHARE_*。
    //   · 必须读【所有】日志文件,不能只读最新的。
    //   · 打不开绝不能静默跳过当成"日志是空的" —— 那会让断言全部假绿。
    QDir d(m_goodDir);
    const QStringList files = d.entryList({QString("hmi_*.log")}, QDir::Files);
    if (files.isEmpty()) return QString();

    QString all;
    for (const QString& name : files) {
        const QString path = QDir::toNativeSeparators(d.filePath(name));
        HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;   // 真打不开:下面 isEmpty() 会判红
        LARGE_INTEGER size;
        if (GetFileSizeEx(h, &size) && size.QuadPart > 0) {
            const int cap = static_cast<int>(size.QuadPart);
            QByteArray buf(cap, Qt::Uninitialized);
            DWORD got = 0;
            if (ReadFile(h, buf.data(), cap, &got, nullptr)) {
                buf.resize(got);
                all += QString::fromUtf8(buf);
            }
        }
        CloseHandle(h);
    }
    return all;
}

void tst_LoggingFileUnavailable::applyDir(const QString& dir) {
    Config c;
    c.directory    = dir;
    c.defaultLevel = QtDebugMsg;   // 让所有级别的消息都能过(本文件要测的是"写不写得出")
    applyConfig(c);
}

// ---------------------------------------------------------------------------

void tst_LoggingFileUnavailable::initTestCase() {
    QVERIFY2(m_dir.isValid(), "临时目录建不出来,后面的断言都无从谈起");

    m_goodDir = m_dir.path();

    // ★ 造一个【一定开不了】的日志目录:
    //   先在 <tmp>/blocker 放一个【文件】,再让日志目录是 <tmp>/blocker/logs。
    //   mkpath 造不出"文件底下的目录" ⇒ open 必然失败。
    const QString blocker = m_dir.path() + "/blocker";
    {
        QFile f(blocker);
        QVERIFY2(f.open(QIODevice::WriteOnly),
                 "造不出那个挡路文件,后面'日志打不开'的前提就不成立");
        f.write("not a directory");
        f.close();
    }
    m_badDir = blocker + "/logs";

    // ★ 前提校验:这个目录必须真的建不出来。
    //   少了这条,万一哪天 mkpath 的行为变了(比如允许穿过文件),
    //   后面"日志打不开"的断言就变成在测另一件事 —— 而且照样是绿的。
    QVERIFY2(!QDir().mkpath(m_badDir),
             "这个目录居然建出来了 —— 说明'日志打不开'的前提不成立,本文件无效");
    QVERIFY2(!QDir(m_badDir).exists(), "同上");

    // ★ 装 handler。注意装之前【不能】有任何消息走进来,
    //   否则第一次打开尝试会被别的消息消耗掉,后面的计数断言就飘了。
    applyDir(m_badDir);
    registerCategories();
    resetLoggingDiagnostics();
}

void tst_LoggingFileUnavailable::cleanupTestCase() {
    Config c;                      // 交还给进程默认配置
    applyConfig(c);
}

// ---------------------------------------------------------------------------

void tst_LoggingFileUnavailable::logFileUnopenable_openIsAttemptedOnlyOnce() {
    resetLoggingDiagnostics();

    // 这条消息会让 handler 尝试打开 <badDir>/hmi_*.log ⇒ 失败。
    qCWarning(hmiApp) << "G19-应当让日志打开失败";

    const Diagnostics d = loggingDiagnostics();

    QVERIFY2(d.openAttempts == 1,
             qPrintable(QString(
                 "★ 尝试打开日志文件的次数 = %1,期望 1 ★\n"
                 "  1 次 = 试一次、记下来、之后不再试(正确)。\n"
                 "  >1 次 = 每条消息都在重试(一次 mkpath + 一次 open 失败,\n"
                 "  每条日志一遍)⇒ stderr 被刷爆、CPU 白烧,而日志一个字也没写进去。")
                 .arg(d.openAttempts)));

    // ★ 顺带钉一下:没有发生重入。
    //   实测(Qt 6.8.3)Qt 不会重入自定义 handler,所以这个计数正常恒为 0。
    //   它是哨兵:哪天 Qt 改了语义,这里会先红。
    QVERIFY2(d.reentryCount == 0,
             qPrintable(QString(
                 "★ messageHandler 被重入了 %1 次 ★\n"
                 "  实测 Qt 6.8.3 不会重入自定义 handler(探针:tst_QtReentryProbe)。\n"
                 "  这里不为 0,说明要么 Qt 改了语义,要么我们自己在 handler 里\n"
                 "  调了会打日志的东西 —— 后者会撞上同一把非递归 QMutex。")
                 .arg(d.reentryCount)));
}

void tst_LoggingFileUnavailable::logFileUnopenable_mustNotRetryForever() {
    // ★ 打不开之后,后面这 100 条消息【不许】再去试打开。
    //   少了这条,上面那条"openAttempts == 1"可能是因为
    //   "第一条消息之后测试就结束了"才碰巧成立。
    const Diagnostics before = loggingDiagnostics();

    for (int i = 0; i < 100; ++i) {
        qCDebug(hmiPoll) << "G19-STORM-" << i;
    }

    const Diagnostics after = loggingDiagnostics();
    const int attempts = after.openAttempts - before.openAttempts;

    QVERIFY2(attempts == 0,
             qPrintable(QString(
                 "★ 打不开之后又去试了 %1 次 ★\n"
                 "  日志文件打不开时,每条消息都重试一次的话:每条日志一遍 mkpath +\n"
                 "  一遍 open 失败 + 一遍 stderr 报错 ⇒ CPU 和 stderr 都被刷爆,\n"
                 "  而日志一个字也没写进去。")
                 .arg(attempts)));

    QVERIFY2(after.reentryCount == before.reentryCount,
             "这 100 条消息里又发生了重入 —— 同一个错误处理路径不止一处");
}

void tst_LoggingFileUnavailable::logFileUnopenable_isReportedOnceInAUniformFormat() {
    // ★ 这一条钉的是【操作员有没有被告知】,以及【被告知了几次】。
    //   只看计数器只能证明"代码跑过了",证明不了"话说出来了" ——
    //   而"日志没了却没人知道"正是这条缺陷最要紧的地方。
    //
    //   现场看日志的人要能一眼认出这两行:
    //     Logging: failed to open <路径>: <原因>     ← 底层原因
    //     Logging: 日志文件打不开,本会话【只写 stderr】… ← 影响与出路
    StderrCapture cap;
    QVERIFY2(cap.start(m_dir.path() + "/stderr.txt"),
             "重定向 stderr 失败 —— 本用例验证不到任何东西");

    // ★ 把"打不开"的状态【重新装弹】,让这次失败发生在抓取窗口【里面】。
    //
    //   少了这一步,本用例会【假红】:第一次失败早在 initTestCase /
    //   上一个用例里就已经发生过了(那两条报错打在窗口之前),
    //   窗口里一条都没有 ⇒ "报了 1 次"数出来是 0 次。
    //   —— 而它会【看起来】像是"根本没报错",把结论引到反方向去。
    //
    //   applyDir 走的是 applyConfig,那里会把 fileBroken 复位
    //   (换配置 = 可能换目录 ⇒ 之前"打不开"的状态作废)。
    applyDir(m_badDir);
    resetLoggingDiagnostics();

    for (int i = 0; i < 5; ++i) {
        qCWarning(hmiApp) << "G19-REPORT-" << i;
    }
    const QString err = cap.stop();

    // ① 底层原因:必须说出来,而且【只说一次】
    const int openErr = countOccurrences(err, "Logging: failed to open");
    QVERIFY2(openErr == 1,
             qPrintable(QString(
                 "『failed to open』出现了 %1 次,期望 1 次。\n"
                 "  0 次 = 操作员根本不知道日志没了(最糟:日志静默全丢)。\n"
                 "  >1 次 = 刷屏(每条日志一遍)。").arg(openErr)));

    // ② 影响与出路:也必须说,而且【只说一次】
    const int notice = countOccurrences(err, "Logging: 日志文件打不开");
    QVERIFY2(notice == 1,
             qPrintable(QString(
                 "『日志文件打不开』这条提示出现了 %1 次,期望 1 次。\n"
                 "  它要回答的是【出了什么事、现在怎么办】——\n"
                 "  只报底层错误不报影响,现场看到的人仍然不知道日志已经没了。").arg(notice)));

    // ③ 只试了一次 ⇒ 上面两行自然也只出现一次
    const Diagnostics d = loggingDiagnostics();
    QVERIFY2(d.openAttempts == 1,
             qPrintable(QString("尝试打开 %1 次,期望 1 次。").arg(d.openAttempts)));
}

void tst_LoggingFileUnavailable::afterDirectoryFixed_loggingWorksAgain() {
    // ★ 降级不是终态:把目录改回能开的,日志必须自己恢复。
    //   少了这条,"打不开就不写了"这种处理会被当成"正常"——
    //   而现场表现是"日志永远停在某一行",没有任何提示说为什么。
    applyDir(m_goodDir);

    qCDebug(hmiApp) << "G19-RECOVER-应当出现在日志文件里";

    const QString log = readLog();
    QVERIFY2(!log.isEmpty(),
             "换回正常目录之后,日志文件里还是什么都没有 —— "
             "handler 没有从'打不开'的状态里恢复过来");

    QVERIFY2(log.contains("G19-RECOVER"),
             "换回正常目录之后这条消息没落盘 —— 恢复逻辑不对");

    // ★ 反向对照:换了目录之后【不该】再出现打不开的报错。
    //   (打不开的目录在 m_badDir,这里读的是 m_goodDir,两处不冲突。)
    QVERIFY2(!log.contains("G19-应当让日志打开失败"),
             "这条消息本该只出现在 stderr(日志当时打不开),不该落进文件 —— "
             "说明它落盘的位置不对,后面那条断言可能是假绿");
}

QTEST_MAIN(tst_LoggingFileUnavailable)
#include "tst_LoggingFileUnavailable.moc"

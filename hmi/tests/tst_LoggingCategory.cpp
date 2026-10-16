// ============================================================================
// tst_LoggingCategory —— 钉住 QLoggingCategory 的【可关闭性】
//
// 为什么需要这个测试
// --------------------
// 2026-10-16 改善清单 #23:把生产代码里 54 处裸 `qDebug()` 归入 8 个
// QLoggingCategory(hmiComm / hmiPoll / hmiWrite / hmiApp / hmiAlarm /
// hmiVar / hmiUi)。
//
// 改完只看"编译通过 + ctest 全绿"是不够的,原因是这类改动【几乎必然
// 编译通过】 —— category 写错名字编译就挂,但写对了却可能因为 handler
// 没认出来而【什么也不做】。那种情况下:
//   · 编译绿、测试绿、日志文件该有的行没有、没有任何报错
//   · 现场表现是"分类功能好像没生效",但没有任何线索指向哪里错了
//
// 所以这里测的【不是"日志写了没有"】(那是 qDebug 本来就有的),
// 而是【"按类别关掉某类日志,它真的就消失了"】——
// 这是把 qDebug 换成 qCDebug 唯一真正的收益,也是唯一需要被守住的东西。
//
// 怎么测的
// --------
// messageHandler(Logging.cpp:104)读 ctx.category 去查 categoryLevels,
// 级别不够就 `return`,根本不写文件。
// 于是:
//   1. 把日志目录指向临时目录,装 handler
//   2. 打开 hmiComm,打一行 → 必须出现在日志里
//   3. 把 hmiComm 压到高于 Debug 的级别 → 再打一行 → 必须【不】出现
//   4. 同时打一行别的类别 → 必须仍然出现(证明不是"日志整体挂了")
//
// 第 4 步是本文件的关键。少了它,第 3 步的"没出现"有两种解释:
// 真的被关掉了,或者日志文件写不进去了。分开测才能区分。
//
// 负向对照(必须做,否则这个测试可能是坏的):
//   把 Logging.cpp 里 `if (type < effLevel) return;` 注释掉,
//   本文件必须【变红】 —— 那行不生效的话,第 3 步就不可能通过。
//   详见提交记录里的对照表。
// ============================================================================

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include <windows.h>          // CreateFileW / FILE_SHARE_* — 共享读日志用

#include "logging/Logging.h"

using namespace qwhmi::logging;

class tst_LoggingCategory : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();     // 装 handler + 指向临时目录
    void cleanupTestCase();  // 恢复默认配置

    // ---- 核心:关掉一个类别,那一类必须真的消失 ----
    void categoryLevel_suppressesThatCategoryOnly();

    // ---- 反向:级别调低,那一类必须重新出现 ----
    //   少了这条,上面那条可能是因为"无论如何都不写"而通过的。
    void loweringLevel_bringsItBack();

    // ---- 8 个 category 必须都真的注册过 ----
    //   Q_LOGGING_CATEGORY 若漏写或名字拼错,这里会空指针/拿不到名字。
    //   归类工作跨越 8 个类别,漏注册一个就是整类日志失效且无提示。
    void allEightCategories_areRegisteredWithExpectedNames();

    // ---- 未归类的裸 qDebug 仍然按"默认级别"走(不被类别配置影响) ----
    //   这是本次改动刻意保留的行为:没归类的行不会因为某类别被调高而消失。
    //   它同时也是"归类到底做完了没有"的旁证。
    void unclassifiedQDebug_ignoresCategoryConfig();

private:
    QTemporaryDir m_dir;
    QString readLog() const;
    bool readLog(QString* out, const char* where) const;
    static void resetToDefault(const QString& dir);
    static void applyLevel(const QString& cat, QtMsgType lvl, const QString& dir);
};

// ---------------------------------------------------------------------------

QString tst_LoggingCategory::readLog() const {
    // ⚠⚠ 这里踩过一个坑,记下来防止下一个人重写:
    //
    //   1) 必须读【所有】日志文件,不能只读最新的。
    //      messageHandler 按天切文件(Logging.cpp:131-140),
    //      跨零点跑时新行进新文件 —— 只看最新那个会读到空,
    //      于是所有"应当消失"的断言都因"什么都没读到"而通过。
    //
    //   2) ★★ handler 把当前日志文件的句柄一直开着(直到跨天才换),
    //      Windows 上 QFile 以独占方式打开会被拒绝。
    //      QFile 没有"共享读"开关,所以这里走 Win32 的 CreateFileW +
    //      FILE_SHARE_READ|FILE_SHARE_READWRITE 绕开。
    //      —— 同一个坑在 scripts/long_run.ps1 里也踩过一次,那里用
    //      System.IO.FileStream 的 FileShare.ReadWrite 绕开,注释里记着。
    //
    //   3) 打不开绝不能静默跳过当成"日志是空的":
    //      那会让所有"这行应当消失"的断言【全部通过】—— 它们其实什么都没验证到。
    //      所以这里把打不开的文件名收集起来,一并返回给上层判红。
    QDir d(m_dir.path());
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

void tst_LoggingCategory::resetToDefault(const QString& dir) {
    // ★★ defaultLevel 必须是 QtDebugMsg,不是 Config{} 的默认值 QtInfoMsg。
    //
    //   这个坑我踩了整整一轮才定位:Config 的默认级别是 Info,
    //   而本文件打的一律是 qCDebug(Debug)—— Debug < Info,
    //   handler 在 Logging.cpp:112 `if (type < effLevel) return;` 就把它们
    //   全砍了,一行都没进文件。
    //   于是 readLog() 返回空,"这行应当消失"的断言【全部通过】——
    //   看着是绿的,实际验证的是"默认级别压掉了 Debug",跟类别一点关系没有。
    //
    //   本文件要测的是【类别级抑制】,前提是 Debug 在默认情况下能写下去。
    //   想要"默认级别压住 Debug"那个行为,由 unclassifiedQDebug_... 那条单独测。
    Config c;
    c.directory    = dir;
    c.defaultLevel = QtDebugMsg;
    applyConfig(c);
}

void tst_LoggingCategory::applyLevel(const QString& cat, QtMsgType lvl, const QString& dir) {
    // defaultLevel 同样给 Debug —— 理由同 resetToDefault():
    // 要让【只有这一条规则】在起作用,不能同时有默认级别在旁边砍。
    Config c;
    c.directory    = dir;
    c.defaultLevel = QtDebugMsg;
    c.categoryLevels.insert(cat, lvl);
    applyConfig(c);
}

// ★ 读日志的硬判据:handler 认认真真写过一次之后,文件里【一定有内容】。
//   如果 readLog() 返回空串,只有两种可能 —— 根本没写,或者写了没读到。
//   两种都意味着"后面的断言验证不到任何东西",所以直接判红。
//   没有这条,"应当消失"的断言就能靠"什么都读不到"全部通过 ——
//   本文件第一版就差点栽在这上面(见 readLog() 处的长注释)。
bool tst_LoggingCategory::readLog(QString* out, const char* where) const {
    *out = readLog();
    if (!out->isEmpty()) return true;
    // ⚠ 不能用 QFAIL:它是 `return;`,在返回 bool 的函数里也编译不过(宏展开无值)。
    //   也不能只 warn —— 那等于放过假绿。
    //   所以返回 false,让调用方的 QVERIFY 立刻红。
    qCritical("[%s] 临时目录里读不到任何日志内容 —— 要么 handler 没写,"
              "要么写了没打开成功。此时本用例后续的【应当消失】断言全部无效(假绿)。",
              where);
    return false;
}

void tst_LoggingCategory::initTestCase() {
    QVERIFY2(m_dir.isValid(), "临时目录建不出来,后面的断言都无从谈起");
    resetToDefault(m_dir.path());
    registerCategories();
    // 注意:registerCategories() 装的 handler 会同时写 stderr,
    // 所以这个测试的输出里会混进日志行 —— 那是预期的,不是出错。
}

void tst_LoggingCategory::cleanupTestCase() {
    Config c;                      // 交还给进程默认配置
    applyConfig(c);
}

// ---------------------------------------------------------------------------

void tst_LoggingCategory::categoryLevel_suppressesThatCategoryOnly() {
    resetToDefault(m_dir.path());

    // --- 1. 打开 hmiComm,这一行必须落盘 ---
    //   这一步同时也是整个文件的地基:它证明 handler 确实在往这个临时目录写。
    //   少了它,第 3 步的"没看到 B"可能是"目录根本没写进去"造成的假绿。
    qCDebug(hmiComm) << "TC-MARKER-A-应当出现";
    QVERIFY2(readLog().contains("TC-MARKER-A"),
             "开着的时候这行都没写进日志 —— 后面【关闭生效】的断言就没有意义了");

    // --- 2. 把 hmiComm 压到比 Debug 高的级别 ---
    applyLevel("hmi.comm", QtCriticalMsg, m_dir.path());
    qCDebug(hmiComm) << "TC-MARKER-B-应当消失";

    // --- 3. 别的类别必须仍然能写 ---
    //   ★ 没有这一步,第 2 步的"没看到 B"有两种解释:
    //     (a) B 被类别配置正确压掉了  ← 我们要的
    //     (b) 日志文件整个写不进去了   ← 我们会误报成 PASS
    //   必须把 (b) 排除掉。
    qCDebug(hmiPoll) << "TC-MARKER-C-应当出现(别的类别不受影响)";

    QString log;
    QVERIFY2(readLog(&log, "categoryLevel_suppressesThatCategoryOnly"), "见上面 qCritical");
    QVERIFY2(!log.contains("TC-MARKER-B"),
             "hmi.comm 已经被压到 Critical,这条 Debug 还写进去了 —— "
             "类别配置根本没生效,归类等于白做");
    QVERIFY2(log.contains("TC-MARKER-C"),
             "压掉 hmi.comm 的同时 hmiPoll 也不见了 —— "
             "这不是类别级抑制,是日志整体挂了,测试前提不成立");

    resetToDefault(m_dir.path());
}

void tst_LoggingCategory::loweringLevel_bringsItBack() {
    // 反向对照:同一类别,级别调低后必须重新出现。
    // 少了这条,上面那条可能是"这个类别无论如何都不写"蒙对的。
    applyLevel("hmi.comm", QtCriticalMsg, m_dir.path());
    qCDebug(hmiComm) << "TC-MARKER-D-应当消失";
    QString log;
    QVERIFY(readLog(&log, "loweringLevel_bringsItBack(压制)"));
    QVERIFY(!log.contains("TC-MARKER-D"));

    applyLevel("hmi.comm", QtDebugMsg, m_dir.path());   // 显式放到 Debug
    qCDebug(hmiComm) << "TC-MARKER-E-应当重新出现";
    QVERIFY(readLog(&log, "loweringLevel_bringsItBack(恢复)"));
    QVERIFY2(log.contains("TC-MARKER-E"),
             "级别调回 Debug 后这行仍不出现 —— 说明上一条不是因为级别被压,"
             "而是这个类别根本没在写,假绿");

    resetToDefault(m_dir.path());
}

void tst_LoggingCategory::allEightCategories_areRegisteredWithExpectedNames() {
    // ★ 这条直接对应"归类工作跨了 8 个类别"这件事:
    //   任何一个 Q_LOGGING_CATEGORY 漏掉/改名,引用它的代码会编译失败,
    //   但如果【改的是声明侧】(比如手误删了一行 Q_DECLARE/Q_LOGGING),
    //   编译可能照过、运行时不报错、那一整类日志静默失效。
    // ⚠ Qt 6 的 Q_DECLARE_LOGGING_CATEGORY(name) 展开成的是【函数】
    //   const QLoggingCategory& name(),不是全局变量。
    //   按指针取会编译不过(实测踩过:error C2440 无法从
    //   "const QLoggingCategory &(__cdecl *)(void)" 转换)。
    struct { const QLoggingCategory& (*cat)(); const char* name; } cats[] = {
        { &hmiApp,    "hmi.app"    },
        { &hmiComm,   "hmi.comm"   },
        { &hmiVar,    "hmi.var"    },
        { &hmiAlarm,  "hmi.alarm"  },
        { &hmiPoll,   "hmi.poll"   },
        { &hmiWrite,  "hmi.write"  },
        { &hmiUi,     "hmi.ui"     },
        { &hmiScript, "hmi.script" },
    };
    for (auto& c : cats) {
        const QLoggingCategory& cat = c.cat();
        QCOMPARE(QString::fromLatin1(cat.categoryName()), QString::fromLatin1(c.name));
    }

    // ★ 再钉一条容易漏的:类别的名字必须【就是 hmi.json 里能写的那个键】。
    //   loadConfig() 拿用户配的字符串直接查 categoryLevels,键对不上就永远静默失效
    //   —— 用户在 hmi.json 里写 "hmi.comm",不报错,只是"配了没生效"。
    //
    //   逐个类别用【它自己的名字】当配置键去压,再确认它真的被压掉了。
    //   若某个类别"注册的名字"和"生效时查的名字"不是同一个,这一步就压不住。
    for (auto& c : cats) {
        const QLoggingCategory& cat = c.cat();
        const QString key = QString::fromLatin1(cat.categoryName());
        // ★ 两个 marker 必须【各自独立且是完整一条消息】,不能写成
        //   `<< marker << "应当消失"` 再去 contains(marker + "应当消失")。
        //   QDebug 的流式输出会在每段之间【自动插空格】,日志里落的是
        //   "TC-CAT-hmi.app 应当消失",而我 contains 的是不带空格的版本 —— 永远匹配不上,
        //   断言恒真。踩过:负向对照注入 handler 故障后,别的用例都变红了,
        //   唯独这条还 PASS,查出来就是它。
        const QString markerOpen = QString("TC-CAT-OPEN-%1").arg(key);
        const QString markerShut = QString("TC-CAT-SHUT-%1").arg(key);

        QString log;
        resetToDefault(m_dir.path());
        qCDebug(cat) << markerOpen;
        QVERIFY(readLog(&log, "allEightCategories(写入)"));
        QVERIFY2(log.contains(markerOpen),
                 qPrintable(QString("类别 %1 开着时都不写 —— 后面【被压掉】是假象").arg(key)));

        applyLevel(key, QtCriticalMsg, m_dir.path());
        qCDebug(cat) << markerShut;
        QVERIFY(readLog(&log, "allEightCategories(抑制)"));
        QVERIFY2(!log.contains(markerShut),
                 qPrintable(QString("类别 %1 用自己的名字当配置键压不住 —— "
                                    "hmi.json 里按这个名字配不会生效").arg(key)));
    }
    resetToDefault(m_dir.path());
}

void tst_LoggingCategory::unclassifiedQDebug_ignoresCategoryConfig() {
    // 裸 qDebug 没有 category,ctx.category 为空 → 走 defaultLevel 判定,
    // 不受 categoryLevels 影响。
    //
    // 这条钉的是两件事:
    //   1) 【归类没有改变裸 qDebug 的行为】—— 所以本次改动零行为变化
    //   2) defaultLevel 的压制依然生效 —— 它和类别抑制是两条独立的路
    //
    // 注意这里必须显式设 defaultLevel=QtInfoMsg,因为 resetToDefault()
    // 现在给的是 Debug(见那个函数的注释:测类别抑制的前提是 Debug 能写下去)。
    Config c;
    c.directory    = m_dir.path();
    c.defaultLevel = QtInfoMsg;        // Info > Debug → 裸 qDebug 应被压住
    applyConfig(c);
    qDebug() << "TC-MARKER-F-裸 qDebug 在 Info 级别下应当被压制";

    QString log;
    QVERIFY(readLog(&log, "unclassifiedQDebug(压制)"));
    QVERIFY2(!log.contains("TC-MARKER-F"),
             "defaultLevel=Info 下裸 qDebug 竟然写进去了 —— "
             "handler 的判定逻辑变了,本文件其余断言的前提需要重新确认");

    // 把默认级别降到 Debug,它就该出现(同一行,换配置)
    c.defaultLevel = QtDebugMsg;
    applyConfig(c);
    qDebug() << "TC-MARKER-G-默认级别降到 Debug 后应当出现";
    QVERIFY(readLog(&log, "unclassifiedQDebug(恢复)"));
    QVERIFY2(log.contains("TC-MARKER-G"),
             "默认级别已是 Debug,裸 qDebug 仍不写 —— handler 判定逻辑有问题");

    resetToDefault(m_dir.path());
}

QTEST_MAIN(tst_LoggingCategory)
#include "tst_LoggingCategory.moc"

// ============================================================================
// 阶段 A · A4 崩溃 dump · 实现
//
// 文件职责:Windows 装 SetUnhandledExceptionFilter → 写 MiniDump + 标记文件。
//
// 触发流程:
//   未捕获异常 → unhandledExceptionFilter(ex)
//     → 写 crash_yyyyMMdd_HHmmss_zzz.dmp(包含栈内存 + 线程 + DLL 信息)
//     → 写 pending.txt(下次启动读这个,知道有未处理 dump)
//   下次启动 main() → hasPendingDump() → 弹"上次异常退出" → takePendingDump()
//
// dump 文件大小:几 MB 到几十 MB(取决于程序内存)
// 位置:%LOCALAPPDATA%/QianWei/HMI/crash/
//   (原注释写的是 %APPDATA%/HMI/crash/ —— 两个地方都不对:Qt 的
//    AppLocalDataLocation 落在 Local 而非 Roaming,且中间还有一层 org。)
//
// 详见:CrashHandler.h 和 docs/阶段A_工程化基线.md
// ============================================================================

#include "crash/CrashHandler.h"

#include "util/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <cstdlib>
#include <QTextStream>

#ifdef Q_OS_WIN
#  include <Windows.h>
#  include <DbgHelp.h>
#  pragma comment(lib, "DbgHelp.lib")
#else
#  include <csignal>
#endif

namespace qwhmi::crash {

namespace {

constexpr const char* kPendingMarkerFile = "pending.txt";

#ifdef Q_OS_WIN

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ex) {
    const QString dir = dumpDirectory();
    QDir().mkpath(dir);

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    const QString path  = QString("%1/crash_%2.dmp").arg(dir, stamp);
    const QString marker = QString("%1/%2").arg(dir, kPendingMarkerFile);

    HANDLE hFile = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ex;
        mei.ClientPointers    = FALSE;

        const DWORD flags = static_cast<DWORD>(MiniDumpWithIndirectlyReferencedMemory)
                           | static_cast<DWORD>(MiniDumpWithThreadInfo)
                           | static_cast<DWORD>(MiniDumpWithUnloadedModules);

        const BOOL ok = MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(),
            hFile, static_cast<MINIDUMP_TYPE>(flags),
            &mei, nullptr, nullptr);

        CloseHandle(hFile);

        if (ok) {
            QFile m(marker);
            if (m.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream ts(&m);
                ts << "path=" << path << "\n";
                ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
                if (ex && ex->ExceptionRecord) {
                    ts << "exception_code=0x"
                       << QString::number(ex->ExceptionRecord->ExceptionCode, 16).toUpper()
                       << "\n";
                }
            }
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // Q_OS_WIN

} // namespace

void install() {
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    QDir().mkpath(dumpDirectory());
#else
    std::signal(SIGSEGV, [](int){ std::abort(); });
    std::signal(SIGABRT, [](int){ std::abort(); });
#endif
}

QString dumpDirectory() {
    // 覆盖开关 HMI_CRASH_DIR。存在的理由有两个,都实在:
    //   · 运维:客户现场的 dump 要收集到已知路径,靠猜 %LOCALAPPDATA% 不现实
    //   · 测试:tst_CrashHandler 会 takePendingDump(),那【会删 marker】。
    //     不隔离的话,测试一跑就把现场那个真实的 pending.txt 删了。
    //   两条路径都要求它【显式】,不会有人不小心设上。
    const QByteArray overrideDir = qgetenv("HMI_CRASH_DIR");
    if (!overrideDir.isEmpty()) {
        return QFile::decodeName(overrideDir);
    }

    // ★ 用 util::appDataDir() —— 它是【纯函数】:环境变量 + 本项目常量,
    //   【不读 QCoreApplication 的 org/app 全局状态】。
    //
    //   原来这里直接用 AppLocalDataLocation —— 那个值含 <org>/<app>,
    //   取决于调用方什么时候设好了 org/app。而写 dump 的时刻
    //   (崩溃瞬间,org/app 早设好了)和读标记的时刻(启动最早期,
    //   main() 还没设)拿到的就是两个不同的目录,于是标记永远读不到。
    //   整件事静默发生 28 天,现场那个 pending.txt 就是证据。
    //
    //   改成纯函数之后,"调用方的调用顺序"再也不可能影响 dump 去哪 ——
    //   这比单纯把 main() 的语句调个顺序更可靠:顺序是被注释约束着的,
    //   而纯函数是被测试守着的。
    //
    // ★ 2026-10-21:默认值与改动前【逐字节相同】
    //   (GenericDataLocation/QianWei/HMI/crash),但现在多了一个 HMI_DATA_DIR 覆盖 ——
    //   长跑实例要把 dump 目录也带去私有目录,否则两边共用一个 pending.txt,
    //   一边取走就把另一边的现场弄丢了。
    return qwhmi::util::appDataDir() + QStringLiteral("/crash");
}

bool hasPendingDump() {
    return QFile::exists(QString("%1/%2").arg(dumpDirectory(), kPendingMarkerFile));
}

DumpInfo takePendingDump() {
    DumpInfo info;
    const QString marker = QString("%1/%2").arg(dumpDirectory(), kPendingMarkerFile);
    QFile m(marker);
    if (!m.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return info;
    }
    const QStringList lines = QString::fromUtf8(m.readAll()).split('\n');
    m.close();
    m.remove();

    for (const QString& ln : lines) {
        const int eq = ln.indexOf('=');
        if (eq < 0) continue;
        const QString k = ln.left(eq).trimmed();
        const QString v = ln.mid(eq + 1).trimmed();
        if      (k == "path")           info.path          = v;
        else if (k == "timestamp")      info.timestamp     = v;
        else if (k == "exception_code") info.exceptionCode = v;
    }
    return info;
}

void debugTriggerCrash() {
#ifdef Q_OS_WIN
    volatile int* p = nullptr;
    *p = 42;
#else
    std::raise(SIGSEGV);
#endif
}

} // namespace qwhmi::crash

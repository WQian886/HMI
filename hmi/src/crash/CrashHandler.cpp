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
// dump 文件大小:几 MB 到几十 MB(取决于程序内存);位置 %APPDATA%/HMI/crash/
//
// 详见:CrashHandler.h 和 docs/阶段A_工程化基线.md
// ============================================================================

#include "crash/CrashHandler.h"

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
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/crash");
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

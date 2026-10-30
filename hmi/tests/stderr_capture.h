// ============================================================================
// stderr_capture.h —— 测试用:把 stderr 临时重定向到文件,事后读回来
//
// 【为什么需要】
//   有些行为只能从 stderr 上看出来:比如"错误到底报了几遍"。
//   计数器能证明"代码跑过了",证明不了"话说出来了、说了几遍"。
//   断言"操作员被告知了几次"就得真的把 stderr 抓下来数。
//
// 【为什么用 _dup2 而不是 freopen】
//   freopen 之后没法干净地还原(stderr 会被换成新文件,原句柄丢了)。
//   _dup2 是【文件描述符级别】的:dup2(新fd, 原stderr的fd) 即可还原。
//
// 【平台】
//   MSVC/Windows only(用了 <io.h> 的 _dup/_dup2/_open)。
//   本仓库只在 Windows 上构建(CI 是 windows-latest),所以不用做跨平台分支。
//
// 【用法】
//   StderrCapture cap;
//   QVERIFY(cap.start(path));
//   ... 触发一些会打日志的动作 ...
//   const QString err = cap.stop();
//   // err 里是这段时间 stderr 的全部内容
// ============================================================================

#pragma once

#include <QDir>
#include <QFile>
#include <QString>

#include <io.h>      // _dup / _dup2 / _open
#include <fcntl.h>
#include <sys/stat.h>

#include <cstdio>

class StderrCapture {
public:
    StderrCapture() = default;
    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    bool start(const QString& path) {
        m_path = path;
        fflush(stderr);
        m_saved = _dup(_fileno(stderr));
        if (m_saved < 0) return false;
        const int fd = _open(qPrintable(QDir::toNativeSeparators(path)),
                             _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                             _S_IREAD | _S_IWRITE);
        if (fd < 0) { _close(m_saved); m_saved = -1; return false; }
        m_fd = fd;
        if (_dup2(fd, _fileno(stderr)) < 0) {
            _close(fd); _close(m_saved);
            m_saved = -1; m_fd = -1;
            return false;
        }
        m_ok = true;
        return true;
    }

    QString stop() {
        if (!m_ok) return QString();
        fflush(stderr);
        _dup2(m_saved, _fileno(stderr));
        _close(m_saved);
        _close(m_fd);
        m_saved = -1; m_fd = -1; m_ok = false;
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll());
    }

private:
    QString m_path;
    int  m_saved = -1;
    int  m_fd    = -1;
    bool m_ok    = false;
};

// 数一个子串在文本里出现了几次
inline int countOccurrences(const QString& haystack, const QString& needle) {
    if (needle.isEmpty()) return 0;
    int n = 0, at = 0;
    while ((at = haystack.indexOf(needle, at)) >= 0) { ++n; at += needle.size(); }
    return n;
}

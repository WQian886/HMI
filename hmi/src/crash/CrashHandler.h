// ============================================================================
// 阶段 A · A4 崩溃 dump · 头文件
//
// 文件职责:声明 dump 安装函数 + DumpInfo 结构 + 检测/读取 API。
//
// 为什么需要:HMI 7×24 跑在客户工厂,崩了要能复现分析。MiniDumpWriteDump
//   写 .dmp 文件,VS 可打开看调用栈;下次启动弹"上次异常退出"对话框。
//
// 在程序中的位置:
//   - main() 第一行:crash::install() —— 任何 Qt 代码之前
//   - QApplication 之前:crash::hasPendingDump() → 弹对话框 → takePendingDump()
//
// 关键 API:install() / hasPendingDump() / takePendingDump() / dumpDirectory()
// 平台:Windows 完整实现(MiniDumpWriteDump + SetUnhandledExceptionFilter);
//       其他平台只注册 SIGSEGV/SIGABRT 信号(简化版,不写 dump)
// 详见:docs/阶段A_工程化基线.md "A4 崩溃 dump" 节
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::crash {

struct DumpInfo {
    QString path;
    QString timestamp;
    QString exceptionCode;
};

void install();
QString dumpDirectory();
bool hasPendingDump();
DumpInfo takePendingDump();

// 仅 Debug 构建:主动触发崩溃,测试用
void debugTriggerCrash();

} // namespace qwhmi::crash

// ============================================================================
// 阶段 A · A4 崩溃 dump · 头文件
//
// 文件职责:声明 dump 安装函数 + DumpInfo 结构 + 检测/读取 API。
//
// 为什么需要:HMI 7×24 跑在客户工厂,崩了要能复现分析。MiniDumpWriteDump
//   写 .dmp 文件,VS 可打开看调用栈;下次启动弹"上次异常退出"对话框。
//
// ★ 2026-10-14 修:两条路径错位,pending.txt 静默失效 28 天
//   ------------------------------------------------------------------
//   dump 目录原本用 QStandardPaths::AppLocalDataLocation 算,而它是
//   AppData/Local/<org>/<app> —— org/app 来自 setOrganizationName /
//   setApplicationName。于是:
//     · 崩溃发生的那一瞬间(org/app 早就设好了)→ 写到 QianWei/HMI/crash/
//     · 启动时检查标记(那时 main() 还没设 org/app)→ 去 AppData/Local/crash/ 找
//   写的人和读的人看两个不同的目录。marker 写得出来,永远读不到,
//   全程零报错。现场那个 pending.txt 就这么停了 28 天。
//
//   教训写在这里,免得下次有人"顺手"把 dumpDirectory() 改回读全局状态:
//     ★ dump 路径必须是【环境变量 + 本文件常量】的纯函数,
//       绝不能依赖调用方在什么时机设了什么全局状态。
//       因为"调用方什么时候设的"只要会漂移,漂移的后果就是
//       崩了却没留下任何可分析的现场 —— 而那是唯一能救命的时刻。
//
// 在程序中的位置:
//   - main() 早期:crash::install() —— 越早越好,但必须在 Qt 可视组件之前
//   - QApplication 【之后】:hasPendingDump() → 弹对话框 → takePendingDump()
//     (QMessageBox 是 QWidget,没有 QApplication 实例就构造会直接 abort)
//
// 关键 API:install() / hasPendingDump() / takePendingDump() / dumpDirectory()
// 平台:Windows 完整实现(MiniDumpWriteDump + SetUnhandledExceptionFilter);
//       其他平台只注册 SIGSEGV/SIGABRT 信号(简化版,不写 dump)
// 详见:docs/阶段A_工程化基线.md "A4 崩溃 dump" 节
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::crash {

// ★ 2026-10-21:应用身份(kOrgName / kAppName)搬到了 util/Paths.h。
//   原因:数据目录(util::appDataDir)现在也要用这两个值,
//   而它们原先住在这个"只管崩溃"的模块里 ——
//   一个全局身份常量放在业务模块里,迟早被改散。
//   main() 改成从 util::Paths.h 取,本模块用 util::appDataDir() 算目录。

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

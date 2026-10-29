// ============================================================================
// util/Paths.h —— 应用数据目录:全应用唯一的"东西放哪"答案
//
// 文件职责:给日志 / history.db / users.json / 审计日志 / 崩溃 dump
//   提供【同一个】目录,外加一个显式的覆盖开关。
//
// ★ 2026-10-21 新增 —— 为什么需要它
//   ------------------------------------------------------------------
//   一次长跑要跟"已经在跑的另一个实例"并存时,两边必须各写各的:
//     · history.db 是 SQLite —— 两个进程同时写会互相锁死
//     · 审计日志按天命名(audit-YYYY-MM-DD.log)—— 第二个实例一开就把它截断
//     · 崩溃标记 pending.txt —— 一边取走,另一边就丢现场
//   而【改 LOCALAPPDATA 环境变量是【没用的】】——
//   Qt 在 Windows 上用 SHGetKnownFolderPath 解析 LocalAppData,
//   那个 API 认的是"当前登录用户的真实路径",不认环境变量。
//   实测(2026-10-21):把 LOCALAPPDATA 指到别处再起一个实例,
//   它照样把日志写进了真实的 %LOCALAPPDATA%\QianWei\HMI\,
//   还把同名的 audit-2026-10-21.log 截断了 ——
//   正在跑的那一轮的审计记录当场没了。
//   ⇒ 隔离只能靠应用自己认一个开关。
//
//   这个开关跟单实例键(HMI_INSTANCE_KEY)是同一条思路:
//   上一轮已经证明"换一把锁就能共存",现在补上"换一份数据目录就能不打架"。
//
// 【安全边界】没设 HMI_DATA_DIR 时,算出来的路径与改动前【逐字节相同】。
// ============================================================================

#pragma once

#include <QString>

namespace qwhmi::util {

// ★ 应用身份的唯一出处。
//   main() 用这两个去设 org/app,Paths 用它们算数据目录 ——
//   同一份值,不许各写各的,否则两边一漂移就又回到"写一个目录读另一个"。
//   (原先这两个常量在 crash/CrashHandler.h 里,2026-10-21 搬到这儿 ——
//    数据目录和 org/app 必须是一对,放在同一个模块里才不会被改散。)
inline constexpr char kOrgName[] = "QianWei";
inline constexpr char kAppName[] = "HMI";

// 全应用数据目录。日志 / history.db / users.json / 审计日志 / 崩溃 dump 都在这儿。
//
//   默认 = <GenericDataLocation>/<org>/<app>
//          (Windows: %LOCALAPPDATA%\QianWei\HMI)
//   覆盖 = 环境变量 HMI_DATA_DIR
//
// ★ 为什么默认不用 QStandardPaths::AppLocalDataLocation:
//   那个值含 <org>/<app>,而 org/app 来自 QCoreApplication 的全局状态 ——
//   "调用方什么时候设的"会漂移。崩溃 dump 那条路已经因为这个漂移丢过 28 天现场
//   (见 crash/CrashHandler.h 顶上的注释)。这里用【常量】拼,不读全局状态。
//
// ★ 为什么不读 LOCALAPPDATA 环境变量:
//   Qt 在 Windows 上不认它(见文件头)。要隔离只能自己开一个口子。
QString appDataDir();

} // namespace qwhmi::util

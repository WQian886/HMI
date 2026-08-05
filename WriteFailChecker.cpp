// 阶段 4.3.x(2026-08-05 Bug #2 修复):WriteFailChecker 实现

#include "WriteFailChecker.h"

WriteFailChecker::WriteFailChecker(QObject* parent) : AlarmChecker(parent) {
    // 名字用英文(系统内部标识),UI 上 alarmLevelName() 转中文
    setName("WriteFail");
    // 写失败 = Critical(跟 HighAlarmChecker 一致档位),不进 Emergency 因为不一定"危急生命"
    setLevel(AlarmLevel::Critical);
}

WriteFailChecker::~WriteFailChecker() = default;

// ★ BUGFIX-2(2026-08-05):写失败转报警,UI 能看到(以前只 qDebug)
//   【背景】WriteScheduler.cpp:89 emit writeFailed(name, reason) 但没人接,
//          auditTable 静默更新,操作员看不到"重试 5 次还失败"这种严重事件
//   【改法】WriteFailChecker::onWriteFailed 收到 → 构造 Alarm → emit triggered
//          AlarmManager 已有的 onCheckerTriggered 会存到 activeAlarms_ + 转发给 UI
//   【取舍】不 emit recovered —— 写失败是持续故障,需要操作员 acknowledge 走 5.5 流程
//          (Acknowledge 后状态 Active → Acknowledged,5.5 BUGFIX 已实现)
//   【参照】WriteScheduler.cpp:onWriteReply 重试到上限分支
void WriteFailChecker::onWriteFailed(const QString& name, const QString& reason) {
    Alarm a;
    a.id_          = QDateTime::currentMSecsSinceEpoch();          // 唯一 ID(跟 HighAlarmChecker 一致)
    a.name         = QString("写失败:%1").arg(name);              // 中文 + 变量名(操作员看得懂)
    a.variableName = name;                                       // 留 Variable 名(虽然没有 Variable 对象)
    a.level        = AlarmLevel::Critical;                        // 跟 setLevel 一致
    a.state        = AlarmState::Active;
    a.triggeredAt  = QDateTime::currentDateTime();
    a.triggerValue = reason;                                     // 失败原因作为"触发值快照"(事故分析用)
    a.threshold    = QString("重试 %1 次后放弃").arg(kMaxRetries_);

    qWarning() << "[WriteFailChecker]" << a.name
               << "value=" << a.triggerValue
               << "threshold=" << a.threshold;

    emit triggered(a);   // AlarmManager 已 connect 这个信号,会存 activeAlarms_ + 转发 alarmTriggered
    // ★ 注意:不 emit recovered —— 写失败不会自动恢复,操作员需手动处理
}

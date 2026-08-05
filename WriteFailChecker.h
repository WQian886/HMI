// 阶段 4.3.x(2026-08-05 Bug #2 修复):写失败转 Critical 报警
//   WriteScheduler 重试到上限 → emit writeFailed(name, reason)
//   WriteFailChecker 接信号 → 构造 Alarm → emit triggered → 进 alarmTable 闪烁
//   ★ 写失败是持续故障,不需要 auto recover,操作员手动 acknowledge 走 5.5 流程

#pragma once

#include "Alarm.h"

class WriteFailChecker : public AlarmChecker {
    Q_OBJECT
public:
    explicit WriteFailChecker(QObject* parent = nullptr);
    ~WriteFailChecker() override;

public slots:
    // 接 WriteScheduler::writeFailed(name, reason)
    // name: 失败变量名(如 "speed")
    // reason: 失败原因(如 "重试 5 次仍失败,已停止重发(PLC 故障?)")
    void onWriteFailed(const QString& name, const QString& reason);

private:
    // 私有常量:重试上限(跟 WriteScheduler 同步,这里再写一份是避免双向依赖)
    // ★ 跟 WriteScheduler::kMaxWriteRetries 保持一致;以后改 WriteScheduler 常量这里也得改
    static constexpr int kMaxRetries_ = 5;
};

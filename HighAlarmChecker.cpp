// 阶段 5.3(2026-08-04):HighAlarmChecker 实现

#include "HighAlarmChecker.h"
#include "PollScheduler.h"  // ★ BUGFIX-3(2026-08-05):需要完整定义才能 connect PollScheduler::rawRead
#include "Variable.h"

HighAlarmChecker::HighAlarmChecker(const QString& alarmName,
                                   Variable* variable,
                                   PollScheduler* pollScheduler,
                                   double threshold,
                                   AlarmLevel level,
                                   QObject* parent)
    : AlarmChecker(parent), variable_(variable), pollScheduler_(pollScheduler), threshold_(threshold)
{
    setName(alarmName);
    setLevel(level);
    // ★ BUGFIX-3(2026-08-05):订阅 PollScheduler::rawRead(原始 PLC 值,死区过滤前)
    //   【背景】原代码订阅 Variable::valueChanged,但 Variable.deadband_ 吞掉小波动
    //          → 温度在 49~51 抖动时 Checker 永远看不到 50+ 的值 → 漏报
    //   【改法】订阅 PollScheduler::rawRead,绕开 Variable 死区
    //   【取舍】PollScheduler 一次只发 1 个 16-bit register;Int32/Float 留后续补
    //   【参照】PollScheduler.cpp:onModbusReply 在 setValue + setQuality 后 emit
    if (pollScheduler_) {
        connect(pollScheduler_, &PollScheduler::rawRead,
                this, &HighAlarmChecker::onRawRead);
    }
}

HighAlarmChecker::~HighAlarmChecker() = default;

// ★ BUGFIX-3(2026-08-05):改订阅 PollScheduler::rawRead(原始值,死区过滤前)
//   【背景】见 HighAlarmChecker.cpp 构造函数 BUGFIX-3 注释
//   【改法】直接拿原始 register 转 double,不再走 Variable::value().toDouble()
//          (那条路径已被 deadband 过滤过)
//   【取舍】v != variable_ 直接 return — PollScheduler 会发所有 Variable 的 rawRead,
//          HighAlarmChecker 只关心自己监控的那一个
//   【保留】原来的 onValueChanged 已删除(不再订阅 valueChanged)
void HighAlarmChecker::onRawRead(Variable* v, quint16 rawValue) {
    if (v != variable_) return;                       // ★ 只关心自己监控的 Variable
    if (!variable_) return;
    if (variable_->quality() != Variable::Quality::Good) return;  // ★ PLC 断线不触发

    // 把原始 register 转 double(只支持 Int16/UInt16/Bool,Int32/Float 后续再补)
    bool ok = false;
    double val = 0.0;
    switch (variable_->dataType()) {
        case Variable::DataType::Int16:
            val = static_cast<double>(static_cast<qint16>(rawValue));
            ok = true;
            break;
        case Variable::DataType::UInt16:
            val = static_cast<double>(rawValue);
            ok = true;
            break;
        case Variable::DataType::Bool:
            val = (rawValue & 0x01) ? 1.0 : 0.0;
            ok = true;
            break;
        // Int32/Float 占 2 个 register,需要 PollScheduler 多次 emit,后续再补
        default:
            return;
    }
    if (!ok) return;

    const bool overThreshold = (val > threshold_);

    if (overThreshold && !active_) {
        // ★ IMPROVE-5.3(2026-08-04):防重复触发(同值不重复 emit)
        //   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):生成唯一 ID 并保存
        active_ = true;
        Alarm a;
        a.id_           = QDateTime::currentMSecsSinceEpoch();
        currentId_      = a.id_;
        a.name          = name_;
        a.variableName  = variable_->name();
        a.level         = level_;
        a.state         = AlarmState::Active;
        a.triggeredAt   = QDateTime::currentDateTime();
        a.triggerValue  = QString::number(val);
        a.threshold     = QString("> %1").arg(threshold_);
        emit triggered(a);
    } else if (!overThreshold && active_) {
        // 恢复判定 = 值回到阈值以下
        // ★ BUGFIX-5.5:用保存的 ID 发 recovered
        active_ = false;
        emit recovered(currentId_);
        currentId_ = 0;
    }
}
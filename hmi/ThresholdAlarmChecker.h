// ★ IMPROVE-5.3-收尾(2026-08-07):合并 HighAlarmChecker + LowAlarmChecker 为 ThresholdAlarmChecker(DRY)
//   【背景】5.3 收尾:High/Low 两个 Checker 各 ~30 行,逻辑只差方向(> vs <),DRY 精神合并为一份
//   【改法】一个类 + Direction 参数;AlarmChecker 基类接口不变,AlarmManager 不用动
//   【取舍】Direction 二元(Above/Below);区间报警(<a && >b)暂不做,留阶段 7+ 再扩展
//   【参照】Wqian 2026-08-07 阶段 5.3 收尾拍板

#pragma once

#include <QTimer>          // ★ 5.3 收尾:durationTimer_ 成员需要(指针,不指也安全但加更稳)
#include "Alarm.h"

class Variable;
class PollScheduler;

class ThresholdAlarmChecker : public AlarmChecker {
    Q_OBJECT
public:
    // alarmName:     报警名("温度高" / "温度低")
    // variable:      要监控的变量(不接管生命周期)
    // pollScheduler: 订阅 PollScheduler::rawRead(原始值,死区过滤前)—— 5.3 Bug #3 修复保留
    // config:        阈值 / 方向 / 等级 / 最小持续时间,集中参数化
    ThresholdAlarmChecker(const QString& alarmName,
                          Variable* variable,
                          PollScheduler* pollScheduler,
                          const CheckerConfig& config,
                          QObject* parent = nullptr);
    ~ThresholdAlarmChecker() override;

    // ★ IMPROVE-6.2(2026-08-07):配置存读 getter(JsonSerializer 拿 variable + config)
    Variable* variable() const { return variable_; }
    const CheckerConfig& config() const { return config_; }

private slots:
    // 跟 HighAlarmChecker 一样订阅 rawRead(原始值,绕开 Variable 死区)
    // v 可能是 PollScheduler 监控的任何 Variable,Checker 只关心 variable_
    void onRawRead(Variable* v, quint16 rawValue);

    // ★ 阶段 6.3(2026-08-07):监听 varMgr->variableReplaced,旧 Variable 死了换新 Variable
    void onVariableReplaced(Variable* oldVar, Variable* newVar);

private:
    // ★ F2(2026-09-30):三态显式化 —— 替换掉原来的 bool active_
    //   【为什么要改】原来 active_ 一个 bool 同时表示"待触发"和"已触发",
    //         只能靠 currentId_ 是否为 0 反推当前在哪个状态。
    //         滞回必须【只】作用在"已触发"这一档(待触发时仍要按原阈值撤销,
    //         否则短促尖峰会因滞回一直挂着,变成另一种静默漏报),
    //         而单个 bool 挂不上去 —— 这才是改三态的真正理由。
    //   【澄清一件事,别被历史注释带跑】"恢复后再超限会报第二次"【不是 bug】:
    //         高→正常→再高就是两次独立工况,报两次是正确的工业语义。
    //         真正要压的是 chatter(慢速来回摆),那是滞回干的事。
    //         详见 tst_ThresholdAlarmChecker::reBreachAfterRecovery_waitsFullMinDurationWithNewId
    //   【改法】三个具名状态,消除"要靠第二个变量反推当前处于哪个状态"这件事。
    //         Idle:   正常,没在等也没报过
    //         Pending:已超限,正在等 minDurationMs
    //         Fired:  已 emit triggered,在等恢复沿(★ 滞回在这一档才生效)
    enum class State { Idle, Pending, Fired };

    Variable*       variable_;        // 不 delete(VariableManager 管)
    PollScheduler*  pollScheduler_;   // 不 delete(hmi 管)
    CheckerConfig   config_;
    State           state_        = State::Idle;  // 当前状态(取代 active_)
    qint64          currentId_   = 0;     // 当前 active Alarm 的 ID(给 recovered 用)
    double          lastValue_   = 0.0;   // 最后一次 rawRead 的值(给 triggerValue 用)
    QTimer*         durationTimer_ = nullptr;  // ★ 5.3 收尾新增:最小持续时间计时器
};
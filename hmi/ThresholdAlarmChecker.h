// ★ IMPROVE-5.3-收尾(2026-08-07):合并 HighAlarmChecker + LowAlarmChecker 为 ThresholdAlarmChecker(DRY)
//   【背景】5.3 收尾:High/Low 两个 Checker 各 ~30 行,逻辑只差方向(> vs <),DRY 精神合并为一份
//   【改法】一个类 + Direction 参数;AlarmChecker 基类接口不变,AlarmManager 不用动
//   【取舍】Direction 二元(Above/Below);区间报警(<a && >b)暂不做,留阶段 7+ 再扩展
//   【参照】Wqian 2026-08-07 阶段 5.3 收尾拍板

#pragma once

#include <QTimer>          // ★ 5.3 收尾:durationTimer_ 成员需要(指针,不指也安全但加更稳)
#include <QPointer>        // ★★ 2026-10-19:variable_ 改成 QPointer,理由见下面那一段
#include "Alarm.h"
#include "Variable.h"      // ★★ 2026-10-19:【不能只前向声明】
                          //   裸指针可以对着一个前向声明的类用,QPointer 不行 ——
                          //   它是模板,QPointer<T> 实例化时要做 static_cast<T*>(QObject*)，
                          //   那个转换要求 T 【已经完整定义】且确实是 QObject 的子类。
                          //   只留 `class Variable;` 的话,编译报的是
                          //     qpointer.h(75): error C2440: 无法从 "QObject*" 转成 "Variable*"
                          //   而报错点在 Qt 的头文件里,离"你少 include 了一个头"很远。
                          //   无环:Variable.h 只 include Qt 的头,不回引本文件。

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

    // ===== ★ F4-b(2026-10-08):运行中改配置 =====
    //
    // 【为什么不删了重建】删掉重建会把 checker 换成一个全新对象,
    //   而 Fired 状态里的 currentId_ 会跟着旧对象一起消失 ——
    //   AlarmManager::activeAlarms_ 里那条记录从此再也不会收到 recovered(),
    //   永远挂在"活动报警"列表里出不去。历史表也会多出一条孤儿行。
    //   这正是 F1(d504b53)刚修过的那类 bug,不能从后门再放进来。
    //   【为什么以前没这问题】以前根本没法在运行中改报警配置 ——
    //   F4 之前的配置只能手改 JSON + 重载工程,重载走 clearAll(),不经过这条路径。
    //   F4-b 是第一个真正的调用者,所以这条路径必须先修对。
    //
    // 【就地改配置后怎么生效】
    //   不主动触发、不主动恢复,【等下一个采集周期由 onRawRead 按新阈值重新判定】。
    //   这是唯一正确的时机:值是那一刻才可信的,配置是我们给的。
    //   Fired 状态下把阈值从 80 调到 60、当前值 70,下一个周期自然判"仍在响";
    //   调到 100、当前值 70,下一个周期自然判"已恢复"并补上恢复记录。
    void setConfig(const CheckerConfig& cfg);

    // ★ 换监控的变量。调用方负责拦住"正在响时换变量"的情况,理由见 .cpp。
    void setVariable(Variable* v);

    // 当前是否正在响(已 emit triggered、还没恢复)。
    // 界面用它决定能不能换变量 —— 见 AlarmConfigDialog::applyDraftToManager。
    bool isActive() const { return state_ == State::Fired; }

private slots:
    // 跟 HighAlarmChecker 一样订阅 rawRead(原始值,绕开 Variable 死区)
    // v 可能是 PollScheduler 监控的任何 Variable,Checker 只关心 variable_
    void onRawRead(Variable* v, quint16 rawValue);

    // ★★ 2026-10-19:onVariableReplaced 已删除,变量指针改用 QPointer。
    //   理由见下面 variable_ 的注释 —— 那条槽【从来没有被 connect 过】
    //   (hmi.cpp 里那条在 E#29 改成"按名查"时被删了),所以它一直是个孤儿:
    //   写它的那个"变量死了就置 nullptr"的意图,从来到底没有生效过。
    //   QPointer 不需要谁来通知:变量一被 delete,它自己就变空。
    //   留着这个槽只会让人以为"已经有人在管这件事了"。

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

    // ★★ 2026-10-19:从裸 `Variable*` 改成 `QPointer<Variable>`。
    //
    // 【它曾经是一个真实的 use-after-free,今天崩得很硬】
    //   变量被删 → VariableManager 广播 variableReplaced(v, nullptr) 并 delete v
    //   → 本类【没有 connect 那个信号】(见上面 onVariableReplaced 已删除那段)
    //   → variable_ 继续指着已释放的堆内存,而且【非空】
    //   → Project::saveToFile 里那一行
    //         tc->variable() ? tc->variable()->name() : QString()
    //     三元判的是"指针是不是空",而悬空指针【不是空】
    //     → 进 true 分支 → 解引用已释放内存
    //
    //   实测(2026-10-19,offscreen 下跑 tst_ProjectMultiPlc):
    //     Exception code 0xc0000005,栈是
    //       Variable::name() ← Project::saveToFile() ← 用例
    //   也就是【不是"存出来的工程打不开",是当场崩】。
    //   我在动手之前写进计划的判断是"三元的 false 分支给空串,文件打不开",
    //   —— 那个判断建立在一个错误前提上(以为这个槽有人在 connect)。
    //
    // 【为什么 QPointer 是对的,而不是把那个槽接上】
    //   槽的问题不在于"少接了一根线",在于它【要靠别人记得接】。
    //   VariableManager 在 clear()(直接 delete,不广播)和 removeVariable()
    //   (广播了才 delete)两条路上表现不一样,而 QPointer 对两条路一视同仁 ——
    //   对象一死指针自动变空,不需要任何人通知。
    //   WriteScheduler::pending_ 早就用了同样的招(见 WriteScheduler.h:60),
    //   这里只是把同一个理由用在同一个地方。
    //
    // 【QPointer 变空之后会怎样】
    //   它不会"自动修好"工程文件。变空后 saveToFile:686 的三元会走 false 分支,
    //   写出 `"variable": ""`,下次加载仍然 findVariable("") 失败 → 工程打不开。
    //   那一半由"删除前先查引用"堵(见 VariableManager 的引用查询),不是这一行。
    //   QPointer 解决的是【不该崩的地方不崩】,不是【不该写坏的文件不写坏】。
    //   两件事分开做,才分得清哪条测试钉的是哪一头。
    QPointer<Variable> variable_;   // 不 delete(VariableManager 管)
    PollScheduler*  pollScheduler_;   // 不 delete(hmi 管)
    CheckerConfig   config_;
    State           state_        = State::Idle;  // 当前状态(取代 active_)
    qint64          currentId_   = 0;     // 当前 active Alarm 的 ID(给 recovered 用)
    double          lastValue_   = 0.0;   // 最后一次 rawRead 的值(给 triggerValue 用)
    QTimer*         durationTimer_ = nullptr;  // ★ 5.3 收尾新增:最小持续时间计时器
};
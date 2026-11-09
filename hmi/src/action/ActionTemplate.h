// ============================================================================
// src/action/ActionTemplate.h —— "报警触发了要做什么"的【唯一出处】
//
// ============================================================================
// 【背景】(F 阶段最后一项:联动模板)
//   原来的联动只有一种形态:一条报警 → 一条"写变量"动作,
//   而且【一条报警只能配一条】——
//     ActionManager::addMapping 的实现是:
//         mappings_[alarmName] = m;      ← 后配的【覆盖】先配的
//   ⇒ 想给同一条报警配两个动作(停机 + 记一条日志),
//     只能放弃一个,或者改代码重新编译。
//   而且"动作有哪些种类"这件事整个写死在
//   ActionManager::onAlarmTriggered 里 —— 没有任何一条测试能看见它。
//
// 【本文件解决什么】
//   ① 动作【类型】可扩展(ActionType):写变量 / 只记日志;
//   ② 一条报警可以配【多步】(ActionTemplate.steps),按配的顺序执行;
//   ③ 判据(哪一步合法、执行完是什么结果)住在这个文件里 ⇒ 可直接单测;
//   ④ ActionManager 只留接线(把"真的写变量"这件事接出去),
//      hmi.cpp 只留调用。
//
// 【★ 为什么单独一个文件】—— 同 ActionWiring.h / AlarmTriggerPolicy.h 一个道理
//   写在 ActionManager 里的判据,测试只能【复制一份】跑 —— 测的是副本,
//   而生产那份被改错了,测试照样全绿。
//   抽出来之后,改一处两边一起变,测试绿了才说明生产也是绿的。
//
// 【★ 边界:本文件不认识 PLC,也不认识 Variable】
//   它只认识一个抽象出口 ActionSink ——
//   "把变量写成这个值"这件事由调用方实现:
//     生产:ActionManager(接 VariableManager + ValueCoercion)
//     测试:假 sink(记下来就行)
//   手法同 src/plc/ModbusRequestParser.h(纯判据,不碰硬件)。
//
// 【★ 不许在这里做什么】
//   · 不许弹窗、不许写文件、不许碰 PLC —— 那些是调用方的活;
//   · 不许在这里"顺手"把类型适配(coerceToDataType)也做了 ——
//     适配要【变量自己的声明类型】,那是 sink 才知道的事,
//     放这里就得把 Variable 拖进来,边界就破了。
//
// 关联:ActionManager(执行方)、JsonSerializer(存读)、
//      Project.cpp(工程文件)、tst_ActionTemplate(单测)
// ============================================================================

#pragma once

#include <QList>
#include <QString>
#include <QVariant>

namespace qwhmi::action {

// ★ 联动的动作类型。
//   【为什么现在只有两种】
//     WriteVariable —— 写变量(标脏 → WriteScheduler → 真的写 PLC),
//                      这是【唯一会动现场】的动作;
//     LogOnly      —— 只在日志/审计里留一条痕,不动 PLC。
//   【取舍】不加"执行脚本""发通知"这类动作:
//     它们要么需要外部依赖(邮件/websockets),要么需要执行任意代码 ——
//     后者在工控上是危险动作(一条报警就能在 HMI 上跑任意代码)。
//     将来要加,先想清楚"它失败了什么后果",别照搬"写变量"的失败语义。
enum class ActionType {
    WriteVariable,
    LogOnly,
};

// 类型 ↔ 字符串(给 JSON 用)。
//   ★ 字符串一旦发布就是【工程文件格式的一部分】,
//     改名会让老工程文件读不出来 —— 所以这里只加不改。
QString actionTypeToString(ActionType t);

// 字符串 → 类型。认不出来返回 false(【不】静默 fallback 成某个类型 ——
//   认不出来的东西必须让上层知道,不然就是"配错了但没人知道")。
bool actionTypeFromString(const QString& s, ActionType* out);

// ★ 一步动作。
struct ActionStep {
    ActionType type = ActionType::WriteVariable;

    // WriteVariable:变量名(【按名字】,不存裸指针 ——
    //   变量被删掉之后指针会变悬空,而名字只是"找不到",不会崩。
    //   同 ActionManager::Mapping 的取舍。)
    QString variableName;

    // WriteVariable:要写的值。
    //   类型适配(coerceToDataType)【不在这里做】—— 那要变量自己的声明类型,
    //   sink 才知道。见文件头【不许在这里做什么】。
    QVariant value;

    // LogOnly:要留的那条痕的文字。
    QString text;
};

// ★ 一条报警对应的一整组动作(模板)。
struct ActionTemplate {
    QString           alarmName;
    QList<ActionStep> steps;
};

// ★ 一步执行完是什么结果。
//   【为什么要有这么多档】
//     每一个都对应一种【现场会看到的现象】,混成一档就分不出来了:
//       Executed    —— 写成功了
//       Logged      —— 只记了日志(本来就是这个动作的效果)
//       NoVariable  —— 变量没了(被删/没加载)⇒ 动作【没执行】
//       InvalidValue—— 值适配不成变量的类型 ⇒ 动作【没执行】
//       Rejected    —— 变量拒收(超范围)⇒ 动作【没执行】
//       UnknownType —— 不认识的动作类型(工程文件里写了新版本才有的类型)
//       InvalidStep —— 这一步本身就不合法(缺变量名 / 缺文字)
//   ★ "动作没执行"和"动作执行了"必须能分开 ——
//     否则审计日志会说"已执行"而 PLC 一个字节都没收到
//     (G-15 / E #28 踩过同一个坑)。
enum class StepOutcome {
    Executed,
    Logged,
    NoVariable,
    InvalidValue,
    Rejected,
    UnknownType,
    InvalidStep,
};

// 结果 → 人看的字(日志 / 测试断言用)。
QString stepOutcomeToString(StepOutcome oc);

// ★ 一步执行完的【完整】结果 —— 比 StepOutcome 多带一个"真正写进去的值"。
//   【为什么要多这一个字段】
//     actionExecuted 信号要报的是【写进去的那个值】(按变量声明类型适配过的),
//     不是用户配的那个值 —— 比如配的是 0(int)而变量是 UInt16,
//     写进去的是 UShort 的 0,报出来得是"0"而不是"0(int)"。
struct StepResult {
    StepOutcome outcome = StepOutcome::InvalidStep;

    // 只有 outcome == Executed 时有意义:真正写进变量的值(字符串形式)。
    QString appliedValue;

    // 这一步【做成了】吗。
    //   Logged 也算做成 —— 只记日志本来就是那个动作的全部效果。
    bool ok() const {
        return outcome == StepOutcome::Executed || outcome == StepOutcome::Logged;
    }
};

// ★ 【抽象出口】—— 公共层不认识 VariableManager,只认识这个接口。
//
//   【为什么要有这一层】
//     "把变量写成这个值"这件事要做的事不少:
//       查变量在不在 → 按变量声明类型适配值 → setValue → 看它收没收 → 标脏
//     而这些【全都】要 VariableManager —— 它是 QObject,带生命周期,
//     拖进公共层会把边界撑破。
//     拆成接口之后:公共层只管"按顺序把每一步做掉,并如实报结果",
//     真正写变量的那几行由调用方实现,测试里换成一个假 sink。
//
//   【为什么 sink 带着报警名】
//     失败日志("联动失败:变量 X 已不存在")和审计信号都要知道
//     【是哪条报警引起的】—— 少了它,现场看到一行失败根本不知道是哪条报警,
//     几十条报警里猜不出来。
class ActionSink {
public:
    explicit ActionSink(const QString& alarmName) : alarmName_(alarmName) {}
    virtual ~ActionSink() = default;

    const QString& alarmName() const { return alarmName_; }

    // 写一个变量。
    //   返回 Executed / NoVariable / InvalidValue / Rejected
    //   ★ 实现方【必须】如实返回 —— 返回 Executed 而实际没写出去,
    //     就是"审计日志说已执行、PLC 没收到"那种撒谎。
    virtual StepResult writeVariable(const QString& variableName, const QVariant& value) = 0;

    // 只留一条痕(日志 / 审计)。
    virtual void logOnly(const QString& text) = 0;

private:
    QString alarmName_;
};

// 这一步本身合不合法(【不】查变量在不在 —— 那是 sink 的事)。
//   err 非空时写入人看的原因。
bool validateStep(const ActionStep& step, QString* err = nullptr);

// 执行一步。
//   ★ 不合法的步骤【不】调 sink,直接返回 InvalidStep / UnknownType ——
//     "配置写错了"不能变成"照样往 PLC 写一个半截的值"。
StepResult executeStep(const ActionStep& step, ActionSink& sink);

} // namespace qwhmi::action

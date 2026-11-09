// 阶段 5.6(2026-08-04):报警联动动作
//   报警触发 → 复用阶段 4 WriteScheduler → Modbus 写 PLC(关阀/停机)
//
// ★ 定位声明(2026-08-27):本模块是「操作便利功能」,不是「安全功能」
//   【背景】按工业功能安全标准(IEC 61508 / IEC 61511),真正的安全功能必须
//          独立于普通监视系统。HMI 是跑在 Windows 上的应用:会崩、会卡、
//          会被杀毒软件占满 CPU、网线会被踢掉。「温度高 → 停机」这条逻辑
//          住在 HMI 里 = HMI 一崩安全保护就没了,而机器还在转。
//   【正确分工】物理层(机械温控开关直接断电)兜底
//              → PLC(安全联锁 + HMI 心跳看门狗)
//              → HMI(只做显示 / 操作员下命令 / 记历史)
//   【结论】本模块正常时帮操作员省几秒,但绝不能是唯一那道防线。
//          它挂了、指针野了、消息丢了,都不该导致事故 —— 因为下面有 PLC 兜着。
//          真实项目上线前必须在 PLC 侧实现同等联锁 + 看门狗。
//   【参照】docs/通用知识/工业界引用失效与安全分层速查.md
//          docs/专题/2026-08-27_ActionManager报警联动与悬空指针.md
//
// ★★ F 阶段(2026-11-09):本模块从「一条报警 → 一条动作」改成「一条报警 → 一组动作」
//   【原来】mappings_[alarmName] = m;  ← 后配的【覆盖】先配的
//     ⇒ 想给同一条报警配两个动作(停机 + 留痕),只能放弃一个。
//   【现在】内部存的是 qwhmi::action::ActionTemplate(报警名 → 一组步骤),
//     addMapping 是【追加】,addTemplate 是【整组替换】。
//   【判据搬家】"一步做掉会是什么结果"这条判据搬进了公共层
//     src/action/ActionTemplate.{h,cpp} ⇒ 可直接单测;
//     本文件只留接线(怎么真的把变量写出去)。
//   【向后兼容】老接口 addMapping / mappings() 的名字和用法都没变,
//     只是 addMapping 从"覆盖"变成"追加"。

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

// ★ 公共层:动作类型 / 步骤 / 模板 / 抽象出口 / 判据
#include "action/ActionTemplate.h"

struct Alarm;  // 前向声明跟 Alarm.h 定义一致(struct)
class Variable;
class VariableManager;  // ★ E #29(2026-09-15):按变量名查当前 Variable* 需要

class ActionManager : public QObject {
    Q_OBJECT
public:
    explicit ActionManager(QObject* parent = nullptr);
    ~ActionManager() override;

    // 注册映射:addMapping("温度高", "speedVar", 0) → 温度高 → 速度=0(模拟停机)
    //   【背景】报警触发后,联动执行安全动作(关阀/停机),避免操作员手慢误事
    //   【★ F 阶段 2026-11-09:语义从【覆盖】改成【追加】】
    //     原来这里是 mappings_[alarmName] = m; ⇒ 同一条报警配第二个动作时,
    //     第一个被【悄悄顶掉】,而界面上什么提示都没有。
    //     现在同一个报警名可以配任意多个动作,按配的顺序依次执行。
    //   【取舍】配置写代码里(阶段 6 再做 JSON 配置文件);按变量名查,触发时拿当前指针
    //   【参照】Wqian 2026-08-04 阶段 5.6 拍板;E #29(2026-09-15)改存 QString
    void addMapping(const QString& alarmName, const QString& variableName, const QVariant& value);

    // ★ F 阶段(2026-11-09):一条报警配【一整组动作】(模板)。
    //   【语义】同一个报警名,后 addTemplate 的【整组替换】前面那组
    //     —— 模板是"完整配置",不是"再加一条"。
    //     (工程文件读入时用这个:action_templates 里的那条要盖掉
    //      老格式 actions 数组里同名的那几条。)
    //   【空模板】steps 为空也【照样存】——
    //     它表示"这条报警【没有】动作",是用来把老格式残留清掉的。
    void addTemplate(const qwhmi::action::ActionTemplate& tpl);

    // ★ 所有模板(报警名 → 动作组),按报警名排序,便于存盘时输出稳定。
    QList<qwhmi::action::ActionTemplate> templates() const;

    void clear();

signals:
    // 阶段 5.6:动作执行后发(给审计/日志用,5.6 暂不接 UI)
    void actionExecuted(const QString& alarmName,
                        const QString& variableName,
                        const QString& value);

public slots:
    // 接收 AlarmManager::alarmTriggered(5.2 已有信号)
    void onAlarmTriggered(const Alarm& a);

    // ★ E #29(2026-09-15):onVariableReplaced 已删除 — 改存变量名后无需监听 variableReplaced 广播

public:
    // ★ E #29(2026-09-15):ActionManager 需要 VariableManager 引用来按名查 Variable*
    void setVariableManager(VariableManager* m) { varMgr_ = m; }

    // ★ IMPROVE-6.2(2026-08-07):暴露 Mapping 给 saveToFile
    //   【背景】原来私有,JsonSerializer 拿不到 alarmName + variable + value
    //   【改法】提到 public,加 alarmName 字段(mappings() getter 返回 QList 拷贝用)
    //   【取舍】暴露内部 struct 增加耦合,但 saveToFile 必须能枚举所有 mappings
    //   【参照】Wqian 2026-08-07 阶段 6.2 开干
    struct Mapping {
        QString   alarmName;
        QString   variableName;  // ★ E #29(2026-09-15):改存变量名,不存裸指针
                                  //   触发时 varMgr_->findVariable(name) 拿当前指针
                                  //   卸下 onVariableReplaced 临时补丁(阶段 6.3)
        QVariant  value;
    };

    // ★ IMPROVE-6.2(2026-08-07):返回所有 mappings(给 saveToFile 用)
    //   ★ F 阶段(2026-11-09):现在是【展平】——
    //     把所有模板里的"写变量"步骤摊平成一串 Mapping。
    //     老工程文件格式(actions 数组)只有"写变量"一种动作,
    //     所以这个展平视图就是老格式该有的样子。
    //     ⚠️ "只记日志"那类步骤在这个视图里【看不见】——
    //       完整信息在 templates() 里,存盘时两个都写(见 Project.cpp)。
    QList<Mapping> mappings() const;

private:
    // ★ 报警名 → 模板(报警名 → 一组动作)
    //   【为什么不是 QHash<QString, Mapping>】
    //     一条报警只能配一条动作 —— 那正是 F 阶段要修的那个限制。
    QHash<QString, qwhmi::action::ActionTemplate> templates_;

    VariableManager* varMgr_ = nullptr;  // ★ E #29(2026-09-15):按名查指针的入口

    // ★ 真正"把变量写出去"的那几行 —— 公共层通过 Sink 调到这儿。
    //   【为什么单独抽成两个私有函数】
    //     公共层(StepResult / Outcome)的判定和"怎么真的写"要分开 ——
    //     前者要能单测(不碰 VariableManager),后者要 VariableManager。
    //   【为什么报警名要一路传下来】
    //     失败日志("联动失败:变量 X 已不存在")必须说清是【哪条报警】引起的,
    //     几十条报警里不说是查不出来的。
    qwhmi::action::StepResult doWriteVariable(const QString& alarmName,
                                              const QString& variableName,
                                              const QVariant& value);
    void doLogOnly(const QString& alarmName, const QString& text);

    // ★ 私有嵌套的 sink —— 把公共层的抽象出口接到本类上。
    //   【为什么不直接让 ActionManager 继承 ActionSink】
    //     ActionSink 的构造函数要带【报警名】,而 ActionManager 构造的时候
    //     还不知道将来是哪条报警 ⇒ 不能在构造期把基类初始化好。
    //     嵌套类在【触发时】构造,报警名那会儿是知道的。
    //   【嵌套类能调本类的 private】—— C++11 起成立,不用 friend。
    class Sink : public qwhmi::action::ActionSink {
    public:
        Sink(ActionManager* owner, const QString& alarmName)
            : qwhmi::action::ActionSink(alarmName), owner_(owner) {}
        qwhmi::action::StepResult writeVariable(const QString& variableName,
                                                const QVariant& value) override {
            return owner_->doWriteVariable(alarmName(), variableName, value);
        }
        void logOnly(const QString& text) override { owner_->doLogOnly(alarmName(), text); }
    private:
        ActionManager* owner_;
    };
};

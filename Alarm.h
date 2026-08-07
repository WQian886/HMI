// 阶段 5.2(2026-08-04):Alarm 数据结构 + AlarmChecker 基类
//   报警系统的"事实层":数据 + 信号出口
//   5.2 只做骨架,5.3 才有具体 Checker 子类(High/Low),5.4 才有 UI

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMetaType>  // Alarm 当 signal 参数用 → 需要 Q_DECLARE_METATYPE

// ★ IMPROVE-5.2(2026-08-04):4 档报警等级,对应 PLC 标准
//   【背景】原 HMI.md 写"信息/警告/严重"3 档,跟 Wqian 对照工业 HMI 习惯(WINCC / iFIX / 威纶通)加了"危急"档
//   【改法】AlarmLevel 4 档,从最轻到最重
//   【取舍】4 档是工业标配;再加就冗余(实际项目最多 4 档)
//   【参照】Wqian 2026-08-04 阶段 5 修订讨论
enum class AlarmLevel   { Info, Warning, Critical, Emergency };

// ★ IMPROVE-5.2(2026-08-04):报警生命周期 3 态
//   【背景】工业 HMI 报警必须支持"触发 → 确认 → 恢复"完整流程
//   【改法】Active(已触发未确认)→ Acknowledged(用户已确认)→ Cleared(条件消失 + 用户清)
//   【取舍】3 态是工业标配;再加中间态(比如 "AutoCleared")会让 UI 复杂
//   【参照】WINCC AlarmView 状态机
enum class AlarmState   { Active, Acknowledged, Cleared };

// ★ IMPROVE-5.2(2026-08-04):单条报警记录
//   【背景】5.2 阶段定下来的字段数据模型
//   【改法】name / variableName / level / state + 3 个时间戳 + 触发值快照 + 阈值
//   【取舍】3 个时间戳是工业标配(触发/确认/恢复);triggerValue 留事故分析用;threshold 调试用
//   【参照】Wqian 2026-08-04 拍板的阶段 5 修订版(inventory/HMI.md)
struct Alarm {
    qint64      id_ = 0;          // 唯一 ID(QDateTime::currentMSecsSinceEpoch() 生成)—— 5.5 BUGFIX
    QString     name;             // 报警名("温度高")
    QString     variableName;     // 关联的 Variable 名("temperature")
	AlarmLevel  level;             // 报警等级
	AlarmState  state;             // 报警状态
    QDateTime   triggeredAt;       // 触发时间 默认 invalid(没触发就是 invalid)
    QDateTime   acknowledgedAt;    // 确认时间 默认 invalid(没确认就是 invalid)
    QDateTime   recoveredAt;       // 默认 invalid(没恢复就是 invalid)
    QString     triggerValue;     // 触发时的值快照("80.5")
    QString     threshold;        // 阈值描述(">= 80.0")
};
Q_DECLARE_METATYPE(Alarm)

// ★ IMPROVE-5.3-收尾(2026-08-07):报警方向 + Checker 配置结构
//   【背景】5.3 收尾:HighAlarmChecker / LowAlarmChecker 合并为 ThresholdAlarmChecker(DRY)
//          把散在构造里的参数集中到 CheckerConfig,方便阶段 6 上 JSON 时反序列化
//   【改法】加 Direction 枚举(Above / Below)+ CheckerConfig struct(集中 4 个参数)
//   【取舍】Direction 只二元;区间报警(<a && >b)留阶段 7+ 再扩
//   【参照】Wqian 2026-08-07 阶段 5.3 收尾拍板
enum class Direction { Above, Below };

// ★ IMPROVE-5.3-收尾(2026-08-07):Checker 配置(为阶段 6 JSON 铺路)
//   【背景】原来 HighAlarmChecker 构造里 threshold / level 散着;5.6 收尾要扩到联动配置
//   【改法】4 个字段集中在一个 struct,阶段 6 上 JSON 时反序列化成它 → new ThresholdAlarmChecker 不用动
//   【取舍】4 个字段够 5.3 用;不加 hysteresis(滞回)/ ack_required(必确认)等高级特性
//   【参照】Wqian 2026-08-07 阶段 5.3 收尾拍板
struct CheckerConfig {
    AlarmLevel level         = AlarmLevel::Warning;  // 报警等级
    Direction  direction     = Direction::Above;     // 阈值方向
    double     threshold     = 0.0;                   // 阈值(> / < 该值触发)
    int        minDurationMs = 500;                   // 最小持续时间(防抖,抖 < 此值不报)
};

// ★ IMPROVE-5.2(2026-08-04):报警检测器基类(具体逻辑由 5.3 子类实现)
//   【背景】5.2 先把基类定好,5.3 才有 HighAlarmChecker / LowAlarmChecker 子类
//   【改法】基类只暴露 name/level getter + triggered/recovered 信号
//   【取舍】onVariableChanged 等具体逻辑放子类,基类保持抽象
//   【参照】VariableManager 基类模式
class AlarmChecker : public QObject {
    Q_OBJECT
public:
    explicit AlarmChecker(QObject* parent = nullptr);
    ~AlarmChecker() override;

    QString    name() const;
    void       setName(const QString& n);
    AlarmLevel level() const;
    void       setLevel(AlarmLevel l);

signals:
    void triggered(const Alarm& alarm);    // 触发时发,Alarm 自带 id_
    void recovered(qint64 id);            // 恢复时发,只发 ID(5.5 BUGFIX:避免同名报警匹配错)

protected:
    QString    name_ = "Unnamed";
    AlarmLevel level_ = AlarmLevel::Warning;
};
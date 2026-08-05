// 阶段 5.6(2026-08-04):报警联动动作
//   报警触发 → 复用阶段 4 WriteScheduler → Modbus 写 PLC(关阀/停机)

#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>

struct Alarm;  // 前向声明跟 Alarm.h 定义一致(struct)
class Variable;

class ActionManager : public QObject {
    Q_OBJECT
public:
    explicit ActionManager(QObject* parent = nullptr);
    ~ActionManager() override;

    // 注册映射:addMapping("温度高", speedVar, 0) → 温度高 → 速度=0(模拟停机)
    //   【背景】报警触发后,联动执行安全动作(关阀/停机),避免操作员手慢误事
    //   【取舍】配置写代码里(阶段 6 再做 JSON 配置文件);Variable 不接管生命周期
    //   【参照】Wqian 2026-08-04 阶段 5.6 拍板
    void addMapping(const QString& alarmName, Variable* variable, const QVariant& value);

    void clear();

signals:
    // 阶段 5.6:动作执行后发(给审计/日志用,5.6 暂不接 UI)
    void actionExecuted(const QString& alarmName,
                        const QString& variableName,
                        const QString& value);

public slots:
    // 接收 AlarmManager::alarmTriggered(5.2 已有信号)
    void onAlarmTriggered(const Alarm& a);

private:
    struct Mapping {
        Variable* variable;   // 不接管生命周期(VariableManager 管)
        QVariant  value;
    };
    QHash<QString, Mapping> mappings_;  // alarm name → mapping
};
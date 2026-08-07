// 阶段 5.2(2026-08-04):AlarmManager 容器类
//   管理所有 AlarmChecker + 当前 active 报警
//   5.2 只做容器 + 信号出口,5.5 才接确认/复位联动

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "Alarm.h"

class AlarmManager : public QObject {
    Q_OBJECT
public:
    explicit AlarmManager(QObject* parent = nullptr);
    ~AlarmManager() override;

    // 注册 Checker(AlarmManager 接管生命周期:内部 setParent(this))
    // 返回 false:checker 为空 / name 为空 / name 重复
    bool addChecker(AlarmChecker* c);

    // 移除(name 对应的 checker 被 delete)
    // 返回 false:name 不存在
    bool removeChecker(const QString& name);

    // 查询
    bool contains(qint64 id) const;
    int  count() const;                                  // active 报警数
    QList<Alarm> activeAlarms() const;                   // 给 5.4 UI 用

    // ★ IMPROVE-6.2(2026-08-07):配置存读访问器(返回所有 Checker 给 saveToFile 用)
    QList<AlarmChecker*> checkers() const { return checkers_.values(); }

    // 5.5 阶段接:用户点确认按钮(用 ID 唯一标识,避免同名报警匹配错误)
    void acknowledge(qint64 id);

    // ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):把所有 active 报警标记为已确认(不删记录)
    //   【背景】Wqian 反馈:复位不应该清报警记录(事故追溯需要)
    //   【改法】遍历 activeAlarms_,逐个调 acknowledge()(各自 emit alarmAcknowledged)
    //   【取舍】保留记录比 clearAll 更安全;clearAll 留作工程师手动清
    //   【参照】Wqian 2026-08-04 反馈
    void acknowledgeAll();

    // 5.5 阶段接:复位按钮 → 清所有 acknowledged 的报警(留作工程师手动清,5.5 不调用)
    void clearAll();

signals:
    // 报警触发 → 5.3 接 tryTransition + 5.4 接 UI
    void alarmTriggered(const Alarm& alarm);
    // 报警确认 → 5.5 UI(停闪),用 ID 唯一标识避免同名匹配错误(5.5 BUGFIX)
    void alarmAcknowledged(qint64 id);
    // 报警恢复 → 5.5 UI(标记恢复时间),用 ID 唯一标识(5.5 BUGFIX)
    void alarmRecovered(qint64 id);
    // 全部清除 → 5.5 UI(清空列表)
    void allCleared();

private slots:
    void onCheckerTriggered(const Alarm& alarm);
    void onCheckerRecovered(qint64 id);

private:
    QHash<qint64, Alarm>          activeAlarms_;   // id → 当前 active 报警(5.5 BUGFIX)
    QHash<QString, AlarmChecker*> checkers_;       // name → checker 实例
};
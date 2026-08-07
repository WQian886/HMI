// 阶段 6.2(2026-08-07):JSON 序列化 helper 类
//   把 Variable / CheckerConfig / ActionManager::Mapping 各自的字段 → QJsonObject
//   反向 (fromJson) 是 6.3 阶段的事,本阶段只做 toJson(save 路径)
//
//   【设计】用静态方法 + namespace 风格,不要状态、无构造
//   【取舍】enum ↔ string 的转换:Variable::DataType/Access 走 Q_ENUM 反射;AlarmLevel/Direction 无 Q_ENUM,手写 switch
//   【参照】Wqian 2026-08-07 阶段 6.2 开干;inventory/HMI.md 阶段 6 schema 设计

#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVariant>
#include <QList>

class Variable;
struct CheckerConfig;
struct Alarm;          // 前向声明,AlarmManager.h 已经定义

class JsonSerializer {
public:
    // ===== Variable → QJsonObject =====
    //   【取舍】不存的字段:value / quality / timestamp / dirty / writeFailCount(runtime 状态)
    static QJsonObject toJson(const Variable& v);

    // ===== CheckerConfig → QJsonObject =====
    static QJsonObject toJson(const CheckerConfig& cfg);

    // ===== ActionManager::Mapping → QJsonObject =====
    //   【注意】Mapping 内部嵌套了 AlarmManager.h 私有类型,这里用前向声明 + 头文件包含
    //   实际使用:include "ActionManager.h" 才能拿到 Mapping 完整定义
    static QJsonObject toActionMappingJson(const QString& alarmName,
                                           const QString& variableName,
                                           const QVariant& value);

    // ===== Enum ↔ String 辅助 =====
    //   AlarmLevel / Direction 没有 Q_ENUM,手写 switch;以后 5.x 加 Q_ENUM 可删
    static QString alarmLevelToString(int level);
    static QString directionToString(int direction);
};
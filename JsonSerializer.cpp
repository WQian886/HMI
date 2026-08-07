// 阶段 6.2(2026-08-07):JSON 序列化 helper 实现

#include "JsonSerializer.h"
#include "Variable.h"
#include "Alarm.h"
#include "ActionManager.h"   // 需要 Mapping 完整定义

#include <QMetaEnum>

// ===== Variable → QJsonObject =====
//   【背景】Variable 有 Q_ENUM(DataType / Access),走 QMetaEnum 反射拿到名字
//   【取舍】range 走 Q2-B 拍板:对象形式 {"min": ..., "max": ...}
//   【参照】Wqian 2026-08-07 阶段 6.2 schema 设计 Q2 答案
QJsonObject JsonSerializer::toJson(const Variable& v) {
    QJsonObject obj;
    obj["name"]             = v.name();
    obj["address"]          = v.address();

    // DataType: 用 QMetaEnum 反射拿名字(如 "Int16")
    QMetaEnum dataTypeEnum = QMetaEnum::fromType<Variable::DataType>();
    obj["data_type"] = QString::fromLatin1(dataTypeEnum.valueToKey(int(v.dataType())));

    // Access: 同上("ReadOnly" / "ReadWrite")
    QMetaEnum accessEnum = QMetaEnum::fromType<Variable::Access>();
    obj["access"] = QString::fromLatin1(accessEnum.valueToKey(int(v.access())));

    obj["deadband"]         = v.deadband();
    obj["poll_interval_ms"] = v.pollIntervalMs();

    // Range(Q2-B):对象形式,只在 hasRange() 为真时存
    if (v.hasRange()) {
        QJsonObject range;
        // ★ 6.2 优化(2026-08-07):用 QJsonValue::fromVariant 保留 Int/Double 类型
        //   之前 .toDouble() 会把 speed 范围 [0, 65535] 存成 [0.0, 65535.0],精度感丢失
        range["min"] = QJsonValue::fromVariant(v.minValue());
        range["max"] = QJsonValue::fromVariant(v.maxValue());
        obj["range"] = range;
    }
    // 没设 range 不存这个字段(节省 JSON 体积)

    return obj;
}

// ===== CheckerConfig → QJsonObject =====
QJsonObject JsonSerializer::toJson(const CheckerConfig& cfg) {
    QJsonObject obj;
    obj["level"]            = alarmLevelToString(int(cfg.level));      // enum class → int 转换
    obj["direction"]        = directionToString(int(cfg.direction));
    obj["threshold"]        = cfg.threshold;
    obj["min_duration_ms"]  = cfg.minDurationMs;
    return obj;
}

// ===== ActionManager::Mapping → QJsonObject =====
QJsonObject JsonSerializer::toActionMappingJson(const QString& alarmName,
                                                const QString& variableName,
                                                const QVariant& value) {
    QJsonObject obj;
    obj["alarm_name"]  = alarmName;
    obj["variable"]    = variableName;
    // write_value 用 JSON 自然类型:number / bool / string
    // QVariant → QJsonValue 自动转换(Int/Double→number,String→string,Bool→bool)
    obj["write_value"] = QJsonValue::fromVariant(value);
    return obj;
}

// ===== Enum ↔ String 辅助 =====
//   【背景】AlarmLevel / Direction 是 enum class,没 Q_ENUM,QMetaEnum 不能直接用
//   【改法】手写 switch + fallback(防御编程)
//   【取舍】以后如果给这俩 enum 加 Q_ENUM,可以直接删,改用 QMetaEnum
QString JsonSerializer::alarmLevelToString(int level) {
    switch (static_cast<AlarmLevel>(level)) {
        case AlarmLevel::Info:      return "Info";
        case AlarmLevel::Warning:   return "Warning";
        case AlarmLevel::Critical:  return "Critical";
        case AlarmLevel::Emergency: return "Emergency";
    }
    return "Warning";   // fallback:未知值用中间档,避免 UI 误判
}

QString JsonSerializer::directionToString(int direction) {
    switch (static_cast<Direction>(direction)) {
        case Direction::Above: return "Above";
        case Direction::Below: return "Below";
    }
    return "Above";   // fallback
}
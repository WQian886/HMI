// 阶段 6.2(2026-08-07):JSON 序列化 helper 实现

#include "JsonSerializer.h"
#include "Variable.h"
#include "Alarm.h"
#include "ActionManager.h"   // 需要 Mapping 完整定义

#include <QDebug>
#include <QMetaEnum>
#include <QStringList>

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
    // ★ 阶段 6.x.1(2026-08-11):分级轮询 —— 写 JSON 时也输出 alwaysPoll
    obj["alwaysPoll"] = v.alwaysPoll();

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
    obj["hysteresis"]       = cfg.hysteresis;   // ★ F2:滞回(老工程没这个字段也不影响读)
    // ★ F6:区域。老工程没这个键 → 读回来是空串 → "未分配"。
    //   写不写这一行都不会让老工程打不开,但【写出来】才能让配置对话框
    //   的回灌读回自己刚存的值 —— 存了不读回来等于没存。
    obj["area"]             = cfg.area;
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

// ===== 阶段 6.3 反向实现 =====

// Variable:从 JSON 构造 new Variable*(需要 VariableManager 作 parent,负责 setParent)
//   【背景】saveToFile 不存 runtime 字段(value/quality/timestamp/dirty),load 也不恢复这些
//   【改法】读 JSON 字段 → 用 QMetaEnum::keyToValue 反向解析 enum → 构造 Variable → 设 deadband/poll/range
//   【取舍】失败返回 nullptr(QMetaEnum 找不到 enum value);caller 决定怎么处理
//   【参照】Wqian 2026-08-07 阶段 6.3 开干
// ★ F3(2026-09-30)实测发现并修掉的 3 个问题都在这个函数里。
//   实测方式:手工改 JSON 加变量,再调生产 Project::loadFromFile(不是重写解析)。
//
// 【问题 1】失败只 return nullptr,调用方拿不到任何原因
//   改前:data_type 写成 "int16"(小写)、漏写 access —— 两种完全不同的错,
//        报出来是同一句 "variable 字段缺失或类型错误",且不含变量名。
//        200 个变量的工程里等于让人一个一个试。
//   改后:错误信息带上【变量名】+【哪个字段】+【合法值是什么】。
//
// 【问题 2】未知字段静默丢弃
//   用户手打 "dead_band"(少个下划线)→ 死区没生效,但他以为配了。
//   配错和没配长得一模一样 —— 静默失败。
//   改后:qWarning 点名变量和字段。不报错是为了留前向兼容余地
//        (新版本加的字段被老版本读到时不该直接拒绝加载)。
Variable* JsonSerializer::fromJsonVariable(const QJsonObject& obj,
                                           VariableManager* parent,
                                           QString* outError) {
    const auto fail = [outError](const QString& msg) -> Variable* {
        if (outError) *outError = msg;
        return nullptr;
    };

    QString name = obj["name"].toString();
    if (name.isEmpty()) {
        return fail(QStringLiteral("缺少 \"name\" 字段(变量名不能为空)"));
    }
    quint16 address = static_cast<quint16>(obj["address"].toInt(-1));
    if (address == 0xFFFF) {   // toInt(-1) 的默认值 = 字段缺失或不是数字
        return fail(QStringLiteral("变量 \"%1\": \"address\" 缺失或不是数字"
                                   "(PLC 寄存器号,0 起)").arg(name));
    }

    // ★ QMetaEnum 没有 keys() 成员(只有 keyCount/valueToKey),自己拼一遍合法值。
    //   报错里必须带这个 —— 用户知道自己填错了,却不知道该填什么的话,还是只能挨个试。
    const auto keysOf = [](const QMetaEnum& e) {
        QStringList ks;
        for (int i = 0; i < e.keyCount(); ++i) ks << QString::fromLatin1(e.valueToKey(i));
        return ks.join(QStringLiteral(", "));
    };

    // DataType 反向:QMetaEnum::keyToValue("Int16") → Variable::DataType::Int16
    QMetaEnum dataTypeEnum = QMetaEnum::fromType<Variable::DataType>();
    const QString dataTypeStr = obj["data_type"].toString();
    int dataTypeVal = dataTypeEnum.keyToValue(dataTypeStr.toLatin1().data());
    if (dataTypeVal < 0) {
        return fail(QStringLiteral("变量 \"%1\": \"data_type\" = \"%2\" 不合法\n"
                                   "  合法值:%3(注意大小写,小写 int16 无效)")
                        .arg(name, dataTypeStr, keysOf(dataTypeEnum)));
    }
    auto dataType = static_cast<Variable::DataType>(dataTypeVal);

    // Access 反向:同上
    QMetaEnum accessEnum = QMetaEnum::fromType<Variable::Access>();
    const QString accessStr = obj["access"].toString();
    int accessVal = accessEnum.keyToValue(accessStr.toLatin1().data());
    if (accessVal < 0) {
        return fail(QStringLiteral("变量 \"%1\": \"access\" = \"%2\" 不合法\n"
                                   "  合法值:%3(ReadOnly=传感器,ReadWrite=可写控制量)")
                        .arg(name, accessStr, keysOf(accessEnum)));
    }
    auto access = static_cast<Variable::Access>(accessVal);

    // ★ 6.3 修正:Variable 构造第 5 参数是 QObject*,VariableManager* 要 static_cast
    //   即使 include 了 VariableManager.h,有些编译器还是要求显式转换(保险)
    Variable* v = new Variable(name, address, dataType, access, static_cast<QObject*>(parent));
    v->setDeadband(obj["deadband"].toDouble());
    v->setPollIntervalMs(obj["poll_interval_ms"].toInt(1000));  // 默认 1000ms
    // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板):分级轮询 —— 从 JSON 读 alwaysPoll
    //   默认 false 兼容旧配置(没写 alwaysPoll 字段也能跳)
    v->setAlwaysPoll(obj["alwaysPoll"].toBool(false));

    // Range(Q2-B):对象形式 {min, max}
    if (obj.contains("range")) {
        QJsonObject range = obj["range"].toObject();
        v->setRange(range["min"].toVariant(), range["max"].toVariant());
    }

    // ★ F3(2026-09-30)问题 2:未知字段静默丢弃 → 改成点名警告。
    //   实测:塞一个 "myCustomJunk" 进去,加载完全成功、零警告,count 照样 8。
    //   用户手打 "dead_band"(少个下划线)就是这个下场 —— 死区没生效,
    //   但他以为配了。**配错和没配长得一模一样,这就是静默失败。**
    //   【为什么只警告不报错】要留前向兼容:新版本加的字段被老版本读到时
    //   不该直接拒绝加载整个工程。那属于"该字段当前版本用不上",
    //   和"用户拼错了"是两回事,不该同等对待。
    //   白名单 = toJson() 会写的字段(见本文件上方 toJson)+ range 内层。
    static const QStringList kKnown = {
        QStringLiteral("name"), QStringLiteral("address"),
        QStringLiteral("data_type"), QStringLiteral("access"),
        QStringLiteral("deadband"), QStringLiteral("poll_interval_ms"),
        QStringLiteral("alwaysPoll"), QStringLiteral("range"),
    };
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!kKnown.contains(it.key())) {
            qWarning().noquote()
                << QStringLiteral("[JsonSerializer] 变量 \"%1\" 含有未知字段 \"%2\",已忽略。"
                                   "拼错字段名是这个现象的常见原因(比如 dead_band ≠ deadband)。")
                       .arg(name, it.key());
        }
    }
    return v;
}

// CheckerConfig:从 JSON 构造(纯数据,无父对象)
CheckerConfig JsonSerializer::checkerConfigFromJson(const QJsonObject& obj) {
    CheckerConfig cfg;
    cfg.level         = alarmLevelFromString(obj["level"].toString());
    cfg.direction     = directionFromString(obj["direction"].toString());
    cfg.threshold     = obj["threshold"].toDouble();
    cfg.minDurationMs = obj["min_duration_ms"].toInt(500);  // 默认 500ms
    // ★ F2:滞回。老工程文件没这个键 → toDouble() 给 0.0,即"不启用"。
    //   【为什么默认必须是 0 而不是猜一个值】默认值猜错 = 静默改变所有老工程的报警行为。
    cfg.hysteresis    = obj["hysteresis"].toDouble(0.0);
    // ★ F6:区域。老工程文件没这个键 → toString() 给空串 → 未分配。
    //   同上,默认值必须老实地是空,不能猜一个区域名填进去 ——
    //   猜出来的区域会出现在筛选下拉里,而那些报警其实不属于它。
    cfg.area          = obj["area"].toString();
    return cfg;
}

// alarmLevel string → enum(失败 fallback Warning 中间档)
AlarmLevel JsonSerializer::alarmLevelFromString(const QString& s) {
    if (s == "Info")      return AlarmLevel::Info;
    if (s == "Warning")   return AlarmLevel::Warning;
    if (s == "Critical")  return AlarmLevel::Critical;
    if (s == "Emergency") return AlarmLevel::Emergency;
    return AlarmLevel::Warning;  // fallback:中间档,避免 UI 误判
}

// direction string → enum(失败 fallback Above)
Direction JsonSerializer::directionFromString(const QString& s) {
    if (s == "Above") return Direction::Above;
    if (s == "Below") return Direction::Below;
    return Direction::Above;  // fallback
}
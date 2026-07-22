// 阶段 3.1:Variable 实现

#include "Variable.h"
#include <cmath>  // 阶段 3.x:std::abs(死区过滤用)

namespace {
// 判断 QVariant 是否匹配指定的 DataType
// 用匿名 namespace,不暴露到 .h
bool matchesDataType(const QVariant& v, Variable::DataType t) {
    using Var = Variable::DataType;
    switch (t) {
        case Var::Bool:    return v.typeId() == QMetaType::Bool;
        case Var::Int16:   return v.typeId() == QMetaType::Short;
        case Var::UInt16:  return v.typeId() == QMetaType::UShort;
        case Var::Int32:   return v.typeId() == QMetaType::Int;
        case Var::Float:   return v.typeId() == QMetaType::Float;
    }
    return false;
}
} // namespace

Variable::Variable(const QString& name,
                   quint16 address,
                   DataType type,
                   Access access,
                   QObject* parent)
    : QObject(parent)
    , name_(name)
    , address_(address)
    , type_(type)
    , access_(access)
{
    // timestamp_ 默认是无效 QDateTime(构造函数自动)
    // quality_ 默认 Bad — 见 header 默认成员初始化
}

void Variable::setValue(const QVariant& v) {
    // 类型校验:不匹配就拒绝,warning 后直接 return
    if (!matchesDataType(v, type_)) {
        qWarning() << "Variable" << name_
                   << "type mismatch: expected" << dataTypeName(type_)
                   << ", got" << v.typeName();
        return;
    }
    // 订阅发布:先判断"旧值 vs 新值",再用结果决定要不要 emit
    // 阶段 3.x:死区过滤
    //   deadband_ == 0 → 走 QVariant !=(原行为,Bool/计数/状态码适用)
    //   deadband_ > 0  → 数值类型按 |新-旧| > deadband 判定(Float 必须,避免精度抖动)
    bool changed;
    if (deadband_ > 0.0 && value_.isValid()) {
        // 数值类型:用 double 减法判死区(Bool 在这分支也会被 toDouble() 转 0/1,逻辑自洽)
        double oldD = value_.toDouble();
        double newD = v.toDouble();
        changed = (std::abs(newD - oldD) > deadband_);
    } else {
        // 死区=0 或首次刷新:老逻辑,严格比较
        changed = (value_ != v);
    }
    value_ = v;
    // timestamp 跟 value 同步更新(保留原行为:调用一次就刷一次)
    timestamp_ = QDateTime::currentDateTime();
    // 仅在值真变了才发信号 — 死区过滤 / 轮询回包 / UI 刷新都挂这根信号
    if (changed) {
        emit valueChanged(*this);
    }
    // quality 不在这里改 — 调用方知道这次是正常数据还是异常数据
}

void Variable::setQuality(Quality q) {
    quality_ = q;
}

void Variable::setTimestamp(const QDateTime& t) {
    timestamp_ = t;
}

QString Variable::dataTypeName(DataType t) {// QMetaEnum::fromType 需要 Q_ENUM 注册,header 里已经加了
    // QMetaEnum::fromType 需要 Q_ENUM 注册,header 里已经加了
    return QString::fromLatin1(
        QMetaEnum::fromType<DataType>().valueToKey(static_cast<int>(t)));
}

QString Variable::accessName(Access a) {
    return QString::fromLatin1(
        QMetaEnum::fromType<Access>().valueToKey(static_cast<int>(a)));
}

QString Variable::qualityName(Quality q) {
    return QString::fromLatin1(
        QMetaEnum::fromType<Quality>().valueToKey(static_cast<int>(q)));
}
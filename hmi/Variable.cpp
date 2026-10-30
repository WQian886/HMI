// 阶段 3.1:Variable 实现

#include "Variable.h"
#include <cmath>  // 阶段 3.x:std::abs(死区过滤用)

// ★ D.5:别名增删(去重 + 跳过 name_)
void Variable::addAlias(const QString& alias) {
    if (alias.isEmpty() || alias == name_) return;
    if (!aliases_.contains(alias)) aliases_.append(alias);
}

void Variable::removeAlias(const QString& alias) {
    aliases_.removeAll(alias);
}

// ★ D.6:派生变量被重算时调 — 跳过类型/死区检查(外部算好的值)
void Variable::recomputeAsDerived(double value) {
    QVariant v = QVariant::fromValue<float>(static_cast<float>(value));
    // 派生变量 setValue 不走类型校验 + 不走范围 + 直接 emit(类似 checkRange=false)
    QDateTime oldTimestamp = timestamp_;
    Quality   oldQuality = quality_;
    value_ = v;
    timestamp_ = QDateTime::currentDateTime();
    quality_ = Quality::Good;
    emit valueChanged(*this);
    (void)oldTimestamp; (void)oldQuality;  // 派生值未来可对比变化率,留接口
}

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

Variable::SetValueResult Variable::setValue(const QVariant& v, bool checkRange) {
    // ★ G-23(2026-10-30):派生变量【不许】被外部写。
    //
    //   【为什么】
    //     派生量的值是【表达式算出来的】,既不是从 PLC 读来的,也不是谁该写进去的。
    //     让别人 setValue 有两个后果,第二个是硬的:
    //       ① 画面上显示一个跟表达式对不上的数 —— 撒谎;
    //       ② 写路径紧接着会 markDirty → WriteScheduler 把它当【真变量】发出去。
    //          而派生量【没有寄存器】,它的 address 是 0 ⇒ 会写到 plc1 的 0 号寄存器。
    //          那是【真的现场寄存器】,不是没人用的占位符。
    //
    //   【跟 PollScheduler 那道过滤的关系】
    //     PollScheduler::onTick 里 `if (v->isDerived()) continue;` 是读这一侧的门;
    //     本行是写这一侧的门。两道门都要有 —— 只加读那道门,界面照样能把
    //     派生量写进 PLC。
    //
    //   【派生量要改值,只有两条路】
    //     改表达式,或者改它依赖的源变量。recomputeAsDerived 是它自己的专属入口。
    if (isDerived_) {
        qWarning() << "Variable" << name_
                   << "is derived: 派生变量的值由表达式算出,不许被外部写"
                   << "(要改值请改表达式或改它的源变量)";
        return SetValueResult::Rejected;
    }
    // 类型校验:不匹配就拒绝,warning 后返 Rejected
    if (!matchesDataType(v, type_)) {
        qWarning() << "Variable" << name_
                   << "type mismatch: expected" << dataTypeName(type_)
                   << ", got" << v.typeName();
        return SetValueResult::Rejected;
    }
    // ★ 阶段 4.x 范围检查(2026-07-28):超出 [min, max] 拒绝,值不变,无信号
    //   checkRange=false 给 PLC 读路径(原始寄存器值可能超工程值范围)
    if (hasRange() && checkRange) {
        double d = v.toDouble();
        if (d < minValue_.toDouble() || d > maxValue_.toDouble()) {
            qWarning() << "Variable" << name_ << "out of range:" << v
                       << "not in [" << minValue_ << "," << maxValue_ << "]";
            return SetValueResult::Rejected;
        }
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
    timestamp_ = QDateTime::currentDateTime();
    // 仅在值真变了才发信号 — 死区过滤 / 轮询回包 / UI 刷新都挂这根信号
    if (changed) {
        value_ = v;
        timestamp_ = QDateTime::currentDateTime();
        emit valueChanged(*this);
        return SetValueResult::Accepted;  // 真改了
    }
    // quality 不在这里改 — 调用方知道这次是正常数据还是异常数据
    return SetValueResult::NoChange;  // 没改(死区 / 同值)
}

// 阶段 4.x:写范围检查 setter
void Variable::setRange(const QVariant& min, const QVariant& max) {
    minValue_ = min;
    maxValue_ = max;
}

void Variable::setQuality(Quality q) {
    if (quality_ == q) return;  // 相同 quality 不 emit(避免没必要的信号,跟 valueChanged 死区逻辑对齐)
    quality_ = q;
    // ★ IMPROVE-A3(2026-07-29):发信号让 Binding / Manager 能感知 quality 变化
    emit qualityChanged(q);
}

void Variable::setTimestamp(const QDateTime& t) {
    if (timestamp_ == t) return;  // 相同 timestamp 不 emit
    timestamp_ = t;
    // ★ IMPROVE-A3(2026-07-29):发信号,虽然现在没 UI 用,阶段 5 报警时间戳会用
    emit timestampChanged(t);
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
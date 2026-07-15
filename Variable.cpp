// 阶段 3.1:Variable 实现

#include "Variable.h"

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
    value_ = v;
    // timestamp 跟 value 同步更新
    timestamp_ = QDateTime::currentDateTime();
    // quality 不在这里改 — 调用方知道这次是正常数据还是异常数据
}

void Variable::setQuality(Quality q) {
    quality_ = q;
}

void Variable::setTimestamp(const QDateTime& t) {
    timestamp_ = t;
}

QString Variable::dataTypeName(DataType t) {
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
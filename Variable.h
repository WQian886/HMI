// 阶段 3.1:Variable 数据模型
// 描述 PLC 内一个数据点(地址 + 类型 + 值 + 质量 + 时间戳)
// 继承 QObject,为 3.2 阶段加 valueChanged 信号打底

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QDateTime>
#include <QMetaEnum>
#include <QtGlobal>

class Variable : public QObject {
    Q_OBJECT

public:
    // ===== 数据类型(PLC 那边对应的寄存器宽度) =====
    enum class DataType {
        Bool,    // 1 bit
        Int16,   // 16 位有符号
        UInt16,  // 16 位无符号
        Int32,   // 32 位有符号(占 2 个寄存器)
        Float    // 32 位浮点(占 2 个寄存器,Modbus IEEE754)
    };
	Q_ENUM(DataType) // 让 QMetaEnum 能反射枚举名字,方便 qDebug / UI 显示

    // ===== 访问权限 =====
    enum class Access {
        ReadOnly,   // 只读(传感器数据)
        ReadWrite   // 可读可写(控制量)
    };
    Q_ENUM(Access)

    // ===== 数据质量(工业三态,跟值分开传) =====
    // PLC 工程师都熟:Good = 值可信,Bad = 通信失败,Stale = 数据陈旧(超时未刷新)
    enum class Quality {
        Good,   // 正常,值可信
        Bad,    // 通信失败 / 解析错误(值不可信,不能用于控制)
        Stale   // 数据陈旧(超时未更新,但保留旧值供参考)
    };
    Q_ENUM(Quality)

    Variable(const QString& name,
             quint16 address,
             DataType type,
             Access access,
             QObject* parent = nullptr);//
    // ===== getters =====
    QString   name()      const { return name_; }//     
    quint16   address()   const { return address_; }//
    DataType  dataType()  const { return type_; }
    Access    access()    const { return access_; }
    QVariant  value()     const { return value_; }
    Quality   quality()   const { return quality_; }
    QDateTime timestamp() const { return timestamp_; }

    // ===== setters =====
    // setValue 自动更新 timestamp;quality 由调用方决定(因为可能是新值也可能是异常)
    void setValue(const QVariant& v);
    void setQuality(Quality q);
    void setTimestamp(const QDateTime& t);

    // ===== enum → 可读名字(给 qDebug / UI 显示用) =====
    // 用了 Q_ENUM 后,可以用 QMetaEnum 反射拿名字
    static QString dataTypeName(DataType t);
    static QString accessName(Access a);
    static QString qualityName(Quality q);

private:
    QString   name_;
    quint16   address_;
    DataType  type_;
    Access    access_;
    QVariant  value_;
    Quality   quality_   = Quality::Bad;  // 初始 Bad,直到首次 setValue
    QDateTime timestamp_;
};
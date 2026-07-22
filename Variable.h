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

    // ===== 信号 =====
    // 订阅发布的"发布"端 — Manager / UI 监听这个信号,决定何时刷新
    signals:
        void valueChanged(const Variable& v);

    // 注:signals: 段结束后必须紧跟一个 access 修饰符
    //    否则 moc 会把后续的构造函数当成"下一个信号"去解析,会报 Not a signal declaration
    public:
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

    // ===== 阶段 3.3:轮询周期(ms),PollScheduler 按此字段调度 =====
    // 默认 1000ms = 1 秒。改这个就改 PollScheduler 多久读一次 PLC
    int        pollIntervalMs() const { return pollIntervalMs_; }
    void       setPollIntervalMs(int ms) { pollIntervalMs_ = ms; }

    // ===== 阶段 3.x:死区阈值(0 = 不启用,>0 = 变化超过这个数才发信号)=====
    // 数值类型适用:Float 强烈建议(防精度抖动);计数器/状态码/控制字一般 0
    // Bool 也能用(0→1 差值 1,配 deadband=0.5 同样有效),但默认 0 不启用
    // 配置入口放阶段 6,先 setter 留出来,默认 0 向后兼容
    double     deadband() const { return deadband_; }
    void       setDeadband(double d) { deadband_ = d; }

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
	QString   name_;// PLC 工程师给的名字,唯一标识一个数据点
	quint16   address_;// PLC 内的地址(寄存器号 / 位号)
	DataType  type_;// 数据类型(PLC 那边对应的寄存器宽度)
	Access    access_;// 访问权限(只读 / 可读可写)
	QVariant  value_;// 当前值(PLC 内的值,可能是 bool/int/float 等)
    Quality   quality_   = Quality::Bad;  // 初始 Bad,直到首次 setValue
	QDateTime timestamp_;// 上次更新时间(PLC 内的值被刷新到本地的时间戳)
	int        pollIntervalMs_ = 1000;     // 阶段 3.3:轮询周期(ms),默认 1000
	double     deadband_       = 0.0;      // 阶段 3.x:死区阈值,0 = 不启用;>0 = |新-旧| > deadband 才发信号
};
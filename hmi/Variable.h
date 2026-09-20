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
        // ★ IMPROVE-A3(2026-07-29):quality / timestamp 单独发信号,阶段 5 报警要显示 quality
        //   (setQuality / setTimestamp 只改字段不 emit,UI 没法知道)
        void qualityChanged(Quality q);
        void timestampChanged(const QDateTime& t);

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

    // ★ 阶段 C · C.5(2026-09-18):plcId — 变量明确属于哪台 PLC(多 PLC 架构)
    //   教训 1.5:每个变量必须明确归属,1 driver = 1 PLC,故障隔离
    //   老工程默认 "plc1"(C.0 schema migrateV2ToV3 兼容)
    QString   plcId()     const { return plcId_; }
    void      setPlcId(const QString& id) { plcId_ = id; }

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

    // ===== ★ D.5(2026-09-20)别名(operator-facing display name)=====
    //   PLC 工程师起内部名(name_:"Speed"),操作员看屏幕想看"转速(rpm)"
    //   → 给 Variable 加 aliases_ 字段(可多个别名,如中英文)
    //   VariableManager 用 name 索引 + 别名反查(findByAlias)
    void addAlias(const QString& alias);
    void removeAlias(const QString& alias);
    QStringList aliases() const { return aliases_; }
    // ★ 便捷:首个别名(给 UI 单行显示用);空则返 name_
    QString displayName() const { return aliases_.isEmpty() ? name_ : aliases_.first(); }
    // 之前返 bool 语义模糊:死区过滤(值没真改)返 true 但没 emit valueChanged → 调用方误以为「改了」
    // 现在 3 态分清楚:
    //   Accepted  = 接受 + 真改了(emit valueChanged)
    //   NoChange  = 接受但死区过滤(没改,没 emit)— 跟轮询回包语义对齐
    //   Rejected  = 拒绝(类型不匹配 / 范围超)— 调用方应回滚 / 报警
    enum class SetValueResult {
        Accepted,   // 真改了
        NoChange,   // 没改(死区 / 同值)
        Rejected    // 拒绝(类型 / 范围)
    };
    Q_ENUM(SetValueResult)

    // setValue 自动更新 timestamp;quality 由调用方决定(因为可能是新值也可能是异常)
    // checkRange = false 给 PLC 读路径用(原始寄存器值可能超工程值范围,不查)
    SetValueResult setValue(const QVariant& v, bool checkRange = true);
    void setQuality(Quality q);
    void setTimestamp(const QDateTime& t);

    // ===== 阶段 4.x:写范围检查(可选,没配 = 不校验)=====
    // UI 输入 / 反向写前用;类型可不严格(toDouble 统一比)
    // 工业常见:温度 -100~200℃ / 压力 0~10 bar / 转速 0~3000 rpm
    void setRange(const QVariant& min, const QVariant& max);
    QVariant minValue() const { return minValue_; }
    QVariant maxValue() const { return maxValue_; }
    bool hasRange() const { return minValue_.isValid() && maxValue_.isValid(); }

    // ===== 阶段 4.3.0:写调度脏标记 =====
    // UI 用户主动 setValue 后调 markDirty(),告知 WriteScheduler「此变量待写 PLC」
    // WriteScheduler 写成功后调 clearDirty()
    // PLC 读回来的 setValue 不动 dirty(因为 PLC 值 == 本地值,自然无需再写)
    // 生命周期:UI 改 → markDirty → WriteScheduler 写成功 → clearDirty
    // 失败保留 dirty,下一帧 WriteScheduler 重试(决策 3:不回滚值)
    void markDirty()    { dirty_ = true; }
    void clearDirty()   { dirty_ = false; }
    bool isDirty() const { return dirty_; }

    // ===== 阶段 6.x.1:分级轮询标记 —— 未来 PollScheduler 用此判断筛选 =====
    bool alwaysPoll() const { return alwaysPoll_; }
    void setAlwaysPoll(bool v) { alwaysPoll_ = v; }

    // ★ IMPROVE-A4(2026-07-29):写失败重试计数,达到上限清 dirty + 报警 + 标 Bad
    //   防止 PLC 故障时反复写同一变量刷屏 + 打满总线
    void incrementWriteFail() { writeFailCount_++; }
    void clearWriteFail()     { writeFailCount_ = 0; }
    int  writeFailCount() const { return writeFailCount_; }

    // ===== enum → 可读名字(给 qDebug / UI 显示用) =====
    // 用了 Q_ENUM 后,可以用 QMetaEnum 反射拿名字
    static QString dataTypeName(DataType t);
    static QString accessName(Access a);
    static QString qualityName(Quality q);

private:
	QString   name_;// PLC 工程师给的名字,唯一标识一个数据点
	quint16   address_;// PLC 内的地址(寄存器号 / 位号)
    QString   plcId_ = QStringLiteral("plc1");  // ★ C.5:所属 PLC,默认 plc1(老工程兼容)
	DataType  type_;// 数据类型(PLC 那边对应的寄存器宽度)
	Access    access_;// 访问权限(只读 / 可读可写)
	QVariant  value_;// 当前值(PLC 内的值,可能是 bool/int/float 等)
    Quality   quality_   = Quality::Bad;  // 初始 Bad,直到首次 setValue
	QDateTime timestamp_;// 上次更新时间(PLC 内的值被刷新到本地的时间戳)
	int        pollIntervalMs_ = 1000;     // 阶段 3.3:轮询周期(ms),默认 1000
	double     deadband_       = 0.0;      // 阶段 3.x:死区阈值,0 = 不启用;>0 = |新-旧| > deadband 才发信号
    bool        dirty_          = false;    // 阶段 4.3.0:写调度脏标志。true = 本地值待写 PLC
    // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板):分级轮询策略 C —— 关键变量(报警/安全)标记
    //   背景:Weintek 多变量场景最佳实践;未来阶段 7 可用于"筛选纵轮询"
    //   取舍:默认 false 不影响现有配置;JSON 里加 "alwaysPoll": true 启用
    bool        alwaysPoll_     = false;
    int         writeFailCount_ = 0;           // ★ IMPROVE-A4(2026-07-29):写失败累计,达上限触发报警
    QVariant    minValue_;                      // 阶段 4.x:范围下限(无效 QVariant = 未配)
    QVariant    maxValue_;                      // 阶段 4.x:范围上限
    QStringList aliases_;                       // ★ D.5:别名列表(可空,默认空 = 用 name_ 显示)
};
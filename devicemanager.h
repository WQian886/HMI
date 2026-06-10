#pragma once
#include <QObject>
#include <QTimer>
#include <QtSerialBus/QModbusTcpClient>
#include <QModbusDataUnit>
#include <QVariant>

// ============================================================
// 地址映射结构体：把汇川地址翻译成 Modbus 地址
// ============================================================
struct AddressArea {
    QString name;       // 区域名：D, R, B, S, X, Y
    int minAddr;        // 最小地址
    int maxAddr;        // 最大地址
    int modbusBase;     // 对应 Modbus 起始地址
    bool isBit;         // true=位(线圈)，false=字(寄存器)
};

// ============================================================
// 变量结构体：一个可读写的变量
// ============================================================
struct DeviceVariable {
    QString name;       // 变量名：温度设定值
    QString area;       // 区域：D, R, B, S, X, Y
    int address;        // 地址：100
    QString dataType;   // 数据类型：INT16, UINT16, BOOL, FLOAT
    QVariant value;     // 当前值
    bool isReadOnly;    // 只读还是读写
	bool isBit;         // true=位(线圈)，false=字(寄存器)
};

// ============================================================
// 设备管理器：核心驱动层
// ============================================================
class DeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager();

    // ===== 连接 =====

    // 连接 PLC
    void connectToPLC(const QString& ip, quint16 port);

    // 断开连接
    void disconnectFromPLC();

    // 连接状态
    bool isConnected() const;

    // ===== 变量表管理 =====

    // 添加变量到变量表
    void addVariable(const DeviceVariable& var);

    // 删除变量（按名称）
    void removeVariable(const QString& name);

    // 清空变量表
    void clearVariables();

    // 获取变量当前值
    QVariant getVariableValue(const QString& name) const;

    // 获取变量表（用于显示）
    QList<DeviceVariable> getVariableTable() const;

signals:
    // 变量值变化（名称、值）
    void variableValueChanged(const QString& name, const QVariant& value);

    // 连接状态变化
    void connectionStateChanged(bool connected);

    // 错误信息
    void errorOccurred(const QString& msg);

    // 日志消息
    void logMessage(const QString& msg);

private slots:
    // 定时轮询所有变量
    void onPollingTimer();

    // 连接状态变化
    void onModbusStateChanged(QModbusDevice::State state);

    // 读取单个变量完成
   // void onReadFinished();

private:
    // 解析汇川地址，返回 Modbus 地址
    int translateAddress(const QString& area, int addr) const;

    // 根据变量创建 Modbus 请求
    QModbusDataUnit createReadRequest(const DeviceVariable& var) const;

    // 解析 Modbus 返回的数据
    QVariant parseReply(const QModbusDataUnit& reply, const DeviceVariable& var) const;

    // 汇川 H5U 地址映射表（固定）
    QList<AddressArea> getH5UAddressMap() const;

private:
    QModbusTcpClient* m_modbusClient = nullptr;
    QTimer* m_pollingTimer = nullptr;

    // 变量表
    QList<DeviceVariable> m_variables;

    // 当前的 Modbus 回复
    QModbusReply* m_currentReply = nullptr;
};
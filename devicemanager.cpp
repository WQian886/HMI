#include "devicemanager.h"
#include <QDebug>
#include <QModbusReply>

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent)
{
    // ===== 创建 MODBUS 客户端 =====
    m_modbusClient = new QModbusTcpClient(this);

    // ===== 创建轮询定时器 =====
    m_pollingTimer = new QTimer(this);
    m_pollingTimer->setInterval(500);  // 默认 500ms 轮询一次

    // ===== 连接信号 =====
    connect(m_modbusClient, &QModbusTcpClient::stateChanged,
        this, &DeviceManager::onModbusStateChanged);
    connect(m_pollingTimer, &QTimer::timeout,
        this, &DeviceManager::onPollingTimer);
}

DeviceManager::~DeviceManager()
{
    disconnectFromPLC();
}

// ===== 连接 PLC =====
void DeviceManager::connectToPLC(const QString& ip, quint16 port)
{
    if (!m_modbusClient) {
        emit errorOccurred("MODBUS 客户端未创建");
        return;
    }

    m_modbusClient->setConnectionParameter(
        QModbusDevice::NetworkAddressParameter, ip);
    m_modbusClient->setConnectionParameter(
        QModbusDevice::NetworkPortParameter, port);

    emit logMessage(QString("正在连接 %1:%2...").arg(ip).arg(port));
    m_modbusClient->connectDevice();
}
void DeviceManager::disconnectFromPLC()
{
    m_pollingTimer->stop();
    if (m_modbusClient) {
        m_modbusClient->disconnectDevice();
    }
}

bool DeviceManager::isConnected() const
{
    return m_modbusClient &&
        m_modbusClient->state() == QModbusDevice::ConnectedState;
}

// ===== 变量表管理 =====
void DeviceManager::addVariable(const DeviceVariable& var)
{
    // 检查是否已存在同名变量
    for (int i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].name == var.name) {
            m_variables[i] = var;  // 更新
            emit logMessage(QString("变量已更新: %1").arg(var.name));
            return;
        }
    }
    m_variables.append(var);
    emit logMessage(QString("变量已添加: %1 [%2%3]")
        .arg(var.name).arg(var.area).arg(var.address));
}

void DeviceManager::removeVariable(const QString& name)
{
    for (int i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].name == name) {
            m_variables.removeAt(i);
            emit logMessage(QString("变量已删除: %1").arg(name));
            return;
        }
    }
}

void DeviceManager::clearVariables()
{
    m_variables.clear();
    emit logMessage("变量表已清空");
}
QVariant DeviceManager::getVariableValue(const QString& name) const
{
    for (const auto& v : m_variables) {
        if (v.name == name) {
            return v.value;
        }
    }
    return QVariant();
}

QList<DeviceVariable> DeviceManager::getVariableTable() const
{
    return m_variables;
}
// ===== 定时轮询 =====
void DeviceManager::onPollingTimer()
{
    if (!isConnected() || m_variables.isEmpty()) {
        return;
    }

    // 每次轮询读取第一个变量（简单实现）
// 实际项目中会批量读取或分组读取
    static int currentIndex = 0;
    currentIndex = currentIndex % m_variables.size();
    DeviceVariable var = m_variables[currentIndex];
    // 创建读取请求
    QModbusDataUnit readUnit = createReadRequest(var);
    if (auto* reply = m_modbusClient->sendReadRequest(readUnit, 1)) {
        connect(reply, &QModbusReply::finished, this, [this, reply, var]() {
            if (reply->error() == QModbusDevice::NoError) {
                QModbusDataUnit result = reply->result();
                QVariant value = parseReply(result, var);
                // 检查值是否变化
                if (var.value != value) {
                    // 更新变量值
                    for (auto& v : m_variables) {
                        if (v.name == var.name) {
                            v.value = value;
                            break;
                        }
                    }
                    emit variableValueChanged(var.name, value);
                }
            }
            reply->deleteLater();
            });
    }
    currentIndex++;
}

// ===== 连接状态变化 =====
void DeviceManager::onModbusStateChanged(QModbusDevice::State state)
{
    if (state == QModbusDevice::ConnectedState) {
        emit connectionStateChanged(true);
        emit logMessage("连接成功");
        m_pollingTimer->start();
    }
    else if (state == QModbusDevice::UnconnectedState) {
        emit connectionStateChanged(false);
        emit logMessage("连接断开");
        m_pollingTimer->stop();
    }
}

// ===== 地址翻译（核心） =====
int DeviceManager::translateAddress(const QString& area, int addr) const
{
    // 汇川 H5U 地址映射表
    struct AreaMap {
        QString name;
        int base;
    };
    static const QList<AreaMap> map = {
    {"D", 0},         // D0-D7999 → 寄存器 0-7999
    {"R", 32768},     // R0-R32767 → 寄存器 32768-65535
    {"B", 12288},     // B0-B32767 → 线圈 12288-45055
    {"S", 57344},     // S0-S4096 → 线圈 57344-61439
    {"X", 63488},     // X0-X1777(八进制) → 线圈 63488-64511
    {"Y", 64512},     // Y0-Y1777(八进制) → 线圈 64512-65535
    };


    for (const auto& m : map) {
        if (m.name == area) {
            return m.base + addr;
        }
    }

    return -1;  // 无效地址
}

QModbusDataUnit DeviceManager::createReadRequest(const DeviceVariable& var) const
{
    int modbusAddr = translateAddress(var.area, var.address);
    if (var.isBit) {
        // 位变量：读线圈
        return QModbusDataUnit(QModbusDataUnit::Coils, modbusAddr, 1);
    }
    else {
        // 字变量：读保持寄存器
        return QModbusDataUnit(QModbusDataUnit::HoldingRegisters, modbusAddr, 1);
    }
}
QVariant DeviceManager::parseReply(
    const QModbusDataUnit& reply, const DeviceVariable& var) const
{
    if (reply.valueCount() == 0) {
        return QVariant();
    }
    if (var.dataType == "BOOL") {
        return reply.value(0) != 0;
    }
    else if (var.dataType == "FLOAT") {
        // 浮点数：两个寄存器组成
        if (reply.valueCount() >= 2) {
            quint16 low = reply.value(0);
            quint16 high = reply.value(1);
            float f;
            quint32 bits = (static_cast<quint32>(high) << 16) | low;
            memcpy(&f, &bits, sizeof(float));
            return f;
        }
    }
    // 默认 INT16 / UINT16
    return static_cast<int>(reply.value(0));
}

// ===== 汇川 H5U 地址映射表（供外部查询） =====
QList<AddressArea> DeviceManager::getH5UAddressMap() const
{
    return {
    {"D", 0, 7999, 0, false},
    {"R", 0, 32767, 32768, false},
    {"B", 0, 32767, 12288, true},
    {"S", 0, 4096, 57344, true},
    {"X", 0, 1777, 63488, true},
    {"Y", 0, 1777, 64512, true},
    };
}
// ============================================================================
// 阶段 C · C.1 · 协议抽象层 - 统一接口
//
// 文件职责:定义 IPollDriver 抽象接口,让 3 个协议(Modbus/S7/EtherNet/IP)
//   都能通过同一接口被业务层调用。
//
// 关键设计:
//   - 通用 read(area, addr, count):支持 4 种数据区域,不再只支持 HoldingReg
//   - 通用 readFinished 信号:QVector<quint16> 而非 QModbusDataUnit(协议无关)
//   - sendReadRequest 异步(沿用 ModbusMaster 的 QEventLoop 模式,S7/EIP 也兼容)
//   - 100% 行为兼容:ModbusDriver 包装现有 ModbusMaster,所有 80 个测试不破
//
// 跟 B3 的关系:
//   - B3 Permission 仍是兜底(动作发生瞬间拦截)
//   - C.1 IPollDriver 让业务层不知道也不关心协议(协议无关)
//   - 两者并存:C.1 + B3 + B4 = 完整架构
//
// 关联:PollScheduler.h, ModbusDriver.h, S7Driver.h(未来), DriverFactory.h
// ============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace qwhmi::drivers {

// 通用数据区域(替代原 QModbusDataUnit::RegisterType,跨协议通用)
//   - Modbus: HoldingReg / InputReg / Coils / DiscreteInput 全支持
//   - S7:     HoldingReg↔DBW / InputReg↔IW / Coils↔位输出 / DiscreteInput↔位输入
//   - EIP:    主要用 HoldingReg,其他有限支持
enum class RegisterArea {
    HoldingReg    = 0,  // 保持寄存器 / DBW
    InputReg      = 1,  // 输入寄存器 / IW
    Coils         = 2,  // 线圈 / 位输出
    DiscreteInput = 3,  // 离散输入 / 位输入
};

class IPollDriver : public QObject {
    Q_OBJECT
public:
    // 显式 default 构造(Q_OBJECT 宏 + virtual 析构会让编译器不生成默认构造,需手动加)
    IPollDriver() = default;
    explicit IPollDriver(QObject* parent) : QObject(parent) {}    // 支持 DriverFactory::create("modbus", parent)
    virtual ~IPollDriver() = default;

    // ===== 连接管理 =====
    //   用 connectTo 跟 Qt 命名一致(QTcpSocket / QModbusClient 都是 connectTo)
    //   异步:连接成功通过 connected() 信号通知(不阻塞)
    virtual void connectTo(const QString& host, quint16 port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // ===== in-flight 状态(给 PollScheduler 调度用)=====
    //   Modbus 协议串行:1 in-flight
    //   S7 / EIP 协议:可多个
    //   driver 自己保证不超协议上限
    virtual int pendingCount() const = 0;

    // ===== 异步读请求(沿用 ModbusMaster 模式)=====
    struct ReadRequest {
        RegisterArea area = RegisterArea::HoldingReg;
        int slaveId = 1;        // Modbus 用,S7/EIP 当设备 ID 或忽略
        int addr = 0;
        int count = 1;
        quint32 requestId = 0; // driver 透传,reply 时回带
    };
    // ★ 2b(2026-10-19):改成返回 bool,与 sendWriteRequest 对称。
    //
    //   【为什么必须改】原来返回 void,而底层 ModbusMaster::sendReadRequest
    //   拒收请求时返回 nullptr 并【不发 readFinished】(只 emit errorOccurred)。
    //   这个"信息"就在返回值里,接口层把它丢了,于是 PollScheduler 无从知道
    //   请求有没有被接收 —— 只能先乐观登记 pending_[reqId],然后等一个
    //   【永远不会来的】reply,把变量永久留在在途态。
    //   现场表现:同一台 PLC 上地址连续的那几个变量有值,中间跳过几个地址的
    //   那几个永远停在旧值,而日志里只有一句"上一帧未完成,稍后再发"。
    //
    //   【返回值语义】true = 已接收,【必定会】来一个 readFinished(成功或失败都算);
    //                     false = 已拒绝,不会来 readFinished,调用方要负责回滚。
    //   注意 stub driver(S7/EIP)会立刻 emit readFinished(false) 然后返回 true ——
    //   它们"接收了请求",失败是通过信号报的,不是通过返回值。
    virtual bool sendReadRequest(const ReadRequest& req) = 0;

    // ===== 异步写请求 =====
    virtual bool sendWriteRequest(RegisterArea area,
                                   int slaveId,
                                   int addr,
                                   const QVector<quint16>& values,
                                   quint32 requestId) = 0;

    // ===== 协议标识(给 UI 显示 / AuditLog 用)=====
    virtual QString protocolName() const = 0;  // "modbus"/"s7"/"eip"

signals:
    // ★ C.1 关键改动:readFinished 从 QModbusDataUnit → QVector<quint16>
    //   PollScheduler 不再依赖 QModbusDataUnit,只看到 raw 16-bit register 数组
    void readFinished(quint32 requestId, QVector<quint16> registers, bool success);

    // writeFinished(success/fail 区分,跟 ModbusMaster 一致)
    void writeFinished(quint32 requestId, bool success);

    // 连接状态
    void connected();
    void disconnected();

    // 错误(给 UI 显示)
    void errorOccurred(const QString& msg);
};

} // namespace qwhmi::drivers

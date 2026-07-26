// 阶段 4.3.0:WriteScheduler 头文件
//
// 与 PollScheduler 对称:读通路是 PollScheduler 周期性 modbus 读,Variable 拿到值;
//                  写通路是 WriteScheduler 周期性扫 dirty Variable,modbus 写 PLC。
//
// 数据流:
//   用户改值 → v->setValue → v->markDirty       (Binding::bindEdit ② 槽)
//                                ↓
//   WriteScheduler::onTick(100ms 一次)
//       找 dirty + ReadWrite + 非 in-flight 的 Variable
//       按数据类型选 FC:Bool→0x05 / UInt16/Int16→0x06 / Int32/Float→0x10
//       modbus_->sendWriteRequest(unit, slaveId=1, reqId)
//                                ↓
//   ModbusMaster::handleWriteResponse / onTimeoutScan
//       emit writeFinished(reqId, success)
//                                ↓
//   WriteScheduler::onWriteReply
//       ├─ success → v->clearDirty + qDebug("OK")
//       └─ fail    → qWarning("FAIL, dirty 保留等下一帧重试")
//
// 决策落地(2026-07-26 拍板):
//   1. 写功能码映射:Bool→0x05 / UInt16/Int16→0x06 / Int32/Float→0x10 (2 reg)
//   2. 写策略:每 tick 只发一个 dirty(简化,够 demo 用;后续可改批量)
//   3. 失败处理:不回滚值 + dirty 保留等下帧重试 + qDebug 提示
//   4. 优先级:FIFO;Safety Variable 单独加锁,留给后续阶段

#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>

class Variable;
class VariableManager;
class ModbusMaster;

class WriteScheduler : public QObject {
    Q_OBJECT
public:
    explicit WriteScheduler(VariableManager* mgr, ModbusMaster* modbus, QObject* parent = nullptr);

    void start();
    void stop();
    bool isRunning() const { return timer_.isActive(); }

private slots:
    void onTick();
    void onWriteReply(int requestId, bool success);

private:
    VariableManager* varMgr_;
    ModbusMaster*    modbus_;
    QTimer           timer_;                           // 100ms tick
    QHash<int, Variable*> pending_;                    // requestId → 等待 ack 的 Variable
    int nextReqId_ = 1;                                // 请求 id 自增
};
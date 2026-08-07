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
#include <QList>
#include <QString>
#include <QDateTime>
#include <QMetaType>  // 阶段 4.x 写审计日志:Q_DECLARE_METATYPE

// 阶段 4.x 写审计日志(2026-07-28):一条写操作的记录
// 放顶层,信号参数用更顺;moc 自定义类型 emit 需要 Q_DECLARE_METATYPE
struct WriteAuditEntry {
    QDateTime timestamp;     // 写动作发生时间
    QString   variableName;  // Variable 名("Speed")
    QString   newValue;      // 写出去的值
    bool      success;       // true = 写成功
    QString   address;       // "0x0002"
    QString   resultText;    // "OK" / "FAIL"
};
Q_DECLARE_METATYPE(WriteAuditEntry)

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

    // 阶段 4.x 写审计日志(2026-07-28):给 UI 一次性拉所有记录
    QList<WriteAuditEntry> auditLog() const { return auditLog_; }

signals:
    // 阶段 4.x 写审计日志(2026-07-28):每条新记录都发,UI 接了加行
    void auditAppended(const WriteAuditEntry& entry);
    // ★ IMPROVE-A4(2026-07-29):写失败达上限报警 → 阶段 5 报警系统接
    //   决策:超限后清 dirty(工业铁律 — 失败不能无限重发),让操作员决定后续
    void writeFailed(const QString& varName, const QString& reason);

private slots:
    void onTick();
    void onWriteReply(quint32 requestId, bool success);

    // ★ 阶段 6.3(2026-08-07):监听 varMgr_->variableReplaced,更新 pending_ 里的失效指针
    //   防止 loadFromFile 时 in-flight 写请求的 reply 触发 UAF(v 已被 delete)
    void onVariableReplaced(Variable* oldVar, Variable* newVar);

private:
    // ★ IMPROVE-A4(2026-07-29):写失败重试上限(超过报警 + 清 dirty)
    static constexpr int kMaxWriteRetries = 3;

    VariableManager* varMgr_;
    ModbusMaster*    modbus_;
    QTimer           timer_;                           // 100ms tick
    QHash<quint32, Variable*> pending_;                // requestId → 等待 ack 的 Variable
    // ★ IMPROVE-A2(2026-07-29):quint32 防回绕
    quint32 nextReqId_ = 1;                            // 请求 id 自增
    QList<WriteAuditEntry> auditLog_;                  // 阶段 4.x 写审计日志
};
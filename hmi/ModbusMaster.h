#pragma once

// ============================================================
// 阶段 2 第 4 练 — 重设计版（跟 Qt 官方 QModbusClient 统一接口）
// 作者:Wqian + 子衿
// 时间:2026-07-07
// ============================================================
// 设计原则:
//   1. 用 QModbusDataUnit 抽象数据单元(读/写共用)
//   2. 用 QModbusReply 表达回复(继承,内部置完成态)
//   3. sendReadRequest / sendWriteRequest 两个方法(不是按 fc 一个一个)
//   4. type → fc 内部映射(Holding=0x03/0x06/0x10, Coils=0x01/0x05/0x0F)
//   5. onTcpReadyRead 按 type switch 分发(读:解析数据 / 写:echo 验证)
//
// 跟 Qt 官方 QModbusClient 接口一致(你以后看 Qt 文档直接能查)
// ============================================================

#include <QObject>
#include <QModbusReply>           // 阶段 2-3 起真用
#include <QModbusDataUnit>        // 阶段 2-4 抽象数据单元
#include <QModbusClient>          // 阶段 2-4 用于 QModbusDataUnit::RegisterType
#include <QByteArray>
#include <QHash>
#include <QTimer>
#include <QDateTime>
#include "TcpClient.h"

class ModbusMaster : public QObject {
    Q_OBJECT
public:
    explicit ModbusMaster(QObject* parent = nullptr);

    // 连接管理(沿用阶段 2-3)
    void connectTo(const QString& host, quint16 port);
    bool isConnected() const;

    // ============================================================
    // 阶段 2-4 重设计:跟 Qt 官方 QModbusClient 接口一致
    // ============================================================
    // 读请求:返回 reply 句柄,用户接 finished 信号拿数据
    // 阶段 3.3 加 requestId(default 0 = 旧调用方式兼容)
    // ★ IMPROVE-A2(2026-07-29):quint32 防回绕,跟 PollScheduler / WriteScheduler 对齐
    QModbusReply* sendReadRequest(const QModbusDataUnit& readUnit, int serverAddress, quint32 requestId = 0);

    // 写请求:返回 reply 句柄,finished 信号表示写完成
    // 阶段 4.3.0:加 requestId(默认 0 = 旧调用方式兼容),跟 writeFinished 配合
    QModbusReply* sendWriteRequest(const QModbusDataUnit& writeUnit, int serverAddress, quint32 requestId = 0);

signals:
    // 错误 + 状态
    void errorOccurred(QString msg);// 错误信号,UI 显示
	void connected();// 连接成功信号,UI 显示
	void disconnected();// 断开连接信号,UI 显示
    // 2026-07-09 新增:pending_ 变空时发 → UI 层可借此重新 enable 发送按钮
    // 触发时机:正常响应 / 异常响应 / 超时清理 / TCP 断开后
    void idle();
    // 阶段 3.3:read 成功后发 → PollScheduler 用 requestId 反查 + 分发到 Variable
    // ★ IMPROVE-A2(2026-07-29):quint32 跟 Pending.requestId + PollScheduler / WriteScheduler 对齐
    void readFinished(quint32 requestId, QModbusDataUnit unit, bool success);
    // 阶段 4.3.0:write 完成后发 → WriteScheduler 用 requestId 反查 + 清 dirty
    // 成功 / 失败 都发(success 区分),失败时 WriteScheduler 保留 dirty 等下一帧重试
    void writeFinished(quint32 requestId, bool success);

private slots:
    // TCP 数据到达 → 拆 MBAP 头 + 按 type 分发拆帧
    void onTcpReadyRead(const QByteArray& data);

    // 50ms 周期扫 pending_,超时请求清理
    void onTimeoutScan();

private:
    // ============================================================
    // 内部类:继承 QModbusReply,装上我们的上下文(type + reply 回填)
    // ============================================================
    class PendingReply : public QModbusReply {
    public:
        PendingReply(QModbusDataUnit::RegisterType type, int serverAddress, QObject* parent = nullptr)
            : QModbusReply(QModbusReply::Common, serverAddress, parent)
            , type_(type) {}

        QModbusDataUnit::RegisterType type() const { return type_; }

    private:
        QModbusDataUnit::RegisterType type_;
    };

    // type → fc 单向映射
    static quint8 typeToReadFc(QModbusDataUnit::RegisterType type);
    static quint8 typeToWriteFc(QModbusDataUnit::RegisterType type, bool single);

    // 请求上下文(TID → {serverAddress, type, reply, sentAt, isWrite})
    // 2026-07-08:上移到 helper 方法之前 —— MSVC /permissive- 不允许 Pending
    // 在使用后才声明(原来在这下面定义,导致 handleReadResponse/handleWriteResponse
    // 参数 Pending* 找不到类型)
    struct Pending {
        int serverAddress;
        QModbusDataUnit::RegisterType type;
        PendingReply* reply = nullptr;     // 拆帧完成时 setResult + setFinished
        QDateTime sentAt;
        int      timeoutMs;
        bool     isWrite;
        // ★ IMPROVE-A2(2026-07-29):quint32 跟 sendReadRequest / sendWriteRequest 参数对齐
        quint32  requestId = 0;            // 阶段 3.3:PollScheduler 用来反查
    };

    // 帧组装/拆帧 helper(Pending 必须在这些之前声明)
	QByteArray buildFrame(quint16 tid, int serverAddress, quint8 fc, const QByteArray& pduBody);// 组装请求帧
	void handleReadResponse(quint8 fc, Pending* req, const QByteArray& frame);// 处理读响应帧
	void handleWriteResponse(quint8 fc, Pending* req, const QByteArray& frame);// 处理写响应帧

	// ★ IMPROVE-B5(2026-07-29):错误收尾 helper,5 处重复 setError/setFinished/deleteLater 提一处
	//   【背景】异常响应 / 帧太短 / 数据不足 / echo 错 等错误路径都重复:
	//          emit errorOccurred + reply->setError(ProtocolError) + setFinished + deleteLater
	//   【改法】提 helper,所有错误路径调一次
	//   【取舍】helper 只覆盖"emit + reply 收尾",其他清理(pending_.erase 等)在调用方做
	void finalizeErrorReply(Pending* req, const QString& msg);

    // 2026-07-09 新增:erase 后 / clear() 后调用 —— pending_ 变空就 emit idle
    void notifyIdleIfEmpty() {
        if (pending_.isEmpty()) emit idle();
    }

public:
    // 阶段 4.3.0 调试暴露:WriteScheduler 用它判断"上一帧未完成"避免自己撞自己
    int pendingCount() const { return pending_.size(); }

private:
    TcpClient* tcp_ = nullptr;
    QByteArray recvBuf_;
    // ★ BUGFIX-A1(2026-07-30):quint32 → quint16,防 buildFrame 形参截断
    //   【背景】原 nextTid_ 用 quint32(防 7.6 天回绕),pending_ key 也是 quint32,
    //          但 buildFrame 形参是 quint16,编译器静默截断高位。
    //          TID > 0xFFFF 时发出去的 MBAP TID 是低 16 位,响应 echo 也是低 16 位,
    //          但 pending_ key 存的是完整 quint32 → 响应永远匹配不上 → 循环超时。
    //   【Bug 现象】Wqian 2026-07-30 报"运行几小时一直弹窗 Modbus 错误 TID=65543",
    //          65543 = 0x10007,实际发出去是 0x0007,响应解析也是 0x0007,
    //          pending_ key 是 0x10007 → find 失败 → 3 秒超时 → 循环。
    //   【修法】nextTid_ + pending_ key + 响应解析 全改回 quint16,跟 buildFrame 一致。
    //   【参照】MBAP 协议规定 TID 是 2 字节(uint16);之前 07-29 IMPROVE-A1 改过 quint32。
    //   【取舍】quint16 跑 1.8 小时回绕一次,但 ModbusMaster 单 in-flight 设计
    //          (pendingCount 限流)保证回绕时 pending_ 一定是空的,不会撞 key。
    quint16 nextTid_ = 1;

    // ★ BUGFIX-A1(2026-07-30):key 类型从 quint32 改回 quint16(跟 buildFrame 一致)
    QHash<quint16, Pending> pending_;

    QTimer* timeoutTimer_ = nullptr;
};
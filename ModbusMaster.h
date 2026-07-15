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
    QModbusReply* sendReadRequest(const QModbusDataUnit& readUnit, int serverAddress);

    // 写请求:返回 reply 句柄,finished 信号表示写完成
    QModbusReply* sendWriteRequest(const QModbusDataUnit& writeUnit, int serverAddress);

signals:
    // 错误 + 状态
    void errorOccurred(QString msg);// 错误信号,UI 显示
	void connected();// 连接成功信号,UI 显示
	void disconnected();// 断开连接信号,UI 显示
    // 2026-07-09 新增:pending_ 变空时发 → UI 层可借此重新 enable 发送按钮
    // 触发时机:正常响应 / 异常响应 / 超时清理 / TCP 断开后
    void idle();

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
    };

    // 帧组装/拆帧 helper(Pending 必须在这些之前声明)
	QByteArray buildFrame(quint16 tid, int serverAddress, quint8 fc, const QByteArray& pduBody);// 组装请求帧
	void handleReadResponse(quint8 fc, Pending* req, const QByteArray& frame);// 处理读响应帧
	void handleWriteResponse(quint8 fc, Pending* req, const QByteArray& frame);// 处理写响应帧

    // 2026-07-09 新增:erase 后 / clear() 后调用 —— pending_ 变空就 emit idle
    void notifyIdleIfEmpty() {
        if (pending_.isEmpty()) emit idle();
    }

private:
    TcpClient* tcp_ = nullptr;
    QByteArray recvBuf_;
    quint16 nextTid_ = 1;

    QHash<quint16, Pending> pending_;

    QTimer* timeoutTimer_ = nullptr;
};
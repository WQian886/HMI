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
// ★ 2026-10-20:通讯帧监视。方向枚举放在 comm/CommFrame.h ——
//   本文件(项目根)和 src/comm/CommTraceModel.h 两边都要用,
//   互相 include 会成环,所以枚举单独放一个没有依赖的小头文件。
#include "comm/CommFrame.h"

class ModbusMaster : public QObject {
    Q_OBJECT
public:
    explicit ModbusMaster(QObject* parent = nullptr);

    // 连接管理(沿用阶段 2-3)
    void connectTo(const QString& host, quint16 port);
    // ★ C.1(2026-09-17):ModbusDriver::disconnect() 需要调这个关闭连接
    //   内部一行 tcp_->close()(C.1 之前 IPollDriver 接口名是 disconnect,ModbusMaster 没这方法)
    void disconnect();
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

    // ★ 2026-10-20:通讯帧监视(调试区第三页"通讯监视")。
    //
    //   【为什么挂在这里,而不是 TcpClient】
    //     TcpClient 那层只有裸字节:它不知道什么是"一帧",
    //     更不知道收到的那一帧对应哪个请求 —— 而"往返耗时"和
    //     "这个响应是哪次请求的"都必须有 TID 才能算。
    //     本类同时握有三样东西:
    //       ① 请求帧的完整字节(buildFrame 的返回值)
    //       ② 响应帧的完整字节(onTcpReadyRead 里拆出来的 frame)
    //       ③ 请求发出的时刻(pending_[tid].sentAt)
    //     所以耗时 = now - sentAt,在这里才算得出来。
    //
    //   【elapsedMs 的含义】
    //     Rx  = 往返耗时(收到响应 - 发出请求)
    //     Tx  = -1(发的时候还不知道要等多久)
    //     Error = 异常响应/超时,能算就带耗时,算不了是 -1
    //
    //   【注意】这个信号【每帧都发】。轮询 100ms 一帧,
    //     跑一夜就是 30 万次 —— 消费方必须自己限制条数
    //     (CommTraceModel 有行数上限,超了淘汰最旧的)。
    void frameTraced(qwhmi::comm::FrameDir dir, const QByteArray& raw,
                     const QString& summary, qint64 elapsedMs);

private slots:
    // TCP 数据到达 → 拆 MBAP 头 + 按 type 分发拆帧
    void onTcpReadyRead(const QByteArray& data);

    // 100ms 周期扫 pending_,超时请求清理
    // ★ 2026-10-14 更正:这里原来写的是 50ms,早就不准了 ——
    //   ModbusMaster.cpp:41 是 timeoutTimer_->setInterval(100),
    //   同类两个文件两个数字。定这个数的是 IMPROVE-A8(2026-07-29),
    //   为了跟 PollScheduler / WriteScheduler 的 100ms tick 对齐。
    //   改任何一处记得两处一起改。
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
	// (buildFrameForTest 在下面 public 段 —— 见那里的说明)

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

    // ★ 2026-10-14:buildFrame 的测试出口(改善清单 #13)。
    //   【为什么需要】ModbusMaster 从阶段 2 起就是核心类,69 个测试目标里
    //   【零个】断言过它组出来的任何一个字节 —— 直到要做 #13
    //   (把手写大端 static_cast<char>((x>>8)&0xFF) 换成 qToBigEndian)才发现:
    //   清单把 #13 标成"纯可读性,零行为变化",而这句话【没有任何测试支撑】,
    //   没有任何东西能证明换完字节一样。这正是本项目反复吃的
    //   "看起来安全的重构":改了没人知道,坏了也没人知道。
    //   先把这个出口加上、把现字节钉成基线,再动生产代码。
    //
    //   【边界】只暴露纯函数式的帧组装,不碰 pending_/tcp_/socket,
    //   所以不需要真连 PLC 就能断言完整 MBAP 头。
    //   见 tests/tst_ModbusFrame.cpp。
    QByteArray buildFrameForTest(quint16 tid, int serverAddress, quint8 fc, const QByteArray& pduBody) {
        return buildFrame(tid, serverAddress, fc, pduBody);
    }

    // ★ 2026-10-14:PDU body 组装(改善清单 #13 从 sendReadRequest/sendWriteRequest 里提取)
    //
    //   【为什么要提取】这两段原来是内联在两个发送函数里的,加上 buildFrame,
    //   全文件有 10 处手写 `static_cast<char>((x >> 8) & 0xFF)`。
    //   内联在发送函数里还有个更实际的后果:**离线测不到** ——
    //   它们要真连上 PLC 才走得到,所以本机没有 PLC 时这些字节从来没被验证过。
    //   提成静态函数后不需要连接就能断言完整 body。
    //
    //   【提成静态(不依赖实例)】它们只用到入参,不碰 pending_/tcp_,
    //   所以是纯函数 —— 纯函数才值得单测。
    //
    //   读请求 body = addr(2) + count(2)
    static QByteArray buildReadBody(quint16 addr, quint16 count);

    // 写请求 body,按 fc 分两种格式(见 .cpp 里那句"之前误用 0x10 格式发 0x0F"的注释):
    //   0x05/0x06 写单   = addr(2) + value(2)
    //   0x0F 多线圈      = addr(2) + count(2) + byteCount(1) + bit-packed(LSB first)
    //   0x10 多寄存器    = addr(2) + count(2) + byteCount(1) + values(N*2,大端)
    static QByteArray buildWriteBody(quint8 fc, quint16 addr, const QList<quint16>& values);

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
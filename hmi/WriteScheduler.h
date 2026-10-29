// ============================================================================
// WriteScheduler 头文件
//
// 与 PollScheduler 对称:读通路是 PollScheduler 周期性 modbus 读,Variable 拿到值;
//                       写通路是 WriteScheduler 周期性扫 dirty Variable,modbus 写 PLC。
//
// 数据流(2b 之后,★ 标的是本次改的):
//   用户改值 → v->setValue → v->markDirty       (Binding::bindEdit ② 槽)
//                                ↓
//   WriteScheduler::onTick(100ms 一次)
//       找 dirty + ReadWrite 的 Variable
//       ★ 按 plcId 分桶,逐 PLC 取 driver(★ 原来全代码零 plcId)
//       按数据类型选 FC:Bool→0x05 / UInt16/Int16→0x06 / Int32/Float→0x10
//       ★ sendWriteRequest(area, slaveId=provider 取的, addr, values, reqId)
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
//   2. 写策略:每台 PLC 每帧只发一个(多 PLC 之前是"全局每帧一个")
//   3. 失败处理:不回滚值 + dirty 保留等下帧重试 + qDebug 提示
//   4. 优先级:FIFO;Safety Variable 单独加锁,留给后续阶段
//
// ============================================================================
// ★ 多 PLC 2b(2026-10-19)—— 本文件被改了什么,以及为什么
// ============================================================================
//
// 【改之前的真实行为 —— 这是本文件最要紧的一句】
//   全代码【零 plcId】。构造函数收一个 ModbusMaster*,onTick 直接
//   modbus_->sendWriteRequest(unit, 1, reqId) —— 那一个 driver 是
//   plcs[0] 建出来的,那个 slaveId 是写死的字面量 1。
//
//   后果:用户在变量上填 plc2,界面上值变了、审计日志记了、
//   WriteFailChecker 还可能为它报过警 —— 而字节从 plc1 的 socket 发了出去。
//   ★ 没有任何一行报错。操作员点 2 号机的按钮,1 号机的数值变了。
//
// 【改法】
//   构造函数从 ModbusMaster* 换成 IPlcDriverProvider*,写之前按 plcId 问
//   provider 要 driver 和 slaveId。provider 是 PlcConnectionManager 实现的,
//   但本类不依赖那个实现类 —— 测试能塞一个 10 行的假 provider。
//
// ★★ 为什么"读侧不走 provider、写侧走",这个不对称是真的不对称:
//   PollScheduler 自己持有 drivers_ 哈希(它是读通路的唯一持有者),
//   所以它不需要再绕一圈问别人;WriteScheduler 从来没持有过 driver,
//   它以前拿到的是别人递进来的单指针。两种形状各有历史原因,
//   不统一是因为统一要动的代码比留着的多。
//
// 【三个刻意的设计,改之前先读】
//
// ① nextReqId_ 必须进程内【全局单调】,不能各 driver 一份
//    三个 ModbusMaster 各自的 nextTid_ 都从 1 开始。TID 归 ModbusMaster、
//    requestId 归本调度器,是两个独立的编号空间 —— 混用会让 onWriteReply
//    按 reqId 反查时张冠李戴。所以 reqId 只由本类一个计数器发。
//
// ② pending_ 的值用 QPointer<Variable>,不是裸指针
//    在途请求发出去之后、reply 回来之前,用户可以换个工程 —— 那会把
//    Variable 删掉。裸指针 + onVariableReplaced 的手工修补覆盖不全:
//    那个信号只在"替换"时发,clear() 那种"直接没了"的情况它压根不发。
//    QPointer 自己在被删时置空,reply 回来时判一下就完事。
//    【为什么没直接删掉 onVariableReplaced】它还服务于 TrendWidget 一类
//    持有者,删了这个槽会让别的持有者少一层保护。先留着,加注释说明职责。
//
// ③ writeFailed(QString, QString) 保留原签名,【不】改
//    WriteFailChecker 和 hmi.cpp 都按老签名接着。改成带 plcId 的重载之后,
//    老接法一行不用改 —— 这是"加能力"而不是"改契约"。
//
// 关联:IPollDriver.h(接口), IPlcDriverProvider.h(本类问它要 driver/slaveId),
//      PlcConnectionManager.h(生产环境的 provider 实现), Variable.h(plcId)
// ============================================================================

#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QDateTime>
#include <QMetaType>  // 阶段 4.x 写审计日志:Q_DECLARE_METATYPE
#include <QPointer>

// 阶段 4.x 写审计日志(2026-07-28):一条写操作的记录
// 放顶层,信号参数用更顺;moc 自定义类型 emit 需要 Q_DECLARE_METATYPE
struct WriteAuditEntry {
    QDateTime timestamp;     // 写动作发生时间
    QString   variableName;  // Variable 名("Speed")
    QString   newValue;      // 写出去的值
    bool      success;       // true = 写成功
    QString   address;       // "0x0002"
    QString   resultText;    // "OK" / "FAIL"
    // ★ 2b(2026-10-19):补上 plcId。
    //   【为什么以前没有】单 PLC 时没有"这值写到哪去了"这个问题。
    //   【为什么以前不加就够】它加在结构体【末尾】,且 moc 自定义类型按位置传参 ——
    //   前面任何一个字段都不能动,动了就是二进制不兼容的静默错位。
    QString   plcId;
};
Q_DECLARE_METATYPE(WriteAuditEntry)

class Variable;
class VariableManager;
namespace qwhmi::drivers {
class IPollDriver;
class IPlcDriverProvider;
// 前向声明。enum class 有确定底层类型,可以这么声明 ——
//   但【底层类型必须和 IPollDriver.h 里写的一致】,否则 ODR 不一致,
//   MSVC 静默按不对的尺寸走,链接期不报错。
enum class RegisterArea : int;
}

class WriteScheduler : public QObject {
    Q_OBJECT
public:
    // ★ 2b:参数从 ModbusMaster* 换成 IPlcDriverProvider*。
    //   provider 可以为 nullptr —— 那时什么都不写(单元测试和未装配时用)。
    explicit WriteScheduler(VariableManager* mgr,
                            qwhmi::drivers::IPlcDriverProvider* provider,
                            QObject* parent = nullptr);

    void start();
    void stop();
    bool isRunning() const { return timer_.isActive(); }

    // 阶段 4.x 写审计日志(2026-07-28):给 UI 一次性拉所有记录
	QList<WriteAuditEntry> auditLog() const { return auditLog_; }// 只读,UI 拉取后自己缓存

    // ★ 2b:测试/诊断用。在途(已发出、等 reply)的请求个数。
    //   【它证明什么】pending_ 没泄漏 —— 某个 driver 拒收请求后,
    //   条目必须【不】被登记进来(登记进去就永远等不到 reply,永久泄漏)。
    //   【它【不】证明什么】不代表"没有多连"或"写成功"。
    int pendingCount() const { return pending_.size(); }

    // ★ 2b:driver 的 writeFinished 要接上来,但连接是【运行期】建的
    //   (PlcConnectionManager 每建一台才发一次 driverAdded)。
    //   所以不能像改之前那样在 start() 里 connect 一个固定对象 ——
    //   那个对象在 start() 时可能还不存在。
    //   反过来:每建一台,由 hmi 在 driverAdded 时调 hookDriver。
    //
    //   【为什么是 public 而不是只在 hmi 里 connect】
    //   hmi 确实能自己 connect(d, &IPollDriver::writeFinished, ws, &onWriteReply) ——
    //   但 onWriteReply 是 private slot,得写成 lambda 转发,再把 hooked_
    //   这份"接了哪些"的状态维护搬到 hmi 去。两处状态永远会走散。
    //   幂等性收在本类里:hookDriver 第一行无条件 disconnect 再 connect,
    //   同一个 driver 连两次的后果(一次 reply 处理两遍)就被堵死了。
    void hookDriver(const QString& plcId, qwhmi::drivers::IPollDriver* driver);
    void unhookDriver(const QString& plcId);

signals:
    // 阶段 4.x 写审计日志(2026-07-28):每条新记录都发,UI 接了加行
    void auditAppended(const WriteAuditEntry& entry);
    // ★ IMPROVE-A4(2026-07-29):写失败达上限报警 → 阶段 5 报警系统接
    //   决策:超限后清 dirty(工业铁律 — 失败不能无限重发),让操作员决定后续
    //   【2b:老签名保留,WriteFailChecker 和 hmi.cpp 一行都不用改】
    void writeFailed(const QString& varName, const QString& reason);

    // ★ 2b(2026-10-19):带 plcId 的重载 —— 给需要按 PLC 归类的界面用
    //   (比如"2 号机写失败"比"某个变量写失败"有用得多)。
    //   【为什么不改老的那个】改了就要动 WriteFailChecker 和 hmi.cpp,
    //   而"加能力"不该逼着下游一起改。老签名继续发,两者同时可用。
    void writeFailedOnPlc(const QString& plcId, const QString& varName, const QString& reason);

private slots:
	void onTick();   // 每 100ms 扫描 dirty Variable,发写请求
    void onWriteReply(quint32 requestId, bool success);

    // ★ 阶段 6.3(2026-08-07):监听 varMgr_->variableReplaced,更新 pending_ 里的失效指针
    //   防止 loadFromFile 时 in-flight 写请求的 reply 触发 UAF(v 已被 delete)
    //   ★ 2b:pending_ 的值已改成 QPointer<Variable>,这条【不再是防 UAF 的必需品】 ——
    //     QPointer 在对象被 delete 时自动置空。但 clear() 那种"不替换直接消失"
    //     的场景仍值得显式登记成 null(可读性 + 让 onWriteReply 的路径统一)。
    void onVariableReplaced(Variable* oldVar, Variable* newVar);

private:
    // ★ IMPROVE-A4(2026-07-29):写失败重试上限(超过报警 + 清 dirty)
    static constexpr int kMaxWriteRetries = 3;

    // 把一个变量转换成 Modbus 请求的三要素。不碰 driver,可单独测。
    //   返回 false = 数据类型不认识(理论上到不了这里,switch 覆盖了 5 种)
    static bool buildWritePayload(Variable* v,
                                  qwhmi::drivers::RegisterArea* outArea,
                                  QVector<quint16>* outValues);

    VariableManager* varMgr_;
    // ★ 2b:问它要 driver 和 slaveId。见头注释"改法"。
    qwhmi::drivers::IPlcDriverProvider* provider_;
    QTimer           timer_;                           // 100ms tick
    // ★ 2b:QPointer 不是裸指针,理由见头注释 ②
    QHash<quint32, QPointer<Variable>> pending_;        // requestId → 等待 ack 的 Variable
    // requestId → 发出时用的 plcId。回复时用来 emit 带 plcId 的 writeFailedOnPlc。
    //   为什么不回头问 v->plcId():变量可能在途时被改成别的 plcId,
    //   那样报警就会指错机器 —— 事故分析时这个错误很贵。
    QHash<quint32, QString> pendingPlcId_;
    // ★ 2b:每台 PLC 当前有幾个在途请求。
    //   改之前是"全局一个在途"(onTick 开头判一次 pendingCount)。
    //   多 PLC 之后全局一个在途等于：一台卡住的 PLC 会让另外两台也停写。
    QHash<QString, int> inflightPerPlc_;
    // ★ BUGFIX-D1(2026-10-26):requestId → 【发出时】写出去的值。
    //   【为什么必须记下来】
    //     审计条目里的 newValue 以前是在【reply 时刻】现取 v->value()。
    //     而写挂起的那 100ms 里,一次轮询回包就可能把本地值冲掉
    //     (PollScheduler 那边的同一个 bug,已修)。
    //     于是审计会记下一个【不是发出去的那个值】——
    //     事故复盘时唯一的证据写着"写了 100",而线上跑的是 0。
    //   【跟 pendingPlcId_ 同一个道理】
    //     那边记 plcId 是因为"变量在途时可能被改归属";这边记值是因为
    //     "变量在途时本地值可能被改"。两个字段是同一个教训的两面。
    //   【为什么不回头问 v->value()】
    //     因为审计要记的是【实际传输的那一帧】,不是"回复时本地长什么样"。
    QHash<quint32, QString> pendingSentValue_;
    // ★ IMPROVE-A2(2026-07-29):quint32 防回绕。进程内全局唯一,理由见头注释 ①。
    quint32 nextReqId_ = 1;                            // 请求 id 自增
    QList<WriteAuditEntry> auditLog_;                  // 阶段 4.x 写审计日志
    QSet<QString> hooked_;                             // 已接 writeFinished 的 plcId
};

// 阶段 3.3:定时轮询调度器
// 职责:每隔 100ms 扫所有 Variable,看谁"到期"就合并连续地址发 1 次 Modbus 读请求
// 监听 ModbusMaster::readFinished,按 requestId 反查 + 拆 reply 数据,setValue 给 Variable
//
// 设计原则:
// 1. 单 timer 集中调度(不给每个 Variable 一个 QObject,timer 太多不高效)
// 2. 按 address 排序后合并连续区间(0x0001+0x0002+0x0003 = 1 次 read holding reg count=3)
// 3. in-flight 机制:nextPollAtMs[v] = -1 表示在等 reply,tick 不再选
// 4. 失败/超时:由 PollScheduler 自己的下一帧重试(不依赖 ModbusMaster 失败信号)

#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QSet>            // ★ IMPROVE-A7(2026-07-29):inFlight_ 用 QSet
#include <QModbusDataUnit>

class Variable;
class VariableManager;
class ModbusMaster;

class PollScheduler : public QObject {
    Q_OBJECT
public:
    explicit PollScheduler(VariableManager* mgr, ModbusMaster* modbus, QObject* parent = nullptr);

	void start();// 开始轮询调度
	void stop();// 停止轮询调度
	bool isRunning() const { return timer_.isActive(); }// 是否正在运行

	// ★ BUGFIX(2026-07-28):重连时清空 nextPollAtMs,避免 -1 in-flight 残留
	//   让所有 Variable 下一帧 tick 立即可发(否则某些 Variable 可能永久卡 in-flight 永远不读)
	void resetAll();

signals:
	// ★ 阶段 5.3+ 修 Bug #3(2026-08-05):原始 PLC 值(死区过滤前)给 AlarmChecker 用
	//   【背景】Variable.deadband_ 吞掉小波动,Checker 订阅 valueChanged 看不到
	//   【改法】PollScheduler 在 setValue + setQuality 后 emit rawRead,带原始 16-bit register
	//   【取舍】一次一个 register(Int32/Float 多个 register 由 PollScheduler 分多次 emit);
	//           quality 此时已设 Good,Checker 可直接信
	void rawRead(Variable* v, quint16 rawRegister);

private slots:
    void onTick();                                       // 100ms 一次扫描
    void onModbusReply(quint32 requestId, QModbusDataUnit unit, bool success);// ModbusMaster::readFinished 回调

private:
    VariableManager* varMgr_;
    ModbusMaster* modbus_;
    QTimer timer_;                                       // 100ms tick
    QHash<Variable*, qint64> nextPollAtMs_;            // 下次轮询时刻(0 = 立即可发,>0 = 等到该时刻)
    // ★ IMPROVE-A7(2026-07-29):in-flight 状态独立用 QSet,替原来的 -1 magic number
    //   原代码 nextPollAtMs_[v] = -1 表示 in-flight,但 -1 跟"下次时间"语义混一起
    //   现在:nextPollAtMs_ 只存时间,inFlight_ 单独表达"请求已发,等 reply"
    QSet<Variable*> inFlight_;
    // ★ IMPROVE-A2(2026-07-29):quint32 防回绕(int 跑 2^31 次也会爆)
    quint32 nextReqId_ = 1;
    QHash<quint32, QList<Variable*>> pending_;         // reqId → 待更新变量列表(合并读用)
};
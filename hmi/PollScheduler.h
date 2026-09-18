// 阶段 3.3:定时轮询调度器
// 职责:每隔 100ms 扫所有 Variable,看谁"到期"就合并连续地址发 1 次 IPollDriver 读请求
// 监听 IPollDriver::readFinished,按 requestId 反查 + 拆 reply 数据,setValue 给 Variable
//
// ★ C.1(2026-09-17):从 ModbusMaster* 改成 IPollDriver*
//   - 不再 include QModbusDataUnit(协议无关)
//   - 跟 DriverFactory 配合,C.4 加协议 UI 时换 driver 不用改 PollScheduler
//
// ★ C.5(2026-09-18):从单 IPollDriver* 改成 QHash<QString, IPollDriver*>
//   - key = plcId("plc1"/"plc2"/...)
//   - 每个 driver 独立 tick,故障隔离(教训 1.5)
//   - 1 driver = 1 PLC
//   - 老工程所有变量默认 plcId="plc1",所以单 driver 行为不变
//
// 设计原则:
// 1. 单 timer 集中调度(不给每个 Variable 一个 QObject,timer 太多不高效)
// 2. 按 address 排序后合并连续区间(0x0001+0x0002+0x0003 = 1 次 read holding reg count=3)
// 3. in-flight 机制:nextPollAtMs[v] = 0 + inFlight_ set(避免 -1 magic number)
// 4. 失败/超时:由 PollScheduler 自己的下一帧重试(不依赖具体 driver 失败信号)
// 5. ★ C.5 多 driver 调度:每个 driver 各自发请求,各自等 reply;1 个 driver 挂不影响其他

#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QSet>            // ★ IMPROVE-A7(2026-07-29):inFlight_ 用 QSet
#include <QVector>         // ★ C.1:readFinished 信号用 QVector<quint16>(协议无关)

class Variable;
class VariableManager;
namespace qwhmi::drivers { class IPollDriver; }

class PollScheduler : public QObject {
    Q_OBJECT
public:
    // ★ C.5(2026-09-18):构造支持多 driver(向后兼容单 driver)
    //   - 单 driver 构造:把所有变量当成 plc1(老工程默认)
    //   - 多 driver 构造:QHash<plcId, IPollDriver*>
    explicit PollScheduler(VariableManager* mgr,
                           qwhmi::drivers::IPollDriver* driver,
                           QObject* parent = nullptr);
    explicit PollScheduler(VariableManager* mgr,
                           const QHash<QString, qwhmi::drivers::IPollDriver*>& drivers,
                           QObject* parent = nullptr);

    // ★ C.5(2026-09-18):运行时添加/移除 PLC(用于切换协议 / 新增 PLC)
    void addDriver(const QString& plcId, qwhmi::drivers::IPollDriver* driver);
    void removeDriver(const QString& plcId);

    // 当前 driver 列表(plcId → IPollDriver*)
    QHash<QString, qwhmi::drivers::IPollDriver*> drivers() const { return drivers_; }

	void start();// 开始轮询调度
	void stop();// 停止轮询调度
	bool isRunning() const { return timer_.isActive(); }// 是否正在运行

	// ★ BUGFIX(2026-07-28):重连时清空 nextPollAtMs,避免 -1 in-flight 残留
	//   让所有 Variable 下一帧 tick 立即可发(否则某些 Variable 可能永久卡 in-flight 永远不读)
	void resetAll();

	// ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询策略 C —— UI 注册基础设施
	//   UI 在屏幕切换时调 addActiveVariable / removeActiveVariable
	//   PollScheduler 保持当前显示变量集合,只 poll 这些
	//   单窗口场景:hmi 构造后调 setAllActive() —— 所有 var 都默认激活
	void addActiveVariable(Variable* v);
	void removeActiveVariable(Variable* v);
	void setAllActive();  // 初始化用:把所有 var 都加入激活集合
	void clearActiveVariables();

signals:
	// ★ 阶段 5.3+ 修 Bug #3(2026-08-05):原始 PLC 值(死区过滤前)给 AlarmChecker 用
	//   【背景】Variable.deadband_ 吞掉小波动,Checker 订阅 valueChanged 看不到
	//   【改法】PollScheduler 在 setValue + setQuality 后 emit rawRead,带原始 16-bit register
	//   【取舍】一次一个 register(Int32/Float 多个 register 由 PollScheduler 分多次 emit);
	//           quality 此时已设 Good,Checker 可直接信
	void rawRead(Variable* v, quint16 rawRegister);

private slots:
    void onTick();                                       // 100ms 一次扫描
    // ★ C.5:readFinished 也带 plcId(因为可能 N 个 driver 同时发 reply)
    void onDriverReadFinished(const QString& plcId, quint32 requestId, QVector<quint16> registers, bool success);

private:
    VariableManager* varMgr_;
    // ★ C.5:从单 IPollDriver* 改成 QHash<QString, IPollDriver*>
    //   老代码 driver_ 单指针保留(兼容性指针),drivers_ 是新多 driver 哈希
    qwhmi::drivers::IPollDriver* legacyDriver_ = nullptr;  // 单 driver 兼容
    QHash<QString, qwhmi::drivers::IPollDriver*> drivers_;  // 多 driver(按 plcId)
    QTimer timer_;                                       // 100ms tick
    QHash<Variable*, qint64> nextPollAtMs_;            // 下次轮询时刻(0 = 立即可发,>0 = 等到该时刻)
    // ★ IMPROVE-A7(2026-07-29):in-flight 状态独立用 QSet,替原来的 -1 magic number
    //   原代码 nextPollAtMs_[v] = -1 表示 in-flight,但 -1 跟"下次时间"语义混一起
    //   现在:nextPollAtMs_ 只存时间,inFlight_ 单独表达"请求已发,等 reply"
    QSet<Variable*> inFlight_;
    // ★ 阶段 6.x.1(2026-08-11 Wqian 拍板)分级轮询 —— 当前显示变量集合
    //   UI 切屏幕时调 add/removeActiveVariable,此处 PollScheduler 保持
    //   onTick 检查:v 不在集合且 alwaysPoll=false → 跳过
    QSet<Variable*> activeVariables_;
    // ★ IMPROVE-A2(2026-07-29):quint32 防回绕(int 跑 2^31 次也会爆)
    quint32 nextReqId_ = 1;
    // ★ C.5:reqId → 待更新变量列表(合并读用)+ plcId(哪个 driver 的 reply)
    QHash<quint32, QList<Variable*>> pending_;         // reqId → variables
    QHash<quint32, QString> pendingPlcId_;             // reqId → plcId(分派 readFinished 用)
};

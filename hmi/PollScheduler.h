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
//
// ★ 2b(2026-10-19):多 driver 骨架(C.5)自己有两个 bug,这次一并修掉。
//   改之前 addDriver 只插哈希不接 readFinished(那个 connect 只在 start() 里),
//   而 hmi.cpp 是先 start() 再 loadFromFile() —— 运行期加进来的 driver
//   永远收不到回包。现在挂接收敛到 hookDriver(),由 running_ 决定什么时候接。
//   详见 .cpp 顶部注释和 addDriver() 的长注释。

#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QSet>            // ★ IMPROVE-A7(2026-07-29):inFlight_ 用 QSet
#include <QVector>         // ★ C.1:readFinished 信号用 QVector<quint16>(协议无关)

class Variable;
class VariableManager;
namespace qwhmi::drivers {
class IPollDriver;
class IPlcDriverProvider;
}

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

    // ★★ 2b(2026-10-19):slaveId 的来源。
    //
    //   【改之前的实际行为】onTick 里写的是 `req.slaveId = 1;`
    //   注释说"slaveId 暂时固定 1,阶段 6 工程文件再做成可配"——
    //   那条注释本身就是在说"这是一个【已知错误】,现在先欠着"。
    //   后果和多 PLC 是一回事的量级:工程里 slave_id 配了 5,
    //   读请求照样发 Unit ID = 1,打到别的从机上,值读回来还"看着有值"。
    //
    //   【为什么不加进构造函数】加进去就得改两个构造函数的签名,
    //   而 tst_IPollDriver 有十几次构造调用会一起改 ——
    //   diff 变大,而"没装 provider 就退回 1"是合理默认,setter 更合适。
    //   【默认值必须是 1】不装 provider 的老用法(单元测试、
    //   单 driver 场景)行为不变。
    void setSlaveIdProvider(qwhmi::drivers::IPlcDriverProvider* provider) {
        slaveIdProvider_ = provider;
    }

    // 当前 driver 列表(plcId → IPollDriver*)
    QHash<QString, qwhmi::drivers::IPollDriver*> drivers() const { return drivers_; }

    // ★ 2b(2026-10-19):当前真正接了 readFinished 的 driver 个数。
    //   【它证明什么】净连接数 —— stop() 之后是 0、start 之后是 N。
    //   【它【不】证明什么】"没有多连"。它是 QSet,重复 insert 不增长,
    //   而且实测把 hookDriver 的幂等 disconnect 删掉它也不变。
    //   想断言"多连了"只能靠 replyDispatchCount()。
    int hookedDriverCount() const { return hooked_.size(); }

    // ★ 2b:reply 分派被进入过多少次(纯观测计数器,生产逻辑不用它)。
    //
    //   【它证明的是什么,不是什么】
    //   证明:每个 requestId 恰好被分派一次(用例断言它 == driver 的发帧数)。
    //   不证明:"hookDriver 里那行幂等 disconnect 被删掉会被抓到" —— 【抓不到】。
    //   实测(2026-10-19):删掉那行,22 条用例全绿。原因是每个调用方
    //   (addDriver 先 unhook、start 有 isActive 挡着)都已经先摘干净了,
    //   那行是防御性的、当前不可达。详见 .cpp 里 hookDriver() 的注释。
    quint64 replyDispatchCount() const { return replyDispatchCount_; }

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
    // ★ 2026-10-10:变量被删/被替换 → 把上面四个容器里的指针全部清干净。
    //   理由见 .cpp 里同名函数的长注释(悬空指针当 key 存在哈希里,本身就是 UB)。
    void onVariableReplaced(Variable* oldVar, Variable* newVar);

private:
    // 两个构造函数都要调 —— 信号连接跟有几个 driver 无关。
    void connectVariableManager();

    // ★ 2b(2026-10-19):readFinished 挂接的【唯一入口】。
    //   幂等性靠"第一行无条件 disconnect 再连"这一个动作拿到,不靠调用方约定顺序 ——
    //   靠约定的东西迟早会被乱序调用打破。
    //   【只在 running_ 为真时被调用】
    void hookDriver(const QString& plcId);
    void unhookDriver(const QString& plcId);

    VariableManager* varMgr_;
    // ★ C.5:从单 IPollDriver* 改成 QHash<QString, IPollDriver*>
    //   老代码 driver_ 单指针保留(兼容性指针),drivers_ 是新多 driver 哈希
    qwhmi::drivers::IPollDriver* legacyDriver_ = nullptr;  // 单 driver 兼容
    QHash<QString, qwhmi::drivers::IPollDriver*> drivers_;  // 多 driver(按 plcId)
    // ★ 2b:问它要 slaveId。没装(单元测试 / 老用法)就退回 1。
    //   【为什么不让 driver 自己报】IPollDriver 没有 slaveId 概念 ——
    //   slaveId 是【这台 PLC 的配置】,不是【这条连接的属性】,归 driver 是错位。
    qwhmi::drivers::IPlcDriverProvider* slaveIdProvider_ = nullptr;
    // ★ 2b(2026-10-19):定时器是不是"应该连着"的状态标志。
    //   【为什么不能省】tst_IPollDriver 里约 7 条用例把 stop() 当拆解用。
    //   如果 stop() 之后 addDriver 又把 readFinished 连上,stop() 就失去意义了 ——
    //   那些用例会看到本不该有的回包流入。
    //   【为什么要有】start() 在 hmi.cpp 里早于工程加载,运行期 addDriver 是常态,
    //   所以"要不要现在连"必须由一个显式状态回答,不能靠 timer_.isActive()
    //   (stop() 之后它也是 false,但 addDriver 的语义要跟 start/stop 对齐)。
    bool running_ = false;
    QSet<QString> hooked_;               // 已经接了 readFinished 的 plcId
    // ★ 2b:纯观测计数器,给"有没有重复连接"用。生产逻辑不读它。见 replyDispatchCount()。
    quint64 replyDispatchCount_ = 0;
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
    // ★ 下面这四个容器都拿【裸 Variable*】当 key 或 value。
    //   QObject 析构不会通知任何人"我的地址失效了" ——
    //   所以谁把指针存起来,谁就得负责在变量消失时清理。
    //   这就是 onVariableReplaced 存在的原因,改动它们时必须同步看那个函数。
    //   (根治方案是改 QSharedPointer = 改善清单 #5,批次 3 才动。)
};

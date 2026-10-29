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

    // ============================================================
    // ★ 2026-10-27(SOAK-W):写侧通路的【可观测性】
    // ============================================================
    //
    // 【为什么要有这一整块】
    //   读侧早就有"轮询统计"这一行(INFO 级、60 秒一条、累计计数),
    //   长跑 harness 靠它判断"业务还在跑"。
    //   而【写侧什么都没有】—— 写被拒收、被排队、作废,全都没有计数。
    //
    //   ⇒ 后果:BUGFIX-E1 修之前那轮("读 547 次全成功、写 604 次全被拒、
    //     0 帧到达从站"),长跑的活体判据【照样绿】——
    //     因为它只看读。写侧那侧【没有任何数字】可看。
    //
    // 【为什么判据要放在【程序里】而不是 harness 里刮日志】
    //   原来的设计是 harness 刮日志 + 用"3 秒时间窗口"猜
    //   "这次拒收是环境问题(断线)还是缺陷(静默拒收)"。
    //   ★ 那条路已经被实测推翻了(见 docs/现状.md):
    //     · 真断线【不产生】"Modbus 错误"—— 那串字在 hmi.cpp(UI 层),
    //       而 TcpClient::onSocketError 故意不发 errorOccurred;
    //     · 断线会 failQueuedWrites() 把排队写作废 ⇒ "排队 > 排队发出"
    //       会【误报】"排队写丢了"。
    //   ⇒ 只有【知道原因的那个地方】能分清环境与缺陷。
    //     原因就在 ModbusMaster 里 —— 所以在这里记。
    //
    // 【为什么字段分得这么细】
    //   因为判据要回答两个不同的问题:
    //     · "写有没有真的落地" → 已发出 / 成功 / 失败
    //     · "没落地的话,是环境还是缺陷" → 拒收的五个分类
    //   合成一个"拒收 N 次"是【不够】的:断线拒收和缺陷拒收都长一个样。
    //
    // 【累计 vs 仪表】
    //   · 累计计数器(尝试/接受/拒收/…):进程内【只增不减】,
    //     harness 在观测窗口里取【增量】。
    //   · 仪表(在队/在途写):【瞬时值】,harness 取"末值 - 首值"。
    //   两类混在一行里,但名字上能看出来(累计的不带"在"字)。
    struct WritePathStats {
        int      instance        = 0;  // 实例号(进程内自增;用于把多台 PLC 的统计分开)
        quint64  attempts        = 0;  // 尝试:进 sendWriteRequest 的次数
        quint64  accepted        = 0;  // 接受:返回了非空句柄(已受理,必定会有 writeFinished)
        quint64  rejected        = 0;  // 拒收:返回了空句柄(【不会】有 writeFinished)
        quint64  rejNotConnected = 0;  // 拒收之 未连接   —— 环境
        quint64  rejInvalid      = 0;  // 拒收之 参数非法 —— 配置
        quint64  rejQueueFull    = 0;  // 拒收之 队列已满 —— ★ 缺陷
        quint64  rejSendFailed   = 0;  // 拒收之 发送失败 —— 环境
        quint64  rejDefect       = 0;  // 拒收之 来源不明 —— ★ 缺陷(见下面的说明)
        quint64  queued          = 0;  // 入队:写被在途的读挡住,进了队列
        quint64  queuedSent      = 0;  // 排队写【真的发出去了】的次数
        quint64  queuedVoided    = 0;  // 排队写【作废】的次数(断线/参数/发送失败)
        quint64  sent            = 0;  // 写帧【真的上了总线】的次数
        quint64  ok              = 0;  // 写成功
        quint64  fail            = 0;  // 写失败(【已经上了总线】的那种)
        int      inQueue         = 0;  // 此刻排队里的条数
        int      inflightWrites  = 0;  // 此刻在途的写(已发出、还没回包)
    };

    // 写侧统计的当前快照(测试与 harness 之外的观测用)。
    WritePathStats writePathStats() const;

    // ★ 2026-10-27:统计行的【格式】,提成纯函数 —— 好让格式本身能被测。
    //
    //   【为什么必须提出来】
    //     这一行的格式是【两方契约】:程序这边打出来,长跑 harness 那边用正则取数。
    //     两边对不上时的表现是【判不出】,不是【判错】——
    //     2026-10-21 就栽过一次:程序打的是 "发出= 271"(等号后多一个空格),
    //     harness 的正则要 "发出=271",于是启动自检等满 120 秒一条都没匹配上,
    //     判成"没资格跑"—— 而那 120 秒里程序一直在正常轮询。
    //     判据坏掉的样子,和程序坏掉的样子,一模一样。
    //
    //   【★ 整行必须是【拼好的一个字符串】,不能一项一项 << 出去 ★】
    //     QDebug 在【相邻两项之间插一个空格】——
    //     `qCInfo(cat) << "发出=" << count` 出来是 "发出= 271"。
    //     所以整行先拼成 QString,再一次 qCInfo(... << utf8.constData())。
    //     ★ 改这里必须同时改 scripts/long_run.ps1 的 $reWriteStats。
    //       tst_WriteStatsFormat 把这份契约钉住,谁改都能立刻看见。
    static QString formatWriteStatsLine(const WritePathStats& s);

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

    // ★ 60 秒一条的写侧统计行(INFO 级)。
    //   【为什么单独一个 timer】跟 PollScheduler::statsTimer_ 一个道理:
    //     "一个写都没发生"恰恰是最该被看见的状态,
    //     那种时候不能因为"没事件"就不打这一行。
    //
    //   ★★ 为什么它【必须是 slot】(而不是普通私有函数)★★
    //     因为契约测试(tst_WriteStatsFormat)要用
    //     QMetaObject::invokeMethod(this, "onWriteStatsTick") 【按名字】调它 ——
    //     只有 moc 登记进元对象的槽才能按名字 invoke。
    //     同 PollScheduler::onStatsTick(它也在 private slots: 里)——
    //     那一条 2026-10-21 出事时,测试就是这么抓住 QDebug 插的空格。
    //     ⇒ 写成普通私有函数的话,测试会"没抓到日志行"而红,
    //       但那是个【误导性的红】(看着像格式坏了,其实是槽没登记)。
    void onWriteStatsTick();

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

    // ============================================================
    // ★ 2026-10-27(SOAK-W):写侧统计的内部记账
    // ============================================================
    //
    // 【为什么"拒收"要记【原因】,而不只是记一个次数】
    //   判据要回答两个不同的问题:
    //     · "写有没有真的落地" → 已发出/成功/失败
    //     · "没落地的话,是环境还是缺陷"
    //   而【只有这里知道原因】—— harness 靠"3 秒内有没有 errorOccurred"
    //   去猜,那条路已经被实测推翻了(真断线不发 errorOccurred,
    //   而且断线会作废排队写 ⇒ 还会误报)。
    //
    // 【★ rejDefect(来源不明)为什么【是缺陷】而不是"忽略" ★】
    //   每一条【合法的】拒收路径都必须在返回 nullptr 之前先写明原因。
    //   如果某条拒收路径【忘了】写原因 ⇒ 原因还是 None
    //   ⇒ 归到 rejDefect ⇒ 判据报 FAIL。
    //   ★ 这正是它的价值:【新增一条拒收路径却忘了记账】这件事,
    //     只有这样才【看得见】。否则就是一条静默的洞。
    enum class RejectReason {
        None = 0,      // 没有已知原因(★ 缺陷:有拒收但没人说为什么)
        NotConnected,  // 未连接       —— 环境
        Invalid,       // 参数非法     —— 配置
        QueueFull,     // 队列已满     —— 缺陷
        SendFailed,    // 发送失败     —— 环境
    };

    // ★ sendWriteRequest 的【真身】—— 公开的 sendWriteRequest 是它的一层 wrapper。
    //
    //   【为什么要拆】
    //     负向对照脚本(scripts/negctl_write_starve.py)会把
    //     "槽被占就排队"那一整段换成【裸 return nullptr;】——
    //     如果计数写在被换掉的那段里,变异体会把计数一起删掉,
    //     那道闸当场瞎掉(而它还绿着,因为"没看见"被当成"没问题")。
    //   ⇒ 计数放在【wrapper】里,替换不到 ⇒ 闸永远有牙。
    //
    //   【★ impl 每次进来第一件事必须把原因清成 None ★】
    //     否则上一轮留下的旧原因会被这一轮读到:
    //     变异体的裸 return nullptr 会被误分类成"有理由的拒收",
    //     于是缺陷被当成环境放过 —— 正是本项目反复栽的那类坑。
    QModbusReply* sendWriteRequestImpl(const QModbusDataUnit& writeUnit,
                                        int serverAddress, quint32 requestId);

    // ★ 60 秒一条的写侧统计行(INFO 级)——
    //   声明在上面的 private slots: 里(必须是 slot,理由见那里的注释)。

    // 当前这一轮 sendWriteRequest 的拒收原因(impl 写,wrapper 读)。
    RejectReason lastRejectReason_ = RejectReason::None;

    // 写侧累计计数器(纯观测,生产逻辑不读它们)
    WritePathStats writeStats_;

    // 进程内实例号(自增)—— 让 harness 能把多台 PLC 的统计分开。
    //   ModbusMaster 每次新建都 +1(只有改 PLC 配置才会重建 driver,
    //   TCP 层面的重连【复用同一个对象】—— 见 PlcConnectionManager::applyConfig)。
    static int s_instanceCounter_;

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
        // ★★ 2026-10-21:写请求的【起始地址】—— 回包 echo 比对要用。
        //
        //   为什么不从 reply 上"拿":QModbusReply 上根本没有这个信息
        //   (reply 只有结果数据,不知道请求的是哪个点)。
        //   原来那一行写的是 `req->reply ? /*addr 通过 reply 拿*/0 : 0` ——
        //   三元两分支【都是 0】,占位从来没填上,于是回包比对实际判的是
        //   "echoAddr != 0",不是"跟请求地址比"。
        //   ⇒ 唯一正确的做法是:发出去的时候把地址存下来。
        //
        //   ★ 只有【写】请求有 echo 地址可对:
        //     写响应(0x05/0x06/0x0F/0x10)回显起始地址;
        //     读响应(0x01~0x04)只有 byteCount + 数据,【没有】地址字段。
        quint16  startAddress = 0;
    };

    // ============================================================
    // ★ 2026-10-26(BUGFIX-E1):写请求【队列】
    //
    //   【它挡的是什么】
    //     ModbusMaster 只有一个在途槽(Modbus 协议串行),
    //     读和写【共用】这一个槽。槽被读占着的时候写请求被【直接拒收】。
    //
    //   【实测出来的现场形态(不是推演)】
    //     2026-10-26 短跑实测(sweeps/sweep_m85):
    //       · 604 次写尝试【全部】被拒,0 帧到达从站
    //       · 同一时刻 547 次读【全部】成功
    //       · 写被拒的时刻 → 读回包到达的时刻:平均 2.18 ms,
    //         94.2% 在 5 ms 以内 —— 写尝试【落在读的在途窗口里】
    //     而同一套配置(sweep_m70 / m99 / m88 / m92 / m95 / m97)的另外六轮,
    //       写【一次都没被拒】(20/20、21/21、18/18 …)。
    //     ⇒ 同样是 poll_interval=50ms、写节拍 100ms,结果【两极分化】:
    //       取决于两个定时器的【启动相位差】,而那个相位差【一辈子不变】
    //       (两个 100ms 节拍互相锁相)。
    //     ⇒ 所以这不是"概率问题",是【开机掷一次硬币】:
    //       掷到坏的那一面,整轮(24 小时也是)一个字节都写不出去。
    //
    //   【为什么这个洞特别危险】
    //     读照常成功 ⇒ 长跑的活体判据(轮询统计:发出/成功 在涨)【照样绿】。
    //     进程活着、内存平稳、日志在长、报警照响、审计照记"已执行"——
    //     而 PLC 上一个字节都没收到。
    //     ⇒ 这正是本项目反复吃的"假绿":量的是【有没有动作】,不是【动作对不对】。
    //
    //   【改法:写不再被拒,而是【排队】】
    //     槽被占着 → 写请求进队列,返回 true(已接受,必定会有 writeFinished)。
    //     槽一空(读回包到达 / 超时清理)→ 立刻把队里的写发出去。
    //     于是写不再跟读【抢】那个槽,而是【等】那个槽 ——
    //     相位差不再是决定因素,写一定会出去。
    //
    //   【为什么不改"给读和写各一个槽"】
    //     Modbus TCP 规范上一个连接同时只能有一个未决请求,
    //     多开一个槽会把"串行协议"变成"并发协议",从站可能直接乱序回包。
    //     排队才是串行协议下正确的做法。
    //
    //   【为什么队列长度上限是 8】
    //     WriteScheduler 本身限制"每台 PLC 同时只发一个写"(inflightPerPlc_),
    //     所以正常情况下队列里【最多 1 条】。8 是防御性的上限:
    //     万一将来那个限制被放宽,也不会让队列无限涨。
    //     队列满 ⇒ 退回【拒收】(返回 nullptr),跟改之前一个行为。
    struct QueuedWrite {
        QModbusDataUnit::RegisterType type = QModbusDataUnit::HoldingRegisters;
        int      serverAddress = 0;
        int      addr = 0;
        QList<quint16> values;      // ★ 在【入队那一刻】就定下来的字节
        quint32  requestId = 0;
        PendingReply* reply = nullptr;
    };
    QList<QueuedWrite> queuedWrites_;
    static constexpr int kMaxQueuedWrites = 8;

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

	// ★ 2026-10-21(BUGFIX-C1):通讯错误【限速】打日志
	//   【为什么要限速】
	//     从站一挂,每一个读请求都会失败;轮询 100ms 一次 ⇒ 一天 86 万条错误。
	//     全打 WARNING 会把日志盘写满,把真正要看的第一条冲掉。
	//   【规则】
	//     第 1 次错误 → WARNING(带完整原因,现场第一眼就要看到)
	//     之后 10 秒内的重复 → Debug(不落默认级别)
	//     每满 10 秒 → WARNING 汇总一行(累计次数 + 最近一次原因)
	//   【为什么放这里而不是 hmi.cpp】
	//     hmi.cpp 那条 errorOccurred 只在 UI 层,协议层的失败原因在这里最全。
	void logCommError(const QString& msg);

	// ★ 2026-10-21:通讯错误计数 + 上次汇总时刻(配合 logCommError 的限速)
	int    commErrorCount_ = 0;
	qint64 lastCommErrorLogMs_ = 0;
	static constexpr qint64 kCommErrorSummaryIntervalMs = 10000;

    // 2026-07-09 新增:erase 后 / clear() 后调用 —— pending_ 变空就 emit idle
    //
    // ★ 2026-10-26(BUGFIX-E1):这个 helper 现在还负责【把排队的写发出去】。
    //   为什么挂在这儿:槽变空只有三种可能 —— 读回包到达 / 超时清理 /
    //   连接变化 —— 而这三种的共同点就是【现在可以发下一帧了】。
    //   写请求等的就是这个时刻。
    //   ⇒ 一处挂钩子,三条路径自动都覆盖,不会漏掉其中一条。
    void notifyIdleIfEmpty() {
        flushQueuedWrites();
        if (pending_.isEmpty()) emit idle();
    }

    // ★ 2026-10-26(BUGFIX-E1):槽空出来时,把排队的写发出去。
    //   【发什么】队里第一个(先进先出 —— 写有顺序,不能乱)。
    //   【什么时候发】只在 pending_ 空的时候发 —— 协议串行,一次只能一帧。
    //   【为什么只发一个】发一个之后槽又被占了,剩下的等下一个空槽。
    void flushQueuedWrites();

    // ★ 2026-10-26(BUGFIX-E1):连接断了/要重连时,排队里的写【全部作废】。
    //   【为什么必须作废,而不是留着】这些写要发给【旧连接】,
    //   连接都没了,发出去也没有意义;而且如果留着,它们永远等不到 writeFinished ——
    //   WriteScheduler 那边 pending_ 就永远清不掉,那台 PLC 从此再也不写任何东西
    //   (跟 2026-10-21 查到的"变量卡在在途态"是同一个形状的故障)。
    void failQueuedWrites(const QString& reason);

    // ★ 2026-10-26(BUGFIX-E1):【单独一条】排队写失败时的收尾。
    //   flushQueuedWrites 里那一条已经出队了,整队作废管不到它,
    //   所以它必须自己发 writeFinished(false) —— 否则调用方永远等一个不来的 reply。
    void failOneQueuedWrite(const QueuedWrite& qw, const QString& reason);

    // ★ 2026-10-26(BUGFIX-E2):连接断开 / 要重连时,把【在途】请求全部作废,
    //   并且【每个都发一个失败通知】。
    //   【为什么必须发通知,不能只把 pending_ 清掉】
    //     原来那两条路(断线 handler / connectTo)只是 pending_.clear()。
    //     于是 PollScheduler 的 inFlight_ 和 WriteScheduler 的 pending_ 都
    //     永远清不掉 —— 那台 PLC 从此再也不读也不再写,而且【零日志】。
    //   【为什么按 isWrite 分流】
    //     写发 writeFinished,读发 readFinished —— 同一个请求不能同时
    //     惊动读调度器和写调度器(跟 onTimeoutScan 里那处同样的道理)。
    void failPendingRequests(const QString& reason);

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

    // ★ 2026-10-27(SOAK-W):写侧统计行的定时器(60 秒一条)。
    //   跟 timeoutTimer_ 分开:统计行【不受】超时扫描的节奏影响,
    //   也不受"有没有请求"影响 —— 只要对象活着,每 60 秒一定有一行。
    QTimer writeStatsTimer_;
};
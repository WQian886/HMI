// ============================================================================
// tst_PlcMultiServerE2E —— 多 PLC 2b(2026-10-19):3 台【真】Modbus TCP 服务端
//
// 【这个测试为什么必须存在】
//   到 2b 为止的所有测试都把 driver 换成了 mock。mock 证明的是
//   "调度器把请求派给了正确的那个对象指针",它证明不了
//   "那三个指针背后是不是三条真的连到三台不同机器的 socket"。
//
//   而 2b 要消灭的那个 bug(写错设备)恰恰发生在【链路层】:
//   mock 收到 0x2222 只说明代码调对了 sendWriteRequest(0x2222, plc2 的指针);
//   "plc2 的指针背后是不是真的那条到 plc2 的 socket",mock 无从判断。
//   一个把三条 socket 全接到 plc1 的实现能通过全部 mock 测试 ——
//   在本文件里它第一秒就现形。
//
// 【★★ 本文件最强的断言:值本身编码身份】
//   三台服务端各自把整个寄存器空间填成 0x1111 / 0x2222 / 0x3333。
//   于是"plc2 的变量读到 0x2222"这一句同时证明了:
//     ① 连的是 plc2 那条 socket     (否则读到 0x1111)
//     ② 解的是 plc2 那份响应        (否则长度/偏移会串)
//     ③ TID 回对了                  (ModbusMaster 按 pending_.find(tid) 匹配,
//                                     TID 串了就【静默丢弃】,ModbusMaster.cpp:396-400)
//     ④ Unit ID 回对了              (不一致也是【静默丢弃不报错】,ModbusMaster.cpp:403-406)
//   这四条任何一条错了,这个值都不会是 0x2222。
//   只断言"三个变量都被刷新过"太弱:三台都填 1 也照样过。
//
// 【为什么服务端自己写,不用现成的】
//   仓库里没有 Modbus 服务端。而 ModbusMaster 的回帧有三条硬约束
//   (TID 回原 / Unit ID 一致 / 长度够),服务端写错的话,测试会红 ——
//   但红的原因是"我的服务端写错了",不是"产品有 bug",那就白测了。
//   所以下面把三条约束逐条写成注释,让服务端代码可审、可负向对照。
//
// 【为什么写路径的用例不启动 PollScheduler】
//   读会调 Variable::setValue 覆盖本地值,而 WriteScheduler 在 onTick 里
//   现取 v->value()。两者跑在一起就有真实的竞态:值刚设成 0x7777,
//   下一帧读回来 0x2222,写出去的就成了 0x2222 ——
//   那会让"服务器收到的是不是我们的值"这条断言随机红,而红的原因是竞态不是 bug。
//   所以写用例单独隔离(不启动 PollScheduler),用 0x7777 这种
//   与三台 fill 值都不一样的数,确保断言区分得开"我们的写"和"读回来的值"。
//
// 【负向对照 —— 两轮,2026-10-19 实跑】
//
//   改坏的方式都是"把按 plcId 查 driver 换成取固定的第一个"。
//   两轮红集**互不相交**,这才说明读、写两条路各被自己的用例独立钉住了 ——
//   只跑一轮的话,会误以为另一个方向也被覆盖了。
//
//   第 1 轮:PollScheduler.cpp onTick 里
//            drivers_.value(plcId, nullptr) → drivers_.values().first()
//     红 3 条:eachPlc_receivesItsOwnReply / onePlcDisconnects_othersKeepPolling
//              / nonContiguousAddresses_allStillRefresh
//     绿 4 条:两个写用例 + slaveId_landsInUnitId_onTheWire
//     现象极好读:本该是 plc1 变量的 v1 上出现了 **8738 = 0x2222**,
//     而期望是 4369 = 0x1111。值本身指着真凶是谁 —— 不用去看日志就知道
//     "plc1 的请求被打到了 plc2 的 socket 上"。
//     ★ 与计划里的预测不符:原以为 onePlcDisconnects 会仍绿,实测【也红了】。
//       原因是它断言的正是"另两台继续刷新",而改坏之后它们读到的是别人的值,
//       数量照涨、身份错了。计划里的那句预测当时是想错了,记录在案。
//
//   第 2 轮:WriteScheduler.cpp onTick 里 driverFor(plcId) → driverFor("plc1")
//     红 2 条:writeReachesOnlyTheTargetServer / writeValue_isOurs_notTheValueReadBack
//     绿 5 条:三个读用例 + slaveId_landsInUnitId_onTheWire + 三台建连
//     现象:s2_->writeCount() 实测 **0**(期望 1)—— 目标服务器一个字节都没收到,
//     而值全跑去了 plc1。写错设备的危害在这里比读更直接:
//     读错是显示值不对,写错是【往别的机器下发控制指令】。
//
//   ★ 一处修正:第 1 轮之前,我因为自己服务端两处组帧错误(Length 少算 uid+fc、
//     读的 count 取错偏移)误判过一次"读路径也坏了"。产品侧当时其实完全正确
//     —— 日志里 slave=9/5/1、addr=300/200/100 都对,是测试端回帧错了。
//     这也正是本文件存在的理由:【端到端出错时,先确认是哪一边错】。
//
// 关联:PlcConnectionManager(建连接), PollScheduler(读), WriteScheduler(写),
//      ModbusMaster(协议实现), TcpClient(链路)
// ============================================================================

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QVariant>

#include "plc/PlcConnectionManager.h"
#include "PollScheduler.h"
#include "WriteScheduler.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/IPollDriver.h"
#include "drivers/IPlcDriverProvider.h"

using qwhmi::drivers::IPollDriver;
using qwhmi::drivers::IPlcDriverProvider;

// ============================================================================
// FakeModbusServer —— 一个只够用的 Modbus TCP 从站
// ============================================================================
namespace {

// ★ 三台的身份标记。整个寄存器空间都填这个值。
//   "plc2 的变量上出现了 0x1111" 这种红,在断言里一眼就懂出了什么事。
constexpr quint16 kFill_plc1 = 0x1111;
constexpr quint16 kFill_plc2 = 0x2222;
constexpr quint16 kFill_plc3 = 0x3333;

// ★ 写用例专用的"我们的值"。三台 fill 值里都没有它 ——
//   这样"服务器收到了 0x7777"才能证明写的是我们的值,而不是读回来的 fill。
constexpr quint16 kWriteValue = 0x7777;

void appendBE16(QByteArray& out, quint16 v) {
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

quint16 readBE16(const QByteArray& b, int off) {
    return static_cast<quint16>((quint8(b.at(off)) << 8) | quint8(b.at(off + 1)));
}

class FakeModbusServer : public QObject {
    Q_OBJECT
public:
    explicit FakeModbusServer(quint16 fillValue, QObject* parent = nullptr)
        : QObject(parent), fill_(fillValue) {}

    // ★ 这个析构函数是【必需的】，不是保险起见。
    //
    //   踩过的坑：cleanupTestCase 里 `delete s1_` 时崩在
    //   QHashPrivate::Span<...>::offset —— 0xC0000005。
    //   原因两条，叠在一起：
    //   ① QTcpServer::nextPendingConnection() 返回的 socket **没有 parent**,
    //      delete FakeModbusServer 并不连带删掉它们;
    //   ② 那些 socket 后来自己析构,发 disconnected,
    //      触发 lambda 里的 `bufs_.remove(s)` —— 而 this 已经没了。
    //      于是对一个已释放的 QHash 做哈希运算,崩在 Span::offset。
    //
    //   → 这里先 clear() 让断开回调无论何时到达都无桶可查;
    //   → onNewConnection 里再给 socket setParent(this),让生命周期真的绑上。
    ~FakeModbusServer() override { bufs_.clear(); }

    // 起服务。传 0 让系统分配端口 —— 三条用例、三台服务端,写死端口必撞。
    // ★ 只 listen LocalHost:bind(QHostAddress::Any) 在 Windows 上会撞保留端口
    //   (445/139 那类),要管理员权限。测试不该需要提权。
    bool start() {
        if (!server_.listen(QHostAddress::LocalHost, 0)) return false;
        connect(&server_, &QTcpServer::newConnection, this, &FakeModbusServer::onNewConnection);
        return true;
    }

    // ★ 模拟"PLC 掉电":abort 掉所有已连的 socket,而不是只 close() 监听。
    //   只 close() 监听的话,已建立的连接不会断,客户端那边【永远不会】
    //   收到 disconnected —— 而"对端察觉到了掉线"正是那个用例的前提。
    void stop() {
        // ★ 迭代【副本】,不能直接迭代 clients_ —— abort() 会同步发 disconnected,
        //   断开 lambda 里 clients_.removeAll(s) 就在改这个正在被迭代的列表。
        //   这不是理论风险:第一次写的就是直接迭代,Qt5/6 的 QList 隐式共享
        //   在 removeAll 触发深拷贝后,迭代器就指到旧块上去了。
        const QList<QTcpSocket*> doomed = clients_;
        for (QTcpSocket* s : doomed) s->abort();
        clients_.clear();
        bufs_.clear();
        server_.close();
    }

    quint16 port() const { return server_.serverPort(); }
    bool isListening() const { return server_.isListening(); }

    // ---- 观测口 ----
    int  readCount()  const { return readCount_; }
    int  writeCount() const { return writeCount_; }
    QList<int>     seenUnitIds()    const { return seenUnitIds_; }
    QList<int>     writtenAddrs()   const { return writtenAddrs_; }
    QList<quint16> writtenValues()  const { return writtenValues_; }
    // 已连接的客户端个数。用来证明"服务端那边真的接受了三条连接",
    // 而不是只相信客户端自己说 connected 了。
    int clientCount() const { return clients_.size(); }

    void resetCounters() {
        readCount_ = writeCount_ = 0;
        seenUnitIds_.clear();
        writtenAddrs_.clear();
        writtenValues_.clear();
    }

private slots:
    void onNewConnection() {
        while (QTcpSocket* s = server_.nextPendingConnection()) {
            s->setParent(this);   // ★ 生命周期绑到 this,见析构函数那段注释
            clients_.append(s);
            bufs_.insert(s, QByteArray());
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                clients_.removeAll(s);
                bufs_.remove(s);
                // 注意【不】在这里 deleteLater:deleteLater 只是排队,
                // 而 bufs_ 的桶已经摘了,下一次 readyRead 若它还在(半开连接)
                // 会重新 insert 一个空桶、丢掉残留字节 —— 所以 abort 掉更干净。
                s->abort();
            });
        }
    }

private:
    // ---- ★ 三条硬约束的对应点全在 respond() 里 ----
    void onReadyRead(QTcpSocket* s) {
        QByteArray& buf = bufs_[s];
        buf.append(s->readAll());

        while (buf.size() >= 7) {
            const int len = readBE16(buf, 4);
            const int frameSize = 6 + len;   // Length 不含它自己那 2 字节
            if (buf.size() < frameSize) break;

            const QByteArray frame = buf.left(frameSize);
            buf.remove(0, frameSize);
            respond(s, frame);
        }
    }

    void respond(QTcpSocket* s, const QByteArray& req) {
        // 请求帧的字节偏移(MBAP 7 字节 + PDU):
        //   [0..1] TID  [2..3] Protocol  [4..5] Length  [6] Unit ID  [7] FC
        //   读: [8..9] addr  [10..11] count
        //   写: [8..9] addr  [10..11] value(0x05/0x06) 或 quantity(0x0F/0x10)
        // ★ 这张表是本函数最容易错的地方 —— 我第一版把读的 count 也读在 8,
        //   于是 count=100 时按 100 个寄存器回,byteCount 声明 200 字节,
        //   而实际数据只有 ... 总之 ModbusMaster 那边判"数据不足"就整帧丢了,
        //   现象是【3 秒后超时、值停在初始值、日志一句错都没有】。

        // ---- 约束 ①:必须回【原 TID】 ----
        //   ModbusMaster 用 pending_.find(tid) 匹配,匹配不上就静默丢弃
        //   (ModbusMaster.cpp:396-400)。回错 TID 的表现是"变量永远不刷新",
        //   而服务端这边【看不到任何异常】。
        const quint16 tid = readBE16(req, 0);

        // ---- 约束 ②:必须回【与请求相同的 Unit ID】 ----
        //   ModbusMaster.cpp:403-406 判 uid != req.serverAddress 就 continue,
        //   同样不报错。slaveId=5 的 plc2 去读 unit id 填 1 的从机,
        //   现象是"值永远停在初始的 Bad"。
        const quint8 uid = quint8(req.at(6));

        seenUnitIds_.append(int(uid));

        const quint8 fc = quint8(req.at(7));
        QByteArray pdu;   // FC 之后的所有字节
        pdu.append(char(fc));   // 功能码原样回

        switch (fc) {
            case 0x01: case 0x02: case 0x03: case 0x04: {
                // 读:body = addr(2) + count(2),count 在【10..11】
                const int count = readBE16(req, 10);
                const int byteCount = (fc == 0x03 || fc == 0x04) ? count * 2 : (count + 7) / 8;
                // ③ byteCount 字段 —— 整帧的 frame[8],ModbusMaster.cpp:467 读它
                pdu.append(char(byteCount));
                if (fc == 0x03 || fc == 0x04) {
                    for (int i = 0; i < count; ++i) appendBE16(pdu, fill_);
                } else {
                    for (int i = 0; i < byteCount; ++i) pdu.append(char(0xFF));
                }
                ++readCount_;
                break;
            }
            // ---- 写:应答都是"回显 addr(2) + X(2)",总长 12 字节 ----
            //   ModbusMaster.cpp:528 判 frame.size() < 12,12 刚好过。
            //   (0x06 回显 addr+value,0x10 回显 addr+quantity)
            case 0x05: case 0x06: case 0x0F: case 0x10: {
                const int addr = readBE16(req, 8);
                writtenAddrs_.append(addr);
                // 0x05/0x06 的 value、0x0F/0x10 的 quantity 都在 req[10..11]。
                // 两种都取同一处 —— 我们只关心"我们发的那个值到了没有"。
                writtenValues_.append(readBE16(req, 10));
                pdu.append(req.mid(8, 4));   // addr(2) + value/qty(2) 原样回显
                ++writeCount_;
                break;
            }
            default: {
                // 异常响应 FC|0x80 + ExCode=0x01(Illegal Function)。
                // 真从站对不认的功能码就是这么回的 —— 比"装作没听见"好,
                // 因为 ModbusMaster 会把它转成一条明确的错误。
                pdu[0] = char(fc | 0x80);
                pdu.append(char(0x01));
                break;
            }
        }

        // ★ 组帧。Length = 【UID(1) + FC 后面全部 PDU】,不含 Length 自己那 2 字节。
        //
        //   【这里我第一版写错了,而且错得很值得记】
        //   当时写的是 `appendBE16(head, resp.size() - 6)`,即"减掉 MBAP 前 6 字节"。
        //   但那 6 字节里【已经不含 uid 了】—— uid 在 resp[6],在 resp.size()-6 的范围里。
        //   于是 Length 少算了 uid + fc 那 2 字节,ModbusMaster 按 frameSize=6+Length
        //   只取到比实际少 2 字节的帧,剩下的半帧留在缓冲里等下一帧 —— 表现为
        //   "读响应 3 秒后超时、写响应判帧太短",而服务端这边一切正常。
        //   → Length 必须【从 Unit ID 开始算】,即 resp.size() - 6 + 1(uid)。
        QByteArray resp;
        appendBE16(resp, tid);                        // ① TID 回原
        appendBE16(resp, 0x0000);                     // Protocol 恒 0
        appendBE16(resp, quint16(1 + pdu.size()));    // Length = UID + PDU
        resp.append(char(uid));                       // ② Unit ID 回原
        resp.append(pdu);

        s->write(resp);
    }

    QTcpServer server_;
    QList<QTcpSocket*> clients_;
    QHash<QTcpSocket*, QByteArray> bufs_;
    quint16 fill_;
    int readCount_ = 0, writeCount_ = 0;
    QList<int> seenUnitIds_, writtenAddrs_;
    QList<quint16> writtenValues_;
};

// ★ 下面这段连线【就是生产里 hmi.cpp 写的那两段】。
//   把同样的逻辑复制到测试里不算"测了生产",但 hmi.cpp 链不进 hmi_core,
//   所以这里连的是同一批类之间的同一组调用关系;
//   负向对照改的是 PollScheduler.cpp / WriteScheduler.cpp 里的 driver 查找,
//   而那正是被这两段连线真正驱动的生产代码。
void wireDriverAdded(PlcConnectionManager* mgr, PollScheduler* ps,
                     WriteScheduler* ws, QObject* recv) {
    QObject::connect(mgr, &PlcConnectionManager::driverAdded, recv,
                     [ps, ws](const QString& id, IPollDriver* d) {
                         ps->addDriver(id, d);
                         ws->hookDriver(id, d);
                     });
    QObject::connect(mgr, &PlcConnectionManager::driverAboutToBeRemoved, recv,
                     [ps, ws](const QString& id) {
                         ps->removeDriver(id);
                         ws->unhookDriver(id);
                     });
}

} // namespace

// ============================================================================

class tst_PlcMultiServerE2E : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ---------- ★★★ 核心:三条真连接,各回各的 ----------
    void threeDrivers_connectToThreeDifferentSockets();
    void eachPlc_receivesItsOwnReply();

    // ---------- 写路径 ----------
    void writeReachesOnlyTheTargetServer();
    void writeValue_isOurs_notTheValueReadBack();

    // ---------- 故障隔离 ----------
    void onePlcDisconnects_othersKeepPolling();

    // ---------- slaveId 真的进了帧 ----------
    void slaveId_landsInUnitId_onTheWire();

    // ---------- 存量 bug:非连续地址 ----------
    void nonContiguousAddresses_allStillRefresh();

private:
    Variable* addReadVar(const QString& name, const QString& plcId, int addr);
    QJsonArray makeConfig() const;
    // 装上三台并等它们都真连上。负向对照要改配置,所以每条用例自己调。
    //   ★ 故意返回 void 而不是 bool:QTRY_* 宏在失败时执行的是 `return;` ——
    //   放在返回 bool 的函数里【编译不过】。改成"失败就中止本用例"更自然,
    //   也不会漏掉后面的断言。
    void bringUpThreePlcs();

    FakeModbusServer* s1_ = nullptr;   // fill 0x1111
    FakeModbusServer* s2_ = nullptr;   // fill 0x2222
    FakeModbusServer* s3_ = nullptr;   // fill 0x3333

    PlcConnectionManager* mgr_ = nullptr;
    PollScheduler*       ps_  = nullptr;
    WriteScheduler*      ws_  = nullptr;
    VariableManager*     vm_  = nullptr;
};

// ---------------------------------------------------------------------------
// 搭建 / 拆
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::initTestCase() {
    s1_ = new FakeModbusServer(kFill_plc1);
    s2_ = new FakeModbusServer(kFill_plc2);
    s3_ = new FakeModbusServer(kFill_plc3);
    QVERIFY2(s1_->start(), "plc1 服务端起不来(端口/权限问题)。");
    QVERIFY2(s2_->start(), "plc2 服务端起不来。");
    QVERIFY2(s3_->start(), "plc3 服务端起不来。");
}

// ★ 每条用例一份全新的装配。
//   【为什么不放 initTestCase 一把搭好】连接是异步的,而不同的用例要改配置
//   (负向对照正是靠改配置做的)。共用一个装好的状态,后面每条用例都得写
//   "先恢复到初始状态"的代码 —— 那种代码漏一行就是一条看不懂的红。
//   代价是每条用例重连三次(回环上几毫秒),换来的是每条都从同一个起点开始。
void tst_PlcMultiServerE2E::init() {
    vm_  = new VariableManager(this);
    mgr_ = new PlcConnectionManager(this);
    // ★ PollScheduler 不再塞单 driver —— 生产里 driver 是运行期由
    //   PlcConnectionManager 一台一台建出来的,启动时一个都还不存在。
    ps_ = new PollScheduler(vm_, nullptr, this);
    // ★ 读侧 slaveId 的来源。没装它的话 PollScheduler 会退回字面量 1,
    //   那是 2b 修掉的两个硬编码点之一(另一个在 WriteScheduler)。
    ps_->setSlaveIdProvider(mgr_);
    ws_ = new WriteScheduler(vm_, mgr_, this);

    wireDriverAdded(mgr_, ps_, ws_, this);
    s1_->resetCounters(); s2_->resetCounters(); s3_->resetCounters();
}

void tst_PlcMultiServerE2E::cleanup() {
    // ★★ 顺序有硬约束,踩过一次:【必须先让 manager 摘完连接,再拆 ps_/ws_】。
    //
    //   我第一版反过来写(ps_->stop(); delete ps_; 先走),立刻 0xC0000005,
    //   崩在 PollScheduler::unhookDriver 里的 QHash::value() —— 因为
    //   wireDriverAdded 捕获的 ps 指针已经悬空了,而 mgr_->applyConfig(空)
    //   还要 emit driverAboutToBeRemoved 去调它。
    //
    //   ★ 这跟生产 hmi.cpp 的生命周期顺序是同一件事:
    //     plcMgr_ 必须【比 pollScheduler_ 活得久】。
    //     hmi.cpp 里靠的是"QObject 按构造逆序析构"——
    //     plcMgr_ 先构造 → 后析构,而它析构时 pollScheduler_ 还活着。
    //     显式 delete 就绕开了这个隐式保证,顺序必须自己写对。
    if (mgr_) {
        mgr_->applyConfig(QJsonArray());   // 先摘三台:emit driverAboutToBeRemoved → ps_/ws_ 还活着
        delete mgr_; mgr_ = nullptr;
    }
    // 再停定时器。stop() 清 pending_ / inflight;TcpClient 的重连退避定时器
    // 还活着的话,delete 之后它会回来碰已释放的 socket。
    if (ps_) { ps_->stop(); delete ps_; ps_ = nullptr; }
    if (ws_) { ws_->stop(); delete ws_; ws_ = nullptr; }
    // ★ 变量放最后清:VariableManager::clear() 会 emit variableReplaced(_, nullptr),
    //   PollScheduler / WriteScheduler 都还连着这个信号(它们刚 stop 但没析构完)。
    if (vm_) { vm_->clear(); delete vm_; vm_ = nullptr; }

    // 掉线那条用例把 s2 stop() 掉了,这里补起一次给下一条用例用。
    // ★ stop() 会换端口(重新 listen(0))—— 下一条用例的 makeConfig()
    //   读的是 port(),所以自动跟上,这里【不能】把端口缓存下来。
    if (!s1_->isListening()) QVERIFY(s1_->start());
    if (!s2_->isListening()) QVERIFY(s2_->start());
    if (!s3_->isListening()) QVERIFY(s3_->start());
}

void tst_PlcMultiServerE2E::cleanupTestCase() {
    delete s1_; s1_ = nullptr;
    delete s2_; s2_ = nullptr;
    delete s3_; s3_ = nullptr;
}

void tst_PlcMultiServerE2E::bringUpThreePlcs() {
    const auto r = mgr_->applyConfig(makeConfig());
    QVERIFY2(r.added.size() == 3,
             // rejected 里是具体哪台(manager 的 qCWarning 已经点名了,
             // 这里再报一次是因为测试的失败信息必须自成一体 ——
             // CI 上只看 ctest 输出的那一行,看不到 qCWarning 的上下文)
             qPrintable(QStringLiteral("只建起 %1 台,rejected=[%2]")
                        .arg(r.added.size())
                        .arg(r.rejected.join(','))));
    for (const char* id : {"plc1", "plc2", "plc3"}) {
        IPollDriver* d = mgr_->driverFor(QString::fromLatin1(id));
        QVERIFY(d);
        QTRY_VERIFY_WITH_TIMEOUT(d->isConnected(), 5000);
    }
}

QJsonArray tst_PlcMultiServerE2E::makeConfig() const {
    const auto one = [](const QString& id, int port, int slaveId) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
        o.insert(QStringLiteral("port"), port);
        o.insert(QStringLiteral("protocol"), QStringLiteral("modbus"));
        o.insert(QStringLiteral("slave_id"), slaveId);
        return o;
    };
    QJsonArray a;
    // ★ 三台的 slave_id 刻意是 1 / 5 / 9(后两个 != 1)。
    //   2b 之前 PollScheduler 和 WriteScheduler 都写死字面量 1,
    //   而 ModbusMaster 对 Unit ID 不匹配的响应是【静默丢弃】——
    //   所以那个 bug 的现场表现是"值永远停在初始 Bad",而不是任何报错。
    a.append(one(QStringLiteral("plc1"), s1_->port(), 1));
    a.append(one(QStringLiteral("plc2"), s2_->port(), 5));
    a.append(one(QStringLiteral("plc3"), s3_->port(), 9));
    return a;
}

Variable* tst_PlcMultiServerE2E::addReadVar(const QString& name,
                                            const QString& plcId, int addr) {
    auto* v = new Variable(name, static_cast<quint16>(addr),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly);
    v->setPlcId(plcId);
    vm_->addVariable(v);
    return v;
}

// ---------------------------------------------------------------------------
// ★★★ 核心
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::threeDrivers_connectToThreeDifferentSockets() {
    bringUpThreePlcs();
    QCOMPARE(mgr_->connectAttemptCount(), 3);

    // 三个指针必须互不相同 —— 一台 PLC 一个 driver,
    // 连"复用同一个 driver 连三个地址"都不允许
    // (那等于把写错设备的老问题换个形式继续留着)。
    IPollDriver* d1 = mgr_->driverFor(QStringLiteral("plc1"));
    IPollDriver* d2 = mgr_->driverFor(QStringLiteral("plc2"));
    IPollDriver* d3 = mgr_->driverFor(QStringLiteral("plc3"));
    QVERIFY(d1 && d2 && d3);
    QVERIFY2(d1 != d2 && d2 != d3 && d1 != d3,
             "三个 plcId 拿到了同一个 driver —— 那样「各回各的」是假的。");

    // ★ 三条连接【服务端那边】也真的接受了。
    //   只信 isConnected() 是自说自话 —— 那是客户端自己的状态位。
    //   整个 2b 要证明的就是"三条 socket 各连各的",而这件事只有对端知道。
    QTRY_COMPARE_WITH_TIMEOUT(s1_->clientCount(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(s2_->clientCount(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(s3_->clientCount(), 1, 5000);
}

void tst_PlcMultiServerE2E::eachPlc_receivesItsOwnReply() {
    bringUpThreePlcs();

    // ★★★ 值本身编码身份 —— 本文件存在的全部理由。
    //   三个变量,三台不同的机器,整个寄存器空间各自填 0x1111/0x2222/0x3333。
    //   plc2 的变量上必须出现 0x2222 —— 如果三条 socket 错接到同一台、
    //   或者 TID / Unit ID 串了(那两种在 ModbusMaster 里都是【静默丢弃】),
    //   它只会停在初始值,而且日志里一句错都没有。
    Variable* v1 = addReadVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100);
    Variable* v2 = addReadVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200);
    Variable* v3 = addReadVar(QStringLiteral("V3"), QStringLiteral("plc3"), 300);
    ps_->setAllActive();
    ps_->start();

    QTRY_COMPARE_WITH_TIMEOUT(v1->value().toInt(), int(kFill_plc1), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(v2->value().toInt(), int(kFill_plc2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(v3->value().toInt(), int(kFill_plc3), 5000);

    // ★ 三条都拿到 quality == Good 才有意义:值变了但状态是 Bad,
    //   说明那是最后一次成功之后的残留,不能算"现在读到了"。
    QCOMPARE(v1->quality(), Variable::Quality::Good);
    QCOMPARE(v2->quality(), Variable::Quality::Good);
    QCOMPARE(v3->quality(), Variable::Quality::Good);

    // ★ 把话挑明:上面三条里任何一条对调(比如 v2 上是 0x1111),
    //   都只有一个原因 —— plc2 的变量用了别的 PLC 的连接。
    //   这正是 2b 之前的状态:界面上看不出任何异常,值也在刷新,
    //   只不过三台机器刷新的是同一台。
    //   Mock 测试永远抓不到这个 —— mock 只验对象指针,不验指针背后的 socket。
    QVERIFY2(v2->value().toInt() != int(kFill_plc1),
             "★ plc2 的变量上出现了 plc1 的值(0x1111)—— 连接串了。");

    // ★ 三台服务端都真的被问过,不是某一台在自说自话。
    QVERIFY2(s1_->readCount() > 0 && s2_->readCount() > 0 && s3_->readCount() > 0,
             "★ 有服务端一个请求都没收到。值对了但没真被问过的话,断言不成立。");
}

// ---------------------------------------------------------------------------
// 写路径
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::writeReachesOnlyTheTargetServer() {
    bringUpThreePlcs();

    // ★★ 【不】启动 PollScheduler。理由见文件头:
    //   读会 setValue 覆盖本地值,而 WriteScheduler 在 onTick 里现取 v->value(),
    //   两者一起跑就有真实竞态:值刚设成 0x7777,下一帧读回来 0x2222,
    //   发出去的就成了 0x2222 —— 那会让本用例随机红,而红因是竞态不是 bug。
    //   写路径必须单独隔离,否则测的是时序而不是路由。
    auto* v = new Variable(QStringLiteral("W2"), 0x0100,
                           Variable::DataType::Int16, Variable::Access::ReadWrite);
    v->setPlcId(QStringLiteral("plc2"));
    v->setValue(QVariant::fromValue(static_cast<short>(kWriteValue)));
    v->markDirty();
    vm_->addVariable(v);

    ws_->start();
    QTRY_COMPARE_WITH_TIMEOUT(s2_->writeCount(), 1, 5000);

    // ★★★ 本用例的正文:另外两台的写计数必须【一直是 0】。
    //   2b 之前 WriteScheduler 全代码零 plcId,三个变量全都从 plcs[0]
    //   的 socket 发出去 —— 界面无异常、审计日志有记录、零日志。
    QVERIFY2(s1_->writeCount() == 0,
             "★ plc1 收到了本该发给 plc2 的写。这是 2b 要消灭的核心 bug:"
             "操作员点 2 号机的按钮,1 号机的数值变了。");
    QVERIFY2(s3_->writeCount() == 0, "★ plc3 收到了本该发给 plc2 的写。");

    // 地址也要对 —— 值对了地址错了同样是写错设备(写到了 2 号机的另一个点)
    QVERIFY2(!s2_->writtenAddrs().isEmpty(), "plc2 一条写都没收到。");
    QCOMPARE(s2_->writtenAddrs().value(0), 0x0100);
}

void tst_PlcMultiServerE2E::writeValue_isOurs_notTheValueReadBack() {
    bringUpThreePlcs();

    auto* v = new Variable(QStringLiteral("W3"), 0x0200,
                           Variable::DataType::Int16, Variable::Access::ReadWrite);
    v->setPlcId(QStringLiteral("plc3"));
    v->setValue(QVariant::fromValue(static_cast<short>(kWriteValue)));
    v->markDirty();
    vm_->addVariable(v);

    ws_->start();
    QTRY_COMPARE_WITH_TIMEOUT(s3_->writeCount(), 1, 5000);

    // ★ kWriteValue(0x7777)刻意不等于 plc3 的 fill(0x3333)。
    //   所以这一条能区分"我们写的值到了"和"读回来的值被写回去"。
    //   两边填同一个数的话,这条用例就永远绿,什么也证明不了。
    QVERIFY2(!s3_->writtenValues().isEmpty(), "plc3 一条写都没收到。");
    QCOMPARE(s3_->writtenValues().value(0), kWriteValue);

    // ★ 服务器确实收到了帧,才能谈"收到了我们的值"——
    //   否则空列表上的 value(0) 恰好等于 0 时,那个比较会骗人。
    QCOMPARE(s1_->writeCount(), 0);
    QCOMPARE(s2_->writeCount(), 0);
}

// ---------------------------------------------------------------------------
// 故障隔离
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::onePlcDisconnects_othersKeepPolling() {
    bringUpThreePlcs();

    Variable* v1 = addReadVar(QStringLiteral("V1"), QStringLiteral("plc1"), 100);
    Variable* v2 = addReadVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200);
    Variable* v3 = addReadVar(QStringLiteral("V3"), QStringLiteral("plc3"), 300);
    ps_->setAllActive();
    ps_->start();

    QTRY_COMPARE_WITH_TIMEOUT(v1->value().toInt(), int(kFill_plc1), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(v2->value().toInt(), int(kFill_plc2), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(v3->value().toInt(), int(kFill_plc3), 5000);

    // ★ 杀掉 plc2 的服务端。stop() 里 abort 掉 socket,
    //   所以对端【真的】会收到 disconnected —— 只 close() 监听是不够的,
    //   已建立的连接不会断,客户端会一直以为连着,本用例就没被测到。
    s2_->stop();

    const int r1Before = s1_->readCount();
    const int r3Before = s3_->readCount();

    // 掉线那台的变量必须被标 Bad —— 界面不能停在一个几分钟前的旧值上
    // 还显示"Good"。这是数据可信度的铁律。
    QTRY_VERIFY_WITH_TIMEOUT(v2->quality() == Variable::Quality::Bad, 5000);

    // ★ 而另外两台的读【必须继续涨】。
    //   这条钉的是故障隔离:一台 PLC 掉线不能让其余的停摆。
    QTRY_VERIFY_WITH_TIMEOUT(s1_->readCount() > r1Before, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(s3_->readCount() > r3Before, 5000);
}

// ---------------------------------------------------------------------------
// slaveId 真的进了帧
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::slaveId_landsInUnitId_onTheWire() {
    bringUpThreePlcs();
    // ★ 必须在 applyConfig 之后才问 —— applyConfig 之前 manager 里一条都没有,
    //   slaveIdFor 会照接口约定回缺省 1(而不是报错或回 0)。
    //   我第一版把这两行写在 bringUpThreePlcs 前面,现象是 QCOMPARE(1, 5) 红,
    //   看起来像"slaveId 没从配置读出来",其实是问得太早。
    QCOMPARE(mgr_->slaveIdFor(QStringLiteral("plc1")), 1);
    QCOMPARE(mgr_->slaveIdFor(QStringLiteral("plc2")), 5);
    QCOMPARE(mgr_->slaveIdFor(QStringLiteral("plc3")), 9);
    // 没配过的 plcId 走缺省 1,绝不能是 0 —— 0 是 Modbus 广播地址
    QCOMPARE(mgr_->slaveIdFor(QStringLiteral("plcX")), 1);

    // ★ 只给 plc2 放变量 —— 于是只有 plc2 的服务端会收到任何请求,
    //   seenUnitIds 里出现别的值就一定是串了。
    addReadVar(QStringLiteral("V2"), QStringLiteral("plc2"), 200);
    ps_->setAllActive();
    ps_->start();

    QTRY_VERIFY_WITH_TIMEOUT(s2_->readCount() > 0, 5000);
    QVERIFY(!s2_->seenUnitIds().isEmpty());

    // 服务端每收到一帧都记了 Unit ID,这里要求【全部】是 5。
    // 写成"包含 5 就过"太弱 —— 混进 1 同样是 bug,而那正是 2b 之前的形态。
    for (int uid : s2_->seenUnitIds()) {
        const QByteArray msg =
            QStringLiteral("★ plc2 配的 slave_id=5,服务端却收到 Unit ID=%1。"
                           "2b 之前两个调度器都写死字面量 1。"
                           "ModbusMaster 对 Unit ID 不匹配的响应是【静默丢弃不报错】"
                           "(ModbusMaster.cpp:403-406),"
                           "现场表现是值永远停在初始值,不报任何错。").arg(uid).toUtf8();
        QVERIFY2(uid == 5, msg.constData());
    }

    // 另外两台一个请求都没收到 —— 证明上面那个 Unit ID 序列确实是 plc2 的
    QCOMPARE(s1_->readCount(), 0);
    QCOMPARE(s3_->readCount(), 0);
}

// ---------------------------------------------------------------------------
// 存量 bug:非连续地址
// ---------------------------------------------------------------------------

void tst_PlcMultiServerE2E::nonContiguousAddresses_allStillRefresh() {
    bringUpThreePlcs();

    // ★ 九个不连续地址(两两相隔 3,中间 3 个没人要)。
    //   PollScheduler 会把它们拆成 9 个独立读请求,而 ModbusMaster 协议串行、
    //   只允许 1 个在途 —— 于是第 2 个请求起会被 sendReadRequest 拒收。
    //
    //   【改之前的实际行为,不是风险,是现状】onTick 不检查返回值:
    //   第 2 帧起 ModbusMaster 拒收(只 emit errorOccurred,【不发 readFinished】,
    //   ModbusMaster.cpp:233-237),而 pending_[reqId] / inFlight_ 在发送【前】
    //   就登记了 —— 于是这批变量永远等不到 reply,永久停在旧值。
    //
    //   现场表现:同一台 PLC 上地址连续的那几个变量有值,
    //   中间跳过几个地址的那几个永远停在旧值,日志里只有一句
    //   "上一帧未完成,稍后再发" —— 看不出后果是什么。
    QList<Variable*> vars;
    for (int i = 0; i < 9; ++i) {
        vars.append(addReadVar(QStringLiteral("N%1").arg(i),
                               QStringLiteral("plc1"), 100 + i * 4));
    }
    ps_->setAllActive();
    ps_->start();

    // ★ 九个必须【全部】刷新。一个不落才叫修好了 ——
    //   只断言"第一个刷新了"的话,改之前也过。
    //
    //   【为什么先等"全部刷新"再一起报,而不是逐个 QTRY】
    //   ① Qt 6 没有 QTRY_VERIFY2_WITH_TIMEOUT(只有 QTRY_VERIFY_WITH_TIMEOUT,
    //      它不带自定义消息),带消息就得放弃超时等 —— 而这里恰恰需要等。
    //   ② 九个变量的期望值【全都是 0x1111】。QTRY 失败只打 Actual/Expected,
    //      不带变量名的话看到 "Actual: 0" 也不知道是哪一个卡住了。
    //      而"哪一个卡住"正是这条用例的全部诊断价值 —— 非连续地址的
    //      症状高度依赖布局(第一个通常能刷新,后面几个开始卡)。
    auto allRefreshed = [this, &vars] {
        for (Variable* v : vars) {
            if (v->value().toInt() != int(kFill_plc1)) return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(allRefreshed(), 10000);

    // 走到这里说明九个都刷新了。这几行是为了让"红了的话"能一眼定位,
    // 代价是零 —— 全绿时它们什么都不做。
    QStringList stuck;
    for (int i = 0; i < vars.size(); ++i) {
        if (vars[i]->value().toInt() != int(kFill_plc1)) {
            stuck << QStringLiteral("N%1(addr=0x%2,值=%3)")
                         .arg(i)
                         .arg(vars[i]->address(), 4, 16, QChar('0'))
                         .arg(vars[i]->value().toString());
        }
    }
    QVERIFY2(stuck.isEmpty(),
             qPrintable(QStringLiteral("这几个没刷新:%1。改之前,地址不连续的那几个"
                                       "会被永久卡在初始值(且日志里一句错都没有)")
                        .arg(stuck.join(QStringLiteral(" / ")))));
}

QTEST_MAIN(tst_PlcMultiServerE2E)
#include "tst_PlcMultiServerE2E.moc"

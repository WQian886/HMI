// ============================================================================
// ★ 多 PLC 2b(2026-10-19):把工程文件里的 plcs[] 变成【运行期真的在线的 N 台 PLC】
//
// 文件职责:
//   输入:一段 QJsonArray(工程文件的 plcs[] 原文)
//   输出:N 个 IPollDriver,各自连各自的 host:port;以及按 plcId 的查询能力
//         (实现 IPlcDriverProvider,给 WriteScheduler 用)
//
// 2a 把配置层修好了(plc_id 进 JSON、plcs[] 存得住),但运行期仍然只连第一台。
// 本类就是那个断掉的上游接线:plcs_ 配置 → N 个 driver。
//
// ===== 三个刻意的取舍(改之前先读这段)=====
//
// ① configApplied() 【不带任何参数】
//    想把 ApplyResult 塞进信号就得:自定义 struct + Q_DECLARE_METATYPE +
//    (走队列连接时还要 qRegisterMetaType)。而这里全部是主线程同步调用,
//    收益为零、坑全占。ApplyResult 只作 applyConfig 的同步返回值,测试直接断言。
//    【谁需要结果就调 applyConfig】,不要为了"统一"加个带参信号。
//
// ② lastConfig_ 存【原始 QJsonArray】,绝不"按已知字段重建"
//    重建 == 宣告"代码没列的字段一律丢弃"。工程文件里今天有 modbus.timeout_ms,
//    明天可能就有 modbus.poll_interval_ms——按已知字段重建的话,今天还没写的字段
//    明天就静默消失了。理由与 Project::saveToFile 里路径 1/2 完全同源。
//    【唯一允许"重建"的是 diff 用的四元组】,那个只用于比较,不用于写回。
//
// ③ Entry::driver 是【非拥有】指针,所有权 100% 靠 setParent(this)
//    QHash 的析构【不】delete 元素值。所以:
//      · 建 driver 时必须 driver->setParent(this)
//      · 删 driver 时必须先 emit driverAboutToBeRemoved(让下游摘连接)
//        再 deleteLater() —— 不能直接 delete,那时下游可能还连着它
//    漏设 parent = 泄漏,而且静态分析【不报】。这是 review 检查项。
//
// ===== diff 判据只比 (host, port, protocol, slave_id) 四元组 =====
//   timeout_ms / max_retries / poll_interval_ms 【不】触发重连:
//   TcpClient 的超时参数在 connectTo() 那一刻就定死了(TcpClient.h:22-27 全是
//   static constexpr 常量,没有 setter),运行期改不动,重连也用不上。
//   所以这类改动只更新配置 + 打日志说"下次生效"。
//
//   【判错两个方向的后果不对称,所以两个方向都要测】
//     · 永远判"没变"  → 用户改了 IP,程序说"已应用",现场连的还是老地址
//     · 永远判"变了"  → 每次打开工程都把所有 PLC 全断线重连一遍,现场 PLC 抖动
//
// ===== 线程 =====
//   单线程架构(全项目一个 100ms QTimer,没有 QThread/QThreadPool),
//   本类因此【只允许在主线程】调用,不做任何加锁。
//   引入线程之前必须先把它挪到线程里,而不是在这里塞一把没用的锁。
//
// 关联:IPlcDriverProvider.h(本类实现的接口), DriverFactory.h(建 driver),
//      Project.h(阶段 3 由它调用), WriteScheduler.h(阶段 4 消费本类)
// ============================================================================

#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QString>
#include <QStringList>

#include "drivers/IPlcDriverProvider.h"

namespace qwhmi::drivers { class IPollDriver; }

// 日志分类。声明在使用它的 .cpp 里(见 Q_LOGGING_CATEGORY)。
class PlcConnectionManager : public QObject,
                             public qwhmi::drivers::IPlcDriverProvider {
    Q_OBJECT
public:
    explicit PlcConnectionManager(QObject* parent = nullptr);
    ~PlcConnectionManager() override;

    // ★ applyConfig 的返回值。只作同步返回,不进信号(见头注释 ①)。
    struct ApplyResult {
        QStringList added;       // 新建的 plcId
        QStringList removed;     // 删除的 plcId
        QStringList reconnected; // 配置变了 → 拆了重建的 plcId
        QStringList unchanged;   // 本次无事可做的 plcId
        QStringList rejected;    // 建不出 driver 的 plcId(原因见日志)

        int touched() const { return added.size() + removed.size() + reconnected.size(); }
    };

    // ★ 全量对齐:传进来的 plcs 就是"应该在线的全集"。
    //   少了的删掉、多了的建上、改了的重连、没动的什么都不做。幂等:同一份数组
    //   连调 N 次,第二次起 added/removed/reconnected 全空,且【一次 connectTo
    //   都不会调用】—— 这条有专门的用例钉着(见 tst_PlcConnectionManager)。
    //
    //   返回值里 rejected 的项会被跳过,其余照常上线 —— 一台填错不挡整个工程,
    //   与 Project::validatePlcEntry 的既有取舍一致。
    ApplyResult applyConfig(const QJsonArray& plcs);

    // ===== IPlcDriverProvider 实现 =====
    // 找不到返回 nullptr(调用方必须处理,不许退化成"取第一个")
    qwhmi::drivers::IPollDriver* driverFor(const QString& plcId) const override;
    // 缺省 1。【不回 0】—— 0 是 Modbus 广播地址。理由见 IPlcDriverProvider.h
    int slaveIdFor(const QString& plcId) const override;

    // 当前在管的 plcId(已排序,便于界面显示稳定)
    QStringList managedIds() const;

    // 上一次 applyConfig 传进来的【原始】数组。没调过 applyConfig 时是空的。
    //   【不要拿它当"当前生效配置"用】—— 它是用户刚填的,不一定建得出 driver。
    const QJsonArray& lastConfig() const { return lastConfig_; }

    // 测试/诊断用:当前存活的 driver 个数
    int driverCount() const { return entries_.size(); }

    // ★ 纯观测计数器:本类一共调用过多少次 driver->connectTo()。
    //
    //   【它证明的是什么,不是什么】
    //   证明:applyConfig 的幂等(第二次调用一次 connectTo 都不发)。
    //   不证明:"TcpClient 真的握手成功" —— 那要看 plcStateChanged。
    //
    //   【为什么要它】没有它,"第二次什么都没做"只能靠 ApplyResult 的空列表推断,
    //   而空列表 + 实际偷偷连了一下是完全可能的 bug。生产逻辑不读这个数。
    int connectAttemptCount() const { return connectAttempts_; }

signals:
    // 新建了一台。hmi 侧把 plcId / driver 交给 PollScheduler::addDriver。
    void driverAdded(const QString& plcId, qwhmi::drivers::IPollDriver* driver);

    // ★【删除前】发,不是删除后。名字里的 AboutTo 就是这个意思。
    //   收到的人要在本函数返回前把 driver 摘干净(PollScheduler::removeDriver),
    //   否则 hmi 要 delete 的瞬间,下游还挂着指向它的连接。
    void driverAboutToBeRemoved(const QString& plcId);

    // 连接状态变化(转发自各 driver 的 connected/disconnected)
    void plcStateChanged(const QString& plcId, bool connected);

    // applyConfig 跑完了。【不带参数】—— 见头注释 ①
    void configApplied();

private:
    // 一台 PLC 的运行期状态
    struct Entry {
        QString id;
        QString host;
        int     port    = 0;
        QString protocol;                       // 归一后的协议 ID("modbus" 等)
        int     slaveId = 1;                    // Modbus unit id,缺省 1

        // 【非拥有】指针。生命周期完全靠 setParent(this) + deleteLater。
        // 详见头注释 ③
        qwhmi::drivers::IPollDriver* driver = nullptr;
    };

    // 建一台并连上。失败(rejected)返回 nullptr,日志已打。
    qwhmi::drivers::IPollDriver* createAndConnect(Entry& e);

    // 从一段 plcs[] JSON 填出一个 Entry(不建 driver)。
    //   返回 false = 这条配的不合法/不完整,原因写进 outReason。
    //   【注意】这里【不】重复 Project::validatePlcEntry 已做的检查以外的东西 ——
    //   本类可能被 PlcConfigDialog 直接喂草稿(没过 validate),所以
    //   id/host/port 的基本合法性在这里必须自己兜一遍。
    static bool parseEntry(const QJsonObject& plc, Entry* out, QString* outReason);

    // ★ diff 判据:只比四元组(host/port/protocol/slaveId)。
    //   timeout_ms / max_retries / poll_interval_ms 不参与 —— 理由见头注释。
    //   【这是全类最容易写错的一处】两个方向都错后果不同,两个方向都有用例。
    static bool sameConnection(const Entry& a, const Entry& b);

    // 删一台:emit driverAboutToBeRemoved → disconnect → deleteLater。
    //   顺序不能变(见头注释 ③),也【不能】用 delete:deleteLater 之前
    //   delete 会让下游仍挂着的连接指向已释放内存。
    void dropEntry(const QString& plcId);

    QHash<QString, Entry> entries_;
    // ★ 存原始数组,绝不重建。理由见头注释 ②
    QJsonArray lastConfig_;
    int connectAttempts_ = 0;
};

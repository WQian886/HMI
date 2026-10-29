// ============================================================================
// tst_ActionWrite —— 报警联动的【写通路】端到端(2026-10-25)
//
// 【这个文件在防什么:一条从来没生效过的"停机"联动】
//
//   24 小时长跑实测(soaks/20261021_164804)挖出来的:
//     工程里配了两条联动("温度高" / "温度低" → 把 Speed 写 0)
//     Speed 的声明类型是 UInt16,write_value 是 JSON 的 0
//     → QJsonValue::toVariant() 给的是 QVariant(LongLong)   ★ 见下面的"实测"一节
//     → Variable::setValue 的 matchesDataType 是【严格 typeId 相等】:
//         case Var::UInt16: return v.typeId() == QMetaType::UShort;   (36)
//       QMetaType::LongLong 是 4,不是 36 → Rejected
//     → ActionManager 不 markDirty → WriteScheduler 永远看不到这个变量
//     → 【整条写通路一次都没被用过】
//
//   现场证据(不是推算):
//     · 从站日志 FC6/FC16 计数 = 0
//     · alarm_history 表里 12028 次报警触发,但 action 行数 = 0
//     · 两份应用日志里【零】条 hmi.alarm 分类的行
//
//   ★ 而这条联动在现场是"温度高就停机" —— 一条从没生效过的安全动作。
//     它"看起来是配好的"(工程文件里有、界面能存、审计能查),
//     唯一让它露馅的方式是【从站真的收到写】。
//
// 【★ 实测出来的 Qt 事实(别凭印象改这个文件)★】
//
//   ★★ JSON 的整数 → QVariant(LongLong),不是 QVariant(Int),也不是 Double ★★
//     这一点我【凭 Qt 头文件推断过两次,两次都推断错了】:
//       第一次以为 QJsonValue::Type 只有 Double ⇒ toVariant() 给 QVariant(Double)
//       第二次以为 JSON 整数会给 QVariant(Int)
//     最后是编了个探针(hmi/tests/probe_qjson_variant.cpp)挂真 Qt 6.8.3 跑出来的:
//       parsed 0     : typeId= 4  typeName= qlonglong
//       parsed -1    : typeId= 4  typeName= qlonglong
//       parsed 70000 : typeId= 4  typeName= qlonglong
//       parsed 0.5   : typeId= 6  typeName= double      ← JSON 小数才是 Double
//       parsed "0"   : typeId=10  typeName= QString
//     ⇒ 本文件里凡是说"JSON 的 0 是 QVariant(Int)"的注释,【全都是错的】,
//       已经全部改正。测试里的断言也跟着改成了 QMetaType::LongLong。
//
//   ★★ 那个 typeId 碰撞的坑(ValueCoercion.cpp 头部有详述)★★
//     DataType 枚举序号跟 QMetaType id 有一处相等:
//       DataType::UInt16(2) == QMetaType::Int(2)
//     所以【C++ 里写 addMapping(..., 0)】(→ QVariant(Int))会撞上 ——
//     而【JSON 里的 "write_value": 0】(→ QVariant(LongLong),id=4)撞不上。
//     两条路都要修,所以本文件两条都测。
//
// 【本文件钉的四件事】
//   ① 工程 JSON → 联动 mapping → 报警触发 → 变量被写成正确类型的值
//      (走真的 Project::loadFromFile,不手工 addMapping ——
//       手工 add 就等于"测副本",生产里 JSON 那条路还是坏的)
//   ② 写出去的【值本身】编码身份(不是"发了一次",是"发的是 0")
//   ③ 类型适配的【边界】:负数写不进 UInt16(不许静默回绕成 65535)
//   ④ 装不上驱动时【不许】假装写成功(不许撒谎的审计)
//
// 【★ 关于"接线在 hmi.cpp 里"这件事 —— 本文件怎么处理的】
//
//   生产的连接语句以前在 hmi.cpp:729-730:
//       connect(alarmMgr_, &AlarmManager::alarmTriggered,
//               actionMgr_, &ActionManager::onAlarmTriggered);
//   而 hmi.cpp 【不在 hmi_core 里】(见 [[hmi-main-window-has-zero-test-coverage]]),
//   所以这个测试【连不到那行 connect】。
//
//   【为什么不把 connect 抄一份进测试】
//     那正是 BUGFIX-C3 的成因:主画面那套报警表【整张废了】,
//     而 68 个测试全绿 —— 因为测试测的是自己抄的副本。
//     "抄一份 connect" 跟"抄一份实现"是同一类错误:
//     生产里那行 connect 被删了,测试照样绿。
//
//   【本文件的做法 —— 已经把接线抽成共享函数了】
//     ① 那根 connect 抽到 src/alarm/ActionWiring.h 的
//        qwhmi::alarm::bindActionToAlarms()。
//        hmi.cpp 调它,【本测试也调它】—— 同一个函数,不是抄的。
//        (跟 MenuBuilder / AlarmTablePresenter 一个路子。)
//     ② 剩下的最后一条"读源码文本"断言,守的是【hmi.cpp 有没有真的调这个函数】——
//        接线被删了它会变红。(同 tst_MenuBar / tst_Theme 的做法。)
//
//   ★ 所以本文件的行为部分【不再】自己 connect ——
//     它 emit 的是 AlarmManager::alarmTriggered 真实信号,
//     而这条信号怎么走到 ActionManager,走的是生产那条线。
//     接线断了 = 这里的联动不执行 = 用例红。
//
// 【负向对照 —— 见 scripts/negctl_action_write.py(实跑,数字是跑出来的)】
//   M1 拆掉 ActionManager 里的类型适配 → 用例红
//   M2 把适配的"负数拒绝"改成静默回绕 → 用例红
//   M3 拆掉 WriteScheduler 的 dirty 检查 → 用例红
//   M4 抽掉 bindActionToAlarms 的 connect → 用例红
//
// 关联:ActionManager.cpp(onAlarmTriggered)、src/util/ValueCoercion.{h,cpp}、
//      src/alarm/ActionWiring.{h,cpp}、Variable.cpp:35-45(matchesDataType)、
//      Project.cpp:570-586(actions 解析)、WriteScheduler.cpp(onTick / buildWritePayload)、
//      hmi.cpp(调用 bindActionToAlarms 的地方)
// ============================================================================

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ActionManager.h"
#include "Alarm.h"
#include "AlarmManager.h"
#include "Project.h"
#include "ThresholdAlarmChecker.h"
#include "Variable.h"
#include "VariableManager.h"
#include "WriteScheduler.h"
#include "PollScheduler.h"
#include "alarm/ActionWiring.h"
#include "drivers/IPollDriver.h"
#include "drivers/IPlcDriverProvider.h"
#include "util/ValueCoercion.h"

using qwhmi::drivers::IPollDriver;
using qwhmi::drivers::IPlcDriverProvider;
using qwhmi::drivers::RegisterArea;

namespace {

// ============================================================================
// 假 driver:只记"发了什么",不真的连网
// ============================================================================
//
// ★ 为什么不复用 tst_WriteSchedulerMultiPlc 那个 MockWriteDriver
//   那边那个刻意做了"拒收一次就恢复"(refuseNext)之类的行为,
//   是给 2b 的多 PLC 路由用例用的。
//   本文件要测的是【值和类型】,不需要那些花招。
//   但【必须重写一遍】而不是 include 它的头 ——
//   它在一个 .cpp 的匿名 namespace 里,没有 .h 可 include。
//   (这是本项目的老形状:mock 写死在测试文件里,想复用就得复制。
//    本文件接受了这次复制,并在下面写明了为什么这次的复制是安全的:
//    这次的 mock 只有"记下发了什么"这一个职责,没有判据在里面。)

struct SentWrite {
    RegisterArea area = RegisterArea::HoldingReg;
    int          slaveId = 1;
    int          addr = 0;
    QVector<quint16> values;
    quint32      requestId = 0;
};

class MockWriteDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit MockWriteDriver(bool connected = true, QObject* parent = nullptr)
        : IPollDriver(parent), connected_(connected) {}

    void connectTo(const QString&, quint16) override {}
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }
    // ★ 在途数 = 未回的读 + 未回的写。
    //   生产里 ModbusMaster 读和写【共用一个在途槽】
    //   (ModbusMaster.cpp:322 和 :410 都是 `if (!pending_.isEmpty()) 就拒收`),
    //   而 PollScheduler 的单 in-flight 防御正是靠 pendingCount() 判的
    //   (PollScheduler.cpp:359)。
    //   ★ mock 只数写的话,读在途时这个数是 0 → 防御不生效 →
    //     测试会以为"可以并行发多个读请求",而生产里发不出去。
    //     那样测出来的时序跟生产不是一回事。
    int  pendingCount() const override { return inflight_ + pendingReads_.size(); }
    QString protocolName() const override { return QStringLiteral("modbus"); }

    bool sendReadRequest(const ReadRequest& req) override {
        ++readCount_;
        lastRead_ = req;
        // ★ 回包【不】在这里自动发出去 —— 由测试用 deliverReadReply 决定何时回。
        //
        //   【为什么不自动回】
        //     "轮询回包把联动刚写的值冲掉"这个 bug,它的 essence 就是【时序】:
        //     必须在"动作写完、标脏"之后、"写发出去"之前,回包才带着旧值到。
        //     自动回的话,回包会在 onTick 里【立刻】到,动作还没发生 ——
        //     那样这条用例测的就不是那个竞态了。
        //   【为什么这是"同形"而不是"造假"】
        //     生产里 ModbusMaster 也是异步的:sendReadRequest 只是把请求排上队,
        //     回包要等 TCP 往返才由 readFinished 送回来。
        //     这里是同一个形状(请求先发出,回包后到),只是把"何时到"交给测试。
        pendingReads_.append(req.requestId);
        return true;
    }

    // ★ 把回包送给【最早】那个还没回的在途读请求。
    //   返回 false = 一个在途读都没有(测试写错了顺序)。
    bool deliverReadReply(const QVector<quint16>& regs) {
        if (pendingReads_.isEmpty()) return false;
        const quint32 id = pendingReads_.takeFirst();
        emit readFinished(id, regs, true);
        return true;
    }

    bool sendWriteRequest(RegisterArea area, int slaveId, int addr,
                          const QVector<quint16>& values, quint32 requestId) override {
        SentWrite w{area, slaveId, addr, values, requestId};
        sent_.append(w);
        ++inflight_;
        return true;
    }

    void completeWrite(quint32 requestId, bool success = true) {
        if (inflight_ > 0) --inflight_;
        emit writeFinished(requestId, success);
    }

    const QList<SentWrite>& sent() const { return sent_; }
    void clearSent() { sent_.clear(); }

    int  readCount() const { return readCount_; }
    ReadRequest lastRead() const { return lastRead_; }

private:
    bool connected_;
    int  inflight_ = 0;
    QList<SentWrite> sent_;
    // ★ 读回包(2026-10-26 新增)。以前这个 mock 的 sendReadRequest 什么都不做,
    //   所以本文件里【没有任何一条用例能碰到读通路】——
    //   而"轮询回包把联动刚写的值冲掉"这个 bug 就住在读通路上。
    int            readCount_ = 0;
    ReadRequest    lastRead_;
    QList<quint32> pendingReads_;   // 已发出、还没回包的读请求
};

class MockProvider : public IPlcDriverProvider {
public:
    void add(const QString& plcId, IPollDriver* d, int slaveId) {
        drivers_.insert(plcId, d);
        slaves_.insert(plcId, slaveId);
    }
    void remove(const QString& plcId) {
        drivers_.remove(plcId);
        slaves_.remove(plcId);
    }
    IPollDriver* driverFor(const QString& plcId) const override {
        return drivers_.value(plcId, nullptr);
    }
    int slaveIdFor(const QString& plcId) const override {
        return slaves_.value(plcId, 1);
    }
private:
    QHash<QString, IPollDriver*> drivers_;
    QHash<QString, int> slaves_;
};

// ============================================================================
// 工程 JSON:照抄长跑工程的结构(soak_project.json)
// ============================================================================
//
// ★ 为什么【照抄】长跑工程而不是编一个更简单的
//   长跑工程是【现场证据】的来源。如果测试用的 JSON 跟它结构不一样,
//   那测试绿了也不能说明长跑里那条路是通的。
//   特别地:write_value 必须是【不带小数点的 0】——
//   写成 0.0 会变成 QVariant(Double),那是另一条路(而且恰好是能通的),
//   测试就变成了"测了能通的那条路"。
//
//   schema_version 写 "1.0"(字符串)—— 长跑工程就是这样的,
//   顺便也把 migrateV1ToV2 那条路走了一遍(actions 段必须被迁移保留)。

QJsonObject buildSoakLikeProject() {
    QJsonObject root;
    root["schema_version"] = QStringLiteral("1.0");
    root["project_name"]   = QStringLiteral("tst_ActionWrite 工程");

    QJsonObject comm;
    comm["host"] = QStringLiteral("127.0.0.1");
    comm["port"] = 1502;
    comm["protocol"] = QStringLiteral("modbus_tcp");
    root["communications"] = comm;

    // ★ 三个变量跟 scripts/soak_project.json 一模一样(名字/地址/类型/量程)。
    //   少一个变量,测试跟现场就不是同一份工程了 —— 那种"测试结构跟现场不同"
    //   的坑本项目已经吃过(G-11:只测了 Above 从没测 Below)。
    QJsonArray vars;
    {
        QJsonObject temp;
        temp["name"] = QStringLiteral("Temperature");
        temp["address"] = 1;
        temp["data_type"] = QStringLiteral("Int16");
        temp["access"] = QStringLiteral("ReadOnly");
        temp["deadband"] = 0;
        temp["poll_interval_ms"] = 200;
        temp["alwaysPoll"] = true;
        QJsonObject range;
        range["min"] = -100;
        range["max"] = 200;
        temp["range"] = range;
        vars.append(temp);
    }
    {
        QJsonObject pressure;
        pressure["name"] = QStringLiteral("Pressure");
        pressure["address"] = 0;
        pressure["data_type"] = QStringLiteral("Int16");
        pressure["access"] = QStringLiteral("ReadOnly");
        pressure["deadband"] = 0;
        pressure["poll_interval_ms"] = 200;
        pressure["alwaysPoll"] = true;
        QJsonObject range;
        range["min"] = -1000;
        range["max"] = 1000;
        pressure["range"] = range;
        vars.append(pressure);
    }
    {
        QJsonObject speed;
        speed["name"] = QStringLiteral("Speed");
        speed["address"] = 2;
        speed["data_type"] = QStringLiteral("UInt16");   // ★ 无符号 —— 本次 bug 的靶心
        speed["access"] = QStringLiteral("ReadWrite");
        speed["deadband"] = 0;
        speed["poll_interval_ms"] = 200;
        speed["alwaysPoll"] = true;
        QJsonObject range;
        range["min"] = 0;
        range["max"] = 65535;
        speed["range"] = range;
        vars.append(speed);
    }
    root["variables"] = vars;

    QJsonArray alarms;
    {
        QJsonObject a;
        a["name"] = QStringLiteral("温度高");
        a["variable"] = QStringLiteral("Temperature");
        a["direction"] = QStringLiteral("Above");
        a["level"] = QStringLiteral("Critical");
        a["threshold"] = 50;
        a["min_duration_ms"] = 50;
        alarms.append(a);
    }
    root["alarms"] = alarms;

    // ★★★ 联动:write_value 是【不带小数点的 0】★★★
    //   → QJsonValue::toVariant() 给 QVariant(LongLong)(typeId 4)
    //   → 跟 UInt16 要的 QMetaType::UShort(36) 对不上
    //   —— 这就是长跑里那条从没生效的联动。
    QJsonArray actions;
    {
        QJsonObject act;
        act["alarm_name"] = QStringLiteral("温度高");
        act["variable"] = QStringLiteral("Speed");
        act["write_value"] = 0;      // ★ 不是 0.0,不是 "0"
        actions.append(act);
    }
    root["actions"] = actions;

    return root;
}

// 一条报警(给直接喂 ActionManager / AlarmManager 的用例用)
Alarm makeAlarm(const QString& name) {
    Alarm a;
    a.id_    = 1;
    a.name   = name;
    a.level  = AlarmLevel::Critical;
    a.state  = AlarmState::Active;
    return a;
}

// 去掉注释再比对 —— hmi.cpp 的注释里满是 "bindActionToAlarms" 这类名字
// (说明它们为什么被删),不去掉就会把"注释提到了"当成"代码还在",测试永远绿。
// (手法同 tst_MenuBar.cpp:947)
QString codeOnly(const QString& text) {
    QString out;
    out.reserve(text.size());
    bool inBlock = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        QString keep;
        int i = 0;
        while (i < line.size()) {
            if (inBlock) {
                const int end = line.indexOf(QLatin1String("*/"), i);
                if (end < 0) { i = line.size(); break; }
                inBlock = false;
                i = end + 2;
                continue;
            }
            const int b = line.indexOf(QLatin1String("/*"), i);
            const int s = line.indexOf(QLatin1String("//"), i);
            if (s >= 0 && (b < 0 || s < b)) { keep += line.mid(i, s - i); break; }
            if (b >= 0) { keep += line.mid(i, b - i); inBlock = true; i = b + 2; continue; }
            keep += line.mid(i);
            break;
        }
        out += keep;
        out += QLatin1Char('\n');
    }
    return out;
}

int countOf(const QString& text, const QString& needle) {
    int n = 0, i = 0;
    while ((i = text.indexOf(needle, i)) >= 0) { ++n; i += needle.size(); }
    return n;
}

} // namespace

// ============================================================================

class tst_ActionWrite : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ---------- ★★★ 核心:报警 → 联动 → PLC 真的收到写 ----------
    void alarmTriggers_actionWritesZeroToPlc();
    void actionWrite_carriesTheRightAddressAndValue();
    void actionExecuted_signalCarriesTheTypedValue();

    // ---------- 类型适配的边界 ----------
    void negativeValue_cannotEnterUInt16();
    void outOfRangeValue_cannotEnterUInt16();
    void valueAlreadyTyped_passesThroughUnchanged();
    void boolVariable_getsBoolNotNumber();

    // ---------- 失败不许撒谎 ----------
    void missingDriver_doesNotReportSuccess();
    void missingVariable_doesNotCrash();

    // ---------- dirty 闸门 ----------
    void nonDirtyVariable_isNotWritten();

    // ---------- 共享接线本身 ----------
    void bindingTwice_doesNotExecuteTheActionTwice();

    // ---------- ★ BUGFIX-D1:轮询回包不许覆盖"待写"的值 ----------
    void pollReply_doesNotOverwriteTheValueAnActionJustWrote();
    void pollReply_stillFeedsTheAlarmSystem_whileAWriteIsPending();
    void readOnlyVariable_isStillOverwrittenByPollReply();
    void auditRecords_theValueActuallySent();

    // ---------- 工程 JSON 那条路(生产真正用的)----------
    void projectJson_writeValueZero_reachesTheAction();
    void projectJson_actionsSurviveSchemaMigration();
    void projectJson_writeValueType_isNotTheVariableType();

    // ---------- 接线在 hmi.cpp 里 ----------
    void hmiSource_callsTheSharedBinding();

private:
    // 把整个环境搭起来,返回 false 表示搭失败
    bool buildFixture(const QJsonObject& root, QString* errOut);

    QTemporaryDir*       tmp_    = nullptr;
    VariableManager*     vm_     = nullptr;
    AlarmManager*        am_     = nullptr;
    ActionManager*       act_    = nullptr;
    PollScheduler*       poll_   = nullptr;
    WriteScheduler*      ws_     = nullptr;
    MockProvider*        prov_   = nullptr;
    MockWriteDriver*     drv_    = nullptr;
    Project*             proj_   = nullptr;
};

// ---------------------------------------------------------------------------

void tst_ActionWrite::init() {
    tmp_  = new QTemporaryDir;
    vm_   = new VariableManager(this);
    am_   = new AlarmManager(this);
    act_  = new ActionManager(this);
    prov_ = new MockProvider;
    drv_  = new MockWriteDriver(true);
    prov_->add(QStringLiteral("plc1"), drv_, 1);

    ws_ = new WriteScheduler(vm_, prov_, this);
    ws_->hookDriver(QStringLiteral("plc1"), drv_);

    // ★ E #29(2026-09-15):按名查变量需要 varMgr
    act_->setVariableManager(vm_);

    // ★★★ 生产里 alarmMgr_ 和 actionMgr_ 之间的那根线 ★★★
    //   —— 现在它是【共享函数】,hmi.cpp 调的是同一个:
    //       qwhmi::alarm::bindActionToAlarms(alarmMgr_, actionMgr_);
    //   本测试【不再】自己写 connect(那是"抄一份",测的是副本)。
    //   抽出来的理由跟文件头那一节一样:写在 hmi.cpp 里的接线,
    //   测试只能复制一份跑 —— 而"报警触发了但联动不执行"是这个模块上
    //   最严重的失效形态(界面正常、审计正常记"已执行",PLC 一个字节没收到)。
    qwhmi::alarm::bindActionToAlarms(am_, act_);
}

void tst_ActionWrite::cleanup() {
    // ★ 析构顺序(2026-10-26 调整)
    //   poll_ 排【最前】:它是唯一带 100ms 定时器的东西,而定时器还活着的时候
    //   就把 vm_ 删掉,下一拍 onTick 会去遍历【已经删掉的】变量 —— use-after-free。
    //   原来 poll_ 排最后是安全的,因为那时 vm_ 早没了、而 poll_ 自己也快没了;
    //   现在本组用例会 start() 轮询,必须先停它再拆别的。
    if (poll_) { poll_->stop(); delete poll_; poll_ = nullptr; }
    delete proj_;   proj_ = nullptr;
    delete ws_;     ws_    = nullptr;
    delete act_;    act_   = nullptr;
    delete am_;     am_    = nullptr;
    delete vm_;     vm_    = nullptr;
    delete prov_;   prov_  = nullptr;
    delete drv_;    drv_   = nullptr;
    delete tmp_;    tmp_   = nullptr;
}

// 一帧 WriteScheduler。onTick 是 private slot,靠 invokeMethod 驱动
// (同 tst_WriteSchedulerMultiPlc 的做法:确定性,不引入定时器时序噪声)。
void tickWrite(WriteScheduler* ws) {
    QMetaObject::invokeMethod(ws, "onTick", Qt::DirectConnection);
}

// ---------------------------------------------------------------------------
// 核心:报警 → 联动 → PLC 真的收到写
// ---------------------------------------------------------------------------

// ★★★ 这条是本文件的主判据 ★★★
//
//   【它要证明什么】
//     一次报警触发之后,从站【真的】收到了写,写的是 Speed,值是 0。
//
//   【为什么不能只断言"变量值变了"】
//     变量的值是【本地】的。它变了只说明 setValue 被接受了,
//     不说明字节真的发到了 PLC —— 而长跑里的故障恰恰是
//     "本地没变"(Rejected),所以只查本地值这一条,
//     跟现场现象对不上。
//     真正要断的是:报警 → 联动 → 变量 → dirty → WriteScheduler → driver。
//     少一环都不算数,而本文件每一环都在链上。
//
//   【★ 这条用例同时也是"接线有没有断"的判据 ★】
//     它不直接调 act_->onAlarmTriggered(),而是 emit AlarmManager 的
//     真实信号 alarmTriggered —— 让【生产那条线】去把信号送到 ActionManager。
//     接线断了(或者被删了),这里的联动就不执行,用例红。
//     (接线被删的另一种后果,由 hmiSource_callsTheSharedBinding 钉住。)
void tst_ActionWrite::alarmTriggers_actionWritesZeroToPlc() {
    // 一个 UInt16 变量,当前值 100(非 0,这样"变成 0"才有意义)
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    // ★ 联动值用 C++ 字面量 0 → QVariant(Int)(typeId 2)
    //   这跟 JSON 那条路(LongLong,typeId 4)【是两个不同的 typeId】,
    //   但撞的是同一个坑:都写不进 UInt16(要 UShort=36)。
    //   两条路都测 —— 只测一条,就等于只覆盖了半个 bug。
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);

    // 报警触发 —— ★ 走真实信号,让共享接线去送
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    // ★ 联动执行了
    QCOMPARE(exec.count(), 1);

    // ★ 变量值变成了 0,而且是【UInt16 精确类型】——
    //   断言 typeId 不是多余的:值"看起来"是 0 但类型不对的话,
    //   下一环(buildWritePayload)仍可能写错 —— 而本文件就是要钉住那一环。
    QVERIFY2(speed->value().typeId() == QMetaType::UShort,
             "★ 联动写进去的值必须是 UInt16 精确类型(QMetaType::UShort)。"
             "类型不对的话 buildWritePayload 会静默按 toUInt() 取值,"
             "而变量自己的 setValue 已经拒过一次了。");
    QCOMPARE(speed->value().toUInt(), quint16(0));

    // ★ 标脏了 —— 不标脏 WriteScheduler 根本不会看它
    QVERIFY2(speed->isDirty(), "★ 联动必须 markDirty,否则 WriteScheduler 不会发它。");

    // ★ 驱动真的收到了一帧写
    tickWrite(ws_);

    QVERIFY2(!drv_->sent().isEmpty(),
             "★ 从站一帧写都没收到 —— 联动没有走到 PLC。"
             "长跑 24 小时的现场就是这个样子(FC6 计数 = 0)。");
    QCOMPARE(drv_->sent().at(0).addr, 2);
    QCOMPARE(drv_->sent().at(0).values.at(0), quint16(0));
}

// 值本身编码身份:地址、值、功能码区域,一个都不能错。
//
// ★ 为什么单独一条:上面那条已经查了 addr 和 value,
//   但这条【多查了 area】(HoldingReg = 写单寄存器 0x06 那条路),
//   而且它【只发一帧】—— 证明不是"发了 N 帧其中一帧对"。
void tst_ActionWrite::actionWrite_carriesTheRightAddressAndValue() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(1234));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    tickWrite(ws_);

    QVERIFY2(!drv_->sent().isEmpty(), "★ 从站一帧写都没收到。");
    QCOMPARE(drv_->sent().size(), 1);      // ★ 恰好一帧
    const SentWrite& w = drv_->sent().at(0);
    QCOMPARE(w.area, RegisterArea::HoldingReg);   // 16 位 → 保持寄存器(FC6 那条路)
    QCOMPARE(w.slaveId, 1);
    QCOMPARE(w.addr, 2);
    QCOMPARE(w.values.size(), 1);
    QCOMPARE(w.values.at(0), quint16(0));
}

// actionExecuted 信号带出去的值必须是【适配后】的类型对应的字符串。
//
// ★ 为什么值得单独钉:信号是给审计/日志用的。
//   如果它带的是适配【前】的值(比如 "0" vs 别的),审计日志里记的
//   就不是真正写出去的东西 —— 那正是 E #28 想消灭的"审计撒谎"。
void tst_ActionWrite::actionExecuted_signalCarriesTheTypedValue() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(7));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QCOMPARE(exec.count(), 1);
    QCOMPARE(exec.at(0).at(1).toString(), QStringLiteral("Speed"));
    // ★ 值是 0。QVariant(quint16 0).toString() = "0"
    QCOMPARE(exec.at(0).at(2).toString(), QStringLiteral("0"));
}

// ---------------------------------------------------------------------------
// 类型适配的边界
// ---------------------------------------------------------------------------

// ★★ 负数写进 UInt16:必须【拒掉】,不许静默回绕成 65535 ★★
//
//   为什么这条最重要:回绕成 65535 的话,现场表现是
//   "我明明要停机(写 0),结果设备跑到了最高速"。
//   那比直接报错危险得多 —— 报错至少有人看得见。
void tst_ActionWrite::negativeValue_cannotEnterUInt16() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), -1);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    // ★ 联动【没执行】—— 负数是非法值,不能当作 0 继续写
    QCOMPARE(exec.count(), 0);
    // ★ 变量值没变(还是 100),绝不能变成 65535
    QCOMPARE(speed->value().toUInt(), quint16(100));
    // ★ 也没标脏 —— 不该有一次写出去
    QVERIFY(!speed->isDirty());

    tickWrite(ws_);
    QVERIFY2(drv_->sent().isEmpty(),
             "★ 负数被拒之后,绝不能有写发出去。"
             "回绕成 65535 会让『停机』变成『全速运行』。");
}

// 超出 65535 的值同样必须拒掉。
void tst_ActionWrite::outOfRangeValue_cannotEnterUInt16() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 70000);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QCOMPARE(exec.count(), 0);
    QCOMPARE(speed->value().toUInt(), quint16(100));
    QVERIFY(!speed->isDirty());
}

// 已经是精确类型的值 → 原样通过(不重复转换)。
//
// ★ 这条守的是"别把已经对的值改坏"。
//   适配器里有一条"已经是对的类型就原样返回"的短路 ——
//   如果那条短路写错了(比如按 DataType 序号比 typeId),
//   float→float 会先转 double 再转回 float,精度就丢了。
//
// ★ 而这条同时守着一个【曾经真的存在】的 bug:
//   旧的短路写的是 `v.typeId() == static_cast<int>(t)` ——
//   拿 QMetaType 的 id 跟 DataType 的枚举序号比。
//   DataType::UInt16(2) 恰好 == QMetaType::Int(2),
//   于是"给 UInt16 变量传 QVariant(Int)"会被误判成"已经是对的类型",
//   原样放行 —— 而 Variable::setValue 要的是 UShort(36) → 还是 Rejected。
//   适配【静默地没起作用】。
//   (ValueCoercion.cpp 头部有完整的推导。)
void tst_ActionWrite::valueAlreadyTyped_passesThroughUnchanged() {
    auto* f = new Variable(QStringLiteral("Temp"), 3,
                           Variable::DataType::Float,
                           Variable::Access::ReadWrite, vm_);
    vm_->addVariable(f);

    const float exact = 12.3456789f;   // 精度敏感的值
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Temp"),
                     QVariant::fromValue<float>(exact));

    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QVERIFY(f->value().typeId() == QMetaType::Float);
    // ★ 逐位相等:float→double→float 走一圈会掉精度
    QVERIFY2(qFuzzyCompare(f->value().toFloat(), exact) &&
             qAbs(f->value().toFloat() - exact) < 1e-6f,
             "★ 已经是 float 的值必须原样通过(精度不能掉)。");
}

// Bool 变量拿到的是 bool,不是数字。
void tst_ActionWrite::boolVariable_getsBoolNotNumber() {
    auto* flag = new Variable(QStringLiteral("Run"), 4,
                              Variable::DataType::Bool,
                              Variable::Access::ReadWrite, vm_);
    vm_->addVariable(flag);

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Run"), 1);

    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QVERIFY(flag->value().typeId() == QMetaType::Bool);
    QCOMPARE(flag->value().toBool(), true);
}

// ---------------------------------------------------------------------------
// 失败不许撒谎
// ---------------------------------------------------------------------------

// 变量所属的 PLC 没有 driver → 字节【不许】出去。
//
// ★ 这条守的是 E #28 那一类:"审计说写了,PLC 实际啥也没收到"。
//   联动的 setValue 成功只说明【本地值】写上了;
//   PLC 那边写不写得出去是 WriteScheduler 的事。
//   这条钉的是"没有 driver 就没有字节离站"这一环。
void tst_ActionWrite::missingDriver_doesNotReportSuccess() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    // 这台 PLC 没有 driver
    prov_->remove(QStringLiteral("plc1"));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    // 联动本身执行了(本地值确实写了)—— 这不是谎:
    //   "联动执行了"说的是本地动作,"PLC 收到了"是另一件事,后者由本条断言。
    QCOMPARE(exec.count(), 1);
    // 但字节没出去
    tickWrite(ws_);
    QVERIFY2(drv_->sent().isEmpty(),
             "没有 driver 就不会有写发出去 —— 这条只是确认链路没偷偷发东西。");
}

// 变量被删了(工程换了)→ 联动不许崩,也不许报告成功。
void tst_ActionWrite::missingVariable_doesNotCrash() {
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("不存在的变量"), 0);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);

    // ★ 不许崩 —— 这条用例的价值就在"跑完不炸"
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QCOMPARE(exec.count(), 0);   // 变量不存在 → 不许说"执行了"
}

// ★ 没被标脏的变量,WriteScheduler 【不许】发出去 ★
//
//   为什么值得单独钉:WriteScheduler 每 100ms 扫一遍所有变量,
//   唯一的筛选条件就是 isDirty()。拆掉它(负向对照 M3)的后果是
//   【所有 ReadWrite 变量每 100ms 被写一次】——
//   现场表现是"PLC 的寄存器被莫名其妙地反复改写",
//   而日志里"发写"那一行会亮成一片。
//
//   ★ 这条【不】跟联动有关:它钉的是"联动执行了才标脏"这个前提的另一半 ——
//     标脏了就该发(主判据那条),没标脏就不该发(本条)。
//   两条合起来才是完整的闸门。
void tst_ActionWrite::nonDirtyVariable_isNotWritten() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));
    // ★ 没有 markDirty() —— 这个变量【没】被任何动作改过

    tickWrite(ws_);

    QVERIFY2(drv_->sent().isEmpty(),
             "★ 没被标脏的变量被写出去了。"
             "WriteScheduler 唯一的筛选条件就是 isDirty();拆掉它 = "
             "所有 ReadWrite 变量每 100ms 被写一次。");
}

// ---------------------------------------------------------------------------
// 共享接线本身
// ---------------------------------------------------------------------------

// 重复绑定【不许】让一次报警触发两次联动。
//
// ★ 为什么值得单独钉:bindActionToAlarms 用了 Qt::UniqueConnection,
//   目的就是"多调一次是安全的"。
//   而如果哪天有人把 UniqueConnection 去掉,后果是:
//     · "停机"动作执行两次
//     · 审计日志同一个动作记两行
//   看起来像 bug,查起来极费时间 —— 而成因只是有人多调了一次本函数。
//
//   ★ 判据:多绑一次之后,一次报警只该有一次联动。
void tst_ActionWrite::bindingTwice_doesNotExecuteTheActionTwice() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    // ★ 再绑一次(init() 里已经绑过一次)
    qwhmi::alarm::bindActionToAlarms(am_, act_);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QVERIFY2(exec.count() == 1,
             "★ 重复绑定之后,一次报警只该触发【一次】联动。"
             "触发两次 = 停机动作执行两次 + 审计重复记两行。");
}

// ---------------------------------------------------------------------------
// ★ BUGFIX-D1(2026-10-26):轮询回包不许覆盖"待写"的值
// ---------------------------------------------------------------------------
//
// 【这个 bug 长什么样】
//
//   联动的写路径是:setValue(新值) → markDirty → 等 WriteScheduler 下一拍
//   (100ms)把值发出去。而 PollScheduler 每 100ms 也轮询【同一个变量】——
//   onTick 的筛选条件里【没有 isDirty】,所以 ReadWrite 变量同时在
//   读列表和写列表里。
//
//   于是存在这个窗口:
//     ① 动作把 Speed 设成 0 并标脏(界面显示 0)
//     ② 轮询回包到,带着【PLC 里的旧值 100】
//     ③ 原来这里无条件 v->setValue(100) → 刚写的 0 被冲掉
//     ④ WriteScheduler 下一拍【现取 v->value()】→ 发出去的是 100
//
//   现场表现:"界面显示停机成功、审计记着写了 0、PLC 上跑的还是老转速"。
//   而长跑工程里那条联动正是"温度高 → 速度写 0"(停机)。
//
//   ★ 为什么这不是"读回的新值更可信"
//     读回的是【PLC 里的值】,我们要保护的是【我们刚要求 PLC 变成的值】。
//     在写真正发出去之前,PLC 里的值【本来就】还是旧的 ——
//     读回来覆盖它是【必然】的,不是意外。
//     写成功之后 PLC 里的值就等于我们写的值了,下一帧读回来自然一致。
//
// 【★ 为什么这条 bug 以前没被测到 —— 而这是本组用例存在的全部理由 ★】
//
//   本文件原来的 mock driver 的 sendReadRequest 【什么都不做】:
//     bool sendReadRequest(const ReadRequest&) override { return true; }
//   —— 也就是说本文件里【没有任何一条用例能碰到读通路】。
//   而"轮询回包把联动刚写的值冲掉"这个 bug 就住在读通路上。
//   换句话说:这个 bug 从来没被任何测试【执行过】,更别说被抓住。
//
//   ★ 而且它不是"碰巧没人写",是【结构性看不见】:
//     tst_PlcMultiServerE2E.cpp:570-574 那条写路径用例【故意不启动
//     PollScheduler】,注释里写着:
//       "读会 setValue 覆盖本地值,而 WriteScheduler 在 onTick 里现取
//        v->value(),两者一起跑就有真实竞态 …… 那会让本用例随机红,
//        而红因是竞态不是 bug。写路径必须单独隔离,否则测的是时序而不是路由。"
//   —— 那条注释【把 bug 当成了测试的障碍绕过去】,而不是去修它。
//   本组用例就是去把这个"绕过"变成"修好"。
//
// 【负向对照 —— M5,见 scripts/negctl_action_write.py】
//   拆掉 PollScheduler 里的 isDirty 守卫 → 用例必须变红。
//   ★ 拆的方式有讲究:只把 `v->isDirty() &&` 去掉的话,那会让【只读变量】
//     也被保护起来 —— 而只读变量不会被写出去,保护它等于让它的值永远
//     不刷新(一个新引入的故障)。
//     所以 M5 是把整个守卫换成"什么都不保护"(回到改动前的行为),
//     而"只读变量仍然被覆盖"那条用例守着 access() 那一半。

// ★ 主判据:轮询回包【不许】冲掉联动刚写的值,而且发出去的还是我们写的那个值。
void tst_ActionWrite::pollReply_doesNotOverwriteTheValueAnActionJustWrote() {
    // 一个 UInt16 变量,当前值 100(PLC 里的旧速度)
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    // ★ 把读通路也装上 —— 这一组用例的全部意义就在这条线上
    //   (以前本文件的 mock 读请求什么都不做,读通路从来没被测到)
    poll_ = new PollScheduler(vm_, drv_, this);
    poll_->setAllActive();
    poll_->start();

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    // ① 报警触发 → 联动把 Speed 设成 0 并标脏
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));
    QVERIFY2(speed->isDirty(), "★ 联动必须标脏。");
    QCOMPARE(speed->value().toUInt(), quint16(0));

    // ② 轮询真的发了读请求
    //   (onTick 是 private slot,靠 invokeMethod 驱动,同 tickWrite 的做法)
    QMetaObject::invokeMethod(poll_, "onTick", Qt::DirectConnection);
    QVERIFY2(drv_->readCount() > 0, "★ 轮询没发出读请求 —— 读通路根本没跑起来,"
             "那这条用例就测不到任何东西。");
    QCOMPARE(drv_->lastRead().addr, 2);

    // ③ ★ 回包带着【PLC 里的旧值 100】到 —— 这正是竞态的那一刻
    //   PLC 上还是 100(我们要求写 0,但那笔写还没发出去)
    QVERIFY2(drv_->deliverReadReply({100}),
             "★ 没法把读回包送出去 —— 一个在途读都没有。");

    // ★★★ 本用例的正文:本地值必须还是 0 ★★★
    QVERIFY2(speed->value().toUInt() == quint16(0),
             "★ 轮询回包把联动刚写的值冲掉了。写挂起的那 100ms 里,"
             "一次读回包就足以把'停机(写 0)'变回'老速度(100)',"
             "而接下来发出去的【就是 100】——"
             "界面显示停机成功,PLC 上跑的是老转速。");

    // ★ 而且质量必须是 Good —— 链路是通的,只是值被保护住了
    QVERIFY2(speed->quality() == Variable::Quality::Good,
             "★ 读回包之后质量必须是 Good。跳过 setValue 不该连累 setQuality —— "
             "quality 说的是'链路通不通',不是'这个值是不是刚读回来的'。");

    // ④ ★ 真正发出去的那一帧,值必须是 0
    //   (WriteScheduler::onTick 现取 v->value(),所以第 ③ 步保住了值,
    //    这里就一定是对的 —— 两步合起来才是一条完整的链)
    tickWrite(ws_);
    QVERIFY2(!drv_->sent().isEmpty(),
             "★ 从站一帧写都没收到 —— 联动没有走到 PLC。");
    QCOMPARE(drv_->sent().at(0).addr, 2);
    QVERIFY2(drv_->sent().at(0).values.at(0) == quint16(0),
             "★ 发出去的不是 0。WriteScheduler 是在 onTick 里现取 v->value() 的,"
             "而那个值刚被读回包冲成了 100 —— 所以【写出去的也是 100】。");

    // ⑤ 写成功之后,dirty 清掉,下一帧读回来(现在 PLC 上确实是 0)自然一致
    drv_->completeWrite(drv_->sent().at(0).requestId, true);
    QVERIFY2(!speed->isDirty(), "★ 写成功后必须清 dirty。");
    QCOMPARE(speed->value().toUInt(), quint16(0));
}

// ★ 反向判据:【只】跳过 setValue,别的照做 —— 特别是报警系统。
//
//   这条守的是"修这个 bug 的时候别把报警系统一起打死了"。
//
//   ThresholdAlarmChecker 整个跑在 PollScheduler::rawRead 上
//   (见 ThresholdAlarmChecker.cpp:135)—— 它看的是【原始寄存器值】,
//   不是 v->value()。而且 onRawRead 头一行就查 quality == Good。
//
//   所以如果有人图省事,把整个分支跳过(而不是只跳过 setValue),
//   后果是:这个变量的报警【彻底不再触发】——
//   而报警正是联动的触发源,于是"温度高 → 停机"这条联动
//   会在【联动的值被保护住】的同时【再也不触发】。
//   两条一起坏,现场表现是"什么都不会发生",比原来那个 bug 还难查。
//
// ★★ 为什么报警要挂在【那个正在写的变量】上,而不是一个别的变量 ★★
//   (这一条是写第一版时踩出来的,记在这儿)
//     第一版把报警挂在 Temperature(只读)上,而 dirty 的是 Speed。
//     结果"把整个分支跳过"这个变异【测不出来】——
//     因为跳过的只是 Speed 的那一轮,而 Temperature 的 rawRead 照发,
//     报警照样触发。测试绿,变异没被抓住。
//   根因:那条用例测的是"别的变量还正常",不是"这个变量还正常"。
//   而"这个变量"才是被守卫影响的那个。
//
//   改成:报警就挂在 dirty 的那个变量上(ReadWrite 变量也可以配报警,
//   工程上很常见 —— 比如"控制字超过阈值就报警")。
//   于是"跳过整个分支"这个变异会:quality 停在 Bad + rawRead 不发
//   → 报警【不触发】→ 用例红。守住了。
void tst_ActionWrite::pollReply_stillFeedsTheAlarmSystem_whileAWriteIsPending() {
    // ★ 报警 + 写,都在【同一个】变量上。
    //   Speed:ReadWrite、UInt16、现在标脏(有一笔写挂着),
    //   报警阈值 50(读回 100 就超阈值)。
    //   ★ 故意【不】预置 quality=Good ——
    //     让 setQuality(Good) 必须真的被执行,报警才有资格触发
    //     (ThresholdAlarmChecker::onRawRead 头一行就查 quality == Good)。
    //     预置了 Good 的话,"跳过 setQuality"这个变异就测不出来了。
    //   ★ 初值给 20(不是回包里的 100)——
    //     这样"值被守住了"这条断言才有内容:如果守卫没生效,
    //     setValue(100) 会把 20 冲成 100,断言立刻红。
    //     初值若也设 100,冲不冲都还是 100,断言就是【空的】。
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(20));
    speed->markDirty();

    poll_ = new PollScheduler(vm_, drv_, this);
    poll_->setAllActive();
    poll_->start();

    // ★ 报警 checker 挂在 poll_ 上(跟生产一样:checker 订阅 rawRead)
    CheckerConfig cfg;
    cfg.level         = AlarmLevel::Critical;
    cfg.direction     = Direction::Above;
    cfg.threshold     = 50;
    cfg.minDurationMs = 50;
    auto* checker = new ThresholdAlarmChecker(QStringLiteral("转速高"), speed, poll_, cfg);

    QSignalSpy trig(checker, &AlarmChecker::triggered);

    // 轮询发请求(Speed 在地址 2,单独一帧)
    QMetaObject::invokeMethod(poll_, "onTick", Qt::DirectConnection);
    QVERIFY2(drv_->readCount() > 0, "★ 轮询没发出读请求。");
    QCOMPARE(drv_->readCount(), 1);
    QCOMPARE(drv_->lastRead().addr, 2);

    // ★ 回包:读到 100(超阈值 50)
    QVERIFY2(drv_->deliverReadReply({100}), "★ 送不出读回包。");

    // ★ 质量必须被设成 Good —— 哪怕值被保护住了
    QVERIFY2(speed->quality() == Variable::Quality::Good,
             "★ 质量没被设成 Good。守卫只该跳过 setValue —— "
             "quality 说的是'链路通不通',不是'这个值是不是刚读回来的'。"
             "而 ThresholdAlarmChecker::onRawRead 头一行就查 quality == Good,"
             "质量不 Good = 报警对这个变量彻底哑掉。");

    // ★ 报警必须触发 —— 即使这个变量此刻有写挂着
    //   minDurationMs = 50,所以要等一下
    QTRY_VERIFY_WITH_TIMEOUT(trig.count() == 1, 2000);

    QVERIFY2(trig.count() == 1,
             "★ 报警没触发 —— 读回包被整个跳过了(rawRead 没发)。"
             "报警系统整个跑在 rawRead 上,跳过它 = 报警彻底哑掉,"
             "而报警正是联动的触发源。"
             "修'读回不许覆盖待写的值'的时候,只能跳过 setValue,"
             "不能跳过 setQuality / rawRead / nextPollAt。");

    // ★ 而且这个变量的写【还是】被保护着 —— 两条判据不是互斥的
    //   初值 20,回包 100。如果守卫没生效,这里会是 100。
    QCOMPARE(speed->value().toUInt(), quint16(20));
}

// ★ 只读变量【照常】被读回包覆盖。
//
//   为什么这条重要:守卫的条件里带了 access() == ReadWrite,
//   而这一半是有理由的 ——
//   ActionManager 不检查 access,它对任何变量都 setValue + markDirty;
//   而 WriteScheduler::onTick 只发 ReadWrite 的变量
//   (见 WriteScheduler.cpp:150)。
//   所以一个只读变量如果被联动标了脏,那笔写【永远发不出去】——
//   这时候如果还保护它,那个值会【永久停在联动写的值上】,
//   跟 PLC 里的真实值永远对不上,画面上是另一个新的"值不刷新"故障。
//
//   ★ 所以这条是【给 access() 那一半的负向对照】:
//     守卫太宽(只判 isDirty 不判 access)→ 这条红。
void tst_ActionWrite::readOnlyVariable_isStillOverwrittenByPollReply() {
    auto* ro = new Variable(QStringLiteral("ReadOnlySpeed"), 2,
                             Variable::DataType::UInt16,
                             Variable::Access::ReadOnly, vm_);
    vm_->addVariable(ro);
    ro->setValue(QVariant::fromValue<quint16>(100));

    poll_ = new PollScheduler(vm_, drv_, this);
    poll_->setAllActive();
    poll_->start();

    // ★ 模拟"联动配了一个只读变量"的后果:它被标脏了,
    //   但 WriteScheduler 永远不会发它(只发 ReadWrite)
    ro->markDirty();

    QMetaObject::invokeMethod(poll_, "onTick", Qt::DirectConnection);
    QVERIFY2(drv_->readCount() > 0, "★ 轮询没发出读请求。");
    QVERIFY2(drv_->deliverReadReply({55}), "★ 送不出读回包。");

    // ★ 值必须被覆盖成 55
    QVERIFY2(ro->value().toUInt() == quint16(55),
             "★ 只读变量的值被挡住了,永远停在旧值上。"
             "只读变量不会被写出去(WriteScheduler 只发 ReadWrite),"
             "所以没有'待写的值'要保护 —— "
             "保护它等于引入一个新的'这个值永远不刷新'故障。");
}

// ★ 审计必须记【实际发出去的那个值】。
//
//   以前 e.newValue 是在 reply 时刻现取 v->value()。
//   而写挂起的那 100ms 里,本地值完全可能变掉 ——
//   于是审计记的是【另一个值】。
//
//   后果不是"显示难看",是【事故复盘时唯一的证据是假的】:
//     审计写着"Speed 写了 100",而 PLC 上跑的是 0(停机没生效)。
//     事后一查日志,结论会是"PLC 收到了 100 就没执行"——
//     方向整个反了,而这条假证据正是当时唯一能看的东西。
//
//   ★ 这跟 pendingPlcId_ 是同一个教训:
//     那边记 plcId 是因为"变量在途时可能被改归属";
//     这边记值是因为"变量在途时本地值可能被改"。
//
// ★★ 这条用例的"陷阱"——第一版它是【假绿】的 ★★
//   第一版只依赖"轮询回包把值冲掉"来制造差异。
//   但那个冲掉已经被 PollScheduler 的守卫挡住了 ——
//   于是"reply 时刻现取 v->value()"和"发出时记下"读到的【都是 0】,
//   把审计的修复撤掉,这条用例【照样绿】。
//   —— 一条假绿的测试比没有测试更坏:它让人以为这条路被守住了。
//
//   所以这里【另开一条不改本地值的路】来制造差异:
//     写【已经发出去了】(发出的是 0),
//     然后【在 reply 回来之前】把本地值改成 77 ——
//     模拟"用户在写挂起的那 100ms 里又动了一次这个控件"。
//     那样:
//       · 发出时记的值 = "0"   ← 真的发到 PLC 上的是这个
//       · reply 时刻现取 = "77" ← 那是本地后来变的,不是发出去的
//     两个值【必然】不同,审计取哪个就一目了然。
//
//   ★ 顺带说一句:这个"在途时本地值又变了"的场景,守卫【挡不住它】——
//     守卫只挡【读回】这一条路,挡不住【用户又改了一次】。
//     所以审计记"发出时的值"不是多余的:它记的是"那一帧里到底是什么",
//     而本地值在那一刻可能已经不代表任何已发出的东西了。
void tst_ActionWrite::auditRecords_theValueActuallySent() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                                Variable::DataType::UInt16,
                                Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(100));

    poll_ = new PollScheduler(vm_, drv_, this);
    poll_->setAllActive();
    poll_->start();

    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);

    // ① 联动:Speed = 0,标脏
    emit am_->alarmTriggered(makeAlarm(QStringLiteral("温度高")));
    QCOMPARE(speed->value().toUInt(), quint16(0));

    // ② 写发出去了 —— 发出的是 0
    tickWrite(ws_);
    QVERIFY2(!drv_->sent().isEmpty(), "★ 从站一帧写都没收到。");
    QCOMPARE(drv_->sent().at(0).values.at(0), quint16(0));

    // ③ ★★ 在 reply 回来【之前】,本地值又变了 —— 模拟"用户在写挂起期间又动了一次"
    //   这一步是本用例的【全部要害】:
    //     不做这一步,"发出时的值"和"reply 时刻的值"都是 0,断言没有鉴别力。
    //   做了这一步,两个值必然不同 —— 审计取哪个,测试立刻能分辨。
    speed->setValue(QVariant::fromValue<quint16>(77));

    // ④ reply 回来 —— 审计条目必须记 0(实际发出去的那个值),不是 77
    drv_->completeWrite(drv_->sent().at(0).requestId, true);

    QCOMPARE(ws_->auditLog().size(), 1);
    QVERIFY2(ws_->auditLog().at(0).newValue == QStringLiteral("0"),
             "★ 审计记的值不是实际发出去的那个。"
             "审计以前是在 reply 时刻现取 v->value() —— "
             "本地值在写挂起期间完全可能已经变了,"
             "于是复盘时唯一的证据写着别的值,方向整个反了。"
             "注意:本用例【没有】靠'轮询回包冲掉值'来制造差异 —— "
             "那条路已经被 PollScheduler 的守卫挡住了,靠它的话本用例是假绿的。");
    QVERIFY2(ws_->auditLog().at(0).success, "★ 写成功了,审计必须记成功。");
}


// ---------------------------------------------------------------------------
// 工程 JSON 那条路(生产真正用的)
// ---------------------------------------------------------------------------

// ★ 走真的 Project::loadFromFile,把工程 JSON 变成联动 mapping。
//
//   【为什么必须走这条路】
//     手工 addMapping 的测试只证明"ActionManager 能写",
//     不证明"工程文件里的联动配置能变成 ActionManager 的 mapping"。
//     而长跑里的 bug 恰恰在【这一段】:
//       Project.cpp:577  QVariant writeValue = acObj["write_value"].toVariant();
//     这行给的是 QVariant(LongLong) —— 手工 addMapping 给的可能是
//     QVariant(Int)。看起来"差不多",但【JSON 那条路还有 schema 迁移、
//     变量解析、名字查表】一堆环节,少一个环节测到就等于没测。
bool tst_ActionWrite::buildFixture(const QJsonObject& root, QString* errOut) {
    const QString path = tmp_->filePath(QStringLiteral("proj.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errOut) *errOut = QStringLiteral("写临时工程文件失败");
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();

    // 变量管理器和联动管理器的引用由 loadFromFile 自己填(它往 varMgr_ 里建变量)
    poll_ = new PollScheduler(vm_, nullptr, this);
    proj_ = new Project(vm_, am_, act_, poll_, nullptr, this);

    QString loadErr;
    connect(proj_, &Project::loadError, this, [&](const QString& r) { loadErr = r; });

    if (!proj_->loadFromFile(path)) {
        if (errOut) *errOut = QStringLiteral("loadFromFile 失败: ") + loadErr;
        return false;
    }
    return true;
}

// ★ 真实工程 JSON → 联动 mapping 齐了 → 报警 → PLC 收到 0。
//
// ★ 这是本文件【最长的一条链】,每一环都是生产类:
//     PollScheduler::rawRead(模拟一次从站回包)
//       → ThresholdAlarmChecker(超限 + 持续 50ms → triggered)
//       → AlarmManager::onCheckerTriggered(活动报警表 + alarmTriggered 信号)
//       → ActionManager::onAlarmTriggered(按名查变量 + 类型适配 + markDirty)
//       → WriteScheduler::onTick(挑 dirty 变量 + 编 payload)
//       → IPollDriver::sendWriteRequest(帧真的发出去)
//   少一环都不算数 —— 长跑里断的正是"类型适配"那一环。
void tst_ActionWrite::projectJson_writeValueZero_reachesTheAction() {
    QString err;
    QVERIFY2(buildFixture(buildSoakLikeProject(), &err), qPrintable(err));

    // ★ 工程里的变量建出来了
    Variable* speed = vm_->findVariable(QStringLiteral("Speed"));
    QVERIFY2(speed != nullptr, "★ 工程里的 Speed 变量没建出来。");
    QCOMPARE(speed->dataType(), Variable::DataType::UInt16);

    // ★ 联动 mapping 也建出来了
    QVERIFY2(act_->mappings().size() == 1,
             "★ 工程里的 actions 段没变成联动 mapping —— "
             "工程文件配了联动,程序却不知道有这回事。");
    QCOMPARE(act_->mappings().at(0).alarmName, QStringLiteral("温度高"));
    QCOMPARE(act_->mappings().at(0).variableName, QStringLiteral("Speed"));

    // ★ 走真的报警链路:ThresholdAlarmChecker → AlarmManager → ActionManager
    //   报警【怎么触发的】要真的走 ThresholdAlarmChecker(生产类),
    //   这样"从 PLC 读数到报警"这一段也是真的。
    Variable* temp = vm_->findVariable(QStringLiteral("Temperature"));
    QVERIFY(temp != nullptr);
    temp->setQuality(Variable::Quality::Good);   // ★ checker 头一行就查 quality

    QSignalSpy trig(am_, &AlarmManager::alarmTriggered);
    QSignalSpy exec(act_, &ActionManager::actionExecuted);

    // 找那条 "温度高" 的 checker(它是 loadFromFile 建的)
    ThresholdAlarmChecker* checker = nullptr;
    for (AlarmChecker* c : am_->checkers()) {
        if (c->name() == QStringLiteral("温度高")) {
            checker = dynamic_cast<ThresholdAlarmChecker*>(c);
            break;
        }
    }
    QVERIFY2(checker != nullptr, "★ 工程里的报警 checker 没建出来。");

    // ★ 喂 60(> 阈值 50)—— rawRead 是 public 信号,直接 emit,
    //   跟 tst_ThresholdAlarmChecker 一样,不需要真总线。
    emit poll_->rawRead(temp, static_cast<quint16>(60));
    QTest::qWait(200);   // min_duration_ms = 50,留足余量

    QVERIFY2(trig.count() == 1, "★ 报警没触发 —— 上游链路有问题,先查那个。");
    QVERIFY2(exec.count() == 1, "★ 报警触发了但联动没执行 —— 这正是长跑里的故障。");

    // ★ 变量被写成 0(UInt16 精确类型)
    QVERIFY(speed->value().typeId() == QMetaType::UShort);
    QCOMPARE(speed->value().toUInt(), quint16(0));

    // ★ WriteScheduler 把它发出去
    tickWrite(ws_);
    QVERIFY2(!drv_->sent().isEmpty(),
             "★ 从站没收到写 —— 整条『读数→报警→联动→PLC』链路断在最后一环。");
    QCOMPARE(drv_->sent().at(0).addr, 2);
    QCOMPARE(drv_->sent().at(0).values.at(0), quint16(0));
}

// schema 迁移不许把 actions 段弄丢。
//
// ★ 为什么单独钉:长跑工程是 schema_version "1.0",
//   加载时会走 migrateV1ToV2。如果迁移把 actions 段弄丢了,
//   联动就【整个消失】—— 而且不报任何错。
void tst_ActionWrite::projectJson_actionsSurviveSchemaMigration() {
    QString err;
    QVERIFY2(buildFixture(buildSoakLikeProject(), &err), qPrintable(err));

    // 迁移后仍应有一条联动
    QVERIFY2(act_->mappings().size() == 1,
             "★ schema 迁移把 actions 段弄丢了 —— "
             "老工程加载后联动整个消失,而且不报任何错。");
    QCOMPARE(act_->mappings().at(0).variableName, QStringLiteral("Speed"));
    // ★ write_value 也得保住(值是 0,类型见下一条用例)
    QCOMPARE(act_->mappings().at(0).value.toInt(), 0);
}

// ★★ 工程 JSON 里的 write_value,类型【本来就不是】变量的类型 ★★
//
//   这条不是"测功能",是【测前提】——
//   它保证上面那条用例真的走的是"需要类型适配"的那条路。
//
//   【实测的 Qt 事实】(探针跑真 Qt 6.8.3 出来的,不是推断):
//     JSON 整数 → QVariant(LongLong),typeId = 4
//     JSON 小数 → QVariant(Double),  typeId = 6
//     JSON 字符串 → QVariant(QString), typeId = 10
//   而 UInt16 变量要的是 QMetaType::UShort,typeId = 36。
//   4 ≠ 36 ⇒ 【类型不匹配】,必须靠适配才写得进去。
//
//   ★ 为什么这条不能省:
//     如果哪天有人把 Project.cpp:577 改成在【解析时】就转好类型
//     (比如 acObj["write_value"].toInt() 然后 fromValue<quint16>),
//     那类型是"对"了,功能测试照样绿 —— 但适配器那条路就没人测了。
//     而适配器正是本次修的东西(负数拒绝、超范围拒绝……)。
//     ★ 而且"解析时转"这个改法本身【是错的】:
//       解析的时候还不知道变量是什么类型,转不了 ——
//       同一个 write_value 要写进 UInt16 还是 Int32,取决于变量。
void tst_ActionWrite::projectJson_writeValueType_isNotTheVariableType() {
    QString err;
    QVERIFY2(buildFixture(buildSoakLikeProject(), &err), qPrintable(err));

    QVERIFY2(!act_->mappings().isEmpty(), "★ 工程里的联动没建出来。");
    const QVariant v = act_->mappings().at(0).value;

    // ★ 实测:JSON 整数给的是 QVariant(LongLong)
    QVERIFY2(v.typeId() == int(QMetaType::LongLong),
             "★ 前提变了:JSON 整数不再是 QVariant(LongLong)。"
             "如果 Qt 改了行为,下面那条\"类型不匹配\"的断言就得跟着重写 —— "
             "但【别】直接把它改成一样,那会让类型适配这条路【没人测】。");
    // ★ 关键:它【不是】变量要的类型
    QVERIFY2(v.typeId() != int(QMetaType::UShort),
             "★ 前提变了:JSON 的 write_value 已经是 UInt16 精确类型了。"
             "那样的话类型适配这条路径就【没被测到】,"
             "而那正是这次修的东西。");
}

// ---------------------------------------------------------------------------
// 接线在 hmi.cpp 里
// ---------------------------------------------------------------------------

// ★ hmi.cpp 到底有没有调那个共享的接线函数?
//
//   【为什么需要这条】
//     本文件的行为部分调的是【共享函数】qwhmi::alarm::bindActionToAlarms,
//     所以"函数本身坏了"会红。但还有一个洞:
//     【hmi.cpp 根本没调这个函数】—— 那生产环境里联动就永远不执行,
//     而本文件的用例【照样全绿】(因为本文件自己调了)。
//     这条用例读 hmi.cpp 源码,把那根洞堵上。
//
//   【诚实的边界】
//     它抓得住"hmi.cpp 不调了 / 调错了参数";
//     抓不住"hmi.cpp 调了但 alarmMgr_ 是 nullptr"(那种情况构造函数顺序就保证了)。
//
//   ★ 为什么要先去掉注释:
//     hmi.cpp 里那段解释为什么抽共享函数的注释里,就写着
//     "bindActionToAlarms" 这个名字 —— 不去注释就会把
//     "注释提到了"当成"代码还在",测试永远绿。
//     (手法同 tst_MenuBar.cpp:947。)
void tst_ActionWrite::hmiSource_callsTheSharedBinding() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING) + QStringLiteral("/hmi.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp: ") + path));
    const QString code = codeOnly(QString::fromUtf8(f.readAll()));

    // ★ hmi.cpp 必须真的调那个共享函数
    QVERIFY2(code.contains(QStringLiteral("bindActionToAlarms(alarmMgr_, actionMgr_)")),
             "★ hmi.cpp 里没有那一句 qwhmi::alarm::bindActionToAlarms(alarmMgr_, actionMgr_)。"
             "生产环境里『报警→联动』就再也不会触发,而测试还是全绿 —— "
             "这正是 BUGFIX-C3 的形状(主画面报警表整张废了,68 个测试全绿)。");

    // ★ 而且【只能】有一处。hmi.cpp 里另有一条 connect(alarmMgr_, …, this, &hmi::onAlarmTriggered)
    //   —— 那是界面显示用的,不是联动,留着是对的。
    //   重复接联动的 connect 才是问题(停机动作执行两次)。
    QVERIFY2(countOf(code, QStringLiteral("bindActionToAlarms(")) == 1,
             "★ hmi.cpp 里 bindActionToAlarms 被调了不止一次 —— "
             "多调一次虽然被 UniqueConnection 兜住,但那是最后一道防线,不该靠它。");

    // ★ 旧的那条裸 connect 必须【不在】了
    //   (它在的时候,两处都接 = 信号发两遍 = 停机动作执行两次)
    QVERIFY2(!code.contains(QStringLiteral("actionMgr_, &ActionManager::onAlarmTriggered")),
             "★ hmi.cpp 里还留着那条裸的 connect(…, actionMgr_, &ActionManager::onAlarmTriggered)。"
             "它跟共享接线重复 = 一次报警触发两次联动。");
}

QTEST_MAIN(tst_ActionWrite)
#include "tst_ActionWrite.moc"

// ============================================================================
// tst_PollSchedulerDerived —— 派生变量【不许】进轮询
//
// 【今天(写这条用例时)发现的真实缺陷】
//   PollScheduler::onTick 遍历 allVariables() 挑"到期"的变量时,
//   过滤条件只有两条:alwaysPoll / activeVariables_,加上 inFlight_ 去重。
//   ★ 【没有 isDerived() 这一条】。
//
//   而 addDerivedVariable(VariableManager.cpp:152)把派生变量建成
//   address=0 —— 它【没有寄存器】,值是本地算出来的。
//   于是这个派生变量:
//     ① 被排进 dueByPlc,plcId 默认 "plc1"
//     ② 调度器向 plc1 申请读【地址 0】
//     ③ reply 回来,onDriverReadFinished 把寄存器里的值【写进这个变量】,
//        覆盖掉 ExpressionParser 刚算出来的结果
//   后果:派生量变成了 plc1 的 0 号寄存器 —— 而那【是真的现场寄存器】,
//        不是没人用的占位符。等有人重算表达式时它又被改回去,
//        画面上这个量在"计算值"和"某台 PLC 的某个寄存器"之间来回跳。
//
//   这跟"读到一个坏值"不是一回事:它是【把本地计算结果和现场数据
//   混在同一个地址上】,而且写的是一台【真的连着的】PLC 的真的寄存器。
//
// 【为什么今天没炸】
//   派生变量在 UI 上建不出来(整个变量界面至今不存在),
//   只有 TagImporter 导 CSV 能造出来;而导出来的变量 alwaysPoll 默认是关的,
//   只要没被 activeVariables_ 点名就进不了这一圈。
//   也就是说它是【差一个条件就能触发】,不是【不可达】——
//   变量管理界面一做完,界面里新建的派生变量默认 alwaysPoll 打开,
//   这条立刻变成常态路径。所以要在界面做出来【之前】堵。
//
// 【负向对照 —— 实跑,2026-10-19】
//   把 `if (v->isDerived()) continue;` 删掉,退回今天的行为:
//     → 红 3:derivedVariable_isNeverPolled /
//             derivedValue_isNotOverwrittenByRegisterZero /
//             twoDerivedVariables_neitherIsPolled
//       绿 1:pollStillWorksForNormalVariables
//   只有"普通变量照常轮询"那条绿是对的 —— 它本来就不该红。
//   和 2b 那两轮同一个判断标准 —— "只红该红的"说明分层是对的。
//
// ★★ 这一栏我【写错过两次】,都记在这儿,因为错的地方比数字本身值钱:
//
//   ① 写预测时写的是"红 2",实跑是红 3 ——
//      我把 twoDerivedVariables_neitherIsPolled 算进了"本来就不该红"。
//      实际它就该红:两个派生变量同样在被拿去读 0 号寄存器。
//      错在【把一条防"只挡住第一个"的用例,当成了跟主断言重复】。
//
//   ② 第一轮实跑确实是"红 2",而且其中一条的绿是【假绿】——
//      derivedValue_isNotOverwrittenByRegisterZero 那时是绿的,我以为预测对了。
//      它绿的原因不是"实现没问题",是【它压根测不到东西】:
//
//        · mock driver 只回 1 个寄存器(不管 req.count 要几个)
//        · 而 Sum 是 Float —— onDriverReadFinished 里 Float 要【2 个】寄存器
//        · 寄存器不够 → 走"数据不够"分支 → 值置成无效 QVariant → 什么都没写
//
//      也就是说:不管 onTick 怎么改,这条用例都绿。
//      一条【怎么改都不会红】的用例,和一个没有用例,是同一件事。
//
//      修了两处,都在【测试这一侧】,都没碰生产逻辑:
//        · mock 按 req.count 回同样多个寄存器(真实 Modbus 就是这个行为)
//        · Sum 从 Float 改成 Int16 —— Int16 要 1 个寄存器,而 Sum 独占地址 0、
//          合并后 count=1,所以【真的写得进去】,覆盖才可能发生
//      改完再跑负向对照,这条如期变红,数字才从 2 变成 3。
//
//   留这条记录的原因:上面第 ② 条正是"负向对照红数比预期少"
//   最常见的真相 —— 少的那条不一定没坏,可能是【坏得根本没被测到】。
//
// 关联:PollScheduler::onTick(第三圈过滤),
//      VariableManager::addDerivedVariable(建出 address=0 的派生变量)
// ============================================================================

#include <QtTest>
#include <QSignalSpy>

#include "PollScheduler.h"
#include "VariableManager.h"
#include "Variable.h"
#include "drivers/IPollDriver.h"

using namespace qwhmi::drivers;

// ★ 一个"会记下所有被请求过的地址"的 mock driver。
//   为什么要自己写而不是复用 tst_IPollDriver 里那个:那条用例只需要
//   "最后一次请求"(lastReadReq),而这里要的是【历史上请求过哪些地址】——
//   派生变量如果混在普通变量里发出去,lastReadReq 可能正好是普通变量那次,
//   于是测试全绿而 bug 还在。要看清"地址 0 有没有人被读过",
//   就得把所有请求都攒下来。
class RecordingDriver : public IPollDriver {
    Q_OBJECT
public:
    explicit RecordingDriver(QObject* parent = nullptr) : IPollDriver(parent) {}

    // ★ 每一个被请求过的起始地址,按顺序
    QList<int> requestedAddrs;
    // ★ 同时记下"读了几个连续寄存器"(count)。
    //   为什么要记 count:PollScheduler 会把【地址连续的变量合并成一次请求】。
    //   我第一版的断言是 requestedAddrs.contains(201),结果红了 ——
    //   而红色说明的是【A(200) 和 B(201) 被合并成了一次 200..201 的读】,
    //   不是"有个变量没被轮询"。
    //   只记起始地址的话,这种"合并"和"漏轮询"在结果上长得一样。
    QList<int> requestedCounts;
    int sendReadCount = 0;
    // 回包内容:地址 0 回 4242(一个好认的数),
    // 它出现就说明"有人在读 0 号寄存器"。
    static constexpr quint16 kRegisterZeroValue = 4242;

    void connectTo(const QString& host, quint16 port) override {
        connectedFlag = true;
        emit connected();
    }
    void disconnect() override { connectedFlag = false; emit disconnected(); }
    bool isConnected() const override { return connectedFlag; }
    int pendingCount() const override { return 0; }

    bool sendReadRequest(const ReadRequest& req) override {
        requestedAddrs << req.addr;
        requestedCounts << req.count;
        ++sendReadCount;
        // ★ 回包要按 req.count 给【同样多个】寄存器,不能只给一个。
        //   真实的 Modbus 读 N 个寄存器就回 N 个;
        //   而 onDriverReadFinished 是按变量类型取位的 ——
        //   Float / Int32 要 2 个寄存器拼,只有 1 个时它走
        //   "数据不够"分支、把值置成【无效 QVariant】,
        //   于是什么都没写进去。
        //   ★ 我第一版只回一个,结果 derivedValue_ 那条用例【永远是绿的】:
        //     它盯着派生变量的值,而那个值因为"寄存器不够"根本没机会被改。
        //     一条【怎么改实现都绿】的用例等于没有 —— 必须让它真的能红。
        QVector<quint16> regs(req.count, kRegisterZeroValue);
        emit readFinished(req.requestId, regs, true);
        return true;
    }

    bool sendWriteRequest(RegisterArea, int, int,
                           const QVector<quint16>&, quint32) override { return true; }
    QString protocolName() const override { return QStringLiteral("recording"); }

    bool connectedFlag = false;

    bool everRequestedAddrZero() const { return requestedAddrs.contains(0); }
};

class tst_PollSchedulerDerived : public QObject {
    Q_OBJECT
private slots:
    // ★ 主断言:地址 0 【一次都不许】被请求
    void derivedVariable_isNeverPolled();

    // ★ 派生变量的值不许被寄存器内容覆盖
    //   (与上一条不同的角度:上一条钉"没发请求",这条钉"值没被改")
    void derivedValue_isNotOverwrittenByRegisterZero();

    // ★ 两个派生变量也都【不许】被读 —— 防"只挡了第一个"的实现
    void twoDerivedVariables_neitherIsPolled();

    // 反向:普通变量照常轮询 —— 修这个过滤不许把正常轮询一起挡掉
    void pollStillWorksForNormalVariables();
};

namespace {

// 起一个会真的轮询几帧的调度器。
//
// ★ 三个必须做对的地方。我第一版三条全错,症状是 requestedAddrs 恒为空
//   —— 而如果没有下面"对照组"那种断言,这会变成【假绿】:
//   "什么都没轮询"同样满足"派生变量没被轮询"。
//   ① driver 必须【已经连上】—— onTick 第 2 步
//      `if (!drv->isConnected()) { 全部标 Bad; continue; }`,
//      没连上则一个请求都发不出去。
//      【用 connectTo() 而不是直接置 connectedFlag】—— 走公开入口,
//      和生产里 PlcConnectionManager 建连接的路径同形。
//   ② 必须 setAllActive() —— onTick 的过滤是
//      `if (!v->alwaysPoll() && !activeVariables_.contains(v)) continue;`
//      靠 alwaysPoll 打开也能过,但【生产的默认路径是 setAllActive()】,
//      两条路都要覆盖到,所以这里走 setAllActive。
//   ③ qWait 要【盖过轮询周期】—— timer 是 100ms,
//      等 30ms 一次都轮不到(我第一版就只等了 30ms)。
//      250ms 沿用 tst_IPollDriver 那批已验证的用例。
void startAndPoll(PollScheduler& sched) {
    sched.setAllActive();
    sched.start();
    QTest::qWait(250);
    sched.stop();
}

// ★ 为什么驱动是"连上了"的,而不是构造时就设好:
//   isConnected() 是运行期状态,由外部建连接流程决定。
//   在构造函数里塞 connectedFlag=true 会把这个测试脚手架的事实
//   藏进类型定义里,读用例的人看不出它为什么能轮询。
//
// ★ 为什么返回裸指针:IPollDriver 继承 QObject,不可拷贝。
RecordingDriver* makeConnectedDriver() {
    auto* d = new RecordingDriver();
    d->connectTo(QStringLiteral("127.0.0.1"), 502);
    return d;
}

} // namespace

// ---------------------------------------------------------------------------

// ★ 主断言。
void tst_PollSchedulerDerived::derivedVariable_isNeverPolled() {
    VariableManager vm;
    RecordingDriver* drv = makeConnectedDriver();

    // 一个普通变量(alwaysPoll 打开,确保它会进轮询 —— 这一条是"对照组")
    auto* speed = new Variable(QStringLiteral("Speed"), quint16(100),
                               Variable::DataType::Int16,
                               Variable::Access::ReadOnly, &vm);
    speed->setAlwaysPoll(true);
    vm.addVariable(speed);

    // ★ 依赖必须先注册好,再建派生变量(见 startAndTick 上面那段注释 ②)
    auto* temp = new Variable(QStringLiteral("Temp"), quint16(101),
                              Variable::DataType::Int16,
                              Variable::Access::ReadOnly, &vm);
    temp->setAlwaysPoll(true);
    vm.addVariable(temp);

    // 一个派生变量:Sum = Speed + Temp。它没有寄存器。
    // ★ 先给两个源变量一个值:addDerivedVariable 结尾会立刻算一次初值,
    //   而求值器对"还没取到值"的依赖会 qWarning 一条 "not found"。
    //   那条警告本身不影响被测逻辑,但它会让测试输出里混进无关噪音,
    //   将来真出问题时不好一眼看出哪条是本次的。
    speed->setValue(10);
    temp->setValue(20);
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed + Temp"),
                                  Variable::DataType::Float));
    Variable* sum = vm.findVariable(QStringLiteral("Sum"));
    QVERIFY(sum);
    QVERIFY(sum->isDerived());
    // ★ 前置事实:派生变量确实没有自己的寄存器(地址 0)。
    //   下面断言的正是"正因为它没有寄存器,所以谁都不该去读它"。
    QCOMPARE(sum->address(), quint16(0));

    // ★ 故意把派生变量的 alwaysPoll【打开】——
    //   变量管理界面做出来之后,用户新建的派生变量默认就是这个状态。
    //   今天的缺陷正因为"默认关着"才没炸,这里把那个前提拿掉。
    sum->setAlwaysPoll(true);

    PollScheduler sched(&vm, drv);
    startAndPoll(sched);

    // 对照组:普通变量确实被读了(否则这条断言可能是"因为什么都没轮询"而绿)
    QVERIFY2(drv->requestedAddrs.contains(100),
             qPrintable(QStringLiteral(
                 "对照组失败:普通变量 Speed(地址 100)没有被轮询 —— "
                 "requestedAddrs = %1。这样 derivedVariable_isNeverPolled "
                 "就可能是\"因为什么都没轮询\"而假绿。")
                 .arg(QStringLiteral("%1")
                          .arg(QString::number(drv->requestedAddrs.size())))));

    QVERIFY2(!drv->everRequestedAddrZero(),
             qPrintable(QStringLiteral(
                 "★★ 派生变量被拿去轮询了 —— 它【没有寄存器】。\n"
                 "  被请求的地址列表里出现了 0:%1\n"
                 "  后果:调度器向 plc1 申请读 0 号寄存器,回包再把这个变量\n"
                 "  的值覆盖成那台 PLC 的真寄存器内容 —— 把本地计算结果\n"
                 "  和现场数据混在同一个地址上。\n"
                 "  修法:onTick 第三圈过滤加 if (v->isDerived()) continue;")
                 .arg(QString::number(drv->requestedAddrs.size()))));
}

// ★ 换个角度钉同一件事:盯【值】而不是盯【请求】。
//   两条并存的理由:一条盯"没发出去",一条盯"值没被改"。
//   万一将来 onTick 的结构变了(比如提前 return、或者请求发出去了
//   但被别的路径回填),盯请求的那条可能还绿着,而值已经错了。
void tst_PollSchedulerDerived::derivedValue_isNotOverwrittenByRegisterZero() {
    VariableManager vm;
    RecordingDriver* drv = makeConnectedDriver();

    auto* speed = new Variable(QStringLiteral("Speed"), quint16(100),
                               Variable::DataType::Int16,
                               Variable::Access::ReadOnly, &vm);
    speed->setAlwaysPoll(true);
    vm.addVariable(speed);
    auto* temp = new Variable(QStringLiteral("Temp"), quint16(101),
                              Variable::DataType::Int16,
                              Variable::Access::ReadOnly, &vm);
    temp->setAlwaysPoll(true);
    vm.addVariable(temp);

    // ★ 派生量建成 Int16,不是 Float —— 这不是随手选的。
    //   onDriverReadFinished 按类型取位:Int16 要 1 个寄存器,
    //   Float / Int32 要 2 个。Sum 独占地址 0(旁边没有别的变量),
    //   合并后 count=1 —— 于是 Float 派生量【永远拿不到可写的值】,
    //   "值被覆盖"这件事在 Float 上根本不会发生,用例会一直是绿的。
    //   也就是说:用 Float 写这条用例,等于写了一条【测不出任何东西】的用例。
    //   Int16 才是这条断言真正想盯的那个形状(1 个寄存器就能写进去)。
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed + Temp"),
                                  Variable::DataType::Int16));
    Variable* sum = vm.findVariable(QStringLiteral("Sum"));
    QVERIFY(sum);
    sum->setAlwaysPoll(true);

    // 给两个源变量一个已知的值,重算一次
    speed->setValue(3);
    temp->setValue(4);
    vm.recomputeDerivedVariables();
    const double before = sum->value().toDouble();

    PollScheduler sched(&vm, drv);
    startAndPoll(sched);

    const double after = sum->value().toDouble();
    QCOMPARE(after, before);

    QVERIFY2(qAbs(after - RecordingDriver::kRegisterZeroValue) > 0.001,
             qPrintable(QStringLiteral(
                 "★★ 派生变量的值变成了 %1 —— 那正是 mock driver 回给"
                 "0 号寄存器的数。计算结果被现场寄存器内容顶掉了。")
                 .arg(after)));
}

// ★ 防"只挡了第一个"的实现(比如只 remember 了某一条派生变量)。
void tst_PollSchedulerDerived::twoDerivedVariables_neitherIsPolled() {
    VariableManager vm;
    RecordingDriver* drv = makeConnectedDriver();

    auto* speed = new Variable(QStringLiteral("Speed"), quint16(100),
                               Variable::DataType::Int16,
                               Variable::Access::ReadOnly, &vm);
    speed->setAlwaysPoll(true);
    vm.addVariable(speed);
    auto* temp = new Variable(QStringLiteral("Temp"), quint16(101),
                              Variable::DataType::Int16,
                              Variable::Access::ReadOnly, &vm);
    temp->setAlwaysPoll(true);
    vm.addVariable(temp);

    QVERIFY(vm.addDerivedVariable(QStringLiteral("Sum"),
                                  QStringLiteral("Speed + Temp"),
                                  Variable::DataType::Float));
    QVERIFY(vm.addDerivedVariable(QStringLiteral("Avg"),
                                  QStringLiteral("(Speed + Temp) / 2"),
                                  Variable::DataType::Float));
    for (const char* n : {"Sum", "Avg"}) {
        Variable* d = vm.findVariable(QString::fromLatin1(n));
        QVERIFY2(d && d->isDerived(), qPrintable(QStringLiteral("%1 不是一个派生变量").arg(QLatin1String(n))));
        d->setAlwaysPoll(true);
    }

    PollScheduler sched(&vm, drv);
    startAndPoll(sched);

    QVERIFY2(!drv->everRequestedAddrZero(),
             qPrintable(QStringLiteral(
                 "★ 请求过 0 号寄存器。两个派生变量都建出来了,"
                 "只挡住一个也算失败。requestedAddrs = %1")
                 .arg(QString::number(drv->requestedAddrs.size()))));
}

// ★ 反向:加了 isDerived 过滤之后,普通变量【照常】轮询。
//   没有这条的话,"一个都不轮询"也能让上面三条全绿。
//
// ★ 这条在第一次跑的时候红过,而红色【不是 bug】——
//   A(200) 和 B(201) 地址连续,PollScheduler 会把它们【合并成一次读】
//   (200 起、count=2)。所以 requestedAddrs 里只有 200,没有 201。
//   我第一版的断言写成 contains(201),于是红了 ——
//   一个"两个变量都在轮询"的好行为,在只记起始地址的 mock 面前
//   长得和一个"漏轮询"的坏行为一模一样。
//
//   ★★ 这里必须把这条记下来,否则下一个人会做同一个改动:
//     把断言从 contains(201) 改成 contains(200) 就算绿了 —— 那就对了。
//     但真正的教训是:【合并是真的发生了,不是断言写错了】。
//     所以断言写成"这次读的区间覆盖了 200 和 201",而不是"有两个请求"。
void tst_PollSchedulerDerived::pollStillWorksForNormalVariables() {
    VariableManager vm;
    RecordingDriver* drv = makeConnectedDriver();

    auto* a = new Variable(QStringLiteral("A"), quint16(200),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly, &vm);
    auto* b = new Variable(QStringLiteral("B"), quint16(201),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly, &vm);
    a->setAlwaysPoll(true);
    b->setAlwaysPoll(true);
    vm.addVariable(a);
    vm.addVariable(b);

    PollScheduler sched(&vm, drv);
    startAndPoll(sched);

    QVERIFY2(!drv->requestedAddrs.isEmpty(),
             "一个请求都没发出去 —— 那上面三条\"派生变量没被轮询\"全是因为"
             "什么都没轮询,而不是因为过滤起了作用。");

    // ★ 断言"这次读覆盖了 200 和 201",允许它们被合并成一次读
    bool covered200 = false, covered201 = false;
    for (int i = 0; i < drv->requestedAddrs.size(); ++i) {
        const int start = drv->requestedAddrs.at(i);
        const int count = drv->requestedCounts.value(i, 1);
        if (200 >= start && 200 < start + count) covered200 = true;
        if (201 >= start && 201 < start + count) covered201 = true;
    }
    QVERIFY2(covered200,
             qPrintable(QStringLiteral(
                 "普通变量 A(地址 200)没有被轮询 —— requestedAddrs = %1")
                 .arg(QString::number(drv->requestedAddrs.size()))));
    QVERIFY2(covered201,
             qPrintable(QStringLiteral(
                 "普通变量 B(地址 201)没有被轮询 —— requestedAddrs = %1")
                 .arg(QString::number(drv->requestedAddrs.size()))));
}

QTEST_MAIN(tst_PollSchedulerDerived)
#include "tst_PollSchedulerDerived.moc"

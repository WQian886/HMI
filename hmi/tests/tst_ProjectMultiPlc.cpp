// ============================================================================
// tst_ProjectMultiPlc —— 多 PLC 2a:配置要"存得住",一个都不能丢
//
// 【2a 到底做了什么】
//   改之前,Project 里的 plcs[] 是个花瓶:
//     · 加载只读 plcsArr[0](Project.cpp 第 8 步),文件里另外几台没人碰
//     · 保存【无条件写死一台】id 恒为 "plc1"
//   合起来的后果:用户配 3 台 PLC → 打开工程 → 点保存 →
//     另外 2 台被静默丢掉。**不报错、不警告、文件里就是没有了。**
//   这个 bug 的恶劣之处在于它是"单向"的:丢掉的再也回不来,
//   而用户直到某天发现 2 号 PLC 不通讯了才知道出过错。
//
//   2a 修的正是这个:加载时全部读进 plcs_,保存时原样写回。
//
// 【2a 明确【没做】什么 —— 这条边界 2b 已经补上】
//   2a 结束时多台 PLC 【不会真的同时连上】,运行时仍然只连 plcs_[0]。
//   2b(2026-10-19)把这一刀补上了:Project 不再自己连,而是通过
//   setPlcApplier 把全部配置递给 PlcConnectionManager。
//   所以本文件那条 multiplePlcs_runtimeStillAppliesOnlyFirst 的断言
//   已经【改写】成 multiplePlcs_allOfThemReachTheApplier ——
//   【断言改了,用例没删】。删掉等于把 2a 的这段历史静默继承给 2b,
//   下一个读这个文件的人就看不出"这一段曾经是个已知缺口"。
//
// 【2b 在本文件里钉的是"数据有没有完整交出去",不是"有没有连上"】
//   真连接是阶段 5 那条 3 个真 QTcpServer 的 E2E 的职责。
//   这里刻意不接 PlcConnectionManager —— 那样这条用例就变成
//   一条"跑得起来"的集成测试,但它失败时你分不清是 Project 递错了
//   还是网络没通。分层的好处就是:这一层红,一定就是这一层坏了。
//
// 【为什么这类测试特别容易假绿】
//   "读回来数量对不对"这种断言,在【加载路径坏掉、保存路径也坏掉】时会互相抵消 ——
//   比如两处都只留 1 台,那么"读 1 台 → 存 1 台 → 再读 1 台"全程绿。
//   所以本文件的核心用例 roundTrip 三台 PLC 是【逐字段逐台】比对的,
//   而且额外钉住 id 这个"用户唯一能看见的标识"不许被改写。
//
// 【负向对照 —— 实测两轮,把 bug 原样装回去跑】
//   ① 保存侧退回"写死一台"(saveToFile 短路掉 plcs_ 分支)
//      → 红 3:saveAndReload_threePlcs_allSurvive /
//              reload_preservesPlcIdVerbatim /
//              roundTrip_preservesUnknownUserFields
//        绿 5 —— 其中 reload_preservesEachPlcsOwnAddress 绿是对的,
//                它只测加载侧,根本没经过保存。
//   ② 加载侧退回"只读第一台"(loadPlcsFromJson 只取 arr[0])
//      → 红 5:含上面那 3 条,另加 reload_preservesEachPlcsOwnAddress
//              和 invalidEntries_areSkippedButOthersSurvive
//
// ★ 两轮合起来才完整:① 只证明"存不住",② 只证明"读不全",
//   两条都堵住才说明数据在往返中【真的恒等】。
//   只做一轮的话,另一侧照样可以坏着而测试全绿 ——
//   那正是这个 bug 当初能活下来的原因:两侧各自都"看起来在工作"。
//
// 【2b 追加的负向对照 —— 同样两轮实跑,2026-10-19】
//   轮次一:第 8 步【不】调用 plcApplier_(装回"只读不建连接",即 2a 的行为)
//     → 红 3:multiplePlcs_allOfThemReachTheApplier(calls != 1)
//             plcApplier_receivesValidatedConfigNotRawArray
//             plcApplier_notInvokedWhenLoadFails
//       绿 10
//   轮次二:递【原始】数组 root["plcs"](没过 validatePlcEntry)
//     → 红 1:plcApplier_receivesValidatedConfigNotRawArray
//       绿 12
//   ★ 轮次二只红 1 条,是【正确且预期】的结果 ——
//     它就是专门为这个方向写的那一条。3 条用例各自盯一个不同方向,
//     不是"3 条一起防 1 个 bug",所以"只红 1"说明分层是对的。
//
// 【★ 2026-10-21 修"存坏工程"那一刀的负向对照 —— 实测两轮】
//
//   病:删掉一个还被报警/联动引用的变量,再存盘 →
//       悬空引用被写进文件 → 下次打开这个工程【整个打不开】
//       (加载端 Project.cpp:556 / :579 findVariable 拿不到 → failWith)。
//   修法:存盘时【跳过】悬空的那条(报警侧空串 / 联动侧悬空名字)。
//
//   轮次一:把修法换成【最粗暴的版本】——"所有报警和联动统统跳过"
//     (Project.cpp 两处守卫改成 `if (true)` / `if (varMgr_)`)
//     → 红 1:danglingReference_isSkipped_butValidOnesSurvive
//       绿 23
//     ★ 这是本轮最要紧的一条观察:老的三条用例
//       (deleteReferencedVariable_savedProjectStillLoads /
//        deleteVariableReferencedByAction_savedProjectStillLoads /
//        deleteReferencedVariable_savingDoesNotCrash)【照样绿】。
//       它们只断言"存出来的工程能打开",而"什么都不写"【也】满足这个断言 ——
//       空的 alarms 数组在加载时循环体一次都不进,工程照样打得开。
//       所以这三条【结构上就抓不住】"好的被一起砍掉"这种实现。
//       → 新用例不是重复覆盖,是补上了一个老用例根本够不到的洞。
//
//   轮次二:把 bug 【原样装回去】
//     (alarmObj["variable"] 退回 `tc->variable() ? … : QString()`,
//      联动侧去掉 findVariable 守卫)
//     → 红 3:上面那两条老用例 + 新用例
//       (新用例报的是更直白的"存出来的工程打不开了(悬空引用没被跳过)")
//       绿 21
//
//   ★ 两轮合起来才说明修法是对的:
//     轮次一证明"新用例补的是老用例抓不到的洞";
//     轮次二证明"红是真的因为 bug 在,不是因为别的"。
//     只做一轮的话,很容易把"新用例也绿了"当成"修好了"——
//     而那恰恰是轮次一演示的那个假象。
//
// 【★ 踩过的坑:回滚源码忘了改时间戳 → 跑的是旧二进制】
//   负向对照做完要把源码装回去。Copy-Item 会把【源文件的旧 mtime】
//   一起带过去,而 ninja 只看 mtime —— 于是它判定"这个文件没变过",
//   【不重编】,ctest 跑的还是刚才那个装了 bug 的二进制。
//   现象:源码明明已经修好,测试却还是红的。差点被误读成"修法没用"。
//   → 回滚之后必须戳一下:(Get-Item f).LastWriteTime = Get-Date
//   这条比看起来重要 —— 它是【假红】的来源,而假红会让人去改本来正确的代码。
//
// 关联:Project::loadPlcsFromJson / setPlcs / plcs() / setPlcApplier,
//      Project.cpp 第 8 步 + saveToFile
// ============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTemporaryDir>

#include "Project.h"
#include "VariableManager.h"
#include "AlarmManager.h"
#include "ActionManager.h"
#include "PollScheduler.h"
#include "VariableRefScan.h"   // ★ 阶段 1:变量引用扫描
#include "ThresholdAlarmChecker.h"  // 造一条阈值报警
#include "WriteFailChecker.h"  // ★ 造一个"查不到变量"的非阈值报警

namespace {

// 一台合法 PLC 的最小配置
QJsonObject makePlc(const QString& id, const QString& host, int port) {
    QJsonObject p;
    p["id"]       = id;
    p["protocol"] = QStringLiteral("modbus_tcp");
    p["host"]     = host;
    p["port"]     = port;
    return p;
}

} // namespace

class tst_ProjectMultiPlc : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ★★★ 核心:三台 PLC 存进去,必须原样存回来,一台都不能少
    void saveAndReload_threePlcs_allSurvive();

    // 每台的 host/port 必须【逐台】对上 —— 防止"顺序变了但数量对"蒙混过关
    void reload_preservesEachPlcsOwnAddress();

    // ★ id 是用户唯一能看见的标识,不许被程序改写
    //   (改前 saveToFile 写死 "plc1",2 号 PLC 存完会变成 1 号 —— 这就是本 bug 的核心)
    void reload_preservesPlcIdVerbatim();

    // ★ 存-取往返必须是【恒等】的,包括用户自己加的未知字段
    //   这条挡的是"保存时按白名单重新生成一遍对象"这种做法:
    //   它会把代码没显式列出的字段全丢掉,而用户完全不知情。
    void roundTrip_preservesUnknownUserFields();

    // 没加载过工程直接保存 → 仍然得到一台默认 PLC(老行为不能变)
    void saveWithoutLoading_writesOneDefaultPlc();

    // 缺 plcs 键的老工程 → 读成 0 台,且【不】报错
    void loadWithoutPlcsField_yieldsZeroAndNoError();

    // 校验:host 为空 / 端口越界 的项被跳过,但【不】让整个工程打不开
    void invalidEntries_areSkippedButOthersSurvive();

    // ★★★ 2b 之后:全部配置都要递到 applier 手上,且逐台对得上
    //   原来是 multiplePlcs_runtimeStillAppliesOnlyFirst(钉 2a 的边界)。
    //   断言改了,【用例没删】—— 删掉就等于把 2a 那段历史静默继承给 2b。
    void multiplePlcs_allOfThemReachTheApplier();

    // 递给 applier 的必须是【校验后】的,不是原始数组(空 host 那台不该去连)
    void plcApplier_receivesValidatedConfigNotRawArray();

    // 加载失败【绝对不能】建连接(否则用户会看到"没加载成功却已经在轮询了")
    void plcApplier_notInvokedWhenLoadFails();

    // ★ 文件里没有 slave_id,保存时【不许】补上缺省 1。
    //   补了会在用户版本控制里冒出一片"我什么都没改"的 diff。
    //   这条同时守着 saveToFile 那道"一个字都不许动"的硬约束。
    void slaveId_absentInFile_isNotGeneratedOnSave();

    // ★★★ 删掉一个被报警引用的变量,存出来的工程【必须还能打开】。
    //   今天的真实行为是打不开 —— 详见下面那条用例的注释。
    void deleteReferencedVariable_savedProjectStillLoads();

    // ★★★ 同上,但走 actions 那一侧。
    //   这两条【必须同时存在】:变量被引用有两个独立的来源
    //   (alarms / actions),只查一个就等于漏了另一个。
    void deleteVariableReferencedByAction_savedProjectStillLoads();

    // ★★★ 上面两条钉的是"文件会坏"。这条钉的是修掉崩溃那一步本身。
    //   没有它,有人把 variable_ 改回裸指针(或者把 QPointer 换成别的),
    //   上面两条只是重新变成崩溃,而不是干净的断言失败 —— 那时候
    //   测试进程直接死掉,后面所有用例都不跑,一次失败看不到全貌。
    void deleteReferencedVariable_savingDoesNotCrash();

    // ★★★ 修"存坏文件"的那一刀,不能顺手把好的也砍了。
    //   修法是"存盘时跳过悬空的引用" —— 这句话有两面:
    //     · 悬空的【必须】跳过(上面两条管)
    //     · 没悬空的【必须】留下(这条管)
    //   少了后半句,一个"把所有报警和联动都跳过"的实现能让上面两条全绿,
    //   而工程里的报警和联动被清光了,用户要等到现场不报警了才发现。
    void danglingReference_isSkipped_butValidOnesSurvive();

    // ---- 引用扫描本身(阶段 1 的正题)----

    // 扫得到阈值报警
    void refScan_findsThresholdAlarmReference();

    // 扫得到联动引用
    void refScan_findsActionReference();

    // ★ 两条引用源【同时】存在时两条都要报出来(不是二选一)
    void refScan_findsBothSourcesAtOnce();

    // 没人引用 → 真的没人引用(反向:查得出"有",也要查得出"没有")
    void refScan_unreferenced_isEmpty();

    // ★★ 扫不到的东西必须被点名报出来(画面的控件绑定)
    //   这条钉的是"诚实"本身:把 blindSpots 悄悄丢掉的话,
    //   界面上就会显示"无引用可删",用户以为已经看全了,
    //   而画面上那个显示温度的表正定格在上一次读到的值。
    void refScan_alwaysDisclosesScreenBlindSpot();

    // 非阈值报警(WriteFailChecker)要被点名 —— 不能静默当成"没有引用"
    void refScan_namesUnscannableCheckers();

    // ★ 只给一半的 manager,另一半必须被报成"未检查",而不是当作"没有"
    void refScan_missingManagerIsReportedNotAssumedClean();

private:
    QTemporaryDir m_dir;
    QString writeRoot(const QJsonObject& root, const QString& name);

    // 存一个完整工程(带 variables/alarms/actions 最小集),返回文件路径
    QString writeProject(const QJsonArray& plcs, const QString& name);
};

// ---------------------------------------------------------------------------

void tst_ProjectMultiPlc::initTestCase() {
    QVERIFY2(m_dir.isValid(), "临时目录建不出来,后面的用例全部无从谈起");
}

QString tst_ProjectMultiPlc::writeRoot(const QJsonObject& root, const QString& name) {
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    f.write(QJsonDocument(root).toJson());
    f.close();
    return path;
}

QString tst_ProjectMultiPlc::writeProject(const QJsonArray& plcs, const QString& name) {
    QJsonObject root;
    root["schema_version"] = 3;              // 不触发 v1→v2 迁移,免得干扰被测逻辑
    root["project_name"]   = QStringLiteral("多 PLC 测试工程");

    // variables 最小集 —— Project::loadFromFile 第 5 步要求这一段存在
    QJsonObject v;
    v["name"] = QStringLiteral("Temp");
    v["address"] = 0;
    v["data_type"] = QStringLiteral("Int16");
    v["access"] = QStringLiteral("ReadOnly");
    v["poll_interval_ms"] = 1000;
    v["plc_id"] = QStringLiteral("plc1");     // 上一步(2a 之前那个提交)加的字段
    root["variables"] = QJsonArray{v};

    root["plcs"] = plcs;
    return writeRoot(root, name);
}

// ---------------------------------------------------------------------------

void tst_ProjectMultiPlc::saveAndReload_threePlcs_allSurvive() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    plcs.append(makePlc("plc2", "192.168.2.20", 502));
    plcs.append(makePlc("plc3", "10.0.0.5",    1502));

    const QString src = writeProject(plcs, "three.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    QVERIFY2(p.loadFromFile(src), "加载工程失败");
    QCOMPARE(p.plcs().size(), 3);

    // 存到新文件,再读回来 —— 真正的考验在【存】这一侧,
    // 因为改之前的 bug 就在这里(保存时写死一台)。
    const QString dst = m_dir.filePath(QStringLiteral("three_out.hmi.json"));
    QVERIFY2(p.saveToFile(dst), "保存工程失败");

    Project p2(&vm, &am, &ag, &poll, nullptr);
    QVERIFY2(p2.loadFromFile(dst), "重新加载失败 —— 存出去的东西自己都读不回来");
    QCOMPARE(p2.plcs().size(), 3);
}

void tst_ProjectMultiPlc::reload_preservesEachPlcsOwnAddress() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    plcs.append(makePlc("plc2", "192.168.2.20", 502));
    plcs.append(makePlc("plc3", "10.0.0.5",    1502));

    const QString src = writeProject(plcs, "addr.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));

    // ★ 逐台比对,而不是只比数量。
    //   只比数量的话,三台全变成同一个地址也能过 —— 那正是改前的实际行为。
    const QStringList hosts = {"192.168.1.10", "192.168.2.20", "10.0.0.5"};
    const QList<int>     ports = {502, 502, 1502};
    for (int i = 0; i < hosts.size(); ++i) {
        QJsonObject got = p.plcs().at(i).toObject();
        QCOMPARE(got["host"].toString(), hosts.at(i));
        QCOMPARE(got["port"].toInt(), ports.at(i));
    }
}

void tst_ProjectMultiPlc::reload_preservesPlcIdVerbatim() {
    // ★★★ 本 bug 的核心。
    //   改之前 saveToFile 写死 plc["id"] = "plc1",于是:
    //     2 号 PLC 存一次 → id 变成 plc1 → 和 1 号 PLC 撞名 → 两台都连不上,
    //     而且文件里看过去"两台都在",只是 id 一样,肉眼很难立刻发现。
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    plcs.append(makePlc("press_line", "192.168.2.20", 502));   // 故意用非 plcN 命名
    QJsonArray extra;
    plcs.append(makePlc("drying", "10.0.0.5", 1502));

    const QString src = writeProject(plcs, "ids.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));

    const QString dst = m_dir.filePath(QStringLiteral("ids_out.hmi.json"));
    QVERIFY(p.saveToFile(dst));

    Project p2(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p2.loadFromFile(dst));

    QCOMPARE(p2.plcs().size(), 3);
    // 名字必须一字不差 —— 尤其 "press_line" 和 "drying",
    // 它们不是 plc1/plc2 那套默认命名,程序没有任何理由去"规范化"它们
    QCOMPARE(p2.plcs().at(0).toObject()["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(p2.plcs().at(1).toObject()["id"].toString(), QStringLiteral("press_line"));
    QCOMPARE(p2.plcs().at(2).toObject()["id"].toString(), QStringLiteral("drying"));
}

void tst_ProjectMultiPlc::roundTrip_preservesUnknownUserFields() {
    // ★ 存-取往返必须【恒等】,包括程序自己根本不认识的字段。
    //
    //   为什么这条重要:将来 2b 要给每台 PLC 加通讯参数(重试次数、超时…)。
    //   如果保存时是"按已知字段重新生成一个对象",那么 2b 没上线前用户
    //   手工加的任何字段都会在保存时消失 —— 而且程序对此一无所知。
    //
    //   这里的 "snmp_community" 是一个当前版本【完全不解析】的字段,
    //   模拟"用户手填的、程序不懂的、但不该被抹掉的东西"。
    QJsonObject custom = makePlc("plc1", "192.168.1.10", 502);
    custom["snmp_community"] = QStringLiteral("public");
    custom["site_note"]      = QStringLiteral("3号车间,靠东墙");

    QJsonArray plcs;
    plcs.append(custom);
    plcs.append(makePlc("plc2", "192.168.2.20", 502));

    const QString src = writeProject(plcs, "extra.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));

    const QString dst = m_dir.filePath(QStringLiteral("extra_out.hmi.json"));
    QVERIFY(p.saveToFile(dst));

    Project p2(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p2.loadFromFile(dst));

    const QJsonObject got = p2.plcs().at(0).toObject();
    QCOMPARE(got["snmp_community"].toString(), QStringLiteral("public"));
    QCOMPARE(got["site_note"].toString(), QStringLiteral("3号车间,靠东墙"));
}

void tst_ProjectMultiPlc::saveWithoutLoading_writesOneDefaultPlc() {
    // ★ 老行为不能变:程序【没有】加载过任何工程时直接保存,
    //   应该得到一台默认 PLC(127.0.0.1:502 / plc1)。
    //   为什么不能写空数组:"新建工程 → 直接保存"是最常见的动作,
    //   写空数组等于给出一个没有任何通讯配置的工程文件。
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    const QString dst = m_dir.filePath(QStringLiteral("fresh.hmi.json"));
    QVERIFY(p.saveToFile(dst));

    Project p2(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p2.loadFromFile(dst));
    QCOMPARE(p2.plcs().size(), 1);
    QCOMPARE(p2.plcs().at(0).toObject()["id"].toString(), QStringLiteral("plc1"));
}

void tst_ProjectMultiPlc::loadWithoutPlcsField_yieldsZeroAndNoError() {
    QJsonObject root;
    root["schema_version"] = 3;
    root["project_name"]   = QStringLiteral("没有 PLC 段的工程");
    QJsonObject v;
    v["name"] = QStringLiteral("Temp");
    v["address"] = 0;
    v["data_type"] = QStringLiteral("Int16");
    v["access"] = QStringLiteral("ReadOnly");
    root["variables"] = QJsonArray{v};
    // 刻意不写 plcs 键

    const QString src = writeRoot(root, "noplc.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    // ★ 加载必须【成功】。缺 plcs 段是合法的工程,不是坏文件 ——
    //   因为"加载失败"意味着用户的变量、报警、画面全丢了,
    //   而他只是没配通讯而已,这两件事不该挂钩。
    QVERIFY2(p.loadFromFile(src), "缺 plcs 段不该导致加载失败");
    QCOMPARE(p.plcs().size(), 0);
}

void tst_ProjectMultiPlc::invalidEntries_areSkippedButOthersSurvive() {
    // 4 台里混 3 台坏配置:空 id / 空 host / 端口越界
    // ★ 期望:坏的三台被跳过,好的一台留下,【整个工程仍然加载成功】。
    //   不能因为一台 IP 打错就让用户的画面和变量全打不开。
    QJsonObject bad1 = makePlc("plc1", "192.168.1.10", 502);
    bad1["id"] = QStringLiteral("");                    // 空 id

    QJsonObject bad2 = makePlc("plc2", QStringLiteral(""), 502);   // 空 host
    QJsonObject bad3 = makePlc("plc3", "192.168.3.30", 70000);     // 端口越界

    QJsonArray plcs;
    plcs.append(bad1);
    plcs.append(bad2);
    plcs.append(bad3);
    plcs.append(makePlc("plc4", "192.168.4.40", 502));

    const QString src = writeProject(plcs, "mixed.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    QVERIFY2(p.loadFromFile(src), "一台配置填错不该让整个工程加载失败");
    QCOMPARE(p.plcs().size(), 1);
    QCOMPARE(p.plcs().at(0).toObject()["id"].toString(), QStringLiteral("plc4"));
}

void tst_ProjectMultiPlc::multiplePlcs_allOfThemReachTheApplier() {
    // ★★★ 这条以前叫 multiplePlcs_runtimeStillAppliesOnlyFirst ——
    //   它钉的是 2a 的【边界】:"2a 只保证配置存得住,没做多台在线"。
    //   2b 做完了,所以【断言改了,用例没删】。
    //   ★ 保留函数原名(改后)也不理想,但删掉就等于把 2a 的这段历史
    //     静默地继承给 2b。git 里应该看得出"这是一条被加强过的旧用例"。
    //
    //   2b 之后 Project 不再自己连,它把【全部】配置递给 setPlcApplier。
    //   所以新的断言是:applier 收到 2 台,且【逐台都对】——
    //   不是"收到 1 台"这条旧断言的反面,而是更强的"全都收到了,而且是对的"。
    //
    //   【为什么这里不起真连接】本文件全部跑在内存对象上,连 PlcConnectionManager
    //   都故意不接 —— 接了就变成阶段 5 那条 3 个真 QTcpServer 的 E2E 的职责。
    //   这一层只钉"数据有没有完整交出去"。
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    plcs.append(makePlc("plc2", "192.168.2.20", 502));

    const QString src = writeProject(plcs, "runtime.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);

    Project p(&vm, &am, &ag, &poll, nullptr);

    int calls = 0;
    QJsonArray received;
    p.setPlcApplier([&](const QJsonArray& cfg) {
        ++calls;
        received = cfg;
    });

    QVERIFY(p.loadFromFile(src));

    QVERIFY2(calls == 1, "加载工程应当调用 applier 恰好一次。多次调用 = 每次建一批 driver。");
    QCOMPARE(received.size(), 2);
    // ★ 逐台比对 id 和地址 —— 只比数量的话,3 台全填成同一个地址也能过
    QCOMPARE(received.at(0).toObject()["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(received.at(0).toObject()["host"].toString(), QStringLiteral("192.168.1.10"));
    QCOMPARE(received.at(1).toObject()["id"].toString(), QStringLiteral("plc2"));
    QCOMPARE(received.at(1).toObject()["host"].toString(), QStringLiteral("192.168.2.20"));
}

void tst_ProjectMultiPlc::plcApplier_receivesValidatedConfigNotRawArray() {
    // ★ 传给 applier 的必须是【校验后】的 plcs_,不是原始数组。
    //   传原始的话,校验没过的那几项也会被拿去建连接 ——
    //   而 host 为空的 PLC 连不上,现场只会看到它一直重连,
    //   用户完全不知道"这一项在加载时就已经被判定为无效了"。
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    QJsonObject bad = makePlc("plc2", QStringLiteral(""), 502);   // 空 host
    plcs.append(bad);

    const QString src = writeProject(plcs, "validated.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    QJsonArray received;
    p.setPlcApplier([&](const QJsonArray& cfg) { received = cfg; });

    QVERIFY(p.loadFromFile(src));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.at(0).toObject()["id"].toString(), QStringLiteral("plc1"));
}

void tst_ProjectMultiPlc::plcApplier_notInvokedWhenLoadFails() {
    // ★ 加载失败【绝对不能】建连接。
    //   失败意味着用户的变量/报警/画面全都没进来 ——
    //   这时候如果照着文件里的地址把 PLC 连上了,用户会看到
    //   "程序明明没加载成功,PLC 却已经在轮询了",完全对不上账,
    //   而且他根本不知道自己的工程文件哪里有问题。
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));

    const QString src = writeProject(plcs, "ok.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    int calls = 0;
    p.setPlcApplier([&](const QJsonArray&) { ++calls; });

    QVERIFY(p.loadFromFile(src));
    QCOMPARE(calls, 1);

    // 这次加载一个不存在的文件
    calls = 0;
    QVERIFY(!p.loadFromFile(m_dir.filePath("根本不存在的文件.json")));
    QCOMPARE(calls, 0);
}

void tst_ProjectMultiPlc::slaveId_absentInFile_isNotGeneratedOnSave() {
    // ★★★ 恒等往返的实战考验。
    //
    //   2b 让 slave_id 变成一个真正的字段(可配、进 Modbus 帧的 unit id)。
    //   最易犯的错:保存时"顺手"把缺省的 slave_id=1 补进去。
    //   那会在用户的版本控制里冒出一片"我什么都没改"的 diff ——
    //   而这类工程文件恰恰是现场调试记录的一部分,改动会淹没在噪声里。
    //
    //   saveToFile 是【硬约束:一个字都不许动】。这条用例就是那道约束的守卫:
    //   有人哪天觉得"补上默认值更规范"而动手,这里立刻红。
    QJsonArray plcs;
    QJsonObject noSlave = makePlc("plc1", "192.168.1.10", 502);
    plcs.append(noSlave);                       // 注意:【没有】slave_id 字段
    QCOMPARE(noSlave.contains(QStringLiteral("slave_id")), false);

    const QString src = writeProject(plcs, "slave.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));

    const QString dst = m_dir.filePath("slave-saved.hmi.json");
    QVERIFY(p.saveToFile(dst));

    // 读回来逐字段比
    QFile f(dst);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray saved = root["plcs"].toArray();

    QCOMPARE(saved.size(), 1);
    QVERIFY2(!saved.at(0).toObject().contains(QStringLiteral("slave_id")),
             "★ 文件里没写 slave_id,保存时【不许】补上缺省值。"
             "补了会在用户版本控制里冒出一片\"我什么都没改\"的 diff,"
             "而这类文件正是现场调试记录,改动会被噪声淹没。");

    // 反过来:文件里【写了】就必须原样写回(不能被抹成缺省)
    QJsonArray plcs2;
    QJsonObject withSlave = makePlc("plc1", "192.168.1.10", 502);
    withSlave["slave_id"] = 9;
    plcs2.append(withSlave);

    const QString src2 = writeProject(plcs2, "slave2.hmi.json");
    VariableManager vm2;  AlarmManager am2;  ActionManager ag2;
    PollScheduler poll2(&vm2, nullptr);
    Project p2(&vm2, &am2, &ag2, &poll2, nullptr);
    QVERIFY(p2.loadFromFile(src2));

    const QString dst2 = m_dir.filePath("slave2-saved.hmi.json");
    QVERIFY(p2.saveToFile(dst2));

    QFile f2(dst2);
    QVERIFY(f2.open(QIODevice::ReadOnly));
    const QJsonObject root2 = QJsonDocument::fromJson(f2.readAll()).object();
    QCOMPARE(root2["plcs"].toArray().at(0).toObject().value(QStringLiteral("slave_id")).toInt(),
             9);
}

// ============================================================================
// ★★★ 删掉一个被报警引用的变量,存出来的工程【必须还能打开】
//
// 【今天(写这条用例时)的真实行为】——★ 这段被改过两次,留个痕迹
//
//   我在动手之前写进计划的判断是:"删变量的那一刻,VariableManager 广播
//   variableReplaced(v, nullptr),ThresholdAlarmChecker::onVariableReplaced
//   把 variable_ 置成 nullptr,于是存盘时写出空串,下次打不开"。
//
//   ★【错的】这个判断建立在一个错误前提上:以为那个槽有人在 connect。
//     全仓库搜下来,ThresholdAlarmChecker::onVariableReplaced
//     【从来没有被 connect 过】—— hmi.cpp 里那条连接在 E#29 改成
//     "按名查"时被删了,槽就成了孤儿(这一点 CHANGELOG.md:228 早就记过)。
//
//     所以 variable_ 在变量被删之后仍然【非空】,只是指着已释放的内存。
//     而 Project.cpp:686 那一行
//         tc->variable() ? tc->variable()->name() : QString()
//     三元判的是"指针是不是空",悬空指针【不是空】 → 进 true 分支
//     → 解引用已释放内存。
//
//     实测:Exception code 0xc0000005,整个测试进程当场死掉(exit code 5),
//     栈是 Variable::name() ← Project::saveToFile() ← 本用例。
//     【不是"存出来的工程打不开",是存的那一下就崩。】
//
//   第一处修复(2026-10-19):variable_ 改成 QPointer<Variable> —— 不依赖
//   "有人记得 connect",对象一被 delete 指针自己就变空。崩没有了。
//
// 【现在钉住的是另一半:文件还是坏的】
//   改成 QPointer 之后,存盘不再崩,但写出的是 `"variable": ""`,
//   下次加载仍然 findVariable("") 失败 → 工程打不开。
//   这一半要靠"删除前先查引用"堵(阶段 1),不是靠改存盘那一行。
//
//   这条链今天【没有任何 UI 能触发】—— 没有界面能删变量。
//   但它是一个已经存在的坑:手改 JSON 删掉一个变量名就能踩到,
//   而且症状是"我明明存成功了,怎么打不开了",现场很难往回想。
//   变量管理界面(下一个提交)做完之后,它会变成"点一下删"的直接后果。
//
// 【这条钉的是哪一头】
//   断言的重心是【存出来的文件能不能重新加载】,而不是"变量字段是不是空串"。
//   因为空串只是症状,"打不开"才是用户真正承受的后果 ——
//   而且只断言空串的话,将来有人把 :686 的三元去掉改成别的写法就绕过去了,
//   但用户照样打不开工程。
// ============================================================================
void tst_ProjectMultiPlc::deleteReferencedVariable_savedProjectStillLoads() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));

    QJsonObject root;
    root["schema_version"] = 3;
    QJsonObject v;
    v["name"]       = QStringLiteral("Temp");
    v["address"]    = 100;
    v["data_type"]  = QStringLiteral("Int16");
    v["access"]     = QStringLiteral("ReadOnly");
    v["plc_id"]     = QStringLiteral("plc1");
    root["variables"] = QJsonArray{v};

    QJsonObject alarm;
    alarm["name"]      = QStringLiteral("TempHigh");
    alarm["variable"]  = QStringLiteral("Temp");
    alarm["level"]     = QStringLiteral("Warning");
    alarm["direction"] = QStringLiteral("Above");
    alarm["threshold"] = 80.0;
    root["alarms"] = QJsonArray{alarm};
    root["plcs"] = plcs;

    const QString src = writeRoot(root, "delvar.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));
    QCOMPARE(am.checkers().size(), 1);

    // ★ 删掉那个被引用的变量 —— 这一步今天没有任何界面会做,
    //   所以这条用例是整个文件里唯一一条"从外面捅进来"的用例。
    QVERIFY(vm.removeVariable(QStringLiteral("Temp")));
    QCOMPARE(vm.count(), 0);

    const QString dst = m_dir.filePath("delvar-saved.hmi.json");
    QVERIFY(p.saveToFile(dst));

    // ★★★ 真正的断言:存出去的东西,自己必须读得回来。
    //   这跟上面 saveAndReload_threePlcs_allSurvive 是同一条原则,
    //   只不过那次验的是"数据别丢",这次验的是"文件别坏"。
    VariableManager vm2;  AlarmManager am2;  ActionManager ag2;
    PollScheduler poll2(&vm2, nullptr);
    Project p2(&vm2, &am2, &ag2, &poll2, nullptr);
    const bool loaded = p2.loadFromFile(dst);

    // 把中间产物也读出来,好让失败消息能说清是哪一步坏的
    QFile f(dst);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
    const QString varRef = saved["alarms"].toArray().at(0).toObject()
                               .value(QStringLiteral("variable")).toString();

    QVERIFY2(loaded,
             qPrintable(QStringLiteral(
                 "★ 删掉被报警引用的变量之后,存出来的工程【打不开】了。\n"
                 "  存出来的 alarms[0].variable = \"%1\"(本该是变量名 \"Temp\")。\n"
                 "  空串会让下次加载在 Project.cpp:556 直接 failWith ——\n"
                 "  用户看到的是\"我明明存成功了,怎么打不开了\",而且无从推断。\n"
                 "  修法:存盘前要么拒绝这种状态,要么把悬空的报警一并处理掉。")
                 .arg(varRef)));
}

// ★★★ 同一条链,走 actions 那一侧。
//
//   【为什么必须单独一条,而不是把 alarms 那条的断言抄一遍】
//   变量被引用有两个【完全独立】的来源,它们坏的方式不一样:
//     · alarms  :存盘时写的是 tc->variable()->name(),变量没了 → 变【空串】
//     · actions :存盘时写的是 m.variableName(ActionManager 存的是【字符串】,
//                见 ActionManager.h:64 和 E #29),变量没了它【不会被清空】,
//                于是文件里留下的是 "Temp" —— 一个指向不存在变量的名字。
//   后果一样致命(加载时 findVariable 拿不到 → failWith → 整个工程打不开),
//   但机制完全不同。只查 alarms 的话,修完 alarms 那条,actions 这条照样坏着,
//   而它坏的方式还更隐蔽:文件里看起来有个正常的变量名,不是空串,
//   肉眼看 JSON 完全看不出哪里坏了。
//
//   ★ 顺带纠正一处我自己的判断错误:写这条用例之前我以为
//     "actions 那条顶多是联动失效,不致命" —— 错的。
//     联动失效说的是【运行期】;而【存盘/加载】这条路一样走 findVariable,
//     一样 failWith。这条用例就是那个判断错误的证据。
// ============================================================================
void tst_ProjectMultiPlc::deleteVariableReferencedByAction_savedProjectStillLoads() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));

    QJsonObject root;
    root["schema_version"] = 3;
    QJsonObject v;
    v["name"]      = QStringLiteral("Valve");
    v["address"]   = 200;
    v["data_type"] = QStringLiteral("Int16");
    v["access"]    = QStringLiteral("ReadWrite");
    v["plc_id"]    = QStringLiteral("plc1");
    root["variables"] = QJsonArray{v};

    // 先有一条报警 —— actions 段在加载时要求它引用的报警存在
    QJsonObject alarm;
    alarm["name"]      = QStringLiteral("ValveOpen");
    alarm["variable"]  = QStringLiteral("Valve");
    alarm["level"]     = QStringLiteral("Warning");
    alarm["direction"] = QStringLiteral("Above");
    alarm["threshold"] = 1.0;
    root["alarms"] = QJsonArray{alarm};

    QJsonObject action;
    action["alarm_name"]   = QStringLiteral("ValveOpen");
    action["variable"]    = QStringLiteral("Valve");
    action["write_value"] = 0;
    root["actions"] = QJsonArray{action};
    root["plcs"] = plcs;

    const QString src = writeRoot(root, "delaction.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));
    QCOMPARE(ag.mappings().size(), 1);

    QVERIFY(vm.removeVariable(QStringLiteral("Valve")));

    const QString dst = m_dir.filePath("delaction-saved.hmi.json");
    QVERIFY(p.saveToFile(dst));

    VariableManager vm2;  AlarmManager am2;  ActionManager ag2;
    PollScheduler poll2(&vm2, nullptr);
    Project p2(&vm2, &am2, &ag2, &poll2, nullptr);
    const bool loaded = p2.loadFromFile(dst);

    QVERIFY2(loaded,
             "★ 删掉被联动引用的变量之后,存出来的工程【打不开】了。\n"
             "  和 alarms 那条不同:这里存出来的 variable 不是空串,\n"
             "  而是仍然存在的 \"Valve\" —— 一个指向已被删除变量的名字。\n"
             "  文件看起来完全正常,肉眼看 JSON 找不出哪里坏了,\n"
             "  但加载时 findVariable 拿不到它,整个工程直接拒绝加载。");
}

// ============================================================================
// ★★★ 钉住"修掉崩溃"这一步本身
//
// 【为什么上面两条还不够】
//   上面两条(存出来的工程打不开)在【今天】是红的,而它们红的方式
//   就是这个 bug —— 修好之后它们转绿,并不再覆盖崩溃那一半。
//   如果没人记得还有崩溃这一半,把 variable_ 改回裸指针是很自然的事
//   ("QPointer 在这里没必要,反正没人删变量"),改回去之后:
//     · 上面两条重新变红,但【变红的方式是进程崩溃】
//     · 0xc0000005 会让整个测试进程当场退出
//     · 后面所有用例一条都不跑,ctest 只看到一个 exit code 5
//   一次失败看不到全貌,是这个仓库反复吃过的那种"失败方式本身掩盖了别的失败"。
//
// 【所以这条的形状和上面两条【不一样】】
//   它【不】断言工程能不能打开 —— 那是上面两条的活。
//   它只断言"存盘这一步【不崩】",崩了 QtTest 会直接死,
//   所以"这条用例跑完了"本身就是通过。这就是它的全部价值。
// ============================================================================
void tst_ProjectMultiPlc::deleteReferencedVariable_savingDoesNotCrash() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));

    QJsonObject root;
    root["schema_version"] = 3;
    QJsonObject v;
    v["name"]       = QStringLiteral("Temp");
    v["address"]    = 100;
    v["data_type"]  = QStringLiteral("Int16");
    v["access"]     = QStringLiteral("ReadOnly");
    v["plc_id"]     = QStringLiteral("plc1");
    root["variables"] = QJsonArray{v};

    QJsonObject alarm;
    alarm["name"]      = QStringLiteral("TempHigh");
    alarm["variable"]  = QStringLiteral("Temp");
    alarm["level"]     = QStringLiteral("Warning");
    alarm["direction"] = QStringLiteral("Above");
    alarm["threshold"] = 80.0;
    root["alarms"] = QJsonArray{alarm};
    root["plcs"] = plcs;

    const QString src = writeRoot(root, "nocrash.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));

    QVERIFY(vm.removeVariable(QStringLiteral("Temp")));

    // ★ 存盘:【不许】崩。
    //   崩了的话这一行之后什么都执行不到,QtTest 直接以 0xc0000005 退出 ——
    //   那正是 QPointer 改成裸指针之后会发生的事(2026-10-19 实测)。
    //   返回值【不断言】:存出来的文件是坏的还是好的,归上面两条管。
    const bool saved = p.saveToFile(m_dir.filePath("nocrash-saved.hmi.json"));

    // 走到这里就说明没崩。留一句日志让"这条为什么通过"在 CI 输出里看得见,
    // 免得将来有人看到一条不做断言的用例,以为是没写完。
    qInfo() << "[nocrash] 删掉被引用变量后存盘未崩溃,saved =" << saved;
}

// ============================================================================
// ★★★ 修"存坏文件"的那一刀,不能顺手把好的也砍了
//
// 【这条在防什么】
//   修法是"存盘时跳过悬空的引用"。这句话有两面,上面两条用例只钉了第一面:
//     · 悬空的那条【必须】跳过 —— 否则整个工程打不开
//     · 没悬空的那条【必须】留下 —— 否则就成了"存盘清空报警和联动"
//
// 【为什么必须专门钉第二面】
//   上面两条只断言"存出来的工程能打开"。
//   而"什么都不写"【也】满足这个断言 —— 空的 alarms 数组在加载时
//   循环体一次都不进,工程照样打得开。
//   ⇒ 一个"把所有报警和联动统统跳过"的实现,能让上面两条全绿,
//     而用户的报警和联动被清光了 —— 他要等到现场不报警了才发现。
//
//   这跟本文件开头写的"读回来数量对不对"是同一类假绿:
//   只断言"能打开"就漏掉了"东西还在不在"。
//
// 【钉的是什么】
//   两个变量、两条报警、两条联动,删掉其中一个变量:
//     · 挂在它身上的那条报警和那条联动 → 必须被跳过
//     · 挂在另一个(还活着)变量上的那两条 → 必须一条不少地留下
//   而且留下的必须【就是】活的那条(不是"随便哪条都行")
// ============================================================================
void tst_ProjectMultiPlc::danglingReference_isSkipped_butValidOnesSurvive() {
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));

    QJsonObject root;
    root["schema_version"] = 3;

    QJsonObject vA;
    vA["name"]      = QStringLiteral("Alpha");
    vA["address"]   = 100;
    vA["data_type"] = QStringLiteral("Int16");
    vA["access"]    = QStringLiteral("ReadOnly");
    vA["plc_id"]    = QStringLiteral("plc1");
    QJsonObject vB;
    vB["name"]      = QStringLiteral("Beta");
    vB["address"]   = 101;
    vB["data_type"] = QStringLiteral("Int16");
    vB["access"]    = QStringLiteral("ReadOnly");
    vB["plc_id"]    = QStringLiteral("plc1");
    root["variables"] = QJsonArray{vA, vB};

    QJsonObject aA;
    aA["name"]      = QStringLiteral("AlphaHigh");
    aA["variable"]  = QStringLiteral("Alpha");
    aA["level"]     = QStringLiteral("Warning");
    aA["direction"] = QStringLiteral("Above");
    aA["threshold"] = 80.0;
    QJsonObject aB;
    aB["name"]      = QStringLiteral("BetaHigh");
    aB["variable"]  = QStringLiteral("Beta");
    aB["level"]     = QStringLiteral("Warning");
    aB["direction"] = QStringLiteral("Above");
    aB["threshold"] = 80.0;
    root["alarms"] = QJsonArray{aA, aB};

    QJsonObject acA;
    acA["alarm_name"]   = QStringLiteral("AlphaHigh");
    acA["variable"]    = QStringLiteral("Alpha");
    acA["write_value"] = 1;
    QJsonObject acB;
    acB["alarm_name"]   = QStringLiteral("BetaHigh");
    acB["variable"]    = QStringLiteral("Beta");
    acB["write_value"] = 2;
    root["actions"] = QJsonArray{acA, acB};

    root["plcs"] = plcs;

    const QString src = writeRoot(root, "mixed.hmi.json");
    QVERIFY2(!src.isEmpty(), "写源工程失败");

    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));
    QCOMPARE(am.checkers().size(), 2);
    QCOMPARE(ag.mappings().size(), 2);

    // 删掉 Beta —— 挂它身上的那条报警和那条联动会悬空
    QVERIFY(vm.removeVariable(QStringLiteral("Beta")));

    const QString dst = m_dir.filePath("mixed-saved.hmi.json");
    QVERIFY(p.saveToFile(dst));

    VariableManager vm2;  AlarmManager am2;  ActionManager ag2;
    PollScheduler poll2(&vm2, nullptr);
    Project p2(&vm2, &am2, &ag2, &poll2, nullptr);
    QVERIFY2(p2.loadFromFile(dst), "★ 存出来的工程打不开了(悬空引用没被跳过)");

    // ★ 活的那些必须一条不少
    QCOMPARE(am2.checkers().size(), 1);
    QCOMPARE(ag2.mappings().size(), 1);

    // 而且留下的必须【就是】活的那条 —— 不是"随便哪条都行"
    AlarmChecker* kept = am2.checkers().first();
    QCOMPARE(kept->name(), QStringLiteral("AlphaHigh"));
    auto* ktc = dynamic_cast<ThresholdAlarmChecker*>(kept);
    QVERIFY(ktc != nullptr);
    QVERIFY(ktc->variable() != nullptr);
    QCOMPARE(ktc->variable()->name(), QStringLiteral("Alpha"));

    QCOMPARE(ag2.mappings().first().alarmName, QStringLiteral("AlphaHigh"));
    QCOMPARE(ag2.mappings().first().variableName, QStringLiteral("Alpha"));
}

// ============================================================================
// ---- 引用扫描(阶段 1 的正题)----
//
// 【这些用例在防什么】
//   界面上要做"有引用就不让删",前提是这个查询【真的查得准】。
//   查询有三种坏法,而且都很难自查:
//     ① 查得到有的、查不到没有的 —— 少报,用户以为删干净了
//     ② 两个来源只查一个 —— 联动那条特别容易漏(它存的是字符串不是指针)
//     ③ 查不到的那部分【不说】 —— 最危险,见 refScan_alwaysDisclosesScreenBlindSpot
// ============================================================================

// 造一个"变量 + 一条监控它的阈值报警"
static void makeAlarmOn(VariableManager* vm, AlarmManager* am,
                        const QString& varName, const QString& alarmName) {
    auto* v = new Variable(varName, quint16(100), Variable::DataType::Int16,
                           Variable::Access::ReadOnly, vm);
    vm->addVariable(v);
    CheckerConfig cfg;
    cfg.level     = AlarmLevel::Warning;
    cfg.direction = Direction::Above;
    cfg.threshold = 80.0;
    am->addChecker(new ThresholdAlarmChecker(alarmName, v, nullptr, cfg));
}

void tst_ProjectMultiPlc::refScan_findsThresholdAlarmReference() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    makeAlarmOn(&vm, &am, QStringLiteral("Temp"), QStringLiteral("TempHigh"));

    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Temp"), &am, &ag);

    QVERIFY(r.hasRefs());
    QCOMPARE(r.refs.size(), 1);
    QCOMPARE(r.refs.first().kind, VariableRef::Kind::Alarm);
    QCOMPARE(r.refs.first().source, QStringLiteral("TempHigh"));
    // ★ describe() 要能直接显示 —— 它会被原样塞进对话框的提示框
    QVERIFY2(r.refs.first().describe().contains(QStringLiteral("TempHigh")),
             qPrintable(r.refs.first().describe()));
}

// ★ 联动这一侧:存的是【字符串】不是指针,比的是字符串。
//   之前那句"联动顶多是运行期失效"的判断就是漏了这一侧 —— 见上面那条用例注释。
void tst_ProjectMultiPlc::refScan_findsActionReference() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    auto* v = new Variable(QStringLiteral("Valve"), quint16(200),
                           Variable::DataType::Int16,
                           Variable::Access::ReadWrite, &vm);
    vm.addVariable(v);
    ag.addMapping(QStringLiteral("ValveOpen"), QStringLiteral("Valve"), 0);

    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Valve"), &am, &ag);

    QVERIFY(r.hasRefs());
    QCOMPARE(r.refs.size(), 1);
    QCOMPARE(r.refs.first().kind, VariableRef::Kind::Action);
    // 联动的来源是【触发它的报警名】,不是变量名
    QCOMPARE(r.refs.first().source, QStringLiteral("ValveOpen"));
    QVERIFY2(r.refs.first().describe().contains(QStringLiteral("ValveOpen")),
             qPrintable(r.refs.first().describe()));
}

// ★ 两个来源同时存在时【两条都要报】。
//   写成"二选一"的话,界面上会告诉用户"只有报警在用",他就去把报警删了,
//   然后联动的名字还留在文件里 —— 下一条联动照样把工程写坏。
void tst_ProjectMultiPlc::refScan_findsBothSourcesAtOnce() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    makeAlarmOn(&vm, &am, QStringLiteral("Temp"), QStringLiteral("TempHigh"));
    ag.addMapping(QStringLiteral("TempHigh"), QStringLiteral("Temp"), 0);

    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Temp"), &am, &ag);

    QCOMPARE(r.refs.size(), 2);
    int alarms = 0, actions = 0;
    for (const VariableRef& x : r.refs) {
        if (x.kind == VariableRef::Kind::Alarm)  ++alarms;
        if (x.kind == VariableRef::Kind::Action) ++actions;
    }
    QCOMPARE(alarms, 1);
    QCOMPARE(actions, 1);
}

// 反向:该说"没人用"的时候必须真说。
//   只测"查得到有"的话,一个永远返回第一条引用的实现也能全绿 ——
//   而那种实现在界面上表现为"什么都删不掉",用户会以为程序坏了。
void tst_ProjectMultiPlc::refScan_unreferenced_isEmpty() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    makeAlarmOn(&vm, &am, QStringLiteral("Temp"), QStringLiteral("TempHigh"));

    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("SomethingElse"), &am, &ag);

    QVERIFY2(!r.hasRefs(), "Temp 被引用,但 SomethingElse 没人用,不该报出来");
}

// ★★★ 这条钉的是"诚实"本身,不是功能。
//
//   【它防的事故】将来有人觉得那句"画面控件查不到"是啰嗦,
//   清理代码时把它删掉 —— 代码里不会红,测试全绿,一切看起来更干净。
//   而界面上会开始显示"无引用,可以删",用户点了删,
//   画面上那个显示温度的表【静默定格在上一次读到的值,不报错】,
//   工程操作员看着一个已经不再更新的读数以为那是现场实际值。
//
//   工业场景下这是能出事的:画面上那个数就是操作员做判断的依据。
//   所以这条用例存在的意义是:让"承认自己看不见"这件事本身
//   变成一条【删不掉】的断言。
void tst_ProjectMultiPlc::refScan_alwaysDisclosesScreenBlindSpot() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    // ★ 故意【不】造任何引用 —— 一个变量都没人用,是最容易"看起来很干净"的情况
    auto* v = new Variable(QStringLiteral("Lonely"), quint16(1),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly, &vm);
    vm.addVariable(v);

    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Lonely"), &am, &ag);

    QVERIFY2(!r.hasRefs(), "这个变量确实没人引用");

    // ★ 但即便如此,扫描【仍然】必须说出"画面控件查不到"
    const QString joined = r.blindSpots.join('\n');
    QVERIFY2(joined.contains(QStringLiteral("画面")),
             qPrintable(QStringLiteral(
                 "★ 画面控件绑定查不到这件事必须被说出来。\n"
                 "  ControlBase::bind() 存 QPointer 但不监听 variableReplaced,\n"
                 "  变量被删时绑它的控件静默定格在最后一个值、不报错。\n"
                 "  而界面上如果不写这句,用户会以为已经看全了。\n"
                 "  当前 blindSpots 内容:%1").arg(joined)));
}

// ★ 非阈值报警要被【点名】,不能静默当成"没有引用"。
//   今天 WriteFailChecker 确实不引用任何变量,所以"查不到"恰好等于"没有" ——
//   但那只是【当前恰好如此】,不是接口保证:
//   基类 AlarmChecker 没有 variable() getter,类型系统答不了这个问题。
//   差一条明确列出来的名字,将来哪个子类开始引用变量就会静默漏掉。
void tst_ProjectMultiPlc::refScan_namesUnscannableCheckers() {
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    auto* v = new Variable(QStringLiteral("Temp"), quint16(100),
                           Variable::DataType::Int16,
                           Variable::Access::ReadOnly, &vm);
    vm.addVariable(v);

    // 造一个【不是】阈值报警的 checker
    auto* wf = new WriteFailChecker();
    wf->setName(QStringLiteral("WriteFail"));
    am.addChecker(wf);

    const QStringList unscannable = unscannableCheckers(&am);
    QCOMPARE(unscannable.size(), 1);
    QCOMPARE(unscannable.first(), QStringLiteral("WriteFail"));

    // 并且它必须出现在扫描结果的 blindSpots 里(而不只是"有个函数能列出来")
    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Temp"), &am, &ag);
    QVERIFY2(r.blindSpots.join('\n').contains(QStringLiteral("WriteFail")),
             qPrintable(r.blindSpots.join('\n')));

    // 反向:阈值报警【不该】被列进"查不到"里
    //   列错了会让界面提示"以下报警类型无法检查",把好好的报警说成可疑
    makeAlarmOn(&vm, &am, QStringLiteral("Temp2"), QStringLiteral("Temp2High"));
    const QStringList unscannable2 = unscannableCheckers(&am);
    QVERIFY2(!unscannable2.contains(QStringLiteral("Temp2High")),
             "阈值报警是有 variable() 的,不该被列成查不到");
}

// ★ 只给一半的 manager,另一半必须被报成"未检查",不能当作"没有引用"。
//   这两种"空"的含义完全不同:
//     · "扫过了,没人引用" → 可以放心删
//     · "根本没看"       → 不能放心删
//   混为一谈的话,界面上就会在拿不到 AlarmManager 的场景里
//   显示"无引用",而实际上有一整层引用从没被检查过。
void tst_ProjectMultiPlc::refScan_missingManagerIsReportedNotAssumedClean() {
    VariableManager vm;  AlarmManager am;
    makeAlarmOn(&vm, &am, QStringLiteral("Temp"), QStringLiteral("TempHigh"));

    // 故意不传 ActionManager
    const VariableRefScanResult r =
        scanVariableRefs(QStringLiteral("Temp"), &am, nullptr);

    // 报警这一侧照常查得到
    QCOMPARE(r.refs.size(), 1);
    // 联动那一侧必须被说成"未检查"
    QVERIFY2(r.blindSpots.join('\n').contains(QStringLiteral("未检查")),
             qPrintable(r.blindSpots.join('\n')));
}

QTEST_MAIN(tst_ProjectMultiPlc)
#include "tst_ProjectMultiPlc.moc"

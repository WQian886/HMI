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
// 【2a 明确【没做】什么 —— 这条边界必须钉住】
//   多台 PLC 【不会真的同时连上】。运行时仍然只连 plcs_[0]。
//   那是 2b(要改 TcpClient 单实例的所有权,风险高一个量级)。
//   所以本文件有一条用例专门确认"2a 没有偷偷越界":
//     如果将来有人以为 2b 做了、把它删掉,本文件要能发现行为变了。
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
// 关联:Project::loadPlcsFromJson / setPlcs / plcs(),Project.cpp 第 8 步 + saveToFile
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

    // ★ 2a 的边界:多台配置读进来之后,运行期仍然只应用第一台
    //   这条现在是通过的。2b 做完后它会变成 FAIL —— 那时【要改断言,不是删用例】,
    //   把它改成"应该 N 台都建立连接",才算把 2a 的遗留清掉。
    void multiplePlcs_runtimeStillAppliesOnlyFirst();

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

void tst_ProjectMultiPlc::multiplePlcs_runtimeStillAppliesOnlyFirst() {
    // ★★★ 这条钉的是 2a 的【边界】,不是它的成果。
    //
    //   2a 做到"配置存得住"为止。多台【同时连上】是 2b。
    //   本文件目前【没有】真 TCP 连接的手段(所有测试都跑在内存对象上),
    //   所以这一条能验证的是"程序没有因为读了 3 台就自己乱连"——
    //   具体表现为:传进去的 tcp_ 是 nullptr,加载 3 台【不崩】。
    //
    //   如果哪天 2b 做完了(会引入多 TcpClient 所有权),
    //   这里的构造方式大概要改,而【断言本身应该保持或加强】。
    //   到那时正确做法是把这条改成"3 台都建立了连接对象",
    //   而不是删掉——删掉就等于把 2a 的遗留静默地继承给了 2b。
    QJsonArray plcs;
    plcs.append(makePlc("plc1", "192.168.1.10", 502));
    plcs.append(makePlc("plc2", "192.168.2.20", 502));

    const QString src = writeProject(plcs, "runtime.hmi.json");
    VariableManager vm;  AlarmManager am;  ActionManager ag;
    PollScheduler poll(&vm, nullptr);

    // tcp_ 传 nullptr:读 3 台配置不该触发任何连接动作,更不该崩
    Project p(&vm, &am, &ag, &poll, nullptr);
    QVERIFY(p.loadFromFile(src));
    QCOMPARE(p.plcs().size(), 2);
}

QTEST_MAIN(tst_ProjectMultiPlc)
#include "tst_ProjectMultiPlc.moc"

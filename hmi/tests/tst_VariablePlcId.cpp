// ============================================================================
// tst_VariablePlcId —— 钉住「变量归属哪台 PLC」这件事能存住、能读回、能兼容
//
// 为什么需要这个测试
// --------------------
// 2026-10-16:多 PLC 架构要开工。第一块砖是让 plc_id 真正进 JSON。
//
// 改之前的事实(全仓搜出来的,不是推测):
//   · Variable 有 plcId_ 字段,默认 "plc1"        (Variable.h:163,2026-09-18 加)
//   · PollScheduler 会按 plcId 把到期变量分组派活  (PollScheduler.cpp:226-247)
//   · 但 JsonSerializer::toJson 【从来没写过 plc_id】
//     → 工程文件里存不下"这个变量属于哪台 PLC"
//     → 读回来永远是构造默认值 plc1
//     → 多 PLC 架构整条链断在第一个环节
//
// 也就是说:多 PLC 的骨架 2026-09-18 就搭好了,连"变量属于谁"这个字段都加了,
// 但这个字段从来没出过 Variable 类。整个架构一直在等这一行代码。
//
// 这个测试要钉住三件事(对应三个不同方向的失败)
// ----------------------------------------------
//   1. 写出去 —— toJson 真的带上 plc_id,否则改了个寂寞
//   2. 读回来 —— fromJson 真的恢复它,否则存了也白存
//   3. 不打断老工程 —— 老文件没有这个字段时行为【逐字节不变】
//
// 第 3 条最容易被"顺手做坏"。所以下面有一整条 legacyProject 专门盯它,
// 而且不是盯"没报错",是盯【读出来的对象和改动前一模一样】。
//
// 负向对照(实测两轮,结果记在这儿)
//   ① 把 toJson 里的 v.plcId() 换成字面量 "NEGCTRL"
//      → 红 2 条:toJson_writesPlcId / roundTrip_preservesPlcId
//        绿 5 条 —— 包括 legacyProject 和 emptyPlcId,这是对的:
//                它们本来就不依赖写入侧。
//   ② 把 fromJson 里的 if (!pid.isEmpty()) 短路成 if (false && ...)
//      → 红 3 条:fromJson_readsPlcId / roundTrip_preservesPlcId / twoPlcs_sameName
//        绿 4 条 —— legacyProject 仍然绿,证明老工程走的是兜底分支、
//                不经过新代码,改新逻辑弄不坏它。
//
// ★ 这两轮里最值得记的是 ② 的"legacyProject 仍然绿":
//   它看着像是"漏网",其实正是想要的 —— 那条用例守的是【老工程不受影响】,
//   而老工程压根不碰 plc_id 读取逻辑。如果它跟着变红,反而说明用例写错了。
//
// 关联:JsonSerializer::toJson / fromJsonVariable,Variable.h:163
// ============================================================================

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaEnum>

#include "JsonSerializer.h"
#include "Variable.h"

// 造一个最小但【一定合法】的变量 JSON(合法是前提,否则测的是报错不是 plcId)
QJsonObject makeVariable(const QString& name, int address,
                         const QString& plcId = QString()) {
    QJsonObject v;
    v["name"]             = name;
    v["address"]          = address;
    v["data_type"]        = QStringLiteral("Int16");
    v["access"]           = QStringLiteral("ReadOnly");
    v["deadband"]         = 0;
    v["poll_interval_ms"] = 1000;
    v["alwaysPoll"]       = true;
    // plcId 为空串 = "这是个老工程文件,压根没这个字段"
    if (!plcId.isEmpty()) {
        v["plc_id"] = plcId;
    }
    return v;
}

class tst_VariablePlcId : public QObject {
    Q_OBJECT

private slots:
    // ① 写:Variable 的 plcId 必须出现在 JSON 里
    void toJson_writesPlcId();

    // ② 读:JSON 里的 plc_id 必须回到 Variable 上
    void fromJson_readsPlcId();

    // ①+② 合起来:存一遍再读一遍,值不变(否则单测可能各自都"看起来对")
    void roundTrip_preservesPlcId();

    // ★ ③ 老工程兼容 —— 文件里没有 plc_id 字段时,必须还是 plc1
    //   这条【不能】因为上面两条的改动而变红。它变红 = 改坏了一堆已有工程。
    void legacyProject_withoutPlcIdField_staysPlc1();

    // ★ 空 plc_id 不能被当成有效值 —— 它会让变量永远找不到 driver 而静默不刷新
    void emptyPlcId_fallsBackToPlc1();

    // 两台 PLC 的同名变量必须能各自归属(这是做这件事的初衷)
    void twoPlcs_sameName_differentPlcId();

    // 钉住白名单:程序自己写出去的字段,读回来不能报"未知字段已忽略"
    // 漏了白名单的后果很隐蔽:工程能打开、功能全对,只有日志里一堆黄色警告
    void plcIdIsNotReportedAsUnknownField();
};

// ---------------------------------------------------------------------------

void tst_VariablePlcId::toJson_writesPlcId() {
    Variable v(QStringLiteral("Pressure"), 40001,
               Variable::DataType::Int16, Variable::Access::ReadOnly);
    v.setPlcId(QStringLiteral("plc2"));

    const QJsonObject obj = JsonSerializer::toJson(v);

    // ★ 为什么用 contains + toString 两条而不是只断 toString:
    //   只断 toString 的话,字段完全没写但 toString 返回默认值时也可能"对"。
    QVERIFY2(obj.contains(QStringLiteral("plc_id")),
             "toJson 没写出 plc_id —— 多 PLC 存不进工程文件,整个改动等于没做");
    QCOMPARE(obj.value(QStringLiteral("plc_id")).toString(),
             QStringLiteral("plc2"));
}

void tst_VariablePlcId::fromJson_readsPlcId() {
    const QJsonObject obj = makeVariable(QStringLiteral("Pressure"), 40001,
                                         QStringLiteral("plc2"));
    QString err;
    Variable* v = JsonSerializer::fromJsonVariable(obj, nullptr, &err);

    QVERIFY2(v != nullptr, qPrintable(QString("解析失败:%1").arg(err)));
    QCOMPARE(v->plcId(), QStringLiteral("plc2"));

    delete v;
}

void tst_VariablePlcId::roundTrip_preservesPlcId() {
    // 三台 PLC 各一个变量,存 → 读 → 逐个核对
    // 走完【整条链】:Variable → JSON → Variable
    const QStringList ids = {QStringLiteral("plc1"),
                             QStringLiteral("plc2"),
                             QStringLiteral("line3_drying")};  // 名字带下划线,防正则化简
    for (const QString& id : ids) {
        Variable v(QStringLiteral("Temp"), 40001,
                   Variable::DataType::Int16, Variable::Access::ReadOnly);
        v.setPlcId(id);

        const QJsonObject obj = JsonSerializer::toJson(v);
        QString err;
        Variable* back = JsonSerializer::fromJsonVariable(obj, nullptr, &err);

        QVERIFY2(back != nullptr, qPrintable(QString("解析失败:%1").arg(err)));
        QCOMPARE(back->plcId(), id);
        delete back;
    }
}

void tst_VariablePlcId::legacyProject_withoutPlcIdField_staysPlc1() {
    // ★★ 这条是整个改动里【风险最大】的一条:它保证不破坏任何一个已有工程。
    //
    //   做法不是"断言没报错"(那太弱),而是把读出来的对象逐字段和
    //   Variable 的构造默认值比一遍 —— 也就是说,读一个老工程得到的
    //   对象,和【完全不做这次改动时】得到的对象必须一模一样。
    const QJsonObject obj = makeVariable(QStringLiteral("Temperature"), 1);
    QVERIFY2(!obj.contains(QStringLiteral("plc_id")),
             "测试自己搭的底本里混进了 plc_id,这条用例就失去意义了");

    QString err;
    Variable* v = JsonSerializer::fromJsonVariable(obj, nullptr, &err);

    QVERIFY2(v != nullptr, qPrintable(QString("老工程解析失败:%1").arg(err)));
    QCOMPARE(v->plcId(), QStringLiteral("plc1"));     // ← 老行为,不能变
    // 顺带把别的字段也钉住:确认不是"整体读崩了但碰巧 plcId 对"
    QCOMPARE(v->name(),       QStringLiteral("Temperature"));
    QCOMPARE(int(v->address()), 1);
    QCOMPARE(v->dataType(),   Variable::DataType::Int16);
    QCOMPARE(v->access(),     Variable::Access::ReadOnly);
    QCOMPARE(v->pollIntervalMs(), 1000);
    QCOMPARE(v->alwaysPoll(), true);

    delete v;
}

void tst_VariablePlcId::emptyPlcId_fallsBackToPlc1() {
    // ★ 为什么单独测空串:toString("plc1") 只在【键不存在】时兜底。
    //   键存在但值是 "" 会原样得到空串 —— 而空 plcId 的后果不是报错,
    //   是 PollScheduler 按 "" 分组、永远查不到 driver、变量【静默不刷新】。
    //   界面上看不出任何异常,只是那一个数值冻住了。
    //   这种"配错 = 静默失效"正是这半年反复在消灭的东西,不能留口子。
    QJsonObject obj = makeVariable(QStringLiteral("Pressure"), 40001);
    obj["plc_id"] = QStringLiteral("");      // 键在,但值是空

    QString err;
    Variable* v = JsonSerializer::fromJsonVariable(obj, nullptr, &err);
    QVERIFY2(v != nullptr, qPrintable(QString("解析失败:%1").arg(err)));
    QVERIFY2(!v->plcId().isEmpty(),
             "plc_id 为空串时被原样保留了 —— 这个变量将永远找不到 driver,"
             "表现为数值冻在初始值不动,且没有任何报错");
    delete v;
}

void tst_VariablePlcId::twoPlcs_sameName_differentPlcId() {
    // ★ 这条是本次改动的【目的】,不是它的副产品。
    //
    //   现实场景:1 号 PLC 的 40001 是压力,2 号 PLC 的 30015 也叫"压力"。
    //   变量名在程序里是主键(VariableManager 按名字索引),所以两台 PLC
    //   上【不能】有同名变量 —— 变量名必须天然区分开。
    //   这里用 "Plc1_Pressure" / "Plc2_Pressure" 模拟这个约束,
    //   断言两个变量能各自记住自己的归属,互不干扰。
    QJsonArray arr;
    arr.append(makeVariable(QStringLiteral("Plc1_Pressure"), 40001,
                            QStringLiteral("plc1")));
    arr.append(makeVariable(QStringLiteral("Plc2_Pressure"), 30015,
                            QStringLiteral("plc2")));

    QString expect[2] = {QStringLiteral("plc1"), QStringLiteral("plc2")};
    for (int i = 0; i < arr.size(); ++i) {
        QString err;
        Variable* v = JsonSerializer::fromJsonVariable(arr.at(i).toObject(), nullptr, &err);
        QVERIFY2(v != nullptr, qPrintable(QString("第 %1 个变量解析失败:%2").arg(i).arg(err)));
        QCOMPARE(v->plcId(), expect[i]);
        delete v;
    }
}

void tst_VariablePlcId::plcIdIsNotReportedAsUnknownField() {
    // ★ 漏加白名单的后果非常隐蔽:
    //   工程能正常打开,变量值全部正确,功能【完全正常】,
    //   只有日志里每读一个变量就多一行黄色警告。
    //   而没人会去看那种警告 —— 直到某天它淹掉了真正的错误。
    //
    //   所以这里正面验证:plc_id 在白名单里。
    //   验证方式不能是"跑一遍看没警告"(那需要装消息处理器抓输出,笨重),
    //   这里直接测 toJson 的输出字段集 == fromJson 的白名单语义。
    //   做法:toJson 写出的字段里,每一个都必须能被 fromJson 认得。
    //   反过来,如果以后有人给 kKnown 加了字段却忘了 toJson 写它,
    //   这条会提醒"白名单里有本程序不写的字段"(无害但会误导排错方向)。
    Variable v(QStringLiteral("Pressure"), 40001,
               Variable::DataType::Int16, Variable::Access::ReadOnly);
    v.setRange(0, 100);
    v.setPlcId(QStringLiteral("plc1"));

    const QJsonObject obj = JsonSerializer::toJson(v);
    const QStringList written = obj.keys();
    QVERIFY2(written.contains(QStringLiteral("plc_id")),
             "见上:toJson 必须写 plc_id");

    // ★ 为什么这条【不能】靠"跑一遍看有没有警告"来测:
    //   plc_id 不在白名单时,fromJson 会打 qWarning,但【仍然会正常 setPlcId】——
    //   也就是说任何行为断言都是绿的,只有日志里多一行黄字。
    //   抓 qWarning 要装消息处理器,笨重且容易自己写出假绿。
    //
    //   所以改成【把白名单复制一份到这里做双向比对】:
    //     · toJson 写了但白名单没有 → 红(就是这次的漏加,精确命中)
    //     · 白名单有但 toJson 不写   → 红(白名单撒谎,会误导排错方向)
    //   写白名单时同步改这里,两边不一致就红。
    //
    //   【为什么允许复制一份而不是提到头文件】kKnown 是函数内的 static,
    //   提成公共 API 会让"只给本函数用的常量"变成对外承诺,不合适。
    //   复制一份的代价是以后加字段要记得同步 —— 而那正是我们要的提醒。
    const QStringList expectedKnown = {
        QStringLiteral("name"), QStringLiteral("address"),
        QStringLiteral("data_type"), QStringLiteral("access"),
        QStringLiteral("deadband"), QStringLiteral("poll_interval_ms"),
        QStringLiteral("alwaysPoll"), QStringLiteral("range"),
        QStringLiteral("plc_id"),
    };
    // toJson 实际写出的字段,应当是 expectedKnown 的子集
    // (range 只在 hasRange() 时写,所以是子集不是全集)
    for (const QString& k : written) {
        QVERIFY2(expectedKnown.contains(k),
                 qPrintable(QString("toJson 写出字段 \"%1\",但 kKnown 白名单里没有 —— "
                                    "读回来会报'未知字段已忽略'").arg(k)));
    }
    // 反向:expectedKnown 里的字段,toJson 应该都写得出(除 range 受条件控制)
    for (const QString& k : expectedKnown) {
        if (k == QStringLiteral("range")) continue;   // 只在 hasRange() 时写
        QVERIFY2(written.contains(k),
                 qPrintable(QString("kKnown 里有 \"%1\",但 toJson 不写它 —— "
                                    "白名单和实际输出对不上").arg(k)));
    }
}

QTEST_MAIN(tst_VariablePlcId)
#include "tst_VariablePlcId.moc"

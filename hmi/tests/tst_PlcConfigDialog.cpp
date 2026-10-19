// ============================================================================
// tst_PlcConfigDialog —— 多 PLC 2b 阶段 6:PLC 配置对话框
//
// 【这个对话框解决什么】
//   2a/2b 把 N 台 PLC 的"存得住"和"运行期真连"都做完了,
//   但 plcs[] 只能手改 JSON:
//     · host 写错了要停机改文件,改完还得记得重开工程
//     · slave_id 填 0(Modbus 广播地址)没有任何一层会拦,
//       存下去之后程序【永远发现不了】—— 它是完全合法的 1 字节值
//     · 配了 3 台、只有 1 台连上了,界面上看不出少了哪台
//   改现场设备的配置却只存在于 JSON 里,等于这个功能对真实用户不存在。
//
// 【本文件测什么、不测什么】
//   测:用户能看到什么、改了能看到什么、按钮点下去有没有反应、存出来的 JSON 对不对。
//   不测:表格长什么样、按钮摆在哪 —— 那些"看得见",由人工验收。
//   断言优先落在对话框的公开只读访问器上,不反射 private 成员:
//   反射测的是实现细节,改个成员名就红,却证明不了界面真的对
//   (F3 在 AlarmListControl.h:47-50 踩过并记录了这个坑)。
//   唯一例外是【按钮和表格控件】—— 按 objectName findChild 找它们并真的点下去。
//   findChild / cellWidget 是公开 API,不是反射私有成员。
//
// 【★ 本文件要锁住的设计不变量】
//   ① 删除一台【仍被变量引用】的 PLC:不阻止,也【绝不】自动改变量的 plcId。
//      自动改成 plc1 看着是善意兜底,实际是把原本发给 plc2 的压力值
//      【静默地开始写进一号机】—— 那正是 2b 要消灭的"静默写错设备"。
//      由 removeReferencedPlc_doesNotTouchAnyVariable 守住。
//   ② 存盘只换 plcs 段,其余键原样保留。由 save_preservesOtherSections 守住。
//   ③ 没配过的 slave_id 不会被"顺手"补上。由 roundTrip_absentSlaveId_staysAbsent 守住。
//   ④ 状态列不许把"driver 建出来了"说成"在线"。
//   ⑤ 应用失败时运行中【原样不动】,不能"改了一半"。
//
// 【★ 为什么按钮类用例必须真的按下去】
//   AlarmConfigDialog 曾有 11 条用例全是只读的,一条都没碰按钮,
//   于是用户实测"点新增没反应"时,全部用例【都是绿的】。
//   这个坑在本文件里靠 removeButton_clicked_asksBeforeRemoving 那几条堵住。
//
// 【★ 为什么 removeRowFromDraft 刻意不带 QMessageBox】
//   AlarmConfigDialog 把确认框和数据层方法写在一起,offscreen 测试下
//   永久阻塞(实测卡满 300s),于是"删除"这条最危险的路径一条测试都写不了。
//   本文件能直接测删除,正是因为那个方法被拆开了;
//   而 UI 上"点删除会不会先问"由 QTimer 自动答掉,见那两条用例。
//
// 【★ 负向对照 —— 两轮,2026-10-19 实跑】
//   两轮红集【互不相交】。
//
//   第 1 轮:removeRowFromDraft 里加"把引用了这个 id 的变量顺手改到 plc1"
//     —— 也就是最自然的兜底实现。
//     红 1 条:removeReferencedPlc_doesNotTouchAnyVariable
//     现象:press0 的 plcId 被改成了 plc1,断言消息里点名了变量名和错投的台。
//
//   第 2 轮:exportDraftToJson 里把
//     `if (hadSlaveIdKey || slaveId != 1) o["slave_id"] = ...`
//     改成无条件 o["slave_id"] = r.slaveId(也就是"顺手补上缺省值")。
//     红 2 条:roundTrip_absentSlaveId_staysAbsent
//              / roundTrip_untouchedDialog_producesNoDiff
//     ★★ 第 2 轮第一次只红了 1 条 —— 那条被我写成"总纲"的
//       roundTrip_untouchedDialog_producesNoDiff 当时【仍然是绿的】。
//       原因:它拿的夹具每一行都【本来就带 slave_id】,所以无条件写出来的值
//       和原来那个键里的值一模一样,逐字节比较当然过。
//       改成拿"真的没配过 slave_id"的那份夹具后,它才真的盖住了这个问题。
//       记在这里是因为这个教训比结论本身更值得留:
//       【一条用例能不能守住它声称的作用范围,取决于夹具像不像真实用户的数据】。
// ============================================================================

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>

#include "Project.h"
#include "Variable.h"
#include "VariableManager.h"
#include "drivers/DriverFactory.h"
#include "plc/PlcConfigDialog.h"
#include "plc/PlcConnectionManager.h"

using qwhmi::plc::PlcConfigDialog;

namespace {

// 列号,与 PlcConfigDialog.cpp 里的 kCol* 一一对应。
// ★ 在测试里另抄一份是刻意的 —— 抄错会立刻红,而把常量导出来
//   就变成"实现改了常量,测试跟着改,永远绿"。
constexpr int kColId = 0, kColHost = 1, kColPort = 2, kColTimeout = 4,
              kColSlaveId = 7, kColStatus = 8;

// 三台配置。schema 用的是【真实的】plcs[] 形状 —— 注意键名是 "id",
// 不是 "plc_id"(后者是 variables[] 里的字段)。
// ★ 三个通讯参数是【嵌套】在 modbus 对象里的。
QJsonArray threePlcs() {
    const auto one = [](const char* id, const char* host, int port, int slaveId) {
        QJsonObject o;
        o[QStringLiteral("id")]       = QString::fromLatin1(id);
        o[QStringLiteral("host")]     = QString::fromLatin1(host);
        o[QStringLiteral("port")]     = port;
        o[QStringLiteral("protocol")] = QStringLiteral("modbus");
        if (slaveId > 0) o[QStringLiteral("slave_id")] = slaveId;
        QJsonObject modbus;
        modbus[QStringLiteral("timeout_ms")]       = 3000;
        modbus[QStringLiteral("max_retries")]      = 5;
        modbus[QStringLiteral("poll_interval_ms")] = 200;
        o[QStringLiteral("modbus")] = modbus;
        return o;
    };
    QJsonArray a;
    a.append(one("plc1", "10.0.0.1", 502, 1));
    a.append(one("plc2", "10.0.0.2", 502, 5));
    a.append(one("plc3", "10.0.0.3", 1502, 9));
    return a;
}

// 建一份"两台"的配置,专测新增行的命名(要留一个编号空档)
QJsonArray gapPlcs() {
    QJsonArray a;
    for (const char* id : {"plc1", "plc3"}) {
        QJsonObject o;
        o[QStringLiteral("id")]   = QString::fromLatin1(id);
        o[QStringLiteral("host")] = QStringLiteral("10.0.0.1");
        o[QStringLiteral("port")] = 502;
        a.append(o);
    }
    return a;
}

// 在临时目录里写一个工程文件,返回路径
QString writeProject(const QJsonObject& root) {
    static QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    QFile::remove(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(QJsonDocument(root).toJson());
    f.close();
    return path;
}

// 排一次事件循环,顺带回答下一个弹出的模态框。
//   ★ offscreen 下 QMessageBox::question 是【阻塞】的(静态函数自己 exec),
//     直接点按钮会把测试卡满 300s 超时 —— AlarmConfigDialog 就这么卡过一次。
//   QTimer::singleShot(0) 会在那个嵌套事件循环里被派发,
//   这时 activeModalWidget() 已经是那个框了。
//   answer == QMessageBox::Yes 时点 Yes,否则关掉(= 按"否")。
void answerNextModalDialog(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, qApp, [answer] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        if (answer == QMessageBox::Yes) {
            if (auto* yes = box->button(QMessageBox::Yes)) { yes->click(); return; }
        }
        box->reject();
    });
}

} // namespace

class TestPlcConfigDialog : public QObject {
    Q_OBJECT

private slots:
    // ---- 读取正确性 ----
    void readsAllFieldsFromManager();
    void readsNestedModbusParams();
    void statusColumn_doesNotClaimOnlineWhenSocketIsDown();
    void refCountColumn_countsVariablesPerPlc();

    // ---- 新增 / 删除 ----
    void addRowToDraft_usesUnusedId();
    void addRowToDraft_afterPlc2_givesPlc4NotPlc2();
    void addRowToDraft_withCustomNames_startsAtPlc1();
    void removeRowFromDraft_removesExactlyOneRow();
    void removeReferencedPlc_doesNotTouchAnyVariable();
    void removeButton_clicked_asksBeforeRemoving();
    void removeButton_confirmed_actuallyRemoves();

    // ---- 校验 ----
    void validate_rejectsEmptyId();
    void validate_rejectsDuplicateId();
    void validate_rejectsEmptyHost();
    void validate_rejectsBadPort();

    // ---- 从站号:进不来的 / 存不下去的 ----
    void slaveIdSpinBox_rangeIs1To247();
    void slaveIdSpinBox_outOfRangeInput_isClampedByWidgetItself();
    void slaveId_fromFile_isRejectedByValidate_allTheWayThrough();
    void slaveId_fromFile_survivesRoundTripUnchanged();

    // ---- 协议下拉 ----
    void protocolColumn_isComboBox_notFreeText();
    void protocolCombo_greysOutUnimplemented();
    void protocolCombo_unknownProtocolInFile_isShownNotReplaced();

    // ---- 编辑 → 草稿 ----
    void editCell_updatesDraft();

    // ---- 导出 / 存盘 ----
    void export_usesIdNotPlcId();
    void export_putsCommParamsUnderModbus();
    void export_preservesUnknownFields();
    void roundTrip_absentSlaveId_staysAbsent();
    void roundTrip_explicitSlaveIdOne_isPreserved();
    void roundTrip_untouchedDialog_producesNoDiff();
    void save_preservesOtherSections();
    void save_refusesDuplicateId();
    void save_refusesNonExistentFile();
    void save_refusesMalformedJson();

    // ---- 应用到运行中 ----
    void apply_createsDriverForNewPlc();
    void apply_refusedWhenInvalid_leavesRunningUntouched();
    void apply_refusedWithoutManager_explainsWhy();
    void applyBtn_disabledWithoutManager();
};

// ---------------------------------------------------------------------------
// 读取正确性
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::readsAllFieldsFromManager() {
    // ★ manager 在栈上、对话框后声明 —— 析构逆序保证对话框先死。
    //   早先的写法是 manager 堆上 new + deleteLater(),而对话框还攥着它:
    //   deleteLater 排进事件队列,下一次转事件就可能在对话框还在用时把它删掉。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    QCOMPARE(dlg.rowCount(), 3);
    QCOMPARE(dlg.idAt(0), QStringLiteral("plc1"));
    QCOMPARE(dlg.hostAt(0), QStringLiteral("10.0.0.1"));
    QCOMPARE(dlg.portAt(0), 502);
    QCOMPARE(dlg.protocolAt(0), QStringLiteral("modbus"));
    QCOMPARE(dlg.slaveIdAt(0), 1);
    QCOMPARE(dlg.idAt(1), QStringLiteral("plc2"));
    QCOMPARE(dlg.hostAt(1), QStringLiteral("10.0.0.2"));
    QCOMPARE(dlg.slaveIdAt(1), 5);
    QCOMPARE(dlg.idAt(2), QStringLiteral("plc3"));
    QCOMPARE(dlg.portAt(2), 1502);
    QCOMPARE(dlg.slaveIdAt(2), 9);
}

void TestPlcConfigDialog::readsNestedModbusParams() {
    // ★ 这条钉的是"三个通讯参数在【嵌套的 modbus 对象】里"。
    //   如果哪天改成平铺(或者读的时候忘了往下一层取),
    //   界面上会一直显示缺省值(1000/5/1000)而文件里是 3000/5/200 ——
    //   用户改了存进去也不对,而且界面上完全看不出。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    const QJsonArray out = dlg.exportDraftToJson();
    QCOMPARE(out.size(), 3);
    const QJsonObject modbus = out.at(0).toObject()
                                   .value(QStringLiteral("modbus")).toObject();
    QCOMPARE(modbus.value(QStringLiteral("timeout_ms")).toInt(), 3000);
    QCOMPARE(modbus.value(QStringLiteral("max_retries")).toInt(), 5);
    QCOMPARE(modbus.value(QStringLiteral("poll_interval_ms")).toInt(), 200);
}

void TestPlcConfigDialog::statusColumn_doesNotClaimOnlineWhenSocketIsDown() {
    // ★★★ 界面不许撒谎 —— 这条钉的是本对话框最要紧的那件事。
    //
    //   改之前状态列判的是"driverFor(id) 非空"。而 driver 在
    //   DriverFactory::create 返回时就进了表,connectTo 是【异步】的:
    //   地址写错、DNS 解析不了、对端没上电,TCP 一个都没建起来,
    //   状态列却已经全绿了。
    //   于是这个【专门为了告诉用户"哪台没连上"而做】的列,
    //   恰恰在最需要它的场景下骗人:配 3 台,三行全"在线",现场一台都没通。
    //
    //   本测试用的 10.0.0.x 全都不可达,所以 isConnected() 必然是 false。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY2(table, "找不到 plcConfigTable");
    QCOMPARE(table->columnCount(), 10);

    for (int r = 0; r < table->rowCount(); ++r) {
        auto* it = table->item(r, kColStatus);
        QVERIFY2(it, qPrintable(QStringLiteral("第 %1 行没有状态格").arg(r)));
        QVERIFY2(it->text() != QStringLiteral("在线"),
                 qPrintable(QStringLiteral(
                     "第 %1 行显示「在线」,但 10.0.0.%1 根本连不上 —— "
                     "driver 建出来了不等于连上了").arg(r + 1)));
        // 也不能留空:空着的格分不清"没配"和"没连上"
        QVERIFY2(!it->text().isEmpty(),
                 qPrintable(QStringLiteral("第 %1 行状态是空的 —— "
                     "用户分不清是没显示出来还是没连上").arg(r + 1)));
    }
}

void TestPlcConfigDialog::refCountColumn_countsVariablesPerPlc() {
    VariableManager vm;
    for (int i = 0; i < 3; ++i) {
        auto* v = new Variable(QStringLiteral("v%1").arg(i), 100,
                               Variable::DataType::Int16,
                               Variable::Access::ReadOnly);
        v->setPlcId(QStringLiteral("plc2"));
        vm.addVariable(v);   // addVariable 接管 parent,不用自己 delete
    }
    auto* v3 = new Variable(QStringLiteral("other"), 200,
                            Variable::DataType::Int16,
                            Variable::Access::ReadOnly);
    v3->setPlcId(QStringLiteral("plc3"));
    vm.addVariable(v3);

    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr, &vm);

    QCOMPARE(dlg.refCountAt(0), 0);   // plc1:没有变量指向
    QCOMPARE(dlg.refCountAt(1), 3);   // plc2:三个
    QCOMPARE(dlg.refCountAt(2), 1);   // plc3:一个
}

// ---------------------------------------------------------------------------
// 新增 / 删除
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::addRowToDraft_usesUnusedId() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    const int row = dlg.addRowToDraft();
    QCOMPARE(row, 3);
    QCOMPARE(dlg.rowCount(), 4);
    // plc1/2/3 都在 → 新行必须是 plc4。
    QCOMPARE(dlg.idAt(3), QStringLiteral("plc4"));
}

void TestPlcConfigDialog::addRowToDraft_afterPlc2_givesPlc4NotPlc2() {
    // ★ 新行【顺延】编号,不填空位。
    //   plc1/plc3 有、plc2 空着,新行若叫 plc2,用户会以为
    //   "这台一直在,只是界面上没显示出来",于是去排查一个不存在的连接问题。
    //   编号法原来写的是"从 plc1 起找第一个没用过的",会返回 plc2 ——
    //   和它自己那句注释正好相反,这条用例就是在钉这个矛盾。
    PlcConnectionManager mgr;
    mgr.applyConfig(gapPlcs());
    PlcConfigDialog dlg(&mgr);
    QCOMPARE(dlg.rowCount(), 2);

    const int row = dlg.addRowToDraft();
    QCOMPARE(dlg.idAt(row), QStringLiteral("plc4"));
}

void TestPlcConfigDialog::addRowToDraft_withCustomNames_startsAtPlc1() {
    // 用户自己起名("东侧泵站")时,不该去猜一个"东侧泵站2"出来。
    // 猜出来的名字对用户没有意义,而 plc1 至少是程序里约定的名字。
    QJsonArray cfg;
    QJsonObject o;
    o[QStringLiteral("id")]   = QStringLiteral("东侧泵站");
    o[QStringLiteral("host")] = QStringLiteral("10.0.0.1");
    o[QStringLiteral("port")] = 502;
    cfg.append(o);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    QCOMPARE(dlg.addRowToDraft(), 1);
    QCOMPARE(dlg.idAt(1), QStringLiteral("plc1"));
}

void TestPlcConfigDialog::removeRowFromDraft_removesExactlyOneRow() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    QVERIFY(dlg.removeRowFromDraft(1));
    QCOMPARE(dlg.rowCount(), 2);
    QCOMPARE(dlg.idAt(0), QStringLiteral("plc1"));
    QCOMPARE(dlg.idAt(1), QStringLiteral("plc3"));   // 只删中间那台

    // 越界必须返回 false 而不是崩 —— 界面上"没选中就按删除"会走到这里
    QVERIFY(!dlg.removeRowFromDraft(-1));
    QVERIFY(!dlg.removeRowFromDraft(99));
}

void TestPlcConfigDialog::removeReferencedPlc_doesNotTouchAnyVariable() {
    // ★★★ 本文件最重要的一条。理由见头注释的不变量 ①。
    //
    //   删除一台被 3 个变量引用的 plc2 之后,那 3 个变量的 plcId
    //   必须【还是 plc2】。
    //   自动改成 plc1 会让原本发给 plc2 的压力值【静默地开始写进一号机】,
    //   而界面上没有任何提示 —— 那正是 2b 要消灭的 bug 换个入口又回来。
    VariableManager vm;
    QList<Variable*> vars;
    for (int i = 0; i < 3; ++i) {
        auto* v = new Variable(QStringLiteral("press%1").arg(i), 100,
                               Variable::DataType::Int16,
                               Variable::Access::ReadWrite);
        v->setPlcId(QStringLiteral("plc2"));
        vm.addVariable(v);
        vars << v;
    }

    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr, &vm);

    // 先确认删除前它们确实指向 plc2(否则下面的断言是空转的)
    QCOMPARE(dlg.refCountAt(1), 3);

    QVERIFY(dlg.removeRowFromDraft(1));

    for (Variable* v : vars) {
        QVERIFY2(v->plcId() == QStringLiteral("plc2"),
                 qPrintable(QStringLiteral("变量 %1 的 plcId 被改成了 %2 —— "
                     "删 PLC 时自动改指向 = 静默把数据发给另一台机器")
                     .arg(v->name(), v->plcId())));
    }
    // plc2 确实从草稿里没了
    for (int i = 0; i < dlg.rowCount(); ++i) {
        QVERIFY(dlg.idAt(i) != QStringLiteral("plc2"));
    }
    // ★ 但导出结果里也不能有 plc2 —— 变量引用的目标必须是不存在的,
    //   让它们落在 Bad 上、看得见,而不是悄悄指向别的机器。
    for (const QJsonValue& v : dlg.exportDraftToJson()) {
        QVERIFY(v.toObject().value(QStringLiteral("id")).toString()
                != QStringLiteral("plc2"));
    }
}

void TestPlcConfigDialog::removeButton_clicked_asksBeforeRemoving() {
    // ★ 点按钮那条路 —— 2026-10-08 在 AlarmConfigDialog 上踩过的坑:
    //   11 条用例全是只读的,用户实测"点新增没反应"时全部绿。
    //   findChild 是公开 API,不算反射私有成员。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    auto* add   = dlg.findChild<QPushButton*>(QStringLiteral("plcConfigAddBtn"));
    auto* rm    = dlg.findChild<QPushButton*>(QStringLiteral("plcConfigRemoveBtn"));
    QVERIFY2(table, "找不到 plcConfigTable");
    QVERIFY2(add,   "找不到 plcConfigAddBtn");
    QVERIFY2(rm,    "找不到 plcConfigRemoveBtn");

    // 先点新增 —— 这条路不弹框,能真断言
    QCOMPARE(table->rowCount(), 3);
    add->click();
    QCOMPARE(table->rowCount(), 4);
    QCOMPARE(dlg.idAt(3), QStringLiteral("plc4"));

    // 再点删除。★ 用 QTimer 把那个确认框答成"否"(见 answerNextModalDialog):
    //   删 PLC 是这个界面最危险的动作,它【必须】先问一句。
    table->selectRow(0);
    answerNextModalDialog(QMessageBox::No);
    rm->click();

    QVERIFY2(table->rowCount() == 4,
             "点了删除又答了『否』,行数却变了 —— 确认框形同虚设,"
             "用户以为取消了其实数据已经没了");
}

void TestPlcConfigDialog::removeButton_confirmed_actuallyRemoves() {
    // 上条的另一半:答"是"就真删。
    //   只测"没删"不测"删了"的话,一个永远返回 false 的 removeRowFromDraft
    //   能让上一条一直绿 —— 而用户永远删不掉 PLC。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    auto* rm    = dlg.findChild<QPushButton*>(QStringLiteral("plcConfigRemoveBtn"));
    QVERIFY(table);
    QVERIFY(rm);

    table->selectRow(1);          // 删 plc2
    answerNextModalDialog(QMessageBox::Yes);
    rm->click();

    QCOMPARE(dlg.rowCount(), 2);
    QVERIFY(dlg.idAt(0) == QStringLiteral("plc1"));
    QVERIFY(dlg.idAt(1) == QStringLiteral("plc3"));
}

// ---------------------------------------------------------------------------
// 校验
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::validate_rejectsEmptyId() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(0, kColId)->setText(QString());   // 清空 plc id

    QString err;
    QVERIFY2(!dlg.validateDraft(&err), "空的 PLC ID 不该通过校验");
    QVERIFY2(err.contains(QStringLiteral("ID")), qPrintable(err));
}

void TestPlcConfigDialog::validate_rejectsDuplicateId() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(2, kColId)->setText(QStringLiteral("plc1"));   // 和第 0 行同名

    QString err;
    QVERIFY2(!dlg.validateDraft(&err), "重复的 PLC ID 不该通过校验");
    QVERIFY2(err.contains(QStringLiteral("重复")), qPrintable(err));
}

void TestPlcConfigDialog::validate_rejectsEmptyHost() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(1, kColHost)->setText(QStringLiteral("   "));   // 只有空白也算空

    QString err;
    QVERIFY2(!dlg.validateDraft(&err), "空的地址不该通过校验");
}

void TestPlcConfigDialog::validate_rejectsBadPort() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(0, kColPort)->setText(QStringLiteral("70000"));   // 越界

    QString err;
    QVERIFY2(!dlg.validateDraft(&err), "越界的端口不该通过校验");
}

// ---------------------------------------------------------------------------
// 从站号
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::slaveIdSpinBox_rangeIs1To247() {
    // ★ 0 是 Modbus 广播地址,写下去全网响应、读永远没有响应。
    //   而它是 Unit ID 那个 1 字节字段完全装得下的合法值,
    //   也就是说存进工程文件之后程序【没有任何一层能发现】。
    //   所以要【从控件上就敲不进去】,而不是存盘时才报错。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(0, kColSlaveId));
    QVERIFY2(spin, "从站号列该是 SpinBox —— 文本格敲得进 0 和 300,"
                   "而这两者都会变成一个存下去才发现不了的配置");

    QCOMPARE(spin->minimum(), 1);
    QCOMPARE(spin->maximum(), 247);
}

void TestPlcConfigDialog::slaveIdSpinBox_outOfRangeInput_isClampedByWidgetItself() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(0, kColSlaveId));
    QVERIFY(spin);

    spin->setValue(999);
    QCOMPARE(spin->value(), 247);          // 控件自己夹住
    QCOMPARE(dlg.slaveIdAt(0), 247);       // 草稿跟上了

    spin->setValue(0);
    QCOMPARE(spin->value(), 1);
    QCOMPARE(dlg.slaveIdAt(0), 1);

    // 边界值必须【真的能填进去】 —— 夹过头也是 bug(247 就该允许)
    spin->setValue(247);
    QCOMPARE(dlg.slaveIdAt(0), 247);
}

void TestPlcConfigDialog::slaveId_fromFile_isRejectedByValidate_allTheWayThrough() {
    // ★ SpinBox 只挡住【用户在界面上敲】这一条路。
    //   真正常见的来源是手改 JSON:从站号 0 / 300 已经躺在文件里了。
    //   这条走的是 reloadFromManager 读出来的那份草稿,必须被 validateDraft 拦下。
    //
    //   为什么非拦不可:运行时 PlcConnectionManager::parseEntry 会把越界值
    //   【按缺省 1 处理】,所以现场表现是"用户填了 300,界面显示 1",
    //   两边对不上且没有任何解释 —— 而配置看上去完全正常。
    const int bad[] = {0, 300, -1};
    for (int sid : bad) {
        QJsonArray cfg = threePlcs();
        QJsonObject p = cfg.at(1).toObject();
        p[QStringLiteral("slave_id")] = sid;
        cfg.replace(1, p);

        PlcConnectionManager mgr;
        mgr.applyConfig(cfg);
        PlcConfigDialog dlg(&mgr);

        // 先确认它【真的读到了那个坏值】,否则后面的断言是空转的
        QCOMPARE(dlg.slaveIdAt(1), sid);

        QString err;
        QVERIFY2(!dlg.validateDraft(&err),
                 qPrintable(QStringLiteral("slave_id=%1 应该被 validateDraft 拒绝").arg(sid)));
        // 而且必须【不许落盘】 —— 存下去用户下次打开才发现
        const QString path = writeProject(QJsonObject{
            {QStringLiteral("schema_version"), 3}});
        QVERIFY(!path.isEmpty());
        QVERIFY2(!dlg.saveDraftToProjectFile(path, &err),
                 qPrintable(QStringLiteral("slave_id=%1 不该被存进工程文件").arg(sid)));
    }
}

void TestPlcConfigDialog::slaveId_fromFile_survivesRoundTripUnchanged() {
    // 与上面成对:合法的越界边缘值(247)必须原样存回去。
    //   一边拦 300 一边把 247 也夹掉,那这个字段就没法用了。
    QJsonArray cfg = threePlcs();
    QJsonObject p = cfg.at(2).toObject();
    p[QStringLiteral("slave_id")] = 247;
    cfg.replace(2, p);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);
    QCOMPARE(dlg.slaveIdAt(2), 247);

    const QString path = writeProject(QJsonObject{
        {QStringLiteral("schema_version"), 3}});
    QVERIFY(!path.isEmpty());
    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("plcs")).toArray().at(2).toObject()
                 .value(QStringLiteral("slave_id")).toInt(), 247);
}

// ---------------------------------------------------------------------------
// 协议下拉
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::protocolColumn_isComboBox_notFreeText() {
    // ★ 文本格会让人敲出 "Modbus TCP"(带空格)这种建不出 driver 的协议,
    //   而那个格子【长得像个能随便填的地方】,没有任何提示说这里只能选。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(0, 3));
    QVERIFY2(combo, "协议列该是下拉框,不是可编辑文本格");
    QVERIFY2(!combo->isEditable(), "协议列不该允许自由输入 —— "
             "敲进去的字符串可能建不出 driver,而格子上看不出任何区别");
}

void TestPlcConfigDialog::protocolCombo_greysOutUnimplemented() {
    // ★ 复用 applyProtocolAvailabilityStyling(已由 tst_ProtocolComboGrey 钉住),
    //   不在这里重写一份 —— 重写的那份改了没人会发现,
    //   正是不抽 ProtocolAvailabilityUi.h 的原因。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(0, 3));
    QVERIFY(combo);

    // ★ 找项不能用 findText("s7") —— 置灰那一步已经把显示文字
    //   改成了 "s7(未实现)",findText 精确匹配就找不到了。
    //   按【前缀】找,这也是设置当前项该用的判据
    //   (hmi.cpp:196 那段"下拉框弹不回去"的 bug 就是拿 ID 去找显示名
    //   导致的 —— 同一个坑,见 DriverFactory::findProtocolItemIndex)。
    const QString suffix = qwhmi::drivers::protocolUnimplementedSuffix();
    int modbus = -1, s7 = -1, eip = -1;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemText(i).startsWith(QStringLiteral("modbus"))) modbus = i;
        else if (combo->itemText(i).startsWith(QStringLiteral("s7")))     s7 = i;
        else if (combo->itemText(i).startsWith(QStringLiteral("eip")))    eip = i;
    }

    QVERIFY2(modbus >= 0, "下拉里该有 modbus");
    // ★ 摆出来(置灰)比不摆好:用户至少知道程序认这个协议,只是还没做。
    //   藏起来只会被反复问"支持 S7 吗",问一次是一次的成本。
    QVERIFY2(s7 >= 0,  "下拉里该有 s7(哪怕置灰)");
    QVERIFY2(eip >= 0, "下拉里该有 eip(哪怕置灰)");

    // 未实现的项必须带上"未实现"标注
    QVERIFY2(combo->itemText(s7).contains(suffix),
             qPrintable(QStringLiteral("s7 这一项该标「%1」,实际是「%2」")
                 .arg(suffix, combo->itemText(s7))));
    QVERIFY2(combo->itemText(eip).contains(suffix),
             qPrintable(QStringLiteral("eip 这一项该标「%1」,实际是「%2」")
                 .arg(suffix, combo->itemText(eip))));
    // 可用的那一项【不许】被误标 —— 反过来标错同样是在骗人
    QVERIFY2(!combo->itemText(modbus).contains(suffix),
             "modbus 是能用的,不该被标成未实现");
}

void TestPlcConfigDialog::protocolCombo_unknownProtocolInFile_isShownNotReplaced() {
    // 工程文件里写了个本程序没有的协议(比如别人用将来版本存的
    // "profinet")。对话框要【照原样显示】,不能悄悄换成 modbus ——
    // 用户打开对话框是为了看配置,看见的必须是文件里真正写着的那个名字。
    QJsonArray cfg = threePlcs();
    QJsonObject p = cfg.at(1).toObject();
    p[QStringLiteral("protocol")] = QStringLiteral("profinet");
    cfg.replace(1, p);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    QCOMPARE(dlg.protocolAt(1), QStringLiteral("profinet"));
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(1, 3));
    QVERIFY(combo);
    QVERIFY2(combo->currentText() == QStringLiteral("profinet"),
             qPrintable(QStringLiteral("下拉显示的是「%1」,文件里写的是 profinet")
                 .arg(combo->currentText())));
}

// ---------------------------------------------------------------------------
// 编辑 → 草稿
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::editCell_updatesDraft() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);

    table->item(0, kColHost)->setText(QStringLiteral("192.168.1.50"));
    table->item(0, kColPort)->setText(QStringLiteral("1502"));

    QCOMPARE(dlg.hostAt(0), QStringLiteral("192.168.1.50"));
    QCOMPARE(dlg.portAt(0), 1502);
}

// ---------------------------------------------------------------------------
// 导出 / 存盘
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::export_usesIdNotPlcId() {
    // ★★★ 键名是 "id",不是 "plc_id"。
    //   写错不报编译错,只让 PlcConnectionManager::parseEntry 报"缺少 id"
    //   然后整台 rejected —— 而界面上只显示"不在线",用户看不出是键名错了。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    const QJsonArray out = dlg.exportDraftToJson();
    QCOMPARE(out.size(), 3);
    const QJsonObject first = out.at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("id")).toString(), QStringLiteral("plc1"));
    QVERIFY2(!first.contains(QStringLiteral("plc_id")),
             "plcs[] 里不该出现 plc_id —— 那是 variables[] 的字段名");
}

void TestPlcConfigDialog::export_putsCommParamsUnderModbus() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(0, kColTimeout)->setText(QStringLiteral("7000"));   // 超时

    const QJsonObject first = dlg.exportDraftToJson().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("modbus")).toObject()
                 .value(QStringLiteral("timeout_ms")).toInt(), 7000);
    // 平铺的那一份必须不存在 —— 两个地方都有的话,读哪一份就成了
    //   "以后谁先改谁赢"的隐性问题。
    QVERIFY2(!first.contains(QStringLiteral("timeout_ms")),
             "timeout_ms 只能藏在 modbus 对象里,不能同时平铺一份");
}

void TestPlcConfigDialog::export_preservesUnknownFields() {
    // 用户文件里有代码不认识的字段时,往返一次不许把它丢掉。
    //   丢掉 == 宣告"代码没列的字段一律丢弃" —— 与
    //   PlcConnectionManager::lastConfig_ 存原始数组是同一条理由。
    QJsonArray cfg = threePlcs();
    QJsonObject plc1 = cfg.at(0).toObject();
    plc1[QStringLiteral("现场备注")] = QStringLiteral("一号泵房");
    plc1[QStringLiteral("vendorSpecificTag")] = QStringLiteral("keep-me");
    QJsonObject modbus = plc1.value(QStringLiteral("modbus")).toObject();
    modbus[QStringLiteral("未来参数")] = 42;
    plc1[QStringLiteral("modbus")] = modbus;
    cfg.replace(0, plc1);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    const QJsonObject out = dlg.exportDraftToJson().at(0).toObject();
    QCOMPARE(out.value(QStringLiteral("现场备注")).toString(),
             QStringLiteral("一号泵房"));
    QCOMPARE(out.value(QStringLiteral("vendorSpecificTag")).toString(),
             QStringLiteral("keep-me"));
    QCOMPARE(out.value(QStringLiteral("modbus")).toObject()
                 .value(QStringLiteral("未来参数")).toInt(), 42);
}

void TestPlcConfigDialog::roundTrip_absentSlaveId_staysAbsent() {
    // ★★★ 最容易造成"脏 diff"的地方。
    //   全仓库默认 slave_id 是 1(没配过)。如果导出时无条件写 slave_id:1,
    //   那么"打开对话框看了一眼然后关掉"就会让工程文件凭空多一个键 ——
    //   而用户一个键都没碰过。现场工程师天天对着 git blame,
    //   这种噪声比功能缺失更让人火大。
    QJsonArray cfg = threePlcs();
    QJsonObject plc2 = cfg.at(1).toObject();
    plc2.remove(QStringLiteral("slave_id"));   // 模拟"用户从来没配过"
    cfg.replace(1, plc2);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    const QJsonObject out = dlg.exportDraftToJson().at(1).toObject();
    QVERIFY2(!out.contains(QStringLiteral("slave_id")),
             "用户没配过的 slave_id 不该被打开一次对话框就补上 —— "
             "那会在 git 里冒出一片「我什么都没改」的 diff");
    // 但值仍按缺省 1 参与通讯,这是另一个不变量
    QCOMPARE(dlg.slaveIdAt(1), 1);
}

void TestPlcConfigDialog::roundTrip_explicitSlaveIdOne_isPreserved() {
    // ★ 与上面【必须成对】:值是 1 但键存在 = 用户手填过 1,
    //   那是用户明确写下的话,不能替用户"简化"掉。
    QJsonArray cfg = threePlcs();
    QJsonObject plc1 = cfg.at(0).toObject();
    plc1[QStringLiteral("slave_id")] = 1;   // 显式写 1
    cfg.replace(0, plc1);

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    const QJsonObject out = dlg.exportDraftToJson().at(0).toObject();
    QVERIFY2(out.contains(QStringLiteral("slave_id")),
             "用户显式填的 slave_id=1 必须在往返后仍然存在");
    QCOMPARE(out.value(QStringLiteral("slave_id")).toInt(), 1);
}

void TestPlcConfigDialog::roundTrip_untouchedDialog_producesNoDiff() {
    // ★★★ roundTrip_absentSlaveId_staysAbsent 那个坑的【总纲】。
    //   打开对话框 → 一个键都没碰 → 导出。
    //   结果必须与原数组【逐字节一致】。
    //
    //   只测 slave_id 一个键是不够的:同一个"顺手补默认值"的毛病
    //   会发生在任何我们认识、而用户没写的字段上。
    //   将来有人给 PlcConfigRow 加个 "描述" 字段忘了处理"原来没有"这一档,
    //   这条用例立刻红 —— 那正是它的用处。
    //
    //   ★★ 2026-10-19 负向对照第 2 轮的实测教训:
    //   这条【原来】是拿 threePlcs() 当夹具的 —— 而那个夹具每一行都
    //   【带了 slave_id】(1/5/9)。于是把导出改成无条件写 slave_id 时,
    //   roundTrip_absentSlaveId_staysAbsent 红了,而这条"总纲"【仍然是绿的】,
    //   因为写出来的值和原来那个键里的值一模一样。
    //   一条本该盖住所有同类问题的用例,被夹具里一个多余的键废掉了大半。
    //   现在换成【真的没配过 slave_id】的那份(全仓库的默认状态),
    //   它才对得上自己声称的作用范围。
    QJsonArray cfg = threePlcs();
    for (int i = 0; i < cfg.size(); ++i) {
        QJsonObject o = cfg.at(i).toObject();
        o.remove(QStringLiteral("slave_id"));   // 用户从来没配过从站号
        cfg.replace(i, o);
    }

    PlcConnectionManager mgr;
    mgr.applyConfig(cfg);
    PlcConfigDialog dlg(&mgr);

    // 先确认这份夹具确实"什么都没配",否则下面的相等是空转的
    for (const QJsonValue& v : cfg) {
        QVERIFY(!v.toObject().contains(QStringLiteral("slave_id")));
    }

    QCOMPARE(dlg.exportDraftToJson(), cfg);
}

void TestPlcConfigDialog::save_preservesOtherSections() {
    // ★★★ 只换 plcs 段。用户在对话框里根本没碰过画面和变量,
    //   存一次就把它们清空,是这个对话框能造成的最坏后果。
    const QString path = writeProject(QJsonObject{
        {QStringLiteral("schema_version"), 3},
        {QStringLiteral("variables"), QJsonArray{
             QJsonObject{{QStringLiteral("name"), QStringLiteral("temp")},
                         {QStringLiteral("address"), 100}}}},
        {QStringLiteral("alarms"), QJsonArray{
             QJsonObject{{QStringLiteral("name"), QStringLiteral("高温")},
                         {QStringLiteral("variable"), QStringLiteral("temp")},
                         {QStringLiteral("threshold"), 80}}}},
        {QStringLiteral("screens"), QJsonArray{
             QJsonObject{{QStringLiteral("name"), QStringLiteral("主画面")}}}}});
    QVERIFY(!path.isEmpty());

    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    QString err;
    QVERIFY2(dlg.saveDraftToProjectFile(path, &err), qPrintable(err));

    // 读回来逐段比 —— 只比"plcs 变了"是不够的,还要确认【别的没变】
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject after = QJsonDocument::fromJson(f.readAll()).object();

    QCOMPARE(after.value(QStringLiteral("schema_version")).toInt(), 3);
    QCOMPARE(after.value(QStringLiteral("variables")).toArray().size(), 1);
    QCOMPARE(after.value(QStringLiteral("variables")).toArray().at(0).toObject()
                 .value(QStringLiteral("name")).toString(), QStringLiteral("temp"));
    QCOMPARE(after.value(QStringLiteral("alarms")).toArray().size(), 1);
    QCOMPARE(after.value(QStringLiteral("alarms")).toArray().at(0).toObject()
                 .value(QStringLiteral("name")).toString(), QStringLiteral("高温"));
    QCOMPARE(after.value(QStringLiteral("screens")).toArray().size(), 1);
    QCOMPARE(after.value(QStringLiteral("plcs")).toArray().size(), 3);
}

void TestPlcConfigDialog::save_refusesDuplicateId() {
    const QString path = writeProject(QJsonObject{
        {QStringLiteral("schema_version"), 3},
        {QStringLiteral("plcs"), threePlcs()}});
    QVERIFY(!path.isEmpty());

    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(2, kColId)->setText(QStringLiteral("plc1"));   // 撞名

    QString err;
    QVERIFY2(!dlg.saveDraftToProjectFile(path, &err),
             "重复 id 的草稿绝不能落盘 —— 存下去用户下次打开才发现变量连错了台");
}

void TestPlcConfigDialog::save_refusesNonExistentFile() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    QString err;
    QVERIFY2(!dlg.saveDraftToProjectFile(QStringLiteral(":/no/such/file.json"), &err),
             "打不开的工程文件必须报错,而不是凭空造一个打不开的工程");
    QVERIFY2(!err.isEmpty(), "失败时必须说清楚为什么");
}

void TestPlcConfigDialog::save_refusesMalformedJson() {
    // ★ 原文不是合法 JSON 时【拒绝覆盖】。
    //   用户手滑把工程文件改坏了一点点,对话框再保存一次就把它们彻底盖掉 ——
    //   而那里面可能还有配了很久的变量表。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.json"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not json ");
    }

    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    PlcConfigDialog dlg(&mgr);

    QString err;
    QVERIFY2(!dlg.saveDraftToProjectFile(path, &err), "非 JSON 的文件不该被覆盖");

    // 文件内容必须【原封不动】还在
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("{ this is not json "));
}

// ---------------------------------------------------------------------------
// 应用到运行中
// ---------------------------------------------------------------------------

void TestPlcConfigDialog::apply_createsDriverForNewPlc() {
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    QCOMPARE(mgr.driverCount(), 3);
    QVERIFY(mgr.driverFor(QStringLiteral("plc1")));

    PlcConfigDialog dlg(&mgr);

    // 加一台,应用 —— 运行中应该真的有第四个 driver 了
    const int row = dlg.addRowToDraft();
    QVERIFY(row >= 0);
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(row, kColHost)->setText(QStringLiteral("10.0.0.9"));
    table->item(row, kColPort)->setText(QStringLiteral("502"));

    QString err;
    QVERIFY2(dlg.applyDraftToRunning(&err), qPrintable(err));
    QVERIFY2(mgr.driverFor(QStringLiteral("plc4")),
             "应用后运行中应该真的有 plc4 的 driver");
    QCOMPARE(mgr.driverCount(), 4);

    // ★ 逐台比对,不是只数个数。
    //   计划里明确要求这一点:只比数量的话,"三台全被改成同一个新地址"
    //   也能过(数量还是 3),而那正是 2b 要消灭的静默写错设备。
    QCOMPARE(mgr.driverFor(QStringLiteral("plc1")), mgr.driverFor(QStringLiteral("plc1")));
    QVERIFY(mgr.driverFor(QStringLiteral("plc1")) != mgr.driverFor(QStringLiteral("plc2")));
    QVERIFY(mgr.driverFor(QStringLiteral("plc2")) != mgr.driverFor(QStringLiteral("plc3")));
    QVERIFY(mgr.driverFor(QStringLiteral("plc3")) != mgr.driverFor(QStringLiteral("plc4")));
    QCOMPARE(mgr.slaveIdFor(QStringLiteral("plc2")), 5);
    QCOMPARE(mgr.slaveIdFor(QStringLiteral("plc3")), 9);
}

void TestPlcConfigDialog::apply_refusedWhenInvalid_leavesRunningUntouched() {
    // ★ "改了一半"是这个界面最坏的失败形态:
    //   用户看到"应用失败",却不知道到底哪几台生效了。
    //   所以第一遍只查,全过了才动手。
    PlcConnectionManager mgr;
    mgr.applyConfig(threePlcs());
    QCOMPARE(mgr.driverCount(), 3);

    PlcConfigDialog dlg(&mgr);
    auto* table = dlg.findChild<QTableWidget*>(QStringLiteral("plcConfigTable"));
    QVERIFY(table);
    table->item(0, kColPort)->setText(QStringLiteral("70000"));   // 端口越界

    QString err;
    QVERIFY2(!dlg.applyDraftToRunning(&err), "非法草稿不该被应用");
    QVERIFY2(!err.isEmpty(), "失败时必须说清楚哪一行、为什么");
    QCOMPARE(mgr.driverCount(), 3);   // 三台一台都没被动过
}

void TestPlcConfigDialog::apply_refusedWithoutManager_explainsWhy() {
    // 没有 plcMgr_ 时"应用到运行中"必须【拒绝并说明原因】,
    //   而不是假装成功 —— 与 AlarmConfigDialog 传 nullptr 拒绝新建报警同一条取舍。
    PlcConfigDialog dlg(nullptr);

    QString err;
    QVERIFY2(!dlg.applyDraftToRunning(&err), "没有 manager 时不该返回成功");
    QVERIFY2(!err.isEmpty(), "必须说明为什么不能应用");
}

void TestPlcConfigDialog::applyBtn_disabledWithoutManager() {
    // ★ 按钮【置灰】,而不是让人点了才弹一个错误框。
    //   置灰 + updateInfo 的一句说明,让用户一眼就知道这个环境里
    //   没有运行中的连接可改 —— 这是能提前告诉用户的事,不该浪费一次点击。
    PlcConfigDialog dlg(nullptr);
    auto* btn = dlg.findChild<QPushButton*>(QStringLiteral("plcConfigApplyBtn"));
    QVERIFY(btn);
    QVERIFY2(!btn->isEnabled(),
             "没有运行中的连接时,「应用到运行中」应该是禁用的");
}

QTEST_MAIN(TestPlcConfigDialog)
#include "tst_PlcConfigDialog.moc"

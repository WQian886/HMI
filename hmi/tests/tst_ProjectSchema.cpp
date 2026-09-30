// ============================================================================
// 阶段 C · C.0 · Project schema 演进单测
//
// 验证 C.0 schema_version 迁移框架:
//   1. migrateV1ToV2 把 communications 单 PLC 提到 plcs[0] 多 PLC 数组
//   2. migrateV1ToV2 schema_version "1.0" → 数字 2
//   3. migrateV1ToV2 保留 modbus 子对象(timeout/max_retries/poll_interval_ms)
//   4. migrateV1ToV2 modbus 子对象缺失时用默认值
//   5. migrateTo 链式调用(预留未来 v2→v3)
//   6. migrateTo 未知版本返回 false
//
// 关联:Project.h, Project.cpp
// ============================================================================

#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include "Project.h"
#include "VariableManager.h"
#include "AlarmManager.h"
#include "ActionManager.h"
#include "PollScheduler.h"
// ★ E.3 测试:controls 序列化需要 ControlBase
#include "widgets/BarControl.h"
#include "widgets/LedControl.h"
#include "widgets/LabelControl.h"
#include "widgets/NumberDisplay.h"
#include "widgets/TextDisplay.h"
#include "widgets/GaugeControl.h"
#include "widgets/TrendControl.h"
#include "widgets/TableControl.h"
#include "widgets/AlarmListControl.h"

using namespace qwhmi::widgets;

class TestProjectSchema : public QObject {
    Q_OBJECT
private slots:
    // ★ E.3 修复:QApplication 创建后注册所有控件(填好工厂注册表)
    //   不能在 static init 跑(那时 QApplication 还没建)
    void initTestCase();

    // v1 → v2 单步迁移
    void migrateV1ToV2_addsPlcsField();
    void migrateV1ToV2_convertsSchemaVersionToNumber();
    void migrateV1ToV2_removesCommunicationsField();
    void migrateV1ToV2_preservesModbusSubObject();
    void migrateV1ToV2_usesDefaultsWhenModbusMissing();
    void migrateV1ToV2_handlesMissingCommunicationsGracefully();

    // migrateTo 链式
    void migrateTo_chainsFromV1ToV2();
    void migrateTo_returnsFalseOnUnknownVersion();

    // saveToFile 写新格式
    void saveToFile_writesSchemaVersion2();
    void saveToFile_writesPlcsArrayNotCommunications();

    // ★ E.3(2026-09-21):screens[].controls 存读 + schema 3 + migrateV2ToV3
    void migrateV2ToV3_addsEmptyScreensArray();
    void saveToFile_writesControlsArray();
    void loadFromFile_restoresControlsViaSignal();
    void roundTrip_preservesControlProperties();
    void unknownControlType_loadsAsNull();
};

// ★ E.3 修复:实例化每个控件一次填注册表(每个 new + delete 触发构造函数里的
//   registerControlType)。**不能在静态初始化时调**(QWidget 需要 QApplication 先建),
//   所以挪到 initTestCase()
//   (initTestCase 在 QTEST_MAIN 创建 QApplication 之后跑,时机正确)

namespace {
// ★ E.3:实例化每个控件一次填注册表(QApplication 已经在 main 里建好,这里 new 是安全的)
void registerAllTestControls() {
    using namespace qwhmi::widgets;
    delete new LabelControl;       delete new NumberDisplay;    delete new TextDisplay;
    delete new BarControl;         delete new GaugeControl;     delete new LedControl;
    delete new TrendControl;       delete new TableControl;     delete new AlarmListControl;
}

// 构造一个标准 v1 JSON 字符串(模拟老 default.json)
QJsonObject makeV1Json() {
    QJsonObject root;
    root["schema_version"] = "1.0";
    root["project_name"]   = "Old Project";
    root["created_at"]     = "2026-08-11T14:36:05";
    root["updated_at"]     = "2026-08-11T14:36:05";
    QJsonObject comm;
    comm["protocol"] = "modbus_tcp";
    comm["host"]     = "192.168.1.10";
    comm["port"]     = 502;
    QJsonObject modbus;
    modbus["timeout_ms"]       = 3000;
    modbus["max_retries"]      = 5;
    modbus["poll_interval_ms"] = 100;
    comm["modbus"] = modbus;
    root["communications"] = comm;
    QJsonArray vars;
    QJsonObject v;
    v["name"] = "Speed";
    v["address"] = 2;
    v["data_type"] = "UInt16";
    v["access"] = "ReadWrite";
    vars.append(v);
    root["variables"] = vars;
    return root;
}
} // namespace

void TestProjectSchema::migrateV1ToV2_addsPlcsField() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root.contains("plcs"));
    QJsonArray plcsArr = root["plcs"].toArray();
    QCOMPARE(plcsArr.size(), 1);
    QJsonObject plc0 = plcsArr[0].toObject();
    QCOMPARE(plc0["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));
    QCOMPARE(plc0["host"].toString(), QStringLiteral("192.168.1.10"));
    QCOMPARE(plc0["port"].toInt(), 502);
}

// ===== initTestCase:注册所有控件(填工厂表)=====
void TestProjectSchema::initTestCase() {
    registerAllTestControls();
}

void TestProjectSchema::migrateV1ToV2_convertsSchemaVersionToNumber() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(root["schema_version"].isString());   // 迁移前是字符串
    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root["schema_version"].isDouble());   // 迁移后是数字
    QCOMPARE(root["schema_version"].toInt(), 2);
}

void TestProjectSchema::migrateV1ToV2_removesCommunicationsField() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();
    QVERIFY(root.contains("communications"));     // 迁移前有

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(!root.contains("communications"));    // 迁移后无(冗余,信息已搬到 plcs[0])
}

void TestProjectSchema::migrateV1ToV2_preservesModbusSubObject() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    QVERIFY(p.migrateV1ToV2(root));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QVERIFY(plc0.contains("modbus"));
    QJsonObject modbus = plc0["modbus"].toObject();
    QCOMPARE(modbus["timeout_ms"].toInt(), 3000);            // 保留
    QCOMPARE(modbus["max_retries"].toInt(), 5);               // 保留
    QCOMPARE(modbus["poll_interval_ms"].toInt(), 100);        // 保留
}

void TestProjectSchema::migrateV1ToV2_usesDefaultsWhenModbusMissing() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();
    QJsonObject comm = root["communications"].toObject();
    comm.remove("modbus");                        // 故意删 modbus 子对象
    root["communications"] = comm;

    QVERIFY(p.migrateV1ToV2(root));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QVERIFY(plc0.contains("modbus"));             // 应该用默认补回来
    QJsonObject modbus = plc0["modbus"].toObject();
    QCOMPARE(modbus["timeout_ms"].toInt(), 3000);
    QCOMPARE(modbus["max_retries"].toInt(), 5);
    QCOMPARE(modbus["poll_interval_ms"].toInt(), 100);
}

void TestProjectSchema::migrateV1ToV2_handlesMissingCommunicationsGracefully() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root;
    root["schema_version"] = "1.0";
    // 没有 communications 字段(最老的格式)
    // 不应该崩,用默认值

    QVERIFY(p.migrateV1ToV2(root));
    QVERIFY(root.contains("plcs"));
    QJsonObject plc0 = root["plcs"].toArray()[0].toObject();
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));   // 默认
    QCOMPARE(plc0["host"].toString(), QStringLiteral("127.0.0.1"));         // 默认
    QCOMPARE(plc0["port"].toInt(), 502);                                    // 默认
}

void TestProjectSchema::migrateTo_chainsFromV1ToV2() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    // migrateTo(fromVersion=1, toVersion=2) 应该依次调 v1→v2
    QVERIFY(p.migrateTo(root, 1, 2));
    QCOMPARE(root["schema_version"].toInt(), 2);
    QVERIFY(root.contains("plcs"));
    QVERIFY(!root.contains("communications"));
}

void TestProjectSchema::migrateTo_returnsFalseOnUnknownVersion() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV1Json();

    // migrateTo(fromVersion=99, toVersion=100) — 没有对应迁移函数 → 返 false
    QVERIFY(!p.migrateTo(root, 99, 100));
    // 失败时 root 不变(不丢数据)
    QCOMPARE(root["schema_version"].toString(), QStringLiteral("1.0"));
}

void TestProjectSchema::saveToFile_writesSchemaVersion2() {
    // saveToFile 需要真实的 managers(检查 !varMgr_ / !alarmMgr_ / !actionMgr_)
    // 所以这 2 个用例建最简实例验证写文件行为
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    // PollScheduler 构造需要 varMgr + ModbusMaster* — 我们不用它的 tick,只用来满足 Project 构造
    // 给 null ModbusMaster 让 PollScheduler 不启动(默认不调用 start())
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    // ★ 原来这里写死 "E:/qianwie/hmi/hmi/tests/build/xxx.json",而 build_tests.ps1
    //   实际用的构建目录是 build_ninja/ —— build/ 根本不存在,saveToFile 落不了盘,
    //   这条用例【在任何机器上都是红的】(换台电脑连路径都对不上)。
    //   改用 QTemporaryDir:它是【目录】,不是已存在的文件,所以不跟 QSaveFile
    //   抢同一个文件名(原先的注释担心 QTemporaryFile 冲突,QTemporaryDir 没这问题);
    //   析构时自动连目录一起删,不用自己收尾。
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString path = dir.filePath("tst_saveToFile_writesSchemaVersion2.json");

    QVERIFY(p.saveToFile(path));

    // 读回 + 验证
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QVERIFY(doc.isObject());
    QJsonObject root = doc.object();
    QCOMPARE(root["schema_version"].toInt(), 3);          // ★ E.3 bump:2 → 3
    QVERIFY(root.contains("plcs"));
    QVERIFY(!root.contains("communications"));             // ← 不再写老字段
}

void TestProjectSchema::saveToFile_writesPlcsArrayNotCommunications() {
    VariableManager vm;
    AlarmManager am;
    ActionManager acm;
    PollScheduler ps(&vm, nullptr);
    Project p(&vm, &am, &acm, &ps, nullptr);

    // 同上:临时目录,不写死盘符
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString path = dir.filePath("tst_saveToFile_writesPlcsArrayNotCommunications.json");

    QVERIFY(p.saveToFile(path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QVERIFY(root.contains("plcs"));
    QJsonArray plcsArr = root["plcs"].toArray();
    QCOMPARE(plcsArr.size(), 1);
    QJsonObject plc0 = plcsArr[0].toObject();
    QCOMPARE(plc0["id"].toString(), QStringLiteral("plc1"));
    QCOMPARE(plc0["protocol"].toString(), QStringLiteral("modbus_tcp"));
}

// ============================================================================
// ★ E.3(2026-09-21):screens[].controls 存读 + schema 3 + migrateV2ToV3
// ============================================================================

// 模拟 v2 JSON(无 screens 字段)— 走迁移应该自动补 screens[0].controls = []
static QJsonObject makeV2Json() {
    QJsonObject root;
    root["schema_version"] = 2;
    root["project_name"] = "v2 Project";
    QJsonArray plcsArr;
    QJsonObject plc;
    plc["id"] = "plc1";
    plc["protocol"] = "modbus_tcp";
    plc["host"] = "127.0.0.1";
    plc["port"] = 502;
    plcsArr.append(plc);
    root["plcs"] = plcsArr;
    return root;
}

// ===== 1. migrateV2ToV3_addsEmptyScreensArray =====
void TestProjectSchema::migrateV2ToV3_addsEmptyScreensArray() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);
    QJsonObject root = makeV2Json();
    QVERIFY(p.migrateV2ToV3(root));
    QCOMPARE(root["schema_version"].toInt(), 3);
    QVERIFY(root.contains("screens"));
    QJsonArray screens = root["screens"].toArray();
    QCOMPARE(screens.size(), 1);
    QJsonObject screen0 = screens[0].toObject();
    QVERIFY(screen0.contains("controls"));
    QCOMPARE(screen0["controls"].toArray().size(), 0);
}

// ===== 2. saveToFile_writesControlsArray =====
void TestProjectSchema::saveToFile_writesControlsArray() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);

    BarControl* bar = new BarControl;
    bar->setPosition({100, 50});
    bar->setMaximum(99.0);
    bar->setFillColor(QColor("#AABBCC"));
    QList<ControlBase*> controls;
    controls.append(bar);

    QString path = QDir::temp().filePath("test_savecontrols.hmi.json");
    QVERIFY(p.saveToFile(path, controls));
    QVERIFY(QFile::exists(path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QCOMPARE(root["schema_version"].toInt(), 3);
    QVERIFY(root.contains("screens"));
    QJsonArray screens = root["screens"].toArray();
    QCOMPARE(screens.size(), 1);
    QJsonArray ctrlArr = screens[0].toObject()["controls"].toArray();
    QCOMPARE(ctrlArr.size(), 1);
    QJsonObject barObj = ctrlArr[0].toObject();
    QCOMPARE(barObj["type"].toString(), QStringLiteral("Bar"));   // ★ E.3:注册表短名
    QCOMPARE(barObj["maximum"].toDouble(), 99.0);
    // ★ QColor 在 JSON 里是 hex 字符串(Qt 6 QJsonValue::fromVariant 行为)
    QCOMPARE(barObj["fillColor"].toString(), QStringLiteral("#aabbcc"));

    delete bar;
    QFile::remove(path);
}

// ===== 3. loadFromFile_restoresControlsViaSignal =====
void TestProjectSchema::loadFromFile_restoresControlsViaSignal() {
    Project p(nullptr, nullptr, nullptr, nullptr, nullptr);

    // 构造 v3 JSON:有 1 个 LedControl
    QJsonObject root;
    root["schema_version"] = 3;
    QJsonArray screens;
    QJsonObject screen0;
    screen0["name"] = "主屏";
    QJsonArray ctrls;
    QJsonObject ledObj;
    ledObj["type"] = "LED";   // ★ E.3:用注册表短名(LedControl 注册为 "LED")
    ledObj["position"] = QJsonObject{{"x", 50.0}, {"y", 60.0}};
    ledObj["size"]     = QJsonObject{{"width", 40.0}, {"height", 40.0}};
    ledObj["onColor"]  = "#112233";
    ctrls.append(ledObj);
    screen0["controls"] = ctrls;
    screens.append(screen0);
    root["screens"] = screens;

    QString path = QDir::temp().filePath("test_loadcontrols.hmi.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson());
    f.close();

    // 监听 controlsLoaded 信号
    QList<ControlBase*> loaded;
    QObject::connect(&p, &Project::controlsLoaded,
                     [&loaded](const QList<ControlBase*>& c) { loaded = c; });

    QVERIFY(p.loadFromFile(path));
    QCOMPARE(loaded.size(), 1);
    // ★ E.3:loaded[0] 是通过 ControlBase::create("LED") new 出来的 LedControl,
    //   类名是 "qwhmi::widgets::LedControl"(含 namespace)
    QCOMPARE(QString::fromUtf8(loaded[0]->metaObject()->className()),
             QStringLiteral("qwhmi::widgets::LedControl"));
    QCOMPARE(loaded[0]->position(), QPointF(50, 60));
    // onColor 是 LedControl 派生属性,需要 downcast
    auto* led = qobject_cast<LedControl*>(loaded[0]);
    QVERIFY(led != nullptr);
    QCOMPARE(led->onColor(), QColor("#112233"));

    // 清理(loadFromFile 创建的控件由 Project 创建,但所有权按契约交给调用方)
    qDeleteAll(loaded);
    QFile::remove(path);
}

// ===== 4. roundTrip_preservesControlProperties =====
void TestProjectSchema::roundTrip_preservesControlProperties() {
    Project p1(nullptr, nullptr, nullptr, nullptr, nullptr);
    Project p2(nullptr, nullptr, nullptr, nullptr, nullptr);

    NumberDisplay* nd = new NumberDisplay;
    nd->setPosition({123, 456});
    nd->setVariableName("plc1.Speed");
    nd->setFormat("%.3f");
    nd->setUnit("rpm");
    QList<ControlBase*> originals;
    originals.append(nd);

    QString path = QDir::temp().filePath("test_roundtrip.hmi.json");
    QVERIFY(p1.saveToFile(path, originals));

    QList<ControlBase*> loaded;
    QObject::connect(&p2, &Project::controlsLoaded,
                     [&loaded](const QList<ControlBase*>& c) { loaded = c; });
    QVERIFY(p2.loadFromFile(path));
    QCOMPARE(loaded.size(), 1);

    NumberDisplay* nd2 = qobject_cast<NumberDisplay*>(loaded[0]);
    QVERIFY(nd2 != nullptr);
    QCOMPARE(nd2->variableName(), QStringLiteral("plc1.Speed"));
    QCOMPARE(nd2->format(),      QStringLiteral("%.3f"));
    QCOMPARE(nd2->unit(),        QStringLiteral("rpm"));
    QCOMPARE(nd2->position(),    QPointF(123, 456));
    QCOMPARE(nd2->size(),        QSizeF(140, 40));   // ★ 默认 size(NumberDisplay 构造函数里设的)

    delete nd;
    qDeleteAll(loaded);
    QFile::remove(path);
}

// ===== 5. unknownControlType_loadsAsNull =====
void TestProjectSchema::unknownControlType_loadsAsNull() {
    QJsonArray arr;
    QJsonObject unknown;
    unknown["type"] = "NonExistentControl_999";
    unknown["position"] = QJsonObject{{"x", 0.0}, {"y", 0.0}};
    arr.append(unknown);

    QList<ControlBase*> out;
    int okCount = Project::loadControlsFromJson(arr,
                                                 &ControlBase::create,
                                                 out);
    QCOMPARE(okCount, 0);            // 未知 type 跳过
    QCOMPARE(out.size(), 0);         // 没创建任何控件
    QVERIFY(out.isEmpty());
}

QTEST_MAIN(TestProjectSchema)
#include "tst_ProjectSchema.moc"

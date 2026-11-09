// ============================================================================
// tst_ActionTemplate.cpp —— F 阶段最后一项:联动模板
//
// 【这个测试在钉什么】
//   一条报警 → 【一整组动作】(多步、多种类型),而不是"一条报警一个动作"。
//
// 【原来为什么不行】
//   ActionManager::addMapping 的实现是:
//       mappings_[alarmName] = m;      ← 后配的【覆盖】先配的
//   ⇒ 想给同一条报警配两个动作(停机 + 留痕),只能放弃一个。
//   而且"动作有哪些种类"整段写死在 ActionManager::onAlarmTriggered 里 ——
//   而 ActionManager 【不】在 hmi_core 的"判据"位置之外……
//   (准确说:它有测试,但"多步/多类型"的判据从来没被单独钉住,
//    改了 ActionManager 的循环体,只有端到端那条用例可能发现 ——
//    而那条用例只配了一个动作。)
//
// 【判据现在住在哪】
//   qwhmi::action(公共层,hmi_core 里):
//     · ActionType      —— 有哪些动作类型
//     · ActionStep      —— 一步动作
//     · ActionTemplate  —— 一条报警的一组动作
//     · validateStep()  —— 这一步合不合法
//     · executeStep()   —— 执行一步,如实报结果
//     · ActionSink      —— 抽象出口("把变量写成这个值"由调用方实现)
//   ActionManager 只留接线(怎么真的把变量写出去)。
//
// 【为什么要分成两层】
//   公共层不认识 VariableManager —— 它只认识 ActionSink。
//   测试用假 sink(把调用记下来),生产用 ActionManager 的嵌套 sink。
//   手法同 src/plc/ModbusRequestParser.h(纯判据,不碰硬件)。
//
// 【负向对照】
//   scripts/negctl_action_template.py —— 6 组变异,全部按预期变红:
//     M1 addMapping 退回"覆盖"        → 多动作那条用例红
//     M2 只执行第一步                  → 步数/顺序那条用例红
//     M3 不认识的类型 fallback 成写变量 → 未知类型那条用例红
//     M4 加载端忽略 action_templates   → 覆盖老格式那条用例红
//     M5 存盘端不写 action_templates   → 存盘那条用例红
//     M6 空的变量名也算合法            → 非法步骤那条用例红
//
// 关联:src/action/ActionTemplate.{h,cpp}、ActionManager.{h,cpp}、
//      JsonSerializer(存读)、Project.cpp(工程文件)
// ============================================================================

#include <QtTest>

#include "ActionManager.h"
#include "Alarm.h"
#include "AlarmManager.h"
#include "JsonSerializer.h"
#include "PollScheduler.h"
#include "Project.h"
#include "Variable.h"
#include "VariableManager.h"
#include "action/ActionTemplate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

using qwhmi::action::ActionSink;
using qwhmi::action::ActionStep;
using qwhmi::action::ActionTemplate;
using qwhmi::action::ActionType;
using qwhmi::action::StepOutcome;
using qwhmi::action::StepResult;

namespace {

// ★ 假 sink —— 把"调用了什么"记下来。
//   【为什么不直接用 ActionManager】
//     ActionManager 那条路要 VariableManager + 真实 Variable,
//     而且只能证明"变量值被改了",证明不了"这一步有没有被调到、
//     调用的顺序对不对"。假 sink 能看见【调用序列】。
class FakeSink : public ActionSink {
public:
    explicit FakeSink(const QString& alarmName) : ActionSink(alarmName) {}

    struct Call {
        QString kind;   // "write" / "log"
        QString a;      // 变量名 / 文字
        QString b;      // 值
    };
    QList<Call> calls;

    StepResult writeVariable(const QString& variableName, const QVariant& value) override {
        calls.append(Call{QStringLiteral("write"), variableName, value.toString()});
        StepResult r;
        r.outcome      = StepOutcome::Executed;
        r.appliedValue = value.toString();
        return r;
    }
    void logOnly(const QString& text) override {
        calls.append(Call{QStringLiteral("log"), text, QString()});
    }

    QStringList kinds() const {
        QStringList out;
        for (const Call& c : calls) out << c.kind;
        return out;
    }
};

Alarm makeAlarm(const QString& name) {
    Alarm a;
    a.id_   = 1;
    a.name  = name;
    a.level = AlarmLevel::Critical;
    a.state = AlarmState::Active;
    return a;
}

// 一步"写变量"。
ActionStep writeStep(const QString& variableName, const QVariant& value) {
    ActionStep s;
    s.type         = ActionType::WriteVariable;
    s.variableName = variableName;
    s.value        = value;
    return s;
}

// 一步"只记日志"。
ActionStep logStep(const QString& text) {
    ActionStep s;
    s.type = ActionType::LogOnly;
    s.text = text;
    return s;
}

// 工程 JSON 的最小骨架:两个变量 + 一条报警。
//   【为什么自己拼 JSON 而不用 saveToFile 造】
//     要测的就是"读工程文件这条路上模板能不能进去" ——
//     用生产代码生成输入,等于用被测对象造输入。
QJsonObject baseProject() {
    QJsonObject root;
    root["schema_version"] = 3;
    root["project_name"]   = QStringLiteral("联动模板测试工程");

    QJsonArray vars;
    for (const QString& name : {QStringLiteral("Speed"), QStringLiteral("Valve")}) {
        QJsonObject v;
        v["name"]             = name;
        v["address"]          = 0;
        v["data_type"]        = QStringLiteral("UInt16");
        v["access"]           = QStringLiteral("ReadWrite");
        v["deadband"]         = 0;
        v["poll_interval_ms"] = 200;
        v["alwaysPoll"]       = true;
        QJsonObject range;
        range["min"] = 0;
        range["max"] = 65535;
        v["range"] = range;
        vars.append(v);
    }
    root["variables"] = vars;

    QJsonArray alarms;
    QJsonObject a;
    a["name"]            = QStringLiteral("温度高");
    a["variable"]        = QStringLiteral("Speed");
    a["direction"]       = QStringLiteral("Above");
    a["level"]           = QStringLiteral("Critical");
    a["threshold"]       = 50;
    a["min_duration_ms"] = 50;
    alarms.append(a);
    root["alarms"] = alarms;

    return root;
}

// 去掉注释再比对 —— 源码注释里满是 "action_templates" 这类名字
// (说明它为什么在那儿),不去掉就会把"注释提到了"当成"代码还在",测试永远绿。
// (手法同 tst_ActionWrite.cpp / tst_MenuBar.cpp)
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

class tst_ActionTemplate : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // ---------- 公共层的判据 ----------
    void typeStrings_roundTrip();
    void unknownTypeString_isNotSilentlyAccepted();
    void validateStep_rejectsEmptyVariableName();
    void validateStep_rejectsEmptyLogText();
    void executeStep_writeVariable_callsSink();
    void executeStep_logOnly_callsSink();
    void executeStep_invalidStep_neverCallsSink();
    void executeStep_unknownType_neverCallsSink();
    void executeStep_keepsConfiguredOrder();

    // ---------- ActionManager:追加而不是覆盖 ----------
    void addMapping_sameAlarmTwice_appendsInsteadOfOverwriting();
    void addTemplate_replacesTheWholeGroup();
    void mappings_isTheFlattenedWriteVariableView();

    // ---------- ActionManager:真的按组执行 ----------
    void oneAlarm_executesEveryStepInOrder();
    void aFailedStep_doesNotStopTheFollowingSteps();
    void logOnlyStep_doesNotTouchAnyVariable();

    // ---------- 存读 ----------
    void json_templateRoundTrip_keepsEveryStep();
    void json_unknownType_failsWithAReadableReason();
    void json_invalidStep_failsWithAReadableReason();

    // ---------- 工程文件 ----------
    void project_legacyActionsOnly_stillLoads();
    void project_actionTemplates_winOverLegacyActions();
    void project_savedFile_containsActionTemplates();
    void project_source_readsAndWritesActionTemplates();

private:
    QTemporaryDir*   tmp_  = nullptr;
    VariableManager* vm_  = nullptr;
    ActionManager*   act_ = nullptr;

    bool writeProject(const QJsonObject& root, QString* outPath);
};

void tst_ActionTemplate::init() {
    tmp_ = new QTemporaryDir;
    vm_  = new VariableManager(this);
    act_ = new ActionManager(this);
    act_->setVariableManager(vm_);
}

void tst_ActionTemplate::cleanup() {
    delete act_; act_ = nullptr;
    delete vm_;  vm_  = nullptr;
    delete tmp_;  tmp_  = nullptr;
}

bool tst_ActionTemplate::writeProject(const QJsonObject& root, QString* outPath) {
    const QString path = tmp_->filePath(QStringLiteral("p.hmi.json"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    if (outPath) *outPath = path;
    return true;
}

// ============================================================================
// ---------- 公共层的判据 ----------
// ============================================================================

// 类型 ↔ 字符串 必须能原样往返 —— 工程文件里存的就是这个字符串。
void tst_ActionTemplate::typeStrings_roundTrip() {
    ActionType t = ActionType::LogOnly;
    const QString s = qwhmi::action::actionTypeToString(t);
    QVERIFY(!s.isEmpty());

    ActionType back = ActionType::WriteVariable;   // 故意给一个不一样的初值
    QVERIFY(qwhmi::action::actionTypeFromString(s, &back));
    QVERIFY(back == ActionType::LogOnly);

    ActionType t2 = ActionType::LogOnly;
    QVERIFY(qwhmi::action::actionTypeFromString(
        qwhmi::action::actionTypeToString(ActionType::WriteVariable), &t2));
    QVERIFY(t2 == ActionType::WriteVariable);
}

// 不认识的类型字符串必须【不】被当成某个类型。
//   ★ 这条守的是"工程文件比程序新"这条路 ——
//     认不出来就 fallback 成写变量,后果是【凭空多一个写 PLC 的动作】。
void tst_ActionTemplate::unknownTypeString_isNotSilentlyAccepted() {
    ActionType t = ActionType::WriteVariable;
    QVERIFY2(!qwhmi::action::actionTypeFromString(QStringLiteral("send_email"), &t),
             "★ 不认识的类型不许被接受。");
    QVERIFY2(!qwhmi::action::actionTypeFromString(QString(), &t),
             "★ 空类型不许被接受。");
    QVERIFY2(!qwhmi::action::actionTypeFromString(QStringLiteral("unknown_action_type"), &t),
             "★ 兜底串也不许被当成合法类型。");
}

// 写变量动作必须有变量名 —— 空名字会被 sink 当成"变量不存在",
// 而"你没配"和"变量被删了"是两回事,混在一起查不出来。
void tst_ActionTemplate::validateStep_rejectsEmptyVariableName() {
    QString why;
    QVERIFY2(!qwhmi::action::validateStep(writeStep(QString(), 0), &why),
             "★ 空变量名必须被判不合法。");
    QVERIFY2(!why.isEmpty(), "★ 必须说清楚为什么不合法。");

    QVERIFY(!qwhmi::action::validateStep(writeStep(QStringLiteral("   "), 0), &why));
    QVERIFY(qwhmi::action::validateStep(writeStep(QStringLiteral("Speed"), 0), &why));
}

// 只记日志的动作必须有文字。
void tst_ActionTemplate::validateStep_rejectsEmptyLogText() {
    QString why;
    QVERIFY2(!qwhmi::action::validateStep(logStep(QString()), &why),
             "★ 空文字必须被判不合法。");
    QVERIFY2(qwhmi::action::validateStep(logStep(QStringLiteral("已停机")), &why),
             "★ 有文字的只记日志动作必须是合法的。");
}

// 合法的一步 → 调 sink 的写变量。
void tst_ActionTemplate::executeStep_writeVariable_callsSink() {
    FakeSink sink(QStringLiteral("温度高"));
    const StepResult r = qwhmi::action::executeStep(writeStep(QStringLiteral("Speed"), 7), sink);

    QVERIFY(r.ok());
    QVERIFY(r.outcome == StepOutcome::Executed);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(sink.calls.first().kind, QStringLiteral("write"));
    QCOMPARE(sink.calls.first().a, QStringLiteral("Speed"));
    QCOMPARE(sink.calls.first().b, QStringLiteral("7"));
}

// 合法的一步 → 调 sink 的只记日志。
void tst_ActionTemplate::executeStep_logOnly_callsSink() {
    FakeSink sink(QStringLiteral("温度高"));
    const StepResult r = qwhmi::action::executeStep(logStep(QStringLiteral("已自动停机")), sink);

    QVERIFY(r.ok());
    QVERIFY(r.outcome == StepOutcome::Logged);
    QCOMPARE(sink.calls.size(), 1);
    QCOMPARE(sink.calls.first().kind, QStringLiteral("log"));
    QCOMPARE(sink.calls.first().a, QStringLiteral("已自动停机"));
}

// 不合法的一步 → 【不许】碰 sink。
//   ★ 这是最要紧的一条:"配置写错了"绝不能变成"照样往 PLC 写一个半截的值"。
void tst_ActionTemplate::executeStep_invalidStep_neverCallsSink() {
    FakeSink sink(QStringLiteral("温度高"));
    const StepResult r = qwhmi::action::executeStep(writeStep(QString(), 0), sink);

    QVERIFY2(!r.ok(), "★ 不合法的一步不许报成功。");
    QVERIFY(r.outcome == StepOutcome::InvalidStep);
    QVERIFY2(sink.calls.isEmpty(), "★ 不合法的一步一个都不许调 sink。");
}

// 不认识的类型 → 【不许】碰 sink。
void tst_ActionTemplate::executeStep_unknownType_neverCallsSink() {
    ActionStep step;
    step.type = static_cast<ActionType>(99);   // 工程文件里没见过的类型

    FakeSink sink(QStringLiteral("温度高"));
    const StepResult r = qwhmi::action::executeStep(step, sink);

    QVERIFY2(!r.ok(), "★ 不认识的类型不许报成功。");
    QVERIFY(r.outcome == StepOutcome::UnknownType);
    QVERIFY2(sink.calls.isEmpty(), "★ 不认识的类型不许调 sink。");
}

// 步骤必须【按配的顺序】执行。
//   ★ 顺序是有意义的:先"停机"再"留痕",和反过来不是一回事。
void tst_ActionTemplate::executeStep_keepsConfiguredOrder() {
    FakeSink sink(QStringLiteral("温度高"));
    ActionStep s1 = writeStep(QStringLiteral("Speed"), 0);
    ActionStep s2 = logStep(QStringLiteral("停机"));
    ActionStep s3 = writeStep(QStringLiteral("Valve"), 0);

    qwhmi::action::executeStep(s1, sink);
    qwhmi::action::executeStep(s2, sink);
    qwhmi::action::executeStep(s3, sink);

    QCOMPARE(sink.kinds(), QStringList({QStringLiteral("write"),
                                        QStringLiteral("log"),
                                        QStringLiteral("write")}));
    QCOMPARE(sink.calls.at(0).a, QStringLiteral("Speed"));
    QCOMPARE(sink.calls.at(1).a, QStringLiteral("停机"));
    QCOMPARE(sink.calls.at(2).a, QStringLiteral("Valve"));
}

// ============================================================================
// ---------- ActionManager:追加而不是覆盖 ----------
// ============================================================================

// ★★★ 本次修改的核心断言 ★★★
//   原来 addMapping 是 `mappings_[alarmName] = m;` ——
//   给同一条报警配第二个动作时,第一个被【悄悄顶掉】,界面上什么提示都没有。
void tst_ActionTemplate::addMapping_sameAlarmTwice_appendsInsteadOfOverwriting() {
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Valve"), 0);

    const QList<ActionTemplate> tpls = act_->templates();
    QVERIFY2(tpls.size() == 1, "★ 同一条报警应该只有【一个模板】(模板 = 一组动作)。");
    QVERIFY2(tpls.first().steps.size() == 2,
             "★ 配了两个动作就必须有两个步骤 —— 原来这里只有 1(被覆盖了)。");
    QCOMPARE(tpls.first().steps.at(0).variableName, QStringLiteral("Speed"));
    QCOMPARE(tpls.first().steps.at(1).variableName, QStringLiteral("Valve"));

    // 老的展平视图也要两条
    QVERIFY2(act_->mappings().size() == 2,
             "★ 展平视图也必须看得见两个动作 —— 少一个就等于有一个被吃掉了。");
}

// addTemplate 是【整组替换】—— 模板是"完整配置"。
void tst_ActionTemplate::addTemplate_replacesTheWholeGroup() {
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Speed"), 0);
    act_->addMapping(QStringLiteral("温度高"), QStringLiteral("Valve"), 0);

    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));
    act_->addTemplate(tpl);

    const QList<ActionTemplate> tpls = act_->templates();
    QCOMPARE(tpls.size(), 1);
    QVERIFY2(tpls.first().steps.size() == 1,
             "★ addTemplate 是整组替换 —— 之前那一步不该还留着。");
}

// mappings() 是【展平】视图:只有"写变量"那类步骤看得见。
void tst_ActionTemplate::mappings_isTheFlattenedWriteVariableView() {
    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));
    tpl.steps.append(logStep(QStringLiteral("停机")));
    tpl.steps.append(writeStep(QStringLiteral("Valve"), 0));
    act_->addTemplate(tpl);

    QVERIFY2(act_->mappings().size() == 2,
             "★ 展平视图只算写变量那类步骤(老工程格式只有这一种)。");
    QCOMPARE(act_->mappings().at(0).variableName, QStringLiteral("Speed"));
    QCOMPARE(act_->mappings().at(1).variableName, QStringLiteral("Valve"));
    // 完整信息在 templates() 里
    QCOMPARE(act_->templates().first().steps.size(), 3);
}

// ============================================================================
// ---------- ActionManager:真的按组执行 ----------
// ============================================================================

// 一条报警 → 整组动作【全部】执行,顺序不变。
void tst_ActionTemplate::oneAlarm_executesEveryStepInOrder() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                               Variable::DataType::UInt16,
                               Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    auto* valve = new Variable(QStringLiteral("Valve"), 3,
                               Variable::DataType::UInt16,
                               Variable::Access::ReadWrite, vm_);
    vm_->addVariable(valve);

    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));
    tpl.steps.append(logStep(QStringLiteral("温度高,已自动停机")));
    tpl.steps.append(writeStep(QStringLiteral("Valve"), 0));
    act_->addTemplate(tpl);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    act_->onAlarmTriggered(makeAlarm(QStringLiteral("温度高")));

    // 两个变量都被写了
    QCOMPARE(speed->value().toInt(), 0);
    QCOMPARE(valve->value().toInt(), 0);
    // 两个"写变量"步骤各发一次 actionExecuted("只记日志"那步不发)
    QCOMPARE(exec.count(), 2);
    // 顺序:先 Speed 后 Valve
    QCOMPARE(exec.at(0).at(1).toString(), QStringLiteral("Speed"));
    QCOMPARE(exec.at(1).at(1).toString(), QStringLiteral("Valve"));
}

// 一步失败【不】许把后面的步骤也一起吞掉。
//   ★ "停机写失败"不该导致"连留痕都不做"。
//     但失败的那一步【必须】留日志(不在这里断言日志,那是负向对照那边的事)。
void tst_ActionTemplate::aFailedStep_doesNotStopTheFollowingSteps() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                               Variable::DataType::UInt16,
                               Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);

    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(writeStep(QStringLiteral("不存在的变量"), 0));   // 这一步会失败
    tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));         // 这一步必须照样做
    act_->addTemplate(tpl);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    act_->onAlarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QVERIFY2(exec.count() == 1,
             "★ 前一步失败不许把后一步一起吞掉 —— 后一步必须执行。");
    QCOMPARE(exec.at(0).at(1).toString(), QStringLiteral("Speed"));
    QCOMPARE(speed->value().toInt(), 0);
}

// "只记日志"那一步不许碰任何变量。
void tst_ActionTemplate::logOnlyStep_doesNotTouchAnyVariable() {
    auto* speed = new Variable(QStringLiteral("Speed"), 2,
                               Variable::DataType::UInt16,
                               Variable::Access::ReadWrite, vm_);
    vm_->addVariable(speed);
    speed->setValue(QVariant::fromValue<quint16>(123));

    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(logStep(QStringLiteral("只留个痕")));
    act_->addTemplate(tpl);

    QSignalSpy exec(act_, &ActionManager::actionExecuted);
    act_->onAlarmTriggered(makeAlarm(QStringLiteral("温度高")));

    QCOMPARE(exec.count(), 0);
    QCOMPARE(speed->value().toInt(), 123);
}

// ============================================================================
// ---------- 存读 ----------
// ============================================================================

// 模板 → JSON → 模板,一个字段都不能少。
void tst_ActionTemplate::json_templateRoundTrip_keepsEveryStep() {
    ActionTemplate tpl;
    tpl.alarmName = QStringLiteral("温度高");
    tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));
    tpl.steps.append(logStep(QStringLiteral("已自动停机")));
    tpl.steps.append(writeStep(QStringLiteral("Valve"), 3));

    const QJsonObject obj = JsonSerializer::toActionTemplateJson(tpl);
    QCOMPARE(obj["alarm_name"].toString(), QStringLiteral("温度高"));
    QCOMPARE(obj["steps"].toArray().size(), 3);

    ActionTemplate back;
    QString err;
    QVERIFY2(JsonSerializer::actionTemplateFromJson(obj, &back, &err),
             qPrintable(QStringLiteral("读回来失败:%1").arg(err)));
    QCOMPARE(back.alarmName, tpl.alarmName);
    QCOMPARE(back.steps.size(), 3);
    QCOMPARE(back.steps.at(0).type, ActionType::WriteVariable);
    QCOMPARE(back.steps.at(0).variableName, QStringLiteral("Speed"));
    QCOMPARE(back.steps.at(0).value.toInt(), 0);
    QCOMPARE(back.steps.at(1).type, ActionType::LogOnly);
    QCOMPARE(back.steps.at(1).text, QStringLiteral("已自动停机"));
    QCOMPARE(back.steps.at(2).variableName, QStringLiteral("Valve"));
    QCOMPARE(back.steps.at(2).value.toInt(), 3);
}

// 不认识的类型 → 读失败,而且要说清楚。
void tst_ActionTemplate::json_unknownType_failsWithAReadableReason() {
    QJsonObject step;
    step["type"]      = QStringLiteral("send_email");
    step["variable"]  = QStringLiteral("Speed");
    step["write_value"] = 0;
    QJsonObject tpl;
    tpl["alarm_name"] = QStringLiteral("温度高");
    tpl["steps"]      = QJsonArray{step};

    ActionTemplate out;
    QString err;
    QVERIFY2(!JsonSerializer::actionTemplateFromJson(tpl, &out, &err),
             "★ 不认识的类型必须读失败(不能静默当成某个类型)。");
    QVERIFY2(err.contains(QStringLiteral("send_email")),
             qPrintable(QStringLiteral("错误信息里必须点名那个类型:%1").arg(err)));
}

// 不合法的一步 → 读失败,而且要说清楚是哪一步。
void tst_ActionTemplate::json_invalidStep_failsWithAReadableReason() {
    QJsonObject step;
    step["type"]       = QStringLiteral("write_variable");
    step["write_value"] = 0;          // ★ 故意不写 variable
    QJsonObject tpl;
    tpl["alarm_name"] = QStringLiteral("温度高");
    tpl["steps"]      = QJsonArray{step};

    ActionTemplate out;
    QString err;
    QVERIFY2(!JsonSerializer::actionTemplateFromJson(tpl, &out, &err),
             "★ 缺变量名的一步必须读失败。");
    QVERIFY2(err.contains(QStringLiteral("第 1 步")),
             qPrintable(QStringLiteral("错误信息必须说清是哪一步:%1").arg(err)));
}

// ============================================================================
// ---------- 工程文件 ----------
// ============================================================================

// ★ 老工程(只有 actions 数组)必须照旧能打开、联动照旧在。
//   扔掉老格式 = 老工程一打开联动全没了,而工程还"打开成功"。
void tst_ActionTemplate::project_legacyActionsOnly_stillLoads() {
    QJsonObject root = baseProject();
    QJsonObject act;
    act["alarm_name"]  = QStringLiteral("温度高");
    act["variable"]    = QStringLiteral("Speed");
    act["write_value"] = 0;
    root["actions"] = QJsonArray{act};
    // ★ 故意【不】写 action_templates —— 这就是一个纯老工程

    QString path;
    QVERIFY(writeProject(root, &path));

    VariableManager vm;
    AlarmManager    am;
    ActionManager   ag;
    PollScheduler   poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    QVERIFY2(p.loadFromFile(path), "★ 纯老工程必须照旧打得开。");
    QVERIFY2(ag.mappings().size() == 1, "★ 老工程里的联动必须还在。");
    QCOMPARE(ag.mappings().first().variableName, QStringLiteral("Speed"));
}

// action_templates 要【赢】老格式 actions。
//   ★ 加载端先读老的、再读新的,新的整组覆盖 ——
//     否则新工程里配的"第二步"会被老格式那条顶掉。
void tst_ActionTemplate::project_actionTemplates_winOverLegacyActions() {
    QJsonObject root = baseProject();

    // 老格式:一条
    QJsonObject act;
    act["alarm_name"]  = QStringLiteral("温度高");
    act["variable"]    = QStringLiteral("Speed");
    act["write_value"] = 0;
    root["actions"] = QJsonArray{act};

    // 新格式:同一条报警,两个步骤
    QJsonObject w1;
    w1["type"]        = QStringLiteral("write_variable");
    w1["variable"]    = QStringLiteral("Speed");
    w1["write_value"] = 0;
    QJsonObject w2;
    w2["type"]        = QStringLiteral("write_variable");
    w2["variable"]    = QStringLiteral("Valve");
    w2["write_value"] = 1;
    QJsonObject tpl;
    tpl["alarm_name"] = QStringLiteral("温度高");
    tpl["steps"]      = QJsonArray{w1, w2};
    root["action_templates"] = QJsonArray{tpl};

    QString path;
    QVERIFY(writeProject(root, &path));

    VariableManager vm;
    AlarmManager    am;
    ActionManager   ag;
    PollScheduler   poll(&vm, nullptr);
    Project p(&vm, &am, &ag, &poll, nullptr);

    QVERIFY2(p.loadFromFile(path), "★ 工程必须打得开。");
    const QList<ActionTemplate> tpls = ag.templates();
    QVERIFY2(tpls.size() == 1, "★ 应该只有一个模板。");
    QVERIFY2(tpls.first().steps.size() == 2,
             "★ 新格式的两个步骤都必须生效 —— 老格式那条只算一条。");
    QCOMPARE(tpls.first().steps.at(0).variableName, QStringLiteral("Speed"));
    QCOMPARE(tpls.first().steps.at(1).variableName, QStringLiteral("Valve"));
    QCOMPARE(tpls.first().steps.at(1).value.toInt(), 1);
}

// 存盘必须把 action_templates 写出去(不然下次打开这组动作就没了)。
void tst_ActionTemplate::project_savedFile_containsActionTemplates() {
    const QString path = tmp_->filePath(QStringLiteral("saved.hmi.json"));
    {
        VariableManager vm;
        AlarmManager    am;
        ActionManager   ag;
        PollScheduler   poll(&vm, nullptr);
        Project p(&vm, &am, &ag, &poll, nullptr);

        // ★ "Speed" 必须【真的存在】——
        //   存盘时引用了不存在变量的那一步会被跳过(悬空引用会让工程下次打不开),
        //   那条规则本身是对的,这里只是别被它干扰。
        vm.addVariable(new Variable(QStringLiteral("Speed"), 2,
                                    Variable::DataType::UInt16,
                                    Variable::Access::ReadWrite, &vm));

        // ★ 模板要加在【将被存盘的那一个】ActionManager 上。
        //   (加在成员 act_ 上就白测了 —— 存盘读的是 p 手里那一个。)
        ActionTemplate tpl;
        tpl.alarmName = QStringLiteral("温度高");
        tpl.steps.append(writeStep(QStringLiteral("Speed"), 0));
        tpl.steps.append(logStep(QStringLiteral("已自动停机")));
        ag.addTemplate(tpl);

        QVERIFY(p.saveToFile(path));
    }

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QVERIFY2(root.contains(QStringLiteral("action_templates")),
             "★ 存出来的工程里必须有 action_templates 段。");
    const QJsonArray tpls = root["action_templates"].toArray();
    QVERIFY2(tpls.size() == 1, "★ 应该存出一组动作。");
    QCOMPARE(tpls.first().toObject()["steps"].toArray().size(), 2);
    // 老格式也要写(前向兼容:老版本程序只认它)
    QVERIFY2(root.contains(QStringLiteral("actions")),
             "★ 老格式 actions 也要写 —— 老版本程序靠它。");
    QCOMPARE(root["actions"].toArray().size(), 1);   // 只有"写变量"那类
}

// 加载端和存盘端【都】得碰 action_templates ——
//   读源码文本是为了钉"接线在",不是钉"行为对"(行为上面几条已经钉了)。
void tst_ActionTemplate::project_source_readsAndWritesActionTemplates() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING "/Project.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 %1").arg(path)));
    QTextStream in(&f);
    const QString code = codeOnly(in.readAll());
    f.close();

    QVERIFY2(countOf(code, QStringLiteral("root[\"action_templates\"]")) >= 2,
             "★ Project.cpp 里必须有【读】和【写】两处 action_templates —— "
             "只写不读 = 存进去下次打开就没了;只读不写 = 配了存不下去。");
}

QTEST_GUILESS_MAIN(tst_ActionTemplate)
#include "tst_ActionTemplate.moc"

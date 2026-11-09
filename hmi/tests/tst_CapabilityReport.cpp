// ============================================================================
// tst_CapabilityReport.cpp —— "当前支持的功能"对话框不许说假话
//
// ============================================================================
// 【这个测试是为哪条问题写的】
//   2026-11-02:那个对话框里【有三句是假的】——
//     ✗ "多 PLC:可以配好存进工程文件,但目前只连第 1 台"
//     ✗ "多台 PLC 同时在线 —— 在做"
//     ✗ "界面上直接加/删 PLC —— 在做"
//   而这三件事【早就做完了】:
//     · PlcConnectionManager 会按工程里的 plcs 数组建 N 台 driver;
//     · 菜单「PLC → PLC 配置」里能加/删 PLC;
//     · 菜单「数据 → 变量配置」里能把变量分给哪台 PLC。
//
//   危害不是"文案不准"这么轻:
//     用户按说明去找"加 PLC"的入口,找不到 ——
//     于是【整个多 PLC 功能被当成不存在】。
//
// 【为什么以前没人发现】
//   那段文字【整段硬编码在 hmi.cpp 里】,而 hmi.cpp 不在 hmi_core 里
//   ⇒ 没有任何一条测试能看见它 ⇒ 它说什么就是什么。
//   （跟 G-26 同一个形状:主画面逻辑零覆盖。）
//
// 【本文件钉住什么】
//   ① 三句谎话不许再出现;
//   ② 跟运行时有关的部分(在线几台 PLC、协议能不能用)由【事实】驱动;
//   ③ "新建工程"确实还没有 —— 这条【现在是真的】,不许顺手删掉;
//   ④ ★ 最重要的一条:对话框【声称】多 PLC 能同时在线,
//      那就真的造两台出来看是不是两台 driver ——
//      声称与事实之间要有钉子,不然那还是一句没被验过的话。
//
// 关联:src/ui/CapabilityReport.h(设计说明)、hmi.cpp(onShowCapabilities)
// ============================================================================

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

#include "plc/PlcConnectionManager.h"
#include "ui/CapabilityReport.h"

using qwhmi::ui::CapabilityFacts;
using qwhmi::ui::buildCapabilitiesHtml;

namespace {

// 一台合法 PLC 的最小配置(字段名与 Project::defaultPlcFromTcp 一致)
QJsonObject makePlc(const QString& id, const QString& host, int port) {
    QJsonObject p;
    p["id"]       = id;
    p["protocol"] = QStringLiteral("modbus_tcp");
    p["host"]     = host;
    p["port"]     = port;
    return p;
}

// 去掉注释再比对 —— hmi.cpp 的注释里会写着这些名字
// (说明它们为什么被抽出来),不去掉就会把"注释提到了"当成"代码还在",
// 测试永远绿。手法同 tst_MenuBar.cpp:947 / tst_ModbusPanel.cpp:90。
QString codeOnly(const QString& text) {
    QString out = text;
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                                  QRegularExpression::DotMatchesEverythingOption));
    out.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return out;
}

// 从源码里抠出某个函数的函数体(按大括号配平)。
static QString extractFunctionBody(const QString& code, const QString& signature) {
    const int sigAt = code.indexOf(signature);
    if (sigAt < 0) return QString();
    const int openAt = code.indexOf(QLatin1Char('{'), sigAt);
    if (openAt < 0) return QString();
    int depth = 0;
    for (int i = openAt; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0) return code.mid(openAt + 1, i - openAt - 1);
        }
    }
    return QString();
}

} // namespace

class tst_CapabilityReport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- ① 谎话不许再出现 ----
    void noFalseClaimsAboutMultiPlc();

    // ---- ② 多 PLC 这条必须说成【做完了】,并带上真实台数 ----
    void multiPlcIsStatedAsWorking_withTheLiveCount();

    // ---- ③ 协议能不能用由事实驱动,不是写死 ----
    void protocolAvailability_followsTheFacts();

    // ---- ④ 真的没有的,也要说出来 ----
    void newProjectIsStillListed_asNotSupported();

    // ---- ⑤ hmi.cpp 真的用了共享模块吗 ----
    void hmiSource_usesTheSharedReport();

    // ---- ⑥ ★ 它声称的事,是真的吗 ★ ----
    void multiPlcClaim_isActuallyTrue();
};

void tst_CapabilityReport::initTestCase() {
    // 一次都不该在 initTestCase 里判红 —— 判据全在各条用例里。
}

void tst_CapabilityReport::noFalseClaimsAboutMultiPlc() {
    CapabilityFacts f;
    f.plcCount = 3;
    const QString html = buildCapabilitiesHtml(f);

    // ★ 三句谎话,逐句钉。
    //   写法用【整句】而不是关键词 —— 关键词容易被别的真话误伤,
    //   而整句不会:改文案的人想改这句话,就一定会碰到这三条断言。
    QVERIFY2(!html.contains(QStringLiteral("只连第 1 台")),
             "★ 对话框还在说『目前只连第 1 台』—— 这已经是假的了:"
             "PlcConnectionManager 会按工程里的 plcs 数组建 N 台 driver。"
             "用户按这句去找,只会以为多 PLC 不存在。");
    QVERIFY2(!html.contains(QStringLiteral("多台 PLC 同时在线")),
             "★ 对话框还在说『多台 PLC 同时在线 —— 在做』—— "
             "它【早就做完了】(tst_PlcConnectionManager::threePlcs_getThreeDistinctDrivers 钉着)。");
    QVERIFY2(!html.contains(QStringLiteral("界面上直接加/删 PLC")),
             "★ 对话框还在说『界面上直接加/删 PLC —— 在做』—— "
             "菜单「PLC → PLC 配置」里【一直】就有添加/删除按钮(PlcConfigDialog)。");
}

void tst_CapabilityReport::multiPlcIsStatedAsWorking_withTheLiveCount() {
    CapabilityFacts f;
    f.plcCount = 3;
    const QString html = buildCapabilitiesHtml(f);

    QVERIFY2(html.contains(QStringLiteral("多 PLC")),
             "★ 对话框里没有『多 PLC』这一条 —— 用户不知道这个功能存在。");
    QVERIFY2(html.contains(QStringLiteral("PLC 配置")),
             "★ 对话框没有告诉用户【入口在哪儿】。"
             "只说『能做』不说『去哪儿做』,用户还是找不到。");
    QVERIFY2(html.contains(QStringLiteral("3")),
             "★ 对话框没有带上【当前在线几台】—— "
             "这是个运行时事实,得由调用方带进来(DriverFactory/PlcConnectionManager)。");

    // 台数必须真的出现在正文里(不是别处的 "3")
    QVERIFY2(html.contains(QStringLiteral("当前在线")) &&
                 html.contains(QStringLiteral("3")),
             "★ 『当前在线 N 台』这一句没出现在正文里。");

    // ★ 台数变了,文字必须跟着变 —— 说明它【不是】写死的
    CapabilityFacts g;
    g.plcCount = 7;
    QVERIFY2(buildCapabilitiesHtml(g) != html,
             "★ 台数从 3 改成 7,生成的文字却一字不差 —— "
             "说明『当前在线 N 台』是写死的常量,不是事实。");
}

void tst_CapabilityReport::protocolAvailability_followsTheFacts() {
    // 现状:S7 / EIP 都是骨架,isUsable 恒 false
    CapabilityFacts off;
    off.plcCount = 1;
    const QString greyed = buildCapabilitiesHtml(off);
    QVERIFY2(greyed.contains(QStringLiteral("灰")),
             "★ 协议不可用时,对话框没有说它是灰的 —— "
             "用户会以为那儿根本没有 S7/EIP 这个选项。");
    QVERIFY2(greyed.contains(QStringLiteral("未实现")),
             "★ 协议不可用时,对话框没说清是『未实现』。"
             "『灰的』只讲颜色,不讲原因 —— 用户需要知道的是原因。");
    QVERIFY2(!greyed.contains(QStringLiteral("已支持")),
             "★ 协议明明还不可用,对话框却说『已支持』。");

    // 将来真接上了(Snap7 / libplctag 落地),这句话必须自己跟着变
    CapabilityFacts on;
    on.plcCount = 1;
    on.s7Usable = true;
    on.eipUsable = true;
    const QString usable = buildCapabilitiesHtml(on);
    QVERIFY2(usable.contains(QStringLiteral("已支持")),
             "★ 协议已经可用,对话框还是那套『灰的/未实现』的说法 —— "
             "写死的文案在协议接上那天还在骗人。");
}

void tst_CapabilityReport::newProjectIsStillListed_asNotSupported() {
    CapabilityFacts f;
    f.plcCount = 1;
    const QString html = buildCapabilitiesHtml(f);

    // ★ 这条【现在是真的】,不许被顺手删掉 ——
    //   "只写真话"的意思是【真的没有的也要写出来】。
    //   证据:MenuBuilder.cpp:138 明确没做这一项,
    //   tst_MenuBar 还扫全菜单禁止出现"新建"字样。
    QVERIFY2(html.contains(QStringLiteral("新建工程")),
             "★ 『新建工程』那条不见了 —— "
             "它现在【是真的没做】(MenuBuilder.cpp:138),"
             "不说的话用户会以为新建工程能做,去找一圈才发现没有。");
}

void tst_CapabilityReport::hmiSource_usesTheSharedReport() {
    const QString path = QStringLiteral(HMI_SRC_DIR_STRING) + QStringLiteral("/hmi.cpp");
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("读不到 hmi.cpp: ") + path));
    const QString code = codeOnly(QString::fromUtf8(f.readAll()));

    const QString body = extractFunctionBody(code, QStringLiteral("void hmi::onShowCapabilities"));
    QVERIFY2(!body.isEmpty(),
             "★ hmi.cpp 里找不到 onShowCapabilities 的函数体 —— "
             "主窗口那个槽被改名或挪走了?");

    // ★ 必须真的调共享模块
    QVERIFY2(body.contains(QStringLiteral("buildCapabilitiesHtml")),
             "★ hmi.cpp 的 onShowCapabilities 里没有调 qwhmi::ui::buildCapabilitiesHtml —— "
             "文案又被搬回主窗口里硬编码了,而那正是它当初开始说假话的原因"
             "(hmi.cpp 不在 hmi_core 里,没人能测)。");

    // ★ 而且【不许】再有硬编码的 <li> 清单
    QVERIFY2(!body.contains(QStringLiteral("<li>")),
             "★ hmi.cpp 的 onShowCapabilities 里还有硬编码的 <li> 清单 —— "
             "文案有两个出处,迟早走偏(而现在这一个是假的)。");

    // ★ 运行时事实必须真的从运行时取
    QVERIFY2(body.contains(QStringLiteral("driverCount")),
             "★ hmi.cpp 没有用 plcMgr_->driverCount() 取『当前在线几台』—— "
             "那个数字是写死的还是猜的?");
}

void tst_CapabilityReport::multiPlcClaim_isActuallyTrue() {
    // ★★★ 最要紧的一条 ★★★
    //   对话框【声称】多 PLC 能同时在线 —— 那就真的造两台出来看。
    //   没有这一条,"声称"就只是一句话,跟原来的谎话一样没有分量。
    //
    //   造的 PLC 指向没人监听的端口 —— connectTo 会失败,
    //   但【driver 实例照样建出来】(这正是 2b 的设计:
    //   建不出来才进 rejected)。
    PlcConnectionManager mgr;

    QJsonArray two;
    two.append(makePlc(QStringLiteral("plc1"), QStringLiteral("127.0.0.1"), 15501));
    two.append(makePlc(QStringLiteral("plc2"), QStringLiteral("127.0.0.1"), 15502));

    const auto result = mgr.applyConfig(two);

    QCOMPARE(mgr.driverCount(), 2);
    QVERIFY2(mgr.driverFor(QStringLiteral("plc1")) != nullptr &&
                 mgr.driverFor(QStringLiteral("plc2")) != nullptr,
             "★ 配了两台 PLC,却没建出两台 driver —— "
             "那对话框里『多 PLC:能加、能同时在线』就是假的,得改文案。");
    QVERIFY2(mgr.driverFor(QStringLiteral("plc1")) != mgr.driverFor(QStringLiteral("plc2")),
             "★ 两台 PLC 共用了同一个 driver 实例 —— "
             "现场表现是三台 PLC 共用一个 socket,最后一次连接的地址赢。");
    QCOMPARE(result.added.size(), 2);

    // ★ 而"当前在线 N 台"这个数字,就是从这里来的
    CapabilityFacts f;
    f.plcCount = mgr.driverCount();
    QVERIFY2(buildCapabilitiesHtml(f).contains(QStringLiteral("当前在线")),
             "★ 生成的文案里没有『当前在线』这几个字。");
}

QTEST_MAIN(tst_CapabilityReport)
#include "tst_CapabilityReport.moc"

// ============================================================================
// tst_ModbusRequestParser —— 钉住 G-20:调试面板绝不能发出【广播写】
//
// ============================================================================
//  【这条缺陷是什么】
//    hmi.cpp 里点"发送"按钮那段,原来长这样:
//
//        int slaveId = ui.modbusSlaveEdit->text().toInt(&ok);
//        int addr    = ui.modbusAddrEdit->text().toInt(&ok);   ★ ok 被覆盖
//        if (!ok || slaveId < 0 || slaveId > 255) { ...; return; }
//
//    ① 两个 toInt 共用同一个 ok ⇒ 第二次调用把第一次的结果整个盖掉。
//       从站号那一栏填了垃圾("ok"、字母、空),地址那一栏填的是对的
//       ⇒ ok 变成 true ⇒ 检查通过。
//    ② toInt 失败时【返回 0】⇒ slaveId = 0。
//    ③ 0 是 Modbus 的【广播地址】—— 写下去整条总线上的设备都会被写。
//       而原来的范围检查是 "0-255",【把 0 放行了】。
//
//    ⇒ 操作员在"从站号"栏里手滑打错一个字符,现场一整条总线被写一遍,
//      而界面上只显示"写成功"。
//
//    另外两条同源的洞,一并修了:
//      · executePendingModbus()(连上再发那条路)重新读 UI 文本且不校验;
//      · executeModbus() 自己再去 UI 上读一遍值列表,绕过了数量上限。
//
//  【本文件钉的是什么】
//    ① 从站号 0 一律拒,而且【要说清楚为什么】(广播)。
//    ② 从站号不是整数 ⇒ 拒(不许"猜"成 0)。
//    ③ 地址不是整数 / 越界 ⇒ 拒。
//    ④ 读数量、写值个数按 Modbus 协议上限。
//    ⑤ 正常路径【必须还能用】—— 只钉"该拒的拒了"不够,
//       那样一个"永远拒绝"的实现也能全绿。
//
//  【负向对照(必须做)】
//    把 ModbusRequestParser.cpp 里 "slaveId == 0" 那个分支去掉(放行 0)
//    ⇒ 用例 slaveId_zero_isRejected_becauseItIsBroadcast 立刻红。
//    详见那个用例里的说明。
//
//  【为什么它单独一个文件,不并进 tst_ModbusPanel】
//    tst_ModbusPanel 读的是 .ui / hmi.cpp 的【源码文本】,验的是"面板长什么样"。
//    本文件验的是"解析出来的东西对不对"—— 两件事,两种失败含义。
// ============================================================================

#include <QtTest>

#include "plc/ModbusRequestParser.h"

using namespace qwhmi::plc;

class tst_ModbusRequestParser : public QObject {
    Q_OBJECT

private slots:
    // ---- 从站号 ----
    void slaveId_zero_isRejected_becauseItIsBroadcast();
    void slaveId_garbage_isRejected_notSilentlyZero();
    void slaveId_outOfRange_isRejected();
    void slaveId_valid_isAccepted();

    // ---- 地址 ----
    void address_garbage_isRejected();
    void address_outOfRange_isRejected();

    // ---- 数量 / 值 ----
    void readCount_limits_followTheProtocol();
    void writeValueCount_limits_followTheProtocol();
    void writeValues_acceptHexNotation();

    // ---- 正常路径必须还能用 ----
    void aNormalReadRequest_parsesToWhatWasTyped();
    void aNormalWriteRequest_parsesToWhatWasTyped();

    // ---- 失败时【一个字段都不许通过】----
    void aRejectedRequest_leavesTheSpecUntouched();
};

// ---------------------------------------------------------------------------
// ① 从站号 0 = 广播
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::slaveId_zero_isRejected_becauseItIsBroadcast() {
    RequestSpec spec;
    QString err;
    const bool ok = parseRequestSpec("0", "10", "1", /*isRead=*/true,
                                     RegisterKind::HoldingRegisters, &spec, &err);

    QVERIFY2(!ok,
             "★ 从站号 0 必须被拒 ★\n"
             "  0 是 Modbus 的广播地址:写下去整条总线上的每一台设备都会被写。\n"
             "  这条用例是本文件的负向对照 —— 把 ModbusRequestParser.cpp 里\n"
             "  \"slaveId == 0\" 那个分支去掉,这里立刻红。");

    QVERIFY2(err.contains("广播"),
             qPrintable(QString("拒绝理由里必须说明【这是广播地址】,否则操作员不知道"
                                 "自己填的 0 意味着什么。实际理由:「%1」").arg(err)));
    QVERIFY2(err.contains("1-247"),
             qPrintable(QString("拒绝理由里必须给出【合法范围】。实际理由:「%1」").arg(err)));
}

// ---------------------------------------------------------------------------
// ② 不是整数 ⇒ 拒(绝不许"猜"成 0)
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::slaveId_garbage_isRejected_notSilentlyZero() {
    // ★ 这一条钉的是"失败返回 0"这个形状 ★
    //   QString::toInt 失败时返回 0 —— 那正是原缺陷的要害:
    //   "解析失败"和"用户填了 0"分不开,于是广播地址被当成合法输入放行。
    const QStringList garbage = {
        QStringLiteral("ok"), QStringLiteral(""), QStringLiteral("  "),
        QStringLiteral("1a"), QStringLiteral("0x10"), QStringLiteral("1.5"),
        QStringLiteral("一"),
    };
    for (const QString& g : garbage) {
        RequestSpec spec;
        QString err;
        const bool ok = parseRequestSpec(g, "10", "1", true,
                                         RegisterKind::HoldingRegisters, &spec, &err);
        QVERIFY2(!ok,
                 qPrintable(QString("从站号填「%1」必须被拒 —— 它不是整数,"
                                     "绝不能被当成 0(广播)放行。").arg(g)));
        QVERIFY2(!err.isEmpty(),
                 qPrintable(QString("从站号填「%1」被拒时必须给出理由。").arg(g)));
    }
}

// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::slaveId_outOfRange_isRejected() {
    for (const QString& g : {QStringLiteral("248"), QStringLiteral("255"),
                             QStringLiteral("256"), QStringLiteral("-1")}) {
        RequestSpec spec;
        QString err;
        QVERIFY2(!parseRequestSpec(g, "10", "1", true,
                                   RegisterKind::HoldingRegisters, &spec, &err),
                 qPrintable(QString("从站号「%1」必须在 1-247,不该被接受。").arg(g)));
    }
}

void tst_ModbusRequestParser::slaveId_valid_isAccepted() {
    for (int id = 1; id <= 247; ++id) {
        RequestSpec spec;
        QString err;
        QVERIFY2(parseRequestSpec(QString::number(id), "10", "1", true,
                                   RegisterKind::HoldingRegisters, &spec, &err),
                 qPrintable(QString("从站号 %1 是合法的,不该被拒:「%2」").arg(id).arg(err)));
        QCOMPARE(spec.slaveId, id);
    }
}

// ---------------------------------------------------------------------------
// ③ 地址
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::address_garbage_isRejected() {
    for (const QString& g : {QStringLiteral("ok"), QStringLiteral(""),
                             QStringLiteral("abc"), QStringLiteral("1.5")}) {
        RequestSpec spec;
        QString err;
        QVERIFY2(!parseRequestSpec("1", g, "1", true,
                                   RegisterKind::HoldingRegisters, &spec, &err),
                 qPrintable(QString("地址填「%1」必须被拒。").arg(g)));
    }
}

void tst_ModbusRequestParser::address_outOfRange_isRejected() {
    RequestSpec spec;
    QString err;
    QVERIFY(!parseRequestSpec("1", "65536", "1", true,
                              RegisterKind::HoldingRegisters, &spec, &err));
    QVERIFY(!parseRequestSpec("1", "-1", "1", true,
                              RegisterKind::HoldingRegisters, &spec, &err));
    // 地址 + 数量越出地址空间
    QVERIFY(!parseRequestSpec("1", "65530", "10", true,
                              RegisterKind::HoldingRegisters, &spec, &err));
}

// ---------------------------------------------------------------------------
// ④ 数量 / 值
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::readCount_limits_followTheProtocol() {
    RequestSpec spec;
    QString err;

    // 寄存器:1-125
    QVERIFY(parseRequestSpec("1", "0", "125", true,
                             RegisterKind::HoldingRegisters, &spec, &err));
    QCOMPARE(spec.count, 125);
    QVERIFY(!parseRequestSpec("1", "0", "126", true,
                              RegisterKind::HoldingRegisters, &spec, &err));

    // 位:1-2000
    QVERIFY(parseRequestSpec("1", "0", "2000", true,
                             RegisterKind::Coils, &spec, &err));
    QCOMPARE(spec.count, 2000);
    QVERIFY(!parseRequestSpec("1", "0", "2001", true,
                              RegisterKind::Coils, &spec, &err));

    // 0 和负数都不行
    QVERIFY(!parseRequestSpec("1", "0", "0", true,
                              RegisterKind::HoldingRegisters, &spec, &err));
    QVERIFY(!parseRequestSpec("1", "0", "-1", true,
                              RegisterKind::HoldingRegisters, &spec, &err));
}

void tst_ModbusRequestParser::writeValueCount_limits_followTheProtocol() {
    RequestSpec spec;
    QString err;

    // 寄存器:1-123(比读少 2 —— 写要带字节计数)
    QStringList vals;
    for (int i = 0; i < 123; ++i) vals << QStringLiteral("1");
    QVERIFY2(parseRequestSpec("1", "0", vals.join(QLatin1Char(',')), false,
                              RegisterKind::HoldingRegisters, &spec, &err),
             qPrintable(QString("123 个寄存器应当允许:「%1」").arg(err)));
    QCOMPARE(spec.values.size(), 123);

    vals << QStringLiteral("1");
    QVERIFY(!parseRequestSpec("1", "0", vals.join(QLatin1Char(',')), false,
                              RegisterKind::HoldingRegisters, &spec, &err));

    // 空值列表不行
    QVERIFY(!parseRequestSpec("1", "0", "", false,
                              RegisterKind::HoldingRegisters, &spec, &err));
    QVERIFY(!parseRequestSpec("1", "0", "  ,  ", false,
                              RegisterKind::HoldingRegisters, &spec, &err));
}

void tst_ModbusRequestParser::writeValues_acceptHexNotation() {
    // 值列表保留 0x 前缀 —— 调试人员是从从站里抄十六进制来的。
    RequestSpec spec;
    QString err;
    QVERIFY2(parseRequestSpec("1", "0", "0xFF,0x00,255", false,
                              RegisterKind::HoldingRegisters, &spec, &err),
             qPrintable(QString("0x 前缀应当被接受:「%1」").arg(err)));
    QCOMPARE(spec.values.size(), 3);
    QCOMPARE(spec.values.at(0), quint16(0xFF));
    QCOMPARE(spec.values.at(1), quint16(0x00));
    QCOMPARE(spec.values.at(2), quint16(255));

    // 越界的值要拒
    QVERIFY(!parseRequestSpec("1", "0", "0x10000", false,
                              RegisterKind::HoldingRegisters, &spec, &err));
}

// ---------------------------------------------------------------------------
// ⑤ 正常路径必须还能用
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::aNormalReadRequest_parsesToWhatWasTyped() {
    RequestSpec spec;
    QString err;
    QVERIFY2(parseRequestSpec("3", "40001", "10", true,
                              RegisterKind::HoldingRegisters, &spec, &err),
             qPrintable(QString("一个正常的读请求不该被拒:「%1」").arg(err)));
    QCOMPARE(spec.slaveId, 3);
    QCOMPARE(spec.address, 40001);
    QCOMPARE(spec.count, 10);
    QVERIFY(spec.isRead);
    QVERIFY(spec.kind == RegisterKind::HoldingRegisters);
    QVERIFY(spec.values.isEmpty());
}

void tst_ModbusRequestParser::aNormalWriteRequest_parsesToWhatWasTyped() {
    RequestSpec spec;
    QString err;
    QVERIFY2(parseRequestSpec("5", "100", "7,8,9", false,
                              RegisterKind::HoldingRegisters, &spec, &err),
             qPrintable(QString("一个正常的写请求不该被拒:「%1」").arg(err)));
    QCOMPARE(spec.slaveId, 5);
    QCOMPARE(spec.address, 100);
    QCOMPARE(spec.values.size(), 3);
    QCOMPARE(spec.values.at(0), quint16(7));
    QCOMPARE(spec.values.at(2), quint16(9));
    QVERIFY(!spec.isRead);
}

// ---------------------------------------------------------------------------
// 失败时一个字段都不许通过
// ---------------------------------------------------------------------------
void tst_ModbusRequestParser::aRejectedRequest_leavesTheSpecUntouched() {
    // ★ 这一条钉的是"部分成功"这个形状 ★
    //   如果解析器先把从站号写进 out、然后才发现地址不合法,
    //   调用方要是没检查返回值就照样发出去,又是一条广播写。
    //   所以约定:【任何一个字段没过,out 一个字节都不许动】。
    RequestSpec spec;
    spec.slaveId = 99;          // 放一个哨兵值
    spec.address = 12345;
    QString err;
    const bool ok = parseRequestSpec("0", "not-a-number", "1", true,
                                     RegisterKind::HoldingRegisters, &spec, &err);
    QVERIFY(!ok);
    QCOMPARE(spec.slaveId, 99);      // 没被动过
    QCOMPARE(spec.address, 12345);   // 没被动过
}

QTEST_GUILESS_MAIN(tst_ModbusRequestParser)
#include "tst_ModbusRequestParser.moc"

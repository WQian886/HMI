// ============================================================================
// 阶段 C · C.4 · 协议切换 UX 锁单测(教训 1.5)
//
// 验证 ProtocolGuard.isCrossProtocolChange() 在 6 种场景下的行为:
//   1. 同协议(modbus→modbus)→ false(允许)
//   2. 同协议大小写不同(MODBUS→modbus)→ false(允许)
//   3. 跨协议(modbus→s7)→ true(阻止)
//   4. 跨协议大小写不同(modbus→S7)→ true(阻止)
//   5. 同协议带前后空格(modbus→" modbus ")→ false(允许)
//   6. 跨协议(modbus→eip)→ true(阻止)
//
// 关联:ProtocolGuard.h, hmi.cpp::onProtocolComboChanged
// ============================================================================

#include <QtTest>
#include "drivers/ProtocolGuard.h"

using namespace qwhmi::drivers;

class TestProtocolSwitch : public QObject {
    Q_OBJECT
private slots:
    void sameProtocol_returnsFalse();
    void sameProtocolCaseInsensitive_returnsFalse();
    void crossProtocol_returnsTrue();
    void crossProtocolCaseInsensitive_returnsTrue();
    void sameProtocolWithWhitespace_returnsFalse();
    void allThreeProtocolCombinationsAreRejected();
};

void TestProtocolSwitch::sameProtocol_returnsFalse() {
    QVERIFY(!isCrossProtocolChange("modbus", "modbus"));
}

void TestProtocolSwitch::sameProtocolCaseInsensitive_returnsFalse() {
    QVERIFY(!isCrossProtocolChange("MODBUS", "modbus"));
    QVERIFY(!isCrossProtocolChange("Modbus", "MODBUS"));
    QVERIFY(!isCrossProtocolChange("S7", "s7"));
}

void TestProtocolSwitch::crossProtocol_returnsTrue() {
    QVERIFY(isCrossProtocolChange("modbus", "s7"));
    QVERIFY(isCrossProtocolChange("s7", "eip"));
    QVERIFY(isCrossProtocolChange("modbus", "eip"));
}

void TestProtocolSwitch::crossProtocolCaseInsensitive_returnsTrue() {
    QVERIFY(isCrossProtocolChange("Modbus", "S7"));
    QVERIFY(isCrossProtocolChange("S7", "EIP"));
}

void TestProtocolSwitch::sameProtocolWithWhitespace_returnsFalse() {
    QVERIFY(!isCrossProtocolChange("modbus", " modbus "));
    QVERIFY(!isCrossProtocolChange(" modbus ", "modbus"));
}

void TestProtocolSwitch::allThreeProtocolCombinationsAreRejected() {
    // 3 种协议相互切换都算跨协议(规划 C.4 UX 锁)
    const QStringList protocols = {"modbus", "s7", "eip"};
    for (const QString& from : protocols) {
        for (const QString& to : protocols) {
            if (from != to) {
                QVERIFY2(isCrossProtocolChange(from, to),
                         qPrintable(QString("expected cross: %1→%2").arg(from).arg(to)));
            }
        }
    }
}

QTEST_APPLESS_MAIN(TestProtocolSwitch)
#include "tst_ProtocolSwitch.moc"

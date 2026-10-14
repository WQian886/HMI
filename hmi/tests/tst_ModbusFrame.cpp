// ============================================================================
// 阶段 C · ModbusMaster 帧组装基线(改善清单 #13,2026-10-14 新增)
// ============================================================================
//
// 【为什么这个文件现在才存在】
// ModbusMaster 从阶段 2 起就是核心类,69 个测试目标里【零个】
// 断言过它组出来的任何一个字节。要做改善清单 #13(把手写大端
// `static_cast<char>((x>>8)&0xFF)` 换成 `qToBigEndian`)时才发现:
// 清单把 #13 标成"纯可读性,零行为变化",而"零行为变化"这句话
// 【没有任何测试支撑】—— 改完没有任何东西能证明字节一样。
//
// 这正是本项目反复吃的坑(efb0f56 F1 孤儿行 / 屏蔽列只读文字):
// 【测试绿 ≠ 功能对】。反过来也成立:【没有测试 ≠ 改动安全】。
// 一次"零行为变化"的重构,在零覆盖的模块上等于闭眼改协议字节 ——
// 而字节序错了是从站不响应的经典原因,现场表现为"一直超时"。
//
// 【本文件的纪律】
//   1. 先钉住【当前实际字节】,不改一行生产代码 —— 这就是基线。
//   2. 改完生产代码,这个文件必须【一个字节都不动】仍然全绿。
//      如果它红了,说明重构真的改变了行为,那是 bug 不是"测试没跟上"。
//   3. 任何断言要能对住"字节序反了"这件事 —— 高低字节不对称的值
//      (如 0x1234、TID 0x0102)才咬得住,全 0x00/0xFF 的值证明不了什么。
//
// 关联:../ModbusMaster.cpp buildFrame/sendReadRequest/sendWriteRequest
//      ../ModbusMaster.h BUGFIX-A1(TID 曾因 quint32→quint16 截断而永远匹配不上)
// ============================================================================

#include <QtTest>
#include <QByteArray>

#include "ModbusMaster.h"

class TestModbusFrame : public QObject {
    Q_OBJECT

private slots:
    // ---- buildFrame:MBAP 头 ----
    void mbap_header_isSevenBytes_tidBigEndian_lenIsUidFcBody();
    void tid_highAndLowBytes_areSwappedIfWrongEndian();
    void protocol_field_isAlwaysZero();
    void length_field_countsUidFcBody_NotItself();
    void unitId_isTruncatedToOneByte();

    // ---- buildFrame:边界 ----
    void tid_0xFFFF_doesNotTruncate();          // BUGFIX-A1 的直接回归
    void largeUnitId_isTruncatedNotOverflowed();
    void emptyBody_stillProducesSevenByteHeader();

    // ---- PDU body 组装(读/写路径)----
    void readRequest_body_isAddrThenCount_bigEndian();
    void writeSingleRegister_body_isAddrThenValue();
    void writeMultipleRegisters_valuesAreBigEndian();
    void writeMultipleCoils_bitPack_lsbFirst();
};

// 把 QByteArray 变成可读的十六进制串,断言失败时一眼看出差在哪个字节。
// "期望 0x0102..." 这种字符串比 QCOMPARE 两个 QByteArray 好读得多 ——
// 后者失败时只打印 "Compared values are not the same"。
static QString hex(const QByteArray& b) {
    QString s;
    for (char c : b) {
        s += QStringLiteral("%1").arg(static_cast<quint8>(c), 2, 16, QLatin1Char('0'));
    }
    return s;
}

static QByteArray bytes(std::initializer_list<int> vals) {
    QByteArray b;
    for (int v : vals) b.append(static_cast<char>(v));
    return b;
}

// ============================================================================
// buildFrame:MBAP 头
// ============================================================================

// MBAP 头 7 字节 = TID(2) + Protocol(2) + Length(2) + UID(1)。
// Length 的定义是【其后字节数】,也就是 UID(1) + FC(1) + body,
// 【不含 Length 自己那 2 字节】。这条最容易写错成 2+body。
void TestModbusFrame::mbap_header_isSevenBytes_tidBigEndian_lenIsUidFcBody() {
    ModbusMaster m;
    const QByteArray body = bytes({0x00, 0x01});   // addr = 1
    const QByteArray f = m.buildFrameForTest(0x0001, 1, 0x03, body);

    QCOMPARE(f.size(), 7 + 1 + body.size());      // 7 头 + 1 FC + 2 body
    // TID 大端:0x00 0x01
    QCOMPARE(hex(f.mid(0, 2)), QStringLiteral("0001"));
    // Protocol 恒为 0
    QCOMPARE(hex(f.mid(2, 2)), QStringLiteral("0000"));
    // Length = 1(UID) + 1(FC) + 2(body) = 4
    QCOMPARE(hex(f.mid(4, 2)), QStringLiteral("0004"));
    // UID
    QCOMPARE(static_cast<quint8>(f.at(6)), quint8(1));
    // FC
    QCOMPARE(static_cast<quint8>(f.at(7)), quint8(0x03));
    QCOMPARE(f.mid(8), body);
}

// ★ 这条是整个文件的核心:TID 必须是【大端】。
//   0x1234 拆成 0x12 0x34;写成小端就是 0x34 0x12 —— 帧长一样、看着像模像样,
//   但从站要么不认、要么把响应 echo 成同一个错序,TID 对不上就永远超时。
//   这类 bug 在没有真 PLC 的情况下【只有】这条断言能挡住。
void TestModbusFrame::tid_highAndLowBytes_areSwappedIfWrongEndian() {
    ModbusMaster m;

    const QByteArray f1234 = m.buildFrameForTest(0x1234, 1, 0x03, QByteArray());
    QCOMPARE(hex(f1234.mid(0, 2)), QStringLiteral("1234"));   // 高字节在前

    // 再钉两个不对称的值,防止"碰巧对了一个"
    const QByteArray fABCD = m.buildFrameForTest(0xABCD, 1, 0x03, QByteArray());
    QCOMPARE(hex(fABCD.mid(0, 2)), QStringLiteral("abcd"));

    const QByteArray f0102 = m.buildFrameForTest(0x0102, 1, 0x03, QByteArray());
    QCOMPARE(hex(f0102.mid(0, 2)), QStringLiteral("0102"));   // 高字节=1,低字节=2
}

void TestModbusFrame::protocol_field_isAlwaysZero() {
    ModbusMaster m;
    // 传各种 tid/UID,protocol 两位都必须是 00 —— Modbus TCP 规定如此。
    for (quint16 tid : {quint16(0x0000), quint16(0xFFFF), quint16(0x1234)}) {
        const QByteArray f = m.buildFrameForTest(tid, 9, 0x10, QByteArray());
        QCOMPARE(hex(f.mid(2, 2)), QStringLiteral("0000"));
    }
}

void TestModbusFrame::length_field_countsUidFcBody_NotItself() {
    ModbusMaster m;
    // body 长度 0 / 1 / 2 / 5 时 Length 分别应是 2 / 3 / 4 / 7。
    // 【为什么单独一条】"Length 把自己也算进去"是一个能通过肉眼看懂的错误,
    // 但它只在某个特定 body 长度下才表现得出来,所以每个长度都要钉。
    struct { int bodyLen; int expectLen; } cases[] = {
        {0, 2}, {1, 3}, {2, 4}, {5, 7},
    };
    for (auto& c : cases) {
        const QByteArray body(c.bodyLen, '\xAA');
        const QByteArray f = m.buildFrameForTest(0x0001, 1, 0x03, body);
        const int got = (static_cast<quint8>(f.at(4)) << 8) | static_cast<quint8>(f.at(5));
        QCOMPARE(got, c.expectLen);
    }
}

void TestModbusFrame::unitId_isTruncatedToOneByte() {
    ModbusMaster m;
    // UID 字段只有 1 字节,超过 255 的部分必须被丢掉而不是溢出到 Length。
    QCOMPARE(static_cast<quint8>(m.buildFrameForTest(0x0001, 0x01,   0x03, QByteArray()).at(6)), quint8(0x01));
    QCOMPARE(static_cast<quint8>(m.buildFrameForTest(0x0001, 0xFF,   0x03, QByteArray()).at(6)), quint8(0xFF));
    QCOMPARE(static_cast<quint8>(m.buildFrameForTest(0x0001, 0x0100, 0x03, QByteArray()).at(6)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(m.buildFrameForTest(0x0001, 0x0107, 0x03, QByteArray()).at(6)), quint8(0x07));
}

// ============================================================================
// buildFrame:边界
// ============================================================================

// ★ BUGFIX-A1(2026-07-30)的直接回归。
//   当时 nextTid_ 是 quint32,而 buildFrame 形参是 quint16,
//   TID > 0xFFFF 时发出去的是低 16 位、pending_ key 存的却是完整 32 位,
//   响应永远匹配不上 → 3 秒超时 → 循环。
//   现场现象就是"TID=65543"反复弹窗。
//   这条断言守着"0xFFFF 能原样进出" —— 截断在这里就会被咬到。
void TestModbusFrame::tid_0xFFFF_doesNotTruncate() {
    ModbusMaster m;
    const QByteArray f = m.buildFrameForTest(0xFFFF, 1, 0x03, QByteArray());
    QCOMPARE(hex(f.mid(0, 2)), QStringLiteral("ffff"));
}

void TestModbusFrame::largeUnitId_isTruncatedNotOverflowed() {
    ModbusMaster m;
    const QByteArray f = m.buildFrameForTest(0x0001, 0x1FF, 0x03, QByteArray());
    // UID 取低 8 位 = 0xFF,且【不得】影响 Length 两位
    QCOMPARE(static_cast<quint8>(f.at(6)), quint8(0xFF));
    QCOMPARE(hex(f.mid(4, 2)), QStringLiteral("0002"));
}

void TestModbusFrame::emptyBody_stillProducesSevenByteHeader() {
    ModbusMaster m;
    const QByteArray f = m.buildFrameForTest(0x0001, 1, 0x03, QByteArray());
    QCOMPARE(f.size(), 8);        // 7 头 + 1 FC,body 为空
    QCOMPARE(hex(f.mid(4, 2)), QStringLiteral("0002"));
}

// ============================================================================
// PDU body
//
// ★ 这三条调的是【生产代码本身】(ModbusMaster::buildReadBody / buildWriteBody),
//   不是在测试里重抄一遍拼字节再断言自己。
//   【为什么强调】这些 body 原来是内联在 sendReadRequest / sendWriteRequest 里的,
//   要真连上 PLC 才走得到 —— 本机没有 PLC,那些字节从来没被验证过。
//   2026-10-14 提成静态纯函数后,这里才第一次能断言它们。
//
//   如果哪天发现这里的断言又变成"在测试里重抄生产逻辑",那就等于没测 ——
//   删了生产代码测试照样绿,正是本项目最熟的那个坑。
// ============================================================================

void TestModbusFrame::readRequest_body_isAddrThenCount_bigEndian() {
    // Modbus 读请求 PDU:起始地址(2,大端) + 寄存器数(2,大端)
    QCOMPARE(hex(ModbusMaster::buildReadBody(0x1234, 0x0010)),
             QStringLiteral("12340010"));
    QCOMPARE(hex(ModbusMaster::buildReadBody(0x0000, 0x0001)),
             QStringLiteral("00000001"));
}

void TestModbusFrame::writeSingleRegister_body_isAddrThenValue() {
    // 写单个保持寄存器(0x06):addr(2) + value(2),【没有 byteCount】
    QCOMPARE(hex(ModbusMaster::buildWriteBody(0x06, 0x0100, {0xABCD})),
             QStringLiteral("0100abcd"));
    // 写单个线圈(0x05)格式相同
    QCOMPARE(hex(ModbusMaster::buildWriteBody(0x05, 0x0100, {0x0001})),
             QStringLiteral("01000001"));
}

void TestModbusFrame::writeMultipleRegisters_valuesAreBigEndian() {
    // 写多个保持寄存器(0x10):
    //   addr(2) + count(2) + byteCount(1) + 每值 2 字节大端
    QCOMPARE(hex(ModbusMaster::buildWriteBody(0x10, 0x0100, {0x1234, 0xABCD})),
             QStringLiteral("01000002041234abcd"));
}

void TestModbusFrame::writeMultipleCoils_bitPack_lsbFirst() {
    // ★ 写多个线圈(0x0F):addr(2) + count(2) + byteCount(1) + bit-packed(LSB first)
    //
    //   【byteCount 必须向上取整】9 个线圈 = 2 字节。
    //   写成 9/8=1(整除截断)会把第 9 个线圈整条丢掉 —— 而从站不会报错,
    //   只是第 9 个线圈一直不动。属于最难查的一类静默错误。
    //
    //   逐字节推一遍(9 个线圈全为 1,LSB first):
    //     线圈 0..7 → byte0 八个 bit 全置上 = 0xFF
    //     线圈 8    → byte1 的 bit0 = 0x01
    //     byteCount = ceil(9/8) = 2
    //   ⇒ 0000(addr) 0009(count) 02(byteCount) ff 01
    //   ★ 这条断言第一次写的时候我【手算错了】,把 byte0 写成了 02。
    //     是跑出来 0000000902ff01 之后回头按协议重推才发现是断言错、代码对。
    //     记在这儿是因为它正是"照着代码改断言"的反面教材:
    //     先按协议算,算不出这个值才去怀疑代码。
    QCOMPARE(hex(ModbusMaster::buildWriteBody(0x0F, 0x0000,
                 {1,1,1,1,1,1,1,1,1})),
             QStringLiteral("0000000902ff01"));

    // 顺手钉住 LSB first 这个方向 —— 这条【必须】单独钉,上面那条区分不出来:
    //  9 个线圈全为 1 时 byte0=ff、byte1=01,把两个字节都填满再错位也看不出。
    //  这里只留两个 1(线圈 0 和线圈 8),其余全 0,按协议推:
    //    线圈 0 → byte0 的 bit0 = 0x01
    //    线圈 8 → byte1 的 bit0 = 0x01
    //    byteCount 仍是 2(因为 coil 8 存在,即便它前面的 8 个里有 0)
    //  ⇒ 0000(addr) 0009(count) 02(byteCount) 01 01
    //  如果把 LSB first 写成 MSB first,byte0 的 1 会跑到 bit7 = 0x80。
    QCOMPARE(hex(ModbusMaster::buildWriteBody(0x0F, 0x0000,
                 {1,0,0,0,0,0,0,0,1})),
             QStringLiteral("00000009020101"));
}

QTEST_MAIN(TestModbusFrame)
#include "tst_ModbusFrame.moc"

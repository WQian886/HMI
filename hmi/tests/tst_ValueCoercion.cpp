// tst_ValueCoercion.cpp —— 直接钉住 util/ValueCoercion 的每一条 coercion 规则。
//
// ★ 为什么必须有这条(覆盖体检挖出来的缺口)
//   coerceToDataType() 是 G-15(联动 24h 一次都没写出去)的根因修复:
//   Variable::setValue 只认精确 typeId,而调用方手里的 QVariant 类型五花八门
//   (JSON 的 0 → LongLong、C++ 字面量 0 → Int、控件算的 → Double)。
//   不适配 = 静默 Rejected = 画面不动、PLC 不动。
//
//   这个文件头部自己写着"修掉了一个真实存在的 bug,别把它改回去"——
//   那个 bug 是【两套编号碰撞】:v.typeId()(QMetaType id) 跟 static_cast<int>(DataType)
//   (DataType 枚举序号) 有几处【恰好相等】(Int16=1==Bool=1、UInt16=2==Int=2…),
//   旧的"已经是对的类型就别动"用错编号比,把 UInt16 变量误判成"已是对的类型",
//   原样把 QVariant(Int) 返回 → setValue 照样拒收 → 适配【静默失效】。
//
//   ★ 之前只被 tst_ActionWrite 的两条端到端用例间接覆盖(走 ActionManager 那条路)。
//     一旦有人"优化"逻辑把碰撞 bug 引回来,只有那 2 条 ActionManager 路径能发现,
//     通用的【规则矩阵】完全没钉住 —— 这正是本项目反复吃的亏(见 G-23~G-28)。
//
//   ★ 本测试逐条钉:
//     · 已经是对的类型 → 原样返回(且 float→float 不丢精度)
//     · ★ typeId 碰撞:QVariant(Int) 给 UInt16 → 必须转成 UShort,不能是误判"已是对的类型"
//     · JSON 风格 QVariant(LongLong) 给 UInt16 → 转成 UShort
//     · Double → Float → 转成 Float
//     · 负数 → UInt16 → 非法(不静默回绕成 65535)
//     · 溢出(Int16/UInt16 上下界外)→ 非法
//     · 字符串 "abc" → Bool / Float → 非法(不静默当 0)
//     · 字符串 "3.5" → Float → 3.5f
//     · 非法/空 QVariant → 非法
//   其中 Int→UInt16 那条,若碰撞 bug 复发(返回原 Int),断言 result.typeId()!=UShort 立即红。
#include <QtTest>
#include <QVariant>

#include "util/ValueCoercion.h"
#include "Variable.h"

class TestValueCoercion : public QObject
{
    Q_OBJECT
private slots:
    // ★ 已经是对的类型 → 原样返回,且 float 不丢精度
    void alreadyCorrectType_returnedAsIs();
    void floatToFloat_noPrecisionLoss();

    // ★★★ 核心碰撞 bug:QVariant(Int) 给 UInt16 变量必须适配成 UShort,不能误判"已是对的类型"
    void intLiteralToUInt16_mustCoerceNotReject();
    // JSON 风格 LongLong 给 UInt16 → UShort
    void longLongToUInt16_coerces();

    // Double → Float
    void doubleToFloat_coerces();

    // 负数给无符号 → 非法(不回绕成 65535)
    void negativeToUInt16_invalid();
    // 溢出 → 非法
    void overflowUInt16_invalid();
    void overflowInt16_invalid();

    // 字符串非法值 → 非法(不静默当 0)
    void stringToBool_invalid();
    void stringToFloat_invalid();
    void stringFloatToFloat_valid();

    // 空 / 非法 QVariant → 非法
    void invalidVariant_invalid();

    // Int → Bool(非 0 即真)
    void intToBool();
};

void TestValueCoercion::alreadyCorrectType_returnedAsIs()
{
    const QVariant in = QVariant::fromValue<quint16>(5);
    const QVariant out = qwhmi::util::coerceToDataType(in, Variable::DataType::UInt16);
    QVERIFY(out.isValid());
    QCOMPARE(out.typeId(), QVariant::fromValue<quint16>(0).typeId()); // UShort
    QCOMPARE(out.value<quint16>(), quint16(5));
}

void TestValueCoercion::floatToFloat_noPrecisionLoss()
{
    const float f = 3.5f;
    const QVariant in = QVariant::fromValue<float>(f);
    const QVariant out = qwhmi::util::coerceToDataType(in, Variable::DataType::Float);
    QVERIFY(out.isValid());
    QCOMPARE(out.typeId(), QVariant::fromValue<float>(0.0f).typeId()); // Float
    QCOMPARE(out.value<float>(), f); // 原样返回,精度不丢
}

void TestValueCoercion::intLiteralToUInt16_mustCoerceNotReject()
{
    // ★ 这正是头部详述的碰撞 bug:DataType::UInt16 的序号是 2,
    //   QMetaType::Int 的 id 也是 2 —— 旧代码拿 static_cast<int>(t) 比会误判"已是对的类型"。
    //   正确行为:QVariant(Int 5) 必须被适配成 QVariant(UShort 5)。
    const QVariant in = QVariant::fromValue<int>(5);
    QCOMPARE(in.typeId(), QMetaType::Int); // 确认输入确实是 Int(不是别的)
    const QVariant out = qwhmi::util::coerceToDataType(in, Variable::DataType::UInt16);
    QVERIFY(out.isValid());
    // 若碰撞 bug 复发,这里会原样返回 Int → typeId 仍是 Int(2) ≠ UShort(36) → 断言红。
    QCOMPARE(out.typeId(), QVariant::fromValue<quint16>(0).typeId()); // UShort
    QCOMPARE(out.value<quint16>(), quint16(5));
}

void TestValueCoercion::longLongToUInt16_coerces()
{
    // JSON 整数 → QVariant(LongLong),给 UInt16 变量必须经适配分支转成 UShort。
    const QVariant in = QVariant::fromValue<qint64>(5);
    QCOMPARE(in.typeId(), QMetaType::LongLong);
    const QVariant out = qwhmi::util::coerceToDataType(in, Variable::DataType::UInt16);
    QVERIFY(out.isValid());
    QCOMPARE(out.typeId(), QVariant::fromValue<quint16>(0).typeId()); // UShort
    QCOMPARE(out.value<quint16>(), quint16(5));
}

void TestValueCoercion::doubleToFloat_coerces()
{
    const QVariant in = QVariant::fromValue<double>(3.5);
    QCOMPARE(in.typeId(), QMetaType::Double);
    const QVariant out = qwhmi::util::coerceToDataType(in, Variable::DataType::Float);
    QVERIFY(out.isValid());
    QCOMPARE(out.typeId(), QVariant::fromValue<float>(0.0f).typeId()); // Float
    QCOMPARE(out.value<float>(), 3.5f);
}

void TestValueCoercion::negativeToUInt16_invalid()
{
    // 负数塞无符号:必须非法,绝不静默回绕成 65535(那会让"停机"联动变成"最高速")。
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant::fromValue<int>(-1), Variable::DataType::UInt16);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::overflowUInt16_invalid()
{
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant::fromValue<qint64>(70000), Variable::DataType::UInt16);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::overflowInt16_invalid()
{
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant::fromValue<int>(70000), Variable::DataType::Int16);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::stringToBool_invalid()
{
    // 字符串不接 Bool:否则 "abc".toDouble()=0 会被当成 false 静默吞掉。
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant(QStringLiteral("abc")), Variable::DataType::Bool);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::stringToFloat_invalid()
{
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant(QStringLiteral("abc")), Variable::DataType::Float);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::stringFloatToFloat_valid()
{
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant(QStringLiteral("3.5")), Variable::DataType::Float);
    QVERIFY(out.isValid());
    QCOMPARE(out.typeId(), QVariant::fromValue<float>(0.0f).typeId()); // Float
    QCOMPARE(out.value<float>(), 3.5f);
}

void TestValueCoercion::invalidVariant_invalid()
{
    const QVariant out = qwhmi::util::coerceToDataType(
        QVariant(), Variable::DataType::UInt16);
    QVERIFY(!out.isValid());
}

void TestValueCoercion::intToBool()
{
    QCOMPARE(qwhmi::util::coerceToDataType(
                 QVariant::fromValue<int>(1), Variable::DataType::Bool).value<bool>(), true);
    QCOMPARE(qwhmi::util::coerceToDataType(
                 QVariant::fromValue<int>(0), Variable::DataType::Bool).value<bool>(), false);
}

QTEST_GUILESS_MAIN(TestValueCoercion)
#include "tst_ValueCoercion.moc"

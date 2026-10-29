// ============================================================================
// util/ValueCoercion.cpp —— 见 ValueCoercion.h 的文件头
//
// ★★ 这里修掉了一个【真实存在】的 bug,别把它改回去 ★★
//
//   ControlBase.cpp 里原来那份实现的"已经是对的类型就别动"是这么写的:
//       if (v.typeId() == static_cast<int>(t)) return v;
//
//   ★ 这是拿【两套不同的编号】在比 ★
//     v.typeId() 返回的是 QMetaType 的 id:
//       Bool=1, Int=2, UInt=3, LongLong=4, ULongLong=5, Double=6,
//       Long=32, Short=33, Char=34, ULong=35, UShort=36, UChar=37, Float=38
//     而 static_cast<int>(t) 是 Variable::DataType 的【枚举序号】:
//       Bool=0, Int16=1, UInt16=2, Int32=3, Float=4
//     两套编号【碰巧】有几处相等:
//       DataType::Int16  (1) == QMetaType::Bool   (1)
//       DataType::UInt16 (2) == QMetaType::Int    (2)
//       DataType::Int32  (3) == QMetaType::UInt   (3)
//       DataType::Float  (4) == QMetaType::LongLong(4)
//
//   ⇒ 后果:传一个 QVariant(Int) 给 UInt16 变量时,那句"已经是对的类型"
//     会【误判成立】,原样把 QVariant(Int) 返回 ——
//     而 Variable::setValue 要的是 QMetaType::UShort(36) → 还是 Rejected。
//     适配【静默地没起作用】。
//
//   ★ 这个 bug 在 ControlBase 里一直没被发现,是因为它只在
//     "数值类型 + 数值类型"之间才犯:ControlBase 那边的控件传的是 double,
//     而 QMetaType::Double 的 id 是 6,不跟任何 DataType 序号撞上,
//     所以那条路一直是好的。
//     一旦有人拿 QVariant(Int) 来(比如 C++ 里写 addMapping(..., 0)),
//     立刻就露馅 —— 而这正是 ActionManager 那条路。
//
//   ★★ 而【JSON 那条路给的不是 QVariant(Int)】★★
//     【实测】(挂真 Qt 6.8.3 跑探针出来的):
//       JSON 整数 → QVariant(LongLong),typeId = 4
//     LongLong(4) 恰好 == DataType::Float(4) —— 也撞上了!
//     但撞的是 Float 那一格,不是 UInt16 那一格。
//     ⇒ 对 UInt16 变量来说:JSON 的 0(LongLong)【不会】误判成"已经是对的类型",
//       它会老老实实走下面的转换分支(转成 UShort,成功);
//       而 C++ 字面量 0(Int)【会】误判成"已经是对的类型",原样放行 → 还是 Rejected。
//     —— 也就是说:【JSON 那条路真正坏在别处】
//       (QJsonValue::toVariant() 给的 LongLong 跟 UShort(36) 对不上,
//        旧的短路没误判,但也没【主动去转】,于是 setValue 照样拒)。
//     而 C++ 字面量那条路【坏在短路本身】。
//     两条路都坏,坏法不同 —— 所以本文件两条都测。
//     (tst_ActionWrite.cpp 里两条各有一条用例。)
//
//   【正确的判据】是"这个 typeId 是不是【这个 DataType 唯一认可的那个 id】",
//   不是"两个编号相等"。下面的 matchesExactly() 就是这个判据。
// ============================================================================

#include "util/ValueCoercion.h"

#include "Variable.h"

#include <limits>

namespace qwhmi::util {

namespace {

// ★ DataType → 它唯一被 Variable::setValue 认可的 QMetaType id。
//   这张表必须跟 Variable.cpp:35-45 的 matchesDataType 一一对应 ——
//   那边认什么,这边就转成什么。两边漂移了,症状是"适配了但还是被拒"。
//
//   ★ 为什么用【显式映射表】而不是再写一遍 `v.typeId() == QMetaType::UShort`:
//     写成映射,一眼就能跟 matchesDataType 对照;
//     写成散在 switch 里的比较,两边各写一遍,迟早漂移。
int exactMetaTypeId(Variable::DataType t) {
    switch (t) {
        case Variable::DataType::Bool:   return QMetaType::Bool;
        case Variable::DataType::Int16:  return QMetaType::Short;
        case Variable::DataType::UInt16: return QMetaType::UShort;
        case Variable::DataType::Int32:  return QMetaType::Int;
        case Variable::DataType::Float:  return QMetaType::Float;
    }
    return 0;   // 走不到的兜底
}

// ★ 值【已经】是这个 DataType 要的精确类型吗?
//   —— 注意这里比的是"QMetaType id",不是"DataType 序号"。
bool matchesExactly(const QVariant& v, Variable::DataType t) {
    return v.typeId() == exactMetaTypeId(t);
}

} // namespace

QVariant coerceToDataType(const QVariant& v, Variable::DataType t) {
    if (!v.isValid()) return {};

    // 已经是精确类型 → 原样返回,别走一趟无意义的转换
    //   (float→float 先转 double 再转回 float 会丢精度)
    if (matchesExactly(v, t)) return v;

    switch (t) {
    case Variable::DataType::Bool: {
        // 非 0 即真。
        // ★ 字符串不接:变量值来自 PLC 寄存器,不是用户输入。
        //   这里接了字符串的话,"false" 会被 toDouble() 变成 0 —— 但
        //   "false" 本来是想表达 false,巧合对了;"abc" 呢?toDouble() 返 0
        //   且 ok=false,于是"abc" 变成 false —— 静默地把非法输入当真值。
        //   宁可拒绝,也不能猜。
        if (v.typeId() == QMetaType::QString) return {};
        return QVariant::fromValue<bool>(v.toDouble() != 0.0);
    }

    case Variable::DataType::Int16: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        if (!ok || n < std::numeric_limits<qint16>::min()
                 || n > std::numeric_limits<qint16>::max()) return {};
        return QVariant::fromValue<qint16>(static_cast<qint16>(n));
    }

    case Variable::DataType::UInt16: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        // ★ 无符号:负数直接判非法,不静默回绕成 65535 ——
        //   那会把 -1 写成 65535,现场表现是"写个 -1 结果寄存器变最大正数",
        //   极难排查(而且在"停机"这类联动上,方向正好是反的)。
        if (!ok || n < 0 || n > std::numeric_limits<quint16>::max()) return {};
        return QVariant::fromValue<quint16>(static_cast<quint16>(n));
    }

    case Variable::DataType::Int32: {
        bool ok = false;
        const qint64 n = v.toLongLong(&ok);
        if (!ok || n < std::numeric_limits<qint32>::min()
                 || n > std::numeric_limits<qint32>::max()) return {};
        return QVariant::fromValue<qint32>(static_cast<qint32>(n));
    }

    case Variable::DataType::Float: {
        // toFloat 失败会返回 0 且 ok=false —— 必须查 ok,
        // 否则用户输 "abc" 会静默写 0(把"看不懂的值"当成"零")。
        if (v.typeId() == QMetaType::QString) {
            bool ok = false;
            const float f = v.toString().toFloat(&ok);
            return ok ? QVariant::fromValue<float>(f) : QVariant();
        }
        return QVariant::fromValue<float>(v.toFloat());
    }
    }
    return {};
}

} // namespace qwhmi::util

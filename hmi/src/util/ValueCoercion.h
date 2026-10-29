// ============================================================================
// util/ValueCoercion.h —— 把"来路不明的 QVariant"适配成 Variable 声明的精确类型
//
// 文件职责:一个函数 —— coerceToDataType()。
//   回答的问题是:"这个值要写进一个 UInt16 变量,该用哪个 QVariant 装它?"
//
// ★ 为什么需要它(2026-10-25 新增,BUGFIX-C5)
//   ------------------------------------------------------------------
//   Variable::setValue 的类型校验是【严格 typeId 相等】:
//     Bool 只认 QMetaType::Bool(1)、Int16 只认 QMetaType::Short(33)、
//     UInt16 只认 QMetaType::UShort(36)、Int32 只认 QMetaType::Int(2)、
//     Float 只认 QMetaType::Float(38)。
//   而调用方手里的值【类型五花八门】:
//     · JSON 里写 "write_value": 0        → QVariant(LongLong)  ★实测
//     · C++ 里写 addMapping(..., 0)        → QVariant(Int)
//     · 控件内部用 double 算              → QVariant(Double)
//   不适配就等于【写不进去】:setValue 返 Rejected,值没设上,
//   而 Rejected 在很多调用点是【静默的】—— 画面不动、PLC 不动。
//
//   ★ 这不是假想。24 小时长跑实测(soaks/20261021_164804):
//     工程里配了两条联动("温度高"/"温度低" → 把 Speed 写 0),
//     Speed 是 UInt16,write_value 是 JSON 的 0 → QVariant(LongLong)
//     → 严格类型校验拒收 → 【一次都没写出去】。
//     从站日志里 FC6/FC16 计数 = 0。
//     而这条联动在现场是"温度高就停机" —— 一条从没生效过的安全动作。
//
//   ★★ 关于 "JSON 的 0 是 QVariant(Int)" —— 那是【错的】★★
//     【实测】(挂真 Qt 6.8.3 跑探针出来的,不是读头文件推断的):
//       JSON 整数   → QVariant(LongLong),typeId = 4
//       JSON 小数   → QVariant(Double), typeId = 6
//       JSON 字符串 → QVariant(QString),typeId = 10
//     理由:Qt 6 的 QJsonValue::Type 枚举里【没有单独的 Integer】,
//     只有 Bool/Double/String —— 但"类型枚举里只有 Double"
//     【不等于】toVariant() 给的是 Double。实测给的是 LongLong。
//     (我凭头文件推断过两次,两次都错。写注释时按实测写。)
//
// ★ 为什么不把宽松化放进 Variable::setValue
//   ------------------------------------------------------------------
//   Variable 是数据层最底层的类,它的严格性【有测试在守】
//   (tst_Variable::setValue_typeMismatch 断言 float 写不进 Int16)。
//   把校验放松掉,那些测试就得改 —— 而它们守的是对的。
//   本项目的既定做法是【在调用方适配】:
//     · tst_ControlBaseWrite.cpp:188 修过一次一模一样的 bug
//       ("写回通道形同虚设"),改法就是在 writeToPlc 里适配。
//     · Binding.cpp:130-138 的 inline edit 同样在调用方适配。
//   本文件就是把那份适配【抽出来共用】,不是再抄一份。
//
// ★ 为什么单独成文件(而不是留在 ControlBase 里)
//   ------------------------------------------------------------------
//   ControlBase 那份是【控件的私有实现】,在 .cpp 的匿名 namespace 里,
//   别的模块用不了 —— 于是 ActionManager 只能"再写一份",
//   而"再写一份"正是本项目反复出事的地方
//   (src/ui/MenuBuilder.cpp / src/alarm/AlarmTablePresenter.cpp 都是同一个道理:
//   抽出来,两边共用同一份)。
//
// 关联:Variable.cpp:35-45(matchesDataType)、ControlBase.cpp(原匿名 namespace 那份)、
//      Binding.cpp:130-138、ActionManager.cpp(新增调用点)
// ============================================================================

#pragma once

#include <QVariant>

// ★ 必须真 include,不能只前向声明。
//   DataType 是【嵌套枚举】,而嵌套类型要求外围类【是完整类型】才能命名 ——
//   "class Variable;" 只能用来写 Variable*,写不了 Variable::DataType。
//   (踩过:前向声明版编出来是 C2027 未定义类型 + C2061 语法错误。)
#include "Variable.h"

namespace qwhmi::util {

// ★ 把 v 适配成 Variable::DataType t 要求的精确 QVariant。
//
//   返回值:
//     · 有效 QVariant —— 已按 t 转好,可以直接交给 Variable::setValue
//     · 无效 QVariant —— 转不了(字符串不是数字 / 溢出 / 负数塞进无符号)。
//       调用方必须【当成非法值处理】,不能"当作 0"继续往下走。
//
//   ★ 拒绝负数 → UInt16,不静默回绕成 65535。
//     写个 -1 结果寄存器变最大正数,现场表现是"我明明要停机,
//     结果设备跑到了最高速" —— 这比直接报错危险得多。
//
//   ★ 为什么"已经是对的类型"要原样返回(而不是也走一遍转换)
//     float→float 走一遍转换会丢精度(先转 double 再转回 float)。
//     所以这里只在【类型确实不对】时才动手。
QVariant coerceToDataType(const QVariant& v, Variable::DataType t);

} // namespace qwhmi::util

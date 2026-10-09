// ============================================================================
// 2026-10-10 · 协议下拉框的"未实现"标注
//
// 【为什么单独一个文件,而不是写在 hmi.cpp 里】
//   这段逻辑一开始是直接写在 hmi 构造函数里的,配套的测试只能把代码
//   【复制一份】到测试文件里跑 —— 那样测的是副本,不是生产代码。
//   hmi.cpp 里那份要是被谁改错了(少调一次、循环条件写反),测试照样全绿。
//   —— 这正是本项目反复吃的亏(CI 那个 .github 位置、filters 漏 27 条,
//   都是"看起来在验证,其实没验证到真正跑的那份")。
//
//   抽出来之后 hmi.cpp 和测试调的是同一个函数,测试绿了才有意义。
//
// 文件职责:给 QComboBox 里 isUsable()==false 的项加"未实现"标注。
//   不负责选不选得动 —— 那由 onProtocolComboChanged 兜(见该函数注释)。
//
// 关联:drivers/DriverFactory.{h,cpp}, hmi.cpp 构造函数
// ============================================================================

#pragma once

class QComboBox;
class QString;

namespace qwhmi::drivers {

// 把下拉框里"还没实现"的协议项置灰 + 加"(未实现)"后缀 + 挂 tooltip。
//
// 【置灰 ≠ 禁用,这是刻意的】
//   QComboBox 没有"禁用某一项"的 API(只有整框 setEnabled)。
//   常见做法是装 QStyledItemDelegate 在 isEnabled() 返回 false,
//   但那样用户点下去【什么都不会发生】—— 连弹窗都看不到,
//   而"为什么不让我选"恰恰是他此刻最需要知道的。
//   现在的选择:置灰 + 文字标注 + tooltip 讲原因,
//   真选下去时 onProtocolComboChanged 弹"协议尚未实现"并撤回下拉框。
//
// 【可重复调用】每次都从当前文字重算,不会叠加出 "S7(未实现)(未实现)"。
void applyProtocolAvailabilityStyling(QComboBox* combo);

} // namespace qwhmi::drivers

// ============================================================================
// 阶段 C · C.1 · IPollDriver 编译单元
//
// 文件职责:本文件几乎为空,只是为了触发 moc + 给链接器一个 .obj 文件
//   链接 IPollDriver::readFinished / writeFinished / connected / disconnected
//   / errorOccurred / staticMetaObject 等 moc 生成的符号。
//
//   因为 IPollDriver 是 abstract class(所有方法都是纯虚函数),不写 .cpp
//   时 msvc 会找不到 moc 符号(尤其是 staticMetaObject)。
//
// 关联:IPollDriver.h
// ============================================================================

#include "drivers/IPollDriver.h"

// 故意留空。include 头文件让 moc 看到 Q_OBJECT 宏就行。

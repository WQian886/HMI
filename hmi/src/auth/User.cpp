// ============================================================================
// 阶段 B · B1 · User 实现
//
// 只放 namesEqual 一个工具函数;User 本体是 POD。
// 关联:User.h
// ============================================================================

#include "auth/User.h"

namespace qwhmi::auth {

bool User::namesEqual(const QString& a, const QString& b) {
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace qwhmi::auth
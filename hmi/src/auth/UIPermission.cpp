// ============================================================================
// 阶段 B · B4 · UI 权限绑定(实现)
//
// 关联:UIPermission.h
//
// 实现细节:
//   - 全局 QHash<QObject*, Entry> 注册表(function-local static,首次访问时构造)
//   - applyAllBindings() 首次调用时 connect CurrentUser 的 userLoggedIn/
//     userLoggedOut → applyAllBindings(函数指针,Qt 6 自动丢多余参数)
//   - applyOne 用 qobject_cast 区分 QAbstractButton 和 QAction,各自调 setEnabled
//
// 为什么用 qobject_cast 而不是 Entry 里存类型指针:
//   - 类型信息天然在 QObject 子类里,QObject 单一继承链用 qobject_cast 零开销
//   - 避免 Entry 持两种类型指针的 union / variant 复杂度
// ============================================================================

#include "auth/UIPermission.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"

#include <QAbstractButton>
#include <QAction>
#include <QHash>
#include <QObject>
#include <QPointer>

namespace qwhmi::auth {

namespace {

struct Entry {
    QPointer<QObject> target;
    Permission         perm;
    bool               isAction;   // 区分 QAbstractButton 和 QAction
};

// 全局注册表(function-local static,跨调用持久,进程退出时自动析构)
QHash<QObject*, Entry>& registry() {
    static QHash<QObject*, Entry> r;
    return r;
}

// 单条应用:按当前 CurrentUser.role 检查权限,设置 enabled
void applyOne(QObject* target, Permission p, bool isAction) {
    if (!target) return;
    const bool allowed = checkPermission(CurrentUser::instance().role(), p);
    if (isAction) {
        if (auto* act = qobject_cast<QAction*>(target)) {
            act->setEnabled(allowed);
        }
    } else {
        if (auto* btn = qobject_cast<QAbstractButton*>(target)) {
            btn->setEnabled(allowed);
        }
    }
}

} // namespace (匿名)

void bindButtonEnable(QAbstractButton* btn, Permission p) {
    if (!btn) return;
    Entry e;
    e.target   = btn;
    e.perm     = p;
    e.isAction = false;
    registry().insert(btn, e);
    // ★ 调 applyAllBindings 而不是 applyOne:首次调用会连 CurrentUser signal,
    //   后续切用户 / 注销 都能自动重算所有绑定
    applyAllBindings();
}

void bindActionEnable(QAction* act, Permission p) {
    if (!act) return;
    Entry e;
    e.target   = act;
    e.perm     = p;
    e.isAction = true;
    registry().insert(act, e);
    applyAllBindings();
}

void applyAllBindings() {
    // ★ 首次调用时钩 CurrentUser 信号,后续切换用户自动重算
    //   函数局部 static 保证 connect 只发生一次(重复 connect 会重复触发槽)
    static bool connected = false;
    if (!connected) {
        if (auto* cu = CurrentUser::instancePtr()) {
            // Qt 6 connect 函数指针形式要求 slot 签名跟 signal 一致,
            //   userLoggedIn 带 QString 参数,用 lambda 显式丢掉
            // ★ 显式 QObject::connect — 匿名 namespace 内 unqualified connect 解析不到
            QObject::connect(cu, &CurrentUser::userLoggedIn,  []{ applyAllBindings(); });
            QObject::connect(cu, &CurrentUser::userLoggedOut, []{ applyAllBindings(); });
        }
        connected = true;
    }
    for (auto it = registry().begin(); it != registry().end(); ++it) {
        const Entry& e = it.value();
        applyOne(e.target.data(), e.perm, e.isAction);
    }
}

} // namespace qwhmi::auth

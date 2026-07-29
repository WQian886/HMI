// 阶段 3.1:VariableManager 实现

#include "VariableManager.h"
#include <algorithm>

VariableManager::VariableManager(QObject* parent)
    : QObject(parent)
{
}

VariableManager::~VariableManager() {
    // 注意:不要 qDeleteAll(vars_)。
    // 因为 addVariable 内部会 setParent(this),QObject 析构时会自动 delete 所有子对象。
    // 再 qDeleteAll = 双重 delete = 崩溃。
    // vars_ 里只持有原始指针,析构时让 QObject 机制清理即可。
}

bool VariableManager::addVariable(Variable* v) {
    if (!v)                       return false;  // 空指针
    const QString name = v->name();
    if (name.isEmpty())           return false;  // name 为空
    if (vars_.contains(name))     return false;  // name 重复

    // ★ IMPROVE-A5(2026-07-29,hotfix):检查 v->parent()
    //   【bug 复盘】原版“直接拒绝”导致变量进了 hash 但 manager 不管 — PollScheduler
    //          遍历不到 → 永远不读 → 压力/温度/转速 label 一直空
    //   【现状】hmi.cpp 现有代码 new Variable 时就传 varMgr_ 作 parent,此时 v->parent()
    //          已经 == this,必须接受(否则全拒)。这里仅作为防御检查 + warning:
    //          阶段 6 工程文件加载外部变量时如果遇到 parent 冲突会 warning 提醒
    if (v->parent() != nullptr && v->parent() != this) {
        qWarning() << "[VariableManager] Variable" << name
                   << "已有别的 parent:" << v->parent()
                   << "(this=" << this << ")— 阶段 6 工程文件需注意可能存在二次 delete";
        // 不拒绝:接受并接管(接管后原 parent 不再 delete 它,生命周期归本 manager)
    }

    // setParent 即使 v->parent() 已是 this,Qt 是 no-op,不会双重父
    v->setParent(this);  // 交给 manager 管生命周期
    vars_.insert(name, v);
    return true;
}

bool VariableManager::removeVariable(const QString& name) {
    auto it = vars_.find(name);
    if (it == vars_.end()) return false;

    Variable* v = it.value();
    vars_.erase(it);    // 先从 hash 移除
    delete v;           // 再 delete(QObject 析构会从父对象 child list 移除自己)
    return true;
}

bool VariableManager::contains(const QString& name) const {
    return vars_.contains(name);
}

int VariableManager::count() const {
    return vars_.size();
}

Variable* VariableManager::findVariable(const QString& name) const {
    return vars_.value(name, nullptr);  // QHash::value 默认值是默认构造的 value 类型
                                        // 这里 Variable* 默认是 nullptr
}

QList<Variable*> VariableManager::allVariables() const {
    return vars_.values();
}

QList<Variable*> VariableManager::variablesByAddress() const {
    QList<Variable*> list = vars_.values();
    std::sort(list.begin(), list.end(),
              [](Variable* a, Variable* b) {
                  return a->address() < b->address();
              });
    return list;
}
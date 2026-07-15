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
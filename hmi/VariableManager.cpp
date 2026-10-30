// 阶段 3.1:VariableManager 实现

#include "VariableManager.h"
#include "tag/ExpressionParser.h"
#include <QSet>
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

    // ★ D.5:检查 alias 是否跟其他 variable 重名(操作员 UI 唯一性)
    for (const QString& alias : v->aliases()) {
        if (findByAlias(alias) != nullptr) {
            qWarning() << "[VariableManager] alias 冲突:" << alias
                       << "(新 var:" << name << ")";
            return false;
        }
    }

    if (vars_.contains(name)) {
        // ★ 6.3 FIX:同 name 替换时,emit variableReplaced 让监听者更新 Variable* 指针
        //   emit 在 delete 之前,让监听者能读 old 的字段做最后处理
        Variable* old = vars_.value(name);
        vars_.remove(name);
        v->setParent(this);
        vars_.insert(name, v);
        connectValueChanged(v);   // ★ G-23:替换进来的新变量同样要接上自动重算
        emit variableReplaced(old, v);
        delete old;
        return true;
    }

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
    connectValueChanged(v);   // ★ G-23:接上"值变了自动重算派生量"
    return true;
}

// ★ G-23(2026-10-30):把变量的 valueChanged 接到自动重算上。
//
//   【为什么先 disconnect 再 connect】
//     同一个 Variable 对象有可能被 addVariable 加两次(比如外部先 new、
//     后面又拿同一个指针来加)。不先断的话会连两次 ——
//     一次值变化触发两次重算,派生量算两遍(值一样,但白白发两遍信号,
//     趋势曲线上会多出两个点)。
//
//   【为什么用 this 当 context】
//     manager 先于变量死时,连接自动断开;变量先死时(clear / removeVariable),
//     QObject 析构也会把连接摘掉。不用手动管生命周期。
void VariableManager::connectValueChanged(Variable* v) {
    if (!v) return;
    disconnect(v, &Variable::valueChanged,
               this, &VariableManager::onVariableValueChanged);
    connect(v, &Variable::valueChanged,
            this, &VariableManager::onVariableValueChanged);
}

bool VariableManager::removeVariable(const QString& name) {
    auto it = vars_.find(name);
    if (it == vars_.end()) return false;

    Variable* v = it.value();
    vars_.erase(it);    // 先从 hash 移除

    // ★ 改善清单 #6/#7(2026-10-10):删变量必须广播 variableReplaced(v, nullptr)
    //
    // 【这个函数原来什么都不 emit,是真 bug】
    //   manager 删掉变量时,那些【把 Variable* 存起来、稍后再解引用】的模块
    //   —— WriteScheduler::pending_、TrendWidget —— 手里的指针当场变悬空。
    //   WriteScheduler::onWriteReply 会 v->clearDirty():
    //   写请求在途(几十~几百毫秒)时用户在界面上删了这个变量,
    //   PLC 回包一到就是 use-after-free。
    //
    // 【为什么一直没被发现】
    //   同名【替换】会 emit、clear() 也早就 emit(2026-08-07,而且写清了理由),
    //   唯独这个函数漏了 —— 三个入口长得一样、两个会通知一个不会。
    //
    //   ★ 更正(2026-10-10 当天晚些时候):原来这里写的是
    //     "只有【用户在界面上删变量】才碰得到" —— 【那句话是错的】。
    //     全仓查过:removeVariable() 除了测试【没有任何生产调用方】,
    //     hmi.ui 里也没有删变量的入口(只有导出按钮和数据显示区)。
    //     别照着这句话去找"哪个按钮会走到这里",找不到的。
    //
    //   【那它为什么还是要 emit】
    //     ① 三个死亡入口语义必须一致。将来加"变量表删除按钮"
    //        (TagImporter 都在了,加删除是自然的下一步),不 emit 就是直接埋一个 UAF;
    //     ② 本函数是 public API,外部模块可以用;
    //     ③ 今天真正在生产里删光变量的是 clear() 那条路(见下),不是本函数 ——
    //        但那两条路早就 emit 了,正因为它们 emit 了,
    //        本函数不 emit 才显得像 bug 而不是像"本来就这样"。
    //
    // 【顺序不能反】
    //   emit 必须在 delete 之前 —— 监听者可能要读 oldVar 的字段做最后处理
    //   (addVariable 的替换分支也是这个顺序,不是巧合)。
    emit variableReplaced(v, nullptr);

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

// ★ D.5:按别名反查 — 遍历所有 var 找 alias 匹配
//   不用额外 hash 表(别名 < 5 个/var,空间换复杂度不划算)
//   重复 alias 不允许:duplicate 在 addVariable 时已 check
Variable* VariableManager::findByAlias(const QString& alias) const {
    if (alias.isEmpty()) return nullptr;
    for (Variable* v : vars_.values()) {
        if (v->aliases().contains(alias)) return v;
    }
    return nullptr;
}

QList<Variable*> VariableManager::allVariables() const {
    return vars_.values();
}

// ★ D.6:添加派生变量
bool VariableManager::addDerivedVariable(const QString& name, const QString& expr,
                                       Variable::DataType type, Variable::Access access) {
    // ★ 解析依赖
    QStringList deps;
    QString err;
    if (!qwhmi::tag::ExpressionParser::collectDependencies(expr, &deps, &err)) {
        qWarning() << "[VariableManager] 表达式解析失败:" << err << "expr=" << expr;
        return false;
    }
    // ★ 检查依赖是否存在
    for (const QString& dep : deps) {
        if (!vars_.contains(dep)) {
            qWarning() << "[VariableManager] 依赖变量不存在:" << dep << "(in" << name << ")";
            return false;
        }
    }
    auto* v = new Variable(name, /*address=*/0, type, access, this);
    v->setExpression(expr);
    v->setDependencies(deps);
    if (!addVariable(v)) {
        // addVariable 失败(名字或别名冲突)→ addVariable 已 delete v
        return false;
    }
    // ★ 立即计算一次初值
    recomputeDerivedVariables();
    return true;
}

// ★ D.6:重算所有派生变量(变量多,O(n*m) 但 n < 50)
void VariableManager::recomputeDerivedVariables() {
    for (Variable* v : vars_.values()) {
        if (!v->isDerived()) continue;
        recomputeOne(v);
    }
}

// ★ G-23(2026-10-30):重算一个派生变量。
//
//   【为什么要单独抽出来】
//     "重算全部"(recomputeDerivedVariables)和"重算一个"(依赖变了的那一个)
//     必须用【同一套】判定:依赖齐不齐、值算不算得出来。
//     分成两处写,迟早有一处忘了判"依赖还没到齐" —— 而那种情况下的
//     表现是"派生量算出一个错的值",比"不算"坏得多。
//
//   【依赖还没取到值 ≠ 出错】
//     PLC 轮询是一台一台回的:同一拍里 Speed 的回包到了,Temp 的还没到。
//     这时算出来的派生值是【错的】,不是"缺数据"。
//     以前这里直接把依赖丢给 evaluate,evaluate 返 NaN 并报一句
//     "variable 'Temp' not found" —— 而真实原因是"还没轮询到",
//     不是"变量不存在"。日志被这种假警报淹了,真出事时反而看不见。
//     现在:依赖【存在但还没值】→ 静默跳过,等下一个源变量到齐再算;
//           依赖【压根不存在】→ 明确警告(这才是真错)。
bool VariableManager::recomputeOne(Variable* d) {
    if (!d || !d->isDerived()) return false;

    for (const QString& depName : d->dependencies()) {
        Variable* dep = vars_.value(depName, nullptr);
        if (!dep) {
            qWarning() << "[VariableManager] 派生" << d->name()
                       << "依赖的变量不存在:" << depName;
            return false;
        }
        if (!dep->value().isValid()) {
            // 还没取到值 —— 等下一次。这不是错误,别刷日志。
            return false;
        }
    }

    // ★ 用 evaluate 拿值
    QString err;
    const double val = qwhmi::tag::ExpressionParser::evaluate(
        d->expression(),
        [this](const QString& name) -> const QVariant {
            Variable* dep = vars_.value(name, nullptr);
            if (!dep) return {};
            return dep->value();
        },
        &err);
    if (qIsNaN(val)) {
        if (!err.isEmpty()) {
            qWarning() << "[VariableManager] 派生" << d->name() << "求值失败:" << err;
        }
        return false;
    }
    d->recomputeAsDerived(val);
    return true;
}

// ★ G-23:变量值变了 → 重算依赖它的派生量(以及依赖那些派生量的派生量)。
//
//   【为什么用队列,不用递归】
//     派生量可以依赖派生量(D2 = D1 * 2,D1 = A + B)。
//     递归写法在链上没问题,但遇到【环】就爆栈 ——
//     而环是用户能配出来的(D1 = D2 + 1,D2 = D1 + 1)。
//     队列 + processed 集合:环最多转一圈就停,不会崩。
//
//   【为什么必须挡住重入】
//     recomputeOne 里 d->recomputeAsDerived() 会 emit valueChanged ——
//     那正是本函数要接的信号。不挡就是自己调自己。
//     挡住之后,连锁由【外层这一轮】的队列继续走完,不会漏。
void VariableManager::onVariableValueChanged(const Variable& v) {
    if (recomputing_) return;   // ★ 重入:外层那一轮会把连锁一起处理完

    // ★ 连锁重算的轮数上限。正常工程里链长 1~2;
    //   这个上限是给"环"兜底的 —— 宁可停下来喊一声,也别把 CPU 跑满。
    static const int kMaxRounds = 64;

    recomputing_ = true;
    QStringList queue{v.name()};
    QSet<QString> processed;
    int rounds = 0;
    while (!queue.isEmpty()) {
        if (++rounds > kMaxRounds) {
            qWarning() << "[VariableManager] 派生量连锁重算超过上限("
                       << kMaxRounds << "轮),可能有环,已停止。触发变量:" << v.name();
            break;
        }
        const QString changedName = queue.takeFirst();
        if (processed.contains(changedName)) continue;
        processed.insert(changedName);

        for (Variable* d : vars_.values()) {
            if (!d || !d->isDerived()) continue;
            if (!d->dependencies().contains(changedName)) continue;
            recomputeOne(d);
            // ★ d 自己也会发 valueChanged(被本槽挡掉了),
            //   所以这里【显式】把 d 入队 —— 依赖 d 的派生量要跟着重算。
            queue.append(d->name());
        }
    }
    recomputing_ = false;
}

// ★ 阶段 6.3(2026-08-07):加载前清空所有 variables(Q4-A 全删干净)
//   【背景】loadFromFile 要"内存 = JSON",先 clear 再 addVariable
//   【改法】遍历 + removeVariable(已经 delete + erase)
//   【取舍】O(n) 但 variables 数量通常 < 20,可忽略
//   【参照】Wqian 2026-08-07 阶段 6.3 开干;Q4-A 拍板
// ★ 阶段 6.3 FIX(2026-08-07):clear 时每个旧 var emit variableReplaced(old, nullptr)
//   监听者据此把 Variable* 置 nullptr(防 dangling)
void VariableManager::clear() {
    if (vars_.isEmpty()) return;
    QList<Variable*> oldVars = vars_.values();
    vars_.clear();
    for (Variable* old : oldVars) {
        emit variableReplaced(old, nullptr);
        delete old;
    }
}

QList<Variable*> VariableManager::variablesByAddress() const {
    QList<Variable*> list = vars_.values();
    std::sort(list.begin(), list.end(),
              [](Variable* a, Variable* b) {
                  return a->address() < b->address();
              });
    return list;
}
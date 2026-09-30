// 阶段 3.1:VariableManager
// 一组 Variable 的容器,提供增删改查 + 按地址排序(3.3 轮询分组用)

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "Variable.h"

class VariableManager : public QObject {
    Q_OBJECT

public:
    explicit VariableManager(QObject* parent = nullptr);
    ~VariableManager() override;

    // 添加变量(VariableManager 接管生命周期:内部 setParent(this))
    // 返回 false:参数为空 / name 为空 / alias 跟别的变量冲突
    //
    // ★ 2026-09-30 更正:本函数【同名不拒绝,直接替换】(旧注释写的"name 重复返回 false"是错的)。
    //   6.3 有意为之 —— 替换前后要 emit variableReplaced,让报警/绑定更新 Variable* 指针。
    //   加这条注释是因为 F3 写测试时按旧注释写"重名应失败",结果发现是静默替换,
    //   当成 bug 差点"修"掉。同名替换是有意行为,不是缺陷。
	bool addVariable(Variable* v);

    // 按 name 移除并 delete
    // 返回 false:name 不存在
    bool removeVariable(const QString& name);

    // 查询
    bool contains(const QString& name) const;// 
    int  count() const;//   
    Variable* findVariable(const QString& name) const;  // 没找到返回 nullptr

    // ★ D.5(2026-09-20):按别名反查(操作员 UI 看 "转速" 反查到 plc1.Speed)
    Variable* findByAlias(const QString& alias) const;  // 没找到返回 nullptr

    // 批量访问
    QList<Variable*> allVariables() const;

    // ★ D.6(2026-09-20):添加派生变量(基于其他变量计算)
    //   expr: 表达式(如 "a + b * 2")
    //   自动解析依赖 + setDependencies + 添加到 manager
    //   依赖的 variable setValue 时自动触发重算
    bool addDerivedVariable(const QString& name,
                           const QString& expr,
                           Variable::DataType type = Variable::DataType::Float,
                           Variable::Access access = Variable::Access::ReadOnly);

    // 手动触发所有派生变量重算(也由 valueChanged 自动触发)
    void recomputeDerivedVariables();

    // 按地址升序返回(3.3 轮询分组:连续地址合并成一次读请求)
    QList<Variable*> variablesByAddress() const;

    // ★ 阶段 6.3(2026-08-07):加载前清空所有 variables(Q4-A 全删干净)
    void clear();

signals:
    // ★ 阶段 6.3(2026-08-07):变量替换信号
    //   触发场景:1) addVariable 同名替换:old=旧,new=新 2) clear 删除:old=旧,new=nullptr
    //   监听者(ThresholdAlarmChecker / ActionManager / Binding)据此更新 Variable* 指针
    void variableReplaced(Variable* oldVar, Variable* newVar);

private:
    QHash<QString, Variable*> vars_;  // name → Variable*
};
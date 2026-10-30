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
    //   ★ G-23(2026-10-30):下面那句"依赖的 variable setValue 时自动触发重算"
    //     以前是一句【空头承诺】—— 本文件全文没有任何 connect,
    //     派生量【从来不会】自己更新。现在这句话兑现了(见下面的 onVariableValueChanged)。
    bool addDerivedVariable(const QString& name,
                           const QString& expr,
                           Variable::DataType type = Variable::DataType::Float,
                           Variable::Access access = Variable::Access::ReadOnly);

    // 手动触发所有派生变量重算。
    // ★ G-23:现在【也】由 valueChanged 自动触发(见 onVariableValueChanged)。
    //   这个公开入口保留:批量导入完变量后可以一次性把初值算出来。
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

private slots:
    // ★ G-23(2026-10-30):变量值变了 → 重算依赖它的派生量。
    //
    //   【为什么以前没有这个槽】
    //     派生量"依赖变了自动重算"这件事,VariableManager.h 的注释里写了,
    //     但代码里【一行 connect 都没有】。于是:
    //       · PLC 轮询回包改了源变量 → 派生量纹丝不动
    //       · 操作员在画面上改了源变量 → 派生量纹丝不动
    //       · 动作联动写了源变量     → 派生量纹丝不动
    //     而界面上派生量显示的是【上一次算出来的旧值】——
    //     跟"这个量真的就是这个数"长得一模一样。静默失败。
    //
    //   【为什么挂在 valueChanged 上,而不是挂在轮询上】
    //     变量的值有四条来路:PLC 轮询回包、界面手动改、动作联动写、
    //     另一个派生量算出来的。挂在 valueChanged 上,四条全覆盖;
    //     挂在轮询上,只有第一条会重算 —— 界面改一下源变量,画面上的
    //     派生量还是老数,而操作员刚改完就盯着那个数看。
    void onVariableValueChanged(const Variable& v);

private:
    // ★ G-23:把一个变量接上"值变了"的自动重算。
    //   单独一个函数是因为 addVariable 有两条插入路径(新加 / 同名替换),
    //   漏掉一条 = 那个场景下派生量不更新 —— 而两条路径长得几乎一样,
    //   正是"每处各写一遍必然漏一个"的形状。
    void connectValueChanged(Variable* v);

    // ★ G-23:重算【一个】派生变量。返回 true = 算出值并写进去了。
    //   抽出来是因为"重算全部"和"重算一个"必须用同一套判定
    //   (依赖齐不齐、算不算得出来),写成两处必然走偏。
    bool recomputeOne(Variable* d);

    // ★ G-23:重入保护。
    //   派生量自己也会 emit valueChanged(算完就发),而派生量可以依赖派生量 ——
    //   不挡住就是自己调自己,栈直接爆。
    bool recomputing_ = false;

    QHash<QString, Variable*> vars_;  // name → Variable*
};
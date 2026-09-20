// ============================================================================
// 阶段 D · D.6 · 派生表达式解析器(手写 4 则运算 + 变量引用)
//
// 文件职责:把 "a + b * 2" 这种表达式解析成可计算的 AST,提供给
//   VariableManager::recomputeDerivedVariables 调用。
//
// 关键设计:
//   - 零三方依赖 — 手写递归下降解析器(operator precedence climbing)
//   - 支持 + - * / ( ) 5 个运算符 + 数字字面量 + 变量名引用
//   - 不支持函数调用 / 一元负号(避免首字符歧义)/ 字符串(只算数值)
//   - 变量解析时通过回调 QVariant resolve(name) → double 拿到值
//   - 解析失败返 NaN + error string
//
// 关联:D.6 Variable.expression_, VariableManager, tst_ExpressionParser
// ============================================================================

#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>

namespace qwhmi::tag {

class ExpressionParser {
public:
    // ★ 解析 + 求值 — 一次到位(简单场景不需要分两步)
    //   expr: 表达式字符串
    //   resolve: 变量名 → QVariant 值的回调(给变量查 variableManager.findVariable)
    //   errorOut: 失败时填错误信息(可空)
    //   返回:成功返 double 值;失败返 NaN + fill errorOut
    static double evaluate(const QString& expr,
                           const std::function<QVariant(const QString&)>& resolve,
                           QString* errorOut = nullptr);

    // ★ 仅解析(收集依赖变量名)— 不求值,用于 setDependencies()
    //   返回:成功 true;失败 false + fill errorOut
    static bool collectDependencies(const QString& expr,
                                    QStringList* depsOut,
                                    QString* errorOut = nullptr);

    // ★ 内部节点 — 暴露出来方便测试
    enum class NodeKind { Number, Variable, UnaryOp, BinaryOp };
    struct Node {
        NodeKind kind;
        double   numberValue = 0.0;
        QString  variableName;       // for Variable
        char      op = 0;             // for BinaryOp: + - * / ;UnaryOp: -
        Node* lhs = nullptr;
        Node* rhs = nullptr;
    };

    // ★ Parser 状态机 — 持有以便增量解析(可复用 AST)
    class Parser {
    public:
        explicit Parser(const QString& expr);
        ~Parser();
        // 解析;失败返 nullptr
        Node* parse(QString* errorOut = nullptr);
        // 求值(已 parse 过的 AST)
        double eval(const std::function<QVariant(const QString&)>& resolve,
                    QString* errorOut = nullptr) const;
        // 收集所有变量名
        QStringList collectVariables() const;
    private:
        struct Impl;
        Impl* d_;
        // 禁止拷贝
        Parser(const Parser&) = delete;
        Parser& operator=(const Parser&) = delete;
    };

    // ★ 释放 AST 节点(free function,Parser 析构也会调)
    static void freeNode(Node* n);
};

} // namespace qwhmi::tag
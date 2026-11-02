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
    // ★ G-24(2026-10-30):表达式安全闸 —— 三个上限,超了【拒绝解析】。
    //
    //   【为什么必须有】
    //     解析器是递归下降的,evalNode / collectVars / freeNode 也全是递归的。
    //     一行 5000 层括号的文本 ⇒ 5000 层栈帧 ⇒ 栈溢出 ⇒ 进程直接死,
    //     而且是【段错误式死法】—— 没有异常、没有日志、没有栈回溯。
    //     而表达式可以来自:CSV 导入的 expression 列、变量配置窗口的表达式框、
    //     手改的工程文件 —— 全是【人给的数据】,不是内部生成的。
    //
    //   【三个闸各管什么(缺一个就有洞)】
    //     kMaxExprLength  管【文本长度】—— 一行文本的字符数上限。
    //                    它同时兜住了"1+1+1+…"这种【左倾二叉树】:
    //                    运算符越多节点越多,节点数 ≈ 长度/2。
    //     kMaxParseDepth  管【括号嵌套】—— 解析期的递归层数上限。
    //                    这一条直接决定解析期的栈深度。
    //     kMaxNodeCount   管【AST 节点数】—— evalNode / freeNode 的递归深度
    //                    由【AST 深度】决定,AST 深度 ≤ 节点数。
    //                    这一条兜住求值期和释放期的栈深度。
    //
    //   【为什么取这些数】
    //     工业现场的公式一般 < 10 层嵌套、< 100 字符。
    //     64 层 × 3 个栈帧/层 ≈ 200 帧;256 个节点 ⇒ 求值/释放各 ≤ 256 帧。
    //     在 Windows 默认 1 MB 主线程栈上,这两条都有【两个数量级】的余量。
    static constexpr int kMaxExprLength = 1024;   // 字符数
    static constexpr int kMaxParseDepth = 64;     // 括号嵌套层数
    static constexpr int kMaxNodeCount  = 256;    // AST 节点数

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
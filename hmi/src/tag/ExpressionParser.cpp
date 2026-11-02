// ============================================================================
// 阶段 D · D.6 · ExpressionParser 实现
// 对应 ExpressionParser.h
//
// 算法:Pratt 解析器(operator precedence parsing),支持:
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/') factor)*
//   factor := number | variable | '(' expr ')' | '-' factor(支持前置负号)
//
// 数字:浮点字面量(支持整数和 . 形式)
// 变量名:[A-Za-z_][A-Za-z0-9_.]* — 支持 plc1.Speed 这种含 . 的名字
// ============================================================================

#include "tag/ExpressionParser.h"

#include <QChar>
#include <QSet>
#include <QStack>
#include <QVector>
#include <cmath>
#include <limits>

namespace qwhmi::tag {

// ============================================================
// Node helpers
// ============================================================
static ExpressionParser::Node* makeNumber(double v) {
    auto* n = new ExpressionParser::Node;
    n->kind = ExpressionParser::NodeKind::Number;
    n->numberValue = v;
    return n;
}
static ExpressionParser::Node* makeVar(const QString& name) {
    auto* n = new ExpressionParser::Node;
    n->kind = ExpressionParser::NodeKind::Variable;
    n->variableName = name;
    return n;
}
static ExpressionParser::Node* makeBinary(char op, ExpressionParser::Node* lhs,
                                        ExpressionParser::Node* rhs) {
    auto* n = new ExpressionParser::Node;
    n->kind = ExpressionParser::NodeKind::BinaryOp;
    n->op = op;
    n->lhs = lhs;
    n->rhs = rhs;
    return n;
}
static ExpressionParser::Node* makeUnary(char op, ExpressionParser::Node* operand) {
    auto* n = new ExpressionParser::Node;
    n->kind = ExpressionParser::NodeKind::UnaryOp;
    n->op = op;
    n->lhs = operand;
    return n;
}

// ★ G-24(2026-10-30):释放改成【迭代式】。
//
//   【原来是递归的】
//     freeNode(n) → freeNode(n->lhs) → … 递归深度 = AST 深度。
//     AST 深度由用户给的表达式决定 ⇒ 一行深表达式能在【释放】时也爆栈。
//     —— 注意这条比解析期的爆栈更阴:解析可能侥幸成功(或者根本没解析),
//        但 AST 迟早要释放,那一瞬间才炸。
//
//   【为什么改成迭代】
//     显式栈 ⇒ 栈深度变成【堆上】的 QVector,与 AST 深度无关。
//     只要堆够大就不会溢栈 —— 而堆不会因为表达式长而爆。
void ExpressionParser::freeNode(Node* n) {
    if (!n) return;
    QVector<Node*> stack;
    stack.push_back(n);
    while (!stack.isEmpty()) {
        Node* cur = stack.takeLast();
        if (!cur) continue;
        if (cur->lhs) stack.push_back(cur->lhs);
        if (cur->rhs) stack.push_back(cur->rhs);
        delete cur;
    }
}

// ★ G-24:嵌套深度守卫 —— RAII,保证每个 return 路径都 --depth。
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& depth) : d(depth) { ++d; }
    ~DepthGuard() { --d; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
};

// ============================================================
// Parser 内部状态
// ============================================================
struct ExpressionParser::Parser::Impl {
    QString src;
    int pos = 0;
    // ★ G-24:两个计数闸
    //   depth     —— 当前括号嵌套层数(解析期栈深度)
    //   nodeCount —— 已建 AST 节点数(求值/释放期的递归深度上限)
    int depth = 0;
    int nodeCount = 0;

    explicit Impl(const QString& s) : src(s) {}

    // ★ G-24:建节点 + 记数。超上限时把【整棵子树】放掉并返 nullptr。
    //   返 nullptr 的调用方一律【不许】再 free 自己手里的节点 ——
    //   那棵树已经被这里放掉了,再 free 一次就是 double free。
    Node* track(Node* n, QString* err) {
        ++nodeCount;
        if (nodeCount > ExpressionParser::kMaxNodeCount) {
            if (err) {
                *err = QString("expression too complex (more than %1 nodes)")
                           .arg(ExpressionParser::kMaxNodeCount);
            }
            ExpressionParser::freeNode(n);
            return nullptr;
        }
        return n;
    }

    // ★ 跳过空白
    void skipWs() {
        while (pos < src.size() && src.at(pos).isSpace()) ++pos;
    }

    // ★ peek 当前字符(空返 '\0')
    QChar peek() const {
        return pos < src.size() ? src.at(pos) : QChar('\0');
    }

    // ★ 消耗 1 字符(已 skipWs)
    QChar eat() {
        QChar c = peek();
        if (pos < src.size()) ++pos;
        return c;
    }

    // ★ 主入口
    Node* parseExpr(QString* err) {
        // ★ G-24:长度闸 —— 先于任何解析动作。
        //   放在最前面,是因为"文本有多长"是最便宜的判断,
        //   而且它把"节点数"的量级也一并管住了(每节点 ≥ 1 字符 + 1 运算符)。
        if (src.size() > ExpressionParser::kMaxExprLength) {
            if (err) {
                *err = QString("expression too long (%1 chars, max %2)")
                           .arg(src.size())
                           .arg(ExpressionParser::kMaxExprLength);
            }
            return nullptr;
        }
        return parseAddSub(err);
    }

    // expr := term (('+'|'-') term)*
    Node* parseAddSub(QString* err) {
        Node* node = parseTerm(err);
        if (!node) return nullptr;
        while (true) {
            skipWs();
            const QChar c = peek();
            if (c != '+' && c != '-') return node;
            const char op = (c == '+') ? '+' : '-';
            eat();
            Node* rhs = parseTerm(err);
            if (!rhs) { freeNode(node); return nullptr; }
            // ★ G-24:记数 —— 超限时 track() 会把整棵子树放掉并返 nullptr
            Node* merged = track(makeBinary(op, node, rhs), err);
            if (!merged) return nullptr;   // ← node/rhs 已经放掉了,不能再 free
            node = merged;
        }
    }

    // term := factor (('*'|'/') factor)*
    Node* parseTerm(QString* err) {
        Node* node = parseFactor(err);
        if (!node) return nullptr;
        while (true) {
            skipWs();
            QChar c = peek();
            if (c != '*' && c != '/') return node;
            char op = (c.unicode() == '*') ? '*' : '/';
            eat();
            Node* rhs = parseFactor(err);
            if (!rhs) { freeNode(node); return nullptr; }
            Node* merged = track(makeBinary(op, node, rhs), err);
            if (!merged) return nullptr;
            node = merged;
        }
    }

    // factor := number | variable | '(' expr ')' | '-' factor
    Node* parseFactor(QString* err) {
        // ★ G-24:深度闸。
        //   DepthGuard 在【所有 return 路径】上自动 --depth ——
        //   手写 ++/-- 一定有漏掉的分支,而漏一条 = 深度计数只增不减
        //   ⇒ 表达式稍复杂点就被误判成"太深"。
        DepthGuard guard(depth);
        if (depth > ExpressionParser::kMaxParseDepth) {
            if (err) {
                *err = QString("expression nesting too deep (max %1)")
                           .arg(ExpressionParser::kMaxParseDepth);
            }
            return nullptr;
        }
        skipWs();
        QChar c = peek();
        if (c.isNull()) { if (err) *err = "unexpected end"; return nullptr; }
        if (c == '(') {
            eat();
            Node* inner = parseAddSub(err);
            if (!inner) return nullptr;
            skipWs();
            if (peek() != ')') { if (err) *err = "missing )"; freeNode(inner); return nullptr; }
            eat();
            return inner;
        }
        if (c == '-') {
            eat();
            Node* inner = parseFactor(err);
            if (!inner) return nullptr;
            return track(makeUnary('-', inner), err);
        }
        if (c == '+') { eat(); return parseFactor(err); }  // 一元 + 跳过
        if (c.isDigit() || c == '.') return parseNumber(err);
        if (c.isLetter() || c == '_') return parseVariable(err);
        if (err) *err = QString("unexpected char '%1' at pos %2").arg(c).arg(pos);
        return nullptr;
    }

    // number := [0-9]+ ('.' [0-9]+)?
    Node* parseNumber(QString* err) {
        const int start = pos;
        while (pos < src.size() && src.at(pos).isDigit()) ++pos;
        if (pos < src.size() && src.at(pos) == '.') {
            ++pos;
            while (pos < src.size() && src.at(pos).isDigit()) ++pos;
        }
        const QString numStr = src.mid(start, pos - start);
        bool ok = false;
        const double v = numStr.toDouble(&ok);
        if (!ok) {
            if (err) *err = QString("invalid number '%1'").arg(numStr);
            return nullptr;
        }
        return track(makeNumber(v), err);
    }

    // variable := [A-Za-z_][A-Za-z0-9_.]*
    Node* parseVariable(QString* err) {
        const int start = pos;
        if (pos < src.size() && (src.at(pos).isLetter() || src.at(pos) == '_')) ++pos;
        while (pos < src.size()
               && (src.at(pos).isLetterOrNumber() || src.at(pos) == '_' || src.at(pos) == '.')) {
            ++pos;
        }
        const QString name = src.mid(start, pos - start);
        if (name.isEmpty()) {
            if (err) *err = "empty variable name";
            return nullptr;
        }
        return track(makeVar(name), err);
    }

    // ★ 求值(递归)
    double evalNode(const Node* n,
                    const std::function<QVariant(const QString&)>& resolve,
                    QString* err) const {
        if (!n) {
            if (err) *err = "null node";
            return std::numeric_limits<double>::quiet_NaN();
        }
        switch (n->kind) {
            case NodeKind::Number: return n->numberValue;
            case NodeKind::Variable: {
                if (!resolve) {
                    if (err) *err = "no resolver for variable '" + n->variableName + "'";
                    return std::numeric_limits<double>::quiet_NaN();
                }
                const QVariant v = resolve(n->variableName);
                if (!v.isValid()) {
                    if (err) *err = "variable '" + n->variableName + "' not found";
                    return std::numeric_limits<double>::quiet_NaN();
                }
                bool ok = false;
                const double d = v.toDouble(&ok);
                if (!ok) {
                    if (err) *err = "variable '" + n->variableName + "' not numeric";
                    return std::numeric_limits<double>::quiet_NaN();
                }
                return d;
            }
            case NodeKind::UnaryOp: {
                const double v = evalNode(n->lhs, resolve, err);
                if (n->op == '-') return -v;
                return v;
            }
            case NodeKind::BinaryOp: {
                const double l = evalNode(n->lhs, resolve, err);
                const double r = evalNode(n->rhs, resolve, err);
                switch (n->op) {
                    case '+': return l + r;
                    case '-': return l - r;
                    case '*': return l * r;
                    case '/':
                        // ★ G-24:改成【精确等于 0】判定。
                        //   原来用 qFuzzyIsNull(r) —— 它的阈值是 1e-12,
                        //   于是【任何 ≤1e-12 的除数】都被当成"除零"报错。
                        //   1/1e-13 本来是个合法的巨大数,却被判成除零:
                        //   派生量算不出来,日志里写着"division by zero",
                        //   而现场真正的除数是 1e-13 —— 报错信息【指错了地方】。
                        //   真除零用 == 0.0 判;除数极小导致的溢出,
                        //   由下面的"结果必须有限"那道闸接住(报"结果非有限数")。
                        if (r == 0.0) {
                            if (err) *err = "division by zero";
                            return std::numeric_limits<double>::quiet_NaN();
                        }
                        return l / r;
                }
                return 0.0;
            }
        }
        return 0.0;
    }

    // ★ 收集所有变量名(去重 + 保序)
    QStringList collectVars(const Node* n, QStringList& out, QSet<QString>& seen) const {
        if (!n) return out;
        switch (n->kind) {
            case NodeKind::Variable:
                if (!seen.contains(n->variableName)) {
                    seen.insert(n->variableName);
                    out.append(n->variableName);
                }
                break;
            case NodeKind::BinaryOp:
                collectVars(n->lhs, out, seen);
                collectVars(n->rhs, out, seen);
                break;
            case NodeKind::UnaryOp:
                collectVars(n->lhs, out, seen);
                break;
            case NodeKind::Number: break;
        }
        return out;
    }
};

ExpressionParser::Parser::Parser(const QString& expr)
    : d_(new Impl(expr)) {}

ExpressionParser::Parser::~Parser() {
    // 注意:这里不 free AST root,因为 Node* 由调用方持有 + 调 freeNode 释放
    delete d_;
}

ExpressionParser::Node* ExpressionParser::Parser::parse(QString* errorOut) {
    Node* root = d_->parseExpr(errorOut);
    if (!root) return nullptr;
    // ★ 检查 trailing chars
    d_->skipWs();
    if (!d_->peek().isNull()) {
        if (errorOut) *errorOut = QString("trailing chars at pos %1").arg(d_->pos);
        freeNode(root);
        return nullptr;
    }
    return root;
}

double ExpressionParser::Parser::eval(
    const std::function<QVariant(const QString&)>& resolve, QString* errorOut) const {
    QString err;
    // ★ 复用需要 root,但这里没存 — 重新解析后单次求值
    Impl tmp(d_->src);
    Node* root = tmp.parseExpr(&err);
    if (!root) { if (errorOut) *errorOut = err; return std::numeric_limits<double>::quiet_NaN(); }
    double v = tmp.evalNode(root, resolve, &err);
    freeNode(root);
    // ★ G-24:结果必须【有限】。
    //   【以前漏了什么】
    //     · 除数溢出 → l/r 得 Inf,【没人拦】⇒ Inf 直接进变量,
    //       还能继续传播给别的派生量;
    //     · Inf - Inf → NaN,而 VariableManager 那边看到 NaN 就 continue,
    //       err 又是空的 ⇒ 【连一句 warning 都不打】——
    //       派生量就这么静悄悄地停在那儿不动了。
    //   【为什么在这里拦】
    //     这是所有求值路径的【唯一出口】(静态 evaluate() 也是走这里),
    //     放在这一处 ⇒ 不会有"某条路径忘了判"。
    if (!std::isfinite(v)) {
        if (err.isEmpty()) err = "expression result is not a finite number";
        v = std::numeric_limits<double>::quiet_NaN();
    }
    if (errorOut && !err.isEmpty()) *errorOut = err;
    return v;
}

QStringList ExpressionParser::Parser::collectVariables() const {
    QStringList out;
    QSet<QString> seen;
    Impl tmp(d_->src);
    QString err;
    Node* root = tmp.parseExpr(&err);
    if (root) {
        tmp.collectVars(root, out, seen);
        freeNode(root);
    }
    return out;
}

// ============================================================
// 静态便捷接口
// ============================================================
double ExpressionParser::evaluate(
    const QString& expr,
    const std::function<QVariant(const QString&)>& resolve,
    QString* errorOut)
{
    Parser p(expr);
    QString err;
    Node* root = p.parse(&err);
    if (!root) { if (errorOut) *errorOut = err; return std::numeric_limits<double>::quiet_NaN(); }
    const double v = p.eval(resolve, errorOut);
    freeNode(root);
    return v;
}

bool ExpressionParser::collectDependencies(
    const QString& expr, QStringList* depsOut, QString* errorOut)
{
    Parser p(expr);
    QString err;
    Node* root = p.parse(&err);
    if (!root) { if (errorOut) *errorOut = err; return false; }
    if (depsOut) {
        *depsOut = p.collectVariables();
    }
    freeNode(root);
    return true;
}

} // namespace qwhmi::tag
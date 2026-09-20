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

void ExpressionParser::freeNode(Node* n) {
    if (!n) return;
    freeNode(n->lhs);
    freeNode(n->rhs);
    delete n;
}

// ============================================================
// Parser 内部状态
// ============================================================
struct ExpressionParser::Parser::Impl {
    QString src;
    int pos = 0;

    explicit Impl(const QString& s) : src(s) {}

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
    Node* parseExpr(QString* err) { return parseAddSub(err); }

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
            node = makeBinary(op, node, rhs);
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
            node = makeBinary(op, node, rhs);
        }
    }

    // factor := number | variable | '(' expr ')' | '-' factor
    Node* parseFactor(QString* err) {
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
            return makeUnary('-', inner);
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
        return makeNumber(v);
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
        return makeVar(name);
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
                        if (qFuzzyIsNull(r)) {
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
    const double v = tmp.evalNode(root, resolve, &err);
    freeNode(root);
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
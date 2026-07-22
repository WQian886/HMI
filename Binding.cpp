// 阶段 4.1:Binding 实现
// 对应 Binding.h
//
// 关键 3 行每版都有:
//   1. applyNow(...)        — 立即同步初值(防启动空白)
//   2. connect(valueChanged, lambda → setProperty("text", formatted))
//                            — 后续每次值变都刷新

#include "Binding.h"
#include "Variable.h"
#include <QLabel>

void Binding::applyNow(QLabel* label, Variable* v, const Formatter& fmt) {
    if (!label || !v) return;
    QString text = fmt ? fmt(v->value()) : v->value().toString();
    label->setProperty("text", text);
}

// 重载 1:最简形式 — 直接 toString
void Binding::bind(QLabel* label, Variable* v) {
    Formatter fmt;  // 空 formatter = 走 toString 路径
    applyNow(label, v, fmt);
    QObject::connect(v, &Variable::valueChanged, label,
                     [label](const Variable& vv) {
                         label->setProperty("text", vv.value().toString());
                     });
}

// 重载 2:格式串 — "压力(bar): %1"
void Binding::bind(QLabel* label, Variable* v, const QString& format) {
    Formatter fmt = [format](const QVariant& val) -> QString {
        return format.arg(val.toString());
    };
    applyNow(label, v, fmt);
    QObject::connect(v, &Variable::valueChanged, label,
                     [label, fmt](const Variable& vv) {
                         label->setProperty("text", fmt(vv.value()));
                     });
}

// 重载 3:完全自定义(lambda)— Bool→ON/OFF,Float 控小数位等
void Binding::bind(QLabel* label, Variable* v, Formatter fmt) {
    applyNow(label, v, fmt);
    QObject::connect(v, &Variable::valueChanged, label,
                     [label, fmt](const Variable& vv) {
                         label->setProperty("text", fmt(vv.value()));
                     });
}

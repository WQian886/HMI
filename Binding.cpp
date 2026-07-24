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
#include <QLineEdit>
#include <QShortcut>

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

// ============================================================
// 阶段 4.2:反向写 + 威纶通输入确认(回车写 / Esc 取消)
// ============================================================

namespace {
    // 脏样式:橙色边框;正常:清掉
    void setDirtyStyle(QLineEdit* edit, bool dirty) {
        if (!edit) return;
        edit->setStyleSheet(dirty
            ? QStringLiteral("border: 2px solid orange;")
            : QString());
    }

    // 从变量重读回 UI(回滚 / Esc 取消 / 初始同步 共用)
    void applyNowEdit(QLineEdit* edit, Variable* v) {
        if (!edit || !v) return;
        edit->setText(v->value().toString());
    }
}

// UI(可编辑控件)↔ 变量,单一触发器 = 回车,与威纶通 eMT PC 仿真行为对齐
//
// 数据流:
//   用户编辑 → textEdited   → 边框变橙(待确认)
//   用户回车 → returnPressed → 校验 → v->setValue → 边框恢复
//                               校验失败 → 从变量重读回 UI(回滚) + qDebug
//   用户 Esc → QShortcut 激活 → 从变量重读回 UI + 边框恢复(取消)
//   变量变   → valueChanged   → 同步到 UI + 边框恢复(由 PollScheduler 周期读触发)
//
// ⚠️ 简化版:变量变就覆盖 UI(包括脏状态时 — 用户改了半截、PLC 也改了,以后者为准)
//          进阶(脏时不同步、提示"冲突")留后续练再说
// ⚠️ 类型硬编码 quint16(本练只演示 speed UInt16,Float/Bool 后续练再参数化)
void Binding::bindEdit(QLineEdit* edit, Variable* v) {
    if (!edit || !v) return;

    applyNowEdit(edit, v);  // 启动同步初值

    // ① 用户编辑 → 变脏(textEdited 只用户输入触发,代码 setText 不回环)
    QObject::connect(edit, &QLineEdit::textEdited, edit,
        [edit](const QString&) { setDirtyStyle(edit, true); });

    // ② 回车 → 写变量
    QObject::connect(edit, &QLineEdit::returnPressed, edit,
        [edit, v]() {
            bool ok = false;
            quint16 val = edit->text().trimmed().toUShort(&ok);
            if (!ok) {
                applyNowEdit(edit, v);   // 回滚
                setDirtyStyle(edit, false);
                qDebug() << "[BindEdit] 校验失败,已回滚:" << edit->text();
                return;
            }
            v->setValue(val);
            setDirtyStyle(edit, false);
        });

    // ③ Esc → 取消
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), edit);
    esc->setContext(Qt::WidgetShortcut);  // 只本控件内有效,不污染全局
    QObject::connect(esc, &QShortcut::activated, edit,
        [edit, v]() {
            applyNowEdit(edit, v);
            setDirtyStyle(edit, false);
        });

    // ④ 变量变 → 同步 UI(正向,与 bind 一样)
    QObject::connect(v, &Variable::valueChanged, edit,
        [edit](const Variable& vv) {
            edit->setText(vv.value().toString());
            setDirtyStyle(edit, false);
        });
}

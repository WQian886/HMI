// 阶段 4.1:Binding 实现
// 对应 Binding.h
//
// 关键 3 行每版都有:
//   1. applyNow(...)        — 立即同步初值(防启动空白)
//   2. connect(valueChanged, lambda → setProperty("text", formatted))
//                            — 后续每次值变都刷新

#include "Binding.h"
#include "Variable.h"
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include <QLabel>
#include <QLineEdit>
#include <QPointer>  // 阶段 4.x:bindEdit 用 QPointer 防止 Variable delete 后 lambda 段错,见改善计划.md #7
#include <QShortcut>
#include <QVariant>   // 阶段 4.3.0 bugfix:QVariant::fromValue<quint16>() 显式转换类型


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

    // ★ 阶段 4.x(2026-07-28):QPointer 防止 Variable 被 remove 后 lambda 段错
    // 【背景】原代码 lambda 捕获 v(Variable*),Variable 被 VariableManager::removeVariable
    //   删除后,lambda 仍然触发 → v->setValue(...) 段错
    // 【改法】用 QPointer 捕获,QObject 析构时自动置 null,lambda 里判 null 早返回
    // 【参照】改善计划.md #7
    QPointer<Variable> vPtr(v);

    applyNowEdit(edit, v);  // 启动同步初值

    // ① 用户编辑 → 变脏(textEdited 只用户输入触发,代码 setText 不回环)
    QObject::connect(edit, &QLineEdit::textEdited, edit,
        [edit](const QString&) { setDirtyStyle(edit, true); });

    // ② 回车 → 写变量
    QObject::connect(edit, &QLineEdit::returnPressed, edit,
        [edit, vPtr]() {
            // ★ 防 v 被 delete(2026-07-28):QPointer 早返
            if (vPtr.isNull()) return;
            Variable* v = vPtr.data();
            // 阶段 4.3.1 类型参数化:按 v->dataType() 分派(替换原 quint16 硬编码)
            bool parseOk = false;
            QVariant typedVal;

            switch (v->dataType()) {
                case Variable::DataType::Bool: {
                    QString s = edit->text().trimmed().toLower();
                    if (s == "true" || s == "1" || s == "on") {
                        typedVal = QVariant::fromValue<bool>(true); parseOk = true;
                    } else if (s == "false" || s == "0" || s == "off") {
                        typedVal = QVariant::fromValue<bool>(false); parseOk = true;
                    }
                    break;
                }
                case Variable::DataType::Int16:
                    typedVal = QVariant::fromValue<qint16>(edit->text().trimmed().toShort(&parseOk));
                    break;
                case Variable::DataType::UInt16:
                    typedVal = QVariant::fromValue<quint16>(edit->text().trimmed().toUShort(&parseOk));
                    break;
                case Variable::DataType::Int32:
                    typedVal = QVariant::fromValue<qint32>(edit->text().trimmed().toInt(&parseOk));
                    break;
                case Variable::DataType::Float:
                    typedVal = QVariant::fromValue<float>(edit->text().trimmed().toFloat(&parseOk));
                    break;
            }

            if (!parseOk) {
                applyNowEdit(edit, v);
                qDebug() << "[BindEdit] 校验失败,已回滚:" << edit->text();
                return;
            }

            // ★ B3(2026-09-17):写值前 checkPermission(WriteVariable)
            const bool ok = qwhmi::auth::checkPermission(
                qwhmi::auth::CurrentUser::instance().role(),
                qwhmi::auth::Permission::WriteVariable);
            if (!ok) {
                applyNowEdit(edit, v);
                setDirtyStyle(edit, true);
                qCWarning(qwhmi::auth::hmiPerm)
                    << "本用户" << qwhmi::auth::CurrentUser::instance().name()
                    << "无 WriteVariable 权限,写入被拒:" << v->name();
                return;
            }

            // ★ IMPROVE-A6(2026-07-29):setValue 返 enum,Rejected 才回滚
            //   Accepted = 真改了,NoChange = 死区过滤(没变),Rejected = 类型/范围不合法
            Variable::SetValueResult r = v->setValue(typedVal);
            if (r == Variable::SetValueResult::Rejected) {
                applyNowEdit(edit, v);   // 从变量重读回 UI(回滚)
                setDirtyStyle(edit, true);  // 边框留橙,告诉用户没成功
                qDebug() << "[BindEdit] 校验失败(类型/范围),已回滚:" << edit->text();
                return;
            }
            // Accepted / NoChange 都算 OK — NoChange 时 markDirty 也会让 WriteScheduler 写一次
            // (虽然值没变,但可能 PLC 端被外部改了,写回去对齐)— 简化决策:都 markDirty
            v->markDirty();           // 阶段 4.3.0:告诉 WriteScheduler 待写 PLC
            setDirtyStyle(edit, false);
        });

    // ③ Esc → 取消
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), edit);
    esc->setContext(Qt::WidgetShortcut);  // 只本控件内有效,不污染全局
    QObject::connect(esc, &QShortcut::activated, edit,
        [edit, vPtr]() {
            // ★ 防 v 被 delete(2026-07-28):QPointer 早返
            if (vPtr.isNull()) return;
            Variable* v = vPtr.data();
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

// ★ 6.5 收尾(2026-08-08 BugFix #1):断开 label 接收的所有 connection
//   【背景】bind 内部用 connect(v, ..., label, ...) 累计不清理,切工程会内存泄漏
//   【改法】disconnect 0 0 0 0 模式断 widget 所有接收的 connection
//   【理由】label 在本工程里只被 Binding::bind 接一次,无其他 source,安全
//   【参照】Wqian 2026-08-08 review 6.4 修复 #1
void Binding::unbind(QLabel* label) {
    if (!label) return;
    QObject::disconnect(label, nullptr, nullptr, nullptr);
}

// ★ 6.5 收尾(2026-08-08 BugFix #1):断开 edit 接收的所有 connection
//   【背景】bindEdit 内部 connect(textEdited / returnPressed / QShortcut / valueChanged)
//          4 个 connection,切工程时全断并重绑
//   【取舍】用户已输入的脏状态会丢(edit 边框从橙→默认),可接受
//   【参照】Wqian 2026-08-08 review 6.4 修复 #1
void Binding::unbindEdit(QLineEdit* edit) {
    if (!edit) return;
    QObject::disconnect(edit, nullptr, nullptr, nullptr);
}

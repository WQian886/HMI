// 阶段 4.1:Binding 框架
// 替 hmi.cpp 里手写的 if/else 分发;加新变量只需写一行 bind(...)
//
// 核心机制:Qt 信号槽 + QObject 属性反射(setProperty("text", x))
//   emit valueChanged → 我们 connect 的 lambda 跑 → label->setProperty("text", formatted)
//   加上"立刻同步初值",启动时不空白
#pragma once

#include <QString>
#include <QVariant>
#include <functional>

class Variable;
class QLabel;
class QLineEdit;

class Binding {
public:
    // 默认:直接显示 v.value().toString()
    static void bind(QLabel* label, Variable* v);

    // 格式串(QString::arg 风格):"压力(bar): %1" → "压力(bar): 100"
    static void bind(QLabel* label, Variable* v, const QString& format);

    // 完全自定义(进阶):lambda — Bool 转 ON/OFF / Float 控小数位都用这个
    using Formatter = std::function<QString(const QVariant&)>;
    static void bind(QLabel* label, Variable* v, Formatter fmt);

    // 阶段 4.2 新增:可编辑控件 ↔ 变量 双向,带"输入确认"(对齐威纶通 eMT PC 仿真行为)
    //   textEdited      → 变脏(边框橙)
    //   returnPressed   → 校验 → 写变量 → 清脏(写 PLC 留给 4.3)
    //   Esc (QShortcut) → 从变量重读回 UI + 清脏(取消)
    //   valueChanged    → 同步到 UI + 清脏(PLC 改 UI 跟着变)
    // 校验/转换失败:回滚(从变量重读)+ qDebug
    static void bindEdit(QLineEdit* edit, Variable* v);

    // ★ 6.5 收尾(2026-08-08 BugFix #1):断开旧 binding,避免切工程时 connection 累积
    //   【背景】bind/bindEdit 内部 connect(v, ..., label, ...) 每次调都新增 connection,旧的不 disconnect
    //   【修法】loaded lambda 重绑前先 unbind,清空 widget 接收的 connection(Binding 唯一 source)
    //   【取舍】unbindEdit 断 edit 全部 connection(textEdited/returnPressed/QShortcut/valueChanged),重绑后重新注册
    //   【参照】Wqian 2026-08-08 review 6.4 修复 #1
    static void unbind(QLabel* label);
    static void unbindEdit(QLineEdit* edit);

private:
    // 内部:bind 调一次立刻把当前 var 值推到 label(防启动空白 1-2 秒)
    static void applyNow(QLabel* label, Variable* v, const Formatter& fmt);
};

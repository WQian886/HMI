// ============================================================================
// src/action/ActionTemplate.cpp —— 见头文件
//
//   【一句话】这一层只做两件事:按顺序执行,以及如实报结果。
//
// 关联:ActionTemplate.h(设计说明)、ActionManager(执行方)
// ============================================================================

#include "action/ActionTemplate.h"

namespace qwhmi::action {

// ===== 类型 ↔ 字符串 =====

QString actionTypeToString(ActionType t) {
    switch (t) {
        case ActionType::WriteVariable: return QStringLiteral("write_variable");
        case ActionType::LogOnly:      return QStringLiteral("log_only");
    }
    // ★ 兜底:多了一个没 switch 到的类型。
    //   这里【故意】不返回空串 —— 空串在 JSON 里会被读成"类型缺失",
    //   跟"类型不认识"混在一起。返回一个显眼的未知串,读的时候就会报错。
    return QStringLiteral("unknown_action_type");
}

bool actionTypeFromString(const QString& s, ActionType* out) {
    if (!out) return false;
    // ★ 大小写不敏感:工程文件是人手写的(还可能被记事本改),
    //   "Write_Variable" / "WRITE_VARIABLE" 不该让整个工程打不开。
    const QString k = s.trimmed().toLower();
    if (k == QLatin1String("write_variable")) {
        *out = ActionType::WriteVariable;
        return true;
    }
    if (k == QLatin1String("log_only")) {
        *out = ActionType::LogOnly;
        return true;
    }
    // ★ 认不出来就【不】fallback —— 上层要能告诉用户"这一步的类型不认识"。
    return false;
}

// ===== 结果 → 字 =====

QString stepOutcomeToString(StepOutcome oc) {
    switch (oc) {
        case StepOutcome::Executed:     return QStringLiteral("Executed");
        case StepOutcome::Logged:       return QStringLiteral("Logged");
        case StepOutcome::NoVariable:   return QStringLiteral("NoVariable");
        case StepOutcome::InvalidValue: return QStringLiteral("InvalidValue");
        case StepOutcome::Rejected:     return QStringLiteral("Rejected");
        case StepOutcome::UnknownType:  return QStringLiteral("UnknownType");
        case StepOutcome::InvalidStep:  return QStringLiteral("InvalidStep");
    }
    return QStringLiteral("UnknownOutcome");
}

// ===== 合法性 =====

bool validateStep(const ActionStep& step, QString* err) {
    auto fail = [&](const QString& why) {
        if (err) *err = why;
        return false;
    };
    switch (step.type) {
        case ActionType::WriteVariable:
            // 变量名不能空 —— 空名字在 sink 那边会被当成"变量不存在",
            // 而"你没配"和"变量被删了"是两回事,混在一起查不出来。
            if (step.variableName.trimmed().isEmpty()) {
                return fail(QStringLiteral("写变量动作缺少 variable(变量名)"));
            }
            // ★ 值【不】在这里判合法 —— 类型适配要变量自己的声明类型,
            //   那是 sink 的事(见头文件【不许在这里做什么】)。
            return true;

        case ActionType::LogOnly:
            if (step.text.trimmed().isEmpty()) {
                return fail(QStringLiteral("只记日志的动作缺少 text(文字)"));
            }
            return true;
    }
    // ★ 走到这里说明有个没 switch 到的类型(比如工程文件来自更新版本)。
    return fail(QStringLiteral("不认识的动作类型"));
}

// ===== 执行 =====

StepResult executeStep(const ActionStep& step, ActionSink& sink) {
    StepResult r;

    // ★ 合法性先查,而且【不】合法的【不】调 sink ——
    //   "配置写错了"绝不能变成"照样往 PLC 写一个半截的值"。
    QString why;
    if (!validateStep(step, &why)) {
        // ★ 认不出来的类型跟"步骤不合法"要分开报:
        //   前者是"工程文件比这个程序新",后者是"配置写错了"。
        if (step.type != ActionType::WriteVariable && step.type != ActionType::LogOnly) {
            r.outcome = StepOutcome::UnknownType;
        } else {
            r.outcome = StepOutcome::InvalidStep;
        }
        return r;
    }

    switch (step.type) {
        case ActionType::WriteVariable:
            return sink.writeVariable(step.variableName, step.value);

        case ActionType::LogOnly:
            sink.logOnly(step.text);
            r.outcome = StepOutcome::Logged;
            return r;
    }
    r.outcome = StepOutcome::UnknownType;
    return r;
}

} // namespace qwhmi::action

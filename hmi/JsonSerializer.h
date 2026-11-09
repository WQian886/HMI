// 阶段 6.2(2026-08-07):JSON 序列化 helper 类
//   把 Variable / CheckerConfig / ActionManager::Mapping 各自的字段 → QJsonObject
//   反向 (fromJson) 是 6.3 阶段的事,本阶段只做 toJson(save 路径)
//
//   【设计】用静态方法 + namespace 风格,不要状态、无构造
//   【取舍】enum ↔ string 的转换:Variable::DataType/Access 走 Q_ENUM 反射;AlarmLevel/Direction 无 Q_ENUM,手写 switch
//   【参照】Wqian 2026-08-07 阶段 6.2 开干;schema 设计见 docs/专题/阶段6_schema设计.md

#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVariant>
#include <QList>

#include "VariableManager.h"  // ★ 6.3 修正:用 include 替前向声明(VariableManager 是 QObject 子类,需要完整定义才能隐式 upcast)
#include "Alarm.h"             // ★ 6.3 修正:AlarmLevel / Direction 是 Alarm.h 嵌套类型,前向声明看不到
// ★ F 阶段(2026-11-09):联动模板(一条报警 → 一组动作)的存读。
#include "action/ActionTemplate.h"

class Variable;
struct CheckerConfig;

class JsonSerializer {
public:
    // ===== Variable → QJsonObject =====
    //   【取舍】不存的字段:value / quality / timestamp / dirty / writeFailCount(runtime 状态)
    static QJsonObject toJson(const Variable& v);

    // ===== CheckerConfig → QJsonObject =====
    static QJsonObject toJson(const CheckerConfig& cfg);

    // ===== ActionManager::Mapping → QJsonObject =====
    //   【注意】Mapping 内部嵌套了 AlarmManager.h 私有类型,这里用前向声明 + 头文件包含
    //   实际使用:include "ActionManager.h" 才能拿到 Mapping 完整定义
    static QJsonObject toActionMappingJson(const QString& alarmName,
                                           const QString& variableName,
                                           const QVariant& value);

    // ===== ★ F 阶段(2026-11-09):联动模板 =====
    //
    //   【为什么要新的格式】
    //     老格式 actions 数组一条 = 一个"写变量"动作,表达不了:
    //       · 一条报警配【多个】动作
    //       · "只记日志"这类【不写变量】的动作
    //     新格式 action_templates:一条报警 = 一组步骤(steps)。
    //
    //   【工程文件格式(一旦发布就是契约,只加不改)】
    //     "action_templates": [
    //       { "alarm_name": "温度高",
    //         "steps": [
    //           { "type": "write_variable", "variable": "Speed", "write_value": 0 },
    //           { "type": "log_only",      "text": "已自动停机" }
    //         ] } ]
    //
    //   【老工程怎么办】老格式照读、照写 —— 加载端先读 actions,
    //   再读 action_templates 覆盖同名的;两边并存不冲突。
    static QJsonObject toActionStepJson(const qwhmi::action::ActionStep& step);
    static QJsonObject toActionTemplateJson(const qwhmi::action::ActionTemplate& tpl);

    // 从 JSON 读一个模板。
    //   失败返回 false 并写原因(同 fromJsonVariable 的做法)——
    //   "类型不认识" / "步骤不合法"必须让用户看见,不能静默跳过。
    static bool actionTemplateFromJson(const QJsonObject& obj,
                                        qwhmi::action::ActionTemplate* out,
                                        QString* outError = nullptr);

    // ===== Enum ↔ String 辅助 =====
    //   AlarmLevel / Direction 没有 Q_ENUM,手写 switch;以后 5.x 加 Q_ENUM 可删
    static QString alarmLevelToString(int level);
    static QString directionToString(int direction);

    // ===== 阶段 6.3 反向(读 JSON 路径)=====

    // Variable:构造 new Variable*(caller 用 varMgr_->addVariable 接管)
    //   【设计】不调 varMgr_->addVariable,留给 caller 决定(成功才加)
    //
    // ★ F3(2026-09-30)加了 outError 参数,原因见 .cpp 里的详细说明:
    //   原来失败只 return nullptr,信息全丢,Project 只能报一句
    //   "variable 字段缺失或类型错误" —— 200 个变量的工程里,
    //   第 137 个写错一个字母,用户完全不知道该改哪一行,只能一个一个试。
    //   传 nullptr 表示不关心错误信息(旧调用方兼容)。
    static Variable* fromJsonVariable(const QJsonObject& obj,
                                      VariableManager* parent,
                                      QString* outError = nullptr);

    // CheckerConfig:纯数据,无父对象
    static CheckerConfig checkerConfigFromJson(const QJsonObject& obj);

    // enum string → enum(失败 fallback 中间档,避免崩溃)
    static AlarmLevel alarmLevelFromString(const QString& s);
    static Direction  directionFromString(const QString& s);
};
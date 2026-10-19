// ============================================================================
// ★★ 2026-10-19:变量引用扫描 —— "这个变量还有谁在用?"
//
// 【为什么需要】
//   删掉一个【被引用】的变量,存出来的工程【整个打不开】。实测两条独立链路:
//     · alarms  :Project.cpp:686 存盘写 tc->variable()->name(),变量没了 → 空串
//                → 下次加载 findVariable("") 失败 → failWith → 拒绝加载
//     · actions :Project.cpp:698 写的是 ActionManager 存的【字符串】,变量没了
//                它【不会被清空】→ 文件里留下 "Temp" 这种指向不存在变量的名字
//                → 同样 findVariable 失败 → 拒绝加载
//                而且这一条更隐蔽:文件看起来完全正常,肉眼看 JSON 找不出问题。
//   症状都是"我明明存成功了,怎么打不开了",现场极难往回想。
//   今天没有任何 UI 能触发(没有界面能删变量),但手改 JSON 就能踩到;
//   变量管理界面做完之后,它会变成"点一下删"的直接后果。
//
// 【为什么放成独立文件,而不是 VariableManager 的一个方法】
//   扫描要读 AlarmManager 和 ActionManager 里的东西。塞进 VariableManager
//   就等于让变量容器反过来依赖报警和联动两层 —— 而那两层本来就依赖变量。
//   这是把依赖调了个头,所以宁可多一个文件。
//
// 【★ 数据层保持中立】
//   本文件只【查询】,不阻止任何删除。"有引用就不让删"是界面层的决定
//   (理由同 PlcConfigDialog::removeRowFromDraft 不含 QMessageBox):
//   同样的查询将来要服务于"改名""导出前检查"等别的用途,
//   在数据层就把它写死成"拒绝删除",那些用途就用不了了。
//
// 【★ 查得到什么、查不到什么 —— 这条必须对调用方说清】
//   查得到:
//     · 阈值报警(ThresholdAlarmChecker),经 dynamic_cast
//     · 联动映射(ActionManager::Mapping),比字符串
//   查不到:
//     · 非阈值报警(WriteFailChecker 等)—— 基类 AlarmChecker 【没有】
//       variable() getter,只有 ThresholdAlarmChecker 有。这类报警
//       【本来就不引用任何变量】,所以"查不到"恰好等于"没有引用"。
//       但这是"当前恰好如此",不是接口保证 —— 所以下面用 unscannableCheckers()
//       把它们【点名报出来】,让调用方能对用户说清,而不是默默当成"无引用"。
//     · ★★ 画面控件的绑定 —— 完全查不到,原因见 variableRefScan.h 同名说明
//       (ControlBase 存 QPointer 但不监听 variableReplaced,
//        且编辑器/运行窗口在 hmi.cpp 里是函数内 static)。
//     · 派生变量的表达式依赖 —— 表达式是字符串,解析器不在这层。
//       今天派生变量靠 dependencies_ 这个 QSet 记录依赖,
//       但那个 set 在【对象】上,变量一被删就没了,存盘也不写它。
//
// 关联:AlarmManager.h(需要 checkers()),ThresholdAlarmChecker.h(需要 variable()),
//      ActionManager.h(需要 mappings()), VariableManager.h(需要 findVariable)
// ============================================================================

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class AlarmManager;
class ActionManager;
class ThresholdAlarmChecker;
class VariableManager;

// ★ 一处引用。kind 区分它来自哪一层 —— 用户看到的是"被什么引用",
//   而不是"被引用了",所以 kind 必须能直接显示。
struct VariableRef {
    enum class Kind {
        Alarm,     // 阈值报警监控着它
        Action,    // 联动动作写它
    };
    Kind    kind    = Kind::Alarm;
    QString source; // 报警名 / 触发它的那个报警名
    // 人类可读的一行,例如 "报警「温度高」(监控)"
    //   【为什么要这一行】调用方(对话框)要把这个直接显示给用户,
    //   自己拼字符串的话格式会各处不一,改文案就得改 N 个地方。
    QString describe() const;
};

// ★ 扫描结果。分开返回"引用""查不到的类别"两样东西,
//   是为了让调用方【必须】考虑后者 —— 只有一个 combined 结构的话,
//   调用方很容易只读第一个字段,于是"查不到"就变成了沉默。
struct VariableRefScanResult {
    QList<VariableRef> refs;

    // ★ 本次扫描没能覆盖的类别(人类可读的说明)。
    //   调用方【必须】把它一起显示给用户。空 = 全部覆盖到了。
    QStringList blindSpots;

    bool hasRefs() const { return !refs.isEmpty(); }
};

// ★ 查一个变量当前被谁引用。
//   varName:变量名(按名字查,不用指针 —— 引用扫描发生在"还在列表里"的时候)
//   【不接管任何指针生命周期,纯只读】传 nullptr 的 manager 视为"这一层没有",
//   并把对应的 blindSpot 记上 —— 而不是假装查过了。
VariableRefScanResult scanVariableRefs(const QString& varName,
                                       AlarmManager* alarmMgr,
                                       ActionManager* actionMgr);

// ★ 列出"当前装着的、但本扫描【覆盖不到】的 checker"的名字。
//   【为什么要有这个】AlarmChecker 基类没有 variable() getter,
//   所以一个非 ThresholdAlarmChecker 的子类如果【将来】开始引用变量,
//   这个扫描会【静默漏掉它】。今天 WriteFailChecker 确实不引用变量,
//   但"今天恰好如此"和"接口保证"是两回事 —— 差一个明确列出来的名字。
QStringList unscannableCheckers(AlarmManager* alarmMgr);

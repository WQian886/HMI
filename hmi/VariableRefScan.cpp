// ============================================================================
// 变量引用扫描 —— 实现。头文件那一大段注释解释了为什么需要它、查得到什么。
// 这里只讲实现层面的两个坑。
// ============================================================================

#include "VariableRefScan.h"

#include "ActionManager.h"
#include "AlarmManager.h"
#include "ThresholdAlarmChecker.h"
#include "Variable.h"        // ← 要完整类型才能调 v->name()(前向声明不够)

#include <algorithm>

QString VariableRef::describe() const {
    switch (kind) {
    case Kind::Alarm:
        // "监控"而不是"引用" —— 报警读这个变量,联动写这个变量,
        // 操作员理解的是"它在看着这个量"和"出事要动这个量"。
        return QStringLiteral("报警「%1」(监控)").arg(source);
    case Kind::Action:
        // 联动的触发条件是报警名,所以那一句要写清楚:
        // "联动(报警「温度高」触发时写它)" —— 只说"联动"的话,
        // 用户不知道这条联动【什么时候】会动这个变量。
        return QStringLiteral("联动(报警「%1」触发时写它)").arg(source);
    }
    return source;
}

QStringList unscannableCheckers(AlarmManager* alarmMgr) {
    QStringList out;
    if (!alarmMgr) return out;
    for (AlarmChecker* c : alarmMgr->checkers()) {
        if (!c) continue;
        // ★ 这里就是 dynamic_cast 存在的理由。
        //   基类 AlarmChecker 没有 variable() getter(见 Alarm.h:104-123),
        //   只有 ThresholdAlarmChecker 有(ThresholdAlarmChecker.h:30)。
        //   所以"哪些报警引用了变量"这个问题,【类型系统本身答不了】,
        //   只能逐个问下下来的那个。
        if (dynamic_cast<ThresholdAlarmChecker*>(c)) continue;
        // 查不到名字就报类型名 —— 总比在名单里留一条"?"让人以为是 UI 的 bug
        const QString n = c->name();
        out << (n.isEmpty() ? QStringLiteral("(未命名报警)") : n);
    }
    std::sort(out.begin(), out.end());   // 名单要稳定,不然两次扫描结果顺序不同
    return out;
}

VariableRefScanResult scanVariableRefs(const QString& varName,
                                       AlarmManager* alarmMgr,
                                       ActionManager* actionMgr) {
    VariableRefScanResult res;
    if (varName.isEmpty()) return res;

    // ---- 报警这一层 ----
    if (alarmMgr) {
        for (AlarmChecker* c : alarmMgr->checkers()) {
            if (!c) continue;
            auto* tc = dynamic_cast<ThresholdAlarmChecker*>(c);
            if (!tc) continue;                       // 非阈值报警,交给 blindSpots
            // ★ 比较的是名字不是指针。
            //   指针比较在这里【不可靠】:如果这个变量已经被删了,
            //   tc->variable() 要么是空、要么(改动之前)是悬空指针,
            //   两种都不是"这个变量"的可信判据。名字才是 JSON 里存的东西,
            //   也是加载时拿来 findVariable 的东西 —— 坏的是名字那条链。
            Variable* v = tc->variable();
            if (!v || v->name() != varName) continue;
            VariableRef r;
            r.kind   = VariableRef::Kind::Alarm;
            r.source = c->name();
            res.refs << r;
        }
    } else {
        res.blindSpots << QStringLiteral("报警(未接入报警管理器,未检查)");
    }

    // ---- 联动这一层 ----
    if (actionMgr) {
        for (const ActionManager::Mapping& m : actionMgr->mappings()) {
            // ActionManager 存的是【字符串】(E #29 改成按名查),
            // 所以这里就是比字符串 —— 而且这正是坏掉的那条链:
            // 变量被删之后它【不会被清空】,存盘照写这个字符串。
            if (m.variableName != varName) continue;
            VariableRef r;
            r.kind   = VariableRef::Kind::Action;
            r.source = m.alarmName;
            res.refs << r;
        }
    } else {
        res.blindSpots << QStringLiteral("联动(未接入联动管理器,未检查)");
    }

    // ---- 这一层【明确】查不到,必须说出来 ----
    //   画面控件:ControlBase::bind() 存 QPointer<Variable>,
    //   但【不监听】variableReplaced(ThresholdAlarmChecker / WriteScheduler /
    //   HistoryRecorder / TrendWidget 全都监听,只有它不听)——
    //   变量被删,绑它的控件【静默定格在最后一个值,不报错】。
    //   而控件列表在 EditorMainWindow::controls() / RuntimeCanvas::controls(),
    //   那两个窗口在 hmi.cpp 里是【函数内 static】,hmi 类没有成员指针,
    //   从这里拿不到 —— 就算拿到了,"绑在哪个变量上"这个信息
    //   也只是 ControlBase::bound_ 这个私有成员,没有暴露出来。
    //   这不是"暂时没查",是【这一层查不到】,不能让它看起来像"无引用"。
    res.blindSpots << QStringLiteral(
        "画面控件的绑定(编辑器/运行画面)在这里查不到 —— "
        "删之前请到编辑器里确认没有控件绑着它");

    // 非阈值报警:今天它们不引用变量,但接口保证不了(见头文件说明)
    const QStringList unscannable = unscannableCheckers(alarmMgr);
    if (!unscannable.isEmpty()) {
        res.blindSpots << QStringLiteral(
            "以下报警类型无法检查变量引用(接口未提供):%1")
                           .arg(unscannable.join(QStringLiteral("、")));
    }

    return res;
}

// 阶段 5.2(2026-08-04):AlarmManager 实现

#include "AlarmManager.h"
#include "auth/AuditLog.h"      // ★ B5(2026-09-17):确认报警留痕
#include "auth/CurrentUser.h"
#include "auth/Permission.h"
#include "logging/Logging.h"    // ★ F2-c:屏蔽事件走 hmiAlarm 分类

AlarmManager::AlarmManager(QObject* parent) : QObject(parent) {}
AlarmManager::~AlarmManager() = default;

bool AlarmManager::addChecker(AlarmChecker* c) {
    if (!c || c->name().isEmpty() || checkers_.contains(c->name())) return false;
    c->setParent(this);  // 接管生命周期(析构时自动 delete)
    connect(c, &AlarmChecker::triggered, this, &AlarmManager::onCheckerTriggered);
    connect(c, &AlarmChecker::recovered, this, &AlarmManager::onCheckerRecovered);
    checkers_.insert(c->name(), c);
    return true;
}

bool AlarmManager::removeChecker(const QString& name) {
    auto it = checkers_.find(name);
    if (it == checkers_.end()) return false;
    delete it.value();  // setParent(this) 的对象 delete 会自动从 QObject 树移除
    checkers_.erase(it);
    return true;
}

bool AlarmManager::contains(qint64 id) const { return activeAlarms_.contains(id); }
int  AlarmManager::count() const { return activeAlarms_.size(); }

QList<Alarm> AlarmManager::activeAlarms() const {
    return activeAlarms_.values();
}

// ★ F3(2026-09-30):改成返回 bool —— 三种失败以前全是静默 return,
//   UI 只能"点了没反应",操作员分不清是权限问题还是已经确认过。
//   详见 AlarmManager.h 里的说明。三条失败路径现在各有各的返回值,
//   并且权限那条把"为什么被拒"也带出来(qCWarning 保留,给日志用)。
bool AlarmManager::acknowledge(qint64 id) {
    // ★ B3(2026-09-17):确认报警前 checkPermission(AcknowledgeAlarm)
    if (!qwhmi::auth::checkPermission(qwhmi::auth::CurrentUser::instance().role(),
                              qwhmi::auth::Permission::AcknowledgeAlarm)) {
        qCWarning(qwhmi::auth::hmiPerm)
            << "本用户" << qwhmi::auth::CurrentUser::instance().name()
            << "无 AcknowledgeAlarm 权限,确认被拒 alarmId=" << id;
        return false;
    }
    auto it = activeAlarms_.find(id);
    if (it == activeAlarms_.end()) return false;   // 找不到(已恢复/已清除)
    if (it->state != AlarmState::Active) return false;  // 已经确认/清除就不重复
    // ★ B5(2026-09-17):记一下原报警名,audit 行可读
    const QString alarmName = it->name;
    it->state = AlarmState::Acknowledged;
    it->acknowledgedAt = QDateTime::currentDateTime();
    emit alarmAcknowledged(id);
    // ★ B5:确认报警留痕(只在权限检查通过后才到这里)
    //   注:AlarmManager 在全局 namespace,用全限定避免和 AlarmLevel 等其他"auth"符号冲突
    qwhmi::auth::AuditLog::instance().appendCurrent(
        "alarm.ack", QString::number(id),
        QStringLiteral("name=%1").arg(alarmName),
        QStringLiteral("ok"));
    return true;
}

// ★ 阶段 6.3 FIX(2026-08-07):clearAll 必须真清一切 —— 不光清 activeAlarms_,也要清 checkers_
//   【背景】原来只清 active alarms → loadFromFile 调 clearAll 后,代码默认加的 checker(温度高/低)还在
//          → addChecker "温度高" 名字冲突 → 返回 false → emit loadError → 整个 loadFromFile 中止
//   【改法】delete 每个 checker(checkers_ 裸指针,addChecker 内部 setParent(this),delete 自动从 QObject 树移除)
//   【取舍】clearAll 名字模糊,严格说应该拆成 clearActiveAlarms() + clearCheckers(),
//          但 6.3 阶段先最小改动(合并清),6.4 阶段再考虑拆(影响面可控)
//   【参照】Wqian 2026-08-07 阶段 6.3 调试过程;Q4-A 拍板(内存 = JSON)
void AlarmManager::clearAll() {
    if (!activeAlarms_.isEmpty()) {
        activeAlarms_.clear();
    }
    // ★ F1(2026-09-30):连存档一起清。
    //   【取舍】"复位不该清历史"这条原则(Wqian 2026-08-04)针对的是
    //   事故追溯所需的记录 —— 那些已经由 AlarmRecorder 落进 SQLite 了,
    //   内存存档只是工作缓冲,不是历史。所以这里清掉不违反那条原则。
    //   【为什么必须清】Project::loadFromFile 开头调 clearAll(),
    //   换工程时留着上个工程的存档,findAlarm() 会串号。
    archived_.clear();
    if (!checkers_.isEmpty()) {
        for (auto it = checkers_.begin(); it != checkers_.end(); ++it) {
            delete it.value();   // setParent(this) 后 delete 会从 QObject 树自动移除
        }
        checkers_.clear();
    }
    // ★ F2-c:屏蔽名单必须一起清。
    //   换工程(Project::loadFromFile 开头就调 clearAll)时若留着上个工程的屏蔽名单,
    //   新工程一开局就可能有报警处于静默状态 —— 而且新工程里同名 checker
    //   很可能压根不是被屏蔽的那条,属于最典型的"藏起来"。
    //   逐个发 shelveChanged(false) 而不是静默 clear,
    //   好让界面上"N 条被屏蔽"的数字归零。
    if (!shelved_.isEmpty()) {
        const QStringList names = shelvedNames();
        shelved_.clear();
        for (const QString& n : names) emit shelveChanged(n, false);
    }
    emit allCleared();
}

// ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):保留记录,只标记已确认
//   详见 h 文件说明
// ★ F3:返回真正确认成功的条数。权限不足时 acknowledge 全返回 false,
//   这里就是 0 —— hmi.cpp 的复位路径据此提示"没权限",而不是假装复位完成了。
int AlarmManager::acknowledgeAll() {
    // ★ 拷贝 keys 避免遍历中容器被改(虽然 acknowledge 不删,但保险)
    const QList<qint64> ids = activeAlarms_.keys();
    int done = 0;
    for (qint64 id : ids) {
        if (acknowledge(id)) ++done;   // 单个 acknowledge 已经会 emit alarmAcknowledged
    }
    return done;
}

// ★ BUGFIX-5.2(2026-08-04):同一报警重复触发 → 不覆盖已有的(否则丢失原始 triggeredAt)
//   【取舍】保留首次触发时间,事故分析更准;重复触发只更新 triggerValue
//   ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):改用 ID 匹配,避免同名报警错配
void AlarmManager::onCheckerTriggered(const Alarm& alarm) {
    // ★ F2-c:屏蔽拦截 —— 所有 checker 报警都从这里过,是唯一的收口点。
    //   【为什么不能放在各 Checker 里】那样只有 ThresholdAlarmChecker 受管,
    //   写失败报警(WriteFailChecker)等类别照样会响 —— 屏蔽就漏了。
    //   【注意】检测器照常在算、传感器照常在越线,这里只是不让它进系统。
    //   【必须可见】这次拦截【不】发任何"我藏了一条"的通知 ——
    //   那样每 100ms 一次 rawRead 会刷屏。改为在 shelve() 时就固定可见:
    //   shelveCount() + shelveChanged 信号,界面常显"N 条报警被屏蔽"。
    if (shelved_.contains(alarm.name)) {
        qCDebug(hmiAlarm) << "[alarm] 报警已被屏蔽,本次触发不进入系统 name=" << alarm.name;
        return;
    }
    auto it = activeAlarms_.find(alarm.id_);
    if (it != activeAlarms_.end()) {
        it->triggerValue = alarm.triggerValue;  // 更新当前值
        return;
    }
    activeAlarms_.insert(alarm.id_, alarm);
    emit alarmTriggered(alarm);
}

// ★ F2-c:屏蔽 / 取消屏蔽
//
// 【为什么这里要查 checkers_】shelve 一个没注册过的名字必须失败。
//   否则工程师以为自己屏蔽了"压力高",实际那条报警根本没配,
//   或者配的名字拼错了 —— 界面显示"已屏蔽",实际什么都没发生。
//   这跟"静默失败"是同一类,只是方向相反:静默没发生 vs 静默发生。
bool AlarmManager::shelve(const QString& name) {
    if (name.isEmpty() || !checkers_.contains(name)) return false;
    // 幂等:已经屏蔽了就不重复计数、不重复发信号
    if (shelved_.contains(name)) return true;
    shelved_.insert(name);
    qCInfo(hmiAlarm) << "[alarm] 报警被屏蔽 name=" << name;
    emit shelveChanged(name, true);
    return true;
}

bool AlarmManager::unshelve(const QString& name) {
    // ★ 语义分层,两种"没成功"要分开:
    //   ① 名字没注册过 → false(和 shelve 同理,不能让操作员以为撤了)
    //   ② 已注册但当前没被屏蔽 → true(幂等 no-op,调用方不必先查 isShelved)
    //   混成一种返回的话,UI 就分不清"报警名写错了"和"本来就没屏蔽"。
    if (name.isEmpty() || !checkers_.contains(name)) return false;
    if (!shelved_.contains(name)) return true;
    shelved_.remove(name);
    qCInfo(hmiAlarm) << "[alarm] 取消屏蔽 name=" << name;
    emit shelveChanged(name, false);
    return true;
}

bool AlarmManager::isShelved(const QString& name) const {
    return shelved_.contains(name);
}

// ★ F1(2026-09-30)重写:恢复时【先发信号再 erase】
//
// 【原来错在哪】顺序是 erase → emit:
//     it->recoveredAt = now();
//     activeAlarms_.erase(it);        ← 记录没了(对象销毁,acknowledgedAt/state 一并没了)
//     emit alarmRecovered(id);        ← 只传 id
// 而 AlarmRecorder::onAlarmRecovered 靠遍历 activeAlarms() 取完整数据 ——
// 刚 erase 的那条必然遍历不到,直接走兜底 insertAction(id, {}, 0, ...)。
// 结果:历史表里【每一条恢复行】的 name/level/variable/value/threshold 全空,
// 事故追溯查出来是一行只有 id 和时间的孤儿。
//
// 【证据】tst_AlarmRecorder::recoveredRow_actuallyCarriesNameAndLevel
//   走生产链路(fireTrigger → fireRecover),断言恢复行的 name 非空 —— 红。
//   旧测试 recordRecovered_writesActionTwo 只断言 action==2 && id 匹配,
//   恰好是这条路径上唯一没坏的两样,所以它绿了两个月。
//
// 【这次改了什么】不只是调顺序:
//   1. 恢复的记录进 archived_(有界 kMaxArchived),不是直接销毁
//      —— 只调顺序的话 acknowledgedAt/state 照样当场蒸发,
//      "未确认就恢复了"这种现场常见情况无从回答;
//   2. emit 挪到 erase 之前,且给信号带上完整 Alarm,
//      下游不必再回头去查一个可能已经没了的容器;
//   3. state 置 Cleared —— 这个枚举值定义在 Alarm.h:24 一直没人写,属于死代码。
//
// 【兼容性】两个消费者(hmi::onAlarmRecovered / AlarmListControl::onAlarmRecovered)
//   都只接 qint64 id、不读内容,多传一个参数不影响它们(见文件头 grep 结果)。
void AlarmManager::onCheckerRecovered(qint64 id) {
    auto it = activeAlarms_.find(id);
    if (it == activeAlarms_.end()) return;

    Alarm recovered = it.value();          // ★ 拷贝出来:下面 erase 后原对象就没了
    recovered.recoveredAt = QDateTime::currentDateTime();
    recovered.state = AlarmState::Cleared;  // ★ 之前没人写这个值,是死代码

    // 存档(有界)。满了丢最旧的,保留最近的 —— 事故追溯靠 SQLite,
    // 内存存档只是"刚恢复的还能查一下"。
    archived_.append(recovered);
    while (archived_.size() > kMaxArchived) {
        archived_.removeFirst();
    }

    activeAlarms_.erase(it);
    // ★ emit 在 erase 之后、但【传的是拷贝】—— 这样"此刻已不在 active 里"
    //   仍然成立(下游若据此判断状态不会误判),而内容又完整。
    //   顺序不影响内容,因为带的是 recovered 拷贝而非引用。
    emit alarmRecovered(id, recovered);
}

const Alarm* AlarmManager::findAlarm(qint64 id) const {
    auto it = activeAlarms_.constFind(id);
    if (it != activeAlarms_.constEnd()) return &it.value();
    for (const Alarm& a : archived_) {
        if (a.id_ == id) return &a;
    }
    return nullptr;
}

void AlarmManager::clearArchived() {
    archived_.clear();
}
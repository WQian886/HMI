// 阶段 5.2(2026-08-04):AlarmManager 容器类
//   管理所有 AlarmChecker + 当前 active 报警
//   5.2 只做容器 + 信号出口,5.5 才接确认/复位联动

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include "Alarm.h"

class AlarmManager : public QObject {
    Q_OBJECT
public:
    explicit AlarmManager(QObject* parent = nullptr);
    ~AlarmManager() override;

    // 注册 Checker(AlarmManager 接管生命周期:内部 setParent(this))
    // 返回 false:checker 为空 / name 为空 / name 重复
    bool addChecker(AlarmChecker* c);

    // 移除(name 对应的 checker 被 delete)
    // 返回 false:name 不存在
    bool removeChecker(const QString& name);

    // 查询
    bool contains(qint64 id) const;
    int  count() const;                                  // active 报警数
    QList<Alarm> activeAlarms() const;                   // 给 5.4 UI 用

    // ★ F1(2026-09-30):按 id 找报警,active 找不到就翻 archived_
    //   【为什么需要】恢复时 activeAlarms_ 里的记录会被 erase 掉,
    //   而 AlarmRecorder 靠遍历 activeAlarms() 取数据填历史行,
    //   于是"每一条恢复行的 name/level/variable 全是空的"(已用测试证明)。
    //   现在恢复的记录进 archived_,这个函数是它的正确取法。
    //   查不到返回 nullptr。
    const Alarm* findAlarm(qint64 id) const;

    // ★ F1(2026-09-30):已恢复报警的存档(有界,见 kMaxArchived)
    //   【取舍】存档不是"永久保留" —— 那正是 AlarmRecorder 落 SQLite 的活,
    //   两边都永久保留等于同一份数据存两遍、还各有一份过期策略。
    //   内存里只留最近 N 条,够 UI 回看和刚恢复时的联动查询用。
    QList<Alarm> archivedAlarms() const { return archived_; }
    void clearArchived();

    // 存档上限。超过时丢最旧的(见 onCheckerRecovered)。
    // 【为什么要有界】这是台 7×24 不关机的机器,没人会来点"清存档"。
    // 无界增长的结果是运行几个月后吃光内存,而报警数据本该在 SQLite 里。
    static constexpr int kMaxArchived = 500;

    // ★ IMPROVE-6.2(2026-08-07):配置存读访问器(返回所有 Checker 给 saveToFile 用)
    QList<AlarmChecker*> checkers() const { return checkers_.values(); }

    // 5.5 阶段接:用户点确认按钮(用 ID 唯一标识,避免同名报警匹配错误)
    //
    // ★ F3(2026-09-30):改成返回 bool。
    //   【为什么】原来是 void。三种"没确认成"的情况全都静默:
    //     ① 权限不足 —— 只 qCWarning 一行就 return
    //     ② id 不在 active 里
    //     ③ 已经确认过了
    //   UI 层拿不到任何信息,只能"点了没反应"。而 F3 恰好要加确认入口 ——
    //   操作员双击一行,界面纹丝不动,他无法分辨是"没权限"(该找工程师)
    //   还是"已经确认过了"(正常)。这正是本项目反复吃的静默失败。
    //   返回 true = 这次真的把它标成已确认了。
    //   返回 false = 没成功(权限不足 / 找不到 / 已是已确认状态)。
    //   【注意】③ 返回 false 是有意的:重复确认不是错误,但也不是"成功了一次"。
    //   调用方要区分 ① 和 ③ 的话,③ 可以自己先查状态。
    bool acknowledge(qint64 id);

    // ★ BUGFIX-5.5(2026-08-04 Wqian 反馈):把所有 active 报警标记为已确认(不删记录)
    //   【背景】Wqian 反馈:复位不应该清报警记录(事故追溯需要)
    //   【改法】遍历 activeAlarms_,逐个调 acknowledge()(各自 emit alarmAcknowledged)
    //   【取舍】保留记录比 clearAll 更安全;clearAll 留作工程师手动清
    //   【参照】Wqian 2026-08-04 反馈
    //
    // ★ F3:返回"这次真正确认了几条"(权限不足时一条都没确认,返回 0)。
    //   原来返回 void,复位按钮调它之后不知道自己到底确认了没有。
    //   hmi.cpp:742 的复位路径现在会拿这个数来提示操作员。
    int acknowledgeAll();

    // 5.5 阶段接:复位按钮 → 清所有 acknowledged 的报警(留作工程师手动清,5.5 不调用)
    void clearAll();

    // ============================================================
    // ★ F2-c(2026-09-30):报警屏蔽(shelve / inhibit)
    //
    // 【解决什么】滞回压的是"报太多";屏蔽解决的是另一类 ——
    //   "这条报警现在没意义,别烦我"。典型场景:传感器检修中、
    //   某台泵本来就不投用,它的报警纯属噪声。
    //
    // 【为什么放在 AlarmManager 而不是 Checker】
    //   Checker 只知道"我检测到了",它不知道"这条现在该不该报"。
    //   放 Manager 层有三个好处:
    //     ① 对所有 Checker 类别一致(写失败报警 WriteFailChecker 等同样受管,
    //        放 ThresholdAlarmChecker 里就只有阈值报警能被屏蔽)
    //     ② 状态可查/可撤销/可枚举 —— 界面能显示"N 条被屏蔽"
    //     ③ 换工程时 clearAll 一次清干净
    //
    // 【本项目反复吃的那类静默失败,在屏蔽上尤其致命】
    //   如果"被屏蔽的 checker 不触发"是唯一表现,那就是:
    //     屏幕上没有行、没有计数、没有日志 —— 操作员会以为"这次真没报警",
    //     而实际是【被我们藏起来了】。
    //   更糟:检修结束忘了取消屏蔽 → 传感器真的坏了 → 依然没有报警。
    //   设备坏了而界面一片安静,这是能出人命的。
    //   所以硬约束三条:
    //     ① shelveCount() 随时可查(界面据此显示"N 条报警被屏蔽")
    //     ② shelveChanged 信号让界面跟着变,不会永远显示 0
    //     ③ 屏蔽必须能撤销(unshelve),不是单向门
    //
    // 【为什么不做"自动到期"】
    //   工业上常见"屏蔽 8 小时自动恢复"。本次【不做】——
    //   它需要一台常驻定时器 + 到期语义 + 跨重启策略,而当前项目连
    //   报警编辑 UI 都还没有(配置只能手改 JSON),先落"手动可撤销"这一半,
    //   免得做出一个"以为会自己恢复、实际不会"的半成品。
    //   风险由"界面常显屏蔽数 + 审计留痕"兜住,而不是靠一个不会到来的自动恢复。
    //
    // 返回 false:名字为空 / 没注册过这个 checker
    // 重复 shelve 同一名字返回 true 但不重复计数、不重复发信号(幂等)。
    bool shelve(const QString& name);
    bool unshelve(const QString& name);
    bool isShelved(const QString& name) const;
    int  shelveCount() const { return shelved_.size(); }
    // ★ QSet 没有 keys() —— 返回 values() 并逐个转 QString。
    //   (QHash 才是 keys();这个错是我写的时候顺手带过来的,编译器当场抓住。)
    QStringList shelvedNames() const {
        QStringList out;
        out.reserve(shelved_.size());
        for (const QString& n : shelved_) out << n;
        return out;
    }

signals:
    // 报警触发 → 5.3 接 tryTransition + 5.4 接 UI
    void alarmTriggered(const Alarm& alarm);
    // 报警确认 → 5.5 UI(停闪),用 ID 唯一标识避免同名匹配错误(5.5 BUGFIX)
    void alarmAcknowledged(qint64 id);
    // 报警恢复 → 5.5 UI(标记恢复时间),用 ID 唯一标识(5.5 BUGFIX)
    //
    // ★ F1(2026-09-30):多带一个 const Alarm& —— 恢复之后 activeAlarms_ 里
    //   就没这条了,只传 id 的话下游想记点什么都得回查一个可能已空的容器
    //   (AlarmRecorder 就是这么存出孤儿行的)。
    //   实参是拷贝,不是 activeAlarms_ 里的引用 —— 接收方可以安全持有。
    void alarmRecovered(qint64 id, const Alarm& recovered);
    // 全部清除 → 5.5 UI(清空列表)
    void allCleared();

    // ★ F2-c:某条报警的屏蔽状态变了。
    //   【为什么不靠轮询 shelveCount()】界面不轮询这个数 ——
    //   屏蔽动作发生在工程师那边,操作员的屏幕上必须自己跳出来。
    //   没有这个信号的话,界面会一直显示"N 条被屏蔽:0",
    //   而实际有人已经屏蔽了两条 —— 那就正是本设计要防的"藏起来"。
    //   shelved=true 表示现在处于屏蔽态。
    void shelveChanged(const QString& name, bool shelved);

private slots:
    void onCheckerTriggered(const Alarm& alarm);
    void onCheckerRecovered(qint64 id);

private:
    QHash<qint64, Alarm>          activeAlarms_;   // id → 当前 active 报警(5.5 BUGFIX)
    QHash<QString, AlarmChecker*> checkers_;       // name → checker 实例

    // ★ F1(2026-09-30):已恢复报警存档(按恢复顺序 append,最旧的在前)
    //   【背景】5.2 IMPROVE 的注释写着"完整版应该保留 archived_ 存档,但 5.4 UI 暂时不需要",
    //           当时判断"暂时不需要"是对的 —— 结果 6.x 接 AlarmRecorder 时踩了:
    //           恢复时记录被 erase,Recorder 取不到内容,历史表存出孤儿行。
    //           "暂时不需要"的判断,代价由接下游的人承担了。
    //   【现在为什么需要】不是 UI 需要,是【数据落库需要】——
    //   AlarmRecorder 要在恢复的那一刻拿到完整记录,否则历史查询结果是残缺的。
    QList<Alarm>                  archived_;

    // ★ F2-c:被屏蔽的 checker 名字集合。
    //   【为什么用名字而不是 AlarmChecker*】clearAll() 会 delete 所有 checker,
    //   存指针就得先清理这些悬空项;存名字则 clearAll 清空 set 即可,
    //   而且和 checkers_ 的 key 是同一套命名,排查时好对照。
    QSet<QString>                 shelved_;
};
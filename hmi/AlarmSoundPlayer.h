// ============================================================================
// 阶段 F · F5(2026-10-08):报警声音 / 蜂鸣
//
// 文件职责:把"某条报警发生了"翻译成"响一声/不响",并管住重复提醒与静默时段。
//
// ★ 为什么单独一个类,不塞进 AlarmManager
//   AlarmManager 是【事实层】:谁响了、谁恢复了、谁被确认了,它只记录。
//   声音是【表现层】的事:响不响、响多响、几点不响,都是现场偏好,
//   同一套报警配置在白班和夜班应该表现不同。混进 AlarmManager 就等于
//   把"发生了"和"要不要叫"搅在一起,而这两件事的生命周期根本不同。
//
// ★ 为什么用 Windows MessageBeep,而不是 QSoundEffect + wav 文件
//   1. MessageBeep 自带 4 种可区分的声音,正好对上 4 档等级 ——
//      等级区分是 F5 的核心需求,用不着自己造波形。
//   2. 不引入 Qt6::Multimedia。那是 Qt 6.8 需要【单独安装】的模块,
//      加进来要同时动 CMakeLists / vcxproj / 开发机 Qt 安装,
//      而它换来的是"能放自己的 wav"。先不装,真嫌系统音难听再换 ——
//      下面的 setSoundSink() 就是为那次替换留的口子。
//   3. MessageBeep 是同步调用(约 1 秒后系统自动停),
//      不占线程、不需要事件循环,单测里也不会挂住。
//
// ★ 【重要】测得到什么,测不到什么 —— 别把绿灯当成"功能能用"
//   这个类里"响不响"的判定逻辑是纯的,可以 100% 测;
//   但"声音真的从音箱出来了"测不了 —— MessageBeep 不返回任何东西。
//   所以 tst_AlarmSound 测的是【决策】,不是【耳朵】。
//   第一次合上代码时必须人耳确认一遍,这一步没有测试能替代。
//
//   关联:./AlarmManager.h, ./Alarm.h
// ============================================================================

#pragma once

#include <QObject>
#include <QDateTime>
#include <QTime>
#include <QSet>

#include <array>
#include <functional>

class AlarmManager;
class QTimer;
struct Alarm;
// ★ AlarmLevel 是 Alarm.h 里的 enum class,不是 struct —— 它【不能】被
//   struct Alarm; 那个前向声明顺带带出来。漏了它,下面所有用到 AlarmLevel
//   的地方都会报"未声明的标识符",而且报错点离病根很远。
enum class AlarmLevel;

// ★ F5:报警声音播放器
class AlarmSoundPlayer : public QObject {
    Q_OBJECT

public:
    // 【为什么要"不响"也有名字】
    //   只写"响 / 不响"的话,出问题时用户只能说"它没响",你还是不知道
    //   是被关了、被确认了、还是进了静默时段 —— 这正是本项目反复吃亏的
    //   "静默失败"。所以每一次【故意不响】都必须能说清是哪一种。
    enum class Decision {
        Beep,                  // 该响
        SilencedBySwitch,      // 总开关关了
        SilencedByLevel,       // 这一档等级被单独关掉了(如 Info 默认不响)
        SilencedByAcknowledged,// 已确认 —— 操作员说了"知道了",就别再叫
        SilencedByQuietHours,  // 在静默时段内
        DuplicateSuppressed    // 静默时段结束后再次评估时已恢复/已确认
    };
    Q_ENUM(Decision)

    // 4 档等级的下标,统一走这一个函数 ——
    // 任何地方要做 array 下标都不能自己 static_cast,否则加档/换序时会漏改。
    static int levelIndex(AlarmLevel level);
    static int levelCount();

    struct Settings {
        // 总开关。默认开。
        bool masterEnabled = true;

        // 每档是否发声,下标 = levelIndex(level)。
        // 【为什么 Info 默认不响】现场最吵的就是信息级报警(泵启动、
        //   模式切换……),全都响的话操作员三天就麻木了,而麻木 = 真报警也听不见。
        //   这和 F2-c 屏蔽同源:信息量太大时,不响也是配置出来的,不是默认行为。
        std::array<bool, 4> levelEnabled{{ false, true, true, true }};

        // 未确认就每隔 repeatIntervalMs 再提醒一次。
        // 【为什么默认开】声音报警的全部价值在于"人不在跟前也知道"。
        //   只响一次的话,操作员走开了就再也听不到 —— 那还不如只做颜色。
        bool repeatUntilAck = true;
        int  repeatIntervalMs = 5000;

        // 静默时段(免打扰)。跨零点时 start > end,例如 22:00 → 07:00。
        bool quietEnabled = false;
        int  quietStartMin = 22 * 60;
        int  quietEndMin   = 7 * 60;
    };

    explicit AlarmSoundPlayer(AlarmManager* mgr, QObject* parent = nullptr);
    ~AlarmSoundPlayer() override;

    Settings settings() const;
    void setSettings(const Settings& s);

    // ★ 把"真的发声"这一步换成测试能观测的东西。
    //   默认实现走 MessageBeep;测试注入一个计数 lambda,
    //   于是"响了没有/响了几次/响的是哪一档"全部可断言,
    //   而不用真的在 CI 上发出声音。
    void setSoundSink(std::function<void(AlarmLevel)> sink);

    // ★ 试听:直接发声,【绕开 sink,也不看任何静音规则】。
    //   静音时段开着、等级全关了、已确认了 —— 点了试听都得响。
    //   试听试的是"声音本身合不合适",不是"此刻该不该响";
    //   把它做成走判定逻辑的话,用户在静默时段里点一下什么也听不到,
    //   只会得出"这功能坏了"的结论。
    //   不计入 beepCount_ —— 它不是一条报警发出的声音。
    void previewBeep(AlarmLevel level);

    // ★ 累计实际发声次数。仅供测试与诊断,业务代码不要用。
    int  beepCount() const { return beepCount_; }
    // ★ 当前"还没确认、且该等级开着声音"的报警条数。
    //   【它【不】考虑静默时段】这是刻意的:
    //   静默时段里这条数依然 >0,界面因此能显示"N 条报警未确认"。
    //   这和 F2-c 屏蔽是同一条原则 —— 关掉的只是声音,不是这件事本身。
    //   真要在这里把静默时段也算进去,界面就会在夜班显示"0 条报警",
    //   而隔壁那条报警正躺在活动列表里没处理过 —— 那才是藏起来了。
    int  pendingReminderCount() const;

    // ---- 纯判定,不碰任何状态(单测直接调它)----
    bool inQuietHours(const QDateTime& now) const;
    Decision decide(const Alarm& a, const QDateTime& now) const;

private slots:
    void onTriggered(const Alarm& a);
    void onAcknowledged(qint64 id);
    void onRecovered(qint64 id, const Alarm& recovered);
    void onAllCleared();
    void onRepeatTick();

private:
    void doBeep(AlarmLevel level);

    AlarmManager* mgr_ = nullptr;
    Settings      settings_;
    std::function<void(AlarmLevel)> sink_;
    // ★ 记着"哪些 id 响过"只为诊断:静默时段内触发的报警不响,
    //   出了静默时段要不要补一声,是 F5 里最容易做糊的一处,
    //   有了它至少能查清当时到底发生了什么。
    QSet<qint64>  suppressedByQuietHours_;

    class QTimer* repeatTimer_ = nullptr;   // 前向声明,不引入完整 QTimer
    int beepCount_ = 0;
};
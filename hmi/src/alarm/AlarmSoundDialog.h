// ============================================================================
// 阶段 F · F5(2026-10-08):报警声音设置对话框
//
// ★ 为什么这个对话框必须存在(而不是"改 JSON 里的一串 true/false")
//   本项目已经因为同样的理由栽过一次:滞回和屏蔽做了功能却没做入口,
//   结果只能手改 JSON —— 那等于功能对真实用户不存在。
//   而且"响不响"是【现场偏好】,不是工程逻辑:
//   白班和夜班不一样,信息级在泵房里没人想听。
//   所以它必须是现场能随手调的东西。
//
// ★ 为什么放在 src/alarm/ 而 AlarmSoundPlayer 放在仓库根
//   分层照着现有约定走:领域逻辑(AlarmManager / AlarmSoundPlayer)在根,
//   界面(src/alarm/*Dialog)在 src/alarm/。别为了"放一起"破坏这个约定。
//
// ★ 权限
//   要 EditProject —— 和报警配置对话框同一条。改报警声音 = 改工程行为。
//
//   关联:alarm/AlarmConfigDialog.h, ../../AlarmSoundPlayer.h
// ============================================================================

#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QSpinBox;
class QTimeEdit;

class AlarmSoundPlayer;

enum class AlarmLevel;   // 见 ../../Alarm.h —— 只用到值,不依赖它的定义

namespace qwhmi::alarm {

class AlarmSoundDialog : public QDialog {
    Q_OBJECT
public:
    // player 不接管生命周期(主窗口持有)。传 nullptr 时对话框直接 disabled ——
    // 界面能打开却改不了任何东西,是最糟的一种"看起来能用"。
    explicit AlarmSoundDialog(AlarmSoundPlayer* player, QWidget* parent = nullptr);

    int exec() override;

    // ★ 给测试用的只读出口:读回来的就是"点确定后会写进 player 的那一份"。
    //   不暴露这个,测试就只能反射 private 成员,测的就成了实现细节。
    bool   masterEnabled() const;
    bool   levelEnabled(int index) const;
    bool   repeatEnabled() const;
    int    repeatIntervalMs() const;
    bool   quietEnabled() const;
    QString quietStartText() const;   // "HH:mm"
    QString quietEndText() const;

private:
    void loadFromPlayer();
    void saveToPlayer();
    // 试听时用哪一档:从重到轻取第一个开着的。
    // 没有这一档就退到 Warning —— 试听【必须永远有声音】,
    // 静音状态下点试听却一声不响,用户只会以为功能坏了。
    AlarmLevel highestEnabledLevel() const;

    AlarmSoundPlayer* player_ = nullptr;

    QCheckBox*  masterChk_    = nullptr;
    QCheckBox*  levelChk_[4]  = {nullptr, nullptr, nullptr, nullptr};
    QCheckBox*  repeatChk_    = nullptr;
    QSpinBox*   repeatSpin_   = nullptr;
    QCheckBox*  quietChk_     = nullptr;
    QTimeEdit*  quietStart_   = nullptr;
    QTimeEdit*  quietEnd_     = nullptr;
};

} // namespace qwhmi::alarm
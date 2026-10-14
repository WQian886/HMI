// ============================================================================
// 阶段 B · B2.2 · 自动注销(无操作计时)
//
// 文件职责:监听 QApplication 全局事件,5 分钟无操作 → 发 inactivityTimeout 信号。
//
// 实现:
//   - installTo(QObject*) 把 eventFilter 装到目标(QApplication 实例)
//   - 监听 MouseButtonPress / KeyPress / MouseMove / Wheel 等"操作"事件
//   - 每次操作重置 QTimer
//   - QTimer 到期 → emit inactivityTimeout(主流程收到后弹 LoginDialog 重新登录)
//
// 为什么不用 QAbstractNativeEventFilter:跨平台复杂,B2.2 阶段在 QEvent 层足够
//   (覆盖了所有键盘/鼠标事件,无操作自然不会触发)
//
// 关联:CurrentUser.h
// ============================================================================

#pragma once

#include <QObject>
#include <QTimer>

class QEvent;

namespace qwhmi::auth {

class InactivityWatcher : public QObject {
    Q_OBJECT
public:
    explicit InactivityWatcher(QObject* parent = nullptr);
    ~InactivityWatcher() override;

    // 装到目标(通常是 qApp)
    void installTo(QObject* target);

    // 启动(开始计时)
    void start();
    void stop();

    // 配置超时(默认 5 分钟)
    //
    // ★ BUGFIX(2026-10-14):原来是【假 setter】——
    //     void setTimeoutMs(int ms) { timeoutMs_ = ms; if (timer_->isActive()) start(); }
    //   它只改成员变量,从没调过 timer_->setInterval()。
    //   而 setInterval(timeoutMs_) 只在【构造函数】里调过一次,
    //   所以这个 setter 改成什么值,计时器实际走的都还是构造时的 5 分钟。
    //   那个 if (timer_->isActive()) start() 也只是【重新启动】,
    //   重启用的是 timer_ 上【现存的 interval】—— 还是 5 分钟。
    //
    //   【危害】不崩不报错,只是【安静地不生效】:
    //   调用者以为"超时改成 30 秒了",实际 5 分钟。
    //   这类 bug 最难查 —— 代码读起来完全合理,行为却不跟字面意思走。
    //
    //   【怎么发现的】做 --soak 时要给这个类补测试(它此前零覆盖),
    //   新写的 tst_InactivityWatcher 第一轮就有 5 条红,
    //   根因全指向这里。见该文件里 setTimeoutMs_actuallyChangesTheTimer 的注释。
    void setTimeoutMs(int ms) {
        timeoutMs_ = ms;
        // ★ 关键:必须同步到计时器。只改字段的话计时器完全不知情。
        if (timer_ != nullptr) timer_->setInterval(ms);
        if (timer_ != nullptr && timer_->isActive()) start();
    }
    int timeoutMs() const { return timeoutMs_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    // 到期发出 — 主流程收到后弹 LoginDialog
    void inactivityTimeout();

private:
    QTimer* timer_       = nullptr;
    int     timeoutMs_   = 5 * 60 * 1000;  // 5 分钟
    bool    installed_   = false;
};

} // namespace qwhmi::auth
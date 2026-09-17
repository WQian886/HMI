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
    void setTimeoutMs(int ms) { timeoutMs_ = ms; if (timer_->isActive()) start(); }
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
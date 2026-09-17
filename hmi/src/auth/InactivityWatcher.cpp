// ============================================================================
// 阶段 B · B2.2 · InactivityWatcher 实现
//
// 关联:InactivityWatcher.h
// ============================================================================

#include "auth/InactivityWatcher.h"

#include <QEvent>
#include <QTimer>

#include "logging/Logging.h"

namespace qwhmi::auth {

InactivityWatcher::InactivityWatcher(QObject* parent)
    : QObject(parent), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    timer_->setInterval(timeoutMs_);
    connect(timer_, &QTimer::timeout, this, [this]() {
        qCWarning(hmiApp) << "[InactivityWatcher] 超时无操作,emit inactivityTimeout";
        emit inactivityTimeout();
    });
}

InactivityWatcher::~InactivityWatcher() = default;

void InactivityWatcher::installTo(QObject* target) {
    if (target == nullptr) {
        qCWarning(hmiApp) << "[InactivityWatcher] installTo 失败:target=nullptr";
        return;
    }
    target->installEventFilter(this);
    installed_ = true;
    qCInfo(hmiApp) << "[InactivityWatcher] 装到" << target->metaObject()->className()
                   << "超时=" << timeoutMs_ << "ms";
}

void InactivityWatcher::start() {
    if (!installed_) {
        qCWarning(hmiApp) << "[InactivityWatcher] start 失败:未 installTo";
        return;
    }
    timer_->start();
}

void InactivityWatcher::stop() {
    timer_->stop();
}

bool InactivityWatcher::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:           // 注意:鼠标移动频繁会一直重置(可选屏蔽)
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Wheel:
        // 任一用户操作 → 重置 5 分钟倒计时
        if (timer_->isActive()) {
            timer_->start();   // 重启(等价于 reset)
        }
        break;
    default:
        break;
    }
    return false;   // 不吞事件
}

} // namespace qwhmi::auth
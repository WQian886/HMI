// ============================================================================
// 阶段 A · A2 单实例 · 头文件
//
// 文件职责:用 QSharedMemory 检测程序是否已在运行,实现"双击图标激活主实例"。
//
// 为什么需要:用户在任务栏双击图标、或外部程序调起第二份 → 不应同时
//   跑两个 HMI 实例。第二实例通过共享内存把命令行参数(尤其是 --project)
//   传给主实例,主实例收到后激活自己 + 打开参数指定的工程。
//
// 在程序中的位置:
//   - main() 启动早期:instance.tryBecomePrimary() → false 就退出(参数已传走)
//   - 主实例定时检查 shm(每 500ms)→ 收到消息发 messageReceived(args) 信号
//   - main.cpp 槽:收到 messageReceived → 窗口激活 + --project 打开工程
//
// 关键 API:tryBecomePrimary(key) / sendMessageToPrimary(args) / isPrimary()
// 信号:messageReceived(QStringList args) —— 主实例收到第二实例的命令
// 详见:docs/阶段A_工程化基线.md "A2 单实例" 节
// ============================================================================

#pragma once

#include <QObject>
#include <QStringList>

namespace qwhmi::instance {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    bool tryBecomePrimary(const QString& key = QStringLiteral("hmi-single-instance-v1"));
    bool isPrimary() const { return isPrimary_; }

    // 非主实例 → 给主实例发消息
    bool sendMessageToPrimary(const QStringList& args);

signals:
    void messageReceived(const QStringList& args);

private slots:
    void checkIncomingMessages();

private:
    class Impl;
    Impl* d_         = nullptr;
    bool  isPrimary_ = false;
};

} // namespace qwhmi::instance

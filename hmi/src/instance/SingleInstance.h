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

    // ★ 2026-10-21:默认键提升为【公开常量】
    //   原来它只是 tryBecomePrimary 的默认参数值,写在头文件里。
    //   现在 main.cpp 也要用它(当"用户没指定键"时的缺省值),
    //   两边各写一份字符串,迟早有一个忘了改 —— 而漏改的后果是
    //   "两个实例互相把对方当 sender",症状极难查。
    static const char* kDefaultKey;   // "hmi-single-instance-v1"

    // ★ 2026-10-21:实例键从【环境变量】HMI_INSTANCE_KEY 取。
    //   【为什么走环境变量,而不是命令行选项】
    //     单实例检测发生在 main() 的最前面 —— 比 QApplication 还早。
    //     而 QCommandLineParser::process() 需要一个 app 实例,
    //     于是"命令行选项"这条路上,单实例键【拿不到】。
    //     (要把解析器整体前移,就得把 parse() 挪到 QApplication 之前,
    //      那是改公共启动路径,风险比收益大。)
    //     环境变量没有这个次序问题:任何时候都能读。
    //   【安全边界】没设这个变量时,行为与改动前【完全一样】。
    //     它只是给"需要和已有实例共存"的场景(长跑)留一个出口,
    //     不是绕过单实例机制的后门:同一键的第二个实例照样被挡。
    static QString keyFromEnvironment();

    bool tryBecomePrimary(const QString& key = QString::fromLatin1(kDefaultKey));
    bool isPrimary() const { return isPrimary_; }

    // 非主实例 → 给主实例发消息
    bool sendMessageToPrimary(const QStringList& args);

    // ★ G-25:共享内存里的消息协议头(前面是它,后面才是载荷)。
    //   公开出来是为了让测试能读出"处理了几条消息"这类事实。
    struct MessageHeader {
        quint32 magic;
        quint32 version;
        quint32 seq;        // 发送方自增序号 —— "这条我处理过了吗"的唯一依据
        quint32 payloadLen; // 载荷字节数
    };

    // ★ G-25:协议常量
    static constexpr quint32 kShmMagic   = 0x484D494Du;  // 'HMIM'
    static constexpr quint32 kShmVersion = 1u;
    static constexpr int     kShmSize    = 1024;         // 共享内存字节数
    static constexpr int     kPollIntervalMs = 500;      // 轮询间隔

    // ★ G-25(2026-10-30):检查一次共享内存里有没有【新】消息。
    //
    //   【为什么从 private slot 挪到 public slot】
    //     这件事以前【只有定时器会调】,于是它只有一条"每 500ms 跑一遍"的
    //     路径,测试够不着 —— "一条消息只处理一次"这条根本没法钉。
    //     挪成 public slot 之后,测试可以【直接调】,不依赖事件循环,
    //     结果是确定的(定时器在测试里压根不转,因为没有事件循环)。
    //
    //   【它现在做什么】
    //     读共享内存头部 → 取出 seq。
    //     ★ seq 和上次处理过的一样 ⇒ 【直接跳过】——
    //       这就是 G-25 的全部修复。原来这里是"读完照发",
    //       同一份内容每 500ms 重放一次。
public slots:
    void checkIncomingMessages();

signals:
    void messageReceived(const QStringList& args);

private:

private:
    class Impl;
    Impl* d_         = nullptr;
    bool  isPrimary_ = false;
};

} // namespace qwhmi::instance

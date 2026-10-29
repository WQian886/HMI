// ============================================================================
// 阶段 A · A2 单实例 · 实现
//
// 文件职责:QSharedMemory 包装,实现"是否在跑"检测 + "消息传递"。
//
// 实现要点:
//   - tryBecomePrimary:attach 只读检测存在 → create 1024 字节 shm 成功则主实例
//   - sendMessageToPrimary:第二实例 attach 读写 → 序列化 args 写入 shm
//   - checkIncomingMessages:主实例 500ms lock shm → 读出 args → emit messageReceived
//
// 注意:进程退出时 shm 自动释放(QSharedMemory 析构),无需显式清理
//
// DIAG-2026-09-09(Wqian 反馈"运行不起来"排查):
//   [Fix A] tryBecomePrimary 加残留接管(taskkill /f 不调析构,shm 残留时仍能成为主实例)
//   [Fix B] sendMessageToPrimary 补 lock/unlock 配对(原代码没 lock 就 memcpy + unlock)
//
// 详见:SingleInstance.h 和 docs/阶段A_工程化基线.md
// ============================================================================

#include "instance/SingleInstance.h"
#include "logging/Logging.h"

#include <QSharedMemory>
#include <QTimer>
#include <QIODevice>
#include <QDataStream>
#include <cstring>

namespace qwhmi::instance {

// ★ 2026-10-21:见头文件里的说明 —— 默认键的【唯一出处】。
const char* SingleInstance::kDefaultKey = "hmi-single-instance-v1";

// ★ 2026-10-21:实例键从环境变量 HMI_INSTANCE_KEY 取。
//   没设 / 设成空 → 用默认键(行为与改动前完全一致)。
//   设了 → 用它。于是同一台机器上可以并存两个实例:
//   各自一把共享内存锁,谁也不抢谁。
QString SingleInstance::keyFromEnvironment() {
    const QByteArray v = qgetenv("HMI_INSTANCE_KEY");
    if (v.isEmpty()) {
        return QString::fromLatin1(kDefaultKey);
    }
    return QString::fromLocal8Bit(v);
}

class SingleInstance::Impl {
public:
    QString        key;
    QSharedMemory  shm;
};

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent), d_(new Impl) {}

SingleInstance::~SingleInstance() {
    delete d_;
}

bool SingleInstance::tryBecomePrimary(const QString& key) {
    d_->key = key;
    d_->shm.setKey(key);

    if (d_->shm.attach(QSharedMemory::ReadOnly)) {
        d_->shm.detach();
        // ★ DIAG-2026-09-09 [Fix A]:残留检测
        //   taskkill /f 强杀进程不调析构,QSharedMemory 不释放 → 下次启动误判"已有主实例"
        //   解决:attach ReadWrite + lock,成功说明真活主实例,失败说明残留,可接管
        QSharedMemory probe(d_->key);
        if (probe.attach(QSharedMemory::ReadWrite) && probe.lock()) {
            probe.unlock();
            probe.detach();
            qCInfo(hmiApp) << "[SingleInstance] 真有活主实例,本进程当 sender";
            isPrimary_ = false;
            return false;
        }
        // 残留:probe.detach 后再占用残留 shm 并释放(Windows 上需先 attach 才能 detach)
        if (probe.isAttached()) probe.detach();
        qCWarning(hmiApp) << "[SingleInstance] 检测到残留 shm(taskkill /f 强杀未析构),准备接管";
        if (d_->shm.attach(QSharedMemory::ReadWrite) && d_->shm.isAttached()) {
            d_->shm.detach();
        }
    }

    if (!d_->shm.create(1024)) {
        qWarning("SingleInstance: create failed: %s",
                 qUtf8Printable(d_->shm.errorString()));
        isPrimary_ = false;
        return false;
    }

    isPrimary_ = true;
    auto* t = new QTimer(this);
    t->setInterval(500);
    connect(t, &QTimer::timeout, this, &SingleInstance::checkIncomingMessages);
    qCInfo(hmiApp) << "[SingleInstance] timer created,parent=" << t->parent()
                   << "interval=" << t->interval();
    t->start();
    qCInfo(hmiApp) << "[SingleInstance] timer 已启动";
    return true;
}

bool SingleInstance::sendMessageToPrimary(const QStringList& args) {
    if (isPrimary_) return false;
    QSharedMemory shm(d_->key);
    if (!shm.attach(QSharedMemory::ReadWrite)) {
        qCWarning(hmiApp) << "[SingleInstance] sendMessage: attach 失败:"
                          << shm.errorString();
        return false;
    }
    // ★ DIAG-2026-09-09 [Fix B]:补 lock/unlock 配对
    //   原代码直接 memcpy + unlock + detach,违反 QSharedMemory 协议
    //   可能导致主实例 checkIncoming 拿到半写数据,反序列化崩
    if (!shm.lock()) {
        qCWarning(hmiApp) << "[SingleInstance] sendMessage: lock 失败";
        shm.detach();
        return false;
    }
    qCInfo(hmiApp) << "[SingleInstance] sendMessage: attach+lock 成功,开始写 args"
                   << args.size() << "项:" << args;

    QByteArray buf;
    {
        QDataStream out(&buf, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << args;
    }
    if (buf.size() > shm.size()) {
        qCWarning(hmiApp) << "[SingleInstance] sendMessage: 数据太大" << buf.size() << ">" << shm.size();
        shm.unlock();
        shm.detach();
        return false;
    }
    std::memcpy(shm.data(), buf.constData(), static_cast<size_t>(buf.size()));
    shm.unlock();
    shm.detach();
    qCInfo(hmiApp) << "[SingleInstance] sendMessage: 写完" << buf.size() << "字节,unlock+detach";
    return true;
}

void SingleInstance::checkIncomingMessages() {
    static int callCount = 0;
    qCInfo(hmiApp) << "[SingleInstance] checkIncomingMessages CALLED, count=" << ++callCount;
    //   因为上次测试 checkIncoming 完全不出现,加这个保底日志
    static int tick = 0;
    if (++tick % 10 == 1) {
        qCInfo(hmiApp) << "[SingleInstance] TICK" << tick
                       << "isPrimary=" << isPrimary_
                       << "isAttached=" << d_->shm.isAttached();
    }

    if (!isPrimary_ || !d_->shm.isAttached()) {
        return;
    }
    if (!d_->shm.lock()) {
        // 没拿到锁,可能 secondary 正在写,等下一轮
        d_->shm.unlock();
        return;
    }
    const QByteArray data(static_cast<const char*>(d_->shm.constData()),
                          static_cast<int>(d_->shm.size()));
    d_->shm.unlock();
    qCInfo(hmiApp) << "[SingleInstance] checkIncoming: read" << data.size() << "字节";
    if (data.isEmpty()) return;
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    QStringList args;
    in >> args;
    qCInfo(hmiApp) << "[SingleInstance] checkIncoming: 解析到" << args.size() << "项:"
                   << args;
    if (!args.isEmpty()) {
        qCInfo(hmiApp) << "[SingleInstance] emit messageReceived";
        emit messageReceived(args);
    }
}

} // namespace qwhmi::instance
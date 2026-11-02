// ============================================================================
// 阶段 A · A2 单实例 · 实现
//
// 文件职责:QSharedMemory 包装,实现"是否在跑"检测 + "消息传递"。
//
// 实现要点:
//   - tryBecomePrimary:attach 只读检测存在 → create 1024 字节 shm 成功则主实例
//   - sendMessageToPrimary:第二实例 attach 读写 → 写"协议头 + 载荷"进 shm
//   - checkIncomingMessages:主实例 500ms lock shm → 看序号 → 新消息才 emit
//
// ★ G-25(2026-10-30):共享内存里多了一个协议头。
//
//   【原来】shm 里就是一段 QDataStream 序列化出来的 QStringList。
//         发送方写进去,主实例每 500ms 读一遍 —— 【读完不清零、不记已读】。
//         ⇒ 同一条消息被【无限重放】:每 500ms 重新 emit 一次
//           messageReceived ⇒ main.cpp 那个槽每 500ms 把窗口拉到前台一次
//           (操作员点别的窗口会被抢回去)+ 日志每 tick 6~8 行 ≈ 60 万行/天。
//
//   【现在】shm = [协议头 magic|version|seq|payloadLen][载荷]
//         发送方在锁内把 seq 加 1 再写;主实例记住"我处理到第几号了",
//         seq 没变 ⇒ 跳过。⇒ 同一条消息只处理一次。
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
    // ★ G-25:上次处理过的消息序号。
    //   共享内存里的内容【不会被清掉】(发送方写、主实例读,两边都不擦),
    //   所以"这条是不是新的"只能靠序号判断,不能靠"有没有读过"。
    quint32        lastSeq = 0;
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

    if (!d_->shm.create(kShmSize)) {
        qWarning("SingleInstance: create failed: %s",
                 qUtf8Printable(d_->shm.errorString()));
        isPrimary_ = false;
        return false;
    }

    isPrimary_ = true;

    // ★ G-25:把共享内存的头部清干净。
    //   【为什么】上一任主实例被强杀(taskkill /f)时,shm 里可能还留着
    //   一条旧消息。新主实例一上来 lastSeq=0,看到那条旧消息(seq≠0)
    //   就会当成"新消息"处理 ⇒ 窗口莫名其妙被拉到前台一次。
    //   自己当上主实例的那一刻先把头部写成"空的、seq=0",这条就断了。
    if (d_->shm.lock()) {
        MessageHeader h{kShmMagic, kShmVersion, 0, 0};
        std::memcpy(d_->shm.data(), &h, sizeof(h));
        d_->shm.unlock();
    }

    auto* t = new QTimer(this);
    t->setInterval(kPollIntervalMs);
    connect(t, &QTimer::timeout, this, &SingleInstance::checkIncomingMessages);
    qCDebug(hmiApp) << "[SingleInstance] timer created,parent=" << t->parent()
                    << "interval=" << t->interval();
    t->start();
    qCInfo(hmiApp) << "[SingleInstance] 已当上主实例,开始轮询共享内存,间隔"
                   << kPollIntervalMs << "ms";
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
    qCDebug(hmiApp) << "[SingleInstance] sendMessage: attach+lock 成功,开始写 args"
                    << args.size() << "项:" << args;

    QByteArray buf;
    {
        QDataStream out(&buf, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << args;
    }

    // ★ G-25:载荷前面加一个协议头(序号在头里)。
    const qint64 headerBytes = qint64(sizeof(MessageHeader));
    if (qint64(buf.size()) + headerBytes > shm.size()) {
        qCWarning(hmiApp) << "[SingleInstance] sendMessage: 数据太大" << buf.size()
                          << "+" << headerBytes << ">" << shm.size();
        shm.unlock();
        shm.detach();
        return false;
    }

    // ★ G-25:读出当前头部,序号 +1。
    //   【为什么序号在发送方算,不在主实例算】
    //     主实例算的话,主实例重启就归零 —— 而 shm 里的旧内容还在,
    //     新主实例会把那条旧消息当成"新的"再处理一遍。
    //     发送方算 + 主实例记住"我处理到第几号了",这条链才是闭合的。
    MessageHeader h{};
    std::memcpy(&h, shm.constData(), sizeof(h));
    if (h.magic != kShmMagic || h.version != kShmVersion) {
        // 头部不认识(第一次写,或者对端是旧版本)⇒ 从头开始
        h.magic   = kShmMagic;
        h.version = kShmVersion;
        h.seq     = 0;
    }
    h.seq += 1;
    h.payloadLen = quint32(buf.size());

    std::memcpy(shm.data(), &h, sizeof(h));
    std::memcpy(static_cast<char*>(shm.data()) + headerBytes,
                buf.constData(), static_cast<size_t>(buf.size()));
    shm.unlock();
    shm.detach();
    qCDebug(hmiApp) << "[SingleInstance] sendMessage: 写完 seq=" << h.seq
                    << "," << buf.size() << "字节,unlock+detach";
    return true;
}

// ★ G-25:检查共享内存里有没有【新】消息。
//
//   【原来这里是什么】
//     每 500ms 把 shm 整个读一遍、反序列化、然后 emit ——
//     【读完什么都不记】。于是同一条消息被【无限重放】:
//       · main.cpp 那个槽每 500ms 把窗口拉到前台一次
//         (操作员点别的窗口会被抢回去)
//       · 每 tick 打 6~8 行日志 ⇒ 一天约 60 万行
//
//   【现在这里是什么】
//     先读协议头里的 seq。seq 和上次处理过的一样 ⇒ 直接跳过。
//     只有【新的】消息(seq 变了)才反序列化、才 emit。
void SingleInstance::checkIncomingMessages() {
    if (!isPrimary_ || !d_->shm.isAttached()) {
        return;
    }
    if (!d_->shm.lock()) {
        // 没拿到锁,可能 secondary 正在写,等下一轮
        d_->shm.unlock();
        return;
    }

    MessageHeader h{};
    std::memcpy(&h, d_->shm.constData(), sizeof(h));
    const qint64 headerBytes = qint64(sizeof(MessageHeader));

    // 头部不认识 / 没有载荷 ⇒ 没有消息
    if (h.magic != kShmMagic || h.version != kShmVersion || h.payloadLen == 0) {
        d_->shm.unlock();
        return;
    }

    // ★ 这就是 G-25 的修复点:同一条消息不处理第二遍。
    if (h.seq == d_->lastSeq) {
        d_->shm.unlock();
        qCDebug(hmiApp) << "[SingleInstance] seq=" << h.seq << " 已处理过,跳过";
        return;
    }

    // 载荷长度对不上 ⇒ 当成坏数据,记下序号(不再重试),跳过
    if (headerBytes + qint64(h.payloadLen) > d_->shm.size()) {
        qCWarning(hmiApp) << "[SingleInstance] 载荷长度不对:" << h.payloadLen
                          << ",共享内存只有" << d_->shm.size() << "字节";
        d_->lastSeq = h.seq;
        d_->shm.unlock();
        return;
    }

    const QByteArray payload(
        static_cast<const char*>(d_->shm.constData()) + headerBytes,
        static_cast<int>(h.payloadLen));
    // ★ 先记序号,再处理 —— 就算反序列化失败,也不该下一轮再来一遍。
    d_->lastSeq = h.seq;
    d_->shm.unlock();

    QDataStream in(payload);
    in.setVersion(QDataStream::Qt_6_0);
    QStringList args;
    in >> args;
    if (args.isEmpty()) {
        qCDebug(hmiApp) << "[SingleInstance] 消息是空的,忽略";
        return;
    }
    qCInfo(hmiApp) << "[SingleInstance] 收到第二实例的消息(" << args.size()
                   << "项):" << args;
    emit messageReceived(args);
}

} // namespace qwhmi::instance
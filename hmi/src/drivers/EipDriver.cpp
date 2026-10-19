// ============================================================================
// 阶段 C · C.3 · EIP(EtherNet/IP)协议 Driver 实现(libplctag LGPL 包装层)
//
// 当前状态(C.3.1 骨架,2026-09-18):
//   - 所有 IPollDriver 接口已最小实现(返回 false / 0),后续 C.3.2-C.3.4 填充
//   - libplctag DLL 通过 QLibrary::load 动态加载;失败时不崩
//   - 异步转同步的 QEventLoop 模式骨架就位,等待 plc_tag_status 实现
//
// 设计要点(对应教训 + 规划 C.3 节):
//   - QLibrary 动态加载:LGPL 合规硬要求(同 Snap7,规划 C.2 第 449-450 行)
//   - 异步 → 同步:QEventLoop + QTimer 单次 + poll plc_tag_status(规划 C.3 第 307 行 + 5.3 节)
//   - 1 driver = 1 PLC(教训 1.5)
//   - 错误处理:libplctag 状态码 → IPollDriver::errorOccurred
//
// 后续任务:
//   - C.3.2:DriverFactory 加 eip 分支
//   - C.3.3:写 tst_EipDriver(Mock libplctag 验证异步轮询)
//   - C.3.4:真连接 Logix Designer(待仿真器到位)
//   - C.6a:真机技术验证
//
// 关联:EipDriver.h, IPollDriver.h, S7Driver.cpp(对照风格)
// ============================================================================

#include "drivers/EipDriver.h"

#include <QDebug>
#include <QEventLoop>
#include <QTimer>

namespace qwhmi::drivers {

// libplctag 状态码(规划 C.3 节常用值,C.3.4 真接时全部映射)
static constexpr int PLCTAG_STATUS_OK           = 0;
static constexpr int PLCTAG_STATUS_PENDING      = 1;
static constexpr int PLCTAG_STATUS_TIMEOUT      = -1;
static constexpr int PLCTAG_STATUS_ERROR        = -2;

// ============================================================================
// 构造 / 析构
// ============================================================================

EipDriver::EipDriver(QObject* parent)
    : IPollDriver(parent)
{
    // C.3.4 TODO: 构造时尝试 loadLibplctagLibrary()
    //   失败不应崩,留给 connectTo 再试
}

EipDriver::~EipDriver()
{
    if (tag_ != 0 && pPlcTagDestroy) {
        pPlcTagDestroy(tag_);
        tag_ = 0;
    }
    unloadLibplctagLibrary();
}

// ============================================================================
// IPollDriver 接口 - C.3.1 骨架(后续小步填充)
// ============================================================================

void EipDriver::connectTo(const QString& host, quint16 port)
{
    host_ = host;
    port_ = port;

    // C.3.4 TODO: 真 libplctag 连接逻辑
    //   1. 加载 DLL(如果还没)
    //   2. pPlcTagSetIntAttr(tag, "ip", host.toLocal8Bit())  — 但要先创建 tag
    //      实际 libplctag 用 attribute 字符串传入:plc_tag_create("protocol=ab-eip&gateway=...&path=...&name=...", 0, "tagname")
    //   3. pPlcTagCreate(...) — 启动 CIP 连接(异步)
    //   4. waitForTagCompletion → emit connected()
    qWarning() << "[EipDriver] connectTo C.3.1 stub:" << host << ":" << port
               << "(真连接在 C.3.4 实现)";
    emit errorOccurred(QStringLiteral("EipDriver.connectTo: C.3.1 stub - 真连接在 C.3.4 实现"));
}

void EipDriver::disconnect()
{
    if (tag_ != 0 && pPlcTagAbort) {
        pPlcTagAbort(tag_);
        if (pPlcTagDestroy) {
            pPlcTagDestroy(tag_);
        }
        tag_ = 0;
    }
    emit disconnected();
}

bool EipDriver::isConnected() const
{
    if (tag_ == 0 || !pPlcTagStatus) return false;
    return pPlcTagStatus(tag_) == PLCTAG_STATUS_OK;
}

int EipDriver::pendingCount() const
{
    // libplctag 单 driver 单 in-flight(简化模型,真场景可支持多 tag)
    return 0;
}

// ★ 2b(2026-10-19):返回 bool。stub 会立刻 emit readFinished(false),
//   即"接收了请求、失败通过信号报",所以返回 true。理由同 S7Driver。
bool EipDriver::sendReadRequest(const ReadRequest& req)
{
    // C.3.4 TODO: 真 libplctag 读逻辑
    //   1. tag_ = pPlcTagCreate(tagName, 0, "protocol=ab-eip&...")  — 第一次创建
    //      后续复用 tag_,只 plc_tag_read
    //   2. pPlcTagRead(tag_, 0)  — 启动异步读
    //   3. waitForTagCompletion → 读 buffer
    //   4. emit readFinished(...)
    Q_UNUSED(req);
    qWarning() << "[EipDriver] sendReadRequest C.3.1 stub";
    emit readFinished(req.requestId, QVector<quint16>{}, false);
    return true;
}

bool EipDriver::sendWriteRequest(RegisterArea area, int slaveId, int addr,
                                 const QVector<quint16>& values, quint32 requestId)
{
    // C.3.4 TODO: 真 libplctag 写逻辑
    //   1. tag_ = pPlcTagCreate(...) — 写前必须先创建
    //   2. 写入 buffer(用 plc_tag_set_int_attribute 或直接 memcpy tag 内部 buffer)
    //      实际:plc_tag_set_byte_array(tag, 0, data, size) 或
    //           通过 tag 内部指针写,然后 plc_tag_write
    //   3. pPlcTagWrite(tag_, 0) — 启动异步写
    //   4. waitForTagCompletion → emit writeFinished(...)
    Q_UNUSED(area);
    Q_UNUSED(slaveId);
    Q_UNUSED(addr);
    Q_UNUSED(values);
    qWarning() << "[EipDriver] sendWriteRequest C.3.1 stub";
    emit writeFinished(requestId, false);
    return false;
}

// ============================================================================
// libplctag 库加载/卸载(C.3.1 占位,C.3.4 完善)
// ============================================================================

bool EipDriver::loadLibplctagLibrary()
{
    // C.3.4 TODO: 实际 QLibrary::resolve 函数指针
    //   1. libplctagLib_.setFileName("libplctag") 或 "libplctag.dll"
    //   2. libplctagLib_.load()
    //   3. resolve 8 个函数指针(plc_tag_create/destroy/read/write/status/get_int_attr/set_int_attr/abort)
    //   4. 任一失败 → emit errorOccurred + return false
    qWarning() << "[EipDriver] loadLibplctagLibrary C.3.1 stub";
    return false;
}

void EipDriver::unloadLibplctagLibrary()
{
    if (libplctagLib_.isLoaded()) {
        libplctagLib_.unload();
    }
    pPlcTagCreate      = nullptr;
    pPlcTagDestroy     = nullptr;
    pPlcTagRead        = nullptr;
    pPlcTagWrite       = nullptr;
    pPlcTagStatus      = nullptr;
    pPlcTagGetIntAttr  = nullptr;
    pPlcTagSetIntAttr  = nullptr;
    pPlcTagAbort       = nullptr;
}

// ============================================================================
// 辅助方法
// ============================================================================

QString EipDriver::buildTagName(RegisterArea area, int slaveId, int addr)
{
    // 简化映射(C.3.4 真接 Logix 时细化):
    //   - slaveId 当 file number(AB 风格,如 N7 = integer file)
    //   - addr 当 element number
    //   - 返回 "N{slaveId}:{addr}" — MicroLogix/SLC500 风格
    // CompactLogix/ControlLogix 用 tag_name 字符串(如 "MyTag[3]")
    // 骨架阶段用 N7:0 风格(简单,测试友好)
    switch (area) {
        case RegisterArea::HoldingReg:
        case RegisterArea::InputReg:
            return QStringLiteral("N%1:%2").arg(slaveId).arg(addr);
        case RegisterArea::Coils:
        case RegisterArea::DiscreteInput:
            return QStringLiteral("B%1:%2").arg(slaveId).arg(addr);  // Boolean file
    }
    return QStringLiteral("N7:0");
}

bool EipDriver::waitForTagCompletion(int timeoutMs)
{
    // QEventLoop + QTimer 单次 + poll plc_tag_status(规划 C.3 第 5.3 节)
    // C.3.4 真接时实现
    Q_UNUSED(timeoutMs);
    if (!pPlcTagStatus) return false;

    QEventLoop loop;
    QTimer::singleShot(timeoutMs_, &loop, &QEventLoop::quit);
    QTimer pollTimer;
    pollTimer.setInterval(10);  // 10ms poll interval

    QObject::connect(&pollTimer, &QTimer::timeout, &loop, [this, &loop]() {
        if (!pPlcTagStatus) {
            loop.quit();
            return;
        }
        const int status = pPlcTagStatus(tag_);
        if (status != PLCTAG_STATUS_PENDING) {
            loop.quit();
        }
    });
    pollTimer.start();
    loop.exec();
    pollTimer.stop();

    return pPlcTagStatus(tag_) == PLCTAG_STATUS_OK;
}

} // namespace qwhmi::drivers

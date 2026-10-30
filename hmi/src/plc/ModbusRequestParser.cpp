// ============================================================================
// ModbusRequestParser 实现
//
// 头文件里有完整的问题说明(G-20)。这里只讲实现上的几个选择。
//
//  【为什么用 strict int 而不是 QString::toInt 默认行为】
//    toInt(&ok) 会接受 "+12"、前后空格,也会在失败时【返回 0】——
//    "失败返回 0"正是原缺陷的要害:失败和"填了 0"分不开。
//    本模块一律把"解析失败"和"解析出 0"分成两条不同的结果。
//
//  【为什么值列表保留 0x 前缀,而从站号/地址不保留】
//    值列表("0xFF,0x00")是调试人员手工抄从站里的十六进制,保留这个习惯。
//    从站号/地址在面板上就是十进制寄存器号,两种写法混着来只会让人猜。
//    —— 保守原则:【不改现有可用行为】,只把会写坏设备的那条路堵上。
// ============================================================================

#include "plc/ModbusRequestParser.h"

namespace qwhmi::plc {

namespace {

// ★ 严格十进制整数。失败返回 false(【不】返回 0)。
bool parseStrictInt(const QString& text, int* out) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    const int v = t.toInt(&ok, 10);
    if (!ok) return false;
    if (out) *out = v;
    return true;
}

// ★ 位类型(线圈/离散输入)的数量上限比寄存器大得多。
bool isBitKind(RegisterKind k) {
    return k == RegisterKind::Coils || k == RegisterKind::DiscreteInputs;
}

// ★ Modbus 协议上限(标准里的硬限制,不是本项目的偏好)
constexpr int kMaxRegRead   = 125;    // FC03/FC04
constexpr int kMaxRegWrite  = 123;    // FC16(比读少 2:要带字节计数)
constexpr int kMaxBitRead   = 2000;   // FC01/FC02
constexpr int kMaxBitWrite  = 1968;   // FC15

void setError(QString* errorReason, const QString& msg) {
    if (errorReason) *errorReason = msg;
}

} // namespace

// ---------------------------------------------------------------
QVector<quint16> parseValueList(const QString& text, QString* errOut) {
    QVector<quint16> result;
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        QString t = p.trimmed();
        if (t.isEmpty()) continue;
        bool ok = false;
        quint16 v = 0;
        if (t.startsWith("0x", Qt::CaseInsensitive)) {
            v = t.toUShort(&ok, 16);  // 0x 前缀走十六进制
        } else {
            v = t.toUShort(&ok, 10);  // 默认十进制
        }
        if (!ok) {
            if (errOut) *errOut = QString("非法值: '%1'(需 0-65535 或 0x 前缀)").arg(t);
            return {};
        }
        result.append(v);
    }
    if (result.isEmpty() && errOut) *errOut = "值列表为空";
    return result;
}

// ---------------------------------------------------------------
bool validateSlaveId(const QString& slaveText, int* outSlaveId, QString* errorReason) {
    int slaveId = 0;
    if (!parseStrictInt(slaveText, &slaveId)) {
        setError(errorReason,
                 slaveText.trimmed().isEmpty()
                     ? QStringLiteral("从站号不能为空")
                     : QStringLiteral("从站号必须是十进制整数(现在填的是「%1」)")
                           .arg(slaveText.trimmed()));
        return false;
    }
    // ★★ G-20:0 是广播地址 —— 写下去整条总线上的设备都会被写。★★
    //   这一条原来被 "0-255" 的范围检查放行了。
    if (slaveId == 0) {
        setError(errorReason,
                 QStringLiteral("从站号 0 是【广播地址】—— 写下去整条总线上的设备都会被写,"
                                "已拒绝。请填具体从站号(1-247)。"));
        return false;
    }
    if (slaveId < 0) {
        setError(errorReason, QStringLiteral("从站号不能为负(现在填的是 %1)").arg(slaveId));
        return false;
    }
    if (slaveId > 247) {
        setError(errorReason,
                 QStringLiteral("从站号必须在 1-247(248-255 是保留地址,现在填的是 %1)").arg(slaveId));
        return false;
    }
    if (outSlaveId) *outSlaveId = slaveId;
    return true;
}

// ---------------------------------------------------------------
bool parseRequestSpec(const QString& slaveText,
                       const QString& addrText,
                       const QString& dataText,
                       bool isRead,
                       RegisterKind kind,
                       RequestSpec* out,
                       QString* errorReason) {
    // ---- ① 从站号 ----
    int slaveId = 0;
    if (!validateSlaveId(slaveText, &slaveId, errorReason)) {
        return false;
    }

    // ---- ② 地址 ----
    int address = 0;
    if (!parseStrictInt(addrText, &address)) {
        setError(errorReason,
                 addrText.trimmed().isEmpty()
                     ? QStringLiteral("地址不能为空")
                     : QStringLiteral("地址必须是十进制整数(现在填的是「%1」)")
                           .arg(addrText.trimmed()));
        return false;
    }
    if (address < 0) {
        setError(errorReason, QStringLiteral("地址不能为负(现在填的是 %1)").arg(address));
        return false;
    }
    if (address > 65535) {
        setError(errorReason, QStringLiteral("地址必须在 0-65535(现在填的是 %1)").arg(address));
        return false;
    }

    RequestSpec spec;
    spec.slaveId = slaveId;
    spec.address = address;
    spec.isRead  = isRead;
    spec.kind    = kind;

    if (isRead) {
        // ---- ③ 读:数量 ----
        int count = 0;
        if (!parseStrictInt(dataText, &count)) {
            setError(errorReason,
                     dataText.trimmed().isEmpty()
                         ? QStringLiteral("读数量不能为空")
                         : QStringLiteral("读数量必须是十进制整数(现在填的是「%1」)")
                               .arg(dataText.trimmed()));
            return false;
        }
        const int maxCount = isBitKind(kind) ? kMaxBitRead : kMaxRegRead;
        if (count < 1 || count > maxCount) {
            setError(errorReason,
                     QStringLiteral("读数量必须在 1-%1(现在填的是 %2)").arg(maxCount).arg(count));
            return false;
        }
        // 地址 + 数量不能越出 65536 的地址空间
        if (address + count > 65536) {
            setError(errorReason,
                     QStringLiteral("地址 %1 + 读 %2 个 超出了 65536 的地址空间")
                         .arg(address).arg(count));
            return false;
        }
        spec.count = count;
    } else {
        // ---- ④ 写:值列表 ----
        QString err;
        const QVector<quint16> vals = parseValueList(dataText, &err);
        if (vals.isEmpty()) {
            setError(errorReason, err.isEmpty() ? QStringLiteral("写值不能为空") : err);
            return false;
        }
        const int maxCount = isBitKind(kind) ? kMaxBitWrite : kMaxRegWrite;
        if (vals.size() > maxCount) {
            setError(errorReason,
                     QStringLiteral("一次最多写 %1 个(现在填了 %2 个)").arg(maxCount).arg(vals.size()));
            return false;
        }
        if (address + vals.size() > 65536) {
            setError(errorReason,
                     QStringLiteral("地址 %1 + 写 %2 个 超出了 65536 的地址空间")
                         .arg(address).arg(vals.size()));
            return false;
        }
        spec.values = vals;
        spec.count  = vals.size();
    }

    if (out) *out = spec;
    return true;
}

} // namespace qwhmi::plc

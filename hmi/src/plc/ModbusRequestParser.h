// ============================================================================
// ModbusRequestParser —— 调试面板那一行输入的【唯一解析处】
//
// ============================================================================
//  【G-20(2026-10-30):这条模块是为了修一个【会写坏现场设备】的缺陷】
//
//    原来的代码在 hmi.cpp 里(点"发送"按钮那个 lambda):
//
//        int slaveId = ui.modbusSlaveEdit->text().toInt(&ok);
//        int addr    = ui.modbusAddrEdit->text().toInt(&ok);   // ★ ok 被覆盖了
//        if (!ok || slaveId < 0 || slaveId > 255) { ... return; }
//
//    ★ 两个 toInt 共用同一个 ok ★
//      第二次调用把第一次的结果【整个盖掉】。
//      于是:从站号那一栏填了垃圾(比如 "ok"、字母、空),
//      地址那一栏填的是对的 ⇒ ok 变成 true ⇒ 检查通过
//      ⇒ 而 toInt 失败时返回的是 0 ⇒ slaveId = 0
//      ⇒ ★ 0 是 Modbus 的【广播地址】★
//      ⇒ 写请求会发给【总线上的每一台设备】。
//
//    换句话说:操作员在"从站号"栏里手滑打错一个字符,
//    现场那一整条总线上的设备都会被写一遍。
//    而界面上只会显示"写成功"。
//
//    另外两处同源的洞:
//      · 从站号 0 本来就被 0-255 的检查【放行】了 —— 0 就是广播。
//      · executePendingModbus() 在"连上了再发"那条路径上【重新读了一遍 UI 文本】,
//        没有校验 ⇒ 用户在"点发送"和"连上"之间把那一栏改掉,就又是一条广播写。
//
//  【为什么抽成共享模块】
//    hmi.cpp 【不在 hmi_core 里】⇒ 写在它里面的逻辑测试只能"抄一份跑"——
//    测的是副本,生产代码改了测试照样绿(这是本仓库反复踩的坑)。
//    抽出来之后,测试调的是【生产用的同一个函数】。
//
//  【本模块的规矩】
//    ① 只认【整数字面量】:带空格、十六进制前缀、负号都按"不是数字"处理
//      (Modbus 调试面板的语义就是十进制寄存器号,别猜)。
//    ② 从站号 1-247。★ 0 = 广播,248-255 = 保留 ★ 两者都拒。
//    ③ 地址 0-65535。
//    ④ 读数量 / 写值个数按 Modbus 协议上限:
//      寄存器 读 1-125 / 写 1-123;位 读 1-2000 / 写 1-1968。
//    ⑤ 任何一个字段不合法 ⇒ 【整份请求都不发】,并给出【能照着改】的中文原因。
//
//  【负向对照】
//    见 tst_ModbusRequestParser.cpp:把"从站号 0 放行"改回去,那个用例会立刻红。
//
//  关联:hmi.cpp(调试面板发送按钮), tst_ModbusRequestParser.cpp
// ============================================================================

#pragma once

#include <QString>
#include <QVector>

namespace qwhmi::plc {

// ★ 寄存器类型。跟 UI 下拉框的顺序一一对应(下拉框索引 0=保持寄存器 …)。
//   这里【不】用 QModbusDataUnit::RegisterType,是为了让本模块只依赖 QtCore ——
//   它是纯解析逻辑,拖上 SerialBus 只会让"能不能测"这件事变复杂。
enum class RegisterKind {
    HoldingRegisters = 0,   // 保持寄存器
    InputRegisters   = 1,   // 输入寄存器
    Coils            = 2,   // 线圈
    DiscreteInputs   = 3,   // 离散输入
};

// ★ 一次请求的完整描述。
//   ★ 只有 parseRequestSpec() 验过之后的 RequestSpec 才允许拿去发 ★
//     ——手搓一个 RequestSpec 绕开校验,就是绕开上面那三条洞。
struct RequestSpec {
    int          slaveId = 0;
    int          address = 0;
    int          count   = 1;      // 读时用
    bool         isRead  = true;
    RegisterKind kind    = RegisterKind::HoldingRegisters;
    QVector<quint16> values;       // 写时用
};

// ---------------------------------------------------------------
// 解析 "1,2,3" / "0xFF,0x00" 这种逗号分隔 uint16 列表。
// 失败:errOut 写错误信息(用于状态栏),返回值是空 list。
// ---------------------------------------------------------------
QVector<quint16> parseValueList(const QString& text, QString* errOut = nullptr);

// ---------------------------------------------------------------
// 校验从站号。
//   ★ 0 是广播地址 —— 写下去整条总线上的设备都会写。必须拒。★
// 返回 false 时 errorReason 里是【能照着改】的中文原因。
// ---------------------------------------------------------------
bool validateSlaveId(const QString& slaveText, int* outSlaveId, QString* errorReason);

// ---------------------------------------------------------------
// 解析整份请求(从站号 + 地址 + 数量/写值)。
//   isRead = true  → dataText 是"读几个"(十进制整数)
//   isRead = false → dataText 是"写什么值"(逗号分隔的 uint16 列表)
// 返回 false 时【一个字段都没通过】,out 不许拿去发。
// ---------------------------------------------------------------
bool parseRequestSpec(const QString& slaveText,
                       const QString& addrText,
                       const QString& dataText,
                       bool isRead,
                       RegisterKind kind,
                       RequestSpec* out,
                       QString* errorReason);

} // namespace qwhmi::plc
